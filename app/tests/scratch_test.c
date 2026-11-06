/* The explicit scratch workspace (deps->scratch) and the output contract of app.h "Memory and output":
 *  - the app uses no memory but deps->scratch, the MCP parse arena and what core/buf.h functions write, so an `out`
 *    / `errout` of exactly content length + 1 bytes is enough, and the unused capacity behind the content is never
 *    touched (every buffer here is placed at the END of a static array, so a write past its capacity also trips the
 *    sanitiser, and its spare bytes are filled with a sentinel that must survive);
 *  - a scratch region that is NULL, too short or misaligned is refused (CLI: internal error; app_mcp_init: false);
 *  - the largest texts the operations can produce fit in exactly APP_SCRATCH_MIN;
 *  - an output that does not fit is all-or-nothing;
 *  - the reply of the MCP server does not depend on how much of the parse arena a request left;
 *  - a missing env port is an internal error. */
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"
#include "json/json.h"

#define MD5 AK_MD5
#define URL "https://dl.example.org/f/1"
#define JSON_HDR "Content-Type: application/json\r\n"
#define KEY KIT_KEY
#define INTERNAL "annas-c: internal error: invalid dependencies"
#define MISSING "ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set"
#define DONE_TEXT "Book downloaded successfully to: /dl/" MD5 ".bin"
#define SENT ((char)0xA5)
#define BIG ((size_t)65536)
#define HASH "\"hash\":\"" MD5 "\""

static char g_big_a[BIG];
static char g_big_b[BIG];
static char g_req[4096];
static char g_args[4096];
static char g_exp[40000];
static char g_pad_line[70000];
static core_buf_t g_o, g_e; /* test frames are limited to 512 bytes: the big objects live here */
static app_deps_t g_d;

/* A buffer of exactly `cap` bytes at the end of `big`, spare bytes set to the sentinel. */
static char *tail_buf(core_buf_t *b, char *big, size_t cap) {
    char *store = big + BIG - cap;
    memset(store, SENT, cap);
    (void)core_buf_init(b, store, cap);
    return store;
}

/* Bytes behind the content and its NUL are untouched. */
static void check_spare(ctest_t *t, const core_buf_t *b, const char *store, size_t cap) {
    size_t from = core_buf_len(b) + 1u;
    bool ok = true;
    for (size_t i = from; i < cap; i++) {
        ok = ok && store[i] == SENT;
    }
    CHECK(t, ok);
}

static void check_content(ctest_t *t, const core_buf_t *b, const char *want) {
    core_str_t s = core_buf_str(b);
    CHECK_EQ_U64(t, s.len, strlen(want));
    CHECK(t, s.len == 0 || memcmp(s.ptr, want, s.len) == 0);
}

static int run_cli(const app_deps_t *d, core_buf_t *o, core_buf_t *e, int argc, const char *const *argv) {
    static app_cmd_t cmd;
    static app_usage_error_t ue;
    if (!app_cli_parse(argc, argv, &cmd, &ue)) {
        return -1000;
    }
    return app_cli_run(&cmd, d, o, e);
}
#define RUN_CLI(d, o, e, ...) \
    run_cli((d), (o), (e), AK_ARGC("annas-c", __VA_ARGS__), (const char *const[]){"annas-c", __VA_ARGS__})

static void script_download(ak_t *a, const char *file_body) {
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 200, "OK", "", file_body));
}

static app_mcp_result_t mcp_into(ak_t *a, const char *line, core_buf_t *o) {
    const app_deps_t *d = ak_deps(a);
    if (!a->mcp_ready) {
        a->mcp_ready = app_mcp_init(&a->mcp, d, a->parse_mem, sizeof a->parse_mem);
    }
    return app_mcp_handle(&a->mcp, core_str_from_cstr(line), o);
}

static app_mcp_result_t call(ak_t *a, const char *tool, const char *args, core_buf_t *o) {
    (void)snprintf(g_req, sizeof g_req,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}",
                   tool, args);
    return mcp_into(a, g_req, o);
}

/* ---- exact-size outputs ------------------------------------------------------------------------------------ */

static void t_cli_success_with_exact_buffers(ctest_t *t) {
    ak_t *a = ak_begin();
    script_download(a, "x");
    char *so = tail_buf(&g_o, g_big_a, strlen(DONE_TEXT "\n") + 1u);
    char *se = tail_buf(&g_e, g_big_b, 1);
    int rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download", MD5);
    CHECK_EQ_I64(t, rc, 0);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), DONE_TEXT "\n");
    CHECK_EQ_STR(t, core_buf_cstr(&g_e), "");
    CHECK(t, !core_buf_overflow(&g_o) && !core_buf_overflow(&g_e));
    (void)so;
    (void)se;
    ak_check_stream(t, a);
    kit_check_file(t, a->k, MD5 ".bin", "x", 1);
}

static void t_cli_success_leaves_spare_capacity_untouched(ctest_t *t) {
    ak_t *a = ak_begin();
    script_download(a, "x");
    char *so = tail_buf(&g_o, g_big_a, 8192);
    char *se = tail_buf(&g_e, g_big_b, 8192);
    int rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download", MD5);
    CHECK_EQ_I64(t, rc, 0);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), DONE_TEXT "\n");
    CHECK_EQ_STR(t, core_buf_cstr(&g_e), "");
    check_spare(t, &g_o, so, 8192);
    check_spare(t, &g_e, se, 8192);
    ak_check_stream(t, a);
}

static void t_cli_failure_with_exact_buffers(ctest_t *t) {
    static const char want[] = "annas-c: E_API: 500: oops\n";
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 500, "Error", "", "oops"));
    char *so = tail_buf(&g_o, g_big_a, 1);
    char *se = tail_buf(&g_e, g_big_b, sizeof want);
    int rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download-url", MD5);
    CHECK_EQ_I64(t, rc, 4);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), "");
    CHECK_EQ_STR(t, core_buf_cstr(&g_e), want);
    CHECK(t, !core_buf_overflow(&g_o) && !core_buf_overflow(&g_e));
    (void)so;
    (void)se;
    ak_check_stream(t, a);
    /* the same failure with large buffers: identical bytes, spare capacity untouched */
    a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 500, "Error", "", "oops"));
    so = tail_buf(&g_o, g_big_a, 4096);
    se = tail_buf(&g_e, g_big_b, 4096);
    rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download-url", MD5);
    CHECK_EQ_I64(t, rc, 4);
    CHECK_EQ_STR(t, core_buf_cstr(&g_e), want);
    check_spare(t, &g_o, so, 4096);
    check_spare(t, &g_e, se, 4096);
}

static void t_cli_config_error_with_exact_buffers(ctest_t *t) {
    static const char want[] = "annas-c: E_CONFIG: " MISSING "\n";
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    (void)tail_buf(&g_o, g_big_a, 1);
    (void)tail_buf(&g_e, g_big_b, sizeof want);
    int rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download", MD5);
    CHECK_EQ_I64(t, rc, 2);
    CHECK_EQ_STR(t, core_buf_cstr(&g_e), want);
    CHECK(t, !core_buf_overflow(&g_o) && !core_buf_overflow(&g_e));
    ak_check_no_io(t, a);
}

static void t_cli_internal_error_and_help_with_exact_buffers(ctest_t *t) {
    static const char want[] = INTERNAL "\n";
    ak_t *a = ak_begin();
    g_d = *ak_deps(a);
    g_d.scratch = NULL;
    (void)tail_buf(&g_o, g_big_a, 1);
    (void)tail_buf(&g_e, g_big_b, sizeof want);
    int rc = RUN_CLI(&g_d, &g_o, &g_e, "download", MD5);
    CHECK_EQ_I64(t, rc, 1);
    CHECK_EQ_STR(t, core_buf_cstr(&g_e), want);
    CHECK(t, !core_buf_overflow(&g_o) && !core_buf_overflow(&g_e));
    ak_check_no_io(t, a);
    (void)tail_buf(&g_o, g_big_a, sizeof "annas-c 0.1\n");
    (void)tail_buf(&g_e, g_big_b, 1);
    rc = RUN_CLI(NULL, &g_o, &g_e, "--version");
    CHECK_EQ_I64(t, rc, 0);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), "annas-c 0.1\n");
    CHECK(t, !core_buf_overflow(&g_o) && !core_buf_overflow(&g_e));
}

/* The fit is judged on the SCRUBBED text: a short key makes it longer, a long key shorter. */
static void t_cli_fit_is_judged_after_scrubbing(ctest_t *t) {
    static const char longer[] = "Book downloaded successfully to: /dl/REDACTED.pdf\n";
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "abc");
    kit_set_key(a->k, "abc");
    script_download(a, "x");
    (void)tail_buf(&g_o, g_big_a, sizeof longer);
    (void)tail_buf(&g_e, g_big_b, 1);
    int rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download", MD5, "--name", "abc", "--format", "pdf");
    CHECK_EQ_I64(t, rc, 0);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), longer);
    CHECK(t, !core_buf_overflow(&g_o));
    ak_check_stream(t, a);
    a = ak_begin(); /* the 33-byte default key becomes 8 bytes: exact fit of the shorter text */
    script_download(a, "x");
    (void)tail_buf(&g_o, g_big_a, sizeof longer);
    (void)tail_buf(&g_e, g_big_b, 1);
    rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download", MD5, "--name", KEY, "--format", "pdf");
    CHECK_EQ_I64(t, rc, 0);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), longer);
    CHECK(t, !core_buf_overflow(&g_o));
    ak_check_stream(t, a);
}

static void t_cli_exact_buffers_are_not_cleared(ctest_t *t) {
    ak_t *a = ak_begin();
    script_download(a, "x");
    size_t cap = 2u + strlen(DONE_TEXT "\n") + 1u;
    (void)tail_buf(&g_o, g_big_a, cap);
    (void)tail_buf(&g_e, g_big_b, 4);
    core_buf_put_str(&g_o, core_str_from_cstr("ab"));
    core_buf_put_str(&g_e, core_str_from_cstr("cd"));
    int rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download", MD5);
    CHECK_EQ_I64(t, rc, 0);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), "ab" DONE_TEXT "\n");
    CHECK_EQ_STR(t, core_buf_cstr(&g_e), "cd");
    CHECK(t, !core_buf_overflow(&g_o) && !core_buf_overflow(&g_e));
    ak_check_stream(t, a);
}

/* ---- overflow: all or nothing ------------------------------------------------------------------------------- */

static void t_cli_overflow_of_stdout_is_all_or_nothing(ctest_t *t) {
    ak_t *a = ak_begin();
    size_t need = strlen(DONE_TEXT "\n") + 1u;
    script_download(a, "x");
    char *so = tail_buf(&g_o, g_big_a, 2u + need - 1u); /* "ab" + text does not fit by exactly one byte */
    (void)tail_buf(&g_e, g_big_b, 64);
    core_buf_put_str(&g_o, core_str_from_cstr("ab"));
    int rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download", MD5);
    CHECK_EQ_I64(t, rc, 0); /* the operation succeeded; only its text is lost */
    check_content(t, &g_o, "ab");
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), "ab");
    CHECK(t, core_buf_overflow(&g_o));
    check_content(t, &g_e, "");
    CHECK(t, !core_buf_overflow(&g_e));
    check_spare(t, &g_o, so, 2u + need - 1u);
    ak_check_stream(t, a);
    kit_check_file(t, a->k, MD5 ".bin", "x", 1);
    /* control: one byte more and the text is appended whole */
    a = ak_begin();
    script_download(a, "x");
    (void)tail_buf(&g_o, g_big_a, 2u + need);
    (void)tail_buf(&g_e, g_big_b, 64);
    core_buf_put_str(&g_o, core_str_from_cstr("ab"));
    rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download", MD5);
    CHECK_EQ_I64(t, rc, 0);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), "ab" DONE_TEXT "\n");
    CHECK(t, !core_buf_overflow(&g_o));
}

static void t_cli_overflow_of_stderr_is_all_or_nothing(ctest_t *t) {
    static const char line[] = "annas-c: E_API: 500: oops\n";
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 500, "Error", "", "oops"));
    (void)tail_buf(&g_o, g_big_a, 64);
    char *se = tail_buf(&g_e, g_big_b, 2u + sizeof line - 1u);
    core_buf_put_str(&g_e, core_str_from_cstr("xy"));
    int rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download-url", MD5);
    CHECK_EQ_I64(t, rc, 4); /* the exit code is the operation's */
    CHECK_EQ_STR(t, core_buf_cstr(&g_e), "xy");
    CHECK(t, core_buf_overflow(&g_e));
    check_content(t, &g_o, "");
    CHECK(t, !core_buf_overflow(&g_o));
    check_spare(t, &g_e, se, 2u + sizeof line - 1u);
    ak_check_stream(t, a);
    a = ak_begin(); /* control: one more byte */
    kit_api(a->k, MD5, "", kit_http(a->k, 500, "Error", "", "oops"));
    (void)tail_buf(&g_o, g_big_a, 64);
    (void)tail_buf(&g_e, g_big_b, 2u + sizeof line);
    core_buf_put_str(&g_e, core_str_from_cstr("xy"));
    rc = RUN_CLI(ak_deps(a), &g_o, &g_e, "download-url", MD5);
    CHECK_EQ_I64(t, rc, 4);
    CHECK_EQ_STR(t, core_buf_cstr(&g_e), "xyannas-c: E_API: 500: oops\n");
    CHECK(t, !core_buf_overflow(&g_e));
}

static void t_cli_overflow_of_help_and_internal_error(ctest_t *t) {
    ak_t *a = ak_begin();
    (void)tail_buf(&g_o, g_big_a, 100);
    (void)tail_buf(&g_e, g_big_b, 64);
    core_buf_put_str(&g_o, core_str_from_cstr("q"));
    int rc = RUN_CLI(NULL, &g_o, &g_e, "--help");
    CHECK_EQ_I64(t, rc, 0);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), "q");
    CHECK(t, core_buf_overflow(&g_o));
    (void)tail_buf(&g_o, g_big_a, 64);
    (void)tail_buf(&g_e, g_big_b, 10); /* the internal error line needs 46 bytes */
    g_d = *ak_deps(a);
    g_d.scratch_cap = 0;
    rc = RUN_CLI(&g_d, &g_o, &g_e, "download", MD5);
    CHECK_EQ_I64(t, rc, 1);
    CHECK_EQ_STR(t, core_buf_cstr(&g_e), "");
    CHECK(t, core_buf_overflow(&g_e));
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), "");
    ak_check_no_io(t, a);
}

static void t_mcp_overflow_is_all_or_nothing_for_every_capacity(ctest_t *t) {
    static const char ping[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}";
    static const char reply[] = AK_OKR("1", "{}");
    size_t rl = strlen(reply);
    for (size_t cap = 0; cap <= rl + 3u; cap++) {
        ak_t *a = ak_begin();
            char *so = tail_buf(&g_o, g_big_a, cap);
        size_t pre = cap >= 3u ? 2u : 0u;
        bool fits = cap >= pre + rl + 1u;
        if (pre > 0) {
            core_buf_put_str(&g_o, core_str_from_cstr("ab"));
        }
        app_mcp_result_t r = mcp_into(a, ping, &g_o);
        CHECK_EQ_I64(t, r, fits ? APP_MCP_REPLY : APP_MCP_E_OVERFLOW);
        CHECK_EQ_U64(t, core_buf_overflow(&g_o), !fits);
        CHECK_EQ_U64(t, core_buf_len(&g_o), fits ? pre + rl : pre);
        if (fits) {
            CHECK(t, memcmp(core_buf_str(&g_o).ptr + pre, reply, rl) == 0);
            check_spare(t, &g_o, so, cap);
        } else {
            check_content(t, &g_o, pre > 0 ? "ab" : "");
            check_spare(t, &g_o, so, cap);
        }
        ak_check_no_io(t, a);
    }
}

static void t_mcp_overflow_after_scrubbing_and_after_a_tool_run(ctest_t *t) {
    static const char want[] = AK_OKR("\"REDACTED\"", "{}");
    ak_t *a = ak_begin();
    const char *line = "{\"jsonrpc\":\"2.0\",\"id\":\"" KEY "\",\"method\":\"ping\"}";
    (void)tail_buf(&g_o, g_big_a, sizeof want); /* exactly the scrubbed reply: shorter than the unscrubbed one */
    CHECK_EQ_I64(t, mcp_into(a, line, &g_o), APP_MCP_REPLY);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), want);
    (void)tail_buf(&g_o, g_big_a, sizeof want - 1u);
    CHECK_EQ_I64(t, mcp_into(a, line, &g_o), APP_MCP_E_OVERFLOW);
    check_content(t, &g_o, "");
    a = ak_begin(); /* a tool run whose reply is lost: the run happened, the next call still works */
    script_download(a, "x");
    (void)tail_buf(&g_o, g_big_a, 40);
    CHECK_EQ_I64(t, call(a, "book_download", "{" HASH "}", &g_o), APP_MCP_E_OVERFLOW);
    check_content(t, &g_o, "");
    CHECK(t, core_buf_overflow(&g_o));
    ak_check_stream(t, a);
    kit_check_file(t, a->k, MD5 ".bin", "x", 1);
    (void)tail_buf(&g_o, g_big_a, 4096);
    CHECK_EQ_I64(t, mcp_into(a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}", &g_o), APP_MCP_REPLY);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), AK_OKR("1", "{}"));
}

/* ---- the MCP reply does not depend on the spare capacity of `out` ------------------------------------------------- */

static void t_mcp_replies_with_exact_out_and_untouched_spare_capacity(ctest_t *t) {
    ak_t *a = ak_begin();
    script_download(a, "x"); /* scripted before the first use of the kit; consumed by the second call */
    static const char list[] = AK_OKR("1", AK_TOOLS_LIST);
    char *so = tail_buf(&g_o, g_big_a, sizeof list);
    CHECK_EQ_I64(t, mcp_into(a, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}", &g_o), APP_MCP_REPLY);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), list);
    (void)so;
    static const char tool[] = AK_OKR("1", AK_TOOL_OK("Book downloaded successfully to: /dl/" MD5 ".bin"));
    so = tail_buf(&g_o, g_big_a, sizeof tool);
    CHECK_EQ_I64(t, call(a, "book_download", "{" HASH "}", &g_o), APP_MCP_REPLY);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), tool);
    CHECK(t, !core_buf_overflow(&g_o));
    ak_check_stream(t, a);
    a = ak_begin(); /* an operation error, large buffer: same bytes shape, spare capacity untouched */
    kit_api(a->k, MD5, "", kit_http(a->k, 500, "Error", "", "oops"));
    so = tail_buf(&g_o, g_big_a, 8192);
    CHECK_EQ_I64(t, call(a, "fast_download_url", "{" HASH "}", &g_o), APP_MCP_REPLY);
    CHECK_EQ_STR(t, core_buf_cstr(&g_o), AK_OKR("1", AK_TOOL_ERR("annas-c: E_API: 500: oops")));
    check_spare(t, &g_o, so, 8192);
    ak_check_stream(t, a);
}

/* ---- memory shortfalls of the MCP server (app_mcp_init) ------------------------------------------------------- */

/* The DOM of `pre` + pad bytes + `"}`, measured with json_parse on a fresh arena of the server's size: the
 * "memory left after the DOM" of app.h is capacity - used. SIZE_MAX when the DOM does not fit. */
static size_t line_with_pad(const char *pre, size_t pad) {
    size_t n = strlen(pre);
    memcpy(g_pad_line, pre, n);
    memset(g_pad_line + n, 'p', pad);
    memcpy(g_pad_line + n + pad, "\"}", 3);
    return n + pad + 2u;
}

static size_t left_after_dom(size_t len) {
    static core_arena_t ar;
    static unsigned char mem[APP_MCP_ARENA_MIN];
    json_limits_t lim = json_limits_default();
    const json_value_t *root = NULL;
    lim.max_input = APP_MCP_LINE_MAX;
    (void)core_arena_init(&ar, mem, sizeof mem);
    if (json_parse(&ar, (core_str_t){g_pad_line, len}, &lim, &root, NULL) != JSON_OK) {
        return (size_t)-1;
    }
    return core_arena_capacity(&ar) - core_arena_used(&ar);
}

/* Builds the line whose DOM leaves between `want` and `want + 64` bytes (the largest pad that leaves `want`);
 * returns its length, 0 when there is none. */
static size_t line_leaving(const char *pre, size_t want) {
    size_t lo = 0;
    size_t hi = APP_MCP_ARENA_MIN;
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1u) / 2u;
        size_t left = left_after_dom(line_with_pad(pre, mid));
        if (left != (size_t)-1 && left >= want) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    size_t len = line_with_pad(pre, lo);
    size_t left = left_after_dom(len);
    return (left != (size_t)-1 && left >= want && left <= want + 64u) ? len : 0;
}

#define CALL_PRE \
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"fast_download_url\",\"arguments\":{" HASH "}},\"pad\":\""
#define LIST_PRE "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\",\"pad\":\""
#define PING_PRE "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"pad\":\""
#define NOMEM_NULL AK_ERR("null", "-32603", "Internal error: request does not fit in memory")

/* One server over an arena of exactly APP_MCP_ARENA_MIN bytes, `want` bytes left after the DOM of the line. */
static app_mcp_result_t on_small_arena(ak_t *a, const char *pre, size_t want) {
    static app_mcp_t srv;
    static unsigned char small[APP_MCP_ARENA_MIN];
    size_t len = line_leaving(pre, want);
    ak_clear(a);
    if (len == 0 || !app_mcp_init(&srv, ak_deps(a), small, sizeof small)) {
        return APP_MCP_E_ARG;
    }
    return app_mcp_handle(&srv, (core_str_t){g_pad_line, len}, &a->out);
}

static void t_mcp_call_needs_arena_memory_left_after_the_dom(ctest_t *t) {
    ak_t *a = ak_begin();
    app_mcp_result_t r = on_small_arena(a, CALL_PRE, 12000);
    ak_check_reply(t, a, r, NOMEM_NULL); /* less than APP_MCP_CALL_MEM: not run, no port touched */
    ak_check_no_io(t, a);
    a = ak_begin();
    r = on_small_arena(a, CALL_PRE, 5000);
    ak_check_reply(t, a, r, NOMEM_NULL);
    ak_check_no_io(t, a);
    a = ak_begin(); /* control: with enough left the same request runs */
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    r = on_small_arena(a, CALL_PRE, 20000);
    ak_check_reply(t, a, r, AK_OKR("1", AK_TOOL_OK("Download URL: " URL)));
    ak_check_stream(t, a);
}

static void t_mcp_reply_that_does_not_fit_the_arena_leftover(ctest_t *t) {
    ak_t *a = ak_begin();
    app_mcp_result_t r = on_small_arena(a, LIST_PRE, 800); /* the tool list needs 1.7 KB, the error 107 bytes */
    ak_check_reply(t, a, r, NOMEM_NULL);
    a = ak_begin();
    r = on_small_arena(a, LIST_PRE, 4000); /* control */
    ak_check_reply(t, a, r, AK_OKR("1", AK_TOOLS_LIST));
    a = ak_begin();
    r = on_small_arena(a, PING_PRE, 800); /* control: a small reply fits the same leftover */
    ak_check_reply(t, a, r, AK_OKR("1", "{}"));
    a = ak_begin();
    r = on_small_arena(a, PING_PRE, 150);
    ak_check_reply(t, a, r, AK_OKR("1", "{}"));
    ak_check_no_io(t, a);
}

static void t_mcp_even_the_error_reply_does_not_fit(ctest_t *t) {
    ak_t *a = ak_begin();
    char *so = tail_buf(&g_o, g_big_a, 64);
    core_buf_put_str(&g_o, core_str_from_cstr("ab"));
    static app_mcp_t srv;
    static unsigned char small[APP_MCP_ARENA_MIN];
    size_t len = line_leaving(PING_PRE, 20);
    CHECK(t, len > 0);
    CHECK(t, app_mcp_init(&srv, ak_deps(a), small, sizeof small));
    CHECK_EQ_I64(t, app_mcp_handle(&srv, (core_str_t){g_pad_line, len}, &g_o), APP_MCP_E_OVERFLOW);
    check_content(t, &g_o, "ab");
    CHECK(t, core_buf_overflow(&g_o));
    check_spare(t, &g_o, so, 64);
    ak_check_no_io(t, a);
}

static void t_mcp_reply_length_cap(ctest_t *t) {
    static char big[APP_MCP_REPLY_MAX + 64u];
    static const char pre[] = "{\"jsonrpc\":\"2.0\",\"id\":\"";
    static const char post[] = "\",\"method\":\"ping\"}";
    ak_t *a = ak_begin();
    size_t n = APP_MCP_REPLY_MAX - 37u; /* reply = 23 + n + 14 bytes */
    for (int pass = 0; pass < 2; pass++) {
        size_t at = sizeof pre - 1u;
        memcpy(a->line, pre, at);
        memset(a->line + at, 'a', n + (size_t)pass);
        at += n + (size_t)pass;
        memcpy(a->line + at, post, sizeof post);
        at += sizeof post - 1u;
        (void)core_buf_init(&g_o, big, sizeof big);
        app_mcp_result_t r = ak_mcp_n(a, a->line, 0); /* init the server, ignore the blank */
        CHECK_EQ_I64(t, r, APP_MCP_NO_REPLY);
        r = app_mcp_handle(&a->mcp, (core_str_t){a->line, at}, &g_o);
        CHECK_EQ_I64(t, r, APP_MCP_REPLY);
        if (pass == 0) { /* exactly APP_MCP_REPLY_MAX bytes: delivered, id echoed whole */
            CHECK_EQ_U64(t, core_buf_len(&g_o), APP_MCP_REPLY_MAX);
            CHECK(t, memcmp(core_buf_str(&g_o).ptr + 23, a->line + sizeof pre - 1u, n) == 0);
        } else {         /* one byte more: the -32603 error replaces it */
            CHECK_EQ_STR(t, core_buf_cstr(&g_o), NOMEM_NULL);
        }
    }
    ak_check_no_io(t, a);
}

/* ---- scratch validation -------------------------------------------------------------------------------------- */

static void refused(ctest_t *t, ak_t *a, const app_deps_t *dp) {
    ak_clear(a);
    int rc = RUN_CLI(dp, &a->out, &a->err, "download", MD5);
    ak_check_cli(t, a, rc, 1, "", INTERNAL "\n");
    ak_check_no_io(t, a);
}

static void t_cli_scratch_is_validated(ctest_t *t) {
    ak_t *a = ak_begin();
    g_d = *ak_deps(a);
    g_d.scratch = NULL;
    refused(t, a, &g_d); /* NULL with a good size */
    g_d = *ak_deps(a);
    g_d.scratch_cap = APP_SCRATCH_MIN - 1u;
    refused(t, a, &g_d);
    g_d.scratch_cap = 0;
    refused(t, a, &g_d);
    g_d = *ak_deps(a);
    g_d.scratch = a->scratch + 1; /* misaligned, long enough */
    refused(t, a, &g_d);
    g_d.scratch = a->scratch + 8;
    refused(t, a, &g_d);
    g_d.scratch = a->scratch + 15;
    refused(t, a, &g_d);
    g_d = *ak_deps(a);
    g_d.scratch = NULL;
    g_d.scratch_cap = 0;
    refused(t, a, &g_d);
    /* the scratch check comes before the configuration: without scratch there is no memory for an error text */
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    a->deps_ready = false;
    g_d = *ak_deps(a);
    g_d.scratch_cap = APP_SCRATCH_MIN - 1u;
    refused(t, a, &g_d);
    g_d = *ak_deps(a); /* control: the same environment with a good scratch is a normal configuration error */
    ak_clear(a);
    int rc = RUN_CLI(&g_d, &a->out, &a->err, "download", MD5);
    ak_check_cli(t, a, rc, 2, "", "annas-c: E_CONFIG: " MISSING "\n");
}

static void t_cli_scratch_boundaries_are_accepted(ctest_t *t) {
    ak_t *a = ak_begin();
    script_download(a, "x");
    g_d = *ak_deps(a);
    g_d.scratch = a->scratch + 16; /* aligned, not the start of the array */
    g_d.scratch_cap = APP_SCRATCH_MIN;
    int rc = RUN_CLI(&g_d, &a->out, &a->err, "download", MD5);
    ak_check_cli(t, a, rc, 0, DONE_TEXT "\n", "");
    ak_check_stream(t, a);
    a = ak_begin();
    script_download(a, "x");
    g_d = *ak_deps(a);
    g_d.scratch_cap = APP_SCRATCH_MIN + 64u; /* larger than the minimum */
    rc = RUN_CLI(&g_d, &a->out, &a->err, "download", MD5);
    ak_check_cli(t, a, rc, 0, DONE_TEXT "\n", "");
    ak_check_stream(t, a);
    a = ak_begin(); /* help and version need no deps at all, whatever the scratch */
    rc = RUN_CLI(NULL, &a->out, &a->err, "--version");
    ak_check_cli(t, a, rc, 0, "annas-c 0.1\n", "");
}

static void t_cli_scratch_is_refused_for_download_url_too(ctest_t *t) {
    ak_t *a = ak_begin();
    g_d = *ak_deps(a);
    g_d.scratch_cap = APP_SCRATCH_MIN - 1u;
    int rc = RUN_CLI(&g_d, &a->out, &a->err, "download-url", MD5);
    ak_check_cli(t, a, rc, 1, "", INTERNAL "\n");
    ak_check_no_io(t, a);
    a = ak_begin(); /* control */
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 0, "Download URL: " URL "\n", "");
    ak_check_stream(t, a);
}

static void t_mcp_init_validates_scratch(ctest_t *t) {
    ak_t *a = ak_begin();
    static app_mcp_t srv;
    g_d = *ak_deps(a);
    CHECK(t, app_mcp_init(&srv, &g_d, a->parse_mem, sizeof a->parse_mem)); /* control */
    g_d.scratch = NULL;
    CHECK(t, !app_mcp_init(&srv, &g_d, a->parse_mem, sizeof a->parse_mem));
    g_d = *ak_deps(a);
    g_d.scratch_cap = APP_SCRATCH_MIN - 1u;
    CHECK(t, !app_mcp_init(&srv, &g_d, a->parse_mem, sizeof a->parse_mem));
    g_d.scratch_cap = 0;
    CHECK(t, !app_mcp_init(&srv, &g_d, a->parse_mem, sizeof a->parse_mem));
    g_d = *ak_deps(a);
    g_d.scratch = a->scratch + 1;
    CHECK(t, !app_mcp_init(&srv, &g_d, a->parse_mem, sizeof a->parse_mem));
    g_d.scratch = a->scratch + 8;
    CHECK(t, !app_mcp_init(&srv, &g_d, a->parse_mem, sizeof a->parse_mem));
    g_d.scratch = a->scratch + 16; /* controls: aligned and long enough */
    CHECK(t, app_mcp_init(&srv, &g_d, a->parse_mem, sizeof a->parse_mem));
    g_d = *ak_deps(a);
    g_d.scratch_cap = APP_SCRATCH_MIN + 64u;
    CHECK(t, app_mcp_init(&srv, &g_d, a->parse_mem, sizeof a->parse_mem));
    g_d = *ak_deps(a);
    g_d.workspace_cap = 100; /* ports and workspace are checked per tools/call, not here */
    CHECK(t, app_mcp_init(&srv, &g_d, a->parse_mem, sizeof a->parse_mem));
}

/* ---- the largest texts fit in exactly APP_SCRATCH_MIN ------------------------------------------------------------ */

#define LONG_URL_LEN 2048u
#define ACCT_QUOTES 2040u

static char g_url[LONG_URL_LEN + 1];
static char g_target[LONG_URL_LEN];
static char g_acct[ACCT_QUOTES * 2u + 16u];
static char g_title[201];

static void build_longest(void) {
    size_t n = 0;
    memcpy(g_url, "https://dl.example.org/", 23);
    memset(g_url + 23, 'u', LONG_URL_LEN - 23u);
    g_url[LONG_URL_LEN] = '\0';
    memcpy(g_target, g_url + 22, LONG_URL_LEN - 22u + 1u);
    memcpy(g_acct, "{\"q\":\"", 6);
    n = 6;
    for (size_t i = 0; i < ACCT_QUOTES; i++) {
        g_acct[n++] = '\\';
        g_acct[n++] = '"';
    }
    memcpy(g_acct + n, "\"}", 3);
    memset(g_title, 'a', 200);
    g_title[200] = '\0';
}

static size_t jesc(char *dst, size_t at, const char *s) {
    static const char hex[] = "0123456789abcdef";
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            dst[at++] = '\\';
            dst[at++] = (char)c;
        } else if (c < 0x20u) {
            dst[at++] = '\\';
            dst[at++] = 'u';
            dst[at++] = '0';
            dst[at++] = '0';
            dst[at++] = hex[c >> 4];
            dst[at++] = hex[c & 15u];
        } else {
            dst[at++] = (char)c;
        }
    }
    dst[at] = '\0';
    return at;
}

static void script_longest(ak_t *a, bool file) {
    build_longest();
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, g_url, g_acct)));
    if (file) {
        kit_get(a->k, "dl.example.org", 443, g_target, kit_http(a->k, 200, "OK", "", "x"));
    }
}

static void t_cli_longest_texts_fit_in_the_minimum_scratch(ctest_t *t) {
    ak_t *a = ak_begin();
    script_longest(a, true);
    int rc = AK_CLI(a, "download", MD5, "--name", g_title, "--format", "pdf");
    (void)snprintf(g_exp, sizeof g_exp, "Book downloaded successfully to: /dl/%s.pdf\nAccount: %s\n", g_title, g_acct);
    ak_check_cli(t, a, rc, 0, g_exp, "");
    ak_check_stream(t, a);
    a = ak_begin();
    script_longest(a, false);
    rc = AK_CLI(a, "download-url", MD5);
    (void)snprintf(g_exp, sizeof g_exp, "Download URL: %s\nAccount: %s\n", g_url, g_acct);
    ak_check_cli(t, a, rc, 0, g_exp, "");
    ak_check_stream(t, a);
}

static void t_mcp_longest_replies_fit_in_the_minimum_scratch(ctest_t *t) {
    ak_t *a = ak_begin();
    size_t n;
    script_longest(a, true);
    (void)snprintf(g_args, sizeof g_args, "{" HASH ",\"title\":\"%s\",\"format\":\"pdf\"}", g_title);
    app_mcp_result_t r = call(a, "book_download", g_args, &a->out);
    n = (size_t)snprintf(g_exp, sizeof g_exp, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"");
    (void)snprintf(g_pad_line, sizeof g_pad_line, "Book downloaded successfully to: /dl/%s.pdf\nAccount: %s", g_title, g_acct);
    n = jesc(g_exp, n, g_pad_line);
    memcpy(g_exp + n, "\"}],\"isError\":false}}", 22);
    ak_check_reply(t, a, r, g_exp);
    CHECK(t, core_buf_len(&a->out) > 8000u); /* the escaped account really is the large part */
    ak_check_stream(t, a);
    a = ak_begin();
    script_longest(a, false);
    r = call(a, "fast_download_url", "{" HASH "}", &a->out);
    n = (size_t)snprintf(g_exp, sizeof g_exp, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"");
    (void)snprintf(g_pad_line, sizeof g_pad_line, "Download URL: %s\nAccount: %s", g_url, g_acct);
    n = jesc(g_exp, n, g_pad_line);
    memcpy(g_exp + n, "\"}],\"isError\":false}}", 22);
    ak_check_reply(t, a, r, g_exp);
    ak_check_stream(t, a);
}

/* ---- a missing env port -------------------------------------------------------------------------------------- */

static void t_cli_env_port_missing(ctest_t *t) {
    static const ports_env_ops_t no_get = {NULL};
    ak_t *a = ak_begin();
    g_d = *ak_deps(a);
    g_d.env.ops = NULL;
    int rc = RUN_CLI(&g_d, &a->out, &a->err, "download", MD5);
    ak_check_cli(t, a, rc, 1, "", INTERNAL "\n");
    ak_check_no_io(t, a);
    ak_clear(a);
    g_d = *ak_deps(a);
    g_d.env.ops = &no_get;
    rc = RUN_CLI(&g_d, &a->out, &a->err, "download-url", MD5);
    ak_check_cli(t, a, rc, 1, "", INTERNAL "\n");
    ak_check_no_io(t, a);
    ak_clear(a);
    rc = RUN_CLI(&g_d, &a->out, &a->err, "--version"); /* control: commands without operations do not need it */
    ak_check_cli(t, a, rc, 0, "annas-c 0.1\n", "");
}

static void t_mcp_env_port_missing(ctest_t *t) {
    static const ports_env_ops_t no_get = {NULL};
    static app_mcp_t srv;
    ak_t *a = ak_begin();
    g_d = *ak_deps(a);
    g_d.env.ops = NULL;
    CHECK(t, app_mcp_init(&srv, &g_d, a->parse_mem, sizeof a->parse_mem));
    ak_clear(a);
    snprintf(g_req, sizeof g_req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"fast_download_url\",\"arguments\":{" HASH "}}}");
    CHECK_EQ_I64(t, app_mcp_handle(&srv, core_str_from_cstr(g_req), &a->out), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), AK_OKR("1", AK_TOOL_ERR(INTERNAL)));
    ak_check_no_io(t, a);
    g_d.env.ops = &no_get;
    CHECK(t, app_mcp_init(&srv, &g_d, a->parse_mem, sizeof a->parse_mem));
    ak_clear(a);
    snprintf(g_req, sizeof g_req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"book_download\",\"arguments\":{" HASH "}}}");
    CHECK_EQ_I64(t, app_mcp_handle(&srv, core_str_from_cstr(g_req), &a->out), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), AK_OKR("1", AK_TOOL_ERR(INTERNAL)));
    ak_check_no_io(t, a);
    /* the other methods work, and without an env there is no key to scrub */
    ak_clear(a);
    CHECK_EQ_I64(t, app_mcp_handle(&srv, core_str_from_cstr("{\"jsonrpc\":\"2.0\",\"id\":\"" KEY "\",\"method\":\"ping\"}"), &a->out),
                 APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), AK_OKR("\"" KEY "\"", "{}"));
    /* control: the same server over a working env scrubs it */
    a = ak_begin();
    ak_check_reply(t, a, ak_mcp(a, "{\"jsonrpc\":\"2.0\",\"id\":\"" KEY "\",\"method\":\"ping\"}"), AK_OKR("\"REDACTED\"", "{}"));
}

CTEST_SUITE("scratch",
    {"cli_success_with_exact_buffers", t_cli_success_with_exact_buffers},
    {"cli_success_leaves_spare_capacity_untouched", t_cli_success_leaves_spare_capacity_untouched},
    {"cli_failure_with_exact_buffers", t_cli_failure_with_exact_buffers},
    {"cli_config_error_with_exact_buffers", t_cli_config_error_with_exact_buffers},
    {"cli_internal_error_and_help_with_exact_buffers", t_cli_internal_error_and_help_with_exact_buffers},
    {"cli_fit_is_judged_after_scrubbing", t_cli_fit_is_judged_after_scrubbing},
    {"cli_exact_buffers_are_not_cleared", t_cli_exact_buffers_are_not_cleared},
    {"cli_overflow_of_stdout_is_all_or_nothing", t_cli_overflow_of_stdout_is_all_or_nothing},
    {"cli_overflow_of_stderr_is_all_or_nothing", t_cli_overflow_of_stderr_is_all_or_nothing},
    {"cli_overflow_of_help_and_internal_error", t_cli_overflow_of_help_and_internal_error},
    {"mcp_overflow_is_all_or_nothing_for_every_capacity", t_mcp_overflow_is_all_or_nothing_for_every_capacity},
    {"mcp_overflow_after_scrubbing_and_after_a_tool_run", t_mcp_overflow_after_scrubbing_and_after_a_tool_run},
    {"mcp_replies_with_exact_out_and_untouched_spare_capacity", t_mcp_replies_with_exact_out_and_untouched_spare_capacity},
    {"mcp_call_needs_arena_memory_left_after_the_dom", t_mcp_call_needs_arena_memory_left_after_the_dom},
    {"mcp_reply_that_does_not_fit_the_arena_leftover", t_mcp_reply_that_does_not_fit_the_arena_leftover},
    {"mcp_even_the_error_reply_does_not_fit", t_mcp_even_the_error_reply_does_not_fit},
    {"mcp_reply_length_cap", t_mcp_reply_length_cap},
    {"cli_scratch_is_validated", t_cli_scratch_is_validated},
    {"cli_scratch_boundaries_are_accepted", t_cli_scratch_boundaries_are_accepted},
    {"cli_scratch_is_refused_for_download_url_too", t_cli_scratch_is_refused_for_download_url_too},
    {"mcp_init_validates_scratch", t_mcp_init_validates_scratch},
    {"cli_longest_texts_fit_in_the_minimum_scratch", t_cli_longest_texts_fit_in_the_minimum_scratch},
    {"mcp_longest_replies_fit_in_the_minimum_scratch", t_mcp_longest_replies_fit_in_the_minimum_scratch},
    {"cli_env_port_missing", t_cli_env_port_missing},
    {"mcp_env_port_missing", t_mcp_env_port_missing})
