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

/* ---- dot-segment removal: the RFC 3986 section 5.2.4 write cursor --------------------------------------------
 *
 * The output buffer itself is the stack of kept items. Each item is "/seg" (one per '/' of the path; text
 * before the first '/' is ignored). A normal segment (empty ones too) is appended, "." is dropped, and ".."
 * truncates the output back to the '/' of the last kept item (never above `floor`, where the path starts).
 * Every input byte is copied at most once and removed at most once: linear time, no extra memory. */

static void put_range(core_buf_t *out, const merged_t *m, size_t s, size_t e) {
    size_t i;
    for (i = s; i < e; i++) {
        core_buf_put_char(out, m_at(m, i));
    }
}

/* Drops the last kept item (the output after `floor`), if any. Every item is "/seg" and the first one starts at
 * `floor` (emit_path appends the '/' before anything else), so the scan back always ends at a '/' >= floor. */
static void pop_item(core_buf_t *out, size_t floor) {
    core_str_t s = core_buf_str(out);
    size_t n = s.len;
    if (n <= floor) {
        return;
    }
    do {
        n--;
    } while (s.ptr[n] != '/');
    core_buf_truncate(out, n);
}

static void emit_path(core_buf_t *out, const merged_t *m) {
    size_t floor = core_buf_len(out);
    size_t len = m_len(m);
    size_t i = 0;
    bool dir = false; /* the last segment was "." or ".." */
    while (i < len && !core_buf_overflow(out)) {
        size_t e;
        int kind;
        if (m_at(m, i) != '/') {
            i++;
            continue;
        }
        e = seg_end(m, i + 1);
        kind = seg_kind(m, i + 1, e);
        dir = kind != 0;
        if (kind == 0) {
            core_buf_put_char(out, '/');
            put_range(out, m, i + 1, e);
        } else if (kind == 2) {
            pop_item(out, floor);
        }
        i = e;
    }
    if (dir) {
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

/* A scheme character: a letter, or after the first character also a digit, '+', '-' or '.'. */
static bool scheme_char(char c, size_t i) {
    return is_alpha(c) || (i > 0 && ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'));
}

/* Length of a leading "<letter>[<letter|digit|+|-|.>]*" followed by ':', else 0. A leading ':' also gives 0. */
static size_t scheme_len(core_str_t loc) {
    size_t i;
    for (i = 0; i < loc.len; i++) {
        if (!scheme_char(loc.ptr[i], i)) {
            return loc.ptr[i] == ':' ? i : 0;
        }
    }
    return 0;
}

static bool scheme_is_https(core_str_t loc, size_t n) {
    static const char k[] = "https";
    size_t i;
    if (n != 5) {
        return false;
    }
    for (i = 0; i < n; i++) {
        /* k holds only letters, so OR-ing 0x20 matches exactly each letter's two cases. */
        if (((unsigned char)loc.ptr[i] | 0x20u) != (unsigned char)k[i]) {
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
    bool https = scheme_is_https(loc, sl);
    r->has_auth = false; /* path, has_query and (if has_query) query are set by split_pqf on every success path */
    if (sl > 0 && !https) {
        return HTTP_E_SCHEME;
    }
    if (any_bad_byte(loc)) {
        return HTTP_E_URL;
    }
    if (https) {
        core_str_t after = {loc.ptr + sl + 1, loc.len - sl - 1}; /* scheme_len found the ':' at loc[sl] */
        if (!core_str_starts_with(after, LIT("//"))) {
            return HTTP_E_URL;
        }
        return parse_authority_form((core_str_t){after.ptr + 2, after.len - 2}, r);
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
    size_t i = core_str_find(cur->target, LIT("?"));
    q->has_query = i != CORE_NPOS;
    if (!q->has_query) {
        *path = cur->target;
        return;
    }
    *path = (core_str_t){cur->target.ptr, i};
    q->query = (core_str_t){cur->target.ptr + i + 1, cur->target.len - i - 1};
}

static merged_t pick_path(const ref_t *r, core_str_t cur_path) {
    merged_t m = {{NULL, 0}, r->path};
    size_t d = cur_path.len;
    if (r->has_auth) {
        return m;
    }
    if (r->path.len == 0) {
        m.b = cur_path;
        return m;
    }
    if (r->path.ptr[0] == '/') {
        return m;
    }
    while (d > 0 && cur_path.ptr[d - 1] != '/') {
        d--;
    }
    m.a = (core_str_t){cur_path.ptr, d};
    return m;
}

/* `qs` supplies the query (its query is read only when its has_query is set). */
static void emit_url(core_buf_t *out, const ref_t *r, const http_url_t *cur, const merged_t *m, const ref_t *qs) {
    core_str_t host = r->has_auth ? r->host : cur->host;
    uint16_t port = r->has_auth ? r->port : cur->port;
    core_buf_put_str(out, LIT("https://"));
    core_buf_put_str(out, host);
    if (port != 443) {
        core_buf_put_char(out, ':');
        core_buf_put_u64(out, port);
    }
    emit_path(out, m);
    if (qs->has_query) {
        core_buf_put_char(out, '?');
        core_buf_put_str(out, qs->query);
    }
}

static bool resolve_args_ok(const http_redirects_t *r, const http_url_t *cur, core_str_t location,
                            const core_buf_t *out) {
    return r != NULL && cur != NULL && out != NULL && (location.ptr != NULL || location.len == 0) &&
           (cur->host.ptr != NULL || cur->host.len == 0) && (cur->target.ptr != NULL || cur->target.len == 0);
}

http_status_t http_redirect_resolve(http_redirects_t *r, const http_url_t *cur, int status, core_str_t location,
                                    core_buf_t *out) {
    ref_t ref;
    core_str_t cur_path;
    ref_t cq;
    merged_t m;
    http_status_t st;
    if (!resolve_args_ok(r, cur, location, out)) {
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
    emit_url(out, &ref, cur, &m, (!ref.has_auth && ref.path.len == 0 && !ref.has_query) ? &cq : &ref);
    if (core_buf_overflow(out)) {
        return HTTP_E_OVERFLOW;
    }
    r->count++;
    return HTTP_OK;
}
