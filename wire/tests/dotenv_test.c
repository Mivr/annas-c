/* wire: the `.env` file and the layering of the environment (spec section 1: the process environment wins over `.env`).
 * Observed through download / download-url with NO network: either a configuration error (the layered value is
 * visible in the message) or, when the configuration is fine, the TLS error of the deliberately unparsable CA bundle
 * (exit 5, "annas-c: E_NET: TLS error"), which proves the run got past `.env`, CA and config up to the connect. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ctest/ctest.h"
#include "wire/tests/kit.h"

#define E_TLS "annas-c: E_NET: TLS error\n"
#define E_UNSET "annas-c: E_CONFIG: ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set\n"

static char g_text[WIRE_DOTENV_MAX + 64u];

static void base(void) {
    kw_reset();
    kw_ca(KW_CA_FAKE);
}

static bool starts_with(const char *s, const char *p) {
    return strncmp(s, p, strlen(p)) == 0;
}

/* A configuration error that echoes `needle`: exit 2, no stdout. */
static void check_config_error(ctest_t *t, int rc, const char *needle) {
    CHECK_EQ_I64(t, rc, 2);
    CHECK_EQ_U64(t, kw_out_len(), 0);
    CHECK(t, starts_with(kw_err(), "annas-c: E_CONFIG: "));
    CHECK(t, kw_contains(kw_err(), needle));
}

static void t_no_dotenv_is_fine(ctest_t *t) {
    base();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("ANNAS_BASE_URL", "bad_host");
    check_config_error(t, KW_CLI("download-url", KW_MD5), "bad_host");
    CHECK_EQ_STR(t, kw_read_path(0), ".env");
    CHECK_EQ_U64(t, kw()->refused, 0);
}

static void t_dotenv_is_read_from_the_current_directory_first(ctest_t *t) {
    base();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_file(".env", "# nothing\n");
    kw_env("ANNAS_BASE_URL", "bad_host");
    check_config_error(t, KW_CLI("download-url", KW_MD5), "bad_host");
    CHECK_EQ_U64(t, kw_reads(), 2);
    CHECK_EQ_STR(t, kw_read_path(0), ".env");
    CHECK_EQ_STR(t, kw_read_path(1), KW_CA_PATH);
}

static void t_dotenv_supplies_a_missing_value(ctest_t *t) {
    base();
    kw_file(".env", "ANNAS_BASE_URL=bad_dotenv\nANNAS_SECRET_KEY=" KW_KEY "\n");
    check_config_error(t, KW_CLI("download-url", KW_MD5), "bad_dotenv");
}

static void t_process_env_wins_over_dotenv(ctest_t *t) {
    base();
    kw_file(".env", "ANNAS_BASE_URL=bad_dotenv\nANNAS_SECRET_KEY=" KW_KEY "\n");
    kw_env("ANNAS_BASE_URL", "bad_process");
    check_config_error(t, KW_CLI("download-url", KW_MD5), "bad_process");
    CHECK(t, !kw_contains(kw_err(), "bad_dotenv"));
}

static void t_process_env_wins_even_when_set_to_empty(ctest_t *t) {
    /* control: the key from .env alone gets past the configuration (to the TLS error) */
    base();
    kw_file(".env", "ANNAS_SECRET_KEY=" KW_KEY "\n");
    kw_check(t, KW_CLI("download-url", KW_MD5), 5, "", E_TLS);
    /* the process sets the key to the empty string: that wins, and an empty key is not a key */
    base();
    kw_file(".env", "ANNAS_SECRET_KEY=" KW_KEY "\n");
    kw_env("ANNAS_SECRET_KEY", "");
    kw_check(t, KW_CLI("download-url", KW_MD5), 2, "", E_UNSET);
}

static void t_key_and_directory_from_both_sources(ctest_t *t) {
    base();
    kw_file(".env", "ANNAS_SECRET_KEY=" KW_KEY "\nANNAS_DOWNLOAD_PATH=/abs\n");
    kw_env("ANNAS_DOWNLOAD_PATH", "rel/dir");
    kw_check(t, KW_CLI("download", KW_MD5), 2, "",
             "annas-c: E_CONFIG: ANNAS_DOWNLOAD_PATH must be an absolute path, got: rel/dir\n");
    /* control: without the process override the .env directory is used and the run reaches the connect */
    base();
    kw_file(".env", "ANNAS_SECRET_KEY=" KW_KEY "\nANNAS_DOWNLOAD_PATH=/abs\n");
    kw_check(t, KW_CLI("download", KW_MD5), 5, "", E_TLS);
}

static void t_nothing_configured(ctest_t *t) {
    base();
    kw_check(t, KW_CLI("download", KW_MD5), 2, "", E_UNSET);
}

static void t_dotenv_syntax_is_apps(ctest_t *t) {
    /* CRLF, quotes, comments, spaces around the name and the value, a later line replacing an earlier one */
    base();
    kw_file(".env",
            "# comment\r\n  ANNAS_SECRET_KEY = \"" KW_KEY "\"  \r\nANNAS_DOWNLOAD_PATH=/old\r\n"
            "ANNAS_DOWNLOAD_PATH=rel\r\n");
    kw_check(t, KW_CLI("download", KW_MD5), 2, "",
             "annas-c: E_CONFIG: ANNAS_DOWNLOAD_PATH must be an absolute path, got: rel\n");
}

static void t_dotenv_value_reaches_the_ca_search(ctest_t *t) {
    kw_reset();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("ANNAS_BASE_URL", "bad_host");
    kw_file(".env", "SSL_CERT_FILE=/from/dotenv.pem\n");
    kw_file("/from/dotenv.pem", KW_CA_FAKE);
    check_config_error(t, KW_CLI("download-url", KW_MD5), "bad_host");
    CHECK_EQ_STR(t, kw_read_path(1), "/from/dotenv.pem");
}

static void t_unreadable_dotenv(ctest_t *t) {
    static const int errs[] = {-EACCES, -EISDIR, -EIO, -ENOTDIR, -ELOOP};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        base();
        kw_env("ANNAS_SECRET_KEY", KW_KEY);
        kw_file_err(".env", errs[i]);
        kw_check(t, KW_CLI("download-url", KW_MD5), 2, "", WIRE_ERR_DOTENV_READ);
        CHECK_EQ_U64(t, kw_reads(), 1); /* the CA bundle is not looked at */
    }
    /* control: only -ENOENT means "no file" */
    base();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_file_err(".env", -ENOENT);
    kw_check(t, KW_CLI("download-url", KW_MD5), 5, "", E_TLS);
}

static void t_oversize_dotenv(ctest_t *t) {
    base();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_file_fill(".env", ' ', WIRE_DOTENV_MAX + 1u);
    kw_check(t, KW_CLI("download-url", KW_MD5), 2, "", WIRE_ERR_DOTENV_BIG);
    base();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_file_fill(".env", ' ', 3u * WIRE_DOTENV_MAX);
    kw_check(t, KW_CLI("download-url", KW_MD5), 2, "", WIRE_ERR_DOTENV_BIG);
    /* control: exactly the cap is accepted */
    base();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_file_fill(".env", ' ', WIRE_DOTENV_MAX);
    kw_check(t, KW_CLI("download-url", KW_MD5), 5, "", E_TLS);
}

static size_t build_entries(size_t n) {
    size_t len = 0;
    for (size_t i = 0; i < n; i++) {
        int w = snprintf(g_text + len, sizeof g_text - len, "V%zu=%zu\n", i, i);
        len += (size_t)w;
    }
    return len;
}

static void t_dotenv_entry_limit(ctest_t *t) {
    base();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_file_n(".env", g_text, build_entries(65));
    kw_check(t, KW_CLI("download-url", KW_MD5), 2, "", WIRE_ERR_DOTENV_MANY);
    /* control: 64 entries are fine (lines that are skipped do not count) */
    base();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    size_t n = build_entries(64);
    memcpy(g_text + n, "# c\n\nnot a line\n=x\n", 20);
    kw_file_n(".env", g_text, n + 20u);
    kw_check(t, KW_CLI("download-url", KW_MD5), 5, "", E_TLS);
}

static void t_the_key_is_never_printed_by_wire_errors(ctest_t *t) {
    base();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_file_err(".env", -EACCES);
    (void)KW_CLI("download-url", KW_MD5);
    kw_check_no_key(t, KW_KEY);
    base();
    kw_file(".env", "ANNAS_SECRET_KEY=" KW_KEY "\n");
    kw_env("ANNAS_BASE_URL", "bad_host");
    (void)KW_CLI("download-url", KW_MD5);
    kw_check_no_key(t, KW_KEY);
    CHECK(t, kw_err_len() > 0u);
}

static void t_null_envp_is_an_empty_environment(ctest_t *t) {
    base();
    kw_file(".env", "ANNAS_SECRET_KEY=" KW_KEY "\nSSL_CERT_FILE=" KW_CA_PATH "\n");
    kw_file(KW_CA_PATH, KW_CA_FAKE);
    const char *argv[] = {"annas-c", "download-url", KW_MD5};
    CHECK_EQ_I64(t, wire_run(kw_mem(), WIRE_CLI, kw_io(), 3, argv, NULL), 5);
    CHECK_EQ_STR(t, kw_err(), E_TLS);
}

static void t_memory_may_hold_anything_and_be_reused(ctest_t *t) {
    base();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("ANNAS_BASE_URL", "bad_host");
    memset(kw_mem(), 0xA5, sizeof(wire_mem_t));
    check_config_error(t, KW_CLI("download-url", KW_MD5), "bad_host");
    kw_clear_output();
    check_config_error(t, KW_CLI("download-url", KW_MD5), "bad_host");
    CHECK_EQ_U64(t, kw_err_len(), strlen(kw_err()));
}

CTEST_SUITE("wire_dotenv", {"no_dotenv_is_fine", t_no_dotenv_is_fine},
            {"dotenv_is_read_from_the_current_directory_first", t_dotenv_is_read_from_the_current_directory_first},
            {"dotenv_supplies_a_missing_value", t_dotenv_supplies_a_missing_value},
            {"process_env_wins_over_dotenv", t_process_env_wins_over_dotenv},
            {"process_env_wins_even_when_set_to_empty", t_process_env_wins_even_when_set_to_empty},
            {"key_and_directory_from_both_sources", t_key_and_directory_from_both_sources},
            {"nothing_configured", t_nothing_configured}, {"dotenv_syntax_is_apps", t_dotenv_syntax_is_apps},
            {"dotenv_value_reaches_the_ca_search", t_dotenv_value_reaches_the_ca_search},
            {"unreadable_dotenv", t_unreadable_dotenv}, {"oversize_dotenv", t_oversize_dotenv},
            {"dotenv_entry_limit", t_dotenv_entry_limit},
            {"the_key_is_never_printed_by_wire_errors", t_the_key_is_never_printed_by_wire_errors},
            {"null_envp_is_an_empty_environment", t_null_envp_is_an_empty_environment},
            {"memory_may_hold_anything_and_be_reused", t_memory_may_hold_anything_and_be_reused})
