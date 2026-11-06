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

static bool has_dotdot(const merged_t *m) {
    size_t s = 1;
    size_t len = m_len(m);
    for (;;) {
        size_t e = seg_end(m, s);
        if (seg_kind(m, s, e) == 2) {
            return true;
        }
        if (e >= len) {
            return false;
        }
        s = e + 1;
    }
}

/* Does the normal segment starting at `s` survive later ".." segments? Scans the later segments backwards. */
static bool survives(const merged_t *m, size_t s) {
    size_t cur_end = m_len(m);
    unsigned skip = 0;
    for (;;) {
        size_t st = cur_end;
        int kind;
        while (st > 0 && m_at(m, st - 1) != '/') {
            st--;
        }
        if (st <= s) {
            return skip == 0;
        }
        kind = seg_kind(m, st, cur_end);
        if (kind == 2) {
            skip++;
        } else if (kind == 0 && skip > 0) {
            skip--;
        }
        cur_end = st - 1;
    }
}

static void put_range(core_buf_t *out, const merged_t *m, size_t s, size_t e) {
    size_t i;
    for (i = s; i < e; i++) {
        core_buf_put_char(out, m_at(m, i));
    }
}

static void emit_path(core_buf_t *out, const merged_t *m) {
    bool any_dd = has_dotdot(m);
    size_t len = m_len(m);
    size_t s = 1;
    int kind;
    for (;;) {
        size_t e = seg_end(m, s);
        kind = seg_kind(m, s, e);
        if (kind == 0 && (!any_dd || survives(m, s))) {
            core_buf_put_char(out, '/');
            put_range(out, m, s, e);
        }
        if (e >= len) {
            break;
        }
        s = e + 1;
    }
    if (kind != 0) {
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
