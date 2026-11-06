/* app_dotenv_parse and the env layer (.env under the process env), tested through the ports_env_t they produce. */
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"
#include "ports/fakes.h"

static app_dotenv_t g_d;
static app_env_layer_t g_l;
static ports_fake_env_t g_pe;
static ports_fake_env_entry_t g_pent[4];

/* Parses `text` and returns the layered port over a process env holding the n entries of `proc`. */
static ports_env_t layered(ctest_t *t, const char *text, size_t len, const char *const *proc, size_t n) {
    core_str_t file = {text, len};
    for (size_t i = 0; i < n; i++) {
        g_pent[i].name = core_str_from_cstr(proc[2u * i]);
        g_pent[i].value = core_str_from_cstr(proc[2u * i + 1u]);
    }
    CHECK(t, ports_fake_env_init(&g_pe, g_pent, n));
    CHECK(t, app_dotenv_parse(file, &g_d));
    CHECK(t, app_env_layer_init(&g_l, &g_d, ports_fake_env_port(&g_pe)));
    return app_env_layer_port(&g_l);
}

static ports_env_t file_only(ctest_t *t, const char *text) { return layered(t, text, strlen(text), NULL, 0); }

static bool get(ports_env_t e, const char *name, core_str_t *v) { return e.ops->get(e.ctx, core_str_from_cstr(name), v); }

static void is(ctest_t *t, ports_env_t e, const char *name, const char *want) {
    core_str_t v = {NULL, 0};
    CHECK(t, get(e, name, &v));
    CHECK(t, ak_eq(v, want));
}

static void unset(ctest_t *t, ports_env_t e, const char *name) {
    core_str_t v = {(const char *)1, 99};
    CHECK(t, !get(e, name, &v));
    CHECK(t, v.ptr == NULL);
    CHECK_EQ_U64(t, v.len, 0);
}

static void t_basic_assignments(ctest_t *t) {
    ports_env_t e = file_only(t, "ANNAS_SECRET_KEY=abc123\nANNAS_DOWNLOAD_PATH=/books\nANNAS_BASE_URL=example.org\n");
    is(t, e, "ANNAS_SECRET_KEY", "abc123");
    is(t, e, "ANNAS_DOWNLOAD_PATH", "/books");
    is(t, e, "ANNAS_BASE_URL", "example.org");
    unset(t, e, "ANNAS_OTHER");
}

static void t_comments_blank_lines_and_whitespace(ctest_t *t) {
    ports_env_t e = file_only(t, "# a comment\n\n   \n\t\n  # indented comment A=1\n#B=2\nC=3\n");
    is(t, e, "C", "3");
    unset(t, e, "A");
    unset(t, e, "B");
    unset(t, e, "# a comment");
}

static void t_last_line_needs_no_newline(ctest_t *t) {
    ports_env_t e = file_only(t, "A=1\nB=2");
    is(t, e, "A", "1");
    is(t, e, "B", "2");
    e = file_only(t, "A=1\n"); /* control: with newline */
    is(t, e, "A", "1");
    e = file_only(t, "");
    unset(t, e, "A");
}

static void t_crlf_is_tolerated(ctest_t *t) {
    ports_env_t e = file_only(t, "A=1\r\nB=\"two\"\r\n# c\r\n\r\nC=3\r\nD=4\r");
    is(t, e, "A", "1");
    is(t, e, "B", "two");
    is(t, e, "C", "3");
    is(t, e, "D", "4");
    unset(t, e, "A\r");
}

static void t_surrounding_whitespace_is_trimmed(ctest_t *t) {
    ports_env_t e = file_only(t, "  A  =  v w  \n\tB=\t1\t\n C=x\n");
    is(t, e, "A", "v w");
    is(t, e, "B", "1");
    is(t, e, "C", "x");
    unset(t, e, "  A  ");
}

static void t_double_quotes(ctest_t *t) {
    ports_env_t e = file_only(t, "A=\"x y\"\nB=\"x # y\"\nC=\"\"\nD=\" pad \"\nE=  \"trim\"  \nF=\"a\"b\"\nG=\"=\"\n");
    is(t, e, "A", "x y");
    is(t, e, "B", "x # y");
    is(t, e, "C", "");
    is(t, e, "D", " pad ");
    is(t, e, "E", "trim");
    is(t, e, "F", "a\"b");
    is(t, e, "G", "=");
}

static void t_other_quote_shapes_are_verbatim(ctest_t *t) {
    ports_env_t e = file_only(t, "A=\"\nB=\"abc\nC=abc\"\nD='x'\nE=\"a\" # trailing\nF=x\"y\"\n");
    is(t, e, "A", "\"");
    is(t, e, "B", "\"abc");
    is(t, e, "C", "abc\"");
    is(t, e, "D", "'x'");
    is(t, e, "E", "\"a\" # trailing");
    is(t, e, "F", "x\"y\"");
}

static void t_values_are_verbatim_otherwise(ctest_t *t) {
    ports_env_t e = file_only(t, "A=b=c\nB==x\nC=x # not a comment\nD=#x\nE=$HOME\nF=a\\nb\nG=\xc3\xa9\n");
    is(t, e, "A", "b=c");
    is(t, e, "B", "=x");
    is(t, e, "C", "x # not a comment");
    is(t, e, "D", "#x");
    is(t, e, "E", "$HOME");
    is(t, e, "F", "a\\nb");
    is(t, e, "G", "\xc3\xa9");
}

static void t_empty_value_is_set(ctest_t *t) {
    ports_env_t e = file_only(t, "A=\nB=   \nC=x\n");
    is(t, e, "A", "");
    is(t, e, "B", "");
    is(t, e, "C", "x");
}

static void t_invalid_lines_are_skipped_and_valid_ones_still_count(ctest_t *t) {
    ports_env_t e = file_only(t,
        "NOEQUALS\n=value\n  =value\nA B=1\nexport C=2\na-b=3\nd.e=4\nF G\n\xc3\xa9=5\n"
        "_OK1=a\n9LIVES=b\nlower=c\nZ=z\n");
    unset(t, e, "NOEQUALS");
    unset(t, e, "");
    unset(t, e, "A B");
    unset(t, e, "A");
    unset(t, e, "C");
    unset(t, e, "export C");
    unset(t, e, "a-b");
    unset(t, e, "d.e");
    unset(t, e, "\xc3\xa9");
    is(t, e, "_OK1", "a");
    is(t, e, "9LIVES", "b");
    is(t, e, "lower", "c");
    is(t, e, "Z", "z");
}

static void t_names_are_case_sensitive(ctest_t *t) {
    ports_env_t e = file_only(t, "Key=1\nKEY=2\n");
    is(t, e, "Key", "1");
    is(t, e, "KEY", "2");
    unset(t, e, "key");
}

static void t_later_line_wins(ctest_t *t) {
    ports_env_t e = file_only(t, "A=1\nB=x\nA=2\nA=\n");
    is(t, e, "A", "");
    is(t, e, "B", "x");
    e = file_only(t, "A=1\nA=2\n");
    is(t, e, "A", "2");
}

static void t_empty_name_is_never_found(ctest_t *t) {
    ports_env_t e = file_only(t, "=x\nA=1\n");
    unset(t, e, "");
    core_str_t v = {NULL, 0};
    CHECK(t, !e.ops->get(e.ctx, (core_str_t){NULL, 0}, &v));
    CHECK(t, !get(e, "A", NULL)); /* ports rule: a NULL value pointer gives false */
    CHECK(t, !get(e, "NOPE", NULL));
}

static void t_values_point_into_the_file_bytes(ctest_t *t) {
    static const char text[] = "A=hello\nB=\"quoted\"\n";
    ports_env_t e = layered(t, text, sizeof text - 1u, NULL, 0);
    core_str_t a = {NULL, 0};
    core_str_t b = {NULL, 0};
    CHECK(t, get(e, "A", &a));
    CHECK(t, get(e, "B", &b));
    CHECK(t, a.ptr == text + 2);
    CHECK(t, b.ptr == text + 11);
    CHECK_EQ_U64(t, a.len, 5);
    CHECK_EQ_U64(t, b.len, 6);
}

static void t_process_env_wins(ctest_t *t) {
    const char *proc[] = {"A", "from-process", "P", "only-process"};
    ports_env_t e = layered(t, "A=from-file\nB=from-file\n", 24, proc, 2);
    is(t, e, "A", "from-process");
    is(t, e, "B", "from-file");
    is(t, e, "P", "only-process");
    unset(t, e, "NOPE");
}

static void t_process_env_set_but_empty_still_wins(ctest_t *t) {
    const char *proc[] = {"A", ""};
    ports_env_t e = layered(t, "A=from-file\n", 12, proc, 1);
    is(t, e, "A", "");
}

static void t_no_file_means_process_env_only(ctest_t *t) {
    const char *proc[] = {"A", "1"};
    ports_fake_env_entry_t ent[1];
    ports_fake_env_t pe;
    app_env_layer_t l;
    ent[0].name = core_str_from_cstr(proc[0]);
    ent[0].value = core_str_from_cstr(proc[1]);
    CHECK(t, ports_fake_env_init(&pe, ent, 1));
    CHECK(t, app_env_layer_init(&l, NULL, ports_fake_env_port(&pe)));
    ports_env_t e = app_env_layer_port(&l);
    is(t, e, "A", "1");
    unset(t, e, "B");
}

static void t_layer_init_refuses_bad_arguments(ctest_t *t) {
    static app_dotenv_t d;
    app_env_layer_t l;
    ports_env_t none = {NULL, NULL};
    CHECK(t, app_dotenv_parse((core_str_t){NULL, 0}, &d));
    CHECK(t, !app_env_layer_init(NULL, &d, none));
    CHECK(t, !app_env_layer_init(&l, &d, none));
    static const ports_env_ops_t noget = {NULL};
    ports_env_t bad = {&noget, NULL};
    CHECK(t, !app_env_layer_init(&l, &d, bad));
    ports_fake_env_t pe;
    CHECK(t, ports_fake_env_init(&pe, NULL, 0));
    CHECK(t, app_env_layer_init(&l, &d, ports_fake_env_port(&pe))); /* control */
}

static void t_entry_limit(ctest_t *t) {
    static char text[8192];
    static app_dotenv_t d;
    size_t n = 0;
    for (unsigned i = 0; i < 64; i++) {
        n += (size_t)snprintf(text + n, sizeof text - n, "K%u=v\n", i);
    }
    core_str_t f = {text, n};
    CHECK(t, app_dotenv_parse(f, &d));
    n += (size_t)snprintf(text + n, sizeof text - n, "# comment\n\nbad line\n"); /* skipped lines do not count */
    f.len = n;
    CHECK(t, app_dotenv_parse(f, &d));
    n -= strlen("# comment\n\nbad line\n");
    n += (size_t)snprintf(text + n, sizeof text - n, "K64=v\n");
    f.len = n;
    memset(&d, 0x55, sizeof d);
    CHECK(t, !app_dotenv_parse(f, &d)); /* the 65th accepted line */
    CHECK_EQ_U64(t, d.count, 0);
}

static void t_parse_arguments(ctest_t *t) {
    static app_dotenv_t d;
    core_str_t some = {"A=1\n", 4};
    CHECK(t, !app_dotenv_parse(some, NULL));
    CHECK(t, !app_dotenv_parse((core_str_t){NULL, 4}, &d));
    CHECK(t, app_dotenv_parse((core_str_t){NULL, 0}, &d)); /* control */
    CHECK_EQ_U64(t, d.count, 0);
    CHECK(t, app_dotenv_parse(some, &d));
    CHECK_EQ_U64(t, d.count, 1);
}

static void t_realistic_file(ctest_t *t) {
    ports_env_t e = file_only(t,
        "# annas-c configuration\r\n"
        "ANNAS_SECRET_KEY=\"FAKEKEY-not-a-real-key-0123456789\"\r\n"
        "ANNAS_DOWNLOAD_PATH=/home/me/books # not a comment\r\n"
        "\r\n"
        "ANNAS_BASE_URL=annas-archive.gl\r\n");
    is(t, e, "ANNAS_SECRET_KEY", "FAKEKEY-not-a-real-key-0123456789");
    is(t, e, "ANNAS_DOWNLOAD_PATH", "/home/me/books # not a comment");
    is(t, e, "ANNAS_BASE_URL", "annas-archive.gl");
}

CTEST_SUITE("dotenv",
    {"basic_assignments", t_basic_assignments},
    {"comments_blank_lines_and_whitespace", t_comments_blank_lines_and_whitespace},
    {"last_line_needs_no_newline", t_last_line_needs_no_newline},
    {"crlf_is_tolerated", t_crlf_is_tolerated},
    {"surrounding_whitespace_is_trimmed", t_surrounding_whitespace_is_trimmed},
    {"double_quotes", t_double_quotes},
    {"other_quote_shapes_are_verbatim", t_other_quote_shapes_are_verbatim},
    {"values_are_verbatim_otherwise", t_values_are_verbatim_otherwise},
    {"empty_value_is_set", t_empty_value_is_set},
    {"invalid_lines_are_skipped_and_valid_ones_still_count", t_invalid_lines_are_skipped_and_valid_ones_still_count},
    {"names_are_case_sensitive", t_names_are_case_sensitive},
    {"later_line_wins", t_later_line_wins},
    {"empty_name_is_never_found", t_empty_name_is_never_found},
    {"values_point_into_the_file_bytes", t_values_point_into_the_file_bytes},
    {"process_env_wins", t_process_env_wins},
    {"process_env_set_but_empty_still_wins", t_process_env_set_but_empty_still_wins},
    {"no_file_means_process_env_only", t_no_file_means_process_env_only},
    {"layer_init_refuses_bad_arguments", t_layer_init_refuses_bad_arguments},
    {"entry_limit", t_entry_limit},
    {"parse_arguments", t_parse_arguments},
    {"realistic_file", t_realistic_file})
