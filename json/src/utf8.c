#include "core/parse.h"
#include "json/src/internal.h"

static bool cont(unsigned char c, unsigned char lo, unsigned char hi) { return c >= lo && c <= hi; }

/* Required length and the allowed range of the second byte for lead byte c; len 0 when c cannot lead. */
static size_t lead_info(unsigned char c, unsigned char *lo, unsigned char *hi) {
    *lo = 0x80;
    *hi = 0xBF;
    if (c < 0x80) {
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF) {
        return 2;
    }
    if (c >= 0xE0 && c <= 0xEF) {
        *lo = (c == 0xE0) ? 0xA0 : 0x80;
        *hi = (c == 0xED) ? 0x9F : 0xBF;
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        *lo = (c == 0xF0) ? 0x90 : 0x80;
        *hi = (c == 0xF4) ? 0x8F : 0xBF;
        return 4;
    }
    return 0;
}

size_t json_utf8_seq_len(const unsigned char *p, size_t n) {
    unsigned char lo, hi;
    size_t len, k;
    len = lead_info(p[0], &lo, &hi);
    if (len == 0 || len > n) {
        return 0;
    }
    if (len >= 2 && !cont(p[1], lo, hi)) {
        return 0;
    }
    for (k = 2; k < len; k++) {
        if (!cont(p[k], 0x80, 0xBF)) {
            return 0;
        }
    }
    return len;
}

/* ---- string decoding ---- */

/* The input byte at pos, or NULL at and past the end. */
static const unsigned char *byte_at(const json_parser_t *p, size_t pos) { return pos < p->n ? p->s + pos : NULL; }

bool json_byte_is(const json_parser_t *p, size_t pos, unsigned char ch) {
    const unsigned char *b = byte_at(p, pos);
    return b != NULL && *b == ch;
}

static json_err_t fail(json_parser_t *p, json_err_t e, size_t off) {
    p->err = off;
    return e;
}

/* Hex value of the 4 digits at pos. Returns SIZE_MAX when valid, else the offset of the first bad digit; at
 * the end of the input that is p->n (pos <= p->n on entry). */
static size_t hex4(const json_parser_t *p, size_t pos, uint32_t *cp) {
    uint32_t v = 0;
    size_t k;
    const unsigned char *b;
    for (k = 0; k < 4; k++) {
        b = byte_at(p, pos + k);
        if (b == NULL || core_parse_hex_nibble((char)*b) < 0) {
            return pos + k;
        }
        v = (v << 4) | (uint32_t)core_parse_hex_nibble((char)*b);
    }
    *cp = v;
    return SIZE_MAX;
}

static void put(char *dst, size_t at, unsigned char c) {
    if (dst != NULL) {
        dst[at] = (char)c;
    }
}

/* Writes code point cp as UTF-8 at dst[at]; returns the byte count. */
static size_t put_cp(char *dst, size_t at, uint32_t cp) {
    if (cp < 0x80) {
        put(dst, at, (unsigned char)cp);
        return 1;
    }
    if (cp < 0x800) {
        put(dst, at, (unsigned char)(0xC0 | (cp >> 6)));
        put(dst, at + 1, (unsigned char)(0x80 | (cp & 0x3F)));
        return 2;
    }
    if (cp < 0x10000) {
        put(dst, at, (unsigned char)(0xE0 | (cp >> 12)));
        put(dst, at + 1, (unsigned char)(0x80 | ((cp >> 6) & 0x3F)));
        put(dst, at + 2, (unsigned char)(0x80 | (cp & 0x3F)));
        return 3;
    }
    put(dst, at, (unsigned char)(0xF0 | (cp >> 18)));
    put(dst, at + 1, (unsigned char)(0x80 | ((cp >> 12) & 0x3F)));
    put(dst, at + 2, (unsigned char)(0x80 | ((cp >> 6) & 0x3F)));
    put(dst, at + 3, (unsigned char)(0x80 | (cp & 0x3F)));
    return 4;
}

/* A well-formed "\uDC00..\uDFFF" escape at pos? */
static bool low_escape_at(const json_parser_t *p, size_t pos, uint32_t *lo) {
    if (!json_byte_is(p, pos, '\\') || !json_byte_is(p, pos + 1, 'u')) {
        return false;
    }
    if (hex4(p, pos + 2, lo) != SIZE_MAX) {
        return false;
    }
    return *lo >= 0xDC00 && *lo <= 0xDFFF;
}

/* \uXXXX with p->i at the backslash. Appends at dst[*o]. */
static json_err_t esc_unicode(json_parser_t *p, char *dst, size_t *o) {
    size_t b = p->i, pos = b + 6, bad;
    uint32_t cp, lo;
    bad = hex4(p, b + 2, &cp);
    if (bad != SIZE_MAX) {
        return fail(p, JSON_E_SYNTAX, bad);
    }
    if (cp >= 0xD800 && cp <= 0xDBFF) {
        if (!low_escape_at(p, pos, &lo)) {
            return fail(p, JSON_E_UTF8, b);
        }
        cp = 0x10000 + ((cp & 0x3FF) << 10) + (lo & 0x3FF);
        pos += 6;
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        return fail(p, JSON_E_UTF8, b);
    }
    *o += put_cp(dst, *o, cp);
    p->i = pos;
    return JSON_OK;
}

/* Simple one-letter escape: the decoded byte, or 0 when c is not one. */
static unsigned char simple_escape(unsigned char c) {
    switch (c) {
    case '"': return '"';
    case '\\': return '\\';
    case '/': return '/';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    default: return 0;
    }
}

static json_err_t esc(json_parser_t *p, char *dst, size_t *o) {
    size_t b = p->i;
    const unsigned char *c = byte_at(p, b + 1);
    unsigned char r;
    if (c == NULL) {
        return fail(p, JSON_E_SYNTAX, b + 1);
    }
    if (*c == 'u') {
        return esc_unicode(p, dst, o);
    }
    r = simple_escape(*c);
    if (r == 0) {
        return fail(p, JSON_E_SYNTAX, b + 1);
    }
    put(dst, *o, r);
    *o += 1;
    p->i = b + 2;
    return JSON_OK;
}

/* One non-quote step of a string at p->i. */
static json_err_t str_step(json_parser_t *p, char *dst, size_t *o) {
    unsigned char c = p->s[p->i];
    size_t len, k;
    if (c == '\\') {
        return esc(p, dst, o);
    }
    if (c < 0x20) {
        return fail(p, JSON_E_SYNTAX, p->i);
    }
    len = json_utf8_seq_len(p->s + p->i, p->n - p->i);
    if (len == 0) {
        return fail(p, JSON_E_UTF8, p->i);
    }
    for (k = 0; k < len; k++) {
        put(dst, *o + k, p->s[p->i + k]);
    }
    *o += len;
    p->i += len;
    return JSON_OK;
}

json_err_t json_str_decode(json_parser_t *p, char *dst, size_t *out_len) {
    size_t start = p->i, o = 0;
    json_err_t e;
    p->i++;
    for (;;) {
        if (p->i >= p->n) {
            return fail(p, JSON_E_SYNTAX, p->n);
        }
        if (p->s[p->i] == '"') {
            p->i++;
            *out_len = o;
            return JSON_OK;
        }
        e = str_step(p, dst, &o);
        if (e != JSON_OK) {
            return e;
        }
        if (o > p->lim.max_string) {
            return fail(p, JSON_E_LIMIT, start);
        }
    }
}
