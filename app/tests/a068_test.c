/* A068: inputs for the survivors of the A063 measurement that the public headers (app.h) let a test tell apart:
 *  - `.env` whitespace (space, \t, \v, \f) and a NULL file with a length;
 *  - a tools/call whose request leaves too little parse memory; a line that fills the parse memory exactly after an
 *    earlier line (the arena is rewound for every line);
 *  - the two scrubbing passes on replies of about 64 KiB: the reply length limit with and without a key, the key as
 *    its percent-encoded form repeated;
 *  - every control byte in a JSON string of a reply.
 * The key used is a fake. */
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"
#include "json/json.h"

#define MD5 AK_MD5
#define NOMEM_NULL AK_ERR("null", "-32603", "Internal error: request does not fit in memory")
#define PRE "{\"jsonrpc\":\"2.0\",\"id\":\""
#define POST "\",\"method\":\"ping\"}"
#define PING_LINE "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}"
#define FAKE_KEY "ab+cd/ef=gh"
#define FAKE_ENC "ab%2Bcd%2Fef%3Dgh"

/* ---- .env ----------------------------------------------------------------------------------------------------- */

static void t_dotenv_trims_vertical_tab_and_form_feed(ctest_t *t) {
    static const char file[] = "\vA=b\f\n\f\v C = d \v\f\nE=\"f\"\v\n";
    static app_dotenv_t d;
    CHECK(t, app_dotenv_parse((core_str_t){file, sizeof file - 1u}, &d));
    CHECK_EQ_U64(t, d.count, 3);
    CHECK(t, ak_eq(d.entries[0].name, "A"));
    CHECK(t, ak_eq(d.entries[0].value, "b"));
    CHECK(t, ak_eq(d.entries[1].name, "C"));
    CHECK(t, ak_eq(d.entries[1].value, "d"));
    CHECK(t, ak_eq(d.entries[2].name, "E"));
    CHECK(t, ak_eq(d.entries[2].value, "f"));
}

static void t_dotenv_vertical_tab_and_form_feed_each_alone(ctest_t *t) {
    static const char lead_v[] = "\vA=b";
    static const char lead_f[] = "\fA=b";
    static const char trail_v[] = "A=b\v";
    static const char trail_f[] = "A=b\f";
    static const char *const files[] = {lead_v, lead_f, trail_v, trail_f};
    for (size_t i = 0; i < 4u; i++) {
        static app_dotenv_t d;
        CHECK(t, app_dotenv_parse((core_str_t){files[i], strlen(files[i])}, &d));
        CHECK_EQ_U64(t, d.count, 1);
        CHECK(t, ak_eq(d.entries[0].name, "A"));
        CHECK(t, ak_eq(d.entries[0].value, "b"));
    }
}

static void t_dotenv_null_file_with_a_length_is_refused(ctest_t *t) {
    static app_dotenv_t d;
    d.count = 7;
    CHECK(t, !app_dotenv_parse((core_str_t){NULL, 1}, &d));
    CHECK_EQ_U64(t, d.count, 0);
    d.count = 7;
    CHECK(t, !app_dotenv_parse((core_str_t){NULL, 2}, &d));
    CHECK_EQ_U64(t, d.count, 0);
    CHECK(t, app_dotenv_parse((core_str_t){NULL, 0}, &d)); /* control: an empty file */
    CHECK_EQ_U64(t, d.count, 0);
}

/* ---- parse memory ------------------------------------------------------------------------------------------------ */

static unsigned char g_mem[AK_PARSE_CAP];
static unsigned char g_meas[AK_PARSE_CAP];
static char g_req[APP_MCP_ARENA_MIN + 256u];
static app_mcp_t g_srv;

/* Bytes of arena the DOM of g_req[0..len) uses. */
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

static size_t build_notification(size_t pad) {
    static const char pre[] = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"pad\":\"";
    memcpy(g_req, pre, sizeof pre - 1u);
    memset(g_req + sizeof pre - 1u, 'p', pad);
    memcpy(g_req + sizeof pre - 1u + pad, "\"}", 3);
    return sizeof pre - 1u + pad + 2u;
}

/* Largest pad for which the DOM of the request built by `build` uses at most `target` bytes of arena. */
typedef size_t (*build_fn)(size_t pad);

static size_t fit_pad(build_fn build, size_t target) {
    size_t pad = 1000;
    size_t u = dom_used(build(pad));
    if (u == 0 || u > target) {
        return 0;
    }
    pad += target - u; /* the DOM grows by about one byte per byte of padding: close; settle exactly */
    while (pad > 0 && (u = dom_used(build(pad))) > target) {
        pad--;
    }
    while ((u = dom_used(build(pad + 1u))) != 0 && u <= target) {
        pad++;
    }
    return pad;
}

static const char *g_call;

static size_t build_call(size_t pad) {
    int n = snprintf(g_req, sizeof g_req, "{\"jsonrpc\":\"2.0\",\"id\":\"q\",\"method\":\"tools/call\",\"params\":%s,\"pad\":\"",
                     g_call);
    memset(g_req + n, 'p', pad);
    memcpy(g_req + n + pad, "\"}", 3);
    return (size_t)n + pad + 2u;
}

/* A tools/call for either tool with less than APP_MCP_CALL_MEM bytes of arena left after its DOM: nothing runs. */
static void t_call_with_too_little_memory_runs_nothing(ctest_t *t) {
    static const char *const calls[] = {
        "{\"name\":\"book_download\",\"arguments\":{\"hash\":\"" MD5 "\"}}",
        "{\"name\":\"fast_download_url\",\"arguments\":{\"hash\":\"" MD5 "\"}}"};
    static const size_t lefts[] = {8000, 16300};
    for (size_t i = 0; i < 2u; i++) {
        for (size_t j = 0; j < 2u; j++) {
            ak_t *a = ak_begin();
            size_t cap = APP_MCP_ARENA_MIN;
            g_call = calls[i];
            size_t pad = fit_pad(build_call, cap - lefts[j]);
            CHECK(t, pad > 0);
            size_t len = build_call(pad);
            size_t used = dom_used(len);
            CHECK(t, cap - used >= lefts[j] && cap - used < lefts[j] + 16u);
            CHECK(t, app_mcp_init(&g_srv, ak_deps(a), g_mem, cap));
            ak_clear(a);
            CHECK_EQ_I64(t, app_mcp_handle(&g_srv, (core_str_t){g_req, len}, &a->out), APP_MCP_REPLY);
            CHECK_EQ_STR(t, ak_out(a), NOMEM_NULL);
            ak_check_no_io(t, a);
        }
    }
}

/* The second line's DOM fills the parse memory as far as the arena can: it parses only if the arena was rewound to 0. */
static void t_second_line_filling_the_parse_memory_is_parsed(ctest_t *t) {
    ak_t *a = ak_begin();
    size_t cap = APP_MCP_ARENA_MIN;
    size_t pad = fit_pad(build_notification, cap);
    CHECK(t, pad > 0);
    size_t len = build_notification(pad);
    size_t used = dom_used(len);
    CHECK(t, used > 0 && used <= cap);
    CHECK(t, cap - used < 16u); /* the line really fills the memory */
    CHECK(t, app_mcp_init(&g_srv, ak_deps(a), g_mem, cap));
    ak_clear(a);
    CHECK_EQ_I64(t, app_mcp_handle(&g_srv, (core_str_t){PING_LINE, sizeof PING_LINE - 1u}, &a->out),
                 APP_MCP_REPLY); /* an earlier line */
    ak_clear(a);
    CHECK_EQ_I64(t, app_mcp_handle(&g_srv, (core_str_t){g_req, len}, &a->out), APP_MCP_NO_REPLY);
    CHECK_EQ_U64(t, core_buf_len(&a->out), 0);
    CHECK(t, !core_buf_overflow(&a->out));
    CHECK_EQ_I64(t, app_mcp_handle(&g_srv, (core_str_t){g_req, len}, &a->out), APP_MCP_NO_REPLY); /* and again */
    ak_check_no_io(t, a);
}

/* ---- scrubbing of replies of about 64 KiB ---------------------------------------------------------------------------- */

#define BIGLINE ((size_t)200200)
static char g_line[BIGLINE];
static char g_idtext[BIGLINE];
static char g_want[BIGLINE];
static char g_out_store[BIGLINE];
static core_buf_t g_o;

/* The first n bytes of FAKE_ENC repeated. */
static void make_id(size_t n) {
    size_t w = strlen(FAKE_ENC);
    for (size_t i = 0; i < n; i++) {
        g_idtext[i] = FAKE_ENC[i % w];
    }
    g_idtext[n] = '\0';
}

/* The expected reply: every complete FAKE_ENC of the id (non-overlapping, left to right) is REDACTED. */
static size_t expect_ping(size_t n, bool scrub) {
    size_t w = strlen(FAKE_ENC);
    size_t at = 0;
    memcpy(g_want, PRE, sizeof PRE - 1u);
    at = sizeof PRE - 1u;
    for (size_t i = 0; i < n;) {
        if (scrub && n - i >= w && memcmp(g_idtext + i, FAKE_ENC, w) == 0) {
            memcpy(g_want + at, "REDACTED", 8);
            at += 8u;
            i += w;
        } else {
            g_want[at++] = g_idtext[i++];
        }
    }
    memcpy(g_want + at, "\",\"result\":{}}", 15);
    return at + 14u;
}

static app_mcp_result_t ping_n(ak_t *a, size_t n) {
    make_id(n);
    int k = snprintf(g_line, sizeof g_line, PRE "%s" POST, g_idtext);
    (void)ak_mcp_n(a, g_line, 0); /* creates the server over the 3 MiB parse memory */
    (void)core_buf_init(&g_o, g_out_store, sizeof g_out_store);
    return app_mcp_handle(&a->mcp, (core_str_t){g_line, (size_t)k}, &g_o);
}

static void check_delivered(ctest_t *t, ak_t *a, size_t n, bool scrub, size_t want_len) {
    CHECK_EQ_I64(t, ping_n(a, n), APP_MCP_REPLY);
    size_t len = expect_ping(n, scrub);
    CHECK_EQ_U64(t, len, want_len);
    CHECK_EQ_U64(t, core_buf_len(&g_o), want_len);
    CHECK(t, memcmp(core_buf_str(&g_o).ptr, g_want, len) == 0);
    ak_check_no_io(t, a);
}

static void check_nomem(ctest_t *t, ak_t *a, size_t n) {
    CHECK_EQ_I64(t, ping_n(a, n), APP_MCP_REPLY);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), NOMEM_NULL);
    ak_check_no_io(t, a);
}

static void t_without_a_key_a_reply_of_exactly_the_maximum_is_delivered(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    check_delivered(t, a, 65499, false, APP_MCP_REPLY_MAX);
    a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    check_nomem(t, a, 65500);
    a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    check_delivered(t, a, 1000, false, 1037);
}

static void t_encoded_key_repeated_in_a_reply_shrinks_it_below_the_maximum(ctest_t *t) {
    static const size_t sizes[] = {65499, 65500, 100000};
    static const size_t want[] = {30868, 30869, 47099};
    for (size_t i = 0; i < 3u; i++) {
        ak_t *a = ak_begin();
        ak_env(a, "ANNAS_SECRET_KEY", FAKE_KEY);
        check_delivered(t, a, sizes[i], true, want[i]);
    }
}

static void t_encoded_key_repeated_in_a_reply_that_stays_too_long(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", FAKE_KEY);
    check_nomem(t, a, 200000);
    a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", FAKE_KEY);
    check_delivered(t, a, 17u * 3u, true, 37u + 24u);
}

/* Passes whose output would be far longer than the finalised-text limit: the reply is the -32603 error, whatever
 * lies behind the end of the pass buffer. */
static void huge_expansion(ctest_t *t, const char *key, const char *unit, size_t count, const char *want) {
    ak_t *a = ak_begin();
    size_t w = strlen(unit);
    ak_env(a, "ANNAS_SECRET_KEY", key);
    for (size_t i = 0; i < count; i++) {
        memcpy(g_idtext + i * w, unit, w);
    }
    g_idtext[count * w] = '\0';
    int k = snprintf(g_line, sizeof g_line, PRE "%s" POST, g_idtext);
    (void)ak_mcp_n(a, g_line, 0);
    (void)core_buf_init(&g_o, g_out_store, sizeof g_out_store);
    CHECK_EQ_I64(t, app_mcp_handle(&a->mcp, (core_str_t){g_line, (size_t)k}, &g_o), APP_MCP_REPLY);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), want);
    ak_check_no_io(t, a);
}

static void t_first_pass_expanding_far_past_the_limit(ctest_t *t) { huge_expansion(t, "z", "z", 190000, NOMEM_NULL); }

static void t_second_pass_expanding_far_past_the_limit(ctest_t *t) { /* the error reply is scrubbed as well: both of its closing braces are the key */
    huge_expansion(t, "}", "%7D", 21800,
                   "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":"
                   "\"Internal error: request does not fit in memory\"REDACTEDREDACTED"); }

/* ---- control bytes in a reply text ------------------------------------------------------------------------------------ */

static void t_every_control_byte_in_a_text_is_escaped(ctest_t *t) {
    static const char hex[] = "0123456789abcdef";
    static char json_url[160];
    static char want[400];
    static char req[256];
    (void)snprintf(req, sizeof req,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":"
                   "\"fast_download_url\",\"arguments\":{\"hash\":\"%s\"}}}",
                   MD5);
    for (int c = 1; c < 0x20; c++) {
        ak_t *a = ak_begin();
        /* the API gives the URL with the control byte as a JSON escape; annas hands the byte itself on */
        (void)snprintf(json_url, sizeof json_url, "https://dl.example.org/f/\\u00%c%c!", hex[c >> 4], hex[c & 15]);
        kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", "Content-Type: application/json\r\n", kit_ok_json(a->k, json_url, NULL)));
        (void)snprintf(want, sizeof want,
                       "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":"
                       "\"Download URL: https://dl.example.org/f/\\u00%c%c!\"}],\"isError\":false}}",
                       hex[c >> 4], hex[c & 15]);
        ak_check_reply(t, a, ak_mcp(a, req), want);
        ak_check_stream(t, a);
    }
}

CTEST_SUITE("app_a068",
    {"dotenv_trims_vertical_tab_and_form_feed", t_dotenv_trims_vertical_tab_and_form_feed},
    {"dotenv_vertical_tab_and_form_feed_each_alone", t_dotenv_vertical_tab_and_form_feed_each_alone},
    {"dotenv_null_file_with_a_length_is_refused", t_dotenv_null_file_with_a_length_is_refused},
    {"call_with_too_little_memory_runs_nothing", t_call_with_too_little_memory_runs_nothing},
    {"second_line_filling_the_parse_memory_is_parsed", t_second_line_filling_the_parse_memory_is_parsed},
    {"without_a_key_a_reply_of_exactly_the_maximum_is_delivered", t_without_a_key_a_reply_of_exactly_the_maximum_is_delivered},
    {"encoded_key_repeated_shrinks_below_the_maximum", t_encoded_key_repeated_in_a_reply_shrinks_it_below_the_maximum},
    {"encoded_key_repeated_stays_too_long", t_encoded_key_repeated_in_a_reply_that_stays_too_long},
    {"first_pass_expanding_far_past_the_limit", t_first_pass_expanding_far_past_the_limit},
    {"second_pass_expanding_far_past_the_limit", t_second_pass_expanding_far_past_the_limit},
    {"every_control_byte_in_a_text_is_escaped", t_every_control_byte_in_a_text_is_escaped})
