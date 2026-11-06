/* Tests for url/url.h: percent-encoding and decoding. Public header only. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ctest/ctest.h"
#include "url/url.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define EMPTY ((core_str_t){NULL, 0})

static char store[4096];
static core_buf_t B;

static void fresh(size_t cap) {
    memset(store, 0xA5, sizeof store);
    core_buf_init(&B, store, cap);
}

static int unreserved(unsigned c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '.' || c == '_' || c == '~';
}

/* Encodes (in, inlen) into a fresh 512-byte buffer; expects URL_OK and exactly `ex`. */
static void enc(ctest_t *t, const char *in, size_t inlen, const char *ex, size_t exlen) {
    fresh(512);
    CHECK_EQ_I64(t, url_pct_encode(&B, (core_str_t){in, inlen}), URL_OK);
    CHECK_EQ_U64(t, core_buf_len(&B), exlen);
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), ex, exlen);
    CHECK(t, !core_buf_overflow(&B));
}
#define ENC(t, in, ex) enc((t), (in), sizeof(in) - 1, (ex), sizeof(ex) - 1)

static void dec(ctest_t *t, const char *in, size_t inlen, const char *ex, size_t exlen) {
    fresh(512);
    CHECK_EQ_I64(t, url_pct_decode(&B, (core_str_t){in, inlen}), URL_OK);
    CHECK_EQ_U64(t, core_buf_len(&B), exlen);
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), ex, exlen);
    CHECK(t, !core_buf_overflow(&B));
}
#define DEC(t, in, ex) dec((t), (in), sizeof(in) - 1, (ex), sizeof(ex) - 1)

static void t_encode_unreserved_kept(ctest_t *t) {
    ENC(t, "abcxyz", "abcxyz");
    ENC(t, "ABCXYZ", "ABCXYZ");
    ENC(t, "0123456789", "0123456789");
    ENC(t, "-._~", "-._~");
    ENC(t, "a-b.c_d~e", "a-b.c_d~e");
    ENC(t, "The_Quick-Brown.Fox~42", "The_Quick-Brown.Fox~42");
}

static void t_encode_space_is_percent20(ctest_t *t) {
    ENC(t, " ", "%20");
    ENC(t, "hello world", "hello%20world");
    ENC(t, "  ", "%20%20");
    ENC(t, " a ", "%20a%20");
}

static void t_encode_reserved_and_special(ctest_t *t) {
    ENC(t, "+", "%2B");
    ENC(t, "a+b", "a%2Bb");
    ENC(t, "%", "%25");
    ENC(t, "100%", "100%25");
    ENC(t, "%41", "%2541");
    ENC(t, "a/b?c=d&e#f", "a%2Fb%3Fc%3Dd%26e%23f");
    ENC(t, "!", "%21");
    ENC(t, "*", "%2A");
    ENC(t, "'", "%27");
    ENC(t, "(", "%28");
    ENC(t, ")", "%29");
    ENC(t, ";", "%3B");
    ENC(t, ":", "%3A");
    ENC(t, "@", "%40");
    ENC(t, "$", "%24");
    ENC(t, ",", "%2C");
    ENC(t, "[", "%5B");
    ENC(t, "]", "%5D");
    ENC(t, "\"", "%22");
    ENC(t, "<", "%3C");
    ENC(t, ">", "%3E");
    ENC(t, "\\", "%5C");
    ENC(t, "^", "%5E");
    ENC(t, "`", "%60");
    ENC(t, "{", "%7B");
    ENC(t, "|", "%7C");
    ENC(t, "}", "%7D");
    ENC(t, "\x7f", "%7F");
    ENC(t, "\t", "%09");
    ENC(t, "\n", "%0A");
    ENC(t, "\r\n", "%0D%0A");
}

static void t_encode_bytes_are_upper_case_hex(ctest_t *t) {
    ENC(t, "\xab", "%AB");
    ENC(t, "\xff", "%FF");
    ENC(t, "\xcd\xef", "%CD%EF");
    ENC(t, "\x0a", "%0A");
    ENC(t, "\xc3\xa9", "%C3%A9");
    ENC(t, "\xe2\x82\xac", "%E2%82%AC");
    ENC(t, "\xf0\x9f\x98\x80", "%F0%9F%98%80");
    ENC(t, "caf\xc3\xa9 au lait", "caf%C3%A9%20au%20lait");
    ENC(t, "\xd0\x94\xd1\x8e\xd0\xbd\xd0\xb0", "%D0%94%D1%8E%D0%BD%D0%B0");
}

static void t_encode_nul_and_binary(ctest_t *t) {
    ENC(t, "\0", "%00");
    ENC(t, "a\0b", "a%00b");
    ENC(t, "\0\0", "%00%00");
    ENC(t, "\1\2\3", "%01%02%03");
}

static void t_encode_every_byte_value(ctest_t *t) {
    static const char hex[] = "0123456789ABCDEF";
    unsigned c;
    for (c = 0; c < 256; c++) {
        char in[1] = {(char)c};
        char ex[3];
        size_t exlen;
        if (unreserved(c)) {
            ex[0] = (char)c;
            exlen = 1;
        } else {
            ex[0] = '%';
            ex[1] = hex[c >> 4];
            ex[2] = hex[c & 15];
            exlen = 3;
        }
        enc(t, in, 1, ex, exlen);
    }
}

static void t_encode_empty_input(ctest_t *t) {
    fresh(8);
    CHECK_EQ_I64(t, url_pct_encode(&B, EMPTY), URL_OK);
    CHECK_EQ_U64(t, core_buf_len(&B), 0);
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("")), URL_OK);
    CHECK_EQ_I64(t, url_pct_encode(&B, (core_str_t){"xyz", 0}), URL_OK);
    CHECK_EQ_U64(t, core_buf_len(&B), 0);
    CHECK(t, !core_buf_overflow(&B));
    fresh(1);   /* an empty append never overflows, even with no room at all */
    CHECK_EQ_I64(t, url_pct_encode(&B, EMPTY), URL_OK);
    CHECK(t, !core_buf_overflow(&B));
}

static void t_encode_appends_after_existing_text(ctest_t *t) {
    fresh(64);
    core_buf_put_str(&B, LIT("q="));
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("a b")), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "q=a%20b");
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("&")), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "q=a%20b%26");
}

static void t_encode_capacity_boundaries(ctest_t *t) {
    fresh(4);   /* "abc" + NUL fits exactly */
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("abc")), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "abc");
    fresh(3);   /* one short */
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("abc")), URL_E_OVERFLOW);
    CHECK(t, core_buf_overflow(&B));
    fresh(4);   /* "%20" + NUL fits exactly */
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT(" ")), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "%20");
    fresh(3);   /* "%20" does not fit */
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT(" ")), URL_E_OVERFLOW);
    CHECK(t, core_buf_overflow(&B));
    fresh(0);
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("a")), URL_E_OVERFLOW);
    fresh(6);   /* "a%20b" = 5 bytes + NUL */
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("a b")), URL_OK);
    fresh(5);
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("a b")), URL_E_OVERFLOW);
    fresh(7);   /* 3-byte result of 1 byte, then the next one does not fit */
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("  ")), URL_OK);
    fresh(6);
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("  ")), URL_E_OVERFLOW);
}

static void t_encode_into_already_overflowed_buffer(ctest_t *t) {
    fresh(3);
    core_buf_put_str(&B, LIT("toolong"));
    CHECK(t, core_buf_overflow(&B));
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("a")), URL_E_OVERFLOW);
    fresh(8);
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("a")), URL_OK);   /* control */
}

static void t_encode_bad_arguments(ctest_t *t) {
    fresh(16);
    CHECK_EQ_I64(t, url_pct_encode(&B, LIT("ok")), URL_OK);   /* control */
    CHECK_EQ_I64(t, url_pct_encode(NULL, LIT("ok")), URL_E_ARG);
    CHECK_EQ_I64(t, url_pct_encode(NULL, EMPTY), URL_E_ARG);
    CHECK_EQ_I64(t, url_pct_encode(&B, (core_str_t){NULL, 3}), URL_E_ARG);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "ok");
    CHECK(t, !core_buf_overflow(&B));
}

static void t_decode_basic(ctest_t *t) {
    DEC(t, "abc", "abc");
    DEC(t, "%20", " ");
    DEC(t, "hello%20world", "hello world");
    DEC(t, "a%2Bb", "a+b");
    DEC(t, "a%2bb", "a+b");
    DEC(t, "%41%42%43", "ABC");
    DEC(t, "%C3%A9", "\xc3\xa9");
    DEC(t, "%c3%a9", "\xc3\xa9");
    DEC(t, "%c3%A9", "\xc3\xa9");
    DEC(t, "%25", "%");
    DEC(t, "100%25", "100%");
    DEC(t, "%2541", "%41");   /* decoded once only */
    DEC(t, "%2F%3F%23", "/?#");
    DEC(t, "-._~", "-._~");
    DEC(t, "%7E", "~");
}

static void t_decode_plus_and_raw_bytes_are_kept(ctest_t *t) {
    DEC(t, "a+b", "a+b");   /* '+' is NOT a space */
    DEC(t, "+", "+");
    DEC(t, "a b", "a b");
    DEC(t, "a/b?c", "a/b?c");
    DEC(t, "\xc3\xa9", "\xc3\xa9");
    DEC(t, "\xff\x80", "\xff\x80");
    DEC(t, "caf\xc3\xa9%20x", "caf\xc3\xa9 x");
}

static void t_decode_nul(ctest_t *t) {
    DEC(t, "%00", "\0");
    DEC(t, "a%00b", "a\0b");
    DEC(t, "%00%00", "\0\0");
    DEC(t, "a\0b", "a\0b");
}

static void t_decode_empty(ctest_t *t) {
    fresh(4);
    CHECK_EQ_I64(t, url_pct_decode(&B, EMPTY), URL_OK);
    CHECK_EQ_I64(t, url_pct_decode(&B, LIT("")), URL_OK);
    CHECK_EQ_U64(t, core_buf_len(&B), 0);
}

static void t_decode_every_byte_both_cases(ctest_t *t) {
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    unsigned c;
    for (c = 0; c < 256; c++) {
        char a[3] = {'%', lo[c >> 4], lo[c & 15]};
        char b[3] = {'%', up[c >> 4], up[c & 15]};
        char e[3] = {'%', up[c >> 4], lo[c & 15]};
        char ex[1] = {(char)c};
        dec(t, a, 3, ex, 1);
        dec(t, b, 3, ex, 1);
        dec(t, e, 3, ex, 1);
    }
}

/* Malformed input: URL_E_INVALID and nothing appended to a buffer holding "pre". */
static void bad(ctest_t *t, const char *in, size_t inlen) {
    fresh(64);
    core_buf_put_str(&B, LIT("pre"));
    CHECK_EQ_I64(t, url_pct_decode(&B, (core_str_t){in, inlen}), URL_E_INVALID);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "pre");
    CHECK(t, !core_buf_overflow(&B));
}
#define BAD(t, in) bad((t), (in), sizeof(in) - 1)

static void t_decode_malformed_sequences(ctest_t *t) {
    BAD(t, "%");
    BAD(t, "%4");
    BAD(t, "%zz");
    BAD(t, "%4g");
    BAD(t, "%g4");
    BAD(t, "%GG");
    BAD(t, "abc%");
    BAD(t, "abc%2");
    BAD(t, "%%41");
    BAD(t, "%41%");
    BAD(t, "%41%4");
    BAD(t, "%41%zz");
    BAD(t, "a%zzb");
    BAD(t, "% 20");
    BAD(t, "%2 0");
    BAD(t, "%-1");
    BAD(t, "%+1");
    BAD(t, "%\xc3\xa9");
    BAD(t, "%2\0");
    BAD(t, "%\0" "1");
    BAD(t, "%0x");
    BAD(t, "%/0");
    BAD(t, "%:0");
    BAD(t, "%@0");
    BAD(t, "%`0");
    BAD(t, "%0/");
    BAD(t, "%0:");
    BAD(t, "%0@");
    BAD(t, "%0G");
    BAD(t, "%0`");
    BAD(t, "%0g");
}

static void t_decode_each_nonhex_digit_rejected(ctest_t *t) {
    static const char *const hexd = "0123456789abcdefABCDEF";
    unsigned c;
    for (c = 0; c < 256; c++) {
        char s1[3] = {'%', (char)c, '0'};
        char s2[3] = {'%', '0', (char)c};
        if (c != 0 && strchr(hexd, (int)c) != NULL) {
            char ok1[3] = {'%', (char)c, '0'};
            fresh(8);
            CHECK_EQ_I64(t, url_pct_decode(&B, (core_str_t){ok1, 3}), URL_OK);
            CHECK_EQ_U64(t, core_buf_len(&B), 1);
            fresh(8);
            CHECK_EQ_I64(t, url_pct_decode(&B, (core_str_t){s2, 3}), URL_OK);
        } else {
            bad(t, s1, 3);
            bad(t, s2, 3);
        }
    }
}

static void t_decode_malformed_beats_overflow(ctest_t *t) {
    fresh(2);
    CHECK_EQ_I64(t, url_pct_decode(&B, LIT("abcdef%z")), URL_E_INVALID);
    CHECK(t, !core_buf_overflow(&B));
    fresh(2);
    CHECK_EQ_I64(t, url_pct_decode(&B, LIT("abcdefgh")), URL_E_OVERFLOW);   /* control */
    CHECK(t, core_buf_overflow(&B));
}

static void t_decode_capacity_boundaries(ctest_t *t) {
    fresh(4);
    CHECK_EQ_I64(t, url_pct_decode(&B, LIT("%41%42%43")), URL_OK);   /* 3 bytes + NUL */
    CHECK_EQ_STR(t, core_buf_cstr(&B), "ABC");
    fresh(3);
    CHECK_EQ_I64(t, url_pct_decode(&B, LIT("%41%42%43")), URL_E_OVERFLOW);
    CHECK(t, core_buf_overflow(&B));
    fresh(2);
    CHECK_EQ_I64(t, url_pct_decode(&B, LIT("%41")), URL_OK);
    fresh(1);
    CHECK_EQ_I64(t, url_pct_decode(&B, LIT("%41")), URL_E_OVERFLOW);
}

static void t_decode_appends_and_bad_arguments(ctest_t *t) {
    fresh(32);
    core_buf_put_str(&B, LIT("x="));
    CHECK_EQ_I64(t, url_pct_decode(&B, LIT("a%20b")), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "x=a b");
    CHECK_EQ_I64(t, url_pct_decode(NULL, LIT("a")), URL_E_ARG);
    CHECK_EQ_I64(t, url_pct_decode(NULL, EMPTY), URL_E_ARG);
    CHECK_EQ_I64(t, url_pct_decode(&B, (core_str_t){NULL, 2}), URL_E_ARG);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "x=a b");
}

static uint64_t g_state;
static unsigned rnd(void) {   /* xorshift64*, seeded and local to this file */
    g_state ^= g_state >> 12;
    g_state ^= g_state << 25;
    g_state ^= g_state >> 27;
    return (unsigned)((g_state * UINT64_C(0x2545F4914F6CDD1D)) >> 33);
}

static void t_roundtrip_random_bytes(ctest_t *t) {
    unsigned i;
    g_state = UINT64_C(0x1234567890ABCDEF);
    for (i = 0; i < 3000; i++) {
        static char in[80], enc_[512], back[512];
        size_t n = rnd() % 81, k, elen, expect_len = 0;
        core_buf_t e, d;
        int ok_shape = 1;
        for (k = 0; k < n; k++) {
            unsigned r = rnd();
            in[k] = (char)((r & 3) == 0 ? (unsigned)(unsigned char)"-._~ /?#&=+%"[(r >> 2) % 12] : (r >> 8) & 255);
            expect_len += unreserved((unsigned char)in[k]) ? 1 : 3;
        }
        core_buf_init(&e, enc_, sizeof enc_);
        CHECK_EQ_I64(t, url_pct_encode(&e, (core_str_t){in, n}), URL_OK);
        elen = core_buf_len(&e);
        CHECK_EQ_U64(t, elen, expect_len);
        for (k = 0; k < elen; k++) {
            unsigned char c = (unsigned char)enc_[k];
            if (c == '%') {
                if (k + 2 >= elen) { ok_shape = 0; break; }
                if (!strchr("0123456789ABCDEF", enc_[k + 1]) || !strchr("0123456789ABCDEF", enc_[k + 2])) ok_shape = 0;
                k += 2;
            } else if (!unreserved(c)) {
                ok_shape = 0;
            }
        }
        CHECK(t, ok_shape);
        core_buf_init(&d, back, sizeof back);
        CHECK_EQ_I64(t, url_pct_decode(&d, (core_str_t){enc_, elen}), URL_OK);
        CHECK_EQ_U64(t, core_buf_len(&d), n);
        CHECK_EQ_MEM(t, back, core_buf_len(&d), in, n);
    }
}

CTEST_SUITE("url_pct",
    {"encode_unreserved_kept", t_encode_unreserved_kept},
    {"encode_space_is_percent20", t_encode_space_is_percent20},
    {"encode_reserved_and_special", t_encode_reserved_and_special},
    {"encode_bytes_are_upper_case_hex", t_encode_bytes_are_upper_case_hex},
    {"encode_nul_and_binary", t_encode_nul_and_binary},
    {"encode_every_byte_value", t_encode_every_byte_value},
    {"encode_empty_input", t_encode_empty_input},
    {"encode_appends_after_existing_text", t_encode_appends_after_existing_text},
    {"encode_capacity_boundaries", t_encode_capacity_boundaries},
    {"encode_into_already_overflowed_buffer", t_encode_into_already_overflowed_buffer},
    {"encode_bad_arguments", t_encode_bad_arguments},
    {"decode_basic", t_decode_basic},
    {"decode_plus_and_raw_bytes_are_kept", t_decode_plus_and_raw_bytes_are_kept},
    {"decode_nul", t_decode_nul},
    {"decode_empty", t_decode_empty},
    {"decode_every_byte_both_cases", t_decode_every_byte_both_cases},
    {"decode_malformed_sequences", t_decode_malformed_sequences},
    {"decode_each_nonhex_digit_rejected", t_decode_each_nonhex_digit_rejected},
    {"decode_malformed_beats_overflow", t_decode_malformed_beats_overflow},
    {"decode_capacity_boundaries", t_decode_capacity_boundaries},
    {"decode_appends_and_bad_arguments", t_decode_appends_and_bad_arguments},
    {"roundtrip_random_bytes", t_roundtrip_random_bytes})
