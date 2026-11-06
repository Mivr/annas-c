/* Tests for url/url.h: redaction of the `key=` query parameter (spec section 1: the key never
 * appears in error text). Keys in this file are made-up strings. Public header only. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ctest/ctest.h"
#include "url/url.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define EMPTY ((core_str_t){NULL, 0})

static char store[2048];
static core_buf_t B;

static void fresh(size_t cap) {
    memset(store, 0xA5, sizeof store);
    core_buf_init(&B, store, cap);
}

static void red(ctest_t *t, const char *in, size_t inlen, const char *ex, size_t exlen) {
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_redact_key(&B, (core_str_t){in, inlen}), URL_OK);
    CHECK_EQ_U64(t, core_buf_len(&B), exlen);
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), ex, exlen);
    CHECK(t, !core_buf_overflow(&B));
}
#define R(t, in, ex) red((t), (in), sizeof(in) - 1, (ex), sizeof(ex) - 1)

static void t_typical_urls(ctest_t *t) {
    CHECK_EQ_STR(t, URL_REDACTED, "REDACTED");
    R(t, "https://annas-archive.gl/dyn/api/fast_download.json?md5=0123456789abcdef0123456789abcdef&key=FAKESECRET",
      "https://annas-archive.gl/dyn/api/fast_download.json?md5=0123456789abcdef0123456789abcdef&key=REDACTED");
    R(t, "?key=SECRET&md5=abc", "?key=REDACTED&md5=abc");
    R(t, "https://x.org/a?key=S", "https://x.org/a?key=REDACTED");
    R(t, "https://x.org/a?md5=m&key=S&z=1", "https://x.org/a?md5=m&key=REDACTED&z=1");
    R(t, "key=SECRET", "key=REDACTED");
    R(t, "key=S", "key=REDACTED");
}

static void t_all_occurrences(ctest_t *t) {
    R(t, "a=1&key=S1&b=2&key=S2", "a=1&key=REDACTED&b=2&key=REDACTED");
    R(t, "key=a&key=b&key=c", "key=REDACTED&key=REDACTED&key=REDACTED");
    R(t, "key=a&key=", "key=REDACTED&key=REDACTED");
    R(t, "x?key=a y?key=b", "x?key=REDACTED y?key=REDACTED");
}

static void t_empty_value_is_replaced(ctest_t *t) {
    R(t, "key=", "key=REDACTED");
    R(t, "?key=", "?key=REDACTED");
    R(t, "?key=&x=1", "?key=REDACTED&x=1");
    R(t, "a=1&key=#f", "a=1&key=REDACTED#f");
    R(t, "key= next", "key=REDACTED next");
}

static void t_value_terminators(ctest_t *t) {
    R(t, "key=S&x=1", "key=REDACTED&x=1");
    R(t, "key=S#frag", "key=REDACTED#frag");
    R(t, "key=S more text", "key=REDACTED more text");
    R(t, "key=S\tmore", "key=REDACTED\tmore");
    R(t, "key=S\nmore", "key=REDACTED\nmore");
    R(t, "key=S\rmore", "key=REDACTED\rmore");
    R(t, "key=S\vmore", "key=REDACTED\vmore");
    R(t, "key=S\fmore", "key=REDACTED\fmore");
    R(t, "key=S\"more", "key=REDACTED\"more");
    R(t, "key=S'more", "key=REDACTED'more");
    R(t, "{\"u\":\"https://x/?md5=a&key=SEC\",\"error\":\"x\"}", "{\"u\":\"https://x/?md5=a&key=REDACTED\",\"error\":\"x\"}");
    R(t, "GET https://x/?key=SEC failed", "GET https://x/?key=REDACTED failed");
    R(t, "{\"error\":\"bad key=ABC123 provided\"}", "{\"error\":\"bad key=REDACTED provided\"}");
}

static void t_value_runs_through_other_bytes(ctest_t *t) {
    R(t, "key=a=b", "key=REDACTED");
    R(t, "key=a%26b&c=1", "key=REDACTED&c=1");
    R(t, "key=key=x", "key=REDACTED");
    R(t, "key=a;x=1", "key=REDACTED");
    R(t, "key=a?b", "key=REDACTED");
    R(t, "key=a/b+c", "key=REDACTED");
    R(t, "key=a\0b&x", "key=REDACTED&x");
    R(t, "key=\xc3\xa9\xff&x", "key=REDACTED&x");
    R(t, "key=a<b>c", "key=REDACTED");
    R(t, "key=a\\b", "key=REDACTED");
}

static void t_boundaries_before_the_name(ctest_t *t) {
    R(t, "a=1;key=S", "a=1;key=REDACTED");
    R(t, "x key=S", "x key=REDACTED");
    R(t, "x\tkey=S", "x\tkey=REDACTED");
    R(t, "x\nkey=S", "x\nkey=REDACTED");
    R(t, "x\rkey=S", "x\rkey=REDACTED");
    R(t, "x\vkey=S", "x\vkey=REDACTED");
    R(t, "x\fkey=S", "x\fkey=REDACTED");
    R(t, "\"key=S\"", "\"key=REDACTED\"");
    R(t, "'key=S'", "'key=REDACTED'");
    R(t, "a#key=S", "a#key=REDACTED");
    R(t, "a=b#c&key=S", "a=b#c&key=REDACTED");
}

static void t_other_names_are_untouched(ctest_t *t) {
    R(t, "monkey=1", "monkey=1");
    R(t, "keys=1", "keys=1");
    R(t, "KEY=1", "KEY=1");
    R(t, "Key=1", "Key=1");
    R(t, "kEy=1", "kEy=1");
    R(t, "key", "key");
    R(t, "key&x=1", "key&x=1");
    R(t, "key:1", "key:1");
    R(t, "apikey=zzz", "apikey=zzz");
    R(t, "secret_key=zzz", "secret_key=zzz");
    R(t, "xkey=1", "xkey=1");
    R(t, "key =1", "key =1");
    R(t, "k=ey", "k=ey");
    R(t, "ke=y", "ke=y");
    R(t, "ey=key", "ey=key");
    R(t, "?a=key", "?a=key");
    R(t, "?a=key=", "?a=key=");   /* "key=" inside a value of another parameter is preceded by '=' */
    R(t, "-key=1", "-key=1");
    R(t, "x.key=1", "x.key=1");
    R(t, "9key=1", "9key=1");
    R(t, "/key=1", "/key=1");
    R(t, "%key=1", "%key=1");
}

static void t_positive_controls_next_to_untouched(ctest_t *t) {
    R(t, "monkey=1&key=1", "monkey=1&key=REDACTED");
    R(t, "keys=1&key=2", "keys=1&key=REDACTED");
    R(t, "KEY=1&key=2", "KEY=1&key=REDACTED");
    R(t, "key&x=1&key=2", "key&x=1&key=REDACTED");
    R(t, "apikey=a&key=b&secret_key=c", "apikey=a&key=REDACTED&secret_key=c");
    R(t, "/key=1 key=2", "/key=1 key=REDACTED");
}

static void t_nothing_to_redact(ctest_t *t) {
    static char big[600];
    R(t, "", "");
    R(t, "https://annas-archive.gl/md5/0123456789abcdef0123456789abcdef", "https://annas-archive.gl/md5/0123456789abcdef0123456789abcdef");
    R(t, "plain text with = and & and ? and #", "plain text with = and & and ? and #");
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_redact_key(&B, EMPTY), URL_OK);
    CHECK_EQ_U64(t, core_buf_len(&B), 0);
    memset(big, 'q', sizeof big);
    red(t, big, sizeof big, big, sizeof big);
}

static void t_secret_value_never_survives(ctest_t *t) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.~%+/=:;?<>[]{}!$()*,@\\^`|";
    uint64_t s = UINT64_C(0xFEEDFACE12345678);
    unsigned i;
    for (i = 0; i < 400; i++) {
        static char in[256], secret[40], ex[256];
        size_t sl = 6 + i % 30, k, n = 0, m = 0;
        static const char pre[] = "https://x.org/a?md5=abc&", suf[] = "&z=1 end";
        for (k = 0; k < sl; k++) {
            s = s * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
            secret[k] = alphabet[(s >> 33) % (sizeof alphabet - 1)];
        }
        memcpy(in, pre, sizeof pre - 1);
        n = sizeof pre - 1;
        memcpy(in + n, "key=", 4);
        n += 4;
        memcpy(in + n, secret, sl);
        n += sl;
        memcpy(in + n, suf, sizeof suf - 1);
        n += sizeof suf - 1;
        memcpy(ex, pre, sizeof pre - 1);
        m = sizeof pre - 1;
        memcpy(ex + m, "key=REDACTED", 12);
        m += 12;
        memcpy(ex + m, suf, sizeof suf - 1);
        m += sizeof suf - 1;
        red(t, in, n, ex, m);
        CHECK_EQ_U64(t, core_str_find((core_str_t){core_buf_cstr(&B), core_buf_len(&B)}, (core_str_t){secret, sl}), CORE_NPOS);
    }
}

static void t_builder_output_redacts_cleanly(ctest_t *t) {
    static char url[512], out[512];
    core_buf_t u, o;
    core_buf_init(&u, url, sizeof url);
    CHECK_EQ_I64(t, url_build_fast_download(&u, LIT("x.org"), LIT("0123456789abcdef0123456789abcdef"),
                                            LIT("FAKE secret&k=2#z")), URL_OK);
    core_buf_init(&o, out, sizeof out);
    CHECK_EQ_I64(t, url_redact_key(&o, (core_str_t){core_buf_cstr(&u), core_buf_len(&u)}), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&o),
                 "https://x.org/dyn/api/fast_download.json?md5=0123456789abcdef0123456789abcdef&key=REDACTED");
}

static void t_capacity_boundaries(ctest_t *t) {
    fresh(13);   /* "key=REDACTED" is 12 bytes */
    CHECK_EQ_I64(t, url_redact_key(&B, LIT("key=SECRET")), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "key=REDACTED");
    fresh(12);
    CHECK_EQ_I64(t, url_redact_key(&B, LIT("key=SECRET")), URL_E_OVERFLOW);
    CHECK(t, core_buf_overflow(&B));
    fresh(8);   /* untouched text of 7 bytes + NUL */
    CHECK_EQ_I64(t, url_redact_key(&B, LIT("abcdefg")), URL_OK);
    fresh(7);
    CHECK_EQ_I64(t, url_redact_key(&B, LIT("abcdefg")), URL_E_OVERFLOW);
    fresh(0);
    CHECK_EQ_I64(t, url_redact_key(&B, LIT("a")), URL_E_OVERFLOW);
    fresh(0);
    CHECK_EQ_I64(t, url_redact_key(&B, EMPTY), URL_OK);
}

static void t_appends_and_bad_arguments(ctest_t *t) {
    fresh(sizeof store);
    core_buf_put_str(&B, LIT("err: "));
    CHECK_EQ_I64(t, url_redact_key(&B, LIT("?key=S")), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "err: ?key=REDACTED");
    CHECK_EQ_I64(t, url_redact_key(NULL, LIT("?key=S")), URL_E_ARG);
    CHECK_EQ_I64(t, url_redact_key(NULL, EMPTY), URL_E_ARG);
    CHECK_EQ_I64(t, url_redact_key(&B, (core_str_t){NULL, 4}), URL_E_ARG);
    CHECK_EQ_STR(t, core_buf_cstr(&B), "err: ?key=REDACTED");
}

CTEST_SUITE("url_redact",
    {"typical_urls", t_typical_urls},
    {"all_occurrences", t_all_occurrences},
    {"empty_value_is_replaced", t_empty_value_is_replaced},
    {"value_terminators", t_value_terminators},
    {"value_runs_through_other_bytes", t_value_runs_through_other_bytes},
    {"boundaries_before_the_name", t_boundaries_before_the_name},
    {"other_names_are_untouched", t_other_names_are_untouched},
    {"positive_controls_next_to_untouched", t_positive_controls_next_to_untouched},
    {"nothing_to_redact", t_nothing_to_redact},
    {"secret_value_never_survives", t_secret_value_never_survives},
    {"builder_output_redacts_cleanly", t_builder_output_redacts_cleanly},
    {"capacity_boundaries", t_capacity_boundaries},
    {"appends_and_bad_arguments", t_appends_and_bad_arguments})
