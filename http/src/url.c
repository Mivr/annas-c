#include <string.h>

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

/* True if every byte of the non-empty label `l` (label_ok has passed) is a digit. */
static bool all_digits(core_str_t l) {
    size_t i;
    for (i = 0; i < l.len; i++) {
        if (!is_digit(l.ptr[i])) {
            return false;
        }
    }
    return true;
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
    if (!all_digits(l) || (l.len > 1 && l.ptr[0] == '0')) {
        return false;
    }
    for (i = 0; i < l.len; i++) {
        v = v * 10u + (unsigned)(l.ptr[i] - '0');
        if (v > 255u) {
            return false;
        }
    }
    return true;
}

/* Takes the next dot-separated label off *rest. After the last one, *rest is not used again. */
static void next_label(core_str_t *rest, core_str_t *label, bool *last) {
    const char *dot = memchr(rest->ptr, '.', rest->len);
    size_t n;
    if (dot == NULL) {
        *label = *rest;
        *last = true;
        return;
    }
    n = (size_t)(dot - rest->ptr);
    *label = (core_str_t){rest->ptr, n};
    rest->ptr += n + 1;
    rest->len -= n + 1;
    *last = false;
}

/* Walks the labels: checks each, counts them, reports the last. */
static bool walk_labels(core_str_t s, unsigned *count, core_str_t *last_label, bool *quad_ok) {
    core_str_t rest = s;
    core_str_t l;
    bool last;
    *count = 0;
    *quad_ok = true;
    do {
        next_label(&rest, &l, &last);
        if (!label_ok(l)) {
            return false;
        }
        (*count)++;
        if (!octet_ok(l)) {
            *quad_ok = false;
        }
    } while (!last);
    *last_label = l;
    return true;
}

bool http_host_valid(core_str_t s) {
    unsigned count;
    core_str_t last_label;
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
    if (p.len < 1 || p.ptr[0] == '0') {
        return HTTP_E_URL;
    }
    for (i = 0; i < p.len; i++) {
        if (!is_digit(p.ptr[i])) {
            return HTTP_E_URL;
        }
        v = v * 10u + (unsigned)(p.ptr[i] - '0');
        if (v > 65535u) {
            return HTTP_E_URL;
        }
    }
    *port = (uint16_t)v;
    return HTTP_OK;
}

/* "host[:port]". Userinfo ('@') and brackets fail the host or port check, so they need no test of their own;
 * `auth.ptr` is not NULL (both callers slice a URL). */
http_status_t http_authority_parse(core_str_t auth, core_str_t *host, uint16_t *port) {
    const char *colon = memchr(auth.ptr, ':', auth.len);
    core_str_t h = {auth.ptr, colon == NULL ? auth.len : (size_t)(colon - auth.ptr)};
    http_status_t st = HTTP_OK;
    uint16_t pt = 443;
    if (!http_host_valid(h)) {
        return HTTP_E_URL;
    }
    if (colon != NULL) {
        st = port_parse((core_str_t){colon + 1, auth.len - h.len - 1}, &pt);
    }
    if (st != HTTP_OK) {
        return st;
    }
    *host = h;
    *port = pt;
    return HTTP_OK;
}

enum { URL_SCHEME_LEN = 8 }; /* strlen("https://") */

static bool scheme_https(core_str_t url) {
    static const char k_letters[] = "https";
    size_t i;
    if (url.len < URL_SCHEME_LEN) {
        return false;
    }
    for (i = 0; i < sizeof k_letters - 1; i++) {
        /* k_letters holds only letters, so OR-ing 0x20 matches exactly each letter's two cases. */
        if (((unsigned char)url.ptr[i] | 0x20u) != (unsigned char)k_letters[i]) {
            return false;
        }
    }
    return memcmp(url.ptr + 5, "://", 3) == 0;
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
    const char *slash;
    core_str_t rest;
    http_url_t u;
    http_status_t st;
    if (out == NULL || (url.ptr == NULL && url.len > 0)) {
        return HTTP_E_ARG;
    }
    if (!scheme_https(url)) {
        return HTTP_E_SCHEME;
    }
    (void)core_str_split_once(url, LIT("://"), NULL, &rest); /* scheme_https found "://" at index 5 */
    /* The authority ends at the first '/'. A '?' or '#' before it is not valid in a host or port, and no '/' at
     * all is HTTP_E_URL too, so every case other than a valid authority and a '/' is the one error. */
    slash = memchr(rest.ptr, '/', rest.len);
    if (slash == NULL) {
        return HTTP_E_URL;
    }
    i = (size_t)(slash - url.ptr);
    st = http_authority_parse((core_str_t){rest.ptr, (size_t)(slash - rest.ptr)}, &u.host, &u.port);
    if (st != HTTP_OK) {
        return st;
    }
    /* A bad byte in the authority already failed the host or port check; the '/' itself is fine. */
    if (url.len > HTTP_MAX_URL || url_has_bad_byte((core_str_t){slash + 1, url.len - i - 1})) {
        return HTTP_E_URL;
    }
    u.target = (core_str_t){url.ptr + i, fragment_start(url, i) - i};
    *out = u;
    return HTTP_OK;
}
