/* Tests for url/url.h: ANNAS_BASE_URL validation and normalisation (spec section 1). */
#include <stddef.h>
#include <string.h>

#include "ctest/ctest.h"
#include "url/url.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define EMPTY ((core_str_t){NULL, 0})

static char store[512];
static core_buf_t B;

static void fresh(size_t cap) {
    memset(store, 0xA5, sizeof store);
    core_buf_init(&B, store, cap);
}

static void norm_ok(ctest_t *t, const char *in, size_t inlen, const char *ex, size_t exlen) {
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_base_normalize((core_str_t){in, inlen}, &B), URL_OK);
    CHECK_EQ_U64(t, core_buf_len(&B), exlen);
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), ex, exlen);
    CHECK(t, !core_buf_overflow(&B));
}
#define OK(t, in, ex) norm_ok((t), (in), sizeof(in) - 1, (ex), sizeof(ex) - 1)

/* Invalid: URL_E_INVALID and a buffer holding "pre" is left exactly as it was. */
static void norm_bad(ctest_t *t, const char *in, size_t inlen) {
    fresh(sizeof store);
    core_buf_put_str(&B, LIT("pre"));
    CHECK_EQ_I64(t, url_base_normalize((core_str_t){in, inlen}, &B), URL_E_INVALID);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "pre");
    CHECK(t, !core_buf_overflow(&B));
}
#define BAD(t, in) norm_bad((t), (in), sizeof(in) - 1)

static void t_default_when_empty(ctest_t *t) {
    OK(t, "", "annas-archive.gl");
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_base_normalize(EMPTY, &B), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "annas-archive.gl");
    CHECK_EQ_STR(t, URL_DEFAULT_BASE, "annas-archive.gl");
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_base_normalize((core_str_t){"junk", 0}, &B), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "annas-archive.gl");
}

static void t_plain_domains_accepted(ctest_t *t) {
    OK(t, "annas-archive.gl", "annas-archive.gl");
    OK(t, "annas-archive.org", "annas-archive.org");
    OK(t, "localhost", "localhost");
    OK(t, "a", "a");
    OK(t, "0", "0");
    OK(t, "127.0.0.1", "127.0.0.1");
    OK(t, "xn--80ak6aa92e.com", "xn--80ak6aa92e.com");
    OK(t, "a-b.c-d.e", "a-b.c-d.e");
    OK(t, "mirror1.example.net", "mirror1.example.net");
    OK(t, "a..b", "a..b");   /* the spec only constrains the character set */
    OK(t, "-", "-");
    OK(t, ".", ".");
    OK(t, "http", "http");   /* looks like a scheme but has no "://" */
    OK(t, "https", "https");
}

static void t_scheme_and_trailing_slash_stripped(ctest_t *t) {
    OK(t, "https://annas-archive.gl", "annas-archive.gl");
    OK(t, "http://annas-archive.gl", "annas-archive.gl");
    OK(t, "https://annas-archive.gl/", "annas-archive.gl");
    OK(t, "http://annas-archive.gl/", "annas-archive.gl");
    OK(t, "annas-archive.gl/", "annas-archive.gl");
    OK(t, "https://a", "a");
    OK(t, "https://a/", "a");
    OK(t, "http://127.0.0.1/", "127.0.0.1");
    OK(t, "https://http", "http");
    OK(t, "http://https/", "https");
}

static void t_invalid_domains(ctest_t *t) {
    BAD(t, "Example.org");
    BAD(t, "example.ORG");
    BAD(t, "HTTPS://example.org");
    BAD(t, "Https://example.org");
    BAD(t, "HTTP://example.org");
    BAD(t, "x_y.org");
    BAD(t, "x.org:8080");
    BAD(t, "https://x.org:8080");
    BAD(t, "x.org/path");
    BAD(t, "https://x.org/path/");
    BAD(t, "x.org//");
    BAD(t, "https://x.org//");
    BAD(t, "//x.org");
    BAD(t, "ftp://x.org");
    BAD(t, "https:/x.org");
    BAD(t, "https//x.org");
    BAD(t, "https:x.org");
    BAD(t, "httpx://y");
    BAD(t, "https://http://x.org");
    BAD(t, "https://https://x.org");
    BAD(t, "http://https://x.org");
    BAD(t, "x.org?q=1");
    BAD(t, "x.org#frag");
    BAD(t, "user@x.org");
    BAD(t, "https://user:pw@x.org");
    BAD(t, "[::1]");
    BAD(t, "x org");
    BAD(t, "x.org\\");
    BAD(t, "x.org%2f");
    BAD(t, "x.org;y");
    BAD(t, "x.org,y");
    BAD(t, "x+y.org");
    BAD(t, "x=y.org");
    BAD(t, "\xc3\xa9.org");
    BAD(t, "x.\xff");
}

static void t_nothing_left_after_stripping(ctest_t *t) {
    BAD(t, "https://");
    BAD(t, "http://");
    BAD(t, "/");
    BAD(t, "//");
    BAD(t, "http:///");
    BAD(t, "https:///");
    BAD(t, "://");
}

static void t_whitespace_and_nul_are_not_trimmed(ctest_t *t) {
    BAD(t, " x.org");
    BAD(t, "x.org ");
    BAD(t, " ");
    BAD(t, "x.org\n");
    BAD(t, "x.org\r\n");
    BAD(t, "\tx.org");
    BAD(t, "https://x.org/ ");
    BAD(t, "https:// x.org");
    BAD(t, "x.org/\n");
    BAD(t, "x.org\0");
    BAD(t, "\0");
    BAD(t, "x\0.org");
    BAD(t, "https://x.org\0");
    BAD(t, "https://x.org/\0");
}

static void t_validity_per_byte(ctest_t *t) {
    unsigned c;
    for (c = 0; c < 256; c++) {
        char one[1] = {(char)c};
        char mid[3] = {'a', (char)c, 'b'};
        int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        CHECK_EQ_I64(t, url_base_is_valid((core_str_t){one, 1}), ok);
        CHECK_EQ_I64(t, url_base_is_valid((core_str_t){mid, 3}), ok);
    }
    CHECK(t, !url_base_is_valid(EMPTY));
    CHECK(t, !url_base_is_valid(LIT("")));
    CHECK(t, !url_base_is_valid((core_str_t){"abc", 0}));
    CHECK(t, url_base_is_valid(LIT("annas-archive.gl")));
    CHECK(t, url_base_is_valid((core_str_t){"abc.org!", 7}));   /* only len bytes count */
    CHECK(t, !url_base_is_valid(LIT("a/")));
    CHECK(t, !url_base_is_valid(LIT("https://a")));
    CHECK(t, !url_base_is_valid(LIT("a\0b")));
}

static void t_appends_after_existing_text(ctest_t *t) {
    fresh(sizeof store);
    core_buf_put_str(&B, LIT("host="));
    CHECK_EQ_I64(t, url_base_normalize(LIT("https://x.org/"), &B), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "host=x.org");
}

static void t_capacity_boundaries(ctest_t *t) {
    fresh(17);   /* 16 bytes + NUL */
    CHECK_EQ_I64(t, url_base_normalize(LIT("annas-archive.gl"), &B), URL_OK);
    fresh(16);
    CHECK_EQ_I64(t, url_base_normalize(LIT("annas-archive.gl"), &B), URL_E_OVERFLOW);
    CHECK(t, core_buf_overflow(&B));
    fresh(17);
    CHECK_EQ_I64(t, url_base_normalize(EMPTY, &B), URL_OK);   /* the default has the same size */
    fresh(16);
    CHECK_EQ_I64(t, url_base_normalize(EMPTY, &B), URL_E_OVERFLOW);
    CHECK(t, core_buf_overflow(&B));
    fresh(17);   /* the stripped scheme does not count toward the size */
    CHECK_EQ_I64(t, url_base_normalize(LIT("https://annas-archive.gl/"), &B), URL_OK);
    fresh(2);
    CHECK_EQ_I64(t, url_base_normalize(LIT("a"), &B), URL_OK);
    fresh(1);
    CHECK_EQ_I64(t, url_base_normalize(LIT("a"), &B), URL_E_OVERFLOW);
    fresh(0);
    CHECK_EQ_I64(t, url_base_normalize(LIT("a"), &B), URL_E_OVERFLOW);
}

static void t_invalid_beats_overflow(ctest_t *t) {
    fresh(2);
    CHECK_EQ_I64(t, url_base_normalize(LIT("Bad_Domain.example"), &B), URL_E_INVALID);
    CHECK(t, !core_buf_overflow(&B));
    fresh(2);
    CHECK_EQ_I64(t, url_base_normalize(LIT("good-domain.example"), &B), URL_E_OVERFLOW);   /* control */
}

static void t_bad_arguments(ctest_t *t) {
    fresh(64);
    CHECK_EQ_I64(t, url_base_normalize(LIT("x.org"), &B), URL_OK);   /* control */
    CHECK_EQ_I64(t, url_base_normalize(LIT("x.org"), NULL), URL_E_ARG);
    CHECK_EQ_I64(t, url_base_normalize(EMPTY, NULL), URL_E_ARG);
    CHECK_EQ_I64(t, url_base_normalize((core_str_t){NULL, 4}, &B), URL_E_ARG);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "x.org");
}

CTEST_SUITE("url_base",
    {"default_when_empty", t_default_when_empty},
    {"plain_domains_accepted", t_plain_domains_accepted},
    {"scheme_and_trailing_slash_stripped", t_scheme_and_trailing_slash_stripped},
    {"invalid_domains", t_invalid_domains},
    {"nothing_left_after_stripping", t_nothing_left_after_stripping},
    {"whitespace_and_nul_are_not_trimmed", t_whitespace_and_nul_are_not_trimmed},
    {"validity_per_byte", t_validity_per_byte},
    {"appends_after_existing_text", t_appends_after_existing_text},
    {"capacity_boundaries", t_capacity_boundaries},
    {"invalid_beats_overflow", t_invalid_beats_overflow},
    {"bad_arguments", t_bad_arguments})
