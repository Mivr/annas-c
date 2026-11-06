/* wire: the command line. --help, --version and usage errors need no file, no environment and no network; the texts are
 * app's, byte for byte (the expected text is computed with app_cli_run / app_cli_parse); the mode of the second binary
 * (annas-c-mcp) takes no arguments; invalid arguments of wire_run are exit 1 and touch nothing. */
#include <string.h>

#include "app/app.h"
#include "ctest/ctest.h"
#include "wire/tests/kit.h"

static app_cmd_t g_cmd;
static app_usage_error_t g_uerr;
static char g_exp_out[8192];
static char g_exp_err[8192];
static core_buf_t g_eo;
static core_buf_t g_ee;

static void base(void) {
    kw_reset();
    kw_net_env();
}

/* The text app prints for HELP / VERSION. */
static void expected(app_cmd_kind_t kind) {
    app_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.kind = kind;
    (void)core_buf_init(&g_eo, g_exp_out, sizeof g_exp_out);
    (void)core_buf_init(&g_ee, g_exp_err, sizeof g_exp_err);
    (void)app_cli_run(&cmd, NULL, &g_eo, &g_ee);
}

static void check_untouched(ctest_t *t) {
    CHECK_EQ_U64(t, kw_reads(), 0);
    CHECK_EQ_U64(t, kw()->unix_calls, 0);
    CHECK_EQ_U64(t, kw()->in_calls, 0);
    CHECK_EQ_U64(t, kw()->fs.opens, 0);
    CHECK_EQ_U64(t, kw()->refused, 0);
    kw_check_no_empty_writes(t);
}

/* A usage error reads `.env` (best effort, to scrub the key) and nothing else. */
static void check_only_dotenv_read(ctest_t *t) {
    CHECK_EQ_U64(t, kw_reads(), 1);
    CHECK_EQ_STR(t, kw_read_path(0), ".env");
    CHECK_EQ_U64(t, kw()->unix_calls, 0);
    CHECK_EQ_U64(t, kw()->in_calls, 0);
    CHECK_EQ_U64(t, kw()->fs.opens, 0);
    CHECK_EQ_U64(t, kw()->refused, 0);
    kw_check_no_empty_writes(t);
}

static void t_help_and_version(ctest_t *t) {
    static const char *const help[] = {"--help", "-h"};
    expected(APP_CMD_HELP);
    CHECK(t, strlen(g_exp_out) > 300u);
    for (size_t i = 0; i < 2; i++) {
        base();
        kw_check(t, KW_CLI(help[i]), 0, g_exp_out, "");
        check_untouched(t);
        CHECK_EQ_U64(t, kw()->w_calls[2], 0);
    }
    expected(APP_CMD_VERSION);
    CHECK_EQ_STR(t, g_exp_out, "annas-c 0.1\n");
    base();
    kw_check(t, KW_CLI("--version"), 0, "annas-c 0.1\n", "");
    check_untouched(t);
}

static void t_help_works_with_nothing_configured(ctest_t *t) {
    kw_reset(); /* no environment, no files, nothing */
    expected(APP_CMD_HELP);
    kw_check(t, KW_CLI("--help"), 0, g_exp_out, "");
    check_untouched(t);
}

static void usage_case(ctest_t *t, int argc, const char *const *argv) {
    memset(&g_uerr, 0, sizeof g_uerr);
    CHECK(t, !app_cli_parse(argc, argv, &g_cmd, &g_uerr));
    base();
    kw_check(t, kw_run(WIRE_CLI, argc, argv), 2, "", g_uerr.text);
    CHECK(t, g_uerr.len > 10u);
    check_only_dotenv_read(t);
}

static void t_usage_errors(ctest_t *t) {
    static const char *const a0[] = {"annas-c"};
    static const char *const a1[] = {"annas-c", "bogus"};
    static const char *const a2[] = {"annas-c", "download"};
    static const char *const a3[] = {"annas-c", "download", KW_MD5, "--nope"};
    static const char *const a4[] = {"annas-c", "download", KW_MD5, "--timeout"};
    static const char *const a5[] = {"annas-c", "download-url", KW_MD5, "extra"};
    static const char *const a6[] = {"annas-c", "download", KW_MD5, "--path-index", "-1"};
    static const char *const a7[] = {"annas-c", "book-download", KW_MD5, "noext"};
    static const char *const a8[] = {"annas-c", "--version", "x"};
    static const char *const a9[] = {"annas-c", "mcp", "x"};
    usage_case(t, 1, a0);
    usage_case(t, 2, a1);
    usage_case(t, 2, a2);
    usage_case(t, 4, a3);
    usage_case(t, 4, a4);
    usage_case(t, 4, a5);
    usage_case(t, 5, a6);
    usage_case(t, 4, a7);
    usage_case(t, 3, a8);
    usage_case(t, 3, a9);
}

static void t_a_usage_error_never_echoes_the_key(ctest_t *t) {
    base(); /* the key from the process environment */
    CHECK_EQ_I64(t, KW_CLI(KW_KEY), 2);
    CHECK(t, kw_contains(kw_err(), "annas-c: E_ARG: unknown command: "));
    CHECK(t, kw_contains(kw_err(), "REDACTED"));
    kw_check_no_key(t, KW_KEY);
    check_only_dotenv_read(t);
    kw_reset(); /* the key from .env only */
    kw_file(".env", "ANNAS_SECRET_KEY=" KW_KEY "\n");
    CHECK_EQ_I64(t, KW_CLI("download", KW_MD5, KW_KEY), 2);
    CHECK(t, kw_contains(kw_err(), "REDACTED"));
    kw_check_no_key(t, KW_KEY);
    kw_reset(); /* control: a different word is echoed as it is */
    kw_file(".env", "ANNAS_SECRET_KEY=" KW_KEY "\n");
    CHECK_EQ_I64(t, KW_CLI("bogus-word"), 2);
    CHECK(t, kw_contains(kw_err(), "unknown command: bogus-word"));
    CHECK(t, !kw_contains(kw_err(), "REDACTED"));
}

static void t_a_broken_dotenv_does_not_change_a_usage_error(ctest_t *t) {
    static const char *const argv[] = {"annas-c", "bogus"};
    CHECK(t, !app_cli_parse(2, argv, &g_cmd, &g_uerr));
    kw_reset();
    kw_file_err(".env", -5);
    kw_check(t, kw_run(WIRE_CLI, 2, argv), 2, "", g_uerr.text);
    kw_reset();
    kw_file_fill(".env", 'x', 3u * WIRE_DOTENV_MAX);
    kw_check(t, kw_run(WIRE_CLI, 2, argv), 2, "", g_uerr.text);
    kw_reset();
    kw_file(".env", "ANNAS_SECRET_KEY=" KW_KEY "\n");
    kw_check(t, kw_run(WIRE_CLI, 2, argv), 2, "", g_uerr.text);
}

static void t_a_valid_command_is_not_a_usage_error(ctest_t *t) {
    base();
    kw_env("ANNAS_BASE_URL", "bad_host");
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 2);
    CHECK(t, kw_contains(kw_err(), "E_CONFIG"));
    CHECK(t, kw_reads() >= 2u); /* it did read .env and the CA bundle */
}

static void t_mcp_mode_takes_no_arguments(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ "\n");
    kw_check(t, KW_MCP0(), 0, KW_PING_REPLY "\n", "");
    CHECK_EQ_U64(t, kw_reads() >= 2u, 1);

    static const char *const bad[][4] = {{"annas-c-mcp", "download", KW_MD5, NULL},
                                         {"annas-c-mcp", "mcp", NULL, NULL},
                                         {"annas-c-mcp", "--version", "x", NULL},
                                         {"annas-c-mcp", "bogus", NULL, NULL},
                                         {"annas-c-mcp", KW_KEY, NULL, NULL},
                                         {"annas-c-mcp", "", NULL, NULL}};
    static const int argc[] = {3, 2, 3, 2, 2, 2};
    for (size_t i = 0; i < 6; i++) {
        base();
        kw_in(KW_PING_REQ "\n");
        kw_check(t, kw_run(WIRE_MCP, argc[i], bad[i]), 2, "", WIRE_ERR_MCP_ARGS);
        check_untouched(t);
        kw_check_no_key(t, KW_KEY);
    }
}

static void t_mcp_mode_help_and_version(ctest_t *t) {
    expected(APP_CMD_HELP);
    base();
    kw_check(t, KW_RUN(WIRE_MCP, "annas-c-mcp", "--help"), 0, g_exp_out, "");
    check_untouched(t);
    base();
    CHECK_EQ_I64(t, KW_RUN(WIRE_MCP, "annas-c-mcp", "-h"), 0);
    CHECK_EQ_STR(t, kw_out(), g_exp_out);
    base();
    kw_check(t, KW_RUN(WIRE_MCP, "annas-c-mcp", "--version"), 0, "annas-c 0.1\n", "");
    check_untouched(t);
}

static void t_cli_mcp_subcommand_runs_the_loop(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ "\n");
    kw_check(t, KW_CLI("mcp"), 0, KW_PING_REPLY "\n", "");
}

static int run_with(wire_mem_t *m, wire_mode_t mode, const wire_io_t *io, int argc, const char *const *argv) {
    return wire_run(m, mode, io, argc, argv, kw()->envp);
}

static void t_invalid_arguments_of_wire_run(ctest_t *t) {
    static const char *const argv[] = {"annas-c", "--version"};
    wire_sys_t sys;
    wire_io_t io;
    base();
    CHECK_EQ_I64(t, run_with(kw_mem(), WIRE_CLI, kw_io(), 2, argv), 0); /* control */
    CHECK_EQ_STR(t, kw_out(), "annas-c 0.1\n");
    base();
    CHECK_EQ_I64(t, run_with(NULL, WIRE_CLI, kw_io(), 2, argv), 1);
    CHECK_EQ_I64(t, run_with(kw_mem(), WIRE_CLI, NULL, 2, argv), 1);
    CHECK_EQ_I64(t, run_with(kw_mem(), WIRE_CLI, kw_io(), 2, NULL), 1);
    CHECK_EQ_I64(t, run_with(kw_mem(), WIRE_CLI, kw_io(), 0, argv), 1);
    CHECK_EQ_I64(t, run_with(kw_mem(), WIRE_CLI, kw_io(), -1, argv), 1);
    CHECK_EQ_I64(t, run_with(kw_mem(), (wire_mode_t)0, kw_io(), 2, argv), 1);
    CHECK_EQ_I64(t, run_with(kw_mem(), (wire_mode_t)3, kw_io(), 2, argv), 1);
    io = *kw_io();
    io.sys = NULL;
    CHECK_EQ_I64(t, run_with(kw_mem(), WIRE_CLI, &io, 2, argv), 1);
    for (int which = 0; which < 4; which++) {
        sys = *kw_io()->sys;
        io = *kw_io();
        io.sys = &sys;
        if (which == 0) {
            sys.read_file = NULL;
        } else if (which == 1) {
            sys.read_in = NULL;
        } else if (which == 2) {
            sys.write_out = NULL;
        } else {
            sys.unix_seconds = NULL;
        }
        CHECK_EQ_I64(t, run_with(kw_mem(), WIRE_CLI, &io, 2, argv), 1);
        CHECK_EQ_I64(t, run_with(kw_mem(), WIRE_MCP, &io, 2, argv), 1);
    }
    CHECK_EQ_U64(t, kw_out_len(), 0);
    CHECK_EQ_U64(t, kw_err_len(), 0);
    check_untouched(t);
}

CTEST_SUITE("wire_usage", {"help_and_version", t_help_and_version},
            {"help_works_with_nothing_configured", t_help_works_with_nothing_configured},
            {"usage_errors", t_usage_errors}, {"a_usage_error_never_echoes_the_key", t_a_usage_error_never_echoes_the_key},
            {"a_broken_dotenv_does_not_change_a_usage_error", t_a_broken_dotenv_does_not_change_a_usage_error},
            {"a_valid_command_is_not_a_usage_error", t_a_valid_command_is_not_a_usage_error},
            {"mcp_mode_takes_no_arguments", t_mcp_mode_takes_no_arguments},
            {"mcp_mode_help_and_version", t_mcp_mode_help_and_version},
            {"cli_mcp_subcommand_runs_the_loop", t_cli_mcp_subcommand_runs_the_loop},
            {"invalid_arguments_of_wire_run", t_invalid_arguments_of_wire_run})
