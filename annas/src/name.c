#include "annas/src/internal.h"

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

/* "<sanitised title or md5>.<format or bin>" */
static bool build_name(const annas_request_t *req, const char *md5, annas_download_result_t *res) {
    char clean[FSNAME_BUF_SIZE];
    core_str_t title = req->title.len > 0 ? req->title : (core_str_t){md5, URL_MD5_BUF - 1u};
    core_str_t ext = req->format.len > 0 ? req->format : core_str_from_cstr("bin");
    size_t n = fsname_sanitize(title, clean, sizeof clean);
    core_buf_t b;
    if (n == 0 || !core_buf_init(&b, res->name, sizeof res->name)) {
        return false;
    }
    core_buf_put(&b, clean, n);
    core_buf_put_char(&b, '.');
    for (size_t i = 0; i < ext.len; i++) {
        core_buf_put_char(&b, core_ascii_lower(ext.ptr[i]));
    }
    res->name_len = core_buf_len(&b);
    return !core_buf_overflow(&b);
}

/* dir without trailing '/' + "/" + name */
static bool build_path(const annas_ctx_t *ctx, annas_download_result_t *res) {
    core_str_t dir = ctx->cfg->download_dir;
    core_buf_t b;
    while (dir.len > 0 && dir.ptr[dir.len - 1u] == '/') {
        dir.len--;
    }
    if (!core_buf_init(&b, res->path, sizeof res->path)) {
        return false;
    }
    core_buf_put_str(&b, dir);
    core_buf_put_char(&b, '/');
    core_buf_put(&b, res->name, res->name_len);
    res->path_len = core_buf_len(&b);
    return !core_buf_overflow(&b);
}

annas_code_t name_build(const annas_ctx_t *ctx, annas_error_t *err, const annas_request_t *req, const char *md5,
                        annas_download_result_t *res) {
    if (!build_name(req, md5, res) || !build_path(ctx, res)) {
        return msg_text(err, ANNAS_E_FS, 0, ctx_key(ctx), "could not save the file");
    }
    return ANNAS_OK;
}
