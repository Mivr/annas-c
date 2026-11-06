/* app_cli_run: successful runs and the fixed texts, byte for byte, through scripted ports. */
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"

#define MD5 AK_MD5
#define URL "https://dl.example.org/f/1"
#define ACCT "{\"downloads_left\":24,\"downloads_per_day\":25}"
#define JSON_HDR "Content-Type: application/json\r\n"

#define HELP_TEXT                                                                                                  \
    "annas-c 0.1: download files from Anna's Archive with a member account key\n"                                  \
    "\n"                                                                                                           \
    "Usage:\n"                                                                                                     \
    "  annas-c download <md5> [--name <title>] [--format <ext>] [--path-index <n>] [--domain-index <n>] [--timeout <seconds>]\n" \
    "  annas-c book-download <md5> <filename.ext>\n"                                                               \
    "  annas-c download-url <md5> [--path-index <n>] [--domain-index <n>] [--timeout <seconds>]\n"                 \
    "  annas-c mcp\n"                                                                                              \
    "  annas-c --help\n"                                                                                           \
    "  annas-c --version\n"                                                                                        \
    "\n"                                                                                                           \
    "Environment (a .env file in the current directory is read first, then the process environment wins):\n"       \
    "  ANNAS_SECRET_KEY     member account key (required)\n"                                                       \
    "  ANNAS_DOWNLOAD_PATH  absolute download directory (required by download)\n"                                  \
    "  ANNAS_BASE_URL       domain of the site (default annas-archive.gl)\n"                                       \
    "\n"                                                                                                           \
    "Exit codes: 0 ok, 2 bad usage or configuration, 3 browser check, 4 HTTP or API error,\n"                      \
    "5 network error or timeout, 6 file error, 7 parse error\n"

/* API call (extra = "" or "&path_index=1"...) answering with `acct` (or none) and the file GET answering `body`. */
static void script(kit_t *k, const char *md5, const char *extra, const char *acct, const char *body) {
    kit_api(k, md5, extra, kit_http(k, 200, "OK", JSON_HDR, kit_ok_json(k, URL, acct)));
    if (body != NULL) {
        kit_get(k, "dl.example.org", 443, "/f/1", kit_http(k, 200, "OK", "", body));
    }
}

static void t_download_defaults(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, MD5, "", ACCT, "FILEBYTES");
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/" MD5 ".bin\nAccount: " ACCT "\n", "");
    ak_check_stream(t, a);
    kit_check_file(t, a->k, MD5 ".bin", "FILEBYTES", 9);
}

static void t_download_name_and_format(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, MD5, "", ACCT, "abc");
    int rc = AK_CLI(a, "download", MD5, "--name", "My Book", "--format", "PDF");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/My Book.pdf\nAccount: " ACCT "\n", "");
    ak_check_stream(t, a);
    kit_check_file(t, a->k, "My Book.pdf", "abc", 3);
}

static void t_download_name_is_sanitised(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, MD5, "", NULL, "x");
    int rc = AK_CLI(a, "download", MD5, "--name", "../etc/pass:wd", "--format", "txt");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/__etc_pass_wd.txt\n", "");
    ak_check_stream(t, a);
    kit_check_file(t, a->k, "__etc_pass_wd.txt", "x", 1);
}

static void t_alias_book_download(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, MD5, "", ACCT, "epub-bytes");
    int rc = AK_CLI(a, "book-download", MD5, "Some Title.EPUB");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/Some Title.epub\nAccount: " ACCT "\n", "");
    ak_check_stream(t, a);
    kit_check_file(t, a->k, "Some Title.epub", "epub-bytes", 10);
}

static void t_no_account_line_when_the_api_sends_none(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, MD5, "", NULL, "z");
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/" MD5 ".bin\n", "");
    ak_check_stream(t, a);
}

static void t_indexes_are_sent_to_the_api(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, MD5, "&path_index=3&domain_index=0", NULL, "z");
    int rc = AK_CLI(a, "download", MD5, "--path-index", "3", "--domain-index", "0");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/" MD5 ".bin\n", "");
    ak_check_stream(t, a);
    a = ak_begin(); /* control: only one of them */
    script(a->k, MD5, "&domain_index=7", NULL, "z");
    rc = AK_CLI(a, "download", "--domain-index", "7", MD5);
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/" MD5 ".bin\n", "");
    ak_check_stream(t, a);
}

static void t_uppercase_md5_is_normalised(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, MD5, "", NULL, "z");
    int rc = AK_CLI(a, "download", "0123456789ABCDEF0123456789ABCDEF");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/" MD5 ".bin\n", "");
    ak_check_stream(t, a);
}

static void t_download_url(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, MD5, "", ACCT, NULL);
    int rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 0, "Download URL: " URL "\nAccount: " ACCT "\n", "");
    ak_check_stream(t, a);
    kit_check_fs_untouched(t, a->k);
}

static void t_download_url_with_options_and_without_account(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, MD5, "&path_index=2&domain_index=1", NULL, NULL);
    int rc = AK_CLI(a, "download-url", MD5, "--domain-index", "1", "--path-index", "2", "--timeout", "30");
    ak_check_cli(t, a, rc, 0, "Download URL: " URL "\n", "");
    ak_check_stream(t, a);
    kit_check_fs_untouched(t, a->k);
}

static void t_download_url_needs_no_download_directory(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_DOWNLOAD_PATH", NULL);
    script(a->k, MD5, "", NULL, NULL);
    int rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 0, "Download URL: " URL "\n", "");
    ak_check_stream(t, a);
}

static void t_base_url_from_the_environment(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_BASE_URL", "https://mirror.example.org/");
    kit_set_base(a->k, "mirror.example.org");
    script(a->k, MD5, "", NULL, NULL);
    int rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 0, "Download URL: " URL "\n", "");
    ak_check_stream(t, a);
}

static void t_download_directory_trailing_slashes(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_DOWNLOAD_PATH", "/data/books//");
    script(a->k, MD5, "", NULL, "z");
    int rc = AK_CLI(a, "download", MD5, "--name", "n", "--format", "bin");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /data/books/n.bin\n", "");
    ak_check_stream(t, a);
}

static void t_dotenv_supplies_the_configuration(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    ak_env(a, "ANNAS_DOWNLOAD_PATH", NULL);
    ak_dotenv(a, "# config\r\nANNAS_SECRET_KEY=\"" KIT_KEY "\"\r\nANNAS_DOWNLOAD_PATH=/from-dotenv\r\n");
    script(a->k, MD5, "", NULL, "z");
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /from-dotenv/" MD5 ".bin\n", "");
    ak_check_stream(t, a);
}

static void t_process_environment_beats_dotenv(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_DOWNLOAD_PATH", "/from-process");
    ak_dotenv(a, "ANNAS_DOWNLOAD_PATH=/from-dotenv\nANNAS_SECRET_KEY=another-key\n");
    script(a->k, MD5, "", NULL, "z");
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /from-process/" MD5 ".bin\n", "");
    ak_check_stream(t, a); /* the API request carried the process key, not the .env key */
}

static void t_timeout_option_is_enforced(ctest_t *t) {
    ak_t *a = ak_begin();
    a->k->read_advance = 1000; /* the API read ends at the deadline of a 1 s timeout */
    a->k->allow_unused = true;
    script(a->k, MD5, "", NULL, "z");
    int rc = AK_CLI(a, "download", MD5, "--timeout", "1");
    ak_check_cli(t, a, rc, 5, "", "annas-c: E_TIMEOUT: operation timed out\n");
    ak_check_stream(t, a);
    kit_check_fs_untouched(t, a->k);
    a = ak_begin(); /* control: the same script with 5 s */
    a->k->read_advance = 1000;
    script(a->k, MD5, "", NULL, "z");
    rc = AK_CLI(a, "download", MD5, "--timeout", "5");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/" MD5 ".bin\n", "");
    ak_check_stream(t, a);
    a = ak_begin(); /* control: 0 means the default (3600 s) */
    a->k->read_advance = 1000;
    script(a->k, MD5, "", NULL, "z");
    rc = AK_CLI(a, "download", MD5, "--timeout", "0");
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/" MD5 ".bin\n", "");
    ak_check_stream(t, a);
}

static void t_default_timeout_is_one_hour(ctest_t *t) {
    /* two reads of read_advance each: the clock is start + 2 * advance before the temp file is created */
    ak_t *a = ak_begin();
    a->k->read_advance = 1799999; /* 1 ms before the 3600 s deadline */
    script(a->k, MD5, "", NULL, "z");
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 0, "Book downloaded successfully to: /dl/" MD5 ".bin\n", "");
    ak_check_stream(t, a);
    a = ak_begin();
    a->k->read_advance = 1800000; /* exactly at the deadline */
    script(a->k, MD5, "", NULL, "z");
    rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 5, "", "annas-c: E_TIMEOUT: operation timed out\n");
    ak_check_stream(t, a);
    kit_check_no_partial(t, a->k, 0);
}

static void t_help(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", NULL);
    ak_env(a, "ANNAS_DOWNLOAD_PATH", NULL);
    int rc = AK_CLI(a, "--help");
    ak_check_cli(t, a, rc, 0, HELP_TEXT, "");
    ak_check_no_io(t, a);
    a = ak_begin();
    rc = AK_CLI(a, "-h");
    ak_check_cli(t, a, rc, 0, HELP_TEXT, "");
    ak_check_no_io(t, a);
}

static void t_version(ctest_t *t) {
    ak_t *a = ak_begin();
    int rc = AK_CLI(a, "--version");
    ak_check_cli(t, a, rc, 0, "annas-c 0.1\n", "");
    ak_check_no_io(t, a);
}

static void t_help_and_version_need_no_deps(ctest_t *t) {
    static char o[4096];
    static char e[256];
    core_buf_t out;
    core_buf_t err;
    app_cmd_t c;
    memset(&c, 0, sizeof c);
    c.kind = APP_CMD_VERSION;
    CHECK(t, core_buf_init(&out, o, sizeof o));
    CHECK(t, core_buf_init(&err, e, sizeof e));
    CHECK_EQ_I64(t, app_cli_run(&c, NULL, &out, &err), 0);
    CHECK_EQ_STR(t, core_buf_cstr(&out), "annas-c 0.1\n");
    CHECK_EQ_STR(t, core_buf_cstr(&err), "");
    c.kind = APP_CMD_HELP;
    CHECK(t, core_buf_init(&out, o, sizeof o));
    CHECK_EQ_I64(t, app_cli_run(&c, NULL, &out, &err), 0);
    CHECK_EQ_STR(t, core_buf_cstr(&out), HELP_TEXT);
}

static void t_buffers_are_appended_to(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, MD5, "", NULL, "z");
    core_buf_put_str(&a->out, core_str_from_cstr("OUT:"));
    core_buf_put_str(&a->err, core_str_from_cstr("ERR:"));
    int rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 0, "OUT:Book downloaded successfully to: /dl/" MD5 ".bin\n", "ERR:");
    ak_check_stream(t, a);
    a = ak_begin();
    core_buf_put_str(&a->out, core_str_from_cstr("OUT:"));
    core_buf_put_str(&a->err, core_str_from_cstr("ERR:"));
    rc = AK_CLI(a, "download", "zz");
    ak_check_cli(t, a, rc, 2, "OUT:", "ERR:annas-c: E_ARG: md5 must be 32 hexadecimal characters\n");
}

static void t_two_runs_on_one_deps_object(ctest_t *t) {
    ak_t *a = ak_begin();
    script(a->k, MD5, "", ACCT, NULL);
    script(a->k, MD5, "&path_index=1", NULL, NULL);
    int rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 0, "Download URL: " URL "\nAccount: " ACCT "\n", "");
    ak_clear(a);
    rc = AK_CLI(a, "download-url", MD5, "--path-index", "1");
    ak_check_cli(t, a, rc, 0, "Download URL: " URL "\n", "");
    ak_check_stream(t, a);
}

CTEST_SUITE("cli_run",
    {"download_defaults", t_download_defaults},
    {"download_name_and_format", t_download_name_and_format},
    {"download_name_is_sanitised", t_download_name_is_sanitised},
    {"alias_book_download", t_alias_book_download},
    {"no_account_line_when_the_api_sends_none", t_no_account_line_when_the_api_sends_none},
    {"indexes_are_sent_to_the_api", t_indexes_are_sent_to_the_api},
    {"uppercase_md5_is_normalised", t_uppercase_md5_is_normalised},
    {"download_url", t_download_url},
    {"download_url_with_options_and_without_account", t_download_url_with_options_and_without_account},
    {"download_url_needs_no_download_directory", t_download_url_needs_no_download_directory},
    {"base_url_from_the_environment", t_base_url_from_the_environment},
    {"download_directory_trailing_slashes", t_download_directory_trailing_slashes},
    {"dotenv_supplies_the_configuration", t_dotenv_supplies_the_configuration},
    {"process_environment_beats_dotenv", t_process_environment_beats_dotenv},
    {"timeout_option_is_enforced", t_timeout_option_is_enforced},
    {"default_timeout_is_one_hour", t_default_timeout_is_one_hour},
    {"help", t_help},
    {"version", t_version},
    {"help_and_version_need_no_deps", t_help_and_version_need_no_deps},
    {"buffers_are_appended_to", t_buffers_are_appended_to},
    {"two_runs_on_one_deps_object", t_two_runs_on_one_deps_object})
