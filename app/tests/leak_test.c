/* The member key never appears in any output of the CLI or the MCP server, whatever the server, the client or the
 * file name echoes. Every case first proves that the text WOULD contain the key without the scrubbing (the
 * unscrubbed form is visible in the fake fs or in the request), or has a near-miss control. */
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"

#define MD5 AK_MD5
#define URL "https://dl.example.org/f/1"
#define JSON_HDR "Content-Type: application/json\r\n"
#define KEY KIT_KEY
#define HASH "\"hash\":\"" MD5 "\""

static char g_req[4096];

static app_mcp_result_t call(ak_t *a, const char *tool, const char *args) {
    (void)snprintf(g_req, sizeof g_req,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}",
                   tool, args);
    return ak_mcp(a, g_req);
}

static void t_title_equal_to_the_key_on_success(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 200, "OK", "", "x"));
    int rc = AK_CLI(a, "download", MD5, "--name", KEY, "--format", "pdf");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/REDACTED.pdf\n", "");
    ak_check_stream(t, a);
    kit_check_file(t, a->k, KEY ".pdf", "x", 1); /* the real file name is untouched: only the output is scrubbed */
}

static void t_title_containing_the_key_twice_and_around(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 200, "OK", "", "x"));
    int rc = AK_CLI(a, "download", MD5, "--name", "pre-" KEY KEY "-post", "--format", "txt");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/pre-REDACTEDREDACTED-post.txt\n", "");
    ak_check_stream(t, a);
}

static void t_near_miss_titles_are_kept(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 200, "OK", "", "x"));
    int rc = AK_CLI(a, "download", MD5, "--name", "FAKEKEY-not-a-real-key-012345678", "--format", "pdf");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/FAKEKEY-not-a-real-key-012345678.pdf\n", "");
    ak_check_stream(t, a);
}

static void t_percent_encoded_key_is_scrubbed_too(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "ab+cd/ef=gh");
    kit_set_key(a->k, "ab+cd/ef=gh");
    CHECK_EQ_STR(t, a->k->key_pct, "ab%2Bcd%2Fef%3Dgh");
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 200, "OK", "", "x"));
    int rc = AK_CLI(a, "download", MD5, "--name", "ab%2Bcd%2Fef%3Dgh", "--format", "pdf");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/REDACTED.pdf\n", "");
    ak_check_stream(t, a);
    a = ak_begin(); /* the raw form: '/' and '+' survive the sanitiser only partly, so use the alnum tail */
    ak_env(a, "ANNAS_SECRET_KEY", "ab+cd=gh");
    kit_set_key(a->k, "ab+cd=gh");
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 200, "OK", "", "x"));
    rc = AK_CLI(a, "download", MD5, "--name", "ab+cd=gh", "--format", "pdf");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/REDACTED.pdf\n", "");
    ak_check_stream(t, a);
}

static void t_server_text_echoing_the_key(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 403, "Forbidden", JSON_HDR,
                                    "{\"download_url\":null,\"error\":\"bad key " KEY " and %46AKE " KEY "\"}"));
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 4, "", "annas-c: E_API: 403: bad key REDACTED and %46AKE REDACTED\n");
    ak_check_stream(t, a);
    a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 500, "Error", "", "oops key=" KEY " oops"));
    rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 4, "", "annas-c: E_API: 500: oops key=REDACTED oops\n");
    ak_check_stream(t, a);
}

static void t_file_exists_error_echoes_a_scrubbed_name(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 200, "OK", "", "x"));
    CHECK(t, kit_preload(a->k, KEY ".pdf", "old"));
    int rc = AK_CLI(a, "download", MD5, "--name", KEY, "--format", "pdf");
    ak_check_cli(t, a, rc, 6, "", "annas-c: E_FS: file already exists: REDACTED.pdf\n");
    ak_check_stream(t, a);
}

static void t_download_path_echo_is_scrubbed(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_DOWNLOAD_PATH", "relative-" KEY);
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 2, "", "annas-c: E_CONFIG: ANNAS_DOWNLOAD_PATH must be an absolute path, got: relative-REDACTED\n");
    ak_check_no_io(t, a);
    a = ak_begin();
    ak_env(a, "ANNAS_BASE_URL", "bad " KEY);
    rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 2, "", "annas-c: E_CONFIG: ANNAS_BASE_URL is not a valid domain, got: bad REDACTED\n");
}

static void t_the_key_from_dotenv_is_scrubbed_too(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    ak_dotenv(a, "ANNAS_SECRET_KEY=" KEY "\nANNAS_DOWNLOAD_PATH=/dl\n");
    kit_api(a->k, MD5, "", kit_http(a->k, 500, "Error", "", "echo " KEY));
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 4, "", "annas-c: E_API: 500: echo REDACTED\n");
    ak_check_stream(t, a);
}

static void t_mcp_id_echo_is_scrubbed(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_check_reply(t, a, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":\"" KEY "\",\"method\":\"ping\"}"),
                   AK_OKR("\"REDACTED\"", "{}"));
    ak_check_reply(t, a, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":\"x" KEY "y\",\"method\":\"nope\"}"),
                   AK_ERR("\"xREDACTEDy\"", "-32601", "Method not found"));
    /* a JSON-escaped spelling of the key is decoded first, so it is found in the re-encoded output */
    ak_check_reply(t, a, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":\"FAKEKEY\\u002dnot-a-real-key-0123456789\",\"method\":\"ping\"}"),
                   AK_OKR("\"REDACTED\"", "{}"));
    ak_check_reply(t, a, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":\"FAKEKEY-not-a-real-key-012345678\",\"method\":\"ping\"}"),
                   AK_OKR("\"FAKEKEY-not-a-real-key-012345678\"", "{}")); /* control: a near miss stays */
    ak_check_no_io(t, a);
}

static void t_mcp_echoed_names_are_scrubbed(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_check_reply(t, a, call(a, KEY, "{}"),
                   AK_ERR("1", "-32602", "Invalid params: unknown tool: REDACTED"));
    ak_check_reply(t, a, call(a, "book_download", "{\"" KEY "\":1}"),
                   AK_ERR("1", "-32602", "Invalid params: unknown argument: REDACTED"));
    ak_check_no_io(t, a);
}

static void t_mcp_tool_results_are_scrubbed(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 200, "OK", "", "x"));
    ak_check_reply(t, a, call(a, "book_download", "{" HASH ",\"title\":\"" KEY "\",\"format\":\"pdf\"}"),
                   AK_OKR("1", AK_TOOL_OK("Book downloaded successfully to: /dl/REDACTED.pdf")));
    ak_check_stream(t, a);
    kit_check_file(t, a->k, KEY ".pdf", "x", 1);
    a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 403, "Forbidden", JSON_HDR, "{\"download_url\":null,\"error\":\"key " KEY " rejected\"}"));
    ak_check_reply(t, a, call(a, "fast_download_url", "{" HASH "}"),
                   AK_OKR("1", AK_TOOL_ERR("annas-c: E_API: 403: key REDACTED rejected")));
    ak_check_stream(t, a);
}

static void t_without_a_configured_key_nothing_is_scrubbed(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    CHECK_EQ_I64(t, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":\"" KEY "\",\"method\":\"ping\"}"), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), AK_OKR("\"" KEY "\"", "{}"));
    a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "");
    ak_check_reply(t, a, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"ping\"}"), AK_OKR("\"abc\"", "{}"));
}

CTEST_SUITE("leak",
    {"title_equal_to_the_key_on_success", t_title_equal_to_the_key_on_success},
    {"title_containing_the_key_twice_and_around", t_title_containing_the_key_twice_and_around},
    {"near_miss_titles_are_kept", t_near_miss_titles_are_kept},
    {"percent_encoded_key_is_scrubbed_too", t_percent_encoded_key_is_scrubbed_too},
    {"server_text_echoing_the_key", t_server_text_echoing_the_key},
    {"file_exists_error_echoes_a_scrubbed_name", t_file_exists_error_echoes_a_scrubbed_name},
    {"download_path_echo_is_scrubbed", t_download_path_echo_is_scrubbed},
    {"the_key_from_dotenv_is_scrubbed_too", t_the_key_from_dotenv_is_scrubbed_too},
    {"mcp_id_echo_is_scrubbed", t_mcp_id_echo_is_scrubbed},
    {"mcp_echoed_names_are_scrubbed", t_mcp_echoed_names_are_scrubbed},
    {"mcp_tool_results_are_scrubbed", t_mcp_tool_results_are_scrubbed},
    {"without_a_configured_key_nothing_is_scrubbed", t_without_a_configured_key_nothing_is_scrubbed})
