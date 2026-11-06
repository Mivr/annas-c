/* Boundary tests for the pieces of app that the mutation run found weakly pinned (A037; R004 T6/T7 classes):
 *  - the percent-encoded form of the key, byte by byte (every byte class, the hex digits, near misses);
 *  - the output limit of the first scrubbing pass, at the limit and one byte over;
 *  - the UTF-8 cleaning of MCP texts: every range edge of the second byte, a sequence ending the text;
 *  - the memory a tools/call needs after the request DOM (APP_MCP_CALL_MEM + 6 x the id length), exactly at the limit
 *    and one byte short, for every kind of id;
 *  - the reply buffer that is exactly as large as what the DOM left (reply length + NUL).
 * Only the public header is used. */
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"
#include "json/json.h"

#define MD5 AK_MD5
#define URL "https://dl.example.org/f/1"
#define JSON_HDR "Content-Type: application/json\r\n"
#define HASH "\"hash\":\"" MD5 "\""
#define NOMEM_NULL AK_ERR("null", "-32603", "Internal error: request does not fit in memory")

/* ---- the percent-encoded key ----------------------------------------------------------------------------------- */

static char g_key[512];
static char g_enc[2048];
static char g_wrap[2048];
static char g_line[4096];
static char g_exp[4096];

/* url_pct_encode written out independently: unreserved bytes are kept, every other byte is %XX in upper-case hex. */
static void encode(const char *key, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)key; *p != 0; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' ||
            *p == '.' || *p == '_' || *p == '~') {
            out[o++] = (char)*p;
        } else {
            out[o++] = '%';
            out[o++] = hex[*p >> 4];
            out[o++] = hex[*p & 15u];
        }
    }
    out[o] = '\0';
}

static void ping_id(ctest_t *t, ak_t *a, const char *id, const char *shown) {
    (void)snprintf(g_line, sizeof g_line, "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"method\":\"ping\"}", id);
    (void)snprintf(g_exp, sizeof g_exp, AK_OKR("\"%s\"", "{}"), shown);
    CHECK_EQ_I64(t, ak_mcp(a, g_line), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), g_exp);
}

/* Every byte 0x01..0xFF in one key (255 bytes, within ANNAS_KEY_MAX): the id carrying its encoded form is redacted
 * whole, so each byte's class (kept / %XX) and both hex digits of each %XX are what the scrubbing expects. */
static void t_every_byte_value_in_the_encoded_key(ctest_t *t) {
    ak_t *a = ak_begin();
    for (int i = 1; i < 256; i++) {
        g_key[i - 1] = (char)i;
    }
    g_key[255] = '\0';
    ak_env(a, "ANNAS_SECRET_KEY", g_key);
    encode(g_key, g_enc);
    CHECK_EQ_U64(t, strlen(g_enc), 66u + 3u * 189u); /* 66 unreserved bytes kept, 189 bytes as %XX */
    ping_id(t, a, g_enc, "REDACTED");
    (void)snprintf(g_wrap, sizeof g_wrap, "<%s>", g_enc);
    ping_id(t, a, g_wrap, "<REDACTED>");
    ak_check_no_io(t, a);
}

/* The same, one byte at a time: the bytes next to each edge of the unreserved set, and the set's members. */
static void t_encoded_form_of_each_edge_byte(ctest_t *t) {
    static const unsigned char edge[] = {'@', 'A', 'Z', '[', '`', 'a', 'z', '{', '/', '0', '9', ':', '-', '.', ',',
                                         '_', '~', '^', '!', '}', ' ', '+', '=', '%', 0x7f, 0x80, 0xff, 0x01, 0x1f};
    for (size_t i = 0; i < sizeof edge; i++) {
        ak_t *a = ak_begin();
        g_key[0] = '(';
        g_key[1] = (char)edge[i];
        g_key[2] = ')';
        g_key[3] = '\0';
        ak_env(a, "ANNAS_SECRET_KEY", g_key);
        encode(g_key, g_enc);
        ping_id(t, a, g_enc, "REDACTED");
        (void)snprintf(g_wrap, sizeof g_wrap, "a%sb", g_enc);
        ping_id(t, a, g_wrap, "aREDACTEDb");
        ak_check_no_io(t, a);
    }
}

/* Near misses of the encoded key "a%2Fb" (key "a/b"): one byte wrong at a time, and the lower-case hex. */
static void t_encoded_key_near_misses(ctest_t *t) {
    static const char *const miss[] = {"x%2Fb", "a%2Fx", "a?2Fb", "a%3Fb", "a%2Eb", "a%2fb", "a%2F", "a%2", "a%",
                                       "%2Fb",  "a2Fb", "a%%2Fb", "A%2Fb", "a%22Fb"};
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "a/b");
    ping_id(t, a, "a%2Fb", "REDACTED"); /* control */
    for (size_t i = 0; i < sizeof miss / sizeof miss[0]; i++) {
        ping_id(t, a, miss[i], miss[i]);
    }
    ak_check_no_io(t, a);
    a = ak_begin(); /* unreserved bytes of the key must match as themselves, not as %XX */
    ak_env(a, "ANNAS_SECRET_KEY", "a b~c");
    ping_id(t, a, "a%20b~c", "REDACTED");
    ping_id(t, a, "a%20b%7Ec", "a%20b%7Ec"); /* '~' is kept, never encoded */
    ping_id(t, a, "a%20bxc", "a%20bxc");
    ping_id(t, a, "a%20b~", "a%20b~");
    ak_check_no_io(t, a);
}

/* A key that url_pct_encode leaves unchanged gets pass 1 only: "REDACTED" written by pass 1 contains "EDA", and a
 * second pass would redact that again. */
static void t_key_that_encodes_to_itself_gets_one_pass(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "EDA");
    ping_id(t, a, "EDA", "REDACTED");
    ping_id(t, a, "xEDAy", "xREDACTEDy");
    ping_id(t, a, "EDAEDA", "REDACTEDREDACTED");
    ak_check_no_io(t, a);
    a = ak_begin(); /* control: with one reserved byte there are two passes, and the second finds the combined form */
    ak_env(a, "ANNAS_SECRET_KEY", "ED A");
    ping_id(t, a, "ED A", "REDACTED");
    ping_id(t, a, "ED%20A", "REDACTED");
    ak_check_no_io(t, a);
}

/* ---- the limit of pass 1 (APP_FIN_MAX = 65536 bytes) ------------------------------------------------------------- */

static char g_big_line[70000];
static char g_big_exp[70000];
static char g_o_store[70000];
static core_buf_t g_o; /* AK_OUT_CAP is 65536 bytes, one short of a reply of APP_MCP_REPLY_MAX bytes and its NUL */

/* Key "z" (8 bytes of output per occurrence; the rest of the reply, and of the -32603 reply, has no 'z'). The reply is
 * {"jsonrpc":"2.0","id":"<id>","result":{}} = 37 bytes + the id; `na` letters z and `nb` letters b make it
 * 37 + 8 na + nb after pass 1. */
static app_mcp_result_t ping_big(ak_t *a, size_t nb_first, size_t na, size_t nb_last) {
    size_t n = 0;
    static const char pre[] = "{\"jsonrpc\":\"2.0\",\"id\":\"";
    memcpy(g_big_line, pre, sizeof pre - 1u);
    n = sizeof pre - 1u;
    memset(g_big_line + n, 'b', nb_first);
    n += nb_first;
    memset(g_big_line + n, 'z', na);
    n += na;
    memset(g_big_line + n, 'b', nb_last);
    n += nb_last;
    memcpy(g_big_line + n, "\",\"method\":\"ping\"}", 19);
    n += 18u;
    (void)ak_mcp_n(a, g_big_line, 0); /* creates the server; a blank line gets no reply */
    (void)core_buf_init(&g_o, g_o_store, sizeof g_o_store);
    return app_mcp_handle(&a->mcp, (core_str_t){g_big_line, n}, &g_o);
}

static void check_big(ctest_t *t, size_t nb_first, size_t na, size_t nb_last, bool fits) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "z");
    app_mcp_result_t r = ping_big(a, nb_first, na, nb_last);
    if (fits) {
        size_t n = 0;
        CHECK_EQ_I64(t, r, APP_MCP_REPLY);
        CHECK_EQ_U64(t, core_buf_len(&g_o), 37u + 8u * na + nb_first + nb_last);
        memcpy(g_big_exp, "{\"jsonrpc\":\"2.0\",\"id\":\"", 23);
        n = 23;
        memset(g_big_exp + n, 'b', nb_first);
        n += nb_first;
        for (size_t i = 0; i < na; i++) {
            memcpy(g_big_exp + n, "REDACTED", 8);
            n += 8;
        }
        memset(g_big_exp + n, 'b', nb_last);
        n += nb_last;
        memcpy(g_big_exp + n, "\",\"result\":{}}", 15);
        CHECK_EQ_STR(t, core_buf_cstr(&g_o), g_big_exp);
    } else {
        CHECK_EQ_I64(t, r, APP_MCP_REPLY);
        CHECK_EQ_STR(t, core_buf_cstr(&g_o), NOMEM_NULL);
    }
    ak_check_no_io(t, a);
}

static void t_pass_one_output_at_the_limit_ending_in_a_redaction(ctest_t *t) {
    check_big(t, 3, 8187, 0, true);  /* 37 + 65496 + 3 = 65536 */
    check_big(t, 4, 8187, 0, false); /* one byte over */
}

static void t_pass_one_output_at_the_limit_ending_in_plain_bytes(ctest_t *t) {
    check_big(t, 0, 8187, 3, true);
    check_big(t, 0, 8187, 4, false);
    check_big(t, 0, 8188, 0, false); /* a whole redaction over: 65541 */
    check_big(t, 0, 8186, 11, true); /* 37 + 65488 + 11 = 65536 */
    check_big(t, 0, 8186, 12, false);
}

/* ---- UTF-8 cleaning ---------------------------------------------------------------------------------------------- */


static void script_500(ak_t *a, const char *body) {
    kit_api(a->k, MD5, "", kit_http(a->k, 500, "Error", "", body));
}

static app_mcp_result_t call_fast(ak_t *a) {
    return ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"fast_download_url\","
                     "\"arguments\":{" HASH "}}}");
}

static void check_text(ctest_t *t, ak_t *a, app_mcp_result_t r, const char *text) {
    (void)snprintf(g_exp, sizeof g_exp,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"annas-c: E_API: 500: "
                   "%s\"}],\"isError\":true}}",
                   text);
    ak_check_reply(t, a, r, g_exp);
    ak_check_stream(t, a);
}

typedef struct {
    const char *body;
    const char *want;
} utf8_case_t;

/* Second bytes just outside the range of their lead byte, on each side, for each lead byte class. */
static const utf8_case_t k_bad[] = {
    {"x\xc2\xc0y", "x??y"},
    {"x\xc2\xff" "y", "x??y"},
    {"x\xc2" "Ay", "x?Ay"},
    {"x\xc2\x7f" "y", "x??y"},
    {"x\xdf\xc0y", "x??y"},
    {"x\xdf" "ay", "x?ay"},
    {"x\xe1\xc0\x80y", "x???y"},
    {"x\xe1\x7f\x80y", "x???y"},
    {"x\xe1\x80\xc0y", "x???y"},
    {"x\xe1\x80\x7fy", "x???y"},
    {"x\xe0\x80\x80y", "x???y"},
    {"x\xe0\xc0\x80y", "x???y"},
    {"x\xe0\xa0\xc0y", "x???y"},
    {"x\xec\xc0\x80y", "x???y"},
    {"x\xed\xc0\x80y", "x???y"},
    {"x\xed\x80\xc0y", "x???y"},
    {"x\xee\xc0\x80y", "x???y"},
    {"x\xef\x80\xc0y", "x???y"},
    {"x\xf1\xc0\x80\x80y", "x????y"},
    {"x\xf1\x80\xc0\x80y", "x????y"},
    {"x\xf1\x80\x80\xc0y", "x????y"},
    {"x\xf1\x7f\x80\x80y", "x????y"},
    {"x\xf0\x80\x80\x80y", "x????y"},
    {"x\xf0\xc0\x80\x80y", "x????y"},
    {"x\xf0\x90\x80\xc0y", "x????y"},
    {"x\xf3\xc0\x80\x80y", "x????y"},
    {"x\xf4\xbf\x80\x80y", "x????y"},
    {"x\xf4\xc0\x80\x80y", "x????y"},
    {"x\xf4\x8f\x80\xc0y", "x????y"},
    {"x\xf4\x7f\x80\x80y", "x????y"}};

static const char *const k_good[] = {
    "x\xc2\xbfy",     "x\xc2\xa0y",     "x\xdf\x80y",     "x\xe0\xbf\xbfy", "x\xe1\x80\x80y",
    "x\xe1\xbf\xbfy", "x\xec\x80\x80y", "x\xed\x80\x80y", "x\xee\xbf\xbfy", "x\xef\x80\x80y",
    "x\xf0\xbf\xbf\xbfy", "x\xf1\x80\x80\x80y", "x\xf1\xbf\xbf\xbfy", "x\xf3\x8f\xbf\xbfy", "x\xf4\x80\x80\x80y",
    /* the sequence is the last thing in the text */
    "x\xc2\x80", "x\xdf\xbf", "x\xe0\xa0\x80", "x\xe2\x82\xac", "x\xef\xbf\xbf", "x\xf0\x90\x80\x80",
    "x\xf0\x9f\x98\x80", "x\xf4\x8f\xbf\xbf"};

static void check_text_config(ctest_t *t, ak_t *a, app_mcp_result_t r, const char *text) {
    (void)snprintf(g_exp, sizeof g_exp,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"%s\"}],\"isError\":true}}",
                   text);
    ak_check_reply(t, a, r, g_exp);
}

static void t_utf8_second_byte_edges_are_rejected(ctest_t *t) {
    for (size_t i = 0; i < sizeof k_bad / sizeof k_bad[0]; i++) {
        ak_t *a = ak_begin();
        script_500(a, k_bad[i].body);
        check_text(t, a, call_fast(a), k_bad[i].want);
    }
}

static void t_utf8_valid_sequences_are_kept(ctest_t *t) {
    for (size_t i = 0; i < sizeof k_good / sizeof k_good[0]; i++) {
        ak_t *a = ak_begin();
        script_500(a, k_good[i]);
        check_text(t, a, call_fast(a), k_good[i]);
    }
}


/* annas shows control bytes of an environment value as ?; none reaches the JSON escaping of the tool text. */
static void t_control_bytes_in_an_environment_echo_are_question_marks(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_BASE_URL", "b\x01\x0f\x10\x1b\x1f\x7f" "d");
    check_text_config(t, a, call_fast(a),
                      "annas-c: E_CONFIG: ANNAS_BASE_URL is not a valid domain, got: b??????d");
    ak_check_no_io(t, a);
}

/* ---- memory left after the request DOM ----------------------------------------------------------------------------- */

#define PAD ((size_t)66000)
static unsigned char g_mem[200000];
static unsigned char g_meas[200000];
static char g_req[70000];
static app_mcp_t g_srv;

/* The request: a tools/call (or ping) with the given id text (JSON) and PAD bytes of padding behind it. */
static size_t build_request(const char *id_json, const char *method_and_params) {
    int n = snprintf(g_req, sizeof g_req, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"method\":%s,\"pad\":\"", id_json,
                     method_and_params);
    memset(g_req + n, 'p', PAD);
    memcpy(g_req + n + PAD, "\"}", 3);
    return (size_t)n + PAD + 2u;
}

/* app.h: the memory left after the DOM is capacity - core_arena_used after the parse. */
static size_t dom_used(size_t len) {
    core_arena_t ar;
    json_limits_t lim = json_limits_default();
    const json_value_t *root = NULL;
    lim.max_input = APP_MCP_LINE_MAX;
    (void)core_arena_init(&ar, g_meas, sizeof g_meas);
    if (json_parse(&ar, (core_str_t){g_req, len}, &lim, &root, NULL) != JSON_OK) {
        return 0;
    }
    return core_arena_used(&ar);
}

/* One server whose parse arena leaves exactly `left` bytes after the DOM of the request in g_req. */
static app_mcp_result_t handle_leaving(ctest_t *t, ak_t *a, size_t len, size_t left) {
    size_t used = dom_used(len);
    ak_clear(a);
    CHECK(t, used > 0);
    CHECK(t, used + left >= APP_MCP_ARENA_MIN);
    CHECK(t, used + left <= sizeof g_mem);
    CHECK(t, app_mcp_init(&g_srv, ak_deps(a), g_mem, used + left));
    CHECK_EQ_U64(t, core_arena_capacity(&g_srv.arena) - used, left);
    return app_mcp_handle(&g_srv, (core_str_t){g_req, len}, &a->out);
}

#define CALL_FAST "{\"name\":\"fast_download_url\",\"arguments\":{" HASH "}}"

/* A call with the given id: with exactly APP_MCP_CALL_MEM + 6 x id_len bytes left it runs, with one byte less no
 * tool runs and no port is touched. */
static void id_threshold(ctest_t *t, const char *id_json, const char *id_shown, size_t id_len) {
    size_t need = APP_MCP_CALL_MEM + 6u * id_len;
    size_t len = build_request(id_json, "\"tools/call\",\"params\":" CALL_FAST);
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    CHECK_EQ_I64(t, handle_leaving(t, a, len, need), APP_MCP_REPLY);
    (void)snprintf(g_exp, sizeof g_exp, AK_OKR("%s", AK_TOOL_OK("Download URL: " URL)), id_shown);
    CHECK_EQ_STR(t, ak_out(a), g_exp);
    ak_check_stream(t, a);
    a = ak_begin();
    CHECK_EQ_I64(t, handle_leaving(t, a, len, need - 1u), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), NOMEM_NULL);
    ak_check_no_io(t, a);
    a = ak_begin(); /* far below, and far above: the limit is not a coincidence of the pad */
    CHECK_EQ_I64(t, handle_leaving(t, a, len, need - 100u), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), NOMEM_NULL);
    ak_check_no_io(t, a);
}

static void t_call_memory_for_a_one_byte_string_id(ctest_t *t) { id_threshold(t, "\"x\"", "\"x\"", 1); }

static void t_call_memory_for_a_ten_byte_string_id(ctest_t *t) {
    id_threshold(t, "\"abcdefghij\"", "\"abcdefghij\"", 10);
}

static void t_call_memory_counts_the_decoded_string_id(ctest_t *t) {
    id_threshold(t, "\"\\u0041\\u0042\\u0043\"", "\"ABC\"", 3); /* 18 bytes of text, 3 decoded */
}

static void t_call_memory_for_a_one_digit_number_id(ctest_t *t) { id_threshold(t, "7", "7", 1); }

static void t_call_memory_for_a_nine_digit_number_id(ctest_t *t) { id_threshold(t, "123456789", "123456789", 9); }

static void t_call_memory_counts_the_text_of_a_number_id(ctest_t *t) { id_threshold(t, "-12.50e1", "-12.50e1", 8); }

static void t_call_memory_for_a_null_id(ctest_t *t) { id_threshold(t, "null", "null", 4); }

static void t_call_memory_for_an_empty_string_id(ctest_t *t) { id_threshold(t, "\"\"", "\"\"", 0); }

/* ---- the reply buffer: exactly what the DOM left (reply length + 1 for the NUL) ------------------------------------- */

#define PING "\"ping\""
#define LIST "\"tools/list\""
#define PING_REPLY AK_OKR("1", "{}")

static void expect_overflow(ctest_t *t, ak_t *a, app_mcp_result_t r) {
    CHECK_EQ_I64(t, r, APP_MCP_E_OVERFLOW);
    CHECK_EQ_U64(t, core_buf_len(&a->out), 0);
    CHECK(t, core_buf_overflow(&a->out));
}

static void t_reply_buffer_of_exactly_the_reply_size(ctest_t *t) {
    size_t len = build_request("1", PING);
    size_t need = strlen(PING_REPLY) + 1u;
    ak_t *a = ak_begin();
    ak_check_reply(t, a, handle_leaving(t, a, len, need), PING_REPLY);
    a = ak_begin();
    ak_check_reply(t, a, handle_leaving(t, a, len, need + 1u), PING_REPLY);
    a = ak_begin(); /* one byte short: the reply is replaced by the error, which does not fit either */
    expect_overflow(t, a, handle_leaving(t, a, len, need - 1u));
    a = ak_begin();
    expect_overflow(t, a, handle_leaving(t, a, len, 1));
    a = ak_begin();
    expect_overflow(t, a, handle_leaving(t, a, len, 0));
    ak_check_no_io(t, a);
}

static void t_error_reply_that_fits_exactly(ctest_t *t) {
    size_t len = build_request("1", LIST);
    size_t nomem = strlen(NOMEM_NULL) + 1u;
    ak_t *a = ak_begin(); /* the tool list does not fit; the -32603 reply does, with its NUL, to the byte */
    ak_check_reply(t, a, handle_leaving(t, a, len, nomem), NOMEM_NULL);
    a = ak_begin();
    expect_overflow(t, a, handle_leaving(t, a, len, nomem - 1u));
    a = ak_begin();
    ak_check_reply(t, a, handle_leaving(t, a, len, strlen(AK_OKR("1", AK_TOOLS_LIST)) + 1u), AK_OKR("1", AK_TOOLS_LIST));
    a = ak_begin();
    ak_check_reply(t, a, handle_leaving(t, a, len, strlen(AK_OKR("1", AK_TOOLS_LIST))), NOMEM_NULL);
    ak_check_no_io(t, a);
}


/* ---- keys at the two ends of a text, and in the file name echo ------------------------------------------------ */

/* The reply of a ping is {"jsonrpc":"2.0","id":1,"result":{}}: the key "{}}" is its very last three bytes. */
static void t_key_that_ends_the_text(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "{}}");
    CHECK_EQ_I64(t, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}"), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":REDACTED");
    CHECK_EQ_I64(t, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}"), APP_MCP_REPLY); /* a second time */
    CHECK_EQ_STR(t, ak_out(a), "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":REDACTED");
    ak_check_no_io(t, a);
}

static void t_key_that_ends_the_text_but_is_longer_than_what_is_left(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "}}}");
    ak_check_reply(t, a, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}"), AK_OKR("1", "{}"));
    ak_check_no_io(t, a);
}

/* The key is the first bytes of the text, and nowhere else. */
static void t_key_that_starts_the_text(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "{\"jsonrpc\"");
    CHECK_EQ_I64(t, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}"), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), "REDACTED:\"2.0\",\"id\":1,\"result\":{}}");
    ak_check_no_io(t, a);
    a = ak_begin(); /* a one-byte key at the very start */
    ak_env(a, "ANNAS_SECRET_KEY", "{");
    CHECK_EQ_I64(t, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}"), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), "REDACTED\"jsonrpc\":\"2.0\",\"id\":1,\"result\":REDACTED}}");
    ak_check_no_io(t, a);
}

/* A key of one byte (the output grows 8-fold) in every place of a CLI text. */
static void t_one_byte_key_in_a_cli_text(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "q");
    kit_set_key(a->k, "q");
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 200, "OK", "", "x"));
    int rc = AK_CLI(a, "download", MD5, "--name", "qqq", "--format", "pdf");
    CHECK_EQ_I64(t, rc, 0);
    CHECK_EQ_STR(t, ak_out(a), "Book downloaded successfully to: /dl/REDACTEDREDACTEDREDACTED.pdf\n");
    CHECK_EQ_STR(t, ak_err(a), "");
    ak_check_stream(t, a);
}

/* The percent-encoded key is shorter than "REDACTED": the second pass reads the text it replaces into, so the text
 * behind the first replacement must not be damaged by the later ones. */
static void t_encoded_key_shorter_than_its_replacement_in_a_cli_text(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "+");
    kit_set_key(a->k, "+");
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 200, "OK", "", "x"));
    int rc = AK_CLI(a, "download", MD5, "--name", "%2B%2B%2Bend", "--format", "pdf");
    CHECK_EQ_I64(t, rc, 0);
    CHECK_EQ_STR(t, ak_out(a), "Book downloaded successfully to: /dl/REDACTEDREDACTEDREDACTEDend.pdf\n");
    CHECK_EQ_STR(t, ak_err(a), "");
    ak_check_stream(t, a);
}

/* Only the first byte of the key needs encoding. */
static void t_only_the_first_key_byte_is_reserved(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "+abc");
    ping_id(t, a, "%2Babc", "REDACTED");
    ping_id(t, a, "+abc", "REDACTED");
    ping_id(t, a, "%2Babd", "%2Babd");
    ak_check_no_io(t, a);
}

/* ---- numbers on the command line ----------------------------------------------------------------------------------- */

static app_cmd_t g_cmd;
static app_usage_error_t g_uerr;

static void t_timeout_next_to_the_int64_minimum(ctest_t *t) {
    static const struct {
        const char *text;
        int64_t want;
    } k[] = {{"-9223372036854775807", -INT64_MAX}, {"-9223372036854775806", -INT64_MAX + 1},
             {"-9223372036854775808", INT64_MIN},  {"9223372036854775807", INT64_MAX},
             {"-1", -1},                           {"-0", 0}};
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        const char *argv[] = {"annas-c", "download", MD5, "--timeout", k[i].text};
        CHECK(t, app_cli_parse(5, argv, &g_cmd, &g_uerr));
        CHECK_EQ_I64(t, g_cmd.timeout_seconds, k[i].want);
    }
    static const char *const bad[] = {"-9223372036854775809", "9223372036854775808", "-", "--1", "", "-+1", "1-"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        const char *argv[] = {"annas-c", "download", MD5, "--timeout", bad[i]};
        CHECK(t, !app_cli_parse(5, argv, &g_cmd, &g_uerr));
    }
}

/* ---- the .env file --------------------------------------------------------------------------------------------- */

static app_dotenv_t g_denv;

static size_t env_count(ctest_t *t, const char *text) {
    CHECK(t, app_dotenv_parse(core_str_from_cstr(text), &g_denv));
    return g_denv.count;
}

static void t_dotenv_name_alphabet_edges(ctest_t *t) {
    static const char good[] = "A=1\nZ=2\na=3\nz=4\n0=5\n9=6\n_=7\nAz09_=8\n";
    CHECK_EQ_U64(t, env_count(t, good), 8);
    CHECK(t, ak_eq(g_denv.entries[0].name, "A") && ak_eq(g_denv.entries[0].value, "1"));
    CHECK(t, ak_eq(g_denv.entries[1].name, "Z") && ak_eq(g_denv.entries[1].value, "2"));
    CHECK(t, ak_eq(g_denv.entries[2].name, "a") && ak_eq(g_denv.entries[2].value, "3"));
    CHECK(t, ak_eq(g_denv.entries[3].name, "z") && ak_eq(g_denv.entries[3].value, "4"));
    CHECK(t, ak_eq(g_denv.entries[4].name, "0") && ak_eq(g_denv.entries[4].value, "5"));
    CHECK(t, ak_eq(g_denv.entries[5].name, "9") && ak_eq(g_denv.entries[5].value, "6"));
    CHECK(t, ak_eq(g_denv.entries[6].name, "_") && ak_eq(g_denv.entries[6].value, "7"));
    CHECK(t, ak_eq(g_denv.entries[7].name, "Az09_") && ak_eq(g_denv.entries[7].value, "8"));
    /* the bytes just outside each class, alone and at either end of a good name */
    static const char *const bad[] = {"@=1", "[=1", "`=1", "{=1", "/=1", ":=1", "-=1", ".=1", "~=1", "^=1",
                                      "-A=1", "A-=1", "A@=1", "[A=1", "a{=1", "`a=1", "0:=1", "/0=1", "A\x80=1", "\x80=1",
                                      "\x80", "A B=1", "export A=1"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK_EQ_U64(t, env_count(t, bad[i]), 0);
    }
}

static bool always_found(void *ctx, core_str_t name, core_str_t *value) {
    (void)ctx;
    (void)name;
    *value = core_str_from_cstr("p");
    return true;
}

/* app.h: "an empty name is never found", whatever the process environment answers to it. */
static void t_layer_never_finds_the_empty_name(ctest_t *t) {
    static const ports_env_ops_t ops = {always_found};
    app_env_layer_t l;
    core_str_t v = {(const char *)1, 7};
    ports_env_t proc = {&ops, NULL};
    CHECK(t, app_env_layer_init(&l, NULL, proc));
    ports_env_t e = app_env_layer_port(&l);
    CHECK(t, !e.ops->get(e.ctx, core_str_from_cstr(""), &v));
    CHECK(t, v.ptr == NULL && v.len == 0);
    CHECK(t, e.ops->get(e.ctx, core_str_from_cstr("X"), &v)); /* control: any other name is asked */
    CHECK(t, ak_eq(v, "p"));
}

/* ---- arguments of app_mcp_handle ------------------------------------------------------------------------------- */

static void t_handle_refuses_a_null_line_of_one_byte(ctest_t *t) {
    ak_t *a = ak_begin();
    (void)ak_mcp_n(a, "x", 0); /* creates the server */
    CHECK_EQ_I64(t, app_mcp_handle(&a->mcp, (core_str_t){NULL, 1}, &a->out), APP_MCP_E_ARG);
    CHECK_EQ_I64(t, app_mcp_handle(&a->mcp, (core_str_t){NULL, 2}, &a->out), APP_MCP_E_ARG);
    CHECK_EQ_U64(t, core_buf_len(&a->out), 0);
    CHECK_EQ_I64(t, app_mcp_handle(&a->mcp, (core_str_t){NULL, 0}, &a->out), APP_MCP_NO_REPLY); /* control */
    ak_check_no_io(t, a);
}

CTEST_SUITE("survivor",
    {"every_byte_value_in_the_encoded_key", t_every_byte_value_in_the_encoded_key},
    {"encoded_form_of_each_edge_byte", t_encoded_form_of_each_edge_byte},
    {"encoded_key_near_misses", t_encoded_key_near_misses},
    {"key_that_encodes_to_itself_gets_one_pass", t_key_that_encodes_to_itself_gets_one_pass},
    {"pass_one_output_at_the_limit_ending_in_a_redaction", t_pass_one_output_at_the_limit_ending_in_a_redaction},
    {"pass_one_output_at_the_limit_ending_in_plain_bytes", t_pass_one_output_at_the_limit_ending_in_plain_bytes},
    {"utf8_second_byte_edges_are_rejected", t_utf8_second_byte_edges_are_rejected},
    {"utf8_valid_sequences_are_kept", t_utf8_valid_sequences_are_kept},
    {"call_memory_for_a_one_byte_string_id", t_call_memory_for_a_one_byte_string_id},
    {"call_memory_for_a_ten_byte_string_id", t_call_memory_for_a_ten_byte_string_id},
    {"call_memory_counts_the_decoded_string_id", t_call_memory_counts_the_decoded_string_id},
    {"call_memory_for_a_one_digit_number_id", t_call_memory_for_a_one_digit_number_id},
    {"call_memory_for_a_nine_digit_number_id", t_call_memory_for_a_nine_digit_number_id},
    {"call_memory_counts_the_text_of_a_number_id", t_call_memory_counts_the_text_of_a_number_id},
    {"call_memory_for_a_null_id", t_call_memory_for_a_null_id},
    {"call_memory_for_an_empty_string_id", t_call_memory_for_an_empty_string_id},
    {"reply_buffer_of_exactly_the_reply_size", t_reply_buffer_of_exactly_the_reply_size},
    {"error_reply_that_fits_exactly", t_error_reply_that_fits_exactly},
    {"control_bytes_in_an_environment_echo_are_question_marks", t_control_bytes_in_an_environment_echo_are_question_marks},
    {"key_that_ends_the_text", t_key_that_ends_the_text},
    {"key_that_ends_the_text_but_is_longer_than_what_is_left", t_key_that_ends_the_text_but_is_longer_than_what_is_left},
    {"key_that_starts_the_text", t_key_that_starts_the_text},
    {"one_byte_key_in_a_cli_text", t_one_byte_key_in_a_cli_text},
    {"encoded_key_shorter_than_its_replacement_in_a_cli_text", t_encoded_key_shorter_than_its_replacement_in_a_cli_text},
    {"only_the_first_key_byte_is_reserved", t_only_the_first_key_byte_is_reserved},
    {"timeout_next_to_the_int64_minimum", t_timeout_next_to_the_int64_minimum},
    {"dotenv_name_alphabet_edges", t_dotenv_name_alphabet_edges},
    {"layer_never_finds_the_empty_name", t_layer_never_finds_the_empty_name},
    {"handle_refuses_a_null_line_of_one_byte", t_handle_refuses_a_null_line_of_one_byte})
