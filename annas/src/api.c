#include "annas/src/internal.h"

#include <string.h>

#define EMPTY_URL_TEXT "API returned empty download URL"

/* The longest request target: check_config bounds the key to ANNAS_KEY_MAX bytes (3 per byte encoded), the md5 is
 * 32 digits, each index at most 20. So the target never overflows its buffer. */
_Static_assert(sizeof "/dyn/api/fast_download.json?md5=" + 32u + sizeof "&key=" + 3u * ANNAS_KEY_MAX +
                       sizeof "&path_index=" + 20u + sizeof "&domain_index=" + 20u < WS_TARGET_CAP,
               "the API request target always fits");

/* E_CONFIG for a base host the request writer refuses (an annas_config_t not made by annas_config_load). */
static annas_code_t base_invalid(const annas_ctx_t *ctx, annas_error_t *err) {
    size_t n = ctx->cfg->base_len;
    if (n >= ANNAS_BASE_MAX) {
        n = ANNAS_BASE_MAX - 1u; /* never read past the array */
    }
    core_str_t shown = {ctx->cfg->base, n};
    msg_body_t body = {core_str_from_cstr("ANNAS_BASE_URL is not a valid domain, got: "), shown, 200};
    return msg_fail(err, ANNAS_E_CONFIG, 0, ctx_key(ctx), &body);
}

static void put_index(core_buf_t *t, const char *name, bool present, uint64_t v) {
    if (present) {
        core_buf_put_str(t, core_str_from_cstr(name));
        core_buf_put_u64(t, v);
    }
}

/* The API URL: the configured host, port 443 and the target (path, md5, percent-encoded key, the optional indices)
 * built in `t`. False when the key cannot be encoded, which only a key slice {NULL, len > 0} does. */
static bool api_url(const annas_ctx_t *ctx, const annas_request_t *req, const char *md5, core_buf_t *t,
                    http_url_t *u) {
    core_buf_put_str(t, core_str_from_cstr("/dyn/api/fast_download.json?md5="));
    core_buf_put(t, md5, URL_MD5_BUF - 1u);
    core_buf_put_str(t, core_str_from_cstr("&key="));
    url_status_t st = url_pct_encode(t, ctx_key(ctx));
    put_index(t, "&path_index=", req->has_path_index, req->path_index);
    put_index(t, "&domain_index=", req->has_domain_index, req->domain_index);
    u->host = (core_str_t){ctx->cfg->base, ctx->cfg->base_len};
    u->port = 443;
    u->target = core_buf_str(t);
    return st == URL_OK;
}

/* A base the writer refuses (http_host_valid: also any base_len above 253, refused before a byte is read) is
 * E_CONFIG. The other failures cannot happen for the workspace buffers and a key slice check_config passed; a
 * {NULL, len > 0} key would fail every message alike, since each message reads the key. */
static annas_code_t api_send(annas_ctx_t *ctx, annas_error_t *err, const ws_t *w, exch_t *x, const annas_request_t *req,
                             const char *md5) {
    core_buf_t tb;
    core_buf_t rb;
    http_url_t u;
    if (!core_buf_init(&tb, (char *)w->rest, WS_TARGET_CAP) || !core_buf_init(&rb, (char *)w->body, WS_BODY_CAP) ||
        !api_url(ctx, req, md5, &tb, &u) || http_request_write(&rb, &u, core_str_from_cstr(ANNAS_VERSION)) != HTTP_OK) {
        return base_invalid(ctx, err);
    }
    return exch_open(x, u.host, u.port, core_buf_str(&rb));
}

static annas_code_t api_read(exch_t *x, const ws_t *w, head_t *h, size_t *blen) {
    http_event_t ev;
    *blen = 0;
    for (;;) {
        annas_code_t c = exch_event(x, &ev);
        if (c != ANNAS_OK) {
            return c;
        }
        if (ev.kind == HTTP_EV_END) {
            return ANNAS_OK;
        }
        if (ev.kind != HTTP_EV_BODY) {
            head_note(h, &ev, NULL);
        } else if (ev.data.len > WS_BODY_CAP - *blen) {
            return exch_fail(x, ANNAS_E_PARSE, 0, "API response is larger than 65536 bytes");
        } else {
            memcpy(w->body + *blen, ev.data.ptr, ev.data.len);
            *blen += ev.data.len;
        }
    }
}

static bool member_str(const json_value_t *obj, const char *name, core_str_t *out) {
    return json_get_str(json_obj_get(obj, core_str_from_cstr(name)), out);
}

/* account_fast_download_info as compact JSON when it is an object, else "". */
static annas_code_t api_account(const annas_ctx_t *ctx, annas_error_t *err, const json_value_t *root,
                                annas_fast_result_t *f) {
    const json_value_t *v = json_obj_get(root, core_str_from_cstr("account_fast_download_info"));
    core_buf_t b;
    f->account_len = 0;
    f->account[0] = '\0';
    if (json_type(v) != JSON_OBJECT) {
        return ANNAS_OK;
    }
    if (!core_buf_init(&b, f->account, sizeof f->account) || !json_write(&b, v)) {
        return msg_text(err, ANNAS_E_PARSE, 0, ctx_key(ctx), "account info is larger than 4096 bytes");
    }
    f->account_len = core_buf_len(&b);
    return ANNAS_OK;
}

static annas_code_t api_success(const annas_ctx_t *ctx, annas_error_t *err, const json_value_t *root, core_str_t url,
                                annas_fast_result_t *f) {
    core_str_t key = ctx_key(ctx);
    if (url.len > ANNAS_URL_MAX) {
        return msg_text(err, ANNAS_E_PARSE, 0, key, "download URL is longer than 2048 bytes");
    }
    annas_code_t c = api_account(ctx, err, root, f);
    if (c != ANNAS_OK) {
        return c;
    }
    core_str_t acct = {f->account, f->account_len};
    if (key_in(url, key)) {
        return msg_text(err, ANNAS_E_PARSE, 0, key, "download URL contains the secret key");
    }
    if (key_in(acct, key)) {
        return msg_text(err, ANNAS_E_PARSE, 0, key, "account info contains the secret key");
    }
    memcpy(f->url, url.ptr, url.len);
    f->url[url.len] = '\0';
    f->url_len = url.len;
    return ANNAS_OK;
}

typedef struct {
    core_arena_t *ar;
    const json_value_t *root;   /* NULL when the body is not valid JSON */
    core_str_t body;
    int status;
} reply_t;

/* The server text that becomes the E_API detail and its length limit, if there is one (else *src and *cut are left
 * as they are). */
static bool pick_detail(const reply_t *r, core_str_t *src, size_t *cut) {
    if (r->root == NULL && r->body.len > 0) {
        *src = r->body;
        *cut = 512;
        return true;
    }
    if (json_type(r->root) == JSON_OBJECT && member_str(r->root, "error", src) && src->len > 0) {
        *cut = 200;
        return true;
    }
    return false;
}

/* E_API "<status>: <detail>" (header: annas_fast_download_url, step 6). */
static annas_code_t api_error(const annas_ctx_t *ctx, annas_error_t *err, const reply_t *r) {
    core_str_t detail = core_str_from_cstr(EMPTY_URL_TEXT);
    core_str_t src;
    size_t cut = MSG_NO_CUT;
    char pre[24];
    /* When the redaction does not fit, the detail stays EMPTY_URL_TEXT, which is shorter than any cut. */
    if (pick_detail(r, &src, &cut)) {
        redact_into(r->ar, src, &detail);
    }
    msg_body_t body = {msg_u64(pre, sizeof pre, "", (uint64_t)r->status, ": "), detail, cut};
    return msg_fail(err, ANNAS_E_API, r->status, ctx_key(ctx), &body);
}

static annas_code_t api_map(annas_ctx_t *ctx, annas_error_t *err, const ws_t *w, const head_t *h, core_str_t body) {
    core_str_t none = {NULL, 0};
    core_arena_t ar;
    json_limits_t lim = {16, ANNAS_API_BODY_MAX, 8192, 1024};
    reply_t r = {&ar, NULL, body, h->status};
    core_str_t url;
    /* http_detect_challenge looks at the first HTTP_CHALLENGE_PREFIX bytes of the body itself. */
    if (h->challenge || http_detect_challenge(h->status, none, body) == HTTP_E_BROWSER_CHECK) {
        return msg_text(err, ANNAS_E_BROWSER_CHECK, 0, ctx_key(ctx), BROWSER_MESSAGE);
    }
    if (!core_arena_init(&ar, w->rest, w->rest_cap) || json_parse(&ar, body, &lim, &r.root, NULL) != JSON_OK) {
        r.root = NULL;
    }
    /* A 204 has no body (the http parser refuses one), so it is never a JSON object: only 200 can succeed. */
    if (h->status == 200 && json_type(r.root) == JSON_OBJECT &&
        member_str(r.root, "download_url", &url) && url.len > 0) {
        return api_success(ctx, err, r.root, url, &w->res->fast);
    }
    return api_error(ctx, err, &r);
}

annas_code_t api_fetch(annas_ctx_t *ctx, const annas_request_t *req, const char *md5, const ws_t *w,
                       annas_error_t *err) {
    exch_t x;
    head_t h = {.challenge = false, .has_loc = false}; /* the status comes with the STATUS event */
    size_t blen;
    core_str_t body;
    exch_init(&x, ctx, err, w);
    annas_code_t c = api_send(ctx, err, w, &x, req, md5);
    if (c == ANNAS_OK) {
        c = api_read(&x, w, &h, &blen);
    }
    exch_close(&x);
    if (c != ANNAS_OK) {
        return c;
    }
    body.ptr = (const char *)w->body;
    body.len = blen;
    return api_map(ctx, err, w, &h, body);
}

annas_code_t annas_fast_download_url(annas_ctx_t *ctx, const annas_request_t *req, annas_fast_result_t *out,
                                     annas_error_t *err) {
    char md5[URL_MD5_BUF];
    ws_t w;
    if (err == NULL) {
        return ANNAS_E_ARG;
    }
    if (ctx == NULL || req == NULL || out == NULL) {
        core_str_t nokey = {NULL, 0};
        return arg_invalid(err, nokey);
    }
    annas_code_t c = check_config(ctx, err, false);
    if (c == ANNAS_OK) {
        c = req_check(ctx, err, req, md5, false);
    }
    if (c != ANNAS_OK) {
        return c;
    }
    op_begin(ctx);
    ws_split(ctx, &w);
    c = api_fetch(ctx, req, md5, &w, err);
    if (c == ANNAS_OK) {
        memcpy(out, &w.res->fast, sizeof *out);
        msg_ok(err);
    }
    return c;
}
