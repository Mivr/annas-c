#include <string.h>

#include "http/src/internal.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static bool is_alnum_byte(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* The punctuation of RFC 9110 tchar. */
static bool is_tchar_symbol(unsigned char c) {
    return c != '\0' && strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

static bool is_tchar(unsigned char c) {
    return is_alnum_byte(c) || is_tchar_symbol(c);
}

/* HTAB, SP, 0x21..0x7E and bytes >= 0x80. */
static bool is_text_byte(unsigned char c) {
    return c == '\t' || (c >= 0x20u && c != 0x7Fu);
}

static bool all_text(const unsigned char *s, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (!is_text_byte(s[i])) {
            return false;
        }
    }
    return true;
}

/* "HTTP/1.0 " or "HTTP/1.1 " at the start of a line of n >= 12 bytes. */
static bool status_prefix_ok(const unsigned char *l, size_t n) {
    return n >= 12 && memcmp(l, "HTTP/1.", 7) == 0 && (l[7] == '0' || l[7] == '1') && l[8] == ' ';
}

static bool is_digit_byte(unsigned char c) {
    return c >= '0' && c <= '9';
}

/* The three digits at l[9..11] as a number, or -1. */
static int status_code(const unsigned char *l) {
    if (!is_digit_byte(l[9]) || !is_digit_byte(l[10]) || !is_digit_byte(l[11])) {
        return -1;
    }
    return (l[9] - '0') * 100 + (l[10] - '0') * 10 + (l[11] - '0');
}

/* A code in 200..599, then either nothing or " <reason text>". */
static bool status_rest_ok(const unsigned char *l, size_t n, int code) {
    return code >= 200 && code <= 599 && (n <= 12 || (l[12] == ' ' && all_text(l + 13, n - 13)));
}

http_status_t http_status_line(http_parser_t *p, size_t n, http_event_t *ev) {
    const unsigned char *l = p->line;
    int code;
    if (!status_prefix_ok(l, n)) {
        return HTTP_E_STATUS_LINE;
    }
    code = status_code(l);
    if (code < 0 || !status_rest_ok(l, n, code)) {
        return HTTP_E_STATUS_LINE;
    }
    ev->kind = HTTP_EV_STATUS;
    ev->status = code;
    ev->minor = l[7] - '0';
    ev->reason = (core_str_t){(const char *)l + (n > 12 ? 13 : 12), n > 12 ? n - 13 : 0};
    p->status = code;
    p->flags = (l[7] == '1') ? (p->flags | HTTP_F_MINOR1) : (p->flags & ~HTTP_F_MINOR1);
    p->state = HTTP_ST_HEADERS;
    return HTTP_OK;
}

static http_status_t parse_length(core_str_t v, uint64_t *out) {
    uint64_t acc = 0;
    size_t i;
    if (v.len == 0) {
        return HTTP_E_CONTENT_LENGTH;
    }
    for (i = 0; i < v.len; i++) {
        uint64_t d;
        if (v.ptr[i] < '0' || v.ptr[i] > '9') {
            return HTTP_E_CONTENT_LENGTH;
        }
        d = (uint64_t)(v.ptr[i] - '0');
        if (acc > (UINT64_MAX - d) / 10u) {
            return HTTP_E_CONTENT_LENGTH;
        }
        acc = acc * 10u + d;
    }
    *out = acc;
    return HTTP_OK;
}

static bool no_body_status(int status) {
    return status == 204 || status == 304;
}

static http_status_t check_length_line(http_parser_t *p, core_str_t value) {
    uint64_t v = 0;
    http_status_t st;
    if ((p->flags & HTTP_F_CL) != 0u) {
        return HTTP_E_CONTENT_LENGTH;
    }
    st = parse_length(value, &v);
    if (st != HTTP_OK) {
        return st;
    }
    if ((p->flags & HTTP_F_TE) != 0u || (no_body_status(p->status) && v != 0)) {
        return HTTP_E_FRAMING;
    }
    p->flags |= HTTP_F_CL;
    p->content_length = v;
    return HTTP_OK;
}

static bool is_chunked_ci(core_str_t v) {
    static const char k[] = "chunked";
    size_t i;
    if (v.len != sizeof k - 1) {
        return false;
    }
    for (i = 0; i < v.len; i++) {
        char c = v.ptr[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != k[i]) {
            return false;
        }
    }
    return true;
}

static http_status_t check_encoding_line(http_parser_t *p, core_str_t value) {
    if ((p->flags & (HTTP_F_TE | HTTP_F_CL | HTTP_F_MINOR1)) != HTTP_F_MINOR1 || no_body_status(p->status) ||
        !is_chunked_ci(value)) {
        return HTTP_E_FRAMING;
    }
    p->flags |= HTTP_F_TE;
    return HTTP_OK;
}

static http_status_t framing_line(http_parser_t *p, bool trailer, core_str_t name, core_str_t value) {
    bool cl = core_str_eq(name, LIT("content-length"));
    bool te = core_str_eq(name, LIT("transfer-encoding"));
    if (trailer) {
        return (cl || te) ? HTTP_E_FRAMING : HTTP_OK;
    }
    if (cl) {
        return check_length_line(p, value);
    }
    return te ? check_encoding_line(p, value) : HTTP_OK;
}

/* Index of the colon ending a valid token name, or n when the line has none. */
static size_t name_end(const unsigned char *l, size_t n) {
    size_t i = 0;
    while (i < n && is_tchar(l[i])) {
        i++;
    }
    return (i > 0 && i < n && l[i] == ':') ? i : n;
}

static bool is_ows(unsigned char c) {
    return c == ' ' || c == '\t';
}

static void lower_ascii(unsigned char *l, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        l[i] = (l[i] >= 'A' && l[i] <= 'Z') ? (unsigned char)(l[i] - 'A' + 'a') : l[i];
    }
}

/* Strips optional whitespace from both ends of l[b..e). */
static core_str_t trim_ows(const unsigned char *l, size_t b, size_t e) {
    while (b < e && is_ows(l[b])) {
        b++;
    }
    while (e > b && is_ows(l[e - 1])) {
        e--;
    }
    return (core_str_t){(const char *)l + b, e - b};
}

http_status_t http_header_line(http_parser_t *p, size_t n, bool trailer, http_event_t *ev) {
    unsigned char *l = p->line;
    size_t c;
    http_status_t st;
    if (is_ows(l[0])) {
        return HTTP_E_FOLDING;
    }
    c = name_end(l, n);
    if (c >= n || !all_text(l + c + 1, n - c - 1)) {
        return HTTP_E_HEADER;
    }
    if (p->header_count >= HTTP_MAX_HEADERS) {
        return HTTP_E_TOO_MANY_HEADERS;
    }
    p->header_count++;
    lower_ascii(l, c);
    ev->name = (core_str_t){(const char *)l, c};
    ev->value = trim_ows(l, c + 1, n);
    st = framing_line(p, trailer, ev->name, ev->value);
    if (st != HTTP_OK) {
        return st;
    }
    ev->kind = trailer ? HTTP_EV_TRAILER : HTTP_EV_HEADER;
    return HTTP_OK;
}

http_status_t http_headers_end(http_parser_t *p, http_event_t *ev) {
    ev->kind = HTTP_EV_HEADERS_END;
    if (no_body_status(p->status)) {
        ev->framing = HTTP_FRAMING_NONE;
        p->state = HTTP_ST_END_PENDING;
    } else if ((p->flags & HTTP_F_TE) != 0u) {
        ev->framing = HTTP_FRAMING_CHUNKED;
        p->state = HTTP_ST_CHUNK_SIZE;
    } else if ((p->flags & HTTP_F_CL) != 0u) {
        ev->framing = HTTP_FRAMING_LENGTH;
        ev->content_length = p->content_length;
        p->remaining = p->content_length;
        p->state = p->content_length == 0 ? HTTP_ST_END_PENDING : HTTP_ST_BODY_LENGTH;
    } else {
        ev->framing = HTTP_FRAMING_CLOSE;
        p->state = HTTP_ST_BODY_CLOSE;
    }
    return HTTP_OK;
}
