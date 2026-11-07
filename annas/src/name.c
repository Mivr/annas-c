#include "annas/src/internal.h"

#include <string.h>

bool name_format_ok(core_str_t f) {
    if (f.len == 0) {
        return true;
    }
    if (f.len > 16u) {
        return false;
    }
    for (size_t i = 0; i < f.len; i++) {
        char c = f.ptr[i];
        bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum) {
            return false;
        }
    }
    return true;
}

annas_code_t req_check(const annas_ctx_t *ctx, annas_error_t *err, const annas_request_t *req, char *md5_out,
                       bool for_download) {
    core_str_t key = ctx_key(ctx);
    bool bad_ptr = (req->md5.ptr == NULL && req->md5.len > 0) ||
                   (for_download && ((req->title.ptr == NULL && req->title.len > 0) ||
                                     (req->format.ptr == NULL && req->format.len > 0)));
    if (bad_ptr) {
        return arg_invalid(err, key);
    }
    if (req->md5.len != URL_MD5_BUF - 1u || !url_md5_normalize(req->md5, md5_out)) {
        return msg_text(err, ANNAS_E_ARG, 0, key, "md5 must be 32 hexadecimal characters");
    }
    if (for_download && !name_format_ok(req->format)) {
        return msg_text(err, ANNAS_E_ARG, 0, key, "format must consist of 1 to 16 ASCII letters or digits");
    }
    return ANNAS_OK;
}

/* The longest name: a sanitised title, '.', a format of at most 16 bytes (req_check); the longest path: a dir of at
 * most ANNAS_DIR_MAX bytes (check_config), '/', the name. So the builders below never overflow. */
_Static_assert(FSNAME_MAX_LEN + 1u + 16u < ANNAS_NAME_MAX, "the file name always fits");
_Static_assert(ANNAS_DIR_MAX + 1u + FSNAME_MAX_LEN + 1u + 16u < ANNAS_PATH_MAX, "the file path always fits");

/* dir without trailing '/' + "/" + name */
static void build_path(const annas_ctx_t *ctx, annas_download_result_t *res) {
    core_str_t dir = ctx->cfg->download_dir;
    core_buf_t b;
    while (dir.len > 0 && dir.ptr[dir.len - 1u] == '/') {
        dir.len--;
    }
    (void)core_buf_init(&b, res->path, sizeof res->path); /* the result's own array: cannot fail */
    core_buf_put_str(&b, dir);
    core_buf_put_char(&b, '/');
    core_buf_put(&b, res->name, res->name_len);
    res->path_len = core_buf_len(&b);
}

/* `src` with every non-overlapping occurrence of the key (left to right) replaced by "REDACTED", into dst; returns the
 * length. dst may be src itself: the output never gets ahead of the input, since the key (16 bytes or more,
 * check_config) is longer than its replacement (8), and the output is never longer than the input. */
static size_t redact_to(char *dst, core_str_t src, core_str_t key) {
    static const char word[] = URL_REDACTED;
    cat_t t = {src, {NULL, 0}, false, false};
    size_t o = 0;
    size_t pos = 0;
    while (pos < src.len) {
        size_t m = key_match(&t, pos, key);
        if (m > 0) {
            memcpy(dst + o, word, sizeof word - 1u);
            o += sizeof word - 1u;
            pos += m;
        } else {
            dst[o] = src.ptr[pos];
            o++;
            pos++;
        }
    }
    return o;
}

/* The name "<sanitised title or md5>.<format or bin>" with the key redacted twice (annas.h, NAME): first from the
 * request's title (the md5 when there is none), before fsname_sanitize cuts and cleans it; then from the complete
 * name. fsname_sanitize returns at least 1 here: `clean` has FSNAME_BUF_SIZE bytes and req_check refused a title
 * {NULL, len > 0}. A title longer than the scratch is read up to the scratch's size. */
static size_t name_text(const annas_ctx_t *ctx, const annas_request_t *req, const char *md5, const ws_t *w) {
    char clean[FSNAME_BUF_SIZE];
    core_str_t key = ctx_key(ctx);
    core_str_t title = req->title.len > 0 ? req->title : (core_str_t){md5, URL_MD5_BUF - 1u};
    core_str_t ext = req->format.len > 0 ? req->format : core_str_from_cstr("bin");
    core_buf_t b;
    if (title.len > w->rest_cap) {
        title.len = w->rest_cap;
    }
    title.len = redact_to((char *)w->rest, title, key);
    title.ptr = (const char *)w->rest;
    size_t n = fsname_sanitize(title, clean, sizeof clean);
    (void)core_buf_init(&b, w->res->name, sizeof w->res->name); /* the result's own array: cannot fail */
    core_buf_put(&b, clean, n);
    core_buf_put_char(&b, '.');
    for (size_t i = 0; i < ext.len; i++) {
        core_buf_put_char(&b, core_ascii_lower(ext.ptr[i]));
    }
    core_str_t whole = core_buf_str(&b);
    return redact_to(w->res->name, whole, key);
}

void name_build(const annas_ctx_t *ctx, const annas_request_t *req, const char *md5, const ws_t *w) {
    annas_download_result_t *res = w->res;
    res->name_len = name_text(ctx, req, md5, w);
    res->name[res->name_len] = '\0';
    build_path(ctx, res);
}
