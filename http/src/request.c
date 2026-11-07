#include "http/src/internal.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static bool slice_arg_bad(core_str_t s) {
    return s.ptr == NULL && s.len > 0;
}

static bool target_ok(core_str_t t) {
    size_t i;
    core_str_t rest;
    if (t.len == 0 || t.ptr[0] != '/') {
        return false;
    }
    rest = (core_str_t){t.ptr + 1, t.len - 1}; /* the '/' just checked needs no range test */
    for (i = 0; i < rest.len; i++) {
        unsigned char c = (unsigned char)rest.ptr[i];
        if (c < 0x21u || c > 0x7Eu) {
            return false;
        }
    }
    return true;
}

static bool version_ok(core_str_t v) {
    size_t i;
    if (v.len < 1 || v.len > 64) {
        return false;
    }
    for (i = 0; i < v.len; i++) {
        char c = v.ptr[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' ||
                  c == '_' || c == '+' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

http_status_t http_request_write(core_buf_t *out, const http_url_t *url, core_str_t version) {
    if (out == NULL || url == NULL || slice_arg_bad(url->host) || slice_arg_bad(url->target) ||
        slice_arg_bad(version)) {
        return HTTP_E_ARG;
    }
    if (!http_host_valid(url->host) || url->port == 0 || !target_ok(url->target) || !version_ok(version)) {
        return HTTP_E_INVALID;
    }
    if (core_buf_overflow(out)) {
        return HTTP_E_OVERFLOW;
    }
    core_buf_put_str(out, LIT("GET "));
    core_buf_put_str(out, url->target);
    core_buf_put_str(out, LIT(" HTTP/1.1\r\nHost: "));
    core_buf_put_str(out, url->host);
    if (url->port != 443) {
        core_buf_put_char(out, ':');
        core_buf_put_u64(out, url->port);
    }
    core_buf_put_str(out, LIT("\r\nUser-Agent: annas-c/"));
    core_buf_put_str(out, version);
    core_buf_put_str(out, LIT("\r\nAccept: */*\r\nConnection: close\r\n\r\n"));
    return core_buf_overflow(out) ? HTTP_E_OVERFLOW : HTTP_OK;
}
