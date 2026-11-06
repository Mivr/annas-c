/* app_cli_parse: every usage error, with its exact text. Each group has accepted near misses in parse_test.c;
 * the near misses used inline here are marked "control". */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"

#define MD5 "0123456789abcdef0123456789abcdef"

static void usage(ctest_t *t, int argc, const char *const *argv, const char *msg) {
    app_cmd_t c;
    app_cmd_t before;
    static app_usage_error_t e;
    static char want[APP_USAGE_MAX + 64];
    memset(&c, 0xA5, sizeof c);
    before = c;
    memset(&e, 0xA5, sizeof e);
    CHECK(t, !app_cli_parse(argc, argv, &c, &e));
    CHECK(t, memcmp(&c, &before, sizeof c) == 0);
    (void)snprintf(want, sizeof want, "annas-c: E_ARG: %s\nTry 'annas-c --help' for usage.\n", msg);
    CHECK_EQ_STR(t, e.text, want);
    CHECK_EQ_U64(t, e.len, strlen(want));
}

static const char *g_argv[24];

static int collect(va_list ap) {
    int n = 1;
    g_argv[0] = "annas-c";
    for (const char *s = va_arg(ap, const char *); s != NULL && n < 23; s = va_arg(ap, const char *)) {
        g_argv[n++] = s;
    }
    g_argv[n] = NULL;
    return n;
}

static void usage_list(ctest_t *t, const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    int n = collect(ap);
    va_end(ap);
    usage(t, n, g_argv, msg);
}
#define USAGE(t, msg, ...) usage_list((t), (msg), __VA_ARGS__, (const char *)NULL)

static void ok(ctest_t *t, int argc, const char *const *argv) {
    app_cmd_t c;
    static app_usage_error_t e;
    CHECK(t, app_cli_parse(argc, argv, &c, &e));
}
static void ok_list(ctest_t *t, ...) {
    va_list ap;
    va_start(ap, t);
    int n = collect(ap);
    va_end(ap);
    ok(t, n, g_argv);
}
#define OK(t, ...) ok_list((t), __VA_ARGS__, (const char *)NULL)

static void t_no_command(ctest_t *t) {
    const char *only[] = {"annas-c"};
    usage(t, 1, only, "missing command");
    app_cmd_t c;
    static app_usage_error_t e;
    CHECK(t, !app_cli_parse(0, only, &c, &e));
    CHECK_EQ_STR(t, e.text, "annas-c: E_ARG: missing command\nTry 'annas-c --help' for usage.\n");
    CHECK(t, !app_cli_parse(2, NULL, &c, &e));
    CHECK_EQ_STR(t, e.text, "annas-c: E_ARG: missing command\nTry 'annas-c --help' for usage.\n");
    CHECK(t, !app_cli_parse(2, only, NULL, &e));
    CHECK(t, !app_cli_parse(1, only, &c, NULL));
    OK(t, "mcp"); /* control */
}

static void t_unknown_command(ctest_t *t) {
    USAGE(t, "unknown command: frobnicate", "frobnicate");
    USAGE(t, "unknown command: Download", "Download");
    USAGE(t, "unknown command: help", "help");
    USAGE(t, "unknown command: -", "-");
    USAGE(t, "unknown command: ", "");
    USAGE(t, "unknown command: search", "search", "query");
    USAGE(t, "unknown command: download-url2", "download-url2", MD5);
    OK(t, "download-url", MD5); /* control */
}

static void t_unknown_option_in_place_of_a_command(ctest_t *t) {
    USAGE(t, "unknown option: -x", "-x");
    USAGE(t, "unknown option: --name", "--name");
    USAGE(t, "unknown option: --Help", "--Help");
    USAGE(t, "unknown option: --version=1", "--version=1");
    USAGE(t, "unknown option: --", "--");
    OK(t, "--help"); /* control */
    OK(t, "-h");
    OK(t, "--version");
}

static void t_help_and_version_take_nothing(ctest_t *t) {
    USAGE(t, "unexpected argument: x", "--help", "x");
    USAGE(t, "unexpected argument: x", "-h", "x");
    USAGE(t, "unexpected argument: download", "--version", "download");
    USAGE(t, "unknown option: --version", "--help", "--version");
    USAGE(t, "unknown option: -h", "--version", "-h");
    USAGE(t, "unknown option: --help", "download", MD5, "--help");
    USAGE(t, "unknown option: -h", "mcp", "-h");
}

static void t_mcp_takes_nothing(ctest_t *t) {
    USAGE(t, "unexpected argument: x", "mcp", "x");
    USAGE(t, "unexpected argument: ", "mcp", "");
    USAGE(t, "unknown option: --timeout", "mcp", "--timeout", "5");
    USAGE(t, "unknown option: --", "mcp", "--", "x"); /* the first problem wins */
}

static void t_missing_md5(ctest_t *t) {
    USAGE(t, "missing <md5>", "download");
    USAGE(t, "missing <md5>", "download-url");
    USAGE(t, "missing <md5>", "book-download");
    USAGE(t, "missing <md5>", "download", "--name", "x");
    USAGE(t, "missing <md5>", "download-url", "--timeout", "5", "--path-index", "1");
    OK(t, "download", MD5, "--name", "x"); /* control */
    OK(t, "download-url", MD5, "--timeout", "5");
}

static void t_unexpected_positional(ctest_t *t) {
    USAGE(t, "unexpected argument: extra", "download", MD5, "extra");
    USAGE(t, "unexpected argument: two", "download", "one", "two");
    USAGE(t, "unexpected argument: extra", "download-url", MD5, "extra");
    USAGE(t, "unexpected argument: ", "download", MD5, "");
    USAGE(t, "unexpected argument: c", "book-download", MD5, "a.pdf", "c");
    USAGE(t, "unexpected argument: x", "download", "--name", "n", MD5, "x");
}

static void t_unknown_option_in_a_command(ctest_t *t) {
    USAGE(t, "unknown option: --bogus", "download", MD5, "--bogus");
    USAGE(t, "unknown option: --name=x", "download", MD5, "--name=x");
    USAGE(t, "unknown option: --Name", "download", MD5, "--Name", "x");
    USAGE(t, "unknown option: -n", "download", MD5, "-n", "x");
    USAGE(t, "unknown option: --path_index", "download", MD5, "--path_index", "1");
    USAGE(t, "unknown option: --", "download", MD5, "--");
}

static void t_options_that_belong_to_other_commands(ctest_t *t) {
    USAGE(t, "unknown option: --name", "download-url", MD5, "--name", "x");
    USAGE(t, "unknown option: --format", "download-url", MD5, "--format", "pdf");
    USAGE(t, "unknown option: --name", "book-download", MD5, "--name", "x");
    USAGE(t, "unknown option: --timeout", "book-download", MD5, "a.pdf", "--timeout");
    USAGE(t, "unknown option: --path-index", "book-download", "--path-index", "1", MD5, "a.pdf");
    OK(t, "download", MD5, "--name", "x", "--format", "pdf"); /* control: the same options on download */
}

static void t_option_without_a_value(ctest_t *t) {
    USAGE(t, "option needs a value: --name", "download", MD5, "--name");
    USAGE(t, "option needs a value: --format", "download", MD5, "--format");
    USAGE(t, "option needs a value: --path-index", "download", MD5, "--path-index");
    USAGE(t, "option needs a value: --domain-index", "download", MD5, "--domain-index");
    USAGE(t, "option needs a value: --timeout", "download", MD5, "--timeout");
    USAGE(t, "option needs a value: --timeout", "download-url", MD5, "--timeout");
    USAGE(t, "option needs a value: --path-index", "download-url", "--path-index");
    OK(t, "download", MD5, "--name", ""); /* control: an empty value is a value */
}

static void t_option_twice(ctest_t *t) {
    USAGE(t, "option given twice: --name", "download", MD5, "--name", "a", "--name", "b");
    USAGE(t, "option given twice: --format", "download", MD5, "--format", "a", "--format", "b");
    USAGE(t, "option given twice: --path-index", "download", MD5, "--path-index", "1", "--path-index", "1");
    USAGE(t, "option given twice: --domain-index", "download-url", MD5, "--domain-index", "1", "--domain-index", "2");
    USAGE(t, "option given twice: --timeout", "download", MD5, "--timeout", "1", "--timeout", "2");
    USAGE(t, "option given twice: --name", "download", MD5, "--name", "", "--name", "b"); /* even if empty */
    OK(t, "download", MD5, "--name", "a", "--format", "b"); /* control: different options */
}

static void t_bad_path_and_domain_index(ctest_t *t) {
    static const char *const bad[] = {"", "-1", "+1", "1x", "x1", "1 ", " 1", "0x10", "1.5", "1e3", "-",
                                      "18446744073709551616", "99999999999999999999999"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char want[96];
        (void)snprintf(want, sizeof want, "invalid value for --path-index: %s", bad[i]);
        USAGE(t, want, "download", MD5, "--path-index", bad[i]);
        (void)snprintf(want, sizeof want, "invalid value for --domain-index: %s", bad[i]);
        USAGE(t, want, "download-url", MD5, "--domain-index", bad[i]);
    }
    OK(t, "download", MD5, "--path-index", "18446744073709551615"); /* control */
}

static void t_bad_timeout(ctest_t *t) {
    static const char *const bad[] = {"abc", "", "-", "--5", "+5", "5s", "1.5", " 5", "5 ", "1e3", "0x10",
                                      "9223372036854775808", "-9223372036854775809", "99999999999999999999"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char want[96];
        (void)snprintf(want, sizeof want, "invalid value for --timeout: %s", bad[i]);
        USAGE(t, want, "download", MD5, "--timeout", bad[i]);
        USAGE(t, want, "download-url", MD5, "--timeout", bad[i]);
    }
    OK(t, "download", MD5, "--timeout", "-9223372036854775808"); /* control */
}

static void t_alias_filename(ctest_t *t) {
    static const char *const bad[] = {"noext", "name.", ".pdf", ".", "..", "...", "", "a b", "dir/name"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char want[96];
        (void)snprintf(want, sizeof want, "filename must be <title>.<ext>, got: %s", bad[i]);
        USAGE(t, want, "book-download", MD5, bad[i]);
    }
    OK(t, "book-download", MD5, "a.b"); /* control: shortest; the first bad group has no '.' with bytes on both sides */
    OK(t, "book-download", MD5, "a b.c");
    OK(t, "book-download", MD5, "dir/name.pdf"); /* '/' is the sanitiser's business, not the parser's */
}

static void t_alias_missing_filename(ctest_t *t) {
    USAGE(t, "missing <filename.ext>", "book-download", MD5);
    USAGE(t, "missing <filename.ext>", "book-download", "x");
    USAGE(t, "missing <md5>", "book-download");
}

static void t_first_problem_wins_left_to_right(ctest_t *t) {
    USAGE(t, "unknown option: --bogus", "download", "--bogus", "--name");
    USAGE(t, "option needs a value: --name", "download", "--name");
    USAGE(t, "invalid value for --path-index: x", "download", MD5, "--path-index", "x", "--bogus");
    USAGE(t, "unknown option: --bogus", "download", MD5, "--bogus", "--path-index", "x");
    USAGE(t, "option given twice: --name", "download", MD5, "--name", "a", "--name", "b", "--bogus");
    USAGE(t, "unexpected argument: b", "download", "a", "b", "--bogus");
    USAGE(t, "unknown option: --bogus", "download", "--bogus"); /* the missing md5 is reported last */
    USAGE(t, "invalid value for --timeout: z", "download", "--timeout", "z");
    USAGE(t, "filename must be <title>.<ext>, got: x", "book-download", MD5, "x", "y");
}

static void t_echoed_arguments_are_cut_and_made_printable(ctest_t *t) {
    USAGE(t, "unknown command: a?b?c", "a\nb\tc");
    USAGE(t, "unknown command: ?", "\x1f");
    USAGE(t, "unknown command: a?b", "a\x7f" "b");
    USAGE(t, "unknown command: caf\xc3\xa9", "caf\xc3\xa9"); /* bytes >= 0x80 pass through */
    static char longarg[160];
    static char want[160];
    static const char *av[3];
    memset(longarg, 'x', 150);
    longarg[150] = '\0';
    memcpy(want, "unknown command: ", 17);
    memset(want + 17, 'x', 100);
    want[117] = '\0';
    av[0] = "annas-c";
    av[1] = longarg;
    usage(t, 2, av, want);
    memset(longarg, 'y', 100); /* control: exactly 100 bytes are kept whole */
    longarg[100] = '\0';
    memcpy(want, "unknown command: ", 17);
    memset(want + 17, 'y', 100);
    want[117] = '\0';
    av[0] = "annas-c";
    av[1] = longarg;
    usage(t, 2, av, want);
    memset(longarg, 'z', 101);
    longarg[101] = '\0';
    memset(want + 17, 'z', 100);
    av[0] = "annas-c";
    av[1] = longarg;
    usage(t, 2, av, want);
    memset(longarg, 'q', 150);
    longarg[150] = '\0';
    static char want2[200];
    memcpy(want2, "filename must be <title>.<ext>, got: ", 37);
    memset(want2 + 37, 'q', 100);
    want2[137] = '\0';
    USAGE(t, want2, "book-download", MD5, longarg);
}

CTEST_SUITE("parse_err",
    {"no_command", t_no_command},
    {"unknown_command", t_unknown_command},
    {"unknown_option_in_place_of_a_command", t_unknown_option_in_place_of_a_command},
    {"help_and_version_take_nothing", t_help_and_version_take_nothing},
    {"mcp_takes_nothing", t_mcp_takes_nothing},
    {"missing_md5", t_missing_md5},
    {"unexpected_positional", t_unexpected_positional},
    {"unknown_option_in_a_command", t_unknown_option_in_a_command},
    {"options_that_belong_to_other_commands", t_options_that_belong_to_other_commands},
    {"option_without_a_value", t_option_without_a_value},
    {"option_twice", t_option_twice},
    {"bad_path_and_domain_index", t_bad_path_and_domain_index},
    {"bad_timeout", t_bad_timeout},
    {"alias_filename", t_alias_filename},
    {"alias_missing_filename", t_alias_missing_filename},
    {"first_problem_wins_left_to_right", t_first_problem_wins_left_to_right},
    {"echoed_arguments_are_cut_and_made_printable", t_echoed_arguments_are_cut_and_made_printable})
