/* app_cli_run: every error class with its exact stderr line, exit code and port interactions; each rejection has a
 * positive control (the same shape of run that succeeds). */
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"

#define MD5 AK_MD5
#define URL "https://dl.example.org/f/1"
#define JSON_HDR "Content-Type: application/json\r\n"
#define CHALLENGE "the site requires browser verification; use download-by-md5 with a hash found in a browser"
#define MISSING "ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set"
#define INTERNAL "annas-c: internal error: invalid dependencies\n"

static void script_ok(kit_t *k, const char *extra, bool with_file) {
    kit_api(k, MD5, extra, kit_http(k, 200, "OK", JSON_HDR, kit_ok_json(k, URL, NULL)));
    if (with_file) {
        kit_get(k, "dl.example.org", 443, "/f/1", kit_http(k, 200, "OK", "", "z"));
    }
}

static void script_api(kit_t *k, int status, const char *reason, const char *hdrs, const char *body) {
    kit_api(k, MD5, "", kit_http(k, status, reason, hdrs, body));
}

static void rejected_before_io(ctest_t *t, ak_t *a, int rc, int code, const char *err) {
    ak_check_cli(t, a, rc, code, "", err);
    ak_check_no_io(t, a);
}

static void t_bad_md5(ctest_t *t) {
    static const char *const bad[] = {"zz", "", "0123456789abcdef0123456789abcde", "0123456789abcdef0123456789abcdef0",
                                      "0123456789abcdef0123456789abcdeg", " 123456789abcdef0123456789abcdef"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ak_t *a = ak_begin();
        int rc = AK_CLI(a, "download", bad[i]);
        rejected_before_io(t, a, rc, 2, "annas-c: E_ARG: md5 must be 32 hexadecimal characters\n");
        a = ak_begin();
        rc = AK_CLI(a, "download-url", bad[i]);
        rejected_before_io(t, a, rc, 2, "annas-c: E_ARG: md5 must be 32 hexadecimal characters\n");
    }
    ak_t *a = ak_begin(); /* control */
    script_ok(a->k, "", false);
    int rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 0, "Download URL: " URL "\n", "");
    ak_check_stream(t, a);
}

static void t_bad_format(ctest_t *t) {
    static const char *const bad[] = {"tar.gz", "a b", "aaaaaaaaaaaaaaaaa", "p-df", ".pdf", "pdf\n", "caf\xc3\xa9"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ak_t *a = ak_begin();
        int rc = AK_CLI(a, "download", MD5, "--format", bad[i]);
        rejected_before_io(t, a, rc, 2, "annas-c: E_ARG: format must consist of 1 to 16 ASCII letters or digits\n");
    }
    ak_t *a = ak_begin();
    int rc = AK_CLI(a, "book-download", MD5, "x.p-df"); /* the alias hands the part after the last dot on */
    ak_check_cli(t, a, rc, 2, "", "annas-c: E_ARG: format must consist of 1 to 16 ASCII letters or digits\n");
    a = ak_begin(); /* control: 16 characters, and a mixed-case one */
    script_ok(a->k, "", true);
    rc = AK_CLI(a, "download", MD5, "--format", "AAAAAAAAAAAAAAAA");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/" MD5 ".aaaaaaaaaaaaaaaa\n", "");
    ak_check_stream(t, a);
}

static void t_bad_timeout_range(ctest_t *t) {
    ak_t *a = ak_begin();
    int rc = AK_CLI(a, "download", MD5, "--timeout", "-5");
    rejected_before_io(t, a, rc, 2, "annas-c: E_ARG: timeout must be 0 or more seconds, got: -5\n");
    a = ak_begin();
    rc = AK_CLI(a, "download-url", MD5, "--timeout", "31536001");
    rejected_before_io(t, a, rc, 2, "annas-c: E_ARG: timeout must be at most 31536000 seconds, got: 31536001\n");
    a = ak_begin();
    rc = AK_CLI(a, "download", MD5, "--timeout", "-9223372036854775808");
    rejected_before_io(t, a, rc, 2,
                       "annas-c: E_ARG: timeout must be 0 or more seconds, got: -9223372036854775808\n");
    a = ak_begin(); /* control: the largest accepted value */
    script_ok(a->k, "", false);
    rc = AK_CLI(a, "download-url", MD5, "--timeout", "31536000");
    ak_check_cli(t, a, rc, 0, "Download URL: " URL "\n", "");
    ak_check_stream(t, a);
}

static void t_missing_configuration(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    int rc = AK_CLI(a, "download", MD5);
    rejected_before_io(t, a, rc, 2, "annas-c: E_CONFIG: " MISSING "\n");
    a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "");
    rc = AK_CLI(a, "download", MD5);
    rejected_before_io(t, a, rc, 2, "annas-c: E_CONFIG: " MISSING "\n");
    a = ak_begin();
    ak_env(a, "ANNAS_DOWNLOAD_PATH", NULL);
    rc = AK_CLI(a, "download", MD5);
    rejected_before_io(t, a, rc, 2, "annas-c: E_CONFIG: " MISSING "\n");
    a = ak_begin();
    ak_env(a, "ANNAS_DOWNLOAD_PATH", "");
    rc = AK_CLI(a, "book-download", MD5, "a.pdf");
    rejected_before_io(t, a, rc, 2, "annas-c: E_CONFIG: " MISSING "\n");
    a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    rc = AK_CLI(a, "download-url", MD5); /* the key is needed here too */
    rejected_before_io(t, a, rc, 2, "annas-c: E_CONFIG: " MISSING "\n");
    a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "");
    ak_dotenv(a, "ANNAS_SECRET_KEY=from-dotenv\n"); /* set-but-empty in the process env still wins */
    rc = AK_CLI(a, "download-url", MD5);
    rejected_before_io(t, a, rc, 2, "annas-c: E_CONFIG: " MISSING "\n");
}

static void t_download_path_must_be_absolute(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_DOWNLOAD_PATH", "books/here");
    int rc = AK_CLI(a, "download", MD5);
    rejected_before_io(t, a, rc, 2, "annas-c: E_CONFIG: ANNAS_DOWNLOAD_PATH must be an absolute path, got: books/here\n");
    a = ak_begin(); /* control: the same relative path is not looked at by download-url */
    ak_env(a, "ANNAS_DOWNLOAD_PATH", "books/here");
    script_ok(a->k, "", false);
    rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 0, "Download URL: " URL "\n", "");
    ak_check_stream(t, a);
}

static void t_invalid_base_url(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_BASE_URL", "bad base!");
    int rc = AK_CLI(a, "download-url", MD5);
    rejected_before_io(t, a, rc, 2, "annas-c: E_CONFIG: ANNAS_BASE_URL is not a valid domain, got: bad base!\n");
    a = ak_begin();
    ak_env(a, "ANNAS_BASE_URL", "http://exa mple.org/");
    rc = AK_CLI(a, "download", MD5);
    rejected_before_io(t, a, rc, 2, "annas-c: E_CONFIG: ANNAS_BASE_URL is not a valid domain, got: http://exa mple.org/\n");
}

static void t_timeout_is_checked_before_the_configuration(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    ak_env(a, "ANNAS_BASE_URL", "bad base!");
    int rc = AK_CLI(a, "download", MD5, "--timeout", "-1");
    rejected_before_io(t, a, rc, 2, "annas-c: E_ARG: timeout must be 0 or more seconds, got: -1\n");
    a = ak_begin(); /* then the base, then the missing key */
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    ak_env(a, "ANNAS_BASE_URL", "bad base!");
    rc = AK_CLI(a, "download", MD5);
    rejected_before_io(t, a, rc, 2, "annas-c: E_CONFIG: ANNAS_BASE_URL is not a valid domain, got: bad base!\n");
}

static void t_api_errors(ctest_t *t) {
    ak_t *a = ak_begin();
    script_api(a->k, 401, "Unauthorized", JSON_HDR, kit_fixture_401);
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 4, "", "annas-c: E_API: 401: Invalid secret key\n");
    ak_check_stream(t, a);
    kit_check_fs_untouched(t, a->k);
    a = ak_begin();
    script_api(a->k, 404, "Not Found", JSON_HDR, kit_fixture_404);
    rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 4, "", "annas-c: E_API: 404: Record not found\n");
    ak_check_stream(t, a);
    a = ak_begin();
    script_api(a->k, 500, "Internal Server Error", "", "oops");
    rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 4, "", "annas-c: E_API: 500: oops\n");
    ak_check_stream(t, a);
    a = ak_begin(); /* control: 200 with a URL succeeds */
    script_ok(a->k, "", true);
    rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/" MD5 ".bin\n", "");
    ak_check_stream(t, a);
}

static void t_api_200_without_a_url(ctest_t *t) {
    ak_t *a = ak_begin();
    script_api(a->k, 200, "OK", JSON_HDR, "{\"download_url\":null,\"error\":\"No fast downloads left\"}");
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 4, "", "annas-c: E_API: 200: No fast downloads left\n");
    ak_check_stream(t, a);
}

static void t_browser_check(ctest_t *t) {
    ak_t *a = ak_begin();
    script_api(a->k, 302, "FOUND", "Location: /search?q=lakos&content=book_any&check=1\r\n", kit_fixture_302);
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 3, "", "annas-c: E_BROWSER_CHECK: " CHALLENGE "\n");
    ak_check_stream(t, a);
    a = ak_begin();
    script_api(a->k, 403, "Forbidden", "", "<html>DDoS-Guard</html>");
    rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 3, "", "annas-c: E_BROWSER_CHECK: " CHALLENGE "\n");
    ak_check_stream(t, a);
}

static void t_http_error_from_the_download_host(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 500, "Internal Server Error", "", "oops"));
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 4, "", "annas-c: E_HTTP: unexpected HTTP status 500\n");
    ak_check_stream(t, a);
    kit_check_fs_untouched(t, a->k);
}

static void t_network_errors(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_connect_err(a->k, "annas-archive.gl", 443, PORTS_E_NET);
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 5, "", "annas-c: E_NET: connection failed\n");
    ak_check_stream(t, a);
    a = ak_begin();
    kit_connect_err(a->k, "annas-archive.gl", 443, PORTS_E_TLS);
    rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 5, "", "annas-c: E_NET: TLS error\n");
    ak_check_stream(t, a);
    a = ak_begin();
    kit_connect(a->k, "annas-archive.gl", 443);
    kit_expect_api(a->k, MD5, "");
    kit_respond(a->k, "HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\n{\"download_url\"", 0);
    kit_eof(a->k);
    rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 5, "", "annas-c: E_NET: connection closed before the response was complete\n");
    ak_check_stream(t, a);
}

static void t_parse_error_for_a_non_https_download_url(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, "{\"download_url\":\"http://insecure.example.org/x\"}"));
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 7, "", "annas-c: E_PARSE: download URL must be an absolute https URL\n");
    ak_check_stream(t, a);
    kit_check_fs_untouched(t, a->k);
    a = ak_begin(); /* control: download-url does not look at the scheme */
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, "{\"download_url\":\"http://insecure.example.org/x\"}"));
    rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 0, "Download URL: http://insecure.example.org/x\n", "");
    ak_check_stream(t, a);
}

static void t_file_exists(ctest_t *t) {
    ak_t *a = ak_begin();
    script_ok(a->k, "", true);
    CHECK(t, kit_preload(a->k, MD5 ".bin", "old"));
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 6, "", "annas-c: E_FS: file already exists: " MD5 ".bin\n");
    ak_check_stream(t, a);
    kit_check_no_partial(t, a->k, 1);
    core_str_t old = {NULL, 0};
    CHECK(t, ports_fake_fs_find(&a->k->fs, core_str_from_cstr(MD5 ".bin"), &old));
    CHECK(t, ak_eq(old, "old"));
    a = ak_begin(); /* control: another name does not collide */
    script_ok(a->k, "", true);
    CHECK(t, kit_preload(a->k, MD5 ".bin", "old"));
    rc = AK_CLI(a, "download", MD5, "--format", "pdf");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/" MD5 ".pdf\n", "");
    ak_check_stream(t, a);
}

static void t_fs_failures_leave_no_partial_file(ctest_t *t) {
    ak_t *a = ak_begin();
    script_ok(a->k, "", true);
    (void)ak_deps(a);
    CHECK(t, ports_fake_fs_inject(&a->k->fs, PORTS_FAKE_FS_OP_CREATE, 1, PORTS_FS_E_IO));
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 6, "", "annas-c: E_FS: could not create a temporary file\n");
    ak_check_stream(t, a);
    kit_check_fs_untouched(t, a->k);
    a = ak_begin();
    script_ok(a->k, "", true);
    (void)ak_deps(a);
    CHECK(t, ports_fake_fs_inject(&a->k->fs, PORTS_FAKE_FS_OP_WRITE, 1, PORTS_FS_E_NOSPACE));
    rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 6, "", "annas-c: E_FS: could not write the file\n");
    ak_check_stream(t, a);
    kit_check_no_partial(t, a->k, 1);
    a = ak_begin();
    script_ok(a->k, "", true);
    (void)ak_deps(a);
    CHECK(t, ports_fake_fs_inject(&a->k->fs, PORTS_FAKE_FS_OP_COMMIT, 1, PORTS_FS_E_IO));
    rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 6, "", "annas-c: E_FS: could not save the file\n");
    ak_check_stream(t, a);
    kit_check_no_partial(t, a->k, 1);
}

/* ---- internal errors -------------------------------------------------------------------------------------- */

static app_cmd_t cmd_of(app_cmd_kind_t kind) {
    app_cmd_t c;
    memset(&c, 0, sizeof c);
    c.kind = kind;
    c.md5 = core_str_from_cstr(MD5);
    c.timeout_seconds = 3600;
    return c;
}

static void t_unusable_commands(ctest_t *t) {
    static const app_cmd_kind_t kinds[] = {APP_CMD_MCP, APP_CMD_NONE, (app_cmd_kind_t)77};
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        ak_t *a = ak_begin();
        app_cmd_t c = cmd_of(kinds[i]);
        int rc = app_cli_run(&c, ak_deps(a), &a->out, &a->err);
        ak_check_cli(t, a, rc, 1, "", INTERNAL);
        ak_check_no_io(t, a);
    }
    ak_t *a = ak_begin(); /* control: a download kind runs */
    script_ok(a->k, "", false);
    app_cmd_t c = cmd_of(APP_CMD_DOWNLOAD_URL);
    int rc = app_cli_run(&c, ak_deps(a), &a->out, &a->err);
    ak_check_cli(t, a, rc, 0, "Download URL: " URL "\n", "");
}

static void t_null_arguments(ctest_t *t) {
    ak_t *a = ak_begin();
    app_cmd_t c = cmd_of(APP_CMD_DOWNLOAD);
    int rc = app_cli_run(NULL, ak_deps(a), &a->out, &a->err);
    ak_check_cli(t, a, rc, 1, "", INTERNAL);
    rc = app_cli_run(&c, NULL, &a->out, &a->err);
    CHECK_EQ_I64(t, rc, 1);
    CHECK_EQ_STR(t, ak_out(a), "");
    CHECK_EQ_STR(t, ak_err(a), INTERNAL);
    ak_clear(a);
    rc = app_cli_run(&c, ak_deps(a), NULL, &a->err);
    CHECK_EQ_I64(t, rc, 1);
    CHECK_EQ_STR(t, ak_err(a), "");
    rc = app_cli_run(&c, ak_deps(a), &a->out, NULL);
    CHECK_EQ_I64(t, rc, 1);
    CHECK_EQ_STR(t, ak_out(a), "");
    ak_check_no_io(t, a);
}

static void t_refused_dependencies(ctest_t *t) {
    ak_t *a = ak_begin();
    app_deps_t d = *ak_deps(a);
    app_cmd_t c = cmd_of(APP_CMD_DOWNLOAD);
    d.workspace_cap = ANNAS_WORKSPACE_MIN - 1u;
    int rc = app_cli_run(&c, &d, &a->out, &a->err);
    ak_check_cli(t, a, rc, 1, "", INTERNAL);
    ak_clear(a);
    d = *ak_deps(a);
    d.workspace = NULL;
    rc = app_cli_run(&c, &d, &a->out, &a->err);
    ak_check_cli(t, a, rc, 1, "", INTERNAL);
    ak_clear(a);
    d = *ak_deps(a);
    d.stream.ops = NULL;
    rc = app_cli_run(&c, &d, &a->out, &a->err);
    ak_check_cli(t, a, rc, 1, "", INTERNAL);
    ak_clear(a);
    d = *ak_deps(a);
    d.fs.ops = NULL;
    rc = app_cli_run(&c, &d, &a->out, &a->err);
    ak_check_cli(t, a, rc, 1, "", INTERNAL);
    ak_check_no_io(t, a);
    ak_clear(a); /* config errors come first */
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    a->deps_ready = false;
    d = *ak_deps(a);
    d.workspace_cap = 10;
    rc = app_cli_run(&c, &d, &a->out, &a->err);
    ak_check_cli(t, a, rc, 2, "", "annas-c: E_CONFIG: " MISSING "\n");
}

CTEST_SUITE("cli_err",
    {"bad_md5", t_bad_md5},
    {"bad_format", t_bad_format},
    {"bad_timeout_range", t_bad_timeout_range},
    {"missing_configuration", t_missing_configuration},
    {"download_path_must_be_absolute", t_download_path_must_be_absolute},
    {"invalid_base_url", t_invalid_base_url},
    {"timeout_is_checked_before_the_configuration", t_timeout_is_checked_before_the_configuration},
    {"api_errors", t_api_errors},
    {"api_200_without_a_url", t_api_200_without_a_url},
    {"browser_check", t_browser_check},
    {"http_error_from_the_download_host", t_http_error_from_the_download_host},
    {"network_errors", t_network_errors},
    {"parse_error_for_a_non_https_download_url", t_parse_error_for_a_non_https_download_url},
    {"file_exists", t_file_exists},
    {"fs_failures_leave_no_partial_file", t_fs_failures_leave_no_partial_file},
    {"unusable_commands", t_unusable_commands},
    {"null_arguments", t_null_arguments},
    {"refused_dependencies", t_refused_dependencies})
