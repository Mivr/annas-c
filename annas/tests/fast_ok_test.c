/* annas_fast_download_url: successful responses (status 200), framings, account info, limits. */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

static char g_body[70000];
static char g_acct[4200];

static annas_code_t call_json(kit_t *k, const char *json) {
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "Content-Type: application/json\r\n", json));
    annas_request_t r = kit_req(KIT_MD5);
    return kit_fast(k, &r);
}

static void t_account_is_compacted(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_ok(t, k, call_json(k,
        "{\n  \"download_url\": \"https://dl.example.org/f/1\",\n"
        "  \"account_fast_download_info\": {\n    \"downloads_left\" : 24,\n    \"downloads_per_day\": 25,\n"
        "    \"recently_downloaded_md5s\": [ \"abc\", \"def\" ],\n    \"ratio\": 1.50,\n    \"big\": 1E+2,\n"
        "    \"n\": null,\n    \"b\": true,\n    \"nested\": { \"z\": [], \"a\": {} },\n    \"s\": \"q\\\"\\u00e9\\n\"\n  }\n}"));
    CHECK_EQ_STR(t, k->fast.url, "https://dl.example.org/f/1");
    CHECK_EQ_STR(t, k->fast.account,
                 "{\"downloads_left\":24,\"downloads_per_day\":25,\"recently_downloaded_md5s\":[\"abc\",\"def\"],"
                 "\"ratio\":1.50,\"big\":1E+2,\"n\":null,\"b\":true,\"nested\":{\"z\":[],\"a\":{}},\"s\":\"q\\\"\xc3\xa9\\u000a\"}");
    CHECK_EQ_U64(t, k->fast.account_len, strlen(k->fast.account));
}

static void t_account_absent_or_not_an_object(ctest_t *t) {
    static const char *const bodies[] = {
        "{\"download_url\":\"https://dl.example.org/f/1\"}",
        "{\"download_url\":\"https://dl.example.org/f/1\",\"account_fast_download_info\":null}",
        "{\"download_url\":\"https://dl.example.org/f/1\",\"account_fast_download_info\":\"x\"}",
        "{\"download_url\":\"https://dl.example.org/f/1\",\"account_fast_download_info\":[1]}",
        "{\"download_url\":\"https://dl.example.org/f/1\",\"account_fast_download_info\":5}"};
    for (size_t i = 0; i < sizeof bodies / sizeof bodies[0]; i++) {
        kit_t *k = kit_begin();
        kit_check_ok(t, k, call_json(k, bodies[i]));
        CHECK_EQ_STR(t, k->fast.url, "https://dl.example.org/f/1");
        CHECK_EQ_STR(t, k->fast.account, "");
        CHECK_EQ_U64(t, k->fast.account_len, 0);
    }
    kit_t *k = kit_begin(); /* empty object is an object */
    kit_check_ok(t, k, call_json(k, "{\"download_url\":\"https://dl.example.org/f/1\",\"account_fast_download_info\":{}}"));
    CHECK_EQ_STR(t, k->fast.account, "{}");
    CHECK_EQ_U64(t, k->fast.account_len, 2);
}

static void t_unknown_keys_and_error_member_are_ignored(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_ok(t, k, call_json(k,
        "{\"///download_url\":[\"This API is intended as a stable JSON API\",\"Example: /dyn/api/fast_download.json?md5=d6e1dc51&key=YOUR_SECRET_KEY\"],"
        "\"extra\":{\"x\":[1,2,3]},\"error\":\"ignored\",\"account_fast_download_info\":{\"downloads_left\":1},"
        "\"download_url\":\"https://dl.example.org/f/1\"}"));
    CHECK_EQ_STR(t, k->fast.url, "https://dl.example.org/f/1");
    CHECK_EQ_STR(t, k->fast.account, "{\"downloads_left\":1}");
}

static void t_url_is_decoded_and_not_validated(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_ok(t, k, call_json(k, "{\"download_url\":\"https:\\/\\/dl.example.org\\/f\\u0041?a=1&b=\\u00e9\"}"));
    CHECK_EQ_STR(t, k->fast.url, "https://dl.example.org/fA?a=1&b=\xc3\xa9");
    CHECK_EQ_U64(t, k->fast.url_len, 34);
    k = kit_begin();
    kit_check_ok(t, k, call_json(k, "{\"download_url\":\"http://insecure.example.org/x\"}"));
    CHECK_EQ_STR(t, k->fast.url, "http://insecure.example.org/x");
    k = kit_begin();
    kit_check_ok(t, k, call_json(k, "{\"download_url\":\"x\"}"));
    CHECK_EQ_STR(t, k->fast.url, "x");
    CHECK_EQ_U64(t, k->fast.url_len, 1);
}

static void t_url_length_limit(ctest_t *t) {
    static char url[2200];
    memset(url, 'u', sizeof url);
    memcpy(url, "https://h/", 10);
    kit_t *k = kit_begin();
    url[2048] = '\0';
    kit_check_ok(t, k, call_json(k, KIT_FMT(k, "{\"download_url\":\"%s\"}", url)));
    CHECK_EQ_U64(t, k->fast.url_len, 2048);
    CHECK_EQ_MEM(t, k->fast.url, 2049, url, 2049);
    k = kit_begin();
    url[2048] = 'u';
    url[2049] = '\0';
    kit_check_err(t, k, call_json(k, KIT_FMT(k, "{\"download_url\":\"%s\"}", url)), ANNAS_E_PARSE, 0,
                  "download URL is longer than 2048 bytes");
}

static void t_account_size_limit(ctest_t *t) {
    memset(g_acct, 'x', sizeof g_acct);
    kit_t *k = kit_begin();
    int n = snprintf(g_body, sizeof g_body, "{\"download_url\":\"https://h/1\",\"account_fast_download_info\":{\"a\":\"%.4088s\"}}", g_acct);
    CHECK(t, n > 0);
    kit_check_ok(t, k, call_json(k, g_body)); /* compact form is exactly 4096 bytes */
    CHECK_EQ_U64(t, k->fast.account_len, 4096);
    CHECK(t, k->fast.account[0] == '{' && k->fast.account[4095] == '}' && k->fast.account[4096] == '\0');
    k = kit_begin();
    n = snprintf(g_body, sizeof g_body, "{\"download_url\":\"https://h/1\",\"account_fast_download_info\":{ \"a\" : \"%.4089s\" }}", g_acct);
    CHECK(t, n > 0);
    kit_check_err(t, k, call_json(k, g_body), ANNAS_E_PARSE, 0, "account info is larger than 4096 bytes");
}

static void run_framing(ctest_t *t, kit_t *k) {
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_fast(k, &r));
    CHECK_EQ_STR(t, k->fast.url, "https://dl.example.org/f/1");
    CHECK_EQ_STR(t, k->fast.account, "{\"downloads_left\":2}");
}

#define JSON1 "{\"download_url\":\"https://dl.example.org/f/1\",\"account_fast_download_info\":{\"downloads_left\":2}}"

static void t_one_byte_reads(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_connect(k, "annas-archive.gl", 443);
    kit_expect_api(k, KIT_MD5, "");
    kit_respond(k, kit_http(k, 200, "OK", "", JSON1), 1);
    run_framing(t, k);
}

static void t_odd_chunk_sizes(ctest_t *t) {
    static const size_t sizes[] = {2, 3, 7, 16, 63, 64, 65, 1000};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        kit_t *k = kit_begin();
        kit_connect(k, "annas-archive.gl", 443);
        kit_expect_api(k, KIT_MD5, "");
        kit_respond(k, kit_http(k, 200, "OK", "X-A: b\r\n", JSON1), sizes[i]);
        run_framing(t, k);
    }
}


/* Splits JSON1 into 4 parts and frames each as a chunk (hex size computed here), plus a trailer. */
static const char *chunked4(kit_t *k, const char *ext) {
    static const char *const parts[4] = {"{\"download_url\":\"https://d", "l.example.org/f/1\",\"account_f",
                                         "ast_download_info\":{\"downloads", "_left\":2}}"};
    static char acc[1024];
    size_t n = 0;
    n += (size_t)snprintf(acc + n, sizeof acc - n, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    for (size_t i = 0; i < 4; i++) {
        n += (size_t)snprintf(acc + n, sizeof acc - n, "%zx%s\r\n%s\r\n", strlen(parts[i]), ext, parts[i]);
    }
    n += (size_t)snprintf(acc + n, sizeof acc - n, "0\r\nX-T: 1\r\n\r\n");
    return kit_put_str(k, acc, n);
}

static void t_chunked_transfer_coding(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_connect(k, "annas-archive.gl", 443);
    kit_expect_api(k, KIT_MD5, "");
    kit_respond(k, chunked4(k, ";ext=1"), 0);
    run_framing(t, k);
}

static void t_chunked_one_byte_reads(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_connect(k, "annas-archive.gl", 443);
    kit_expect_api(k, KIT_MD5, "");
    kit_respond(k, chunked4(k, ""), 1);
    run_framing(t, k);
}

static void t_read_until_close(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_connect(k, "annas-archive.gl", 443);
    kit_expect_api(k, KIT_MD5, "");
    kit_respond(k, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n", 0);
    kit_respond(k, JSON1, 11);
    kit_eof(k);
    run_framing(t, k);
}

static void t_http_1_0_response(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_connect(k, "annas-archive.gl", 443);
    kit_expect_api(k, KIT_MD5, "");
    kit_respond(k, "HTTP/1.0 200 OK\r\n\r\n" JSON1, 0);
    kit_eof(k);
    run_framing(t, k);
}

static size_t fill_json(size_t total) {
    static const char pre[] = "{\"download_url\":\"https://dl.example.org/f/1\",\"pad\":[";
    static const char suf[] = "]}";
    size_t n = 0;
    memcpy(g_body, pre, sizeof pre - 1u);
    n = sizeof pre - 1u;
    size_t remaining = total - n - (sizeof suf - 1u);
    while (remaining >= 1005u) {
        g_body[n++] = '"';
        memset(g_body + n, 'x', 1000);
        n += 1000;
        g_body[n++] = '"';
        g_body[n++] = ',';
        remaining -= 1003u;
    }
    g_body[n++] = '"';
    memset(g_body + n, 'x', remaining - 2u);
    n += remaining - 2u;
    g_body[n++] = '"';
    memcpy(g_body + n, suf, sizeof suf - 1u);
    return n + sizeof suf - 1u;
}

static annas_code_t call_big(kit_t *k, size_t len) {
    kit_connect(k, "annas-archive.gl", 443);
    kit_expect_api(k, KIT_MD5, "");
    kit_respond(k, kit_head(k, 200, "OK", "", len), 0);
    kit_respond_n(k, g_body, len, 4096);
    annas_request_t r = kit_req(KIT_MD5);
    return kit_fast(k, &r);
}

static void t_body_size_limit(ctest_t *t) {
    kit_t *k = kit_begin();
    size_t n = fill_json(65536);
    CHECK_EQ_U64(t, n, 65536);
    kit_check_ok(t, k, call_big(k, n));
    CHECK_EQ_STR(t, k->fast.url, "https://dl.example.org/f/1");
    k = kit_begin();
    n = fill_json(65537);
    CHECK_EQ_U64(t, n, 65537);
    k->allow_unused = true; /* annas may stop reading as soon as the limit is exceeded */
    kit_check_err(t, k, call_big(k, n), ANNAS_E_PARSE, 0, "API response is larger than 65536 bytes");
}

static void t_json_with_leading_and_trailing_whitespace(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_ok(t, k, call_json(k, " \t\r\n{\"download_url\":\"https://dl.example.org/f/1\"}\r\n\t "));
    CHECK_EQ_STR(t, k->fast.url, "https://dl.example.org/f/1");
}

static void t_204_without_body_has_no_url(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n");
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_API, 204, "204: API returned empty download URL");
}

static void t_fixtures_have_expected_sizes(ctest_t *t) {
    CHECK_EQ_U64(t, strlen(kit_fixture_401), 1083);
    CHECK_EQ_U64(t, strlen(kit_fixture_404), 1081);
    CHECK_EQ_U64(t, strlen(kit_fixture_302), 283);
}

CTEST_SUITE("fast_ok",
    {"account_is_compacted", t_account_is_compacted},
    {"account_absent_or_not_an_object", t_account_absent_or_not_an_object},
    {"unknown_keys_and_error_member_are_ignored", t_unknown_keys_and_error_member_are_ignored},
    {"url_is_decoded_and_not_validated", t_url_is_decoded_and_not_validated},
    {"url_length_limit", t_url_length_limit},
    {"account_size_limit", t_account_size_limit},
    {"one_byte_reads", t_one_byte_reads},
    {"odd_chunk_sizes", t_odd_chunk_sizes},
    {"chunked_transfer_coding", t_chunked_transfer_coding},
    {"chunked_one_byte_reads", t_chunked_one_byte_reads},
    {"read_until_close", t_read_until_close},
    {"http_1_0_response", t_http_1_0_response},
    {"body_size_limit", t_body_size_limit},
    {"json_with_leading_and_trailing_whitespace", t_json_with_leading_and_trailing_whitespace},
    {"204_without_body_has_no_url", t_204_without_body_has_no_url},
    {"fixtures_have_expected_sizes", t_fixtures_have_expected_sizes})
