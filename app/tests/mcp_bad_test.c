/* app_mcp_handle: malformed and hostile input never crashes or wedges the server, and never touches a port.
 * Every rejection is followed by a ping that must still be answered (positive control). */
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"
#include "json/json.h"

#define PING "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}"
#define PONG AK_OKR("1", "{}")
#define PARSE_ERR AK_ERR("null", "-32700", "Parse error")
#define INVALID(msg) AK_ERR("null", "-32600", msg)
#define BATCH INVALID("Invalid Request: batch requests are not supported")
#define TOOLONG INVALID("Invalid Request: line longer than 1048576 bytes")

static void rejected(ctest_t *t, ak_t *a, const char *bytes, size_t len, const char *want) {
    ak_check_reply(t, a, ak_mcp_n(a, bytes, len), want);
    ak_check_reply(t, a, ak_mcp(a, PING), PONG); /* still alive */
    ak_check_no_io(t, a);
}

static void t_malformed_json(ctest_t *t) {
    static const char *const bad[] = {
        "{", "}", "[", "]", "not json", "{'a':1}", "{\"a\":01}", "{\"a\":1,}", "[1,", "nul", "tru", "\"unterminated",
        "{\"jsonrpc\":\"2.0\",", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"", (PING " x"), (PING PING), (PING "}"),
        "{} {}", "{\"a\":1,\"a\":2}", "{\"jsonrpc\":\"2.0\",\"id\":1,\"id\":2,\"method\":\"ping\"}",
        "{\"id\":\"\xff\"}", "{\"id\":\"\xc3\"}", "{\"id\":\"a\x01\"}", "{\"id\":\"a\tb\"}", "{\"id\":\"\\x\"}",
        "{\"id\":\"\\ud800\"}", "{\"id\":\"\\udc00\"}", "{\"id\":\"\\u12g4\"}", "{\"id\":1e}", "{\"id\":-}", "{\"id\":.5}",
        "{\"id\":1.}", ("\xef\xbb\xbf" PING), "{\"a\":NaN}", "{\"a\":Infinity}", "{\"a\":undefined}", "//c\n{}", ":", ",",
        "{\"a\" 1}", "{\"a\":}", "{:1}", "{1:1}", "[1 2]", "\x01", "\xff", "{\"a\":\"\xed\xa0\x80\"}",
        "{\"a\":\"\xc0\xaf\"}", "{\"a\":\"\xf4\x90\x80\x80\"}"};
    ak_t *a = ak_begin();
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        rejected(t, a, bad[i], strlen(bad[i]), PARSE_ERR);
    }
    ak_check_reply(t, a, ak_mcp(a, PING), PONG); /* control */
}

static void t_embedded_nul_is_a_parse_error(ctest_t *t) {
    ak_t *a = ak_begin();
    static const char with_nul[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\0";
    rejected(t, a, with_nul, sizeof with_nul - 1u, PONG); /* control: the same line without the NUL */
    rejected(t, a, with_nul, sizeof with_nul, PARSE_ERR);
    static const char mid[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\0\"method\":\"ping\"}";
    rejected(t, a, mid, sizeof mid - 1u, PARSE_ERR);
}

static void t_truncations_of_a_real_request_are_parse_errors(ctest_t *t) {
    static const char full[] =
        "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"book_download\","
        "\"arguments\":{\"hash\":\"0123456789abcdef0123456789abcdef\",\"title\":\"A \\u00e9 \\\"b\\\"\"}}}";
    ak_t *a = ak_begin();
    for (size_t n = 1; n + 1u < sizeof full; n++) {
        CHECK_EQ_I64(t, ak_mcp_n(a, full, n), APP_MCP_REPLY);
        CHECK_EQ_STR(t, ak_out(a), PARSE_ERR);
    }
    ak_check_no_io(t, a);
    ak_check_reply(t, a, ak_mcp(a, PING), PONG);
}

static void t_batches_are_rejected(ctest_t *t) {
    ak_t *a = ak_begin();
    rejected(t, a, "[]", 2, BATCH);
    rejected(t, a, "[1]", 3, BATCH);
    rejected(t, a, "[" PING "]", strlen("[" PING "]"), BATCH);
    rejected(t, a, "[" PING "," PING "]", strlen("[" PING "," PING "]"), BATCH);
    rejected(t, a, " [ ] ", 5, BATCH);
    rejected(t, a, "[[[[]]]]", 8, BATCH);
    rejected(t, a, "[{\"jsonrpc\":\"2.0\",\"method\":\"ping\"}]", 35, BATCH); /* a batch of notifications too */
}

static void t_non_object_roots(ctest_t *t) {
    ak_t *a = ak_begin();
    rejected(t, a, "1", 1, INVALID("Invalid Request"));
    rejected(t, a, "-1.5e3", 6, INVALID("Invalid Request"));
    rejected(t, a, "\"ping\"", 6, INVALID("Invalid Request"));
    rejected(t, a, "null", 4, INVALID("Invalid Request"));
    rejected(t, a, "true", 4, INVALID("Invalid Request"));
    rejected(t, a, "false", 5, INVALID("Invalid Request"));
}

static void t_blank_lines_get_no_reply(ctest_t *t) {
    ak_t *a = ak_begin();
    static const char *const blank[] = {"", " ", "   ", "\t", "\r", "\r\n", " \t\r\n ", "\n\n"};
    for (size_t i = 0; i < sizeof blank / sizeof blank[0]; i++) {
        CHECK_EQ_I64(t, ak_mcp(a, blank[i]), APP_MCP_NO_REPLY);
        CHECK_EQ_U64(t, core_buf_len(&a->out), 0);
    }
    ak_check_no_io(t, a);
    rejected(t, a, "\v", 1, PARSE_ERR); /* control: \v and \f are not JSON whitespace */
    rejected(t, a, "\f", 1, PARSE_ERR);
    rejected(t, a, "\xc2\xa0", 2, PARSE_ERR);
    rejected(t, a, "0", 1, INVALID("Invalid Request"));
}

static void t_deep_nesting(ctest_t *t) {
    static char buf[20100];
    ak_t *a = ak_begin();
    memset(buf, '[', 64);
    memset(buf + 64, ']', 64);
    rejected(t, a, buf, 128, BATCH); /* depth 64 parses, then it is an array */
    memset(buf, '[', 65);
    memset(buf + 65, ']', 65);
    rejected(t, a, buf, 130, PARSE_ERR); /* depth 65 does not */
    memset(buf, '[', 10000);
    memset(buf + 10000, ']', 10000);
    rejected(t, a, buf, 20000, PARSE_ERR);
    memset(buf, '[', 20000); /* unclosed */
    rejected(t, a, buf, 20000, PARSE_ERR);
    for (size_t i = 0; i < 20000; i += 4) { /* {"":{"":{"":... unclosed */
        memcpy(buf + i, "{\"\":", 4);
    }
    rejected(t, a, buf, 20000, PARSE_ERR);
    /* nested inside a valid request: depth counts from the root */
    static char req[300];
    int n = snprintf(req, sizeof req, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":");
    memset(req + n, '[', 62);
    memset(req + n + 62, ']', 62);
    n += 124;
    req[n++] = '}';
    ak_check_reply(t, a, ak_mcp_n(a, req, (size_t)n), PONG); /* depth 63 */
    n = snprintf(req, sizeof req, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":");
    memset(req + n, '[', 64);
    memset(req + n + 64, ']', 64);
    n += 128;
    req[n++] = '}';
    ak_check_reply(t, a, ak_mcp_n(a, req, (size_t)n), PARSE_ERR); /* depth 65 */
}

static void t_binary_garbage(ctest_t *t) {
    static char buf[4096];
    unsigned long long s = 88172645463325252ull;
    ak_t *a = ak_begin();
    for (int round = 0; round < 64; round++) {
        size_t n = (size_t)(round * 61 + 1);
        for (size_t i = 0; i < n; i++) {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            buf[i] = (char)(s >> 24);
        }
        app_mcp_result_t r = ak_mcp_n(a, buf, n);
        CHECK(t, r == APP_MCP_REPLY || r == APP_MCP_NO_REPLY);
        if (r == APP_MCP_REPLY) {
            CHECK(t, strncmp(ak_out(a), "{\"jsonrpc\":\"2.0\",\"id\":", 22) == 0);
            CHECK(t, strchr(ak_out(a), '\n') == NULL);
        }
    }
    ak_check_no_io(t, a);
    ak_check_reply(t, a, ak_mcp(a, PING), PONG);
}

/* ---- the line cap ----------------------------------------------------------------------------------------- */

static size_t padded(ak_t *a, size_t total) {
    size_t n = strlen(PING);
    memcpy(a->line, PING, n);
    memset(a->line + n, ' ', total - n);
    return total;
}

static void t_line_cap(ctest_t *t) {
    ak_t *a = ak_begin();
    size_t n = padded(a, APP_MCP_LINE_MAX);
    ak_check_reply(t, a, ak_mcp_n(a, a->line, n), PONG); /* exactly the cap is handled */
    n = padded(a, APP_MCP_LINE_MAX - 1u);
    ak_check_reply(t, a, ak_mcp_n(a, a->line, n), PONG);
    n = padded(a, APP_MCP_LINE_MAX + 1u);
    ak_check_reply(t, a, ak_mcp_n(a, a->line, n), TOOLONG);
    n = padded(a, APP_MCP_LINE_MAX + 60u);
    ak_check_reply(t, a, ak_mcp_n(a, a->line, n), TOOLONG);
    ak_check_reply(t, a, ak_mcp(a, PING), PONG);
    ak_check_no_io(t, a);
}

static void t_line_cap_comes_before_parsing(ctest_t *t) {
    ak_t *a = ak_begin();
    memset(a->line, 'x', APP_MCP_LINE_MAX + 1u); /* not JSON at all */
    ak_check_reply(t, a, ak_mcp_n(a, a->line, APP_MCP_LINE_MAX + 1u), TOOLONG);
    memset(a->line, ' ', APP_MCP_LINE_MAX + 1u); /* blank but too long */
    ak_check_reply(t, a, ak_mcp_n(a, a->line, APP_MCP_LINE_MAX + 1u), TOOLONG);
    memset(a->line, ' ', APP_MCP_LINE_MAX); /* control: blank at the cap is blank */
    CHECK_EQ_I64(t, ak_mcp_n(a, a->line, APP_MCP_LINE_MAX), APP_MCP_NO_REPLY);
    memset(a->line, 'x', APP_MCP_LINE_MAX); /* control: garbage at the cap is a parse error */
    ak_check_reply(t, a, ak_mcp_n(a, a->line, APP_MCP_LINE_MAX), PARSE_ERR);
}

static void t_large_valid_request(ctest_t *t) {
    ak_t *a = ak_begin();
    static const char head[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":{\"x\":\"";
    static const char tail[] = "\"}}";
    size_t hl = sizeof head - 1u;
    size_t n = 900000;
    memcpy(a->line, head, hl);
    memset(a->line + hl, 'a', n);
    memcpy(a->line + hl + n, tail, sizeof tail - 1u);
    ak_check_reply(t, a, ak_mcp_n(a, a->line, hl + n + 3u), PONG);
    /* a 1 MiB + string cannot fit in a line at all */
    ak_check_no_io(t, a);
}

static void t_many_values_in_one_line(ctest_t *t) {
    ak_t *a = ak_begin();
    size_t n = 0;
    n += (size_t)snprintf(a->line, 200, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":[");
    for (int i = 0; i < 200000 && n < APP_MCP_LINE_MAX - 8u; i++) {
        a->line[n++] = '1';
        a->line[n++] = ',';
    }
    a->line[n++] = '1';
    a->line[n++] = ']';
    a->line[n++] = '}';
    app_mcp_result_t r = ak_mcp_n(a, a->line, n);
    CHECK(t, r == APP_MCP_REPLY); /* either pong or an error reply, never a crash or a hang */
    CHECK(t, strchr(ak_out(a), '\n') == NULL);
    ak_check_reply(t, a, ak_mcp(a, PING), PONG);
}

/* ---- memory and argument handling ----------------------------------------------------------------------- */

static void t_request_larger_than_the_parse_memory(ctest_t *t) {
    ak_t *a = ak_begin();
    static app_mcp_t srv;
    static unsigned char small[APP_MCP_ARENA_MIN];
    static const char head[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":{\"x\":\"";
    CHECK(t, app_mcp_init(&srv, ak_deps(a), small, sizeof small));
    core_str_t ping = core_str_from_cstr(PING);
    ak_clear(a);
    CHECK_EQ_I64(t, app_mcp_handle(&srv, ping, &a->out), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), PONG);
    size_t hl = sizeof head - 1u;
    memcpy(a->line, head, hl);
    memset(a->line + hl, 'a', 100000);
    memcpy(a->line + hl + 100000, "\"}}", 3);
    core_str_t big = {a->line, hl + 100003u};
    ak_clear(a);
    CHECK_EQ_I64(t, app_mcp_handle(&srv, big, &a->out), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), AK_ERR("null", "-32603", "Internal error: request does not fit in memory"));
    ak_clear(a); /* the server is still usable, and the memory was released */
    CHECK_EQ_I64(t, app_mcp_handle(&srv, ping, &a->out), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), PONG);
    ak_check_no_io(t, a);
}

static void t_init_refusals(ctest_t *t) {
    ak_t *a = ak_begin();
    static app_mcp_t srv;
    static unsigned char mem[APP_MCP_ARENA_MIN];
    const app_deps_t *d = ak_deps(a);
    CHECK(t, app_mcp_init(&srv, d, mem, sizeof mem)); /* control: exactly the minimum */
    CHECK(t, !app_mcp_init(&srv, d, mem, sizeof mem - 1u));
    CHECK(t, !app_mcp_init(&srv, d, mem, 0));
    CHECK(t, !app_mcp_init(&srv, d, NULL, sizeof mem));
    CHECK(t, !app_mcp_init(&srv, NULL, mem, sizeof mem));
    CHECK(t, !app_mcp_init(NULL, d, mem, sizeof mem));
}

static void t_handle_argument_errors(ctest_t *t) {
    ak_t *a = ak_begin();
    (void)ak_deps(a);
    CHECK(t, app_mcp_init(&a->mcp, &a->deps, a->parse_mem, sizeof a->parse_mem));
    core_str_t ping = core_str_from_cstr(PING);
    CHECK_EQ_I64(t, app_mcp_handle(NULL, ping, &a->out), APP_MCP_E_ARG);
    CHECK_EQ_I64(t, app_mcp_handle(&a->mcp, ping, NULL), APP_MCP_E_ARG);
    core_str_t nul = {NULL, 5};
    CHECK_EQ_I64(t, app_mcp_handle(&a->mcp, nul, &a->out), APP_MCP_E_ARG);
    CHECK_EQ_U64(t, core_buf_len(&a->out), 0);
    core_str_t none = {NULL, 0}; /* control: an empty line is a blank line */
    CHECK_EQ_I64(t, app_mcp_handle(&a->mcp, none, &a->out), APP_MCP_NO_REPLY);
    CHECK_EQ_I64(t, app_mcp_handle(&a->mcp, ping, &a->out), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), PONG);
}

static void t_output_buffer_too_small(ctest_t *t) {
    ak_t *a = ak_begin();
    (void)ak_deps(a);
    CHECK(t, app_mcp_init(&a->mcp, &a->deps, a->parse_mem, sizeof a->parse_mem));
    char tiny[16];
    core_buf_t small;
    CHECK(t, core_buf_init(&small, tiny, sizeof tiny));
    CHECK_EQ_I64(t, app_mcp_handle(&a->mcp, core_str_from_cstr(PING), &small), APP_MCP_E_OVERFLOW);
    CHECK(t, core_buf_overflow(&small));
    CHECK_EQ_I64(t, ak_mcp(a, PING), APP_MCP_REPLY); /* control: the large buffer works */
    CHECK_EQ_STR(t, ak_out(a), PONG);
}

/* ---- every reply, whatever the input, is one well-formed JSON-RPC object -------------------------------- */

static void check_wellformed(ctest_t *t, ak_t *a) {
    static unsigned char mem[65536];
    core_arena_t ar;
    const json_value_t *root = NULL;
    core_str_t rep = core_buf_str(&a->out);
    CHECK(t, core_arena_init(&ar, mem, sizeof mem));
    CHECK_EQ_I64(t, json_parse(&ar, rep, NULL, &root, NULL), JSON_OK);
    CHECK_EQ_I64(t, json_type(root), JSON_OBJECT);
    core_str_t v = {NULL, 0};
    CHECK(t, json_get_str(json_obj_get(root, core_str_from_cstr("jsonrpc")), &v));
    CHECK(t, ak_eq(v, "2.0"));
    const json_value_t *res = json_obj_get(root, core_str_from_cstr("result"));
    const json_value_t *err = json_obj_get(root, core_str_from_cstr("error"));
    CHECK(t, (res != NULL) != (err != NULL));
    CHECK(t, json_obj_get(root, core_str_from_cstr("id")) != NULL);
    CHECK_EQ_U64(t, json_len(root), 3);
    if (err != NULL) {
        int64_t code = 0;
        CHECK(t, json_get_i64(json_obj_get(err, core_str_from_cstr("code")), &code));
        CHECK(t, code == -32700 || code == -32600 || code == -32601 || code == -32602 || code == -32603);
        CHECK(t, json_get_str(json_obj_get(err, core_str_from_cstr("message")), &v));
        CHECK(t, v.len > 0);
        CHECK_EQ_U64(t, json_len(err), 2);
    }
}

static void t_single_byte_mutations_of_valid_requests(ctest_t *t) {
    static const char *const seeds[] = {
        PING,
        "{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}",
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"nope\",\"arguments\":{}}}"};
    static const unsigned char subst[] = {0, '"', '\\', '{', '}', '[', ']', ',', ':', '1', '-', 'e', 'n', 0xff, '\n', ' '};
    ak_t *a = ak_begin();
    static char buf[256];
    for (size_t s = 0; s < sizeof seeds / sizeof seeds[0]; s++) {
        size_t n = strlen(seeds[s]);
        for (size_t i = 0; i < n; i++) {
            for (size_t k = 0; k < sizeof subst; k++) {
                memcpy(buf, seeds[s], n);
                buf[i] = (char)subst[k];
                app_mcp_result_t r = ak_mcp_n(a, buf, n);
                CHECK(t, r == APP_MCP_REPLY || r == APP_MCP_NO_REPLY);
                if (r == APP_MCP_REPLY) {
                    check_wellformed(t, a);
                    CHECK(t, strchr(ak_out(a), '\n') == NULL);
                }
            }
        }
    }
    ak_check_no_io(t, a); /* none of the mutants is a runnable tool call */
    ak_check_reply(t, a, ak_mcp(a, PING), PONG);
}

static void t_token_soup(ctest_t *t) {
    static const char *const tok[] = {"{", "}", "[", "]", ",", ":", "\"jsonrpc\"", "\"2.0\"", "\"id\"", "\"method\"",
                                      "\"params\"", "\"ping\"", "\"tools/call\"", "\"name\"", "\"arguments\"", "1",
                                      "-1", "null", "true", "\"hash\"", "\"x\"", "\"\\u00e9\"", " ", "\"\\", "0.5e"};
    static char buf[1024];
    unsigned long long s = 0x9e3779b97f4a7c15ull;
    ak_t *a = ak_begin();
    for (int round = 0; round < 4000; round++) {
        size_t n = 0;
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        size_t count = (size_t)((s >> 33) % 40u);
        for (size_t i = 0; i < count; i++) {
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            const char *tk = tok[(s >> 33) % (sizeof tok / sizeof tok[0])];
            size_t l = strlen(tk);
            if (n + l < sizeof buf) {
                memcpy(buf + n, tk, l);
                n += l;
            }
        }
        app_mcp_result_t r = ak_mcp_n(a, buf, n);
        CHECK(t, r == APP_MCP_REPLY || r == APP_MCP_NO_REPLY);
        if (r == APP_MCP_REPLY) {
            check_wellformed(t, a);
        }
    }
    ak_check_no_io(t, a);
    ak_check_reply(t, a, ak_mcp(a, PING), PONG);
}

CTEST_SUITE("mcp_bad",
    {"malformed_json", t_malformed_json},
    {"embedded_nul_is_a_parse_error", t_embedded_nul_is_a_parse_error},
    {"truncations_of_a_real_request_are_parse_errors", t_truncations_of_a_real_request_are_parse_errors},
    {"batches_are_rejected", t_batches_are_rejected},
    {"non_object_roots", t_non_object_roots},
    {"blank_lines_get_no_reply", t_blank_lines_get_no_reply},
    {"deep_nesting", t_deep_nesting},
    {"binary_garbage", t_binary_garbage},
    {"line_cap", t_line_cap},
    {"line_cap_comes_before_parsing", t_line_cap_comes_before_parsing},
    {"large_valid_request", t_large_valid_request},
    {"many_values_in_one_line", t_many_values_in_one_line},
    {"request_larger_than_the_parse_memory", t_request_larger_than_the_parse_memory},
    {"init_refusals", t_init_refusals},
    {"handle_argument_errors", t_handle_argument_errors},
    {"output_buffer_too_small", t_output_buffer_too_small},
    {"single_byte_mutations_of_valid_requests", t_single_byte_mutations_of_valid_requests},
    {"token_soup", t_token_soup})
