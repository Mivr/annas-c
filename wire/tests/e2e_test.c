/* wire, end to end: `annas-c download-url` and `annas-c download` over a REAL TLS handshake with the gate's in-memory
 * TLS server (tests/peer.c): the CA bundle comes from the file seam, certificate time from the wall-clock seam, the
 * deadline from the clock seam, the directory from a fake posix_fs table. Positive controls: the success cases; every
 * failure case differs from one of them in the one thing it tests. */
#include <errno.h>
#include <string.h>

#include "tls_peer.h"
#include "ctest/ctest.h"
#include "wire/tests/kit.h"

#define URL "https://dl.example.org/f/1"
#define ACCOUNT "{\"downloads_left\":24}"
#define OK_URL_OUT "Download URL: " URL "\nAccount: " ACCOUNT "\n"
#define E_TLS "annas-c: E_NET: TLS error\n"
#define FILE_URL "https://" KW_PEER_HOST "/files/b.bin"
#define API_REQ_LINE "GET /dyn/api/fast_download.json?md5=" KW_MD5 "&key=" KW_KEY " HTTP/1.1\r\n"

static void api_ok(void) {
    kw_peer_respond(0, kw_http(200, "OK", KW_API_OK_BODY(URL)));
}

static void t_download_url_over_tls(ctest_t *t) {
    kw_e2e_setup();
    api_ok();
    kw_check(t, KW_CLI("download-url", KW_MD5), 0, OK_URL_OUT, "");
    CHECK_EQ_U64(t, kw_peer_connects(), 1);
    CHECK_EQ_U64(t, kw_peer_closes(), 1);
    CHECK_EQ_STR(t, kw_peer_host(0), KW_PEER_HOST);
    CHECK_EQ_U64(t, kw_peer_port(0), 443);
    CHECK(t, strncmp(kw_peer_request(0), API_REQ_LINE, strlen(API_REQ_LINE)) == 0);
    CHECK(t, kw_contains(kw_peer_request(0), "\r\nHost: " KW_PEER_HOST "\r\n"));
    CHECK(t, kw_contains(kw_peer_request(0), "\r\nUser-Agent: annas-c/0.1\r\n"));
    CHECK(t, kw()->unix_calls > 0u); /* the certificate was judged on the wall-clock seam */
    CHECK_EQ_U64(t, kw()->fs.opens, 0); /* download-url never opens the directory */
    kw_check_no_key(t, KW_KEY);
    kw_check_no_empty_writes(t);
}

static void t_the_directory_need_not_exist_for_download_url(ctest_t *t) {
    kw_e2e_setup();
    api_ok();
    kw()->fs.open_err = -ENOENT;
    kw_check(t, KW_CLI("download-url", KW_MD5), 0, OK_URL_OUT, "");
    CHECK_EQ_U64(t, kw()->fs.opens, 0);
}

static void t_deadline_comes_from_the_clock_seam(ctest_t *t) {
    kw_e2e_setup();
    api_ok();
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 0);
    CHECK_EQ_U64(t, kw_peer_deadline(0), (uint64_t)KW_MONO_S * 1000u + 3600000u);
    kw_e2e_setup();
    api_ok();
    kw()->mono_s = 5000;
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5, "--timeout", "10"), 0);
    CHECK_EQ_U64(t, kw_peer_deadline(0), 5000u * 1000u + 10000u);
}

static void t_optional_indexes_reach_the_request(ctest_t *t) {
    kw_e2e_setup();
    api_ok();
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5, "--domain-index", "3", "--path-index", "2"), 0);
    CHECK(t, kw_contains(kw_peer_request(0), "&path_index=2&domain_index=3 HTTP/1.1\r\n"));
}

static void t_base_url_from_the_process_wins_over_dotenv(ctest_t *t) {
    kw_e2e_setup();
    api_ok();
    kw_file(".env", "ANNAS_BASE_URL=other.invalid\nANNAS_DOWNLOAD_PATH=/elsewhere\n");
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 0);
    CHECK_EQ_STR(t, kw_peer_host(0), KW_PEER_HOST);
    kw_e2e_setup(); /* control: with no process value the .env value is the host that gets connected */
    kw_file(".env", "ANNAS_BASE_URL=other.invalid\n");
    kw_env_clear();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("SSL_CERT_FILE", KW_CA_PATH);
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 5);
    CHECK_EQ_STR(t, kw_peer_host(0), "other.invalid");
}

static void t_api_error(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(401, "Unauthorized", "{\"download_url\":null,\"error\":\"Invalid secret key\"}"));
    kw_check(t, KW_CLI("download-url", KW_MD5), 4, "", "annas-c: E_API: 401: Invalid secret key\n");
    CHECK_EQ_U64(t, kw_peer_closes(), 1);
    kw_check_no_key(t, KW_KEY);
}

static void t_browser_check(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(503, "Service Unavailable", "<html>DDoS-Guard checking your browser</html>"));
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 3);
    CHECK(t, strncmp(kw_err(), "annas-c: E_BROWSER_CHECK: ", 26) == 0);
    CHECK_EQ_U64(t, kw_out_len(), 0);
}

static void t_the_key_echoed_by_the_server_is_redacted(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(400, "Bad Request", "{\"download_url\":null,\"error\":\"bad key " KW_KEY " sorry\"}"));
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 4);
    CHECK(t, kw_contains(kw_err(), "REDACTED"));
    kw_check_no_key(t, KW_KEY);
}

static void t_the_key_that_needs_percent_encoding(ctest_t *t) {
    /* a key outside the key class is E_CONFIG: no request leaves, and the text never echoes the key */
    kw_e2e_setup();
    kw_env_clear();
    kw_env("ANNAS_SECRET_KEY", "FAKE KEY&x=1/+abcd");
    kw_env("ANNAS_BASE_URL", KW_PEER_HOST);
    kw_env("SSL_CERT_FILE", KW_CA_PATH);
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 2);
    CHECK_EQ_U64(t, kw_peer_request(0)[0], 0);
    kw_check_no_key(t, "FAKE KEY&x=1/+abcd");
    kw_check_no_key(t, "FAKE%20KEY%26x%3D1%2F%2B");
    CHECK(t, kw_contains(kw_err(), "annas-c: E_CONFIG: ANNAS_SECRET_KEY may only contain letters, digits"));
}

/* ---- the trust decision ---------------------------------------------------------------------------------------- */

static void check_refused_handshake(ctest_t *t) {
    kw_check(t, KW_CLI("download-url", KW_MD5), 5, "", E_TLS);
    CHECK_EQ_U64(t, kw_peer_request(0)[0], 0); /* the key was NOT sent to a server that was not verified */
    kw_check_no_key(t, KW_KEY);
}

static void t_a_ca_that_does_not_match(ctest_t *t) {
    kw_e2e_setup();
    api_ok();
    kw_env_clear();
    kw_files_clear();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("ANNAS_BASE_URL", KW_PEER_HOST);
    kw_ca(kw_peer_other_ca());
    check_refused_handshake(t);
}

static void t_the_wall_clock_decides_certificate_validity(ctest_t *t) {
    kw_e2e_setup();
    api_ok();
    kw()->unix_s = 0; /* unknown time: fail closed */
    check_refused_handshake(t);
    kw_e2e_setup();
    api_ok();
    kw()->unix_s = 1593561600u; /* 2020-07-01: before the server certificate's window */
    check_refused_handshake(t);
    kw_e2e_setup(); /* control */
    api_ok();
    kw()->unix_s = 1798761600u;
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 0);
}

static void t_server_identities(ctest_t *t) {
    static const int bad[] = {TLS_PEER_EXPIRED, TLS_PEER_NOT_YET_VALID, TLS_PEER_WRONG_HOST, TLS_PEER_UNKNOWN_CA};
    static const int good[] = {TLS_PEER_EC_VALID, TLS_PEER_RSA_VALID, TLS_PEER_CHAIN};
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        kw_e2e_setup();
        kw_peer_identity(good[i]);
        api_ok();
        kw_check(t, KW_CLI("download-url", KW_MD5), 0, OK_URL_OUT, "");
    }
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        kw_e2e_setup();
        kw_peer_identity(bad[i]);
        api_ok();
        check_refused_handshake(t);
    }
}

static void t_the_bundle_is_not_a_certificate(ctest_t *t) {
    kw_e2e_setup();
    api_ok();
    kw_env_clear();
    kw_files_clear();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("ANNAS_BASE_URL", KW_PEER_HOST);
    kw_ca(KW_CA_FAKE);
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 5);
    CHECK_EQ_STR(t, kw_err(), E_TLS);
    CHECK_EQ_U64(t, kw_peer_connects(), 0); /* an unusable bundle trusts nothing and never connects */
}

/* ---- connection failures --------------------------------------------------------------------------------------- */

static void t_connect_failures(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_connect_err(ECONNREFUSED);
    kw_check(t, KW_CLI("download-url", KW_MD5), 5, "", "annas-c: E_NET: connection failed\n");
    CHECK_EQ_U64(t, kw_peer_connects(), 1);
    kw_e2e_setup();
    kw_peer_connect_err(ETIMEDOUT);
    kw_check(t, KW_CLI("download-url", KW_MD5), 5, "", "annas-c: E_TIMEOUT: operation timed out\n");
    kw_check_no_key(t, KW_KEY);
}

static void t_a_server_that_hangs_up(ctest_t *t) {
    kw_e2e_setup(); /* no response scripted: close_notify right after the request */
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 5);
    CHECK(t, strncmp(kw_err(), "annas-c: E_NET: ", 16) == 0);
    kw_check_no_key(t, KW_KEY);
}

/* ---- download ------------------------------------------------------------------------------------------------- */

#define DL_API KW_API_OK_BODY(FILE_URL)
#define DL_OUT(name) "Book downloaded successfully to: /dl/" name "\nAccount: " ACCOUNT "\n"

static void t_download(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(200, "OK", DL_API));
    kw_peer_respond(1, kw_http(200, "OK", "BOOKDATA-1234"));
    kw_check(t, KW_CLI("download", KW_MD5, "--name", "My Book", "--format", "EPUB"), 0, DL_OUT("My Book.epub"), "");
    CHECK_EQ_U64(t, kw_peer_connects(), 2);
    CHECK_EQ_U64(t, kw_peer_closes(), 2);
    CHECK_EQ_STR(t, kw_peer_host(1), KW_PEER_HOST);
    CHECK(t, strncmp(kw_peer_request(1), "GET /files/b.bin HTTP/1.1\r\n", 27) == 0);
    CHECK(t, !kw_contains(kw_peer_request(1), "key="));
    CHECK(t, !kw_contains(kw_peer_request(1), KW_KEY));
    CHECK_EQ_U64(t, kw()->fs.opens, 1);
    CHECK_EQ_STR(t, kw()->fs.dir, "/dl");
    CHECK_EQ_U64(t, kw()->fs.links, 1);
    CHECK_EQ_STR(t, kw()->fs.name, "My Book.epub");
    CHECK_EQ_MEM(t, kw()->fs.content, kw()->fs.content_len, "BOOKDATA-1234", 13);
    CHECK_EQ_U64(t, kw()->fs.closes, 2); /* the temp file's descriptor and the directory */
    kw_check_no_key(t, KW_KEY);
}

static void t_download_default_name(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(200, "OK", DL_API));
    kw_peer_respond(1, kw_http(200, "OK", "x"));
    kw_check(t, KW_CLI("download", KW_MD5), 0, DL_OUT(KW_MD5 ".bin"), "");
    CHECK_EQ_STR(t, kw()->fs.name, KW_MD5 ".bin");
}

static void t_a_file_name_made_from_the_key(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(200, "OK", DL_API));
    kw_peer_respond(1, kw_http(200, "OK", "x"));
    CHECK_EQ_I64(t, KW_CLI("download", KW_MD5, "--name", KW_KEY, "--format", "pdf"), 0);
    CHECK_EQ_STR(t, kw()->fs.name, "REDACTED.pdf"); /* no file is named after the key... */
    kw_check_no_key(t, KW_KEY);                     /* ...and the output does not show it */
    CHECK(t, kw_contains(kw_out(), "REDACTED.pdf"));
}

static void t_the_directory_that_cannot_be_opened(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(200, "OK", DL_API));
    kw_peer_respond(1, kw_http(200, "OK", "x"));
    kw()->fs.open_err = -EACCES;
    CHECK_EQ_I64(t, KW_CLI("download", KW_MD5), 6);
    CHECK(t, strncmp(kw_err(), "annas-c: E_FS: ", 15) == 0);
    CHECK_EQ_U64(t, kw_out_len(), 0);
    CHECK_EQ_U64(t, kw()->fs.opens, 1);
    CHECK_EQ_U64(t, kw()->fs.links, 0);
    kw_check_no_key(t, KW_KEY);
}

static void t_the_directory_is_not_opened_before_a_200(ctest_t *t) {
    kw_e2e_setup();
    kw_peer_respond(0, kw_http(200, "OK", DL_API));
    kw_peer_respond(1, kw_http(404, "Not Found", "gone"));
    CHECK_EQ_I64(t, KW_CLI("download", KW_MD5), 4);
    CHECK(t, strncmp(kw_err(), "annas-c: E_HTTP: ", 17) == 0);
    CHECK_EQ_U64(t, kw()->fs.opens, 0);
    CHECK_EQ_U64(t, kw()->fs.links, 0);
    kw_e2e_setup(); /* an API failure as well */
    kw_peer_respond(0, kw_http(401, "Unauthorized", "{\"download_url\":null,\"error\":\"no\"}"));
    CHECK_EQ_I64(t, KW_CLI("download", KW_MD5), 4);
    CHECK_EQ_U64(t, kw()->fs.opens, 0);
}

static void t_configuration_is_checked_before_any_connection(ctest_t *t) {
    kw_e2e_setup();
    kw_env_clear();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("ANNAS_BASE_URL", KW_PEER_HOST);
    kw_env("ANNAS_DOWNLOAD_PATH", "relative");
    kw_env("SSL_CERT_FILE", KW_CA_PATH);
    kw_check(t, KW_CLI("download", KW_MD5), 2, "",
             "annas-c: E_CONFIG: ANNAS_DOWNLOAD_PATH must be an absolute path, got: relative\n");
    CHECK_EQ_U64(t, kw_peer_connects(), 0);
    CHECK_EQ_U64(t, kw()->fs.opens, 0);
}

static void t_the_run_can_be_repeated_with_the_same_memory(ctest_t *t) {
    kw_e2e_setup();
    api_ok();
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 0);
    kw_clear_output();
    kw_peer_reset();
    api_ok();
    kw_check(t, KW_CLI("download-url", KW_MD5), 0, OK_URL_OUT, "");
    CHECK_EQ_U64(t, kw_peer_connects(), 1);
}

CTEST_SUITE("wire_e2e", {"download_url_over_tls", t_download_url_over_tls},
            {"the_directory_need_not_exist_for_download_url", t_the_directory_need_not_exist_for_download_url},
            {"deadline_comes_from_the_clock_seam", t_deadline_comes_from_the_clock_seam},
            {"optional_indexes_reach_the_request", t_optional_indexes_reach_the_request},
            {"base_url_from_the_process_wins_over_dotenv", t_base_url_from_the_process_wins_over_dotenv},
            {"api_error", t_api_error}, {"browser_check", t_browser_check},
            {"the_key_echoed_by_the_server_is_redacted", t_the_key_echoed_by_the_server_is_redacted},
            {"the_key_that_needs_percent_encoding", t_the_key_that_needs_percent_encoding},
            {"a_ca_that_does_not_match", t_a_ca_that_does_not_match},
            {"the_wall_clock_decides_certificate_validity", t_the_wall_clock_decides_certificate_validity},
            {"server_identities", t_server_identities},
            {"the_bundle_is_not_a_certificate", t_the_bundle_is_not_a_certificate},
            {"connect_failures", t_connect_failures}, {"a_server_that_hangs_up", t_a_server_that_hangs_up},
            {"download", t_download}, {"download_default_name", t_download_default_name},
            {"a_file_name_made_from_the_key", t_a_file_name_made_from_the_key},
            {"the_directory_that_cannot_be_opened", t_the_directory_that_cannot_be_opened},
            {"the_directory_is_not_opened_before_a_200", t_the_directory_is_not_opened_before_a_200},
            {"configuration_is_checked_before_any_connection", t_configuration_is_checked_before_any_connection},
            {"the_run_can_be_repeated_with_the_same_memory", t_the_run_can_be_repeated_with_the_same_memory})
