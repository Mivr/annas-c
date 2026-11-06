/* annas_download: failures before and during the file transfer (everything except fs failures and timeouts). */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define DLURL "https://dl.example.org/f/1?x=1"
#define BROWSER "the site requires browser verification; use download-by-md5 with a hash found in a browser"
#define EARLY "connection closed before the response was complete"

static void api_ok(kit_t *k, const char *url) {
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, url, "{\"downloads_left\":2}")));
}

static annas_code_t run(kit_t *k) {
    annas_request_t r = kit_req(KIT_MD5);
    return kit_download(k, &r);
}

/* API exchange + connect + exact GET for DLURL; the caller adds the response steps. */
static void to_dl(kit_t *k) {
    api_ok(k, DLURL);
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1?x=1");
}

static void t_api_failure_stops_before_any_download(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", kit_http(k, 401, "Unauthorized", "", kit_fixture_401));
    kit_check_err(t, k, run(k), ANNAS_E_API, 401, "401: Invalid secret key");
    kit_check_fs_untouched(t, k);
    k = kit_begin();
    kit_api(k, KIT_MD5, "", kit_http(k, 404, "Not Found", "", kit_fixture_404));
    kit_check_err(t, k, run(k), ANNAS_E_API, 404, "404: Record not found");
    kit_check_fs_untouched(t, k);
    k = kit_begin();
    kit_api(k, KIT_MD5, "", kit_http(k, 302, "Found", "Location: /search?q=a&check=1\r\n", kit_fixture_302));
    kit_check_err(t, k, run(k), ANNAS_E_BROWSER_CHECK, 0, BROWSER);
    kit_check_fs_untouched(t, k);
    k = kit_begin();
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", "{\"download_url\":null}"));
    kit_check_err(t, k, run(k), ANNAS_E_API, 200, "200: API returned empty download URL");
    kit_check_fs_untouched(t, k);
}

static void t_bad_arguments_touch_nothing(ctest_t *t) {
    static const char *const bad[] = {"tar.gz", "pdf/x", "a b", "12345678901234567", "pd\xc3\xa9", ".pdf", "pdf\n", "p-f"};
    for (size_t i = 0; i < COUNT(bad); i++) {
        kit_t *k = kit_begin();
        annas_request_t r = kit_req(KIT_MD5);
        r.format = core_str_from_cstr(bad[i]);
        kit_check_err(t, k, kit_download(k, &r), ANNAS_E_ARG, 0, "format must consist of 1 to 16 ASCII letters or digits");
        CHECK_EQ_U64(t, ports_fake_stream_pos(&k->stream), 0);
        kit_check_fs_untouched(t, k);
    }
    kit_t *k = kit_begin(); /* the md5 is validated first */
    annas_request_t r = kit_req("xyz");
    r.format = core_str_from_cstr("a b");
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_ARG, 0, "md5 must be 32 hexadecimal characters");
}

static void t_download_url_must_be_https(ctest_t *t) {
    static const char *const bad[] = {"http://dl.example.org/x", "ftp://dl.example.org/x", "//dl.example.org/x",
                                      "dl.example.org/x", "/x", "https://", "https://user@dl.example.org/x",
                                      "https://dl.example.org:0/x", "javascript:alert(1)", "https://dl.example.org",
                                      "https://dl example.org/x", " https://dl.example.org/x", "https://dl.example.org/a b",
                                      "HTTP://dl.example.org/x", "https:dl.example.org/x", "https://[::1]/x"};
    for (size_t i = 0; i < COUNT(bad); i++) {
        kit_t *k = kit_begin();
        api_ok(k, bad[i]);
        kit_check_err(t, k, run(k), ANNAS_E_PARSE, 0, "download URL must be an absolute https URL");
        kit_check_fs_untouched(t, k);
    }
}

static void t_unexpected_statuses(ctest_t *t) {
    static const int codes[] = {204, 206, 300, 304, 400, 401, 404, 410, 429, 500, 502, 504};
    for (size_t i = 0; i < COUNT(codes); i++) {
        kit_t *k = kit_begin();
        to_dl(k);
        kit_respond(k, codes[i] == 204 || codes[i] == 304 ? KIT_FMT(k, "HTTP/1.1 %d X\r\nContent-Length: 0\r\n\r\n", codes[i])
                                                           : kit_http(k, codes[i], "X", "", "nope"),
                    0);
        char want[64];
        snprintf(want, sizeof want, "unexpected HTTP status %d", codes[i]);
        kit_check_err(t, k, run(k), ANNAS_E_HTTP, codes[i], want);
        kit_check_fs_untouched(t, k);
    }
}

static void t_challenges_while_downloading(ctest_t *t) {
    static const int codes[] = {403, 503};
    for (size_t i = 0; i < COUNT(codes); i++) {
        kit_t *k = kit_begin();
        to_dl(k);
        kit_respond(k, kit_http(k, codes[i], "X", "", "<html>DDoS-Guard</html>"), 0);
        kit_check_err(t, k, run(k), ANNAS_E_BROWSER_CHECK, 0, BROWSER);
        kit_check_fs_untouched(t, k);
        k = kit_begin(); /* control: no challenge text */
        to_dl(k);
        kit_respond(k, kit_http(k, codes[i], "X", "", "<html>Forbidden</html>"), 0);
        char want[64];
        snprintf(want, sizeof want, "unexpected HTTP status %d", codes[i]);
        kit_check_err(t, k, run(k), ANNAS_E_HTTP, codes[i], want);
    }
    kit_t *k = kit_begin(); /* a redirect to a verification page */
    to_dl(k);
    kit_respond(k, kit_http(k, 302, "Found", "Location: https://dl.example.org/v?check=1\r\n", ""), 0);
    kit_check_err(t, k, run(k), ANNAS_E_BROWSER_CHECK, 0, BROWSER);
    kit_check_fs_untouched(t, k);
}

static void t_challenge_on_a_later_hop(ctest_t *t) {
    kit_t *k = kit_begin();
    api_ok(k, "https://a.example.org/x");
    kit_get(k, "a.example.org", 443, "/x", kit_http(k, 301, "M", "Location: https://b.example.org/y\r\n", ""));
    kit_get(k, "b.example.org", 443, "/y", kit_http(k, 307, "M", "Location: /w?check=1\r\n", ""));
    kit_check_err(t, k, run(k), ANNAS_E_BROWSER_CHECK, 0, BROWSER);
    kit_check_fs_untouched(t, k);
}

static void t_too_many_redirects(ctest_t *t) {
    kit_t *k = kit_begin();
    api_ok(k, "https://h0.example.org/f/0");
    for (int i = 0; i < 6; i++) {
        kit_get(k, KIT_FMT(k, "h%d.example.org", i), 443, KIT_FMT(k, "/f/%d", i),
                kit_http(k, 302, "F", KIT_FMT(k, "Location: https://h%d.example.org/f/%d\r\n", i + 1, i + 1), ""));
    }
    kit_check_err(t, k, run(k), ANNAS_E_HTTP, 302, "too many redirects");
    kit_check_fs_untouched(t, k);
}

static void t_bad_redirects(ctest_t *t) {
    static const struct {
        const char *hdr;
        const char *msg;
    } c[] = {
        {"", "redirect without a Location header"},
        {"Location: \r\n", "redirect without a Location header"},
        {"Location: http://b.example.org/y\r\n", "redirect to a non-https URL"},
        {"Location: ftp://b.example.org/y\r\n", "redirect to a non-https URL"},
        {"Location: javascript:alert(1)\r\n", "redirect to a non-https URL"},
        {"Location: https://user@b.example.org/y\r\n", "invalid redirect location"},
        {"Location: https://b.example.org:99999/y\r\n", "invalid redirect location"},
        {"Location: https:/b.example.org/y\r\n", "invalid redirect location"},
        {"Location: https://b.example.org/a b\r\n", "invalid redirect location"},
        {"Location: https://b..example.org/y\r\n", "invalid redirect location"},
    };
    for (size_t i = 0; i < COUNT(c); i++) {
        kit_t *k = kit_begin();
        to_dl(k);
        kit_respond(k, kit_http(k, 303, "S", c[i].hdr, ""), 0);
        kit_check_err(t, k, run(k), ANNAS_E_HTTP, 303, c[i].msg);
        kit_check_fs_untouched(t, k);
    }
}

static void t_connect_and_write_failures_while_downloading(ctest_t *t) {
    kit_t *k = kit_begin();
    api_ok(k, DLURL);
    kit_connect_err(k, "dl.example.org", 443, PORTS_E_NET);
    kit_check_err(t, k, run(k), ANNAS_E_NET, 0, "connection failed");
    kit_check_fs_untouched(t, k);
    k = kit_begin();
    api_ok(k, DLURL);
    kit_connect_err(k, "dl.example.org", 443, PORTS_E_TLS);
    kit_check_err(t, k, run(k), ANNAS_E_NET, 0, "TLS error");
    k = kit_begin();
    api_ok(k, DLURL);
    kit_connect(k, "dl.example.org", 443);
    kit_fail_write(k, PORTS_E_NET);
    kit_check_err(t, k, run(k), ANNAS_E_NET, 0, "network error");
    kit_check_fs_untouched(t, k);
    k = kit_begin(); /* connect failure on a redirect hop */
    api_ok(k, "https://a.example.org/x");
    kit_get(k, "a.example.org", 443, "/x", kit_http(k, 302, "F", "Location: https://b.example.org/y\r\n", ""));
    kit_connect_err(k, "b.example.org", 443, PORTS_E_NET);
    kit_check_err(t, k, run(k), ANNAS_E_NET, 0, "connection failed");
}

static void t_failures_before_the_head_is_complete_create_no_file(ctest_t *t) {
    static const char *const partial[] = {"", "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n"};
    for (size_t i = 0; i < COUNT(partial); i++) {
        kit_t *k = kit_begin();
        to_dl(k);
        if (partial[i][0] != '\0') {
            kit_respond(k, partial[i], 0);
        }
        kit_fail_read(k, PORTS_E_NET);
        kit_check_err(t, k, run(k), ANNAS_E_NET, 0, "network error");
        kit_check_fs_untouched(t, k);
        k = kit_begin();
        to_dl(k);
        if (partial[i][0] != '\0') {
            kit_respond(k, partial[i], 0);
        }
        kit_eof(k);
        kit_check_err(t, k, run(k), ANNAS_E_NET, 0, EARLY);
        kit_check_fs_untouched(t, k);
    }
}

typedef struct {
    ports_err_t err;
    annas_code_t code;
    const char *msg;
} rmap_t;

static void t_read_failures_after_the_head_discard_the_temp_file(ctest_t *t) {
    static const rmap_t m[] = {{PORTS_E_NET, ANNAS_E_NET, "network error"},
                               {PORTS_E_CLOSED, ANNAS_E_NET, "network error"},
                               {PORTS_E_TLS, ANNAS_E_NET, "TLS error"},
                               {PORTS_E_TIMEOUT, ANNAS_E_TIMEOUT, "operation timed out"}};
    for (size_t i = 0; i < COUNT(m); i++) {
        kit_t *k = kit_begin();
        to_dl(k);
        kit_respond(k, kit_head(k, 200, "OK", "", 20), 0);
        kit_respond(k, "0123456789", 0);
        kit_fail_read(k, m[i].err);
        kit_check_err(t, k, run(k), m[i].code, 0, m[i].msg);
        kit_check_no_partial(t, k, 1);
    }
}

static void t_truncated_bodies_discard_the_temp_file(ctest_t *t) {
    kit_t *k = kit_begin(); /* Content-Length not reached */
    to_dl(k);
    kit_respond(k, kit_head(k, 200, "OK", "", 20), 0);
    kit_respond(k, "0123456789", 0);
    kit_eof(k);
    kit_check_err(t, k, run(k), ANNAS_E_NET, 0, EARLY);
    kit_check_no_partial(t, k, 1);
    k = kit_begin(); /* chunked, no terminating chunk */
    to_dl(k);
    kit_respond(k, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n", 0);
    kit_eof(k);
    kit_check_err(t, k, run(k), ANNAS_E_NET, 0, EARLY);
    kit_check_no_partial(t, k, 1);
    k = kit_begin(); /* control: a close-delimited body is complete at EOF */
    to_dl(k);
    kit_respond(k, "HTTP/1.1 200 OK\r\n\r\nhello", 0);
    kit_eof(k);
    kit_check_ok(t, k, run(k));
    CHECK_EQ_U64(t, ports_fake_fs_commits(&k->fs), 1);
}

static void t_malformed_bodies_discard_the_temp_file(ctest_t *t) {
    kit_t *k = kit_begin();
    to_dl(k);
    kit_respond(k, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\nzz\r\nxx\r\n0\r\n\r\n", 0);
    k->allow_unused = true;
    kit_check_err(t, k, run(k), ANNAS_E_PARSE, 0, "malformed HTTP response");
    kit_check_no_partial(t, k, 1);
    k = kit_begin(); /* missing CRLF after chunk data */
    to_dl(k);
    kit_respond(k, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhelloXX0\r\n\r\n", 0);
    k->allow_unused = true;
    kit_check_err(t, k, run(k), ANNAS_E_PARSE, 0, "malformed HTTP response");
    kit_check_no_partial(t, k, 1);
}

static void t_malformed_head_creates_no_file(ctest_t *t) {
    kit_t *k = kit_begin();
    to_dl(k);
    kit_respond(k, "HTTP/1.1 200 OK\r\nContent-Length: 4\r\nContent-Length: 4\r\n\r\ndata", 0);
    k->allow_unused = true;
    kit_check_err(t, k, run(k), ANNAS_E_PARSE, 0, "malformed HTTP response");
    kit_check_fs_untouched(t, k);
}

CTEST_SUITE("download_err",
    {"api_failure_stops_before_any_download", t_api_failure_stops_before_any_download},
    {"bad_arguments_touch_nothing", t_bad_arguments_touch_nothing},
    {"download_url_must_be_https", t_download_url_must_be_https},
    {"unexpected_statuses", t_unexpected_statuses},
    {"challenges_while_downloading", t_challenges_while_downloading},
    {"challenge_on_a_later_hop", t_challenge_on_a_later_hop},
    {"too_many_redirects", t_too_many_redirects},
    {"bad_redirects", t_bad_redirects},
    {"connect_and_write_failures_while_downloading", t_connect_and_write_failures_while_downloading},
    {"failures_before_the_head_is_complete_create_no_file", t_failures_before_the_head_is_complete_create_no_file},
    {"read_failures_after_the_head_discard_the_temp_file", t_read_failures_after_the_head_discard_the_temp_file},
    {"truncated_bodies_discard_the_temp_file", t_truncated_bodies_discard_the_temp_file},
    {"malformed_bodies_discard_the_temp_file", t_malformed_bodies_discard_the_temp_file},
    {"malformed_head_creates_no_file", t_malformed_head_creates_no_file})
