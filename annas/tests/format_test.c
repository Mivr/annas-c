/* Tests for annas_code_name, annas_exit_code, annas_error_line and the two output formatters. */
#include <string.h>

#include "annas/annas.h"
#include "ctest/ctest.h"

static annas_error_t g_err;
static annas_fast_result_t g_fast;
static annas_download_result_t g_dl;
static char g_store[1024];
static core_buf_t g_buf;

static void fresh(size_t cap) { (void)core_buf_init(&g_buf, g_store, cap); }

static void t_code_names(ctest_t *t) {
    CHECK_EQ_STR(t, annas_code_name(ANNAS_OK), "OK");
    CHECK_EQ_STR(t, annas_code_name(ANNAS_E_ARG), "E_ARG");
    CHECK_EQ_STR(t, annas_code_name(ANNAS_E_CONFIG), "E_CONFIG");
    CHECK_EQ_STR(t, annas_code_name(ANNAS_E_BROWSER_CHECK), "E_BROWSER_CHECK");
    CHECK_EQ_STR(t, annas_code_name(ANNAS_E_HTTP), "E_HTTP");
    CHECK_EQ_STR(t, annas_code_name(ANNAS_E_API), "E_API");
    CHECK_EQ_STR(t, annas_code_name(ANNAS_E_NET), "E_NET");
    CHECK_EQ_STR(t, annas_code_name(ANNAS_E_TIMEOUT), "E_TIMEOUT");
    CHECK_EQ_STR(t, annas_code_name(ANNAS_E_FS), "E_FS");
    CHECK_EQ_STR(t, annas_code_name(ANNAS_E_PARSE), "E_PARSE");
    CHECK_EQ_STR(t, annas_code_name((annas_code_t)99), "E_UNKNOWN");
    CHECK_EQ_STR(t, annas_code_name((annas_code_t)-1), "E_UNKNOWN");
}

static void t_exit_codes(ctest_t *t) {
    CHECK_EQ_I64(t, annas_exit_code(ANNAS_OK), 0);
    CHECK_EQ_I64(t, annas_exit_code(ANNAS_E_ARG), 2);
    CHECK_EQ_I64(t, annas_exit_code(ANNAS_E_CONFIG), 2);
    CHECK_EQ_I64(t, annas_exit_code(ANNAS_E_BROWSER_CHECK), 3);
    CHECK_EQ_I64(t, annas_exit_code(ANNAS_E_HTTP), 4);
    CHECK_EQ_I64(t, annas_exit_code(ANNAS_E_API), 4);
    CHECK_EQ_I64(t, annas_exit_code(ANNAS_E_NET), 5);
    CHECK_EQ_I64(t, annas_exit_code(ANNAS_E_TIMEOUT), 5);
    CHECK_EQ_I64(t, annas_exit_code(ANNAS_E_FS), 6);
    CHECK_EQ_I64(t, annas_exit_code(ANNAS_E_PARSE), 7);
    CHECK_EQ_I64(t, annas_exit_code((annas_code_t)99), 1);
}

static void set_err(annas_code_t c, int status, const char *m) {
    memset(&g_err, 0, sizeof g_err);
    g_err.code = c;
    g_err.http_status = status;
    g_err.message_len = strlen(m);
    memcpy(g_err.message, m, g_err.message_len + 1u);
}

static void t_error_line(ctest_t *t) {
    fresh(sizeof g_store);
    set_err(ANNAS_E_API, 401, "401: Invalid secret key");
    CHECK(t, annas_error_line(&g_err, &g_buf));
    CHECK_EQ_STR(t, core_buf_cstr(&g_buf), "annas-c: E_API: 401: Invalid secret key");
    fresh(sizeof g_store);
    set_err(ANNAS_E_CONFIG, 0, "ANNAS_DOWNLOAD_PATH must be an absolute path, got: x");
    CHECK(t, annas_error_line(&g_err, &g_buf));
    CHECK_EQ_STR(t, core_buf_cstr(&g_buf), "annas-c: E_CONFIG: ANNAS_DOWNLOAD_PATH must be an absolute path, got: x");
    fresh(sizeof g_store);
    core_buf_put(&g_buf, "pre|", 4);
    set_err(ANNAS_E_NET, 0, "network error");
    CHECK(t, annas_error_line(&g_err, &g_buf)); /* appends */
    CHECK_EQ_STR(t, core_buf_cstr(&g_buf), "pre|annas-c: E_NET: network error");
}

static void t_error_line_refusals(ctest_t *t) {
    fresh(sizeof g_store);
    set_err(ANNAS_OK, 0, "");
    CHECK(t, !annas_error_line(&g_err, &g_buf));
    set_err(ANNAS_E_FS, 0, "x");
    CHECK(t, !annas_error_line(NULL, &g_buf));
    CHECK(t, !annas_error_line(&g_err, NULL));
    fresh(sizeof "annas-c: E_FS: x" - 1u); /* one byte short: cap includes the NUL */
    CHECK(t, !annas_error_line(&g_err, &g_buf));
    CHECK(t, core_buf_overflow(&g_buf));
    fresh(sizeof "annas-c: E_FS: x"); /* exactly enough */
    CHECK(t, annas_error_line(&g_err, &g_buf));
    CHECK_EQ_STR(t, core_buf_cstr(&g_buf), "annas-c: E_FS: x");
}

static void set_fast(const char *url, const char *account) {
    memset(&g_fast, 0, sizeof g_fast);
    g_fast.url_len = strlen(url);
    memcpy(g_fast.url, url, g_fast.url_len + 1u);
    g_fast.account_len = strlen(account);
    memcpy(g_fast.account, account, g_fast.account_len + 1u);
}

static void t_format_download_url(ctest_t *t) {
    fresh(sizeof g_store);
    set_fast("https://dl.example.org/f/1?x=1", "{\"downloads_left\":24}");
    CHECK(t, annas_format_download_url(&g_buf, &g_fast));
    CHECK_EQ_STR(t, core_buf_cstr(&g_buf),
                 "Download URL: https://dl.example.org/f/1?x=1\nAccount: {\"downloads_left\":24}");
    fresh(sizeof g_store);
    set_fast("https://dl.example.org/f/1", "");
    CHECK(t, annas_format_download_url(&g_buf, &g_fast));
    CHECK_EQ_STR(t, core_buf_cstr(&g_buf), "Download URL: https://dl.example.org/f/1");
}

static void t_format_download_url_refusals(ctest_t *t) {
    set_fast("https://h/x", "{}");
    CHECK(t, !annas_format_download_url(NULL, &g_fast));
    fresh(sizeof g_store);
    CHECK(t, !annas_format_download_url(&g_buf, NULL));
    fresh(sizeof "Download URL: https://h/x\nAccount: {}" - 1u);
    CHECK(t, !annas_format_download_url(&g_buf, &g_fast));
    fresh(sizeof "Download URL: https://h/x\nAccount: {}");
    CHECK(t, annas_format_download_url(&g_buf, &g_fast));
    CHECK_EQ_STR(t, core_buf_cstr(&g_buf), "Download URL: https://h/x\nAccount: {}");
}

static void set_dl(const char *path, const char *account) {
    memset(&g_dl, 0, sizeof g_dl);
    g_dl.path_len = strlen(path);
    memcpy(g_dl.path, path, g_dl.path_len + 1u);
    g_dl.fast.account_len = strlen(account);
    memcpy(g_dl.fast.account, account, g_dl.fast.account_len + 1u);
    memcpy(g_dl.fast.url, "https://ignored/", 17);
    g_dl.fast.url_len = 16;
}

static void t_format_downloaded(ctest_t *t) {
    fresh(sizeof g_store);
    set_dl("/dl/Some Title.pdf", "{\"downloads_left\":3}");
    CHECK(t, annas_format_downloaded(&g_buf, &g_dl));
    CHECK_EQ_STR(t, core_buf_cstr(&g_buf),
                 "Book downloaded successfully to: /dl/Some Title.pdf\nAccount: {\"downloads_left\":3}");
    fresh(sizeof g_store);
    set_dl("/dl/x.bin", "");
    CHECK(t, annas_format_downloaded(&g_buf, &g_dl));
    CHECK_EQ_STR(t, core_buf_cstr(&g_buf), "Book downloaded successfully to: /dl/x.bin");
}

static void t_format_downloaded_refusals(ctest_t *t) {
    set_dl("/d/x", "");
    CHECK(t, !annas_format_downloaded(NULL, &g_dl));
    fresh(sizeof g_store);
    CHECK(t, !annas_format_downloaded(&g_buf, NULL));
    fresh(sizeof "Book downloaded successfully to: /d/x" - 1u);
    CHECK(t, !annas_format_downloaded(&g_buf, &g_dl));
    fresh(sizeof "Book downloaded successfully to: /d/x");
    CHECK(t, annas_format_downloaded(&g_buf, &g_dl));
}

CTEST_SUITE("format",
    {"code_names", t_code_names},
    {"exit_codes", t_exit_codes},
    {"error_line", t_error_line},
    {"error_line_refusals", t_error_line_refusals},
    {"format_download_url", t_format_download_url},
    {"format_download_url_refusals", t_format_download_url_refusals},
    {"format_downloaded", t_format_downloaded},
    {"format_downloaded_refusals", t_format_downloaded_refusals})
