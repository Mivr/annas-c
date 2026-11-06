#include "app/src/internal.h"

#include <string.h>

core_str_t app_key(const app_deps_t *d) {
    core_str_t v = {NULL, 0};
    if (d == NULL || d->env.ops == NULL || d->env.ops->get == NULL) {
        return v;
    }
    if (!d->env.ops->get(d->env.ctx, core_str_from_cstr("ANNAS_SECRET_KEY"), &v)) {
        v.ptr = NULL;
        v.len = 0;
    }
    return v;
}

static bool unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

static size_t match_raw(core_str_t s, size_t pos, core_str_t key) {
    if (s.len - pos < key.len || memcmp(s.ptr + pos, key.ptr, key.len) != 0) {
        return 0;
    }
    return key.len;
}

static size_t match_pct(core_str_t s, size_t pos, core_str_t key) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = pos;
    for (size_t i = 0; i < key.len; i++) {
        unsigned char c = (unsigned char)key.ptr[i];
        size_t w = unreserved(c) ? 1u : 3u;
        if (s.len - j < w) {
            return 0;
        }
        if (w == 1u ? (unsigned char)s.ptr[j] != c
                    : (s.ptr[j] != '%' || s.ptr[j + 1u] != hex[c >> 4] || s.ptr[j + 2u] != hex[c & 15u])) {
            return 0;
        }
        j += w;
    }
    return j - pos;
}

void app_scrub_put(core_buf_t *dst, core_str_t src, core_str_t key) {
    size_t pos = 0;
    while (pos < src.len) {
        size_t m = 0;
        if (key.len > 0) {
            m = match_raw(src, pos, key);
            m = m > 0 ? m : match_pct(src, pos, key);
        }
        if (m > 0) {
            core_buf_put_str(dst, core_str_from_cstr("REDACTED"));
            pos += m;
        } else {
            core_buf_put_char(dst, src.ptr[pos]);
            pos++;
        }
    }
}

void app_emit(core_buf_t *dst, const core_buf_t *text, core_str_t key) {
    if (core_buf_overflow(text)) {
        core_buf_put(dst, NULL, 1);
        return;
    }
    app_scrub_put(dst, core_buf_str(text), key);
}

size_t app_echo_cut(core_str_t in, char *out) {
    size_t n = in.len < APP_ECHO_MAX ? in.len : APP_ECHO_MAX;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)in.ptr[i];
        out[i] = (c < 0x20u || c == 0x7fu) ? '?' : (char)c;
    }
    return n;
}

/* Length of the well-formed UTF-8 sequence at p[0..n), or 0. */
static size_t seq_len(const unsigned char *p, size_t n) {
    unsigned char c = p[0];
    unsigned char lo = 0x80;
    unsigned char hi = 0xbf;
    size_t need;
    if (c < 0x80u) {
        return 1;
    }
    if (c >= 0xc2u && c <= 0xdfu) {
        need = 1;
    } else if (c >= 0xe0u && c <= 0xefu) {
        need = 2;
        lo = c == 0xe0u ? 0xa0 : lo;
        hi = c == 0xedu ? 0x9f : hi;
    } else if (c >= 0xf0u && c <= 0xf4u) {
        need = 3;
        lo = c == 0xf0u ? 0x90 : lo;
        hi = c == 0xf4u ? 0x8f : hi;
    } else {
        return 0;
    }
    if (n <= need || p[1] < lo || p[1] > hi) {
        return 0;
    }
    for (size_t k = 2; k <= need; k++) {
        if ((p[k] & 0xc0u) != 0x80u) {
            return 0;
        }
    }
    return need + 1u;
}

void app_utf8_clean(char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        size_t l = seq_len((const unsigned char *)s + i, n - i);
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

app_span_t app_span_of(const core_buf_t *b) {
    app_span_t sp = {NULL, 0};
    if (b->cap > b->len + 1u) {
        sp.p = (unsigned char *)b->data + b->len + 1u;
        sp.left = b->cap - b->len - 1u;
    }
    return sp;
}

void *app_span_take(app_span_t *sp, size_t n) {
    size_t pad = (16u - ((uintptr_t)sp->p & 15u)) & 15u;
    void *r;
    if (sp->p == NULL || sp->left < pad || sp->left - pad < n) {
        return NULL;
    }
    r = sp->p + pad;
    sp->p += pad + n;
    sp->left -= pad + n;
    return r;
}

app_op_status_t app_op_run(const app_deps_t *d, const app_op_t *op, app_job_t *job) {
    annas_code_t c;
    if (d == NULL || d->env.ops == NULL || d->env.ops->get == NULL) {
        return APP_OP_INTERNAL;
    }
    if (annas_config_load(d->env, op->download, op->timeout_seconds, &job->cfg, &job->err) != ANNAS_OK) {
        return APP_OP_FAIL;
    }
    if (!annas_ctx_init(&job->ctx, &job->cfg, d->stream, d->fs, d->clock, d->workspace, d->workspace_cap)) {
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
