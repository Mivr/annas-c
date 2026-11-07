/* The member key never appears in any message or result: redaction of server echoes, cuts that straddle the key,
 * percent-encoded echoes, and refusal of results that contain the key. Fake keys only. */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

static char g_body[1200];
static char g_want[800];

static annas_code_t api(kit_t *k, int status, const char *body) {
    kit_api(k, KIT_MD5, "", kit_http(k, status, "X", "", body));
    annas_request_t r = kit_req(KIT_MD5);
    return kit_fast(k, &r);
}

static void t_error_text_echoing_the_key(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_err(t, k, api(k, 401, "{\"error\":\"bad key " KIT_KEY " here\"}"), ANNAS_E_API, 401, "401: bad key REDACTED here");
    k = kit_begin();
    kit_check_err(t, k, api(k, 401, "{\"error\":\"" KIT_KEY KIT_KEY "|" KIT_KEY "\"}"), ANNAS_E_API, 401,
                  "401: REDACTEDREDACTED|REDACTED");
}

static void t_json_escaped_key_is_decoded_before_redaction(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_err(t, k, api(k, 401, "{\"error\":\"x FAKEKEY\\u002dnot-a-real-key-0123456789 y\"}"), ANNAS_E_API, 401,
                  "401: x REDACTED y");
}

static void t_percent_encoded_key_is_redacted(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_set_key(k, "fake-key.1_~"); /* pct_encode(key) == key: the raw bytes are redacted, an escaped spelling is not */
    kit_check_err(t, k, api(k, 400, "{\"error\":\"bad fake-key.1_~ and fake%2Dkey.1_~ and fake-key.1_~\"}"), ANNAS_E_API,
                  400, "400: bad REDACTED and fake%2Dkey.1_~ and REDACTED");
}

static void t_key_parameters_are_redacted_in_server_text(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_err(t, k,
                  api(k, 400, "{\"error\":\"bad /dyn/api/fast_download.json?md5=abc&key=somethingelse&x=1\"}"),
                  ANNAS_E_API, 400, "400: bad /dyn/api/fast_download.json?md5=abc&key=REDACTED&x=1");
    k = kit_begin();
    kit_check_err(t, k, api(k, 500, "<a href=\"/x?key=abc&y=2\">retry</a>"), ANNAS_E_API, 500,
                  "500: <a href=\"/x?key=REDACTED&y=2\">retry</a>");
}

static void t_body_prefix_cut_does_not_split_the_key(ctest_t *t) {
    kit_t *k = kit_begin();
    memset(g_body, 'a', 505);
    memcpy(g_body + 505, KIT_KEY, strlen(KIT_KEY));
    g_body[505 + strlen(KIT_KEY)] = '\0';
    snprintf(g_want, sizeof g_want, "500: %.505s", g_body);
    strcat(g_want, "REDACTE"); /* redaction first, then the 512-byte cut */
    kit_check_err(t, k, api(k, 500, g_body), ANNAS_E_API, 500, g_want);
    k = kit_begin();
    memset(g_body, 'a', 500);
    memcpy(g_body + 500, KIT_KEY "zzzzzzzz", strlen(KIT_KEY) + 9u);
    snprintf(g_want, sizeof g_want, "500: %.500sREDACTEDzzzz", g_body);
    kit_check_err(t, k, api(k, 500, g_body), ANNAS_E_API, 500, g_want);
}

static void t_error_cut_does_not_split_the_key(ctest_t *t) {
    kit_t *k = kit_begin();
    static char json[400];
    static char a[200];
    memset(a, 'a', 190);
    a[190] = '\0';
    snprintf(json, sizeof json, "{\"error\":\"%s%sbbbbbbbbbb\"}", a, KIT_KEY);
    snprintf(g_want, sizeof g_want, "400: %sREDACTEDbb", a);
    kit_check_err(t, k, api(k, 400, json), ANNAS_E_API, 400, g_want);
}

static void t_near_misses_are_left_alone(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_err(t, k, api(k, 400, "{\"error\":\"FAKEKEY-not-a-real-key-012345678 fakekey-not-a-real-key-0123456789\"}"),
                  ANNAS_E_API, 400, "400: FAKEKEY-not-a-real-key-012345678 fakekey-not-a-real-key-0123456789");
}

static void t_result_containing_the_key_is_refused(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_err(t, k, api(k, 200, "{\"download_url\":\"https://dl.example.org/f?k=" KIT_KEY "\"}"), ANNAS_E_PARSE, 0,
                  "download URL contains the secret key");
    k = kit_begin();
    kit_set_key(k, "fake-key.1_~");
    kit_check_err(t, k, api(k, 200, "{\"download_url\":\"https://dl.example.org/f?k=fake-key.1_~\"}"), ANNAS_E_PARSE, 0,
                  "download URL contains the secret key");
    k = kit_begin();
    kit_check_err(t, k, api(k, 200, "{\"download_url\":\"https://dl.example.org/f\",\"account_fast_download_info\":{\"k\":\"" KIT_KEY "\"}}"),
                  ANNAS_E_PARSE, 0, "account info contains the secret key");
    k = kit_begin(); /* control: a near miss is fine */
    kit_check_ok(t, k, api(k, 200, "{\"download_url\":\"https://dl.example.org/f?k=FAKEKEY-not-a-real-key-012345678\"}"));
}

static void t_title_equal_to_the_key_is_redacted_in_fs_errors(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", "{\"download_url\":\"https://dl.example.org/f/1\"}"));
    kit_get(k, "dl.example.org", 443, "/f/1", kit_http(k, 200, "OK", "", "x"));
    CHECK(t, kit_preload(k, "REDACTED.pdf", "old")); /* the name the redacted title gives */
    annas_request_t r = kit_req(KIT_MD5);
    r.title = core_str_from_cstr(KIT_KEY);
    r.format = core_str_from_cstr("pdf");
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_FS, 0, "file already exists: REDACTED.pdf");
    kit_check_no_partial(t, k, 1);
}

CTEST_SUITE("leak",
    {"error_text_echoing_the_key", t_error_text_echoing_the_key},
    {"json_escaped_key_is_decoded_before_redaction", t_json_escaped_key_is_decoded_before_redaction},
    {"percent_encoded_key_is_redacted", t_percent_encoded_key_is_redacted},
    {"key_parameters_are_redacted_in_server_text", t_key_parameters_are_redacted_in_server_text},
    {"body_prefix_cut_does_not_split_the_key", t_body_prefix_cut_does_not_split_the_key},
    {"error_cut_does_not_split_the_key", t_error_cut_does_not_split_the_key},
    {"near_misses_are_left_alone", t_near_misses_are_left_alone},
    {"result_containing_the_key_is_refused", t_result_containing_the_key_is_refused},
    {"title_equal_to_the_key_is_redacted_in_fs_errors", t_title_equal_to_the_key_is_redacted_in_fs_errors})
