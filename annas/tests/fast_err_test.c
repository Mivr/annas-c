/* annas_fast_download_url: every API failure mapping (spec 3.3 step 2, section 2) with exact messages. */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define EMPTY_URL "API returned empty download URL"

static char g_big[20000];
static char g_msg[800];

static annas_code_t api(kit_t *k, int status, const char *reason, const char *hdrs, const char *body) {
    kit_api(k, KIT_MD5, "", kit_http(k, status, reason, hdrs, body));
    annas_request_t r = kit_req(KIT_MD5);
    return kit_fast(k, &r);
}

static annas_code_t api_big(kit_t *k, int status, size_t len) {
    kit_connect(k, "annas-archive.gl", 443);
    kit_expect_api(k, KIT_MD5, "");
    kit_respond(k, kit_head(k, status, "X", "", len), 0);
    kit_respond_n(k, g_big, len, 1500);
    annas_request_t r = kit_req(KIT_MD5);
    return kit_fast(k, &r);
}

static void expect_api_err(ctest_t *t, int status, const char *reason, const char *body, const char *msg) {
    kit_t *k = kit_begin();
    kit_check_err(t, k, api(k, status, reason, "", body), ANNAS_E_API, status, msg);
}

static void t_live_fixture_401(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_err(t, k, api(k, 401, "Unauthorized", "Content-Type: application/json\r\n", kit_fixture_401),
                  ANNAS_E_API, 401, "401: Invalid secret key");
}

static void t_live_fixture_404(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_err(t, k, api(k, 404, "Not Found", "Content-Type: application/json\r\n", kit_fixture_404),
                  ANNAS_E_API, 404, "404: Record not found");
}

static void t_live_fixture_302_is_a_challenge(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_err(t, k,
                  api(k, 302, "FOUND", "Location: /search?q=lakos&content=book_any&check=1\r\n", kit_fixture_302),
                  ANNAS_E_BROWSER_CHECK, 0,
                  "the site requires browser verification; use download-by-md5 with a hash found in a browser");
}

static void t_success_status_with_unusable_url(ctest_t *t) {
    static const char *const bodies[] = {"{\"download_url\":null}", "{\"download_url\":\"\"}", "{}",
                                         "{\"download_url\":5}", "{\"download_url\":[\"https://h/x\"]}",
                                         "{\"download_url\":{\"a\":1}}", "{\"download_url\":true}", "[]", "\"x\"", "null",
                                         "7", "{\"url\":\"https://h/x\"}", "{\"Download_URL\":\"https://h/x\"}"};
    for (size_t i = 0; i < COUNT(bodies); i++) {
        expect_api_err(t, 200, "OK", bodies[i], "200: " EMPTY_URL);
    }
    expect_api_err(t, 200, "OK", "", "200: " EMPTY_URL); /* empty body */
}

static void t_only_200_and_204_succeed(ctest_t *t) {
    static const int codes[] = {201, 202, 203, 206, 226, 299, 400, 404, 500};
    for (size_t i = 0; i < COUNT(codes); i++) {
        char want[64];
        snprintf(want, sizeof want, "%d: " EMPTY_URL, codes[i]);
        expect_api_err(t, codes[i], "X", "{\"download_url\":\"https://dl.example.org/f/1\"}", want);
    }
}

static void t_204_cases(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n");
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_API, 204, "204: " EMPTY_URL);
    k = kit_begin(); /* a 204 with a body is a framing error of the http parser */
    kit_api(k, KIT_MD5, "", "HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\nhello");
    k->allow_unused = true;
    kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_PARSE, 0, "malformed HTTP response");
}

static void t_error_text_is_used(ctest_t *t) {
    expect_api_err(t, 400, "Bad Request", "{\"download_url\":null,\"error\":\"Bad md5\"}", "400: Bad md5");
    expect_api_err(t, 200, "OK", "{\"download_url\":null,\"error\":\"Not a member\"}", "200: Not a member");
    expect_api_err(t, 429, "Too Many", "{\"error\":\"Too many requests\"}", "429: Too many requests");
    expect_api_err(t, 500, "ISE", "{\"error\":\"caf\xc3\xa9 \\u00e9 \\\"q\\\"\"}", "500: caf\xc3\xa9 \xc3\xa9 \"q\"");
    expect_api_err(t, 404, "NF", "{\"download_url\":\"https://h/x\",\"error\":\"nope\"}", "404: nope");
    expect_api_err(t, 404, "NF", "{\"error\":\"first\",\"x\":{\"error\":\"nested\"}}", "404: first");
}

static void t_error_text_falls_back(ctest_t *t) {
    static const char *const bodies[] = {"{\"error\":\"\"}", "{\"error\":null}", "{\"error\":5}", "{\"error\":[\"x\"]}",
                                         "{\"error\":{\"a\":1}}", "{\"download_url\":null,\"error\":false}"};
    for (size_t i = 0; i < COUNT(bodies); i++) {
        expect_api_err(t, 403, "Forbidden", bodies[i], "403: " EMPTY_URL);
    }
}

static void t_error_text_cut_at_200_bytes(ctest_t *t) {
    char want[320];
    kit_t *k = kit_begin();
    memset(g_big, 'e', 201);
    g_big[201] = '\0';
    const char *body = KIT_FMT(k, "{\"error\":\"%.200s\"}", g_big);
    snprintf(want, sizeof want, "400: %.200s", g_big);
    kit_check_err(t, k, api(k, 400, "B", "", body), ANNAS_E_API, 400, want); /* exactly 200: whole */
    k = kit_begin();
    body = KIT_FMT(k, "{\"error\":\"%.201s\"}", g_big);
    kit_check_err(t, k, api(k, 400, "B", "", body), ANNAS_E_API, 400, want); /* 201: cut to 200 */
    k = kit_begin();
    body = KIT_FMT(k, "{\"error\":\"%.199s\"}", g_big);
    snprintf(want, sizeof want, "400: %.199s", g_big);
    kit_check_err(t, k, api(k, 400, "B", "", body), ANNAS_E_API, 400, want);
}

static void t_non_json_body_prefix(ctest_t *t) {
    expect_api_err(t, 500, "ISE", "<html>Oops</html>", "500: <html>Oops</html>");
    expect_api_err(t, 502, "BG", "Bad Gateway", "502: Bad Gateway");
    expect_api_err(t, 200, "OK", "OK", "200: OK");
    expect_api_err(t, 200, "OK", "{\"download_url\":", "200: {\"download_url\":");
    expect_api_err(t, 200, "OK", "{} x", "200: {} x");
    expect_api_err(t, 200, "OK", "{\"a\":1,\"a\":2}", "200: {\"a\":1,\"a\":2}");
    expect_api_err(t, 200, "OK", "{'download_url':'https://h/x'}", "200: {'download_url':'https://h/x'}");
    expect_api_err(t, 200, "OK", "\xef\xbb\xbf{}", "200: \xef\xbb\xbf{}");
    expect_api_err(t, 200, "OK", "{\"error\":\"bad \xff utf8\"}", "200: {\"error\":\"bad \xff utf8\"}");
    expect_api_err(t, 200, "OK", "{\"error\":\"a\x01z\"}", "200: {\"error\":\"a?z\"}");
}

static void t_non_json_prefix_is_512_bytes(ctest_t *t) {
    kit_t *k = kit_begin();
    memset(g_big, 'p', 700);
    g_big[700] = '\0';
    memcpy(g_msg, "500: ", 5);
    memset(g_msg + 5, 'p', 512);
    g_msg[517] = '\0';
    kit_check_err(t, k, api(k, 500, "E", "", g_big), ANNAS_E_API, 500, g_msg);
    k = kit_begin(); /* exactly 512 */
    g_big[512] = '\0';
    kit_check_err(t, k, api(k, 500, "E", "", g_big), ANNAS_E_API, 500, g_msg);
    k = kit_begin(); /* 511 */
    g_big[511] = '\0';
    g_msg[516] = '\0';
    kit_check_err(t, k, api(k, 500, "E", "", g_big), ANNAS_E_API, 500, g_msg);
    k = kit_begin(); /* 513 */
    memset(g_big, 'p', 513);
    g_big[513] = '\0';
    g_msg[516] = 'p';
    g_msg[517] = '\0';
    kit_check_err(t, k, api(k, 500, "E", "", g_big), ANNAS_E_API, 500, g_msg);
}

static void t_control_bytes_become_question_marks(ctest_t *t) {
    expect_api_err(t, 500, "E", "line1\r\nline2\tx\x7f\x1f", "500: line1??line2?x??");
    expect_api_err(t, 400, "E", "{\"error\":\"a\\nb\\tc\\u0000d\\u001fe\\u007ff\"}", "400: a?b?c?d?e?f");
}

static void t_json_limits_make_the_body_not_json(ctest_t *t) {
    char deep[40];
    memset(deep, '[', 17);
    memset(deep + 17, ']', 17);
    deep[34] = '\0';
    char want[64];
    snprintf(want, sizeof want, "200: %s", deep);
    expect_api_err(t, 200, "OK", deep, want); /* depth 17 */
    memset(deep, '[', 16);
    memset(deep + 16, ']', 16);
    deep[32] = '\0';
    expect_api_err(t, 200, "OK", deep, "200: " EMPTY_URL); /* depth 16 is valid JSON, not an object */
}

static void t_json_string_limit(ctest_t *t) {
    kit_t *k = kit_begin();
    memset(g_big, 'e', 8192);
    g_big[8192] = '\0';
    const char *body = KIT_FMT(k, "{\"error\":\"%s\"}", g_big);
    memcpy(g_msg, "400: ", 5);
    memset(g_msg + 5, 'e', 200);
    g_msg[205] = '\0';
    kit_check_err(t, k, api(k, 400, "B", "", body), ANNAS_E_API, 400, g_msg); /* valid: error cut to 200 */
    k = kit_begin();
    g_big[8192] = 'e';
    g_big[8193] = '\0';
    body = KIT_FMT(k, "{\"error\":\"%s\"}", g_big);
    memcpy(g_msg, "400: {\"error\":\"", 15);
    memset(g_msg + 15, 'e', 502);
    g_msg[517] = '\0';
    kit_check_err(t, k, api(k, 400, "B", "", body), ANNAS_E_API, 400, g_msg); /* 8193: not JSON, first 512 bytes */
}

static void t_json_element_limit(ctest_t *t) {
    size_t n = 0;
    kit_t *k = kit_begin();
    n += (size_t)snprintf(g_big, sizeof g_big, "{\"a\":[");
    for (int i = 0; i < 1022; i++) {
        g_big[n++] = '0';
        g_big[n++] = (i == 1021) ? ']' : ',';
    }
    g_big[n++] = '}';
    g_big[n] = '\0';
    kit_check_err(t, k, api(k, 200, "OK", "", g_big), ANNAS_E_API, 200, "200: " EMPTY_URL); /* 1024 values */
    k = kit_begin();
    n = (size_t)snprintf(g_big, sizeof g_big, "{\"a\":[");
    for (int i = 0; i < 1023; i++) {
        g_big[n++] = '0';
        g_big[n++] = (i == 1022) ? ']' : ',';
    }
    g_big[n++] = '}';
    g_big[n] = '\0';
    memcpy(g_msg, "200: ", 5);
    memcpy(g_msg + 5, g_big, 512);
    g_msg[517] = '\0';
    kit_check_err(t, k, api(k, 200, "OK", "", g_big), ANNAS_E_API, 200, g_msg); /* 1025 values */
}

static void t_redirects_are_not_followed(ctest_t *t) {
    static const int codes[] = {301, 302, 303, 307, 308};
    for (size_t i = 0; i < COUNT(codes); i++) {
        kit_t *k = kit_begin();
        char want[40];
        snprintf(want, sizeof want, "%d: " EMPTY_URL, codes[i]);
        kit_check_err(t, k, api(k, codes[i], "R", "Location: https://other.example.org/x\r\n", ""), ANNAS_E_API,
                      codes[i], want);
    }
}

static void t_check_in_location_is_a_challenge(ctest_t *t) {
    static const char *const locs[] = {"/search?q=a&check=1", "https://x.example.org/?check=1", "/a?check=10", "check=1"};
    static const int codes[] = {301, 302, 303, 307, 308};
    const char *msg = "the site requires browser verification; use download-by-md5 with a hash found in a browser";
    for (size_t i = 0; i < COUNT(locs); i++) {
        kit_t *k = kit_begin();
        kit_check_err(t, k, api(k, codes[i % COUNT(codes)], "R", KIT_FMT(k, "Location: %s\r\n", locs[i]), ""),
                      ANNAS_E_BROWSER_CHECK, 0, msg);
    }
    kit_t *k = kit_begin(); /* controls: other Locations, wrong case, wrong status */
    kit_check_err(t, k, api(k, 302, "R", "Location: /a?check=2\r\n", ""), ANNAS_E_API, 302, "302: " EMPTY_URL);
    k = kit_begin();
    kit_check_err(t, k, api(k, 302, "R", "Location: /a?CHECK=1\r\n", ""), ANNAS_E_API, 302, "302: " EMPTY_URL);
    k = kit_begin();
    kit_check_ok(t, k, api(k, 200, "OK", "Location: /a?check=1\r\n", "{\"download_url\":\"https://h/x\"}"));
}

static void t_ddos_guard_challenge(ctest_t *t) {
    const char *msg = "the site requires browser verification; use download-by-md5 with a hash found in a browser";
    static const char *const bodies[] = {"<html>DDoS-Guard</html>", "ddos-guard", "xx DDOS-GUARD xx", "Protected by dDoS-gUaRd"};
    static const int codes[] = {403, 503};
    for (size_t i = 0; i < COUNT(bodies); i++) {
        for (size_t j = 0; j < 2; j++) {
            kit_t *k = kit_begin();
            kit_check_err(t, k, api(k, codes[j], "X", "", bodies[i]), ANNAS_E_BROWSER_CHECK, 0, msg);
        }
    }
    kit_t *k = kit_begin(); /* the challenge wins over a JSON error text */
    kit_check_err(t, k, api(k, 403, "F", "", "{\"error\":\"ddos-guard\"}"), ANNAS_E_BROWSER_CHECK, 0, msg);
}

static void t_ddos_guard_controls(ctest_t *t) {
    expect_api_err(t, 403, "F", "<html>Forbidden</html>", "403: <html>Forbidden</html>");
    expect_api_err(t, 503, "U", "{\"error\":\"down\"}", "503: down");
    expect_api_err(t, 502, "BG", "ddos-guard", "502: ddos-guard");
    expect_api_err(t, 429, "TM", "ddos-guard", "429: ddos-guard");
    expect_api_err(t, 404, "NF", "ddos-guard", "404: ddos-guard");
    expect_api_err(t, 403, "F", "ddos guard", "403: ddos guard");
    kit_t *k = kit_begin();
    kit_check_ok(t, k, api(k, 200, "OK", "", "{\"download_url\":\"https://h/ddos-guard\",\"x\":\"ddos-guard\"}"));
}

static void t_ddos_guard_only_in_first_1024_bytes(ctest_t *t) {
    kit_t *k = kit_begin();
    memset(g_big, ' ', sizeof g_big);
    memcpy(g_big + 1014, "ddos-guard", 10); /* bytes 1014..1023: inside */
    g_big[1100] = '\0';
    kit_check_err(t, k, api_big(k, 403, 1100), ANNAS_E_BROWSER_CHECK, 0,
                  "the site requires browser verification; use download-by-md5 with a hash found in a browser");
    k = kit_begin();
    memset(g_big, ' ', 1100);
    memcpy(g_big + 1015, "ddos-guard", 10); /* straddles the 1024 boundary */
    memcpy(g_msg, "403: ", 5);
    memcpy(g_msg + 5, g_big, 512);
    g_msg[517] = '\0';
    kit_check_err(t, k, api_big(k, 403, 1100), ANNAS_E_API, 403, g_msg);
}

/* A hand-built configuration whose base is not a valid domain (annas_config_load never makes one): E_CONFIG before
 * any port call, from both operations. */
static void bad_base(ctest_t *t, const char *base, size_t base_len, bool download) {
    kit_t *k = kit_begin();
    memset(k->cfg.base, 'a', sizeof k->cfg.base);
    for (size_t i = 63; i < sizeof k->cfg.base; i += 64u) {
        k->cfg.base[i] = '.'; /* labels of 63 bytes: only the total length (253 at most) is wrong */
    }
    if (base[0] != '\0') {
        memset(k->cfg.base, 0, sizeof k->cfg.base);
        memcpy(k->cfg.base, base, strlen(base));
    }
    k->cfg.base[sizeof k->cfg.base - 1u] = '\0';
    k->cfg.base_len = base_len;
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, "https://dl.example.org/f", NULL)));
    k->allow_unused = true;
    CHECK(t, kit_prepare(k));
    annas_request_t r = kit_req(KIT_MD5);
    annas_code_t c = download ? kit_download(k, &r) : kit_fast(k, &r);
    CHECK_EQ_I64(t, c, ANNAS_E_CONFIG);
    CHECK_EQ_I64(t, k->err.code, ANNAS_E_CONFIG);
    CHECK_EQ_I64(t, k->err.http_status, 0);
    CHECK(t, strncmp(k->err.message, "ANNAS_BASE_URL is not a valid domain", 36) == 0);
    CHECK_EQ_U64(t, k->err.message_len, strlen(k->err.message));
    CHECK(t, !kit_contains(k->err.message, k->key));
    CHECK_EQ_U64(t, ports_fake_stream_pos(&k->stream), 0);
    kit_check_fs_untouched(t, k);
}

static void t_a_hand_built_invalid_base_is_a_config_error(ctest_t *t) {
    for (int d = 0; d < 2; d++) {
        bad_base(t, "bad host!", 9, d == 1);
        bad_base(t, "a..b", 4, d == 1);
        bad_base(t, "", 254, d == 1); /* longer than 253 bytes */
        bad_base(t, "", 300, d == 1);
    }
}

CTEST_SUITE("fast_err",
    {"live_fixture_401", t_live_fixture_401},
    {"live_fixture_404", t_live_fixture_404},
    {"live_fixture_302_is_a_challenge", t_live_fixture_302_is_a_challenge},
    {"success_status_with_unusable_url", t_success_status_with_unusable_url},
    {"only_200_and_204_succeed", t_only_200_and_204_succeed},
    {"204_cases", t_204_cases},
    {"error_text_is_used", t_error_text_is_used},
    {"error_text_falls_back", t_error_text_falls_back},
    {"error_text_cut_at_200_bytes", t_error_text_cut_at_200_bytes},
    {"non_json_body_prefix", t_non_json_body_prefix},
    {"non_json_prefix_is_512_bytes", t_non_json_prefix_is_512_bytes},
    {"control_bytes_become_question_marks", t_control_bytes_become_question_marks},
    {"json_limits_make_the_body_not_json", t_json_limits_make_the_body_not_json},
    {"json_string_limit", t_json_string_limit},
    {"json_element_limit", t_json_element_limit},
    {"redirects_are_not_followed", t_redirects_are_not_followed},
    {"check_in_location_is_a_challenge", t_check_in_location_is_a_challenge},
    {"ddos_guard_challenge", t_ddos_guard_challenge},
    {"ddos_guard_controls", t_ddos_guard_controls},
    {"ddos_guard_only_in_first_1024_bytes", t_ddos_guard_only_in_first_1024_bytes},
    {"a_hand_built_invalid_base_is_a_config_error", t_a_hand_built_invalid_base_is_a_config_error})
