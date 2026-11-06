/* tools/call: validation of params and tool arguments (-32602), with a control for every rejected shape. */
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"

#define MD5 AK_MD5
#define URL "https://dl.example.org/f/1"
#define JSON_HDR "Content-Type: application/json\r\n"
#define BOOK "\"book_download\""
#define FAST "\"fast_download_url\""
#define HASH "\"hash\":\"" MD5 "\""
#define ERR(msg) AK_ERR("1", "-32602", "Invalid params: " msg)

static char g_req[4096];

static app_mcp_result_t call(ak_t *a, const char *name_json, const char *args_json) {
    (void)snprintf(g_req, sizeof g_req,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":%s,\"arguments\":%s}}",
                   name_json, args_json);
    return ak_mcp(a, g_req);
}

static app_mcp_result_t call_params(ak_t *a, const char *params) {
    if (params == NULL) {
        return ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\"}");
    }
    (void)snprintf(g_req, sizeof g_req, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":%s}", params);
    return ak_mcp(a, g_req);
}

static void rejects(ctest_t *t, const char *tool, const char *args, const char *want) {
    ak_t *a = ak_begin();
    ak_check_reply(t, a, call(a, tool, args), want);
    ak_check_no_io(t, a);
}

/* A call that gets through validation: it must reach the API (one exchange) and return a non-error result. */
static void accepts(ctest_t *t, const char *tool, const char *args, const char *api_extra) {
    ak_t *a = ak_begin();
    bool is_book = strcmp(tool, BOOK) == 0;
    kit_api(a->k, MD5, api_extra, kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    if (is_book) {
        kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 200, "OK", "", "z"));
    }
    CHECK_EQ_I64(t, call(a, tool, args), APP_MCP_REPLY);
    CHECK(t, kit_contains(ak_out(a), "\"isError\":false"));
    CHECK(t, kit_contains(ak_out(a), "\"result\":{\"content\""));
    ak_check_stream(t, a);
}

static void t_params_must_be_an_object(ctest_t *t) {
    static const char *const bad[] = {NULL, "[]", "[1]", "\"x\"", "5", "null", "true"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ak_t *a = ak_begin();
        ak_check_reply(t, a, call_params(a, bad[i]), ERR("params must be an object"));
        ak_check_no_io(t, a);
    }
    ak_t *a = ak_begin(); /* control: an object reaches the next check */
    ak_check_reply(t, a, call_params(a, "{}"), ERR("name must be a string"));
}

static void t_name_must_be_a_string(ctest_t *t) {
    static const char *const bad[] = {"{}", "{\"name\":1}", "{\"name\":null}", "{\"name\":[\"book_download\"]}",
                                      "{\"name\":{}}", "{\"name\":true}", "{\"Name\":\"book_download\"}",
                                      "{\"arguments\":{\"hash\":\"x\"}}"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ak_t *a = ak_begin();
        ak_check_reply(t, a, call_params(a, bad[i]), ERR("name must be a string"));
        ak_check_no_io(t, a);
    }
}

static void t_unknown_tool(ctest_t *t) {
    rejects(t, "\"nope\"", "{}", ERR("unknown tool: nope"));
    rejects(t, "\"\"", "{}", ERR("unknown tool: "));
    rejects(t, "\"Book_Download\"", "{}", ERR("unknown tool: Book_Download"));
    rejects(t, "\"book_download \"", "{}", ERR("unknown tool: book_download "));
    rejects(t, "\"book-download\"", "{}", ERR("unknown tool: book-download"));
    rejects(t, "\"download\"", "{}", ERR("unknown tool: download"));
    rejects(t, "\"fast_download_url2\"", "{}", ERR("unknown tool: fast_download_url2"));
    rejects(t, "\"book_search\"", "{\"query\":\"x\"}", ERR("unknown tool: book_search")); /* v2, not implemented */
    rejects(t, "\"a\\nb\\u0001c\\u007fd\"", "{}", ERR("unknown tool: a?b?c?d"));
    rejects(t, "\"caf\\u00e9\"", "{}", ERR("unknown tool: caf\xc3\xa9"));
    rejects(t, "\"nope\"", "5", ERR("unknown tool: nope")); /* the name is checked before the arguments */
    rejects(t, "\"quote\\\"d\"", "{}", ERR("unknown tool: quote\\\"d"));
}

static void t_unknown_tool_name_is_cut_to_100_bytes(ctest_t *t) {
    char name[160];
    char want[256];
    memcpy(name, "\"", 1);
    memset(name + 1, 'n', 130);
    memcpy(name + 131, "\"", 2);
    int n = snprintf(want, sizeof want, "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32602,\"message\":\"Invalid params: unknown tool: ");
    memset(want + n, 'n', 100);
    memcpy(want + n + 100, "\"}}", 4);
    ak_t *a = ak_begin();
    ak_check_reply(t, a, call(a, name, "{}"), want);
    memset(name + 1, 'm', 100); /* control: exactly 100 bytes stay whole */
    name[101] = '"';
    name[102] = '\0';
    memset(want + n, 'm', 100);
    ak_check_reply(t, a, call(a, name, "{}"), want);
    ak_check_no_io(t, a);
}

static void t_arguments_must_be_an_object(ctest_t *t) {
    static const char *const bad[] = {"[]", "[1]", "\"x\"", "5", "null", "true", "false"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        rejects(t, BOOK, bad[i], ERR("arguments must be an object"));
        rejects(t, FAST, bad[i], ERR("arguments must be an object"));
    }
    accepts(t, BOOK, "{" HASH "}", ""); /* control */
}

static void t_arguments_may_be_absent_but_hash_is_required(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_check_reply(t, a, call_params(a, "{\"name\":" BOOK "}"), ERR("hash is required"));
    ak_check_reply(t, a, call_params(a, "{\"name\":" FAST "}"), ERR("hash is required"));
    ak_check_reply(t, a, call_params(a, "{\"name\":" FAST ",\"arguments\":{}}"), ERR("hash is required"));
    ak_check_no_io(t, a);
    rejects(t, BOOK, "{}", ERR("hash is required"));
    rejects(t, BOOK, "{\"title\":\"x\"}", ERR("hash is required"));
    rejects(t, FAST, "{\"path_index\":1,\"timeout_seconds\":5}", ERR("hash is required"));
    rejects(t, BOOK, "{\"Hash\":\"x\"}", ERR("unknown argument: Hash"));
    accepts(t, FAST, "{" HASH "}", ""); /* control */
}

static void t_unknown_arguments(ctest_t *t) {
    rejects(t, BOOK, "{\"foo\":1}", ERR("unknown argument: foo"));
    rejects(t, BOOK, "{" HASH ",\"foo\":null}", ERR("unknown argument: foo"));
    rejects(t, BOOK, "{" HASH ",\"query\":\"x\"}", ERR("unknown argument: query"));
    rejects(t, BOOK, "{" HASH ",\"timeout\":5}", ERR("unknown argument: timeout"));
    rejects(t, BOOK, "{" HASH ",\"pathIndex\":5}", ERR("unknown argument: pathIndex"));
    rejects(t, BOOK, "{" HASH ",\"\":5}", ERR("unknown argument: "));
    rejects(t, BOOK, "{" HASH ",\"a\\nb\":5}", ERR("unknown argument: a?b"));
    rejects(t, FAST, "{" HASH ",\"title\":\"x\"}", ERR("unknown argument: title"));
    rejects(t, FAST, "{" HASH ",\"format\":\"pdf\"}", ERR("unknown argument: format"));
    accepts(t, BOOK, "{" HASH ",\"title\":\"x\",\"format\":\"pdf\"}", ""); /* control: book_download takes them */
}

static void t_first_problem_in_source_order_wins(ctest_t *t) {
    rejects(t, BOOK, "{\"foo\":1,\"hash\":5}", ERR("unknown argument: foo"));
    rejects(t, BOOK, "{\"hash\":5,\"foo\":1}", ERR("hash must be a string"));
    rejects(t, BOOK, "{\"title\":1,\"hash\":5}", ERR("title must be a string"));
    rejects(t, BOOK, "{\"hash\":5,\"title\":1}", ERR("hash must be a string"));
    rejects(t, BOOK, "{\"path_index\":-1}", ERR("path_index must be an integer >= 0")); /* before "hash is required" */
    rejects(t, FAST, "{\"timeout_seconds\":\"x\",\"foo\":1}", ERR("timeout_seconds must be an integer"));
    rejects(t, BOOK, "{\"domain_index\":-1,\"path_index\":-1,\"hash\":5}", ERR("domain_index must be an integer >= 0"));
}

static void t_string_arguments_must_be_strings(ctest_t *t) {
    static const char *const bad[] = {"5", "null", "true", "false", "[\"x\"]", "{}", "1.5", "[]"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char args[160];
        (void)snprintf(args, sizeof args, "{\"hash\":%s}", bad[i]);
        rejects(t, BOOK, args, ERR("hash must be a string"));
        rejects(t, FAST, args, ERR("hash must be a string"));
        (void)snprintf(args, sizeof args, "{" HASH ",\"title\":%s}", bad[i]);
        rejects(t, BOOK, args, ERR("title must be a string"));
        (void)snprintf(args, sizeof args, "{" HASH ",\"format\":%s}", bad[i]);
        rejects(t, BOOK, args, ERR("format must be a string"));
    }
    accepts(t, BOOK, "{" HASH ",\"title\":\"\",\"format\":\"\"}", ""); /* control: empty strings are strings */
}

static void t_index_arguments_must_be_unsigned_integers(ctest_t *t) {
    static const char *const bad[] = {"-1", "-0", "1.5", "1.0", "0.0", "1e2", "1E0", "\"1\"", "null", "true", "false",
                                      "[1]", "{}", "[]", "18446744073709551616", "99999999999999999999999"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char args[200];
        (void)snprintf(args, sizeof args, "{" HASH ",\"path_index\":%s}", bad[i]);
        rejects(t, BOOK, args, ERR("path_index must be an integer >= 0"));
        rejects(t, FAST, args, ERR("path_index must be an integer >= 0"));
        (void)snprintf(args, sizeof args, "{" HASH ",\"domain_index\":%s}", bad[i]);
        rejects(t, BOOK, args, ERR("domain_index must be an integer >= 0"));
        rejects(t, FAST, args, ERR("domain_index must be an integer >= 0"));
    }
    accepts(t, FAST, "{" HASH ",\"path_index\":0,\"domain_index\":7}", "&path_index=0&domain_index=7");
    accepts(t, BOOK, "{" HASH ",\"path_index\":18446744073709551615}", "&path_index=18446744073709551615");
    accepts(t, FAST, "{" HASH ",\"domain_index\":3}", "&domain_index=3");
}

static void t_timeout_must_be_an_integer(ctest_t *t) {
    static const char *const bad[] = {"1.5", "1.0", "1e2", "\"5\"", "null", "true", "[5]", "{}",
                                      "9223372036854775808", "-9223372036854775809", "99999999999999999999"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char args[200];
        (void)snprintf(args, sizeof args, "{" HASH ",\"timeout_seconds\":%s}", bad[i]);
        rejects(t, BOOK, args, ERR("timeout_seconds must be an integer"));
        rejects(t, FAST, args, ERR("timeout_seconds must be an integer"));
    }
    accepts(t, BOOK, "{" HASH ",\"timeout_seconds\":0}", "");
    accepts(t, FAST, "{" HASH ",\"timeout_seconds\":3600}", "");
    accepts(t, FAST, "{" HASH ",\"timeout_seconds\":31536000}", "");
}

static void t_repeated_member_is_a_parse_error(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_check_reply(t, a, call(a, BOOK, "{" HASH "," HASH "}"), AK_ERR("null", "-32700", "Parse error"));
    ak_check_no_io(t, a);
}

static void t_bad_arguments_never_touch_a_port(ctest_t *t) {
    ak_t *a = ak_begin();
    static const char *const bad[] = {"{\"hash\":5}", "{}", "{\"foo\":1}", "{" HASH ",\"path_index\":-1}"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK_EQ_I64(t, call(a, BOOK, bad[i]), APP_MCP_REPLY);
        CHECK(t, kit_contains(ak_out(a), "\"error\":{\"code\":-32602"));
    }
    ak_check_no_io(t, a);
}

CTEST_SUITE("mcp_args",
    {"params_must_be_an_object", t_params_must_be_an_object},
    {"name_must_be_a_string", t_name_must_be_a_string},
    {"unknown_tool", t_unknown_tool},
    {"unknown_tool_name_is_cut_to_100_bytes", t_unknown_tool_name_is_cut_to_100_bytes},
    {"arguments_must_be_an_object", t_arguments_must_be_an_object},
    {"arguments_may_be_absent_but_hash_is_required", t_arguments_may_be_absent_but_hash_is_required},
    {"unknown_arguments", t_unknown_arguments},
    {"first_problem_in_source_order_wins", t_first_problem_in_source_order_wins},
    {"string_arguments_must_be_strings", t_string_arguments_must_be_strings},
    {"index_arguments_must_be_unsigned_integers", t_index_arguments_must_be_unsigned_integers},
    {"timeout_must_be_an_integer", t_timeout_must_be_an_integer},
    {"repeated_member_is_a_parse_error", t_repeated_member_is_a_parse_error},
    {"bad_arguments_never_touch_a_port", t_bad_arguments_never_touch_a_port})
