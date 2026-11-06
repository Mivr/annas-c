/* app_mcp_handle: the protocol surface: initialize, notifications, ping, tools/list, id echo, request validation. */
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"

#define VER "\"version\":\"0.1\""
#define INIT_RESULT(v) \
    "{\"protocolVersion\":\"" v "\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"annas-c\"," VER "}}"
#define INIT_REQ(id, v) \
    "{\"jsonrpc\":\"2.0\",\"id\":" id ",\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"" v \
    "\",\"capabilities\":{},\"clientInfo\":{\"name\":\"t\",\"version\":\"1\"}}}"
#define PING(id) "{\"jsonrpc\":\"2.0\",\"id\":" id ",\"method\":\"ping\"}"
#define BAD_ID "Invalid Request: id must be a string, a number or null"
#define BAD_VER "Invalid Request: jsonrpc must be \\\"2.0\\\""

static void reply(ctest_t *t, ak_t *a, const char *line, const char *want) {
    ak_check_reply(t, a, ak_mcp(a, line), want);
    ak_check_no_io(t, a);
}

static void silent(ctest_t *t, ak_t *a, const char *line) {
    CHECK_EQ_I64(t, ak_mcp(a, line), APP_MCP_NO_REPLY);
    CHECK_EQ_U64(t, core_buf_len(&a->out), 0);
    ak_check_no_io(t, a);
}

static void t_initialize_echoes_supported_versions(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, INIT_REQ("1", "2025-06-18"), AK_OKR("1", INIT_RESULT("2025-06-18")));
    reply(t, a, INIT_REQ("2", "2025-03-26"), AK_OKR("2", INIT_RESULT("2025-03-26")));
    reply(t, a, INIT_REQ("3", "2024-11-05"), AK_OKR("3", INIT_RESULT("2024-11-05")));
}

static void t_initialize_unsupported_versions_get_the_newest(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, INIT_REQ("1", "2099-01-01"), AK_OKR("1", INIT_RESULT("2025-06-18")));
    reply(t, a, INIT_REQ("2", "2024-11-04"), AK_OKR("2", INIT_RESULT("2025-06-18")));
    reply(t, a, INIT_REQ("3", ""), AK_OKR("3", INIT_RESULT("2025-06-18")));
    reply(t, a, INIT_REQ("4", "2025-06-18 "), AK_OKR("4", INIT_RESULT("2025-06-18")));
    reply(t, a, INIT_REQ("5", "2025-6-18"), AK_OKR("5", INIT_RESULT("2025-06-18")));
}

static void t_initialize_params_are_validated(ctest_t *t) {
    ak_t *a = ak_begin();
    const char *no_params = "params must be an object";
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}", AK_ERR("1", "-32602", "Invalid params: params must be an object"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":[]}", AK_ERR("1", "-32602", "Invalid params: params must be an object"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":\"x\"}", AK_ERR("1", "-32602", "Invalid params: params must be an object"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":null}", AK_ERR("1", "-32602", "Invalid params: params must be an object"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}", AK_ERR("1", "-32602", "Invalid params: protocolVersion must be a string"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":20250618}}", AK_ERR("1", "-32602", "Invalid params: protocolVersion must be a string"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":null}}", AK_ERR("1", "-32602", "Invalid params: protocolVersion must be a string"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolversion\":\"2025-06-18\"}}", AK_ERR("1", "-32602", "Invalid params: protocolVersion must be a string"));
    /* control: a minimal valid initialize */
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}", AK_OKR("1", INIT_RESULT("2025-06-18")));
    (void)no_params;
}

static void t_initialize_twice_is_fine(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, INIT_REQ("1", "2025-03-26"), AK_OKR("1", INIT_RESULT("2025-03-26")));
    reply(t, a, INIT_REQ("1", "2024-11-05"), AK_OKR("1", INIT_RESULT("2024-11-05")));
}

static void t_initialized_notification(ctest_t *t) {
    ak_t *a = ak_begin();
    silent(t, a, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
    silent(t, a, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\",\"params\":{}}");
    /* control: with an id it is a request and succeeds with an empty result */
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"notifications/initialized\"}", AK_OKR("5", "{}"));
}

static void t_other_notifications_are_silent(ctest_t *t) {
    ak_t *a = ak_begin();
    silent(t, a, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":1}}");
    silent(t, a, "{\"jsonrpc\":\"2.0\",\"method\":\"no/such/method\"}");
    silent(t, a, "{\"jsonrpc\":\"2.0\",\"method\":\"ping\"}");
    silent(t, a, "{\"jsonrpc\":\"2.0\",\"method\":\"tools/list\"}");
    silent(t, a, "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}");
    silent(t, a, "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\"}"); /* even with bad params */
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"no/such/method\"}", AK_ERR("1", "-32601", "Method not found")); /* control */
}

static void t_ping(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, PING("1"), AK_OKR("1", "{}"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\",\"params\":{\"a\":[1,2,{\"b\":null}]}}", AK_OKR("2", "{}"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\",\"params\":[1]}", AK_OKR("3", "{}"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"ping\",\"params\":7}", AK_OKR("4", "{}"));
    reply(t, a, "{\"method\":\"ping\",\"id\":5,\"jsonrpc\":\"2.0\"}", AK_OKR("5", "{}")); /* member order is free */
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"ping\",\"extra\":{\"x\":1},\"more\":[]}", AK_OKR("6", "{}"));
}

static void t_surrounding_whitespace(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, " \t" PING("1") " \t", AK_OKR("1", "{}"));
    reply(t, a, PING("2") "\r", AK_OKR("2", "{}"));
    reply(t, a, PING("3") "\r\n", AK_OKR("3", "{}"));
    reply(t, a, "{ \"jsonrpc\" : \"2.0\" ,\n \"id\" : 4 , \"method\" : \"ping\" }", AK_OKR("4", "{}"));
}

static void t_tools_list(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}", AK_OKR("1", AK_TOOLS_LIST));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"method\":\"tools/list\",\"params\":{\"cursor\":\"abc\"}}", AK_OKR("\"x\"", AK_TOOLS_LIST));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\",\"params\":5}", AK_OKR("3", AK_TOOLS_LIST));
}

static void t_tools_list_works_before_initialize_and_repeatedly(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}", AK_OKR("1", AK_TOOLS_LIST));
    reply(t, a, INIT_REQ("2", "2025-06-18"), AK_OKR("2", INIT_RESULT("2025-06-18")));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\"}", AK_OKR("3", AK_TOOLS_LIST));
}

static void t_id_numbers_are_echoed_verbatim(ctest_t *t) {
    ak_t *a = ak_begin();
    static const char *const ids[] = {"0", "-1", "42", "1.50", "1E+2", "-0", "0.0", "12345678901234567890", "1e-7"};
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        char line[160];
        char want[200];
        (void)snprintf(line, sizeof line, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"method\":\"ping\"}", ids[i]);
        (void)snprintf(want, sizeof want, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{}}", ids[i]);
        reply(t, a, line, want);
    }
}

static void t_id_strings_are_echoed_reencoded(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, PING("\"abc\""), AK_OKR("\"abc\"", "{}"));
    reply(t, a, PING("\"\""), AK_OKR("\"\"", "{}"));
    reply(t, a, PING("\"a/b\""), AK_OKR("\"a/b\"", "{}"));
    reply(t, a, PING("\"a\\/b\""), AK_OKR("\"a/b\"", "{}"));          /* \/ is decoded, re-emitted as '/' */
    reply(t, a, PING("\"q\\\"b\\\\s\""), AK_OKR("\"q\\\"b\\\\s\"", "{}"));
    reply(t, a, PING("\"line\\nbreak\\ttab\""), AK_OKR("\"line\\u000abreak\\u0009tab\"", "{}"));
    reply(t, a, PING("\"\\u00e9\\u0041\""), AK_OKR("\"\xc3\xa9" "A\"", "{}"));       /* decoded to UTF-8 */
    reply(t, a, PING("\"\xe4\xb8\xad\xf0\x9f\x98\x80\""), AK_OKR("\"\xe4\xb8\xad\xf0\x9f\x98\x80\"", "{}"));
    reply(t, a, PING("\"\\ud83d\\ude00\""), AK_OKR("\"\xf0\x9f\x98\x80\"", "{}"));  /* surrogate pair */
    reply(t, a, PING("\"\\u0000x\""), AK_OKR("\"\\u0000x\"", "{}"));
    reply(t, a, PING("\"\\u007f\""), AK_OKR("\"\x7f\"", "{}"));
}

static void t_id_null_is_a_request(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, PING("null"), AK_OKR("null", "{}"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"nope\"}", AK_ERR("null", "-32601", "Method not found"));
}

static void t_id_of_other_types_is_invalid(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, PING("true"), AK_ERR("null", "-32600", BAD_ID));
    reply(t, a, PING("false"), AK_ERR("null", "-32600", BAD_ID));
    reply(t, a, PING("{}"), AK_ERR("null", "-32600", BAD_ID));
    reply(t, a, PING("[]"), AK_ERR("null", "-32600", BAD_ID));
    reply(t, a, PING("[1]"), AK_ERR("null", "-32600", BAD_ID));
    reply(t, a, PING("{\"a\":1}"), AK_ERR("null", "-32600", BAD_ID));
    reply(t, a, PING("1"), AK_OKR("1", "{}")); /* control */
}

static void t_jsonrpc_member_must_be_2_0(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, "{\"jsonrpc\":\"1.0\",\"id\":7,\"method\":\"ping\"}", AK_ERR("7", "-32600", BAD_VER));
    reply(t, a, "{\"jsonrpc\":\"2.0 \",\"id\":7,\"method\":\"ping\"}", AK_ERR("7", "-32600", BAD_VER));
    reply(t, a, "{\"jsonrpc\":2.0,\"id\":7,\"method\":\"ping\"}", AK_ERR("7", "-32600", BAD_VER));
    reply(t, a, "{\"jsonrpc\":null,\"id\":\"s\",\"method\":\"ping\"}", AK_ERR("\"s\"", "-32600", BAD_VER));
    reply(t, a, "{\"id\":7,\"method\":\"ping\"}", AK_ERR("7", "-32600", BAD_VER));
    reply(t, a, "{\"JSONRPC\":\"2.0\",\"id\":7,\"method\":\"ping\"}", AK_ERR("7", "-32600", BAD_VER));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"ping\"}", AK_OKR("7", "{}")); /* control */
}

static void t_invalid_request_without_id_still_gets_a_reply(ctest_t *t) {
    ak_t *a = ak_begin();
    reply(t, a, "{\"jsonrpc\":\"1.0\",\"method\":\"ping\"}", AK_ERR("null", "-32600", BAD_VER));
    reply(t, a, "{\"method\":\"ping\"}", AK_ERR("null", "-32600", BAD_VER));
    reply(t, a, "{\"jsonrpc\":\"2.0\"}", AK_ERR("null", "-32600", "Invalid Request: method must be a string"));
    reply(t, a, "{}", AK_ERR("null", "-32600", BAD_VER));
    silent(t, a, "{\"jsonrpc\":\"2.0\",\"method\":\"ping\"}"); /* control: the well-formed one is silent */
}

static void t_method_must_be_a_string(ctest_t *t) {
    ak_t *a = ak_begin();
    const char *msg = "Invalid Request: method must be a string";
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":3}", AK_ERR("3", "-32600", "Invalid Request: method must be a string"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":5}", AK_ERR("3", "-32600", "Invalid Request: method must be a string"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":null}", AK_ERR("3", "-32600", "Invalid Request: method must be a string"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":[\"ping\"]}", AK_ERR("3", "-32600", "Invalid Request: method must be a string"));
    reply(t, a, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":{}}", AK_ERR("3", "-32600", "Invalid Request: method must be a string"));
    /* the jsonrpc check comes first */
    reply(t, a, "{\"jsonrpc\":\"1.0\",\"id\":3,\"method\":5}", AK_ERR("3", "-32600", BAD_VER));
    /* a bad id comes before both */
    reply(t, a, "{\"jsonrpc\":\"1.0\",\"id\":true,\"method\":5}", AK_ERR("null", "-32600", BAD_ID));
    (void)msg;
}

static void t_unknown_methods(ctest_t *t) {
    ak_t *a = ak_begin();
    static const char *const methods[] = {"foo", "tools/lists", "tools/", "resources/list", "prompts/list", "",
                                          "Ping", "PING", "ping ", "initialized", "tools/call/", "completion/complete",
                                          "logging/setLevel", "notifications/initialised"};
    for (size_t i = 0; i < sizeof methods / sizeof methods[0]; i++) {
        char line[160];
        (void)snprintf(line, sizeof line, "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"%s\"}", methods[i]);
        reply(t, a, line, AK_ERR("9", "-32601", "Method not found"));
    }
    reply(t, a, PING("9"), AK_OKR("9", "{}")); /* control */
}

static void t_replies_never_contain_a_newline(ctest_t *t) {
    ak_t *a = ak_begin();
    CHECK_EQ_I64(t, ak_mcp(a, PING("\"a\\nb\\r\\n\"")), APP_MCP_REPLY);
    CHECK(t, strchr(ak_out(a), '\n') == NULL);
    CHECK(t, strchr(ak_out(a), '\r') == NULL);
    CHECK_EQ_I64(t, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"), APP_MCP_REPLY);
    CHECK(t, strchr(ak_out(a), '\n') == NULL);
}

static void t_output_is_appended(ctest_t *t) {
    ak_t *a = ak_begin();
    (void)ak_deps(a);
    CHECK(t, app_mcp_init(&a->mcp, &a->deps, a->parse_mem, sizeof a->parse_mem));
    core_buf_put_str(&a->out, core_str_from_cstr("PRE"));
    core_str_t line = core_str_from_cstr(PING("1"));
    CHECK_EQ_I64(t, app_mcp_handle(&a->mcp, line, &a->out), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), "PRE" AK_OKR("1", "{}"));
    core_str_t bl = core_str_from_cstr("   ");
    CHECK_EQ_I64(t, app_mcp_handle(&a->mcp, bl, &a->out), APP_MCP_NO_REPLY);
    CHECK_EQ_STR(t, ak_out(a), "PRE" AK_OKR("1", "{}"));
}

CTEST_SUITE("mcp_proto",
    {"initialize_echoes_supported_versions", t_initialize_echoes_supported_versions},
    {"initialize_unsupported_versions_get_the_newest", t_initialize_unsupported_versions_get_the_newest},
    {"initialize_params_are_validated", t_initialize_params_are_validated},
    {"initialize_twice_is_fine", t_initialize_twice_is_fine},
    {"initialized_notification", t_initialized_notification},
    {"other_notifications_are_silent", t_other_notifications_are_silent},
    {"ping", t_ping},
    {"surrounding_whitespace", t_surrounding_whitespace},
    {"tools_list", t_tools_list},
    {"tools_list_works_before_initialize_and_repeatedly", t_tools_list_works_before_initialize_and_repeatedly},
    {"id_numbers_are_echoed_verbatim", t_id_numbers_are_echoed_verbatim},
    {"id_strings_are_echoed_reencoded", t_id_strings_are_echoed_reencoded},
    {"id_null_is_a_request", t_id_null_is_a_request},
    {"id_of_other_types_is_invalid", t_id_of_other_types_is_invalid},
    {"jsonrpc_member_must_be_2_0", t_jsonrpc_member_must_be_2_0},
    {"invalid_request_without_id_still_gets_a_reply", t_invalid_request_without_id_still_gets_a_reply},
    {"method_must_be_a_string", t_method_must_be_a_string},
    {"unknown_methods", t_unknown_methods},
    {"replies_never_contain_a_newline", t_replies_never_contain_a_newline},
    {"output_is_appended", t_output_is_appended})
