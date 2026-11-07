#include "annas/src/internal.h"

#include <string.h>

typedef struct {
    annas_ctx_t *ctx;
    annas_error_t *err;
    const ws_t *w;
    char *loc;          /* Location of the last response, HTTP_MAX_LINE bytes */
    head_t head;
    bool follow;        /* the last response was a redirect to follow */
    uint64_t bytes;
} dl_t;

static annas_code_t dl_fail(const dl_t *d, annas_code_t code, int status, const char *text) {
    return msg_text(d->err, code, status, ctx_key(d->ctx), text);
}

/* ---- the file ---------------------------------------------------------------------------------------------------- */

static annas_code_t dl_create(dl_t *d, ports_fs_file_t *f) {
    annas_ctx_t *c = d->ctx;
    annas_code_t r = op_check(c, d->err);
    if (r != ANNAS_OK) {
        return r;
    }
    if (c->fs.ops->create_temp(c->fs.ctx, f) != PORTS_FS_OK) {
        return dl_fail(d, ANNAS_E_FS, 0, "could not create a temporary file");
    }
    return ANNAS_OK;
}

static annas_code_t dl_write(dl_t *d, ports_fs_file_t f, core_str_t data) {
    annas_ctx_t *c = d->ctx;
    annas_code_t r = op_check(c, d->err);
    if (r != ANNAS_OK) {
        return r;
    }
    if (c->fs.ops->write(c->fs.ctx, f, data.ptr, data.len) != PORTS_FS_OK) {
        return dl_fail(d, ANNAS_E_FS, 0, "could not write the file");
    }
    d->bytes += data.len;
    return ANNAS_OK;
}

static annas_code_t dl_commit(dl_t *d, ports_fs_file_t f) {
    annas_ctx_t *c = d->ctx;
    annas_code_t r = op_check(c, d->err);
    core_str_t name = {d->w->res->name, d->w->res->name_len};
    ports_fs_err_t e;
    if (r != ANNAS_OK) {
        return r;
    }
    e = c->fs.ops->commit(c->fs.ctx, f, name);
    if (e == PORTS_FS_OK) {
        return ANNAS_OK;
    }
    if (e == PORTS_FS_E_EXISTS) {
        msg_body_t body = {core_str_from_cstr("file already exists: "), name, MSG_NO_CUT};
        return msg_fail(d->err, ANNAS_E_FS, 0, ctx_key(c), &body);
    }
    return dl_fail(d, ANNAS_E_FS, 0, "could not save the file");
}

/* Body events to the temp file, one write per event, then the commit at the end of the message. */
NOINLINE static annas_code_t dl_pump(dl_t *d, exch_t *x, ports_fs_file_t f) {
    http_event_t ev;
    for (;;) {
        annas_code_t c = exch_event(x, &ev);
        if (c != ANNAS_OK) {
            return c;
        }
        if (ev.kind == HTTP_EV_END) {
            return dl_commit(d, f);
        }
        if (ev.kind == HTTP_EV_BODY) {
            c = dl_write(d, f, ev.data);
            if (c != ANNAS_OK) {
                return c;
            }
        }
    }
}

static annas_code_t dl_transfer(dl_t *d, exch_t *x) {
    ports_fs_file_t f = PORTS_FS_NO_FILE;
    annas_code_t c = dl_create(d, &f);
    if (c != ANNAS_OK) {
        return c;
    }
    c = dl_pump(d, x, f);
    if (c != ANNAS_OK) {
        /* The first error stays the reported one, whatever discard says. */
        (void)d->ctx->fs.ops->discard(d->ctx->fs.ctx, f);
    }
    return c;
}

/* ---- one response ------------------------------------------------------------------------------------------------ */

static annas_code_t dl_browser(const dl_t *d) { return dl_fail(d, ANNAS_E_BROWSER_CHECK, 0, BROWSER_MESSAGE); }

/* A body event is at most one stream read (WS_IO_CAP bytes): the last one copied by dl_reject still fits. */
_Static_assert(HTTP_CHALLENGE_PREFIX + WS_IO_CAP <= WS_BODY_CAP, "challenge prefix copy does not fit");

/* Neither 200 nor a redirect: look at the first body bytes for a challenge, else E_HTTP. Reads stop once
 * HTTP_CHALLENGE_PREFIX bytes are in; http_detect_challenge itself looks at no more than that. */
NOINLINE static annas_code_t dl_reject(dl_t *d, exch_t *x) {
    http_event_t ev;
    core_str_t none = {NULL, 0};
    size_t n = 0;
    char pre[40];
    while (n < HTTP_CHALLENGE_PREFIX) {
        annas_code_t c = exch_event(x, &ev);
        if (c != ANNAS_OK) {
            return c;
        }
        if (ev.kind == HTTP_EV_END) {
            break;
        }
        if (ev.kind == HTTP_EV_BODY) {
            memcpy(d->w->body + n, ev.data.ptr, ev.data.len);
            n += ev.data.len;
        }
    }
    core_str_t prefix = {(const char *)d->w->body, n};
    if (http_detect_challenge(d->head.status, none, prefix) == HTTP_E_BROWSER_CHECK) {
        return dl_browser(d);
    }
    msg_body_t body = {msg_u64(pre, sizeof pre, "unexpected HTTP status ", (uint64_t)d->head.status, ""), none,
                       MSG_NO_CUT};
    return msg_fail(d->err, ANNAS_E_HTTP, d->head.status, ctx_key(d->ctx), &body);
}

static annas_code_t dl_dispatch(dl_t *d, exch_t *x) {
    int status = d->head.status;
    if (http_is_redirect_status(status)) {
        d->follow = !d->head.challenge;
        return d->head.challenge ? dl_browser(d) : ANNAS_OK;
    }
    if (status == 200) {
        return dl_transfer(d, x);
    }
    return dl_reject(d, x);
}

static annas_code_t dl_head(dl_t *d, exch_t *x) {
    http_event_t ev;
    for (;;) {
        annas_code_t c = exch_event(x, &ev);
        if (c != ANNAS_OK) {
            return c;
        }
        if (ev.kind == HTTP_EV_HEADERS_END) {
            return dl_dispatch(d, x);
        }
        head_note(&d->head, &ev, d->loc);
    }
}

/* One request on its own connection, up to the decision on its response. */
NOINLINE static annas_code_t dl_hop(dl_t *d, const http_url_t *u, core_str_t request) {
    exch_t x;
    exch_init(&x, d->ctx, d->err, d->w);
    annas_code_t c = exch_open(&x, u->host, u->port, request);
    if (c == ANNAS_OK) {
        c = dl_head(d, &x);
    }
    exch_close(&x);
    return c;
}

/* The next URL from the redirect response, into `nb`. */
static annas_code_t dl_resolve(dl_t *d, http_redirects_t *rd, const http_url_t *u, core_buf_t *nb) {
    core_str_t loc = {d->loc, d->head.loc_len};
    http_status_t st = http_redirect_resolve(rd, u, d->head.status, loc, nb);
    const char *text;
    if (st == HTTP_OK) {
        return ANNAS_OK;
    }
    if (st == HTTP_E_TOO_MANY_REDIRECTS) {
        text = "too many redirects";
    } else if (st == HTTP_E_REDIRECT) {
        text = "redirect without a Location header";
    } else if (st == HTTP_E_SCHEME) {
        text = "redirect to a non-https URL";
    } else {
        text = "invalid redirect location";
    }
    return dl_fail(d, ANNAS_E_HTTP, d->head.status, text);
}

/* The target of a redirect holds the key: in the host (compared without case), or in the path or query (exactly).
 * The key class has no ':' or '/', so a key lies wholly inside one of them. */
static annas_code_t dl_redirect_key(const dl_t *d, const http_url_t *u) {
    core_str_t key = ctx_key(d->ctx);
    if (key_in(u->host, key, true) || key_in(u->target, key, false)) {
        return dl_fail(d, ANNAS_E_HTTP, d->head.status, "redirect contains the key");
    }
    return ANNAS_OK;
}

/* Follows the chain starting at the API's download URL until a response is final. A URL that http_url_split
 * accepts always gives a request (valid host, port and target, far below WS_BODY_CAP), and the buffers are the
 * workspace's own, so every failure of the first check is a URL that is not absolute https. */
NOINLINE static annas_code_t dl_run(dl_t *d) {
    unsigned char *bufs[2] = {d->w->rest, d->w->rest + WS_URL_BUF};
    const annas_fast_result_t *f = &d->w->res->fast;
    http_redirects_t rd = {0};
    unsigned which = 0;
    bool redirected = false;
    core_str_t cur = {(const char *)bufs[0], f->url_len};
    d->loc = (char *)d->w->rest + WS_LOC_OFF;
    memcpy(bufs[0], f->url, f->url_len);
    for (;;) {
        http_url_t u;
        core_buf_t rb;
        core_buf_t nb;
        if (http_url_split(cur, &u) != HTTP_OK || !core_buf_init(&rb, (char *)d->w->body, WS_BODY_CAP) ||
            http_request_write(&rb, &u, core_str_from_cstr(ANNAS_VERSION)) != HTTP_OK) {
            return dl_fail(d, ANNAS_E_PARSE, 0, "download URL must be an absolute https URL");
        }
        if (redirected) { /* d->head still holds the redirect response that led here */
            annas_code_t k = dl_redirect_key(d, &u);
            if (k != ANNAS_OK) {
                return k;
            }
        }
        d->follow = false;
        d->head = (head_t){.challenge = false, .has_loc = false, .loc_len = 0};
        annas_code_t c = dl_hop(d, &u, core_buf_str(&rb));
        if (c != ANNAS_OK || !d->follow) {
            return c;
        }
        which ^= 1u;
        (void)core_buf_init(&nb, (char *)bufs[which], WS_URL_BUF); /* a workspace buffer: cannot fail */
        c = dl_resolve(d, &rd, &u, &nb);
        if (c != ANNAS_OK) {
            return c;
        }
        cur = core_buf_str(&nb);
        redirected = true;
    }
}

/* Everything after the API call: names, the transfer, and the hand-over of the result. */
static annas_code_t dl_finish(annas_ctx_t *ctx, annas_error_t *err, const ws_t *w, const annas_request_t *req,
                              const char *md5, annas_download_result_t *out) {
    dl_t d = {.ctx = ctx, .err = err, .w = w, .bytes = 0}; /* loc, head and follow are set per hop by dl_run */
    name_build(ctx, req, md5, w);
    annas_code_t c = dl_run(&d);
    if (c == ANNAS_OK) {
        w->res->bytes = d.bytes;
        memcpy(out, w->res, sizeof *out);
        msg_ok(err);
    }
    return c;
}

annas_code_t annas_download(annas_ctx_t *ctx, const annas_request_t *req, annas_download_result_t *out,
                            annas_error_t *err) {
    char md5[URL_MD5_BUF];
    ws_t w;
    if (err == NULL) {
        return ANNAS_E_ARG;
    }
    if (ctx == NULL || req == NULL || out == NULL) {
        return arg_invalid(err, (core_str_t){NULL, 0});
    }
    annas_code_t c = check_config(ctx->cfg->key, ctx->cfg->download_dir, CFG_DIR, err);
    if (c == ANNAS_OK) {
        c = req_check(ctx, err, req, md5, true);
    }
    if (c != ANNAS_OK) {
        return c;
    }
    op_begin(ctx);
    ws_split(ctx, &w);
    c = api_fetch(ctx, req, md5, &w, err);
    return c == ANNAS_OK ? dl_finish(ctx, err, &w, req, md5, out) : c;
}
