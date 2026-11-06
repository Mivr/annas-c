/* wire, end to end: the member key never appears in any byte written to stdout or stderr, whatever the server echoes
 * back and wherever the key came from, in the command line and in the MCP loop, raw or percent-encoded. Each case
 * checks that "REDACTED" took its place (so the echo really happened: the positive control). */
#include <string.h>

#include "ctest/ctest.h"
#include "wire/tests/kit.h"

#define RAW "FAKE KEY&x=1/+"
#define ENC "FAKE%20KEY%26x%3D1%2F%2B"
#define PING_LINE KW_PING_REQ "\n"

typedef struct {
    int status;
    const char *reason;
    const char *body;
    bool redacted; /* the key is echoed in the output, so REDACTED takes its place (else annas refuses the reply) */
} reply_t;

static const reply_t k_replies[] = {
    {400, "Bad Request", "{\"download_url\":null,\"error\":\"echo " RAW " end\"}", true},
    {400, "Bad Request", "{\"download_url\":null,\"error\":\"echo " ENC " end\"}", true},
    {500, "Server Error", "plain text echo " RAW " and " ENC " twice " RAW, true},
    {200, "OK", "{\"download_url\":\"https://dl.example.org/f?key=" ENC "\",\"account_fast_download_info\":{\"k\":\"" RAW "\"}}", false},
    {200, "OK", "{\"download_url\":\"https://dl.example.org/" RAW "\"}", false},
    {400, "Bad Request", "{\"download_url\":null,\"error\":\"" ENC ENC ENC "\"}", true},
    {200, "OK", "{\"download_url\":\"https://dl.example.org/f/1\",\"account_fast_download_info\":{\"k\":\"" RAW "\"}}", false}};

static void setup(const reply_t *r) {
    kw_e2e_setup();
    kw_env_clear();
    kw_env("ANNAS_SECRET_KEY", RAW);
    kw_env("ANNAS_BASE_URL", KW_PEER_HOST);
    kw_env("SSL_CERT_FILE", KW_CA_PATH);
    kw_peer_respond(0, kw_http(r->status, r->reason, r->body));
}

static void check_clean(ctest_t *t) {
    kw_check_no_key(t, RAW);
    kw_check_no_key(t, ENC);
    CHECK(t, kw_contains(kw_peer_request(0), "key=" ENC " HTTP/1.1")); /* the key did go to the server, encoded */
}

static void t_command_line_replies(ctest_t *t) {
    for (size_t i = 0; i < sizeof k_replies / sizeof k_replies[0]; i++) {
        setup(&k_replies[i]);
        (void)KW_CLI("download-url", KW_MD5);
        check_clean(t);
        CHECK_EQ_U64(t, kw_contains(kw_out(), "REDACTED") || kw_contains(kw_err(), "REDACTED"), k_replies[i].redacted);
    }
}

static void t_mcp_replies(ctest_t *t) {
    for (size_t i = 0; i < sizeof k_replies / sizeof k_replies[0]; i++) {
        setup(&k_replies[i]);
        kw_in("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"fast_download_url\","
              "\"arguments\":{\"hash\":\"" KW_MD5 "\"}}}\n" PING_LINE);
        CHECK_EQ_I64(t, KW_MCP0(), 0);
        check_clean(t);
        CHECK_EQ_U64(t, kw_contains(kw_out(), "REDACTED"), k_replies[i].redacted);
        CHECK_EQ_U64(t, kw_err_len(), 0);
        CHECK_EQ_U64(t, kw_check_reply_lines(t), 2);
    }
}

static void t_the_key_from_dotenv_only(ctest_t *t) {
    setup(&k_replies[0]);
    kw_env_clear();
    kw_env("ANNAS_BASE_URL", KW_PEER_HOST);
    kw_env("SSL_CERT_FILE", KW_CA_PATH);
    kw_file(".env", "ANNAS_SECRET_KEY=\"" RAW "\"\n");
    (void)KW_CLI("download-url", KW_MD5);
    check_clean(t);
    CHECK(t, kw_contains(kw_err(), "REDACTED"));
}

static void t_a_key_that_is_a_prefix_of_the_server_text(ctest_t *t) {
    /* a short key: scrubbing must not depend on its length being above that of "REDACTED" */
    kw_e2e_setup();
    kw_env_clear();
    kw_env("ANNAS_SECRET_KEY", "k3y");
    kw_env("ANNAS_BASE_URL", KW_PEER_HOST);
    kw_env("SSL_CERT_FILE", KW_CA_PATH);
    kw_peer_respond(0, kw_http(400, "Bad Request", "{\"download_url\":null,\"error\":\"k3y k3yk3y x\"}"));
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 4);
    CHECK(t, !kw_contains(kw_err(), "k3y"));
    CHECK(t, kw_contains(kw_err(), "REDACTED REDACTEDREDACTED x"));
}

CTEST_SUITE("wire_e2e_secret", {"command_line_replies", t_command_line_replies}, {"mcp_replies", t_mcp_replies},
            {"the_key_from_dotenv_only", t_the_key_from_dotenv_only},
            {"a_key_that_is_a_prefix_of_the_server_text", t_a_key_that_is_a_prefix_of_the_server_text})
