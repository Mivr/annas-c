/* Tests for http/http.h on the recorded live responses in fixtures/live/ (embedded below: c_test targets carry no data
 * files; the bodies are byte-for-byte the fixture files). The response HEADERS are not recorded in the fixtures
 * (fixtures/live/README.md lists status and Location only), so the head of each message is reconstructed from the
 * README and Content-Length is the fixture size. Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/buf.h"
#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

/* fixtures/live/search_302_check.html (283 bytes) */
static const char k_html[] =
    "<!doctype html>\n"
    "<html lang=en>\n"
    "<title>Redirecting...</title>\n"
    "<h1>Redirecting...</h1>\n"
    "<p>You should be redirected automatically to the target URL: <a href=\"/search?q=lakos&amp;content=book_any&amp;check=1\">/search?q=lakos&amp;content=book_any&amp;check=1</a>. If not, click the link.\n";

/* fixtures/live/api_401_invalid_key.json (1083 bytes) */
static const char k_json401[] =
    "{\n"
    "  \"///download_url\": [\n"
    "    \"This API is intended as a stable JSON API for getting fast download files as a member.\",\n"
    "    \"A successful request will return status code 200 or 204, a `download_url` field and `account_fast_download_info`.\",\n"
    "    \"Bad responses use different status codes, a `download_url` set to `null`, and `error` field with string description.\",\n"
    "    \"Accepted query parameters:\",\n"
    "    \"- `md5` (required): the md5 string of the requested file.\",\n"
    "    \"- `key` (required): the secret key for your account (which must have membership).\",\n"
    "    \"- `path_index` (optional): Integer, 0 or larger, indicating the collection (if the file is present in more than one).\",\n"
    "    \"- `domain_index` (optional): Integer, 0 or larger, indicating the download server, e.g. 0='Fast Partner Server #1'.\",\n"
    "    \"These parameters correspond to the fast download page like this: /fast_download/{md5}/{path_index}/{domain_index}\",\n"
    "    \"Example: /dyn/api/fast_download.json?md5=d6e1dc51a50726f00ec438af21952a45&key=YOUR_SECRET_KEY\"\n"
    "  ],\n"
    "  \"download_url\": null,\n"
    "  \"error\": \"Invalid secret key\"\n"
    "}";

/* fixtures/live/api_404_record_not_found.json (1081 bytes) */
static const char k_json404[] =
    "{\n"
    "  \"///download_url\": [\n"
    "    \"This API is intended as a stable JSON API for getting fast download files as a member.\",\n"
    "    \"A successful request will return status code 200 or 204, a `download_url` field and `account_fast_download_info`.\",\n"
    "    \"Bad responses use different status codes, a `download_url` set to `null`, and `error` field with string description.\",\n"
    "    \"Accepted query parameters:\",\n"
    "    \"- `md5` (required): the md5 string of the requested file.\",\n"
    "    \"- `key` (required): the secret key for your account (which must have membership).\",\n"
    "    \"- `path_index` (optional): Integer, 0 or larger, indicating the collection (if the file is present in more than one).\",\n"
    "    \"- `domain_index` (optional): Integer, 0 or larger, indicating the download server, e.g. 0='Fast Partner Server #1'.\",\n"
    "    \"These parameters correspond to the fast download page like this: /fast_download/{md5}/{path_index}/{domain_index}\",\n"
    "    \"Example: /dyn/api/fast_download.json?md5=d6e1dc51a50726f00ec438af21952a45&key=YOUR_SECRET_KEY\"\n"
    "  ],\n"
    "  \"download_url\": null,\n"
    "  \"error\": \"Record not found\"\n"
    "}";

/* The 401 body as a chunked transfer coding: chunks of 97 bytes with extensions. */
static const char k_chunked401[] =
    "61;part=0\r\n"
    "{\n  \"///download_url\": [\n    \"This API is intended as a stable JSON API for getting fast download\r\n"
    "61;part=1\r\n"
    " files as a member.\",\n    \"A successful request will return status code 200 or 204, a `download_u\r\n"
    "61;part=2\r\n"
    "rl` field and `account_fast_download_info`.\",\n    \"Bad responses use different status codes, a `d\r\n"
    "61;part=3\r\n"
    "ownload_url` set to `null`, and `error` field with string description.\",\n    \"Accepted query para\r\n"
    "61;part=4\r\n"
    "meters:\",\n    \"- `md5` (required): the md5 string of the requested file.\",\n    \"- `key` (required\r\n"
    "61;part=5\r\n"
    "): the secret key for your account (which must have membership).\",\n    \"- `path_index` (optional)\r\n"
    "61;part=6\r\n"
    ": Integer, 0 or larger, indicating the collection (if the file is present in more than one).\",\n  \r\n"
    "61;part=7\r\n"
    "  \"- `domain_index` (optional): Integer, 0 or larger, indicating the download server, e.g. 0='Fas\r\n"
    "61;part=8\r\n"
    "t Partner Server #1'.\",\n    \"These parameters correspond to the fast download page like this: /fa\r\n"
    "61;part=9\r\n"
    "st_download/{md5}/{path_index}/{domain_index}\",\n    \"Example: /dyn/api/fast_download.json?md5=d6e\r\n"
    "61;part=10\r\n"
    "1dc51a50726f00ec438af21952a45&key=YOUR_SECRET_KEY\"\n  ],\n  \"download_url\": null,\n  \"error\": \"Inval\r\n"
    "10;part=11\r\n"
    "id secret key\"\n}\r\n"
    "0\r\n"
    "\r\n";

static http_parser_t g_p;
static char g_body[4096];
static size_t g_body_len;
static char g_loc[256];
static size_t g_loc_len;
static int g_status;
static int g_ended;
static int g_minor;

/* Parses `msg` in pieces of `step` (0: whole); fills the g_* results. Returns the first error or HTTP_OK. */
static http_status_t collect(const char *msg, size_t n, size_t step) {
    size_t off = 0;
    bool first = true;
    g_body_len = 0;
    g_loc_len = 0;
    g_status = 0;
    g_ended = 0;
    g_minor = -1;
    if (!http_parser_init(&g_p)) {
        return HTTP_E_ARG;
    }
    while (off < n || first) {
        size_t take = (step == 0 || step > n - off) ? n - off : step;
        core_str_t in = {msg + off, take};
        unsigned guard;
        first = false;
        off += take;
        for (guard = 0; guard < 100000u; guard++) {
            http_event_t ev;
            http_status_t rc = http_parser_next(&g_p, &in, &ev);
            if (rc != HTTP_OK) {
                return rc;
            }
            if (ev.kind == HTTP_EV_NONE) {
                break;
            }
            if (ev.kind == HTTP_EV_STATUS) {
                g_status = ev.status;
                g_minor = ev.minor;
            } else if (ev.kind == HTTP_EV_HEADER && core_str_eq(ev.name, LIT("location")) && ev.value.len < sizeof g_loc) {
                memcpy(g_loc, ev.value.ptr, ev.value.len);
                g_loc_len = ev.value.len;
            } else if (ev.kind == HTTP_EV_BODY && ev.data.len <= sizeof g_body - g_body_len) {
                memcpy(g_body + g_body_len, ev.data.ptr, ev.data.len);
                g_body_len += ev.data.len;
            } else if (ev.kind == HTTP_EV_END) {
                g_ended = 1;
            }
        }
    }
    return HTTP_OK;
}

static const size_t k_steps[] = {0, 1, 2, 5, 64, 97, 1000};
#define NSTEPS (sizeof k_steps / sizeof k_steps[0])

static void t_search_302_is_a_browser_check(ctest_t *t) {
    static const char head[] = "HTTP/1.1 302 Found\r\n"
                               "Location: /search?q=lakos&content=book_any&check=1\r\n"
                               "Content-Type: text/html; charset=utf-8\r\n"
                               "Content-Length: 283\r\n"
                               "Connection: close\r\n"
                               "\r\n";
    static const char want_loc[] = "/search?q=lakos&content=book_any&check=1";
    static char msg[1024];
    static char store[512];
    size_t n, i;
    memcpy(msg, head, sizeof head - 1);
    memcpy(msg + sizeof head - 1, k_html, sizeof k_html - 1);
    n = sizeof head - 1 + sizeof k_html - 1;
    for (i = 0; i < NSTEPS; i++) {
        http_url_t cur;
        http_redirects_t r = {0};
        core_buf_t out;
        CHECK_EQ_I64(t, collect(msg, n, k_steps[i]), HTTP_OK);
        CHECK_EQ_I64(t, g_status, 302);
        CHECK_EQ_I64(t, g_ended, 1);
        CHECK_EQ_MEM(t, g_body, g_body_len, k_html, sizeof k_html - 1);
        CHECK_EQ_MEM(t, g_loc, g_loc_len, want_loc, sizeof want_loc - 1);
        CHECK_EQ_I64(t, http_detect_challenge(g_status, (core_str_t){g_loc, g_loc_len}, (core_str_t){g_body, g_body_len}),
                     HTTP_E_BROWSER_CHECK);
        /* the redirect itself resolves to the same page with check=1 */
        CHECK_EQ_I64(t, http_url_split(LIT("https://annas-archive.gl/search?q=lakos&content=book_any"), &cur), HTTP_OK);
        CHECK(t, core_buf_init(&out, store, sizeof store));
        CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, g_status, (core_str_t){g_loc, g_loc_len}, &out), HTTP_OK);
        CHECK_EQ_STR(t, core_buf_cstr(&out), "https://annas-archive.gl/search?q=lakos&content=book_any&check=1");
    }
}

static void t_same_redirect_without_check_is_not_a_challenge(ctest_t *t) {
    static const char head[] = "HTTP/1.1 302 Found\r\n"
                               "Location: /search?q=lakos&content=book_any\r\n"
                               "Content-Length: 283\r\n\r\n";
    static char msg[1024];
    size_t n;
    memcpy(msg, head, sizeof head - 1);
    memcpy(msg + sizeof head - 1, k_html, sizeof k_html - 1);
    n = sizeof head - 1 + sizeof k_html - 1;
    CHECK_EQ_I64(t, collect(msg, n, 0), HTTP_OK);
    CHECK_EQ_I64(t, g_status, 302);
    CHECK_EQ_I64(t, http_detect_challenge(g_status, (core_str_t){g_loc, g_loc_len}, (core_str_t){g_body, g_body_len}), HTTP_OK);
}

static void api_case(ctest_t *t, const char *head, int status, const char *json, size_t jl) {
    static char msg[2048];
    size_t i, n = strlen(head);
    memcpy(msg, head, n);
    memcpy(msg + n, json, jl);
    n += jl;
    for (i = 0; i < NSTEPS; i++) {
        CHECK_EQ_I64(t, collect(msg, n, k_steps[i]), HTTP_OK);
        CHECK_EQ_I64(t, g_status, status);
        CHECK_EQ_I64(t, g_minor, 1);
        CHECK_EQ_I64(t, g_ended, 1);
        CHECK_EQ_MEM(t, g_body, g_body_len, json, jl);
        CHECK_EQ_U64(t, g_loc_len, 0);
        CHECK_EQ_I64(t, http_detect_challenge(g_status, (core_str_t){NULL, 0}, (core_str_t){g_body, g_body_len}), HTTP_OK);
    }
}

static void t_api_401(ctest_t *t) {
    api_case(t, "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: 1083\r\n\r\n", 401, k_json401,
             sizeof k_json401 - 1);
}

static void t_api_404(ctest_t *t) {
    api_case(t, "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: 1081\r\n\r\n", 404, k_json404,
             sizeof k_json404 - 1);
}

static void t_api_401_chunked(ctest_t *t) {
    static const char head[] = "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n";
    static char msg[2048];
    size_t i, n;
    memcpy(msg, head, sizeof head - 1);
    memcpy(msg + sizeof head - 1, k_chunked401, sizeof k_chunked401 - 1);
    n = sizeof head - 1 + sizeof k_chunked401 - 1;
    for (i = 0; i < NSTEPS; i++) {
        CHECK_EQ_I64(t, collect(msg, n, k_steps[i]), HTTP_OK);
        CHECK_EQ_I64(t, g_status, 401);
        CHECK_EQ_I64(t, g_ended, 1);
        CHECK_EQ_MEM(t, g_body, g_body_len, k_json401, sizeof k_json401 - 1);
    }
}

/* Synthetic (no capture exists): a DDoS-Guard interstitial; also a strictness control (2 bytes too many). */
static void t_synthetic_ddos_guard_403(ctest_t *t) {
    static const char bad_msg[] = "HTTP/1.1 403 Forbidden\r\nServer: ddos-guard\r\nContent-Length: 47\r\n\r\n"
                                  "<html><title>DDoS-Guard</title>Checking...</html>";
    static const char ok_msg[] = "HTTP/1.1 403 Forbidden\r\nServer: ddos-guard\r\nContent-Length: 49\r\n\r\n"
                                 "<html><title>DDoS-Guard</title>Checking...</html>";
    CHECK_EQ_I64(t, collect(bad_msg, sizeof bad_msg - 1, 0), HTTP_E_EXTRA);
    CHECK_EQ_I64(t, collect(ok_msg, sizeof ok_msg - 1, 1), HTTP_OK);
    CHECK_EQ_I64(t, g_status, 403);
    CHECK_EQ_I64(t, g_ended, 1);
    CHECK_EQ_I64(t, http_detect_challenge(g_status, (core_str_t){NULL, 0}, (core_str_t){g_body, g_body_len}), HTTP_E_BROWSER_CHECK);
}

CTEST_SUITE("http_live",
            { "search_302_is_a_browser_check", t_search_302_is_a_browser_check },
            { "same_redirect_without_check_is_not_a_challenge", t_same_redirect_without_check_is_not_a_challenge },
            { "api_401", t_api_401 },
            { "api_404", t_api_404 },
            { "api_401_chunked", t_api_401_chunked },
            { "synthetic_ddos_guard_403", t_synthetic_ddos_guard_403 })
