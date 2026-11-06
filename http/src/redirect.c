#include "http/src/internal.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

bool http_is_redirect_status(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

/* ---- path model: the concatenation a+b, always starting with '/' ----------------------------------- */

typedef struct {
    core_str_t a, b;
} merged_t;

static size_t m_len(const merged_t *m) {
    return m->a.len + m->b.len;
}

static char m_at(const merged_t *m, size_t i) {
    return i < m->a.len ? m->a.ptr[i] : m->b.ptr[i - m->a.len];
}

static size_t seg_end(const merged_t *m, size_t s) {
    size_t e = s;
    size_t len = m_len(m);
    while (e < len && m_at(m, e) != '/') {
        e++;
    }
    return e;
}

/* 0 = normal segment, 1 = ".", 2 = ".." */
static int seg_kind(const merged_t *m, size_t s, size_t e) {
    if (e - s == 1 && m_at(m, s) == '.') {
        return 1;
    }
    if (e - s == 2 && m_at(m, s) == '.' && m_at(m, s + 1) == '.') {
        return 2;
    }
    return 0;
}

/* ---- dot-segment removal in O(n log n) time and O(log n) memory -------------------------------------------
 *
 * The path is a sequence of items "/seg" (one per '/'); "." is dropped, ".." cancels the nearest earlier kept
 * normal item (never above the root), every other item (empty ones too) is kept unless cancelled. Looking
 * from the right, a ".." raises a counter and a normal item either lowers it (cancelled) or, at 0, is kept.
 * A block of the path acts on that counter as s -> max(s - x, 0) + y, where x is its number of normal items
 * not cancelled inside it and y its number of ".." that cancel nothing inside it. The path is cut into leaf
 * blocks of LEAF_CHARS characters; the counter entering each leaf is found by a walk over the implicit binary
 * tree of blocks, with one saved counter per tree level (LEVELS_MAX). Each leaf is then emitted left to right.
 * Memory: a fixed bitset per leaf plus LEVELS_MAX counters; no allocation, no recursion. */

#define LEAF_CHARS 512u
#define LEVELS_MAX 38u /* LEAF_CHARS << LEVELS_MAX = 2^47 bytes: more than any address space can hold */

static bool is_slash(const merged_t *m, size_t i) {
    return m_at(m, i) == '/';
}

static void put_range(core_buf_t *out, const merged_t *m, size_t s, size_t e) {
    size_t i;
    for (i = s; i < e; i++) {
        core_buf_put_char(out, m_at(m, i));
    }
}

/* (x, y) of the items whose '/' lies in [lo, hi): a forward matching of normal items against "..". */
static void block_xy(const merged_t *m, size_t lo, size_t hi, size_t *x, size_t *y) {
    size_t i = lo;
    *x = 0;
    *y = 0;
    while (i < hi) {
        size_t e;
        int kind;
        if (!is_slash(m, i)) {
            i++;
            continue;
        }
        e = seg_end(m, i + 1);
        kind = seg_kind(m, i + 1, e);
        if (kind == 0) {
            (*x)++;
        } else if (kind == 2 && *x > 0) {
            (*x)--;
        } else if (kind == 2) {
            (*y)++;
        }
        i = e;
    }
}

/* Marks (bit j = j-th item from the right) the kept items of the leaf [lo, hi), given the counter s on its
 * right. Returns the number of items. */
static size_t leaf_marks(const merged_t *m, size_t lo, size_t hi, size_t s, unsigned char *bits) {
    size_t e = seg_end(m, hi);
    size_t k = 0;
    size_t i;
    for (i = hi; i > lo; i--) {
        int kind;
        if (!is_slash(m, i - 1)) {
            continue;
        }
        kind = seg_kind(m, i, e);
        if (kind == 2) {
            s++;
        } else if (kind == 0 && s > 0) {
            s--;
        } else if (kind == 0) {
            bits[k / 8u] = (unsigned char)(bits[k / 8u] | (1u << (k % 8u)));
        }
        e = i - 1;
        k++;
    }
    return k;
}

static void leaf_emit(core_buf_t *out, const merged_t *m, size_t lo, size_t hi, size_t s) {
    unsigned char bits[LEAF_CHARS / 8u] = {0};
    size_t k, i = lo;
    k = leaf_marks(m, lo, hi, s, bits);
    while (i < hi) {
        size_t e;
        if (!is_slash(m, i)) {
            i++;
            continue;
        }
        e = seg_end(m, i + 1);
        k--;
        if ((bits[k / 8u] >> (k % 8u)) & 1u) {
            core_buf_put_char(out, '/');
            put_range(out, m, i + 1, e);
        }
        i = e;
    }
}

/* Counter on the left of the right half [mid, hi) of a node, given the counter s on its right. */
static size_t cross_block(const merged_t *m, size_t mid, size_t hi, size_t s) {
    size_t x, y;
    block_xy(m, mid, hi, &x, &y);
    return (s > x ? s - x : 0) + y;
}

/* Walks from the node at `level` (block index `idx` at that level, entered with counter s) down its left
 * spine to the leaf, saving the counter of each node passed. Returns the leaf's counter. */
static size_t spine(const merged_t *m, size_t *saved, unsigned level, size_t idx, size_t s) {
    size_t len = m_len(m);
    for (; level > 0; level--) {
        size_t half = (size_t)LEAF_CHARS << (level - 1);
        size_t mid = (idx * 2u + 1u) * half;
        saved[level] = s;
        s = cross_block(m, mid, mid + half < len ? mid + half : len, s);
        idx *= 2u;
    }
    return s;
}

static void emit_leaves(core_buf_t *out, const merged_t *m, unsigned top) {
    size_t saved[LEVELS_MAX + 1u];
    size_t len = m_len(m);
    size_t leaves = (len + LEAF_CHARS - 1u) / LEAF_CHARS;
    size_t l, s;
    unsigned t;
    s = spine(m, saved, top, 0, 0);
    for (l = 0;; l++) {
        size_t lo = l * LEAF_CHARS;
        leaf_emit(out, m, lo, lo + LEAF_CHARS < len ? lo + LEAF_CHARS : len, s);
        if (l + 1u >= leaves) {
            return;
        }
        t = 0;
        while ((((l + 1u) >> t) & 1u) == 0u) {
            t++;
        }
        s = spine(m, saved, t, (l + 1u) >> t, saved[t + 1u]);
    }
}

static void emit_path(core_buf_t *out, const merged_t *m) {
    size_t len = m_len(m);
    size_t leaves = (len + LEAF_CHARS - 1u) / LEAF_CHARS;
    size_t last = len;
    unsigned top = 0;
    while (((size_t)1 << top) < leaves) {
        top++;
    }
    if (top > LEVELS_MAX) {
        core_buf_put(out, NULL, 1); /* a path of 2^47 bytes or more: report it as an overflow */
        return;
    }
    emit_leaves(out, m, top);
    while (last > 0 && !is_slash(m, last - 1)) {
        last--;
    }
    if (last > 0 && seg_kind(m, last, len) != 0) {
        core_buf_put_char(out, '/');
    }
}

/* ---- reference parsing ----------------------------------------------------------------------------------- */

typedef struct {
    bool has_auth;
    core_str_t host;
    uint16_t port;
    core_str_t path;     /* as written (may be empty) */
    bool has_query;
    core_str_t query;
} ref_t;

static bool is_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/* Length of a leading "<letter>[<letter|digit|+|-|.>]*" followed by ':', else 0. */
static size_t scheme_len(core_str_t loc) {
    size_t i = 1;
    if (loc.len == 0 || !is_alpha(loc.ptr[0])) {
        return 0;
    }
    while (i < loc.len && (is_alpha(loc.ptr[i]) || (loc.ptr[i] >= '0' && loc.ptr[i] <= '9') || loc.ptr[i] == '+' ||
                           loc.ptr[i] == '-' || loc.ptr[i] == '.')) {
        i++;
    }
    return (i < loc.len && loc.ptr[i] == ':') ? i : 0;
}

static bool scheme_is_https(core_str_t loc, size_t n) {
    static const char k[] = "https";
    size_t i;
    if (n != 5) {
        return false;
    }
    for (i = 0; i < n; i++) {
        char c = loc.ptr[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != k[i]) {
            return false;
        }
    }
    return true;
}

/* Splits "path[?query][#fragment]". */
static void split_pqf(core_str_t rest, ref_t *r) {
    size_t i = 0;
    size_t q;
    while (i < rest.len && rest.ptr[i] != '?' && rest.ptr[i] != '#') {
        i++;
    }
    r->path = (core_str_t){rest.ptr, i};
    r->has_query = i < rest.len && rest.ptr[i] == '?';
    r->query = (core_str_t){NULL, 0};
    if (r->has_query) {
        q = i + 1;
        i = q;
        while (i < rest.len && rest.ptr[i] != '#') {
            i++;
        }
        r->query = (core_str_t){rest.ptr + q, i - q};
    }
}

/* `after` starts at the authority (just past "//"). */
static http_status_t parse_authority_form(core_str_t after, ref_t *r) {
    size_t i = 0;
    http_status_t st;
    while (i < after.len && after.ptr[i] != '/' && after.ptr[i] != '?' && after.ptr[i] != '#') {
        i++;
    }
    st = http_authority_parse((core_str_t){after.ptr, i}, &r->host, &r->port);
    if (st != HTTP_OK) {
        return st;
    }
    r->has_auth = true;
    split_pqf((core_str_t){after.ptr + i, after.len - i}, r);
    if (r->path.len == 0) {
        r->path = LIT("/");
    }
    return HTTP_OK;
}

static bool any_bad_byte(core_str_t s) {
    size_t i;
    for (i = 0; i < s.len; i++) {
        if (http_url_byte_bad((unsigned char)s.ptr[i])) {
            return true;
        }
    }
    return false;
}

static http_status_t ref_parse(core_str_t loc, ref_t *r) {
    size_t sl = scheme_len(loc);
    bool https = sl > 0 && scheme_is_https(loc, sl);
    r->has_auth = false;
    r->path = (core_str_t){NULL, 0};
    r->has_query = false;
    r->query = (core_str_t){NULL, 0};
    if (sl > 0 && !https) {
        return HTTP_E_SCHEME;
    }
    if (any_bad_byte(loc)) {
        return HTTP_E_URL;
    }
    if (https) {
        if (loc.len < sl + 3 || loc.ptr[sl + 1] != '/' || loc.ptr[sl + 2] != '/') {
            return HTTP_E_URL;
        }
        return parse_authority_form((core_str_t){loc.ptr + sl + 3, loc.len - sl - 3}, r);
    }
    if (loc.len >= 2 && loc.ptr[0] == '/' && loc.ptr[1] == '/') {
        return parse_authority_form((core_str_t){loc.ptr + 2, loc.len - 2}, r);
    }
    split_pqf(loc, r);
    return HTTP_OK;
}

/* ---- resolution ------------------------------------------------------------------------------------------ */

/* The current target's path (before '?') and query. */
static void cur_parts(const http_url_t *cur, core_str_t *path, ref_t *q) {
    size_t i = 0;
    while (i < cur->target.len && cur->target.ptr[i] != '?') {
        i++;
    }
    *path = (core_str_t){cur->target.ptr, i};
    q->has_query = i < cur->target.len;
    q->query = q->has_query ? (core_str_t){cur->target.ptr + i + 1, cur->target.len - i - 1} : (core_str_t){NULL, 0};
}

static merged_t pick_path(const ref_t *r, core_str_t cur_path) {
    merged_t m = {{NULL, 0}, {NULL, 0}};
    size_t d = cur_path.len;
    if (r->has_auth || (r->path.len > 0 && r->path.ptr[0] == '/')) {
        m.b = r->path;
    } else if (r->path.len == 0) {
        m.b = cur_path;
    } else {
        while (d > 1 && cur_path.ptr[d - 1] != '/') {
            d--;
        }
        m.a = (core_str_t){cur_path.ptr, d};
        m.b = r->path;
    }
    return m;
}

static void emit_url(core_buf_t *out, const ref_t *r, const http_url_t *cur, const merged_t *m, bool has_q,
                     core_str_t q) {
    core_str_t host = r->has_auth ? r->host : cur->host;
    uint16_t port = r->has_auth ? r->port : cur->port;
    core_buf_put_str(out, LIT("https://"));
    core_buf_put_str(out, host);
    if (port != 443) {
        core_buf_put_char(out, ':');
        core_buf_put_u64(out, port);
    }
    emit_path(out, m);
    if (has_q) {
        core_buf_put_char(out, '?');
        core_buf_put_str(out, q);
    }
}

http_status_t http_redirect_resolve(http_redirects_t *r, const http_url_t *cur, int status, core_str_t location,
                                    core_buf_t *out) {
    ref_t ref;
    core_str_t cur_path;
    ref_t cq;
    merged_t m;
    http_status_t st;
    if (r == NULL || cur == NULL || out == NULL || (location.ptr == NULL && location.len > 0) ||
        (cur->host.ptr == NULL && cur->host.len > 0) || (cur->target.ptr == NULL && cur->target.len > 0)) {
        return HTTP_E_ARG;
    }
    if (!http_is_redirect_status(status) || location.len == 0) {
        return HTTP_E_REDIRECT;
    }
    if (r->count >= HTTP_MAX_REDIRECTS) {
        return HTTP_E_TOO_MANY_REDIRECTS;
    }
    st = ref_parse(location, &ref);
    if (st != HTTP_OK) {
        return st;
    }
    if (core_buf_overflow(out)) {
        return HTTP_E_OVERFLOW;
    }
    cur_parts(cur, &cur_path, &cq);
    m = pick_path(&ref, cur_path);
    if (!ref.has_auth && ref.path.len == 0 && !ref.has_query) {
        emit_url(out, &ref, cur, &m, cq.has_query, cq.query);
    } else {
        emit_url(out, &ref, cur, &m, ref.has_query, ref.query);
    }
    if (core_buf_overflow(out)) {
        return HTTP_E_OVERFLOW;
    }
    r->count++;
    return HTTP_OK;
}
