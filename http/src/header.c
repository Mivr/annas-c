#include <string.h>

#include "http/src/internal.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

/* RFC 9110 tchar: the digits, the letters and the punctuation below. */
static bool is_tchar(unsigned char c) {
    return c != '\0' &&
           strchr("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!#$%&'*+-.^_`|~", c) != NULL;
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

/* The three digits at l[9..11] as a code in 200..599 (first digit 2..5), stored in *code; false otherwise. */
static bool status_code(const unsigned char *l, int *code) {
    if (l[9] < '2' || l[9] > '5' || !is_digit_byte(l[10]) || !is_digit_byte(l[11])) {
        return false;
    }
    *code = (l[9] - '0') * 100 + (l[10] - '0') * 10 + (l[11] - '0');
    return true;
}

/* After the code: either nothing or " <reason text>". */
static bool reason_ok(const unsigned char *l, size_t n) {
    return n <= 12 || (l[12] == ' ' && all_text(l + 13, n - 13));
}

http_status_t http_status_line(http_parser_t *p, size_t n, http_event_t *ev) {
    const unsigned char *l = p->line;
    int code;
    size_t skip;
    if (!status_prefix_ok(l, n) || !status_code(l, &code) || !reason_ok(l, n)) {
        return HTTP_E_STATUS_LINE;
    }
    skip = (n > 12) ? 1u : 0u; /* the single SP before a reason */
    ev->kind = HTTP_EV_STATUS;
    ev->status = code;
    ev->minor = l[7] - '0';
    ev->reason = (core_str_t){(const char *)l + 12 + skip, n - 12 - skip};
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
    uint64_t v;
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
        /* k holds only letters, so OR-ing 0x20 matches exactly each letter's two cases. */
        if (((unsigned char)v.ptr[i] | 0x20u) != (unsigned char)k[i]) {
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

static bool is_ows(unsigned char c) {
    return c == ' ' || c == '\t';
}

/* True if l[0..c) is a token (c > 0); lower-cases it in place. Only token bytes reach the fold, and
 * '[' '\\' ']' are not token bytes, so "< '^'" below is the same as "<= 'Z'". */
static bool lower_token(unsigned char *l, size_t c) {
    size_t i;
    if (c == 0) {
        return false;
    }
    for (i = 0; i < c; i++) {
        if (!is_tchar(l[i])) {
            return false;
        }
        if (l[i] >= 'A' && l[i] < '^') {
            l[i] = (unsigned char)(l[i] - 'A' + 'a');
        }
    }
    return true;
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
    const unsigned char *colon;
    size_t c;
    http_status_t st;
    if (is_ows(l[0])) {
        return HTTP_E_FOLDING;
    }
    colon = memchr(l, ':', n);
    if (colon == NULL) {
        return HTTP_E_HEADER;
    }
    c = (size_t)(colon - l);
    if (!all_text(l + c + 1, n - c - 1) || !lower_token(l, c)) {
        return HTTP_E_HEADER;
    }
    if (p->header_count >= HTTP_MAX_HEADERS) {
        return HTTP_E_TOO_MANY_HEADERS;
    }
    p->header_count++;
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
