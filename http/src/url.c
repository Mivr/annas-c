#include "http/src/internal.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

bool http_url_byte_bad(unsigned char c) {
    return c <= 0x20u || c >= 0x7Fu || c == (unsigned char)'\\';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_hex(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_alnum(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool label_ok(core_str_t l) {
    size_t i;
    if (l.len < 1 || l.len > 63 || l.ptr[0] == '-' || l.ptr[l.len - 1] == '-') {
        return false;
    }
    for (i = 0; i < l.len; i++) {
        if (!is_alnum(l.ptr[i]) && l.ptr[i] != '-') {
            return false;
        }
    }
    return true;
}

static bool all_digits(core_str_t l) {
    size_t i;
    for (i = 0; i < l.len; i++) {
        if (!is_digit(l.ptr[i])) {
            return false;
        }
    }
    return l.len > 0;
}

/* A label that a C resolver would read as a number: all digits, or 0x followed by hex digits. */
static bool numeric_label(core_str_t l) {
    size_t i;
    if (all_digits(l)) {
        return true;
    }
    if (l.len < 3 || l.ptr[0] != '0' || (l.ptr[1] != 'x' && l.ptr[1] != 'X')) {
        return false;
    }
    for (i = 2; i < l.len; i++) {
        if (!is_hex(l.ptr[i])) {
            return false;
        }
    }
    return true;
}

static bool octet_ok(core_str_t l) {
    unsigned v = 0;
    size_t i;
    if (!all_digits(l) || l.len > 3 || (l.len > 1 && l.ptr[0] == '0')) {
        return false;
    }
    for (i = 0; i < l.len; i++) {
        v = v * 10u + (unsigned)(l.ptr[i] - '0');
    }
    return v <= 255u;
}

/* Takes the next dot-separated label off *rest. */
static void next_label(core_str_t *rest, core_str_t *label, bool *last) {
    size_t i;
    for (i = 0; i < rest->len; i++) {
        if (rest->ptr[i] == '.') {
            *label = (core_str_t){rest->ptr, i};
            rest->ptr += i + 1;
            rest->len -= i + 1;
            *last = false;
            return;
        }
    }
    *label = *rest;
    *last = true;
    rest->ptr += rest->len;
    rest->len = 0;
}

/* Walks the labels: checks each, counts them, reports the last. */
static bool walk_labels(core_str_t s, unsigned *count, core_str_t *last_label, bool *quad_ok) {
    core_str_t rest = s;
    core_str_t l = {NULL, 0};
    bool last = false;
    *count = 0;
    *quad_ok = true;
    while (!last) {
        next_label(&rest, &l, &last);
        if (!label_ok(l)) {
            return false;
        }
        (*count)++;
        if (!octet_ok(l)) {
            *quad_ok = false;
        }
    }
    *last_label = l;
    return true;
}

bool http_host_valid(core_str_t s) {
    unsigned count;
    core_str_t last_label = {NULL, 0};
    bool quad_ok;
    if (s.len == 0 || s.len > 253 || s.ptr == NULL) {
        return false;
    }
    if (!walk_labels(s, &count, &last_label, &quad_ok)) {
        return false;
    }
    if (numeric_label(last_label)) {
        return count == 4u && quad_ok;
    }
    return true;
}

static http_status_t port_parse(core_str_t p, uint16_t *port) {
    unsigned v = 0;
    size_t i;
    if (p.len < 1 || p.len > 5 || p.ptr[0] == '0') {
        return HTTP_E_URL;
    }
    for (i = 0; i < p.len; i++) {
        if (!is_digit(p.ptr[i])) {
            return HTTP_E_URL;
        }
        v = v * 10u + (unsigned)(p.ptr[i] - '0');
    }
    if (v > 65535u) {
        return HTTP_E_URL;
    }
    *port = (uint16_t)v;
    return HTTP_OK;
}

http_status_t http_authority_parse(core_str_t auth, core_str_t *host, uint16_t *port) {
    size_t i;
    size_t colon = CORE_NPOS;
    http_status_t st = HTTP_OK;
    core_str_t h;
    uint16_t pt = 443;
    for (i = 0; i < auth.len; i++) {
        if (auth.ptr[i] == '@' || auth.ptr[i] == '[') {
            return HTTP_E_URL;
        }
        if (auth.ptr[i] == ':' && colon == CORE_NPOS) {
            colon = i;
        }
    }
    h = (core_str_t){auth.ptr, colon == CORE_NPOS ? auth.len : colon};
    if (!http_host_valid(h)) {
        return HTTP_E_URL;
    }
    if (colon != CORE_NPOS) {
        st = port_parse((core_str_t){auth.ptr + colon + 1, auth.len - colon - 1}, &pt);
    }
    if (st != HTTP_OK) {
        return st;
    }
    *host = h;
    *port = pt;
    return HTTP_OK;
}

static bool scheme_https(core_str_t url) {
    static const char k_scheme[] = "https://";
    size_t i;
    if (url.len < sizeof k_scheme - 1) {
        return false;
    }
    for (i = 0; i < 5; i++) {
        char c = url.ptr[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != k_scheme[i]) {
            return false;
        }
    }
    return url.ptr[5] == ':' && url.ptr[6] == '/' && url.ptr[7] == '/';
}

static bool url_has_bad_byte(core_str_t url) {
    size_t i;
    for (i = 0; i < url.len; i++) {
        if (http_url_byte_bad((unsigned char)url.ptr[i])) {
            return true;
        }
    }
    return false;
}

/* The first of '/', '?', '#' at or after `from`, or the end of the url. */
static size_t authority_end(core_str_t url, size_t from) {
    size_t i = from;
    while (i < url.len && url.ptr[i] != '/' && url.ptr[i] != '?' && url.ptr[i] != '#') {
        i++;
    }
    return i;
}

/* The first '#' at or after `from`, or the end of the url. */
static size_t fragment_start(core_str_t url, size_t from) {
    size_t i = from;
    while (i < url.len && url.ptr[i] != '#') {
        i++;
    }
    return i;
}

http_status_t http_url_split(core_str_t url, http_url_t *out) {
    size_t i;
    http_url_t u;
    http_status_t st;
    if (out == NULL || (url.ptr == NULL && url.len > 0)) {
        return HTTP_E_ARG;
    }
    if (!scheme_https(url)) {
        return HTTP_E_SCHEME;
    }
    if (url.len > HTTP_MAX_URL || url_has_bad_byte(url)) {
        return HTTP_E_URL;
    }
    i = authority_end(url, 8);
    st = http_authority_parse((core_str_t){url.ptr + 8, i - 8}, &u.host, &u.port);
    if (st != HTTP_OK) {
        return st;
    }
    if (i >= url.len || url.ptr[i] != '/') {
        return HTTP_E_URL;
    }
    u.target = (core_str_t){url.ptr + i, fragment_start(url, i) - i};
    *out = u;
    return HTTP_OK;
}
