/* wire: choosing and reading the CA bundle. Observed without a network: the path log of the file seam, and either the
 * documented CA error text or (when the bundle is accepted) the configuration error of ANNAS_BASE_URL=bad_host, which
 * comes after the CA step. Every rejection has a positive control that gets past the CA step. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ctest/ctest.h"
#include "wire/tests/kit.h"

#define P1 WIRE_CA_PATH_1
#define P2 WIRE_CA_PATH_2
#define P3 WIRE_CA_PATH_3

static char g_path[WIRE_PATH_MAX + 16u];
static char g_dotenv[WIRE_PATH_MAX + 64u];

static void base(void) {
    kw_reset();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("ANNAS_BASE_URL", "bad_host");
}

static bool starts_with(const char *s, const char *p) {
    return strncmp(s, p, strlen(p)) == 0;
}

/* The CA step passed: the configuration error of the base URL came out. */
static void check_accepted(ctest_t *t, int rc) {
    CHECK_EQ_I64(t, rc, 2);
    CHECK_EQ_U64(t, kw_out_len(), 0);
    CHECK(t, starts_with(kw_err(), "annas-c: E_CONFIG: "));
    CHECK(t, kw_contains(kw_err(), "bad_host"));
    kw_check_no_key(t, KW_KEY);
}

static void check_rejected(ctest_t *t, int rc, const char *text) {
    kw_check(t, rc, 2, "", text);
    kw_check_no_key(t, KW_KEY);
}

static void run_dl(ctest_t *t, const char *text, bool ok) {
    int rc = KW_CLI("download-url", KW_MD5);
    if (ok) {
        check_accepted(t, rc);
    } else {
        check_rejected(t, rc, text);
    }
}

static void t_ssl_cert_file_is_the_only_candidate(ctest_t *t) {
    base();
    kw_env("SSL_CERT_FILE", "/x/ca.pem");
    kw_file(P1, KW_CA_FAKE); /* a good system bundle does not rescue an unreadable SSL_CERT_FILE */
    run_dl(t, WIRE_ERR_CA_READ, false);
    CHECK_EQ_U64(t, kw_reads(), 2);
    CHECK_EQ_STR(t, kw_read_path(1), "/x/ca.pem");
    base(); /* control */
    kw_env("SSL_CERT_FILE", "/x/ca.pem");
    kw_file("/x/ca.pem", KW_CA_FAKE);
    kw_file(P1, "unused");
    run_dl(t, "", true);
    CHECK_EQ_U64(t, kw_reads(), 2);
    CHECK_EQ_STR(t, kw_read_path(1), "/x/ca.pem");
}

static void t_empty_ssl_cert_file_is_unset(ctest_t *t) {
    base();
    kw_env("SSL_CERT_FILE", "");
    kw_file(P1, KW_CA_FAKE);
    run_dl(t, "", true);
    CHECK_EQ_U64(t, kw_reads(), 2);
    CHECK_EQ_STR(t, kw_read_path(1), P1);
}

static void t_system_paths_in_order(ctest_t *t) {
    base();
    kw_file(P1, KW_CA_FAKE);
    kw_file(P2, KW_CA_FAKE);
    kw_file(P3, KW_CA_FAKE);
    run_dl(t, "", true);
    CHECK_EQ_U64(t, kw_reads(), 2); /* .env, P1 and no more */
    CHECK_EQ_STR(t, kw_read_path(1), P1);
    base();
    kw_file(P2, KW_CA_FAKE);
    kw_file(P3, KW_CA_FAKE);
    run_dl(t, "", true);
    CHECK_EQ_U64(t, kw_reads(), 3);
    CHECK_EQ_STR(t, kw_read_path(1), P1);
    CHECK_EQ_STR(t, kw_read_path(2), P2);
    base();
    kw_file(P3, KW_CA_FAKE);
    run_dl(t, "", true);
    CHECK_EQ_U64(t, kw_reads(), 4);
    CHECK_EQ_STR(t, kw_read_path(3), P3);
}

static void t_no_candidate(ctest_t *t) {
    base();
    run_dl(t, WIRE_ERR_CA_NONE, false);
    CHECK_EQ_U64(t, kw_reads(), 4);
    CHECK_EQ_STR(t, kw_read_path(1), P1);
    CHECK_EQ_STR(t, kw_read_path(2), P2);
    CHECK_EQ_STR(t, kw_read_path(3), P3);
}

static void t_any_read_error_skips_a_system_candidate(ctest_t *t) {
    static const int errs[] = {-ENOENT, -EACCES, -EISDIR, -EIO};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        base();
        kw_file_err(P1, errs[i]);
        kw_file(P2, KW_CA_FAKE);
        run_dl(t, "", true);
        CHECK_EQ_U64(t, kw_reads(), 3);
        base(); /* and all three failing with the same error is "none" */
        kw_file_err(P1, errs[i]);
        kw_file_err(P2, errs[i]);
        kw_file_err(P3, errs[i]);
        run_dl(t, WIRE_ERR_CA_NONE, false);
    }
}

static void t_ssl_cert_file_read_errors(ctest_t *t) {
    static const int errs[] = {-ENOENT, -EACCES, -EISDIR, -EIO};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        base();
        kw_env("SSL_CERT_FILE", "/x/ca.pem");
        kw_file_err("/x/ca.pem", errs[i]);
        run_dl(t, WIRE_ERR_CA_READ, false);
    }
}

static void t_empty_bundle(ctest_t *t) {
    base();
    kw_env("SSL_CERT_FILE", "/x/ca.pem");
    kw_file("/x/ca.pem", "");
    run_dl(t, WIRE_ERR_CA_EMPTY, false);
    base(); /* a system file that reads as empty is final: the later candidates are not tried */
    kw_file(P1, "");
    kw_file(P2, KW_CA_FAKE);
    run_dl(t, WIRE_ERR_CA_EMPTY, false);
    CHECK_EQ_U64(t, kw_reads(), 2);
    base(); /* control: one byte is enough for wire (the TLS stream judges the content) */
    kw_env("SSL_CERT_FILE", "/x/ca.pem");
    kw_file("/x/ca.pem", "x");
    run_dl(t, "", true);
}

static void t_oversize_bundle(ctest_t *t) {
    base();
    kw_env("SSL_CERT_FILE", "/x/ca.pem");
    kw_file_fill("/x/ca.pem", 'A', WIRE_CA_MAX + 1u);
    run_dl(t, WIRE_ERR_CA_BIG, false);
    CHECK_EQ_U64(t, kw()->last_cap, WIRE_CA_MAX + 1u);
    base();
    kw_env("SSL_CERT_FILE", "/x/ca.pem");
    kw_file_fill("/x/ca.pem", 'A', 4u * WIRE_CA_MAX);
    run_dl(t, WIRE_ERR_CA_BIG, false);
    base(); /* a system file */
    kw_file_fill(P1, 'A', WIRE_CA_MAX + 1u);
    kw_file(P2, KW_CA_FAKE);
    run_dl(t, WIRE_ERR_CA_BIG, false);
    CHECK_EQ_U64(t, kw_reads(), 2);
    base(); /* control: exactly the cap is accepted */
    kw_env("SSL_CERT_FILE", "/x/ca.pem");
    kw_file_fill("/x/ca.pem", 'A', WIRE_CA_MAX);
    run_dl(t, "", true);
    CHECK_EQ_U64(t, kw()->last_cap, WIRE_CA_MAX + 1u);
}

static size_t dotenv_with_path(size_t path_len) {
    size_t n = (size_t)snprintf(g_dotenv, sizeof g_dotenv, "SSL_CERT_FILE=");
    g_path[0] = '/';
    memset(g_path + 1, 'a', path_len - 1u);
    g_path[path_len] = '\0';
    memcpy(g_dotenv + n, g_path, path_len);
    n += path_len;
    g_dotenv[n++] = '\n';
    return n;
}

static void t_path_length_limit(ctest_t *t) {
    base();
    kw_file_n(".env", g_dotenv, dotenv_with_path(WIRE_PATH_MAX));
    kw_file(P1, KW_CA_FAKE);
    run_dl(t, WIRE_ERR_CA_READ, false);
    CHECK_EQ_U64(t, kw_reads(), 1); /* too long: never handed to the seam, no fallback */
    base(); /* control: one byte shorter is passed on (and is simply not there) */
    kw_file_n(".env", g_dotenv, dotenv_with_path(WIRE_PATH_MAX - 1u));
    run_dl(t, WIRE_ERR_CA_READ, false);
    CHECK_EQ_U64(t, kw_reads(), 2);
    CHECK(t, starts_with(kw_read_path(1), "/aaaa"));
    base(); /* control: and a long path that exists works */
    size_t n = dotenv_with_path(WIRE_PATH_MAX - 1u);
    kw_file_n(".env", g_dotenv, n);
    kw_file(g_path, KW_CA_FAKE);
    run_dl(t, "", true);
}

static void t_path_with_a_nul_byte(ctest_t *t) {
    base();
    kw_file_n(".env", "SSL_CERT_FILE=/a\0b\n", 19);
    kw_file("/a", KW_CA_FAKE); /* what a C string would see */
    run_dl(t, WIRE_ERR_CA_READ, false);
    CHECK_EQ_U64(t, kw_reads(), 1);
    base(); /* control: the same file without the NUL byte */
    kw_file_n(".env", "SSL_CERT_FILE=/ab\n", 18);
    kw_file("/ab", KW_CA_FAKE);
    run_dl(t, "", true);
}

static void t_process_ssl_cert_file_wins_over_dotenv(ctest_t *t) {
    base();
    kw_file(".env", "SSL_CERT_FILE=/from/dotenv.pem\n");
    kw_file("/from/dotenv.pem", KW_CA_FAKE);
    run_dl(t, "", true);
    CHECK_EQ_STR(t, kw_read_path(1), "/from/dotenv.pem");
    base();
    kw_file(".env", "SSL_CERT_FILE=/from/dotenv.pem\n");
    kw_file("/from/dotenv.pem", KW_CA_FAKE);
    kw_env("SSL_CERT_FILE", "/from/process.pem");
    kw_file("/from/process.pem", KW_CA_FAKE);
    run_dl(t, "", true);
    CHECK_EQ_STR(t, kw_read_path(1), "/from/process.pem");
    CHECK_EQ_U64(t, kw_reads(), 2);
}

static void t_a_process_ssl_cert_file_set_to_empty_hides_the_dotenv_one(ctest_t *t) {
    base();
    kw_env("SSL_CERT_FILE", "");
    kw_file(".env", "SSL_CERT_FILE=/from/dotenv.pem\n");
    kw_file("/from/dotenv.pem", KW_CA_FAKE);
    run_dl(t, WIRE_ERR_CA_NONE, false); /* unset for wire, and there is no system bundle */
    CHECK_EQ_U64(t, kw_reads(), 4);
    base(); /* control: with the process variable absent the .env one is used */
    kw_file(".env", "SSL_CERT_FILE=/from/dotenv.pem\n");
    kw_file("/from/dotenv.pem", KW_CA_FAKE);
    run_dl(t, "", true);
}

static void t_the_ca_is_needed_before_any_network_operation_only(ctest_t *t) {
    /* download, download-url and mcp need a CA; help, version and usage errors never look for one */
    base();
    kw_check(t, KW_CLI("download", KW_MD5), 2, "", WIRE_ERR_CA_NONE);
    base();
    kw_in("");
    kw_check(t, KW_CLI("mcp"), 2, "", WIRE_ERR_CA_NONE);
    base();
    kw_check(t, KW_MCP0(), 2, "", WIRE_ERR_CA_NONE);
    CHECK_EQ_U64(t, kw()->in_calls, 0);
    base();
    CHECK_EQ_I64(t, KW_CLI("--version"), 0);
    CHECK_EQ_U64(t, kw_reads(), 0);
    base();
    CHECK_EQ_I64(t, KW_CLI("download"), 2); /* usage error: missing md5; only .env is read, for the key */
    CHECK_EQ_U64(t, kw_reads(), 1);
    CHECK_EQ_STR(t, kw_read_path(0), ".env");
}

CTEST_SUITE("wire_ca", {"ssl_cert_file_is_the_only_candidate", t_ssl_cert_file_is_the_only_candidate},
            {"empty_ssl_cert_file_is_unset", t_empty_ssl_cert_file_is_unset},
            {"system_paths_in_order", t_system_paths_in_order}, {"no_candidate", t_no_candidate},
            {"any_read_error_skips_a_system_candidate", t_any_read_error_skips_a_system_candidate},
            {"ssl_cert_file_read_errors", t_ssl_cert_file_read_errors}, {"empty_bundle", t_empty_bundle},
            {"oversize_bundle", t_oversize_bundle}, {"path_length_limit", t_path_length_limit},
            {"path_with_a_nul_byte", t_path_with_a_nul_byte},
            {"process_ssl_cert_file_wins_over_dotenv", t_process_ssl_cert_file_wins_over_dotenv},
            {"a_process_ssl_cert_file_set_to_empty_hides_the_dotenv_one",
             t_a_process_ssl_cert_file_set_to_empty_hides_the_dotenv_one},
            {"the_ca_is_needed_before_any_network_operation_only",
             t_the_ca_is_needed_before_any_network_operation_only})
