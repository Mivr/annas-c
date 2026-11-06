/* wire: the MCP loop (annas-c-mcp and `annas-c mcp`): framing of the newline-delimited input over any pattern of reads,
 * one reply line per request, the reply is written before the next read, the 1 MiB line cap and the resync, the
 * handling at end of input, and the key scrubbing of the layered environment. No network is touched: the tool calls
 * here fail in app's validation. */
#include <string.h>

#include "ctest/ctest.h"
#include "wire/tests/kit.h"

#define INIT_REPLY KW_INIT_REPLY "\n"
#define PING_REPLY KW_PING_REPLY "\n"
#define BIG_ERR                                                                                              \
    "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32600,\"message\":\"Invalid Request: line longer than " \
    "1048576 bytes\"}}\n"
#define PARSE_ERR "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}\n"
#define NOTIF "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"

static void base(void) {
    kw_reset();
    kw_net_env();
}

static void t_empty_input(ctest_t *t) {
    base();
    kw_check(t, KW_MCP0(), 0, "", "");
    kw_check_no_empty_writes(t);
    CHECK_EQ_U64(t, kw()->w_calls[1], 0);
}

static void t_one_request(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, PING_REPLY, "");
    kw_check_no_empty_writes(t);
}

static void t_last_line_without_a_newline(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ);
    kw_check(t, KW_MCP0(), 0, PING_REPLY, "");
    base(); /* control: the same with the newline gives the same bytes */
    kw_in(KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, PING_REPLY, "");
}

static void t_many_lines_in_one_read(ctest_t *t) {
    base();
    kw_in(KW_INIT_REQ "\n" KW_PING_REQ "\n" KW_PING_REQ "\n" NOTIF "\n" KW_INIT_REQ "\n");
    kw_check(t, KW_MCP0(), 0, INIT_REPLY PING_REPLY PING_REPLY INIT_REPLY, "");
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 4);
}

static void t_any_read_pattern_gives_the_same_output(ctest_t *t) {
    static const char text[] = KW_INIT_REQ "\n" KW_PING_REQ "\n" NOTIF "\n\r\n" KW_PING_REQ "\r\n" KW_PING_REQ;
    static const size_t chunks[] = {1, 2, 3, 7, 64, 0};
    for (size_t i = 0; i < sizeof chunks / sizeof chunks[0]; i++) {
        base();
        kw_in(text);
        kw()->in_chunk = chunks[i];
        kw_check(t, KW_MCP0(), 0, INIT_REPLY PING_REPLY PING_REPLY PING_REPLY, "");
    }
}

static void t_a_line_split_across_reads(ctest_t *t) {
    base();
    static const char ping[] = KW_PING_REQ;
    kw_in_n(ping, 10);
    kw_in_n(ping + 10, sizeof ping - 1u - 10u);
    kw_in("\n");
    kw_in("\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, PING_REPLY PING_REPLY, "");
}

static void t_crlf_and_blank_lines(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ "\r\n" "\r\n" "\n" "   \t\n" "\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, PING_REPLY PING_REPLY, "");
}

static void t_notifications_get_no_reply(ctest_t *t) {
    base();
    kw_in(NOTIF "\n" "{\"jsonrpc\":\"2.0\",\"method\":\"ping\"}\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, PING_REPLY, "");
}

static void t_replies_have_no_other_bytes(ctest_t *t) {
    base();
    kw_in(KW_INIT_REQ "\n{\n" KW_PING_REQ "\n[]\n");
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 4);
    CHECK_EQ_U64(t, kw_err_len(), 0);
    CHECK_EQ_STR(t, kw_out_line(0), KW_INIT_REPLY);
    CHECK(t, kw_contains(kw_out_line(1), "-32700"));
    CHECK_EQ_STR(t, kw_out_line(2), KW_PING_REPLY);
    CHECK(t, kw_contains(kw_out_line(3), "batch requests are not supported"));
}

static void t_short_stdout_writes(ctest_t *t) {
    base();
    kw_in(KW_INIT_REQ "\n" KW_PING_REQ "\n");
    kw()->accept_max = 3;
    kw_check(t, KW_MCP0(), 0, INIT_REPLY PING_REPLY, "");
    CHECK(t, kw()->w_real[1] > 20u);
    base();
    kw_in(KW_INIT_REQ "\n" KW_PING_REQ "\n");
    kw()->w_eintr = 4;
    kw()->accept_max = 50;
    kw_check(t, KW_MCP0(), 0, INIT_REPLY PING_REPLY, "");
}

static void t_a_reply_is_written_before_the_next_read(ctest_t *t) {
    base();
    kw_in(KW_INIT_REQ "\n");
    kw_in(KW_PING_REQ "\n");
    kw_in(NOTIF "\n");
    kw_in(KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, INIT_REPLY PING_REPLY PING_REPLY, "");
    CHECK_EQ_U64(t, kw()->in_reads, 5); /* four segments and the end of input */
    CHECK_EQ_U64(t, kw()->out_at_read[0], 0);
    CHECK_EQ_U64(t, kw()->out_at_read[1], strlen(INIT_REPLY));
    CHECK_EQ_U64(t, kw()->out_at_read[2], strlen(INIT_REPLY PING_REPLY));
    CHECK_EQ_U64(t, kw()->out_at_read[3], strlen(INIT_REPLY PING_REPLY));
    CHECK_EQ_U64(t, kw()->out_at_read[4], strlen(INIT_REPLY PING_REPLY PING_REPLY));
}

static void t_one_write_sequence_per_reply(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ "\n" KW_PING_REQ "\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, PING_REPLY PING_REPLY PING_REPLY, "");
    CHECK_EQ_U64(t, kw()->w_real[1], 3); /* each reply and its newline in one call */
}

/* ---- the line cap --------------------------------------------------------------------------------------------- */

static void t_a_line_of_exactly_the_cap_is_handled_normally(ctest_t *t) {
    base(); /* a valid request padded with spaces to exactly 1 MiB: answered */
    kw_in(KW_PING_REQ);
    kw_in_fill(' ', APP_MCP_LINE_MAX - strlen(KW_PING_REQ));
    kw_in("\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, PING_REPLY PING_REPLY, "");
    base(); /* exactly the cap of garbage is a parse error, not the cap error */
    kw_in_fill('x', APP_MCP_LINE_MAX);
    kw_in("\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, PARSE_ERR PING_REPLY, "");
}

static void t_a_line_over_the_cap_is_one_error_and_a_resync(ctest_t *t) {
    base(); /* cap + 1 bytes */
    kw_in(KW_PING_REQ);
    kw_in_fill(' ', APP_MCP_LINE_MAX + 1u - strlen(KW_PING_REQ));
    kw_in("\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, BIG_ERR PING_REPLY, "");
    base(); /* cap + 2 */
    kw_in_fill('x', APP_MCP_LINE_MAX + 2u);
    kw_in("\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, BIG_ERR PING_REPLY, "");
    base(); /* much more, in several reads, then a short line */
    kw_in_fill('x', 3u * APP_MCP_LINE_MAX);
    kw_in_fill('y', 5);
    kw_in("\n" KW_PING_REQ "\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, BIG_ERR PING_REPLY PING_REPLY, "");
}

static void t_the_tail_of_a_long_line_is_not_a_request(ctest_t *t) {
    base(); /* the bytes after the cap form a valid request, but they belong to the rejected line */
    kw_in_fill('x', APP_MCP_LINE_MAX + 5u);
    kw_in(KW_PING_REQ "\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, BIG_ERR PING_REPLY, "");
}

static void t_a_long_line_at_the_end_of_input(ctest_t *t) {
    base();
    kw_in_fill('x', APP_MCP_LINE_MAX + 7u);
    kw_check(t, KW_MCP0(), 0, BIG_ERR, "");
    base(); /* the newline is the very next byte after the cap + 1 */
    kw_in_fill('x', APP_MCP_LINE_MAX + 1u);
    kw_in("\n");
    kw_in(KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, BIG_ERR PING_REPLY, "");
    base(); /* no newline at all, exactly the cap: a parse error at the end of input */
    kw_in_fill('x', APP_MCP_LINE_MAX);
    kw_check(t, KW_MCP0(), 0, PARSE_ERR, "");
}

static void t_reads_are_bounded_while_discarding(ctest_t *t) {
    base();
    kw_in_fill('x', 3u * APP_MCP_LINE_MAX);
    kw_in("\n");
    kw()->in_chunk = 4096;
    kw_check(t, KW_MCP0(), 0, BIG_ERR, "");
    CHECK(t, kw()->last_in_cap <= WIRE_READ_CHUNK);
}

/* ---- tool calls through the loop ---------------------------------------------------------------------------------- */

#define CALL_BAD_HASH \
    "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\",\"params\":{\"name\":\"fast_download_url\"," \
    "\"arguments\":{\"hash\":\"nothex\"}}}"

static void t_tools_list_and_a_failing_call(ctest_t *t) {
    base();
    kw_in("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/list\"}\n" CALL_BAD_HASH "\n" KW_PING_REQ "\n");
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 3);
    CHECK(t, kw_contains(kw_out_line(0), "\"name\":\"book_download\""));
    CHECK(t, kw_contains(kw_out_line(0), "\"name\":\"fast_download_url\""));
    CHECK(t, kw_contains(kw_out_line(1), "\"isError\":true"));
    CHECK(t, kw_contains(kw_out_line(1), "annas-c: E_ARG: "));
    CHECK_EQ_STR(t, kw_out_line(2), KW_PING_REPLY);
    CHECK_EQ_U64(t, kw()->refused, 0); /* the bad hash never reached a connection */
    kw_check_no_key(t, KW_KEY);
}

static void t_missing_configuration_is_a_normal_result(ctest_t *t) {
    kw_reset();
    kw_ca(KW_CA_FAKE); /* no key at all */
    kw_in("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\",\"params\":{\"name\":\"book_download\","
          "\"arguments\":{\"hash\":\"" KW_MD5 "\"}}}\n" KW_PING_REQ "\n");
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK(t, kw_contains(kw_out_line(0), "\"isError\":true"));
    CHECK(t, kw_contains(kw_out_line(0), "must be set"));
    CHECK_EQ_STR(t, kw_out_line(1), KW_PING_REPLY);
}

/* ---- the key ------------------------------------------------------------------------------------------------- */

#define PING_WITH_ID(id) "{\"jsonrpc\":\"2.0\",\"id\":\"" id "\",\"method\":\"ping\"}\n"

static void t_the_key_of_the_process_env_is_scrubbed(ctest_t *t) {
    base();
    kw_in(PING_WITH_ID("a" KW_KEY "b"));
    kw_check(t, KW_MCP0(), 0, "{\"jsonrpc\":\"2.0\",\"id\":\"aREDACTEDb\",\"result\":{}}\n", "");
    kw_check_no_key(t, KW_KEY);
}

static void t_the_key_of_dotenv_is_scrubbed(ctest_t *t) {
    kw_reset(); /* the key is only in the .env file: the layered env is what app scrubs with */
    kw_ca(KW_CA_FAKE);
    kw_file(".env", "ANNAS_SECRET_KEY=" KW_KEY "\n");
    kw_in(PING_WITH_ID("a" KW_KEY "b"));
    kw_check(t, KW_MCP0(), 0, "{\"jsonrpc\":\"2.0\",\"id\":\"aREDACTEDb\",\"result\":{}}\n", "");
    kw_check_no_key(t, KW_KEY);
}

static void t_without_a_key_nothing_is_scrubbed(ctest_t *t) {
    kw_reset(); /* control */
    kw_ca(KW_CA_FAKE);
    kw_in(PING_WITH_ID("a" KW_KEY "b"));
    kw_check(t, KW_MCP0(), 0, "{\"jsonrpc\":\"2.0\",\"id\":\"a" KW_KEY "b\",\"result\":{}}\n", "");
}

static void t_the_key_in_a_method_name_and_a_tool_name(ctest_t *t) {
    base();
    kw_in("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" KW_KEY "\"}\n"
          "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"" KW_KEY "\"}}\n");
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 2);
    kw_check_no_key(t, KW_KEY);
    CHECK(t, kw_contains(kw_out(), "REDACTED"));
}

static void t_memory_is_reusable_and_may_be_dirty(ctest_t *t) {
    base();
    memset(kw_mem(), 0xA5, sizeof(wire_mem_t));
    kw_in(KW_INIT_REQ "\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, INIT_REPLY PING_REPLY, "");
    kw_clear_output();
    kw_in(KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, PING_REPLY, "");
}

static void t_cli_subcommand_and_the_second_binary_are_the_same_loop(ctest_t *t) {
    base();
    kw_in(KW_INIT_REQ "\n" KW_PING_REQ "\n");
    kw_check(t, KW_CLI("mcp"), 0, INIT_REPLY PING_REPLY, "");
    kw_clear_output();
    kw_in(KW_INIT_REQ "\n" KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, INIT_REPLY PING_REPLY, "");
}

CTEST_SUITE("wire_loop", {"empty_input", t_empty_input}, {"one_request", t_one_request},
            {"last_line_without_a_newline", t_last_line_without_a_newline},
            {"many_lines_in_one_read", t_many_lines_in_one_read},
            {"any_read_pattern_gives_the_same_output", t_any_read_pattern_gives_the_same_output},
            {"a_line_split_across_reads", t_a_line_split_across_reads}, {"crlf_and_blank_lines", t_crlf_and_blank_lines},
            {"notifications_get_no_reply", t_notifications_get_no_reply},
            {"replies_have_no_other_bytes", t_replies_have_no_other_bytes},
            {"short_stdout_writes", t_short_stdout_writes},
            {"a_reply_is_written_before_the_next_read", t_a_reply_is_written_before_the_next_read},
            {"one_write_sequence_per_reply", t_one_write_sequence_per_reply},
            {"a_line_of_exactly_the_cap_is_handled_normally", t_a_line_of_exactly_the_cap_is_handled_normally},
            {"a_line_over_the_cap_is_one_error_and_a_resync", t_a_line_over_the_cap_is_one_error_and_a_resync},
            {"the_tail_of_a_long_line_is_not_a_request", t_the_tail_of_a_long_line_is_not_a_request},
            {"a_long_line_at_the_end_of_input", t_a_long_line_at_the_end_of_input},
            {"reads_are_bounded_while_discarding", t_reads_are_bounded_while_discarding},
            {"tools_list_and_a_failing_call", t_tools_list_and_a_failing_call},
            {"missing_configuration_is_a_normal_result", t_missing_configuration_is_a_normal_result},
            {"the_key_of_the_process_env_is_scrubbed", t_the_key_of_the_process_env_is_scrubbed},
            {"the_key_of_dotenv_is_scrubbed", t_the_key_of_dotenv_is_scrubbed},
            {"without_a_key_nothing_is_scrubbed", t_without_a_key_nothing_is_scrubbed},
            {"the_key_in_a_method_name_and_a_tool_name", t_the_key_in_a_method_name_and_a_tool_name},
            {"memory_is_reusable_and_may_be_dirty", t_memory_is_reusable_and_may_be_dirty},
            {"cli_subcommand_and_the_second_binary_are_the_same_loop",
             t_cli_subcommand_and_the_second_binary_are_the_same_loop})
