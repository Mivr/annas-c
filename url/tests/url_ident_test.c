/* Tests for url/url.h: md5 validation/normalisation and DOI recognition (spec sections 3.3, 3.2). */
#include <stddef.h>
#include <string.h>

#include "ctest/ctest.h"
#include "url/url.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define EMPTY ((core_str_t){NULL, 0})
#define GOOD "0123456789abcdef0123456789abcdef"

static int is_hex(unsigned c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static void t_md5_is_valid_accepts_lower_hex(ctest_t *t) {
    CHECK(t, url_md5_is_valid(LIT(GOOD)));
    CHECK(t, url_md5_is_valid(LIT("d41d8cd98f00b204e9800998ecf8427e")));
    CHECK(t, url_md5_is_valid(LIT("00000000000000000000000000000000")));
    CHECK(t, url_md5_is_valid(LIT("ffffffffffffffffffffffffffffffff")));
    CHECK(t, url_md5_is_valid(LIT("99999999999999999999999999999999")));
    CHECK(t, url_md5_is_valid(LIT("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")));
    CHECK(t, url_md5_is_valid((core_str_t){GOOD "zzzz", 32}));   /* only len bytes count */
}

static void t_md5_is_valid_rejects(ctest_t *t) {
    CHECK(t, url_md5_is_valid(LIT(GOOD)));   /* control */
    CHECK(t, !url_md5_is_valid(EMPTY));
    CHECK(t, !url_md5_is_valid(LIT("")));
    CHECK(t, !url_md5_is_valid(LIT("0")));
    CHECK(t, !url_md5_is_valid(LIT("0123456789abcdef0123456789abcde")));      /* 31 */
    CHECK(t, !url_md5_is_valid(LIT("0123456789abcdef0123456789abcdef0")));    /* 33 */
    CHECK(t, !url_md5_is_valid(LIT(GOOD GOOD)));                              /* 64 */
    CHECK(t, !url_md5_is_valid(LIT("0123456789ABCDEF0123456789ABCDEF")));     /* upper case */
    CHECK(t, !url_md5_is_valid(LIT("0123456789abcdef0123456789abcdeF")));
    CHECK(t, !url_md5_is_valid(LIT("A123456789abcdef0123456789abcdef")));
    CHECK(t, !url_md5_is_valid(LIT(" " GOOD)));
    CHECK(t, !url_md5_is_valid(LIT(GOOD "\n")));
    CHECK(t, !url_md5_is_valid((core_str_t){GOOD, 31}));
    CHECK(t, !url_md5_is_valid((core_str_t){GOOD "0", 33}));
}

static void t_md5_is_valid_every_position_and_byte(ctest_t *t) {
    size_t pos;
    unsigned c;
    for (pos = 0; pos < 32; pos++) {
        for (c = 0; c < 256; c++) {
            char s[32];
            int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            memcpy(s, GOOD, 32);
            s[pos] = (char)c;
            CHECK_EQ_I64(t, url_md5_is_valid((core_str_t){s, 32}), ok);
        }
    }
}

static void t_md5_normalize_lowercases(ctest_t *t) {
    CHECK_EQ_U64(t, URL_MD5_BUF, 33);
    char out[URL_MD5_BUF + 8];
    memset(out, 0x5A, sizeof out);
    CHECK(t, url_md5_normalize(LIT("0123456789ABCDEF0123456789ABCDEF"), out));
    CHECK_EQ_STR(t, out, "0123456789abcdef0123456789abcdef");
    CHECK_EQ_U64(t, (unsigned char)out[32], 0);
    CHECK_EQ_U64(t, (unsigned char)out[33], 0x5A);   /* nothing written past the NUL */
    memset(out, 0x5A, sizeof out);
    CHECK(t, url_md5_normalize(LIT("0123456789abcdefABCDEF0123456789"), out));
    CHECK_EQ_STR(t, out, "0123456789abcdefabcdef0123456789");
    CHECK(t, url_md5_normalize(LIT(GOOD), out));
    CHECK_EQ_STR(t, out, GOOD);
    CHECK(t, url_md5_normalize(LIT("D41D8CD98F00B204E9800998ECF8427E"), out));
    CHECK_EQ_STR(t, out, "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(t, url_md5_normalize((core_str_t){"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFzzz", 32}, out));
    CHECK_EQ_STR(t, out, "ffffffffffffffffffffffffffffffff");
}

static void t_md5_normalize_every_position_and_byte(ctest_t *t) {
    size_t pos;
    unsigned c;
    for (pos = 0; pos < 32; pos++) {
        for (c = 0; c < 256; c++) {
            char s[32], out[URL_MD5_BUF], want[33];
            int ok = is_hex(c);
            memcpy(s, GOOD, 32);
            s[pos] = (char)c;
            memset(out, 0x5A, sizeof out);
            CHECK_EQ_I64(t, url_md5_normalize((core_str_t){s, 32}, out), ok);
            if (ok) {
                memcpy(want, GOOD, 32);
                want[pos] = (char)((c >= 'A' && c <= 'F') ? c + 32 : c);
                want[32] = 0;
                CHECK_EQ_STR(t, out, want);
            } else {
                CHECK_EQ_U64(t, (unsigned char)out[0], 0x5A);   /* untouched */
                CHECK_EQ_U64(t, (unsigned char)out[32], 0x5A);
            }
        }
    }
}

static void t_md5_normalize_rejects_and_leaves_output_alone(ctest_t *t) {
    char out[URL_MD5_BUF];
    static const core_str_t bad[] = {
        {"", 0}, {"0", 1}, {"0123456789abcdef0123456789abcde", 31}, {"0123456789abcdef0123456789abcdef0", 33},
        {" 0123456789abcdef0123456789abcdef", 33}, {"0123456789abcdef0123456789abcdef\n", 33},
        {"0123456789abcdef0123456789abcde\0", 32}, {"\0" "123456789abcdef0123456789abcdef", 32},
        {"0123456789abcdef-0123456789abcde", 32}, {"0x23456789abcdef0123456789abcdef", 32}};
    size_t i;
    CHECK(t, url_md5_normalize(LIT(GOOD), out));   /* control */
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memset(out, 0x5A, sizeof out);
        CHECK(t, !url_md5_normalize(bad[i], out));
        CHECK_EQ_U64(t, (unsigned char)out[0], 0x5A);
        CHECK_EQ_U64(t, (unsigned char)out[32], 0x5A);
    }
    memset(out, 0x5A, sizeof out);
    CHECK(t, !url_md5_normalize((core_str_t){NULL, 32}, out));
    CHECK_EQ_U64(t, (unsigned char)out[0], 0x5A);
    CHECK(t, !url_md5_normalize(EMPTY, out));
    CHECK(t, !url_md5_normalize(LIT(GOOD), NULL));
}

/* ---- DOI recognition ---- */

static void doi_ok(ctest_t *t, const char *in, size_t inlen, size_t off, size_t len) {
    core_str_t tr = {"sentinel", 8};
    CHECK(t, url_doi_recognize((core_str_t){in, inlen}, &tr));
    CHECK(t, tr.ptr == in + off);
    CHECK_EQ_U64(t, tr.len, len);
    CHECK(t, url_doi_recognize((core_str_t){in, inlen}, NULL));
}
#define DOK(t, in, off, len) doi_ok((t), (in), sizeof(in) - 1, (off), (len))

static void doi_no(ctest_t *t, const char *in, size_t inlen) {
    static const char sent[] = "sentinel";
    core_str_t tr = {sent, 8};
    CHECK(t, !url_doi_recognize((core_str_t){in, inlen}, &tr));
    CHECK(t, tr.ptr == sent);
    CHECK_EQ_U64(t, tr.len, 8);
    CHECK(t, !url_doi_recognize((core_str_t){in, inlen}, NULL));
}
#define DNO(t, in) doi_no((t), (in), sizeof(in) - 1)

static void t_doi_recognised(ctest_t *t) {
    DOK(t, "10.1000/xyz123", 0, 14);
    DOK(t, "10.1038/nature12373", 0, 19);
    DOK(t, "10.", 0, 3);
    DOK(t, "10.1", 0, 4);
    DOK(t, "10.1002/(SICI)1097-4571(199806)49:8<693::AID-ASI4>3.0.CO;2-O", 0, 60);
    DOK(t, "10.1/a b", 0, 8);   /* inner whitespace is kept */
}

static void t_doi_trimmed(ctest_t *t) {
    DOK(t, " 10.1000/x", 1, 9);
    DOK(t, "10.1000/x ", 0, 9);
    DOK(t, "  10.1000/x  ", 2, 9);
    DOK(t, "\t10.1000/x\n", 1, 9);
    DOK(t, "\r\n\v\f 10.1000/x \f\v\n\r", 5, 9);
    DOK(t, " 10. ", 1, 3);
    DOK(t, "10.1/a b  ", 0, 8);
}

static void t_doi_rejected(ctest_t *t) {
    DOK(t, "10.1000/x", 0, 9);   /* control */
    DNO(t, "");
    DNO(t, " ");
    DNO(t, "   \t\n");
    DNO(t, "10");
    DNO(t, " 10");
    DNO(t, "1.0.1000/x");
    DNO(t, "10,1000/x");
    DNO(t, "10-1000/x");
    DNO(t, "10 .1000/x");
    DNO(t, "10x1000/x");
    DNO(t, "11.1000/x");
    DNO(t, "100.1000/x");
    DNO(t, "010.1000/x");
    DNO(t, "x10.1000/y");
    DNO(t, "doi:10.1000/x");
    DNO(t, "DOI 10.1000/x");
    DNO(t, "https://doi.org/10.1000/x");
    DNO(t, "/10.1000/x");
    DNO(t, "\xc2\xa0" "10.1000/x");   /* NBSP is not whitespace here */
    DNO(t, "\0" "10.1000/x");
    DNO(t, "\"10.1000/x\"");
    DNO(t, "1");
    DNO(t, ".");
    DNO(t, "10\n.1000/x");
}

static void t_doi_every_leading_byte(ctest_t *t) {
    unsigned c;
    for (c = 0; c < 256; c++) {
        char s[8] = {(char)c, '1', '0', '.', '1', '/', 'x', 0};
        char u[7] = {(char)c, '0', '.', '1', '/', 'x', 0};
        int ws = (c == ' ' || (c >= 9 && c <= 13));
        core_str_t tr = {"s", 1};
        CHECK_EQ_I64(t, url_doi_recognize((core_str_t){s, 7}, &tr), ws);
        if (ws) CHECK(t, tr.ptr == s + 1 && tr.len == 6);
        /* c followed by "0.1/x" is never a DOI start, unless c is '1' */
        CHECK_EQ_I64(t, url_doi_recognize((core_str_t){u, 6}, NULL), c == '1');
    }
}

static void t_doi_bad_arguments(ctest_t *t) {
    core_str_t tr = {"s", 1};
    DOK(t, "10.1/x", 0, 6);   /* control */
    CHECK(t, !url_doi_recognize((core_str_t){NULL, 5}, &tr));
    CHECK(t, !url_doi_recognize((core_str_t){NULL, 5}, NULL));
    CHECK(t, !url_doi_recognize(EMPTY, &tr));
    CHECK_EQ_U64(t, tr.len, 1);
}

CTEST_SUITE("url_ident",
    {"md5_is_valid_accepts_lower_hex", t_md5_is_valid_accepts_lower_hex},
    {"md5_is_valid_rejects", t_md5_is_valid_rejects},
    {"md5_is_valid_every_position_and_byte", t_md5_is_valid_every_position_and_byte},
    {"md5_normalize_lowercases", t_md5_normalize_lowercases},
    {"md5_normalize_every_position_and_byte", t_md5_normalize_every_position_and_byte},
    {"md5_normalize_rejects_and_leaves_output_alone", t_md5_normalize_rejects_and_leaves_output_alone},
    {"doi_recognised", t_doi_recognised},
    {"doi_trimmed", t_doi_trimmed},
    {"doi_rejected", t_doi_rejected},
    {"doi_every_leading_byte", t_doi_every_leading_byte},
    {"doi_bad_arguments", t_doi_bad_arguments})
