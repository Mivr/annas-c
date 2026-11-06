/* app_cli_parse: every accepted command line, field by field. The rejections are in parse_err_test.c; each
 * accepted shape here has a rejected near miss there. */
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"

#define MD5 "0123456789abcdef0123456789abcdef"

static app_cmd_t blank(void) {
    app_cmd_t c;
    memset(&c, 0xA5, sizeof c);
    return c;
}

static bool parse(int argc, const char *const *argv, app_cmd_t *c) {
    static app_usage_error_t e;
    return app_cli_parse(argc, argv, c, &e);
}

static void t_download_minimal(ctest_t *t) {
    const char *argv[] = {"annas-c", "download", MD5};
    app_cmd_t c = blank();
    CHECK(t, parse(3, argv, &c));
    CHECK_EQ_I64(t, c.kind, APP_CMD_DOWNLOAD);
    CHECK(t, c.md5.ptr == argv[2]);
    CHECK_EQ_U64(t, c.md5.len, 32);
    CHECK_EQ_U64(t, c.title.len, 0);
    CHECK_EQ_U64(t, c.format.len, 0);
    CHECK(t, !c.has_path_index);
    CHECK(t, !c.has_domain_index);
    CHECK_EQ_I64(t, c.timeout_seconds, 3600);
}

static void t_download_all_options(ctest_t *t) {
    const char *argv[] = {"annas-c", "download", MD5,    "--name",         "My Book", "--format", "PDF",
                          "--path-index", "3",   "--domain-index", "12",   "--timeout", "90"};
    app_cmd_t c = blank();
    CHECK(t, parse(13, argv, &c));
    CHECK_EQ_I64(t, c.kind, APP_CMD_DOWNLOAD);
    CHECK(t, ak_eq(c.md5, MD5));
    CHECK(t, c.title.ptr == argv[4]);
    CHECK(t, ak_eq(c.title, "My Book"));
    CHECK(t, c.format.ptr == argv[6]);
    CHECK(t, ak_eq(c.format, "PDF")); /* annas lower-cases */
    CHECK(t, c.has_path_index);
    CHECK_EQ_U64(t, c.path_index, 3);
    CHECK(t, c.has_domain_index);
    CHECK_EQ_U64(t, c.domain_index, 12);
    CHECK_EQ_I64(t, c.timeout_seconds, 90);
}

static void t_options_in_any_order(ctest_t *t) {
    const char *a[] = {"annas-c", "download", "--timeout", "5", "--name", "T", MD5, "--domain-index", "1"};
    const char *b[] = {"annas-c", "download", "--domain-index", "1", MD5, "--timeout", "5", "--name", "T"};
    app_cmd_t x = blank();
    app_cmd_t y = blank();
    CHECK(t, parse(9, a, &x));
    CHECK(t, parse(9, b, &y));
    for (int i = 0; i < 2; i++) {
        const app_cmd_t *c = i == 0 ? &x : &y;
        CHECK_EQ_I64(t, c->kind, APP_CMD_DOWNLOAD);
        CHECK(t, ak_eq(c->md5, MD5));
        CHECK(t, ak_eq(c->title, "T"));
        CHECK(t, c->has_domain_index);
        CHECK_EQ_U64(t, c->domain_index, 1);
        CHECK(t, !c->has_path_index);
        CHECK_EQ_I64(t, c->timeout_seconds, 5);
    }
}

static void t_option_values_are_taken_whatever_they_look_like(ctest_t *t) {
    const char *argv[] = {"annas-c", "download", MD5, "--name", "--format", "--format", "-", "--timeout", "-5"};
    app_cmd_t c = blank();
    CHECK(t, parse(9, argv, &c));
    CHECK(t, ak_eq(c.title, "--format"));
    CHECK(t, ak_eq(c.format, "-"));
    CHECK_EQ_I64(t, c.timeout_seconds, -5); /* the range is annas's check */
}

static void t_the_lone_dash_is_a_positional(ctest_t *t) {
    const char *argv[] = {"annas-c", "download", "-"};
    app_cmd_t c = blank();
    CHECK(t, parse(3, argv, &c)); /* md5 is not validated here */
    CHECK(t, ak_eq(c.md5, "-"));
}

static void t_md5_is_not_validated_or_normalised_by_parse(ctest_t *t) {
    const char *argv[] = {"annas-c", "download", "ZZ"};
    app_cmd_t c = blank();
    CHECK(t, parse(3, argv, &c));
    CHECK(t, ak_eq(c.md5, "ZZ"));
    const char *up[] = {"annas-c", "download", "0123456789ABCDEF0123456789ABCDEF"};
    CHECK(t, parse(3, up, &c));
    CHECK(t, ak_eq(c.md5, "0123456789ABCDEF0123456789ABCDEF"));
}

static void t_empty_name_and_format_mean_absent(ctest_t *t) {
    const char *argv[] = {"annas-c", "download", MD5, "--name", "", "--format", ""};
    app_cmd_t c = blank();
    CHECK(t, parse(7, argv, &c));
    CHECK_EQ_U64(t, c.title.len, 0);
    CHECK_EQ_U64(t, c.format.len, 0);
}

static void t_number_edges(ctest_t *t) {
    const char *a[] = {"annas-c", "download", MD5, "--path-index", "0", "--domain-index", "007", "--timeout", "0"};
    app_cmd_t c = blank();
    CHECK(t, parse(9, a, &c));
    CHECK(t, c.has_path_index);
    CHECK_EQ_U64(t, c.path_index, 0);
    CHECK_EQ_U64(t, c.domain_index, 7);
    CHECK_EQ_I64(t, c.timeout_seconds, 0);
    const char *b[] = {"annas-c", "download", MD5, "--path-index", "18446744073709551615", "--domain-index",
                       "18446744073709551614", "--timeout", "9223372036854775807"};
    CHECK(t, parse(9, b, &c));
    CHECK_EQ_U64(t, c.path_index, UINT64_MAX);
    CHECK_EQ_U64(t, c.domain_index, UINT64_MAX - 1u);
    CHECK_EQ_I64(t, c.timeout_seconds, INT64_MAX);
    const char *d[] = {"annas-c", "download", MD5, "--timeout", "-9223372036854775808"};
    CHECK(t, parse(5, d, &c));
    CHECK_EQ_I64(t, c.timeout_seconds, INT64_MIN);
    const char *e[] = {"annas-c", "download", MD5, "--timeout", "31536001"};
    CHECK(t, parse(5, e, &c)); /* out of annas's range: still a valid command line */
    CHECK_EQ_I64(t, c.timeout_seconds, 31536001);
    const char *f[] = {"annas-c", "download", MD5, "--timeout", "-0"};
    CHECK(t, parse(5, f, &c));
    CHECK_EQ_I64(t, c.timeout_seconds, 0);
}

static void t_alias_book_download(ctest_t *t) {
    const char *argv[] = {"annas-c", "book-download", MD5, "My Book.pdf"};
    app_cmd_t c = blank();
    CHECK(t, parse(4, argv, &c));
    CHECK_EQ_I64(t, c.kind, APP_CMD_DOWNLOAD);
    CHECK(t, c.md5.ptr == argv[2]);
    CHECK(t, ak_eq(c.title, "My Book"));
    CHECK(t, ak_eq(c.format, "pdf"));
    CHECK(t, c.title.ptr == argv[3]);
    CHECK(t, c.format.ptr == argv[3] + 8);
    CHECK(t, !c.has_path_index);
    CHECK(t, !c.has_domain_index);
    CHECK_EQ_I64(t, c.timeout_seconds, 3600);
}

static void t_alias_splits_at_the_last_dot(ctest_t *t) {
    const char *argv[] = {"annas-c", "book-download", MD5, "a.b.c.epub"};
    app_cmd_t c = blank();
    CHECK(t, parse(4, argv, &c));
    CHECK(t, ak_eq(c.title, "a.b.c"));
    CHECK(t, ak_eq(c.format, "epub"));
    const char *two[] = {"annas-c", "book-download", MD5, "x.y"}; /* shortest valid form */
    CHECK(t, parse(4, two, &c));
    CHECK(t, ak_eq(c.title, "x"));
    CHECK(t, ak_eq(c.format, "y"));
    const char *sp[] = {"annas-c", "book-download", MD5, " .pdf"}; /* a title of one space is still a title */
    CHECK(t, parse(4, sp, &c));
    CHECK(t, ak_eq(c.title, " "));
    CHECK(t, ak_eq(c.format, "pdf"));
}

static void t_alias_does_not_validate_the_format(ctest_t *t) {
    const char *argv[] = {"annas-c", "book-download", MD5, "x.not a format!"};
    app_cmd_t c = blank();
    CHECK(t, parse(4, argv, &c));
    CHECK(t, ak_eq(c.format, "not a format!"));
}

static void t_download_url(ctest_t *t) {
    const char *argv[] = {"annas-c", "download-url", MD5, "--path-index", "1", "--domain-index", "2", "--timeout", "7"};
    app_cmd_t c = blank();
    CHECK(t, parse(9, argv, &c));
    CHECK_EQ_I64(t, c.kind, APP_CMD_DOWNLOAD_URL);
    CHECK(t, ak_eq(c.md5, MD5));
    CHECK_EQ_U64(t, c.title.len, 0);
    CHECK_EQ_U64(t, c.format.len, 0);
    CHECK(t, c.has_path_index);
    CHECK_EQ_U64(t, c.path_index, 1);
    CHECK(t, c.has_domain_index);
    CHECK_EQ_U64(t, c.domain_index, 2);
    CHECK_EQ_I64(t, c.timeout_seconds, 7);
    const char *min[] = {"annas-c", "download-url", MD5};
    CHECK(t, parse(3, min, &c));
    CHECK_EQ_I64(t, c.kind, APP_CMD_DOWNLOAD_URL);
    CHECK(t, !c.has_path_index);
    CHECK(t, !c.has_domain_index);
    CHECK_EQ_I64(t, c.timeout_seconds, 3600);
}

static void t_mcp_help_version(ctest_t *t) {
    app_cmd_t c = blank();
    const char *mcp[] = {"annas-c", "mcp"};
    CHECK(t, parse(2, mcp, &c));
    CHECK_EQ_I64(t, c.kind, APP_CMD_MCP);
    const char *h1[] = {"annas-c", "--help"};
    CHECK(t, parse(2, h1, &c));
    CHECK_EQ_I64(t, c.kind, APP_CMD_HELP);
    const char *h2[] = {"annas-c", "-h"};
    CHECK(t, parse(2, h2, &c));
    CHECK_EQ_I64(t, c.kind, APP_CMD_HELP);
    const char *v[] = {"annas-c", "--version"};
    CHECK(t, parse(2, v, &c));
    CHECK_EQ_I64(t, c.kind, APP_CMD_VERSION);
}

static void t_program_name_is_ignored(ctest_t *t) {
    const char *argv[] = {"", "download", MD5};
    app_cmd_t c = blank();
    CHECK(t, parse(3, argv, &c));
    CHECK_EQ_I64(t, c.kind, APP_CMD_DOWNLOAD);
    const char *argv2[] = {"download", "download", MD5}; /* argv[0] that looks like a command */
    CHECK(t, parse(3, argv2, &c));
    CHECK(t, ak_eq(c.md5, MD5));
}

static void t_every_field_is_rewritten_on_success(ctest_t *t) {
    const char *full[] = {"annas-c", "download", MD5, "--name", "N", "--format", "f", "--path-index", "1",
                          "--domain-index", "2", "--timeout", "9"};
    const char *min[] = {"annas-c", "download-url", MD5};
    app_cmd_t c = blank();
    CHECK(t, parse(13, full, &c));
    CHECK(t, parse(3, min, &c)); /* a reused cmd keeps nothing of the previous command */
    CHECK_EQ_I64(t, c.kind, APP_CMD_DOWNLOAD_URL);
    CHECK_EQ_U64(t, c.title.len, 0);
    CHECK_EQ_U64(t, c.format.len, 0);
    CHECK(t, !c.has_path_index);
    CHECK(t, !c.has_domain_index);
    CHECK_EQ_I64(t, c.timeout_seconds, 3600);
}

CTEST_SUITE("parse",
    {"download_minimal", t_download_minimal},
    {"download_all_options", t_download_all_options},
    {"options_in_any_order", t_options_in_any_order},
    {"option_values_are_taken_whatever_they_look_like", t_option_values_are_taken_whatever_they_look_like},
    {"the_lone_dash_is_a_positional", t_the_lone_dash_is_a_positional},
    {"md5_is_not_validated_or_normalised_by_parse", t_md5_is_not_validated_or_normalised_by_parse},
    {"empty_name_and_format_mean_absent", t_empty_name_and_format_mean_absent},
    {"number_edges", t_number_edges},
    {"alias_book_download", t_alias_book_download},
    {"alias_splits_at_the_last_dot", t_alias_splits_at_the_last_dot},
    {"alias_does_not_validate_the_format", t_alias_does_not_validate_the_format},
    {"download_url", t_download_url},
    {"mcp_help_version", t_mcp_help_version},
    {"program_name_is_ignored", t_program_name_is_ignored},
    {"every_field_is_rewritten_on_success", t_every_field_is_rewritten_on_success})
