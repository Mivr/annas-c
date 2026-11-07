#include "app/src/internal.h"

#include <string.h>

core_str_t app_key(const app_deps_t *d) {
    core_str_t v; /* set by get when it finds the name (ports.h), else below */
    bool found = d != NULL && d->env.ops != NULL && d->env.ops->get != NULL &&
                 d->env.ops->get(d->env.ctx, core_str_from_cstr("ANNAS_SECRET_KEY"), &v);
    if (!found) {
        v.ptr = NULL;
        v.len = 0;
    }
    return v;
}

static bool unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

/* The three matchers take the rest of the text, from the candidate position on, and return the length of the
 * match at its start (0: none). The key is never empty here (see app_finalise). */
static size_t match_raw(core_str_t rest, core_str_t key) { return core_str_starts_with(rest, key) ? key.len : 0; }

static size_t match_pct(core_str_t rest, core_str_t key) {
    static const char hex[] = "0123456789ABCDEF";
    core_str_t t = rest;
    for (size_t i = 0; i < key.len; i++) {
        unsigned char c = (unsigned char)key.ptr[i];
        size_t w = unreserved(c) ? 1u : 3u;
        if (t.len < w) {
            return 0;
        }
        if (w == 1u ? (unsigned char)t.ptr[0] != c : (t.ptr[0] != '%' || t.ptr[1] != hex[c >> 4] || t.ptr[2] != hex[c & 15u])) {
            return 0;
        }
        t.ptr += w;
        t.len -= w;
    }
    return rest.len - t.len;
}

/* One scrubbing pass: the occurrences (raw or percent-encoded key) are replaced by "REDACTED". */
typedef struct {
    core_str_t key;
    bool pct;
} pass_t;

static size_t match_at(core_str_t rest, const pass_t *p) { return p->pct ? match_pct(rest, p->key) : match_raw(rest, p->key); }

/* `res` = the pass over `src`, written to dst[0..APP_FIN_MAX); false when it does not fit. */
static bool pass_run(core_str_t src, const pass_t *p, char *dst, core_str_t *res) {
    static const char red[] = "REDACTED";
    core_str_t rest = src;
    size_t n = 0;
    while (rest.len > 0) {
        size_t m = match_at(rest, p);
        size_t w = m > 0 ? sizeof red - 1u : 1u;
        size_t step = m > 0 ? m : w; /* w is 1 without a match */
        if (APP_FIN_MAX - n < w) {
            return false;
        }
        if (m > 0) {
            memcpy(dst + n, red, w);
        } else {
            dst[n] = rest.ptr[0];
        }
        rest.ptr += step;
        rest.len -= step;
        n += w;
    }
    res->ptr = dst;
    res->len = n;
    return true;
}

/* The key has a byte that url_pct_encode would change, so the encoded key differs from the key. */
static bool pct_differs(core_str_t key) {
    for (size_t i = 0; i < key.len; i++) {
        if (!unreserved((unsigned char)key.ptr[i])) {
            return true;
        }
    }
    return false;
}

bool app_scratch_ok(const app_deps_t *d) {
    return d != NULL && d->scratch != NULL && d->scratch_cap >= APP_SCRATCH_MIN && ((uintptr_t)d->scratch & 15u) == 0;
}

app_scratch_t *app_scratch(const app_deps_t *d) { return (app_scratch_t *)d->scratch; }

bool app_finalise(const app_deps_t *d, core_str_t src, core_str_t *res) {
    app_scratch_t *sc = app_scratch(d);
    pass_t p1 = {app_key(d), false};
    pass_t p2 = {p1.key, true};
    *res = src;
    if (p1.key.len == 0) {
        return src.len <= APP_FIN_MAX; /* nothing to scrub */
    }
    if (!pass_run(src, &p1, sc->p1, res)) {
        return false;
    }
    if (p1.key.len <= ANNAS_KEY_MAX && pct_differs(p1.key)) {
        return pass_run(*res, &p2, sc->u.p2, res); /* reads p1 (or src), writes the area src may live in */
    }
    return true;
}

/* A CLI outcome text is below APP_TEXT_CAP bytes and every byte becomes at most 8 (a one-byte key), in each of the
 * two passes alike (the second one only rewrites bytes the first left): the finalised text always fits. */
_Static_assert(8u * (APP_TEXT_CAP - 1u) <= APP_FIN_MAX, "a finalised outcome text fits");

void app_emit(const app_deps_t *d, core_buf_t *dst, core_str_t text) {
    core_str_t fin;
    (void)app_finalise(d, text, &fin);
    core_buf_put_str(dst, fin);
}

size_t app_echo_cut(core_str_t in, char *out) {
    size_t n = 0;
    while (n < in.len && n < APP_ECHO_MAX) {
        unsigned char c = (unsigned char)in.ptr[n];
        out[n] = (c < 0x20u || c == 0x7fu) ? '?' : (char)c;
        n++;
    }
    return n;
}

/* Continuation bytes a lead byte needs (0: not a lead byte) and the range of the first of them. */
static size_t lead_need(unsigned char c, unsigned char *lo, unsigned char *hi) {
    size_t need;
    *lo = 0x80;
    *hi = 0xbf;
    if (c >= 0xc2u && c <= 0xdfu) {
        need = 1;
    } else if (c >= 0xe0u && c <= 0xefu) {
        need = 2;
        *lo = c == 0xe0u ? 0xa0 : *lo;
        *hi = c == 0xedu ? 0x9f : *hi;
    } else if (c >= 0xf0u && c <= 0xf4u) {
        need = 3;
        *lo = c == 0xf0u ? 0x90 : *lo;
        *hi = c == 0xf4u ? 0x8f : *hi;
    } else {
        return 0;
    }
    return need;
}

/* Bytes 2..need of a sequence are continuation bytes. */
static bool tail_ok(const unsigned char *p, size_t need) {
    for (size_t k = 2; k <= need; k++) {
        if ((p[k] & 0xc0u) != 0x80u) {
            return false;
        }
    }
    return true;
}

/* Length of the well-formed UTF-8 sequence at p, or 0. It never reads past the first byte that is not part of the
 * sequence, which is a NUL at the end of the text at the latest: a NUL is neither a lead nor a continuation byte. */
static size_t seq_len(const unsigned char *p) {
    unsigned char lo;
    unsigned char hi;
    size_t need;
    if (p[0] < 0x80u) {
        return 1;
    }
    need = lead_need(p[0], &lo, &hi);
    if (need == 0 || p[1] < lo || p[1] > hi || !tail_ok(p, need)) {
        return 0;
    }
    return need + 1u;
}

void app_utf8_clean(char *s, size_t n) {
    size_t i = 0;
    while (i != n) { /* a sequence never reaches s[n]: i lands on n */
        size_t l = seq_len((const unsigned char *)s + i);
        if (l == 0) {
            s[i] = '?';
            i++;
        } else {
            i += l;
        }
    }
}

void app_put_escaped(core_buf_t *b, core_str_t s) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.ptr[i];
        if (c == '"' || c == '\\') {
            core_buf_put_char(b, '\\');
            core_buf_put_char(b, (char)c);
        } else if (c < 0x20u) {
            core_buf_put_str(b, core_str_from_cstr("\\u00"));
            core_buf_put_char(b, hex[c >> 4]);
            core_buf_put_char(b, hex[c & 15u]);
        } else {
            core_buf_put_char(b, (char)c);
        }
    }
}

app_op_status_t app_op_run(const app_deps_t *d, const app_op_t *op, app_job_t *job) {
    annas_code_t c;
    if (d == NULL || d->env.ops == NULL || d->env.ops->get == NULL) {
        return APP_OP_INTERNAL;
    }
    if (annas_config_load(d->env, op->download, op->timeout_seconds, &job->cfg, &job->err) != ANNAS_OK) {
        return APP_OP_FAIL;
    }
    if (!annas_ctx_init(&job->ctx, &job->cfg, (annas_ports_t){d->stream, d->fs, d->clock}, d->workspace, d->workspace_cap)) {
        return APP_OP_INTERNAL;
    }
    if (op->download) {
        c = annas_download(&job->ctx, &op->req, &job->dl, &job->err);
    } else {
        c = annas_fast_download_url(&job->ctx, &op->req, &job->dl.fast, &job->err);
    }
    return c == ANNAS_OK ? APP_OP_OK : APP_OP_FAIL;
}

void app_op_text(const app_op_t *op, const app_job_t *job, app_op_status_t st, core_buf_t *b) {
    if (st == APP_OP_OK && op->download) {
        (void)annas_format_downloaded(b, &job->dl);
    } else if (st == APP_OP_OK) {
        (void)annas_format_download_url(b, &job->dl.fast);
    } else if (st == APP_OP_FAIL) {
        (void)annas_error_line(&job->err, b);
    } else {
        core_buf_put_str(b, core_str_from_cstr(APP_INTERNAL_LINE));
    }
}
