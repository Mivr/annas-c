/* tools/call: successful tool runs and operation errors (isError results), exact bytes. */
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"

#define MD5 AK_MD5
#define URL "https://dl.example.org/f/1"
#define ACCT "{\"downloads_left\":24,\"downloads_per_day\":25}"
#define ACCT_ESC "{\\\"downloads_left\\\":24,\\\"downloads_per_day\\\":25}"
#define NL "\\u000a"
#define JSON_HDR "Content-Type: application/json\r\n"
#define CHALLENGE "the site requires browser verification; use download-by-md5 with a hash found in a browser"
#define HASH "\"hash\":\"" MD5 "\""
#define BOOK "book_download"
#define FAST "fast_download_url"
#define MISSING "ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set"

static char g_req[4096];

static app_mcp_result_t call(ak_t *a, const char *tool, const char *args) {
    (void)snprintf(g_req, sizeof g_req,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}",
                   tool, args);
    return ak_mcp(a, g_req);
}

static void script(kit_t *k, const char *extra, const char *acct, const char *body) {
    kit_api(k, MD5, extra, kit_http(k, 200, "OK", JSON_HDR, kit_ok_json(k, URL, acct)));
    if (body != NULL) {
        kit_get(k, "dl.example.org", 443, "/f/1", kit_http(k, 200, "OK", "", body));
    }
}

#define RESULT_OK(text) AK_OKR("1", AK_TOOL_OK(text))
#define RESULT_ERR(text) AK_OKR("1", AK_TOOL_ERR(text))

static void t_book_download(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, "", ACCT, "FILEBYTES");
    ak_check_reply(t, a, call(a, BOOK, "{" HASH "}"),
                   RESULT_OK("Book downloaded successfully to: /dl/" MD5 ".bin" NL "Account: " ACCT_ESC));
    ak_check_stream(t, a);
    kit_check_file(t, a->k, MD5 ".bin", "FILEBYTES", 9);
}

static void t_book_download_without_account(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, "", NULL, "x");
    ak_check_reply(t, a, call(a, BOOK, "{" HASH "}"), RESULT_OK("Book downloaded successfully to: /dl/" MD5 ".bin"));
    ak_check_stream(t, a);
}

static void t_book_download_all_arguments(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, "&path_index=2&domain_index=1", ACCT, "abc");
    ak_check_reply(t, a,
                   call(a, BOOK,
                        "{\"timeout_seconds\":120,\"domain_index\":1,\"format\":\"EPUB\",\"title\":\"My Book\",\"path_index\":2,"
                        HASH "}"),
                   RESULT_OK("Book downloaded successfully to: /dl/My Book.epub" NL "Account: " ACCT_ESC));
    ak_check_stream(t, a);
    kit_check_file(t, a->k, "My Book.epub", "abc", 3);
}

static void t_book_download_empty_title_and_format_mean_absent(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, "", NULL, "x");
    ak_check_reply(t, a, call(a, BOOK, "{" HASH ",\"title\":\"\",\"format\":\"\"}"),
                   RESULT_OK("Book downloaded successfully to: /dl/" MD5 ".bin"));
    ak_check_stream(t, a);
}

static void t_book_download_uppercase_hash(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, "", NULL, "x");
    ak_check_reply(t, a, call(a, BOOK, "{\"hash\":\"0123456789ABCDEF0123456789ABCDEF\"}"),
                   RESULT_OK("Book downloaded successfully to: /dl/" MD5 ".bin"));
    ak_check_stream(t, a);
}

static void t_unicode_title_is_emitted_as_utf8(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, "", NULL, "x");
    ak_check_reply(t, a, call(a, BOOK, "{" HASH ",\"title\":\"Caf\\u00e9 \\u4e2d \\\"q\\\" a\\\\b\",\"format\":\"pdf\"}"),
                   RESULT_OK("Book downloaded successfully to: /dl/Caf\xc3\xa9 \xe4\xb8\xad _q_ a_b.pdf"));
    ak_check_stream(t, a);
    kit_check_file(t, a->k, "Caf\xc3\xa9 \xe4\xb8\xad _q_ a_b.pdf", "x", 1);
}

static void t_fast_download_url(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, "", ACCT, NULL);
    ak_check_reply(t, a, call(a, FAST, "{" HASH "}"), RESULT_OK("Download URL: " URL NL "Account: " ACCT_ESC));
    ak_check_stream(t, a);
    kit_check_fs_untouched(t, a->k);
}

static void t_fast_download_url_all_arguments_and_no_account(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, "&path_index=0&domain_index=4", NULL, NULL);
    ak_check_reply(t, a, call(a, FAST, "{" HASH ",\"path_index\":0,\"domain_index\":4,\"timeout_seconds\":9}"),
                   RESULT_OK("Download URL: " URL));
    ak_check_stream(t, a);
}

static void t_fast_download_url_needs_no_download_directory(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_DOWNLOAD_PATH", NULL);
    script(a->k, "", NULL, NULL);
    ak_check_reply(t, a, call(a, FAST, "{" HASH "}"), RESULT_OK("Download URL: " URL));
    ak_check_stream(t, a);
    a = ak_begin(); /* control: book_download does */
    ak_env(a, "ANNAS_DOWNLOAD_PATH", NULL);
    ak_check_reply(t, a, call(a, BOOK, "{" HASH "}"), RESULT_ERR("annas-c: E_CONFIG: " MISSING));
    ak_check_no_io(t, a);
}

static void t_the_id_is_echoed_in_tool_results(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, "", NULL, NULL);
    (void)snprintf(g_req, sizeof g_req,
                   "{\"jsonrpc\":\"2.0\",\"id\":\"call-7\",\"method\":\"tools/call\",\"params\":{\"name\":\"fast_download_url\",\"arguments\":{" HASH "}}}");
    ak_check_reply(t, a, ak_mcp(a, g_req), AK_OKR("\"call-7\"", AK_TOOL_OK("Download URL: " URL)));
    ak_check_stream(t, a);
}

static void t_the_base_url_comes_from_the_environment(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_BASE_URL", "http://mirror.example.org/");
    kit_set_base(a->k, "mirror.example.org");
    script(a->k, "", NULL, NULL);
    ak_check_reply(t, a, call(a, FAST, "{" HASH "}"), RESULT_OK("Download URL: " URL));
    ak_check_stream(t, a);
}

static void t_two_calls_in_a_row(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, "", ACCT, "one");
    kit_api(a->k, MD5, "", kit_http(a->k, 401, "Unauthorized", JSON_HDR, kit_fixture_401));
    script(a->k, "", NULL, NULL);
    ak_check_reply(t, a, call(a, BOOK, "{" HASH ",\"title\":\"a\",\"format\":\"txt\"}"),
                   RESULT_OK("Book downloaded successfully to: /dl/a.txt" NL "Account: " ACCT_ESC));
    ak_check_reply(t, a, call(a, FAST, "{" HASH "}"), RESULT_ERR("annas-c: E_API: 401: Invalid secret key"));
    ak_check_reply(t, a, call(a, FAST, "{" HASH "}"), RESULT_OK("Download URL: " URL)); /* the failure left no state */
    ak_check_stream(t, a);
}

/* ---- operation errors are results with isError true ---------------------------------------------------------- */

static void err_no_io(ctest_t *t, ak_t *a, const char *args, const char *tool, const char *want) {
    ak_check_reply(t, a, call(a, tool, args), want);
    ak_check_no_io(t, a);
}

static void t_bad_hash_is_an_operation_error(ctest_t *t) {
    static const char *const bad[] = {"", "zz", "0123456789abcdef0123456789abcde", "0123456789abcdef0123456789abcdeg"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char args[120];
        (void)snprintf(args, sizeof args, "{\"hash\":\"%s\"}", bad[i]);
        err_no_io(t, ak_begin(), args, BOOK, RESULT_ERR("annas-c: E_ARG: md5 must be 32 hexadecimal characters"));
        err_no_io(t, ak_begin(), args, FAST, RESULT_ERR("annas-c: E_ARG: md5 must be 32 hexadecimal characters"));
    }
}

static void t_bad_format_is_an_operation_error(ctest_t *t) {
    static const char *const bad[] = {"tar.gz", "a b", "aaaaaaaaaaaaaaaaa", "caf\\u00e9"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char args[160];
        (void)snprintf(args, sizeof args, "{" HASH ",\"format\":\"%s\"}", bad[i]);
        err_no_io(t, ak_begin(), args, BOOK,
                  RESULT_ERR("annas-c: E_ARG: format must consist of 1 to 16 ASCII letters or digits"));
    }
}

static void t_timeout_range_is_an_operation_error(ctest_t *t) {
    err_no_io(t, ak_begin(), "{" HASH ",\"timeout_seconds\":-5}", BOOK,
              RESULT_ERR("annas-c: E_ARG: timeout must be 0 or more seconds, got: -5"));
    err_no_io(t, ak_begin(), "{" HASH ",\"timeout_seconds\":-9223372036854775808}", FAST,
              RESULT_ERR("annas-c: E_ARG: timeout must be 0 or more seconds, got: -9223372036854775808"));
    err_no_io(t, ak_begin(), "{" HASH ",\"timeout_seconds\":31536001}", FAST,
              RESULT_ERR("annas-c: E_ARG: timeout must be at most 31536000 seconds, got: 31536001"));
}

static void t_missing_and_invalid_configuration(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    err_no_io(t, a, "{" HASH "}", FAST, RESULT_ERR("annas-c: E_CONFIG: " MISSING));
    a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", "");
    err_no_io(t, a, "{" HASH "}", BOOK, RESULT_ERR("annas-c: E_CONFIG: " MISSING));
    a = ak_begin();
    ak_env(a, "ANNAS_DOWNLOAD_PATH", "relative/dir");
    err_no_io(t, a, "{" HASH "}", BOOK,
              RESULT_ERR("annas-c: E_CONFIG: ANNAS_DOWNLOAD_PATH must be an absolute path, got: relative/dir"));
    a = ak_begin();
    ak_env(a, "ANNAS_BASE_URL", "bad base!");
    err_no_io(t, a, "{" HASH "}", FAST,
              RESULT_ERR("annas-c: E_CONFIG: ANNAS_BASE_URL is not a valid domain, got: bad base!"));
}

static void t_api_errors(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 401, "Unauthorized", JSON_HDR, kit_fixture_401));
    ak_check_reply(t, a, call(a, BOOK, "{" HASH "}"), RESULT_ERR("annas-c: E_API: 401: Invalid secret key"));
    ak_check_stream(t, a);
    kit_check_fs_untouched(t, a->k);
    a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 404, "Not Found", JSON_HDR, kit_fixture_404));
    ak_check_reply(t, a, call(a, FAST, "{" HASH "}"), RESULT_ERR("annas-c: E_API: 404: Record not found"));
    ak_check_stream(t, a);
    a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 500, "Server Error", "", "He said \"no\"\nand left"));
    ak_check_reply(t, a, call(a, FAST, "{" HASH "}"),
                   RESULT_ERR("annas-c: E_API: 500: He said \\\"no\\\"?and left")); /* the message is one line */
    ak_check_stream(t, a);
}

static void t_browser_check(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 302, "FOUND", "Location: /search?q=x&check=1\r\n", ""));
    ak_check_reply(t, a, call(a, FAST, "{" HASH "}"), RESULT_ERR("annas-c: E_BROWSER_CHECK: " CHALLENGE));
    ak_check_stream(t, a);
}

static void t_download_host_and_network_errors(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, kit_ok_json(a->k, URL, NULL)));
    kit_get(a->k, "dl.example.org", 443, "/f/1", kit_http(a->k, 503, "Unavailable", "", "later"));
    ak_check_reply(t, a, call(a, BOOK, "{" HASH "}"), RESULT_ERR("annas-c: E_HTTP: unexpected HTTP status 503"));
    ak_check_stream(t, a);
    kit_check_fs_untouched(t, a->k);
    a = ak_begin();
    kit_connect_err(a->k, "annas-archive.gl", 443, PORTS_E_NET);
    ak_check_reply(t, a, call(a, FAST, "{" HASH "}"), RESULT_ERR("annas-c: E_NET: connection failed"));
    ak_check_stream(t, a);
    a = ak_begin();
    kit_connect_err(a->k, "annas-archive.gl", 443, PORTS_E_TLS);
    ak_check_reply(t, a, call(a, BOOK, "{" HASH "}"), RESULT_ERR("annas-c: E_NET: TLS error"));
    ak_check_stream(t, a);
}

static void t_timeout(ctest_t *t) {
    ak_t *a = ak_begin();
    a->k->read_advance = 1000;
    a->k->allow_unused = true;
    script(a->k, "", NULL, "x");
    ak_check_reply(t, a, call(a, BOOK, "{" HASH ",\"timeout_seconds\":1}"), RESULT_ERR("annas-c: E_TIMEOUT: operation timed out"));
    ak_check_stream(t, a);
    kit_check_fs_untouched(t, a->k);
    a = ak_begin(); /* control: 5 s is enough for the same script */
    a->k->read_advance = 1000;
    script(a->k, "", NULL, "x");
    ak_check_reply(t, a, call(a, BOOK, "{" HASH ",\"timeout_seconds\":5}"),
                   RESULT_OK("Book downloaded successfully to: /dl/" MD5 ".bin"));
    ak_check_stream(t, a);
    a = ak_begin(); /* control: 0 is the default, 3600 s */
    a->k->read_advance = 1000;
    script(a->k, "", NULL, "x");
    ak_check_reply(t, a, call(a, BOOK, "{" HASH ",\"timeout_seconds\":0}"),
                   RESULT_OK("Book downloaded successfully to: /dl/" MD5 ".bin"));
    ak_check_stream(t, a);
}

static void t_file_exists_and_fs_errors(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, "", NULL, "x");
    CHECK(t, kit_preload(a->k, MD5 ".bin", "old"));
    ak_check_reply(t, a, call(a, BOOK, "{" HASH "}"), RESULT_ERR("annas-c: E_FS: file already exists: " MD5 ".bin"));
    ak_check_stream(t, a);
    kit_check_no_partial(t, a->k, 1);
    a = ak_begin();
    script(a->k, "", NULL, "x");
    (void)ak_deps(a);
    CHECK(t, ports_fake_fs_inject(&a->k->fs, PORTS_FAKE_FS_OP_COMMIT, 1, PORTS_FS_E_IO));
    ak_check_reply(t, a, call(a, BOOK, "{" HASH "}"), RESULT_ERR("annas-c: E_FS: could not save the file"));
    ak_check_stream(t, a);
    kit_check_no_partial(t, a->k, 1);
}

static void t_parse_error(ctest_t *t) {
    ak_t *a = ak_begin();
    kit_api(a->k, MD5, "", kit_http(a->k, 200, "OK", JSON_HDR, "{\"download_url\":\"ftp://x/y\"}"));
    ak_check_reply(t, a, call(a, BOOK, "{" HASH "}"), RESULT_ERR("annas-c: E_PARSE: download URL must be an absolute https URL"));
    ak_check_stream(t, a);
}

static void t_workspace_refused(ctest_t *t) {
    ak_t *a = ak_begin();
    static app_mcp_t srv;
    static unsigned char mem[APP_MCP_ARENA_MIN];
    app_deps_t d = *ak_deps(a);
    d.workspace_cap = 100;
    CHECK(t, app_mcp_init(&srv, &d, mem, sizeof mem));
    ak_clear(a);
    (void)snprintf(g_req, sizeof g_req,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"fast_download_url\",\"arguments\":{" HASH "}}}");
    CHECK_EQ_I64(t, app_mcp_handle(&srv, core_str_from_cstr(g_req), &a->out), APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), RESULT_ERR("annas-c: internal error: invalid dependencies"));
    ak_check_no_io(t, a);
}

CTEST_SUITE("mcp_call",
    {"book_download", t_book_download},
    {"book_download_without_account", t_book_download_without_account},
    {"book_download_all_arguments", t_book_download_all_arguments},
    {"book_download_empty_title_and_format_mean_absent", t_book_download_empty_title_and_format_mean_absent},
    {"book_download_uppercase_hash", t_book_download_uppercase_hash},
    {"unicode_title_is_emitted_as_utf8", t_unicode_title_is_emitted_as_utf8},
    {"fast_download_url", t_fast_download_url},
    {"fast_download_url_all_arguments_and_no_account", t_fast_download_url_all_arguments_and_no_account},
    {"fast_download_url_needs_no_download_directory", t_fast_download_url_needs_no_download_directory},
    {"the_id_is_echoed_in_tool_results", t_the_id_is_echoed_in_tool_results},
    {"the_base_url_comes_from_the_environment", t_the_base_url_comes_from_the_environment},
    {"two_calls_in_a_row", t_two_calls_in_a_row},
    {"bad_hash_is_an_operation_error", t_bad_hash_is_an_operation_error},
    {"bad_format_is_an_operation_error", t_bad_format_is_an_operation_error},
    {"timeout_range_is_an_operation_error", t_timeout_range_is_an_operation_error},
    {"missing_and_invalid_configuration", t_missing_and_invalid_configuration},
    {"api_errors", t_api_errors},
    {"browser_check", t_browser_check},
    {"download_host_and_network_errors", t_download_host_and_network_errors},
    {"timeout", t_timeout},
    {"file_exists_and_fs_errors", t_file_exists_and_fs_errors},
    {"parse_error", t_parse_error},
    {"workspace_refused", t_workspace_refused})
