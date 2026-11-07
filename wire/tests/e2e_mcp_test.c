/* wire, end to end through the MCP loop: tools/call over a REAL TLS handshake with the in-memory server. One TLS
 * stream, one directory and one workspace serve the successive calls of a session; failures of a call are results,
 * never the end of the loop. */
#include <errno.h>
#include <string.h>

#include "ctest/ctest.h"
#include "wire/tests/kit.h"

#define URL "https://dl.example.org/f/1"
#define FILE_URL "https://" KW_PEER_HOST "/files/b.bin"
#define PING_LINE KW_PING_REQ "\n"
#define DL_API KW_API_OK_BODY(FILE_URL)

#define CALL(id, name, args) \
    "{\"jsonrpc\":\"2.0\",\"id\":" id ",\"method\":\"tools/call\",\"params\":{\"name\":\"" name "\",\"arguments\":" args "}}\n"

static void t_fast_download_url(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(200, "OK", KW_API_OK_BODY(URL)));
    kw_in(CALL("3", "fast_download_url", "{\"hash\":\"" KW_MD5 "\",\"path_index\":1}") PING_LINE);
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 2);
    CHECK_EQ_STR(t, kw_out_line(0),
                 "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"Download URL: " URL
                 "\\u000aAccount: {\\\"downloads_left\\\":24}\"}],\"isError\":false}}");
    CHECK_EQ_STR(t, kw_out_line(1), KW_PING_REPLY);
    CHECK(t, kw_contains(kw_peer_request(0), "&path_index=1 HTTP/1.1\r\n"));
    CHECK_EQ_U64(t, kw()->fs.opens, 0);
    CHECK_EQ_U64(t, kw_err_len(), 0);
    kw_check_no_key(t, KW_KEY);
}

static void t_book_download(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(200, "OK", DL_API));
    kw_peer_respond(1, kw_http(200, "OK", "BOOKDATA"));
    kw_in(CALL("4", "book_download", "{\"hash\":\"" KW_MD5 "\",\"title\":\"A Title\",\"format\":\"pdf\"}"));
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 1);
    CHECK(t, kw_contains(kw_out(), "\"isError\":false"));
    CHECK(t, kw_contains(kw_out(), "Book downloaded successfully to: /dl/A Title.pdf"));
    CHECK_EQ_STR(t, kw()->fs.name, "A Title.pdf");
    CHECK_EQ_MEM(t, kw()->fs.content, kw()->fs.content_len, "BOOKDATA", 8);
    CHECK_EQ_U64(t, kw()->fs.opens, 1);
    CHECK_EQ_U64(t, kw()->fs.closes, 2); /* the temp file and the directory, closed when the run ends */
    kw_check_no_key(t, KW_KEY);
}

static void t_a_session_with_several_calls(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(200, "OK", DL_API));
    kw_peer_respond(1, kw_http(200, "OK", "ONE"));
    kw_peer_respond(2, kw_http(200, "OK", DL_API));
    kw_peer_respond(3, kw_http(200, "OK", "TWO"));
    kw_peer_respond(4, kw_http(200, "OK", KW_API_OK_BODY(URL)));
    kw_in(KW_INIT_REQ "\n" CALL("2", "book_download", "{\"hash\":\"" KW_MD5 "\",\"title\":\"one\"}")
          CALL("3", "book_download", "{\"hash\":\"" KW_MD5 "\",\"title\":\"two\"}")
          CALL("4", "fast_download_url", "{\"hash\":\"" KW_MD5 "\"}") PING_LINE);
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 5);
    CHECK_EQ_U64(t, kw_peer_connects(), 5);
    CHECK_EQ_U64(t, kw_peer_closes(), 5);
    CHECK(t, kw_contains(kw_out_line(1), "\"isError\":false"));
    CHECK(t, kw_contains(kw_out_line(2), "\"isError\":false"));
    CHECK(t, kw_contains(kw_out_line(3), "Download URL: " URL));
    CHECK_EQ_STR(t, kw_out_line(4), KW_PING_REPLY);
    CHECK_EQ_U64(t, kw()->fs.opens, 1); /* opened by the first download, kept for the second */
    CHECK_EQ_U64(t, kw()->fs.links, 2);
    CHECK_EQ_STR(t, kw()->fs.name, "two.bin");
    CHECK_EQ_MEM(t, kw()->fs.content, kw()->fs.content_len, "TWO", 3);
    kw_check_no_key(t, KW_KEY);
}

static void t_a_failing_call_does_not_end_the_session(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(401, "Unauthorized", "{\"download_url\":null,\"error\":\"Invalid secret key\"}"));
    kw_peer_respond(1, kw_http(200, "OK", KW_API_OK_BODY(URL)));
    kw_in(CALL("1", "fast_download_url", "{\"hash\":\"" KW_MD5 "\"}")
          CALL("2", "fast_download_url", "{\"hash\":\"" KW_MD5 "\"}"));
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 2);
    CHECK_EQ_STR(t, kw_out_line(0),
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"annas-c: E_API: 401: "
                 "Invalid secret key\"}],\"isError\":true}}");
    CHECK(t, kw_contains(kw_out_line(1), "\"isError\":false")); /* control: the same call succeeds next */
}

static void t_tls_and_network_failures_are_results(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_identity(2); /* TLS_PEER_EXPIRED */
    kw_in(CALL("1", "fast_download_url", "{\"hash\":\"" KW_MD5 "\"}") PING_LINE);
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 2);
    CHECK(t, kw_contains(kw_out_line(0), "annas-c: E_NET: TLS error\"}],\"isError\":true"));
    CHECK_EQ_STR(t, kw_out_line(1), KW_PING_REPLY);
    CHECK_EQ_U64(t, kw_peer_request(0)[0], 0); /* the key never reached the unverified server */
    kw_e2e_setup();
    kw_peer_connect_err(ECONNREFUSED);
    kw_in(CALL("1", "fast_download_url", "{\"hash\":\"" KW_MD5 "\"}") PING_LINE);
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK(t, kw_contains(kw_out_line(0), "annas-c: E_NET: connection failed\"}],\"isError\":true"));
    CHECK_EQ_STR(t, kw_out_line(1), KW_PING_REPLY);
}

static void t_the_directory_that_cannot_be_opened_is_a_result(ctest_t *t) {
    kw_e2e_setup();
    kw()->fs.open_err = -EACCES;
    kw_peer_respond(0, kw_http(200, "OK", DL_API));
    kw_peer_respond(1, kw_http(200, "OK", "x"));
    kw_in(CALL("1", "book_download", "{\"hash\":\"" KW_MD5 "\"}") PING_LINE);
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 2);
    CHECK(t, kw_contains(kw_out_line(0), "annas-c: E_FS: "));
    CHECK(t, kw_contains(kw_out_line(0), "\"isError\":true"));
    CHECK_EQ_STR(t, kw_out_line(1), KW_PING_REPLY);
    CHECK_EQ_U64(t, kw()->fs.links, 0);
}

static void t_the_title_is_the_key(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(200, "OK", DL_API));
    kw_peer_respond(1, kw_http(200, "OK", "x"));
    kw_in(CALL("1", "book_download", "{\"hash\":\"" KW_MD5 "\",\"title\":\"" KW_KEY "\"}"));
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_STR(t, kw()->fs.name, "REDACTED.bin");
    CHECK(t, kw_contains(kw_out(), "REDACTED.bin"));
    kw_check_no_key(t, KW_KEY);
}

static void t_the_session_through_annas_c_mcp_subcommand(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(200, "OK", KW_API_OK_BODY(URL)));
    kw_in(CALL("3", "fast_download_url", "{\"hash\":\"" KW_MD5 "\"}"));
    CHECK_EQ_I64(t, KW_CLI("mcp"), 0);
    CHECK(t, kw_contains(kw_out(), "Download URL: " URL));
}

CTEST_SUITE("wire_e2e_mcp", {"fast_download_url", t_fast_download_url}, {"book_download", t_book_download},
            {"a_session_with_several_calls", t_a_session_with_several_calls},
            {"a_failing_call_does_not_end_the_session", t_a_failing_call_does_not_end_the_session},
            {"tls_and_network_failures_are_results", t_tls_and_network_failures_are_results},
            {"the_directory_that_cannot_be_opened_is_a_result", t_the_directory_that_cannot_be_opened_is_a_result},
            {"the_title_is_the_key", t_the_title_is_the_key},
            {"the_session_through_annas_c_mcp_subcommand", t_the_session_through_annas_c_mcp_subcommand})
