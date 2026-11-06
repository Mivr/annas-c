#include "http/src/internal.h"

/* Value of a hex digit, or -1. */
static int hex_val(unsigned char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool ext_ok(const unsigned char *s, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if ((s[i] < 0x20u && s[i] != '\t') || s[i] == 0x7Fu) {
            return false;
        }
    }
    return true;
}

http_status_t http_chunk_size_line(http_parser_t *p, size_t n) {
    const unsigned char *l = p->line;
    uint64_t v = 0;
    size_t i = 0;
    while (i < n && hex_val(l[i]) >= 0) {
        if ((v >> 60) != 0u) {
            return HTTP_E_CHUNK;
        }
        v = (v << 4) | (uint64_t)hex_val(l[i]);
        i++;
    }
    if (i == 0 || (i < n && (l[i] != ';' || !ext_ok(l + i + 1, n - i - 1)))) {
        return HTTP_E_CHUNK;
    }
    p->remaining = v;
    p->state = v == 0 ? HTTP_ST_TRAILERS : HTTP_ST_CHUNK_DATA;
    return HTTP_OK;
}
