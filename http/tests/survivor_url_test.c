/* Tests for http/http.h: URL and host edge cases (scheme spelling, octets, labels, limits). Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static char g_big[HTTP_MAX_URL + 64];

static bool hv(const char *s) { return http_host_valid(core_str_from_cstr(s)); }

static void split_is(ctest_t *t, const char *url, http_status_t want) {
    http_url_t u;
    CHECK_EQ_I64(t, http_url_split(core_str_from_cstr(url), &u), want);
}

static void t_octets(ctest_t *t) {
    static const char *const good[] = {"0.0.0.0", "1.2.3.4", "9.9.9.9", "10.10.10.10", "99.99.99.99", "100.100.100.100",
                                       "199.199.199.199", "200.200.200.200", "249.249.249.249", "250.250.250.250",
                                       "255.255.255.255", "127.0.0.1", "1.0.0.0", "0.1.0.0", "0.0.1.0", "0.0.0.1"};
    static const char *const bad[] = {"256.1.1.1", "1.256.1.1", "1.1.256.1", "1.1.1.256", "260.1.1.1", "1.260.1.1",
                                      "1.1.260.1", "1.1.1.260", "300.1.1.1", "1.300.1.1", "1.1.300.1", "1.1.1.300",
                                      "999.1.1.1", "1.999.1.1", "1.1.999.1", "1.1.1.999", "1000.1.1.1", "1.1000.1.1",
                                      "1.1.1000.1", "1.1.1.1000", "00.1.1.1", "1.00.1.1", "1.1.00.1", "1.1.1.00",
                                      "01.1.1.1", "1.01.1.1", "1.1.01.1", "1.1.1.01", "001.1.1.1", "1.1.1.001",
                                      "1.2.3", "1.2", "1", "1.2.3.4.5", "1.2.3.", ".1.2.3", "1..2.3", "1.2..3",
                                      "1.2.3.4.", "..", ".", "1.2.3.-1"};
    size_t i;
    for (i = 0; i < sizeof good / sizeof good[0]; i++) {
        CHECK(t, hv(good[i]));
    }
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(t, !hv(bad[i]));
    }
    CHECK(t, hv("1.2.3.a1")); /* last label not numeric: an ordinary DNS name */
    CHECK(t, hv("1.2.3.4a"));
    CHECK(t, hv("a.b.c.d"));
    CHECK(t, hv("1.2.3.d"));
    CHECK(t, !hv("1.2.c.4")); /* numeric last label: must be a strict quad */
}

static void t_labels(ctest_t *t) {
    char h[300];
    size_t n;
    memset(h, 'a', 63);
    h[63] = 0;
    CHECK(t, hv(h));
    memset(h, 'a', 64);
    h[64] = 0;
    CHECK(t, !hv(h));
    memset(h, 'a', 63);
    h[63] = '.';
    memset(h + 64, 'b', 63);
    h[127] = 0;
    CHECK(t, hv(h));
    memset(h + 64, 'b', 64);
    h[128] = 0;
    CHECK(t, !hv(h));
    /* labels of 63 + dots: total length around 253 */
    for (n = 0; n < 4; n++) {
        memset(h + n * 64, 'c', 63);
        h[n * 64 + 63] = '.';
    }
    h[253] = 0;
    memset(h + 192, 'c', 61);
    h[253] = 0;
    CHECK_EQ_U64(t, strlen(h), 253);
    CHECK(t, hv(h));
    h[253] = 'c';
    h[254] = 0;
    CHECK(t, !hv(h));
    CHECK(t, !hv(""));
    CHECK(t, hv("a"));
    CHECK(t, hv("a-b"));
    CHECK(t, hv("a--b"));
    CHECK(t, !hv("-a"));
    CHECK(t, !hv("a-"));
    CHECK(t, !hv("a.-b"));
    CHECK(t, !hv("a-.b"));
    CHECK(t, !hv("a.b-"));
    CHECK(t, !hv("-"));
    CHECK(t, !hv("a_b"));
    CHECK(t, !hv("a b"));
    CHECK(t, !hv("a/b"));
    CHECK(t, !hv("a:b"));
    CHECK(t, hv("0"
                "a"));
    CHECK(t, hv("9a"));
    CHECK(t, hv("Z"));
    CHECK(t, hv("z9"));
    CHECK(t, !hv("a@"));
    CHECK(t, !hv("a["));
    CHECK(t, !hv("a`"));
    CHECK(t, !hv("a{"));
    CHECK(t, !hv("a/"));
    CHECK(t, !hv("a:"));
    CHECK(t, !hv("a\x80"));
    CHECK(t, !hv("a,b"));
    CHECK(t, !hv("a+b"));
    CHECK(t, !http_host_valid((core_str_t){NULL, 3}));
    CHECK(t, !http_host_valid((core_str_t){NULL, 0}));
}

static void t_scheme(ctest_t *t) {
    split_is(t, "https://h/", HTTP_OK);
    split_is(t, "HTTPS://h/", HTTP_OK);
    split_is(t, "hTTPs://h/", HTTP_OK);
    split_is(t, "xttps://h/", HTTP_E_SCHEME);
    split_is(t, "hxtps://h/", HTTP_E_SCHEME);
    split_is(t, "htxps://h/", HTTP_E_SCHEME);
    split_is(t, "httxs://h/", HTTP_E_SCHEME);
    split_is(t, "httpx://h/", HTTP_E_SCHEME);
    split_is(t, "http://h/", HTTP_E_SCHEME);
    split_is(t, "https;//h/", HTTP_E_SCHEME);
    split_is(t, "https:x/h/", HTTP_E_SCHEME);
    split_is(t, "https:/xh/", HTTP_E_SCHEME);
    split_is(t, "https:\\\\h/", HTTP_E_SCHEME);
    split_is(t, "https:/", HTTP_E_SCHEME);
    split_is(t, "https:", HTTP_E_SCHEME);
    split_is(t, "https", HTTP_E_SCHEME);
    split_is(t, "", HTTP_E_SCHEME);
    split_is(t, "/", HTTP_E_SCHEME);
    split_is(t, "h/", HTTP_E_SCHEME);
    split_is(t, "ftp://h/", HTTP_E_SCHEME);
    split_is(t, "https//h/", HTTP_E_SCHEME);
    split_is(t, "https:///", HTTP_E_URL);
    split_is(t, "https://", HTTP_E_URL);
    split_is(t, "https://h", HTTP_E_URL);
    split_is(t, "https://h?x", HTTP_E_URL);
    split_is(t, "https://h#x", HTTP_E_URL);
    split_is(t, "http://h/\x01", HTTP_E_SCHEME);
    split_is(t, "https://h/\x01", HTTP_E_URL);
}

static void t_bytes_and_length(ctest_t *t) {
    char u[16];
    int c;
    for (c = 1; c < 256; c++) {
        bool bad = c <= 0x20 || c >= 0x7f || c == '\\';
        snprintf(u, sizeof u, "https://h/a%cb", c);
        u[11] = (char)c;
        split_is(t, u, bad ? HTTP_E_URL : HTTP_OK);
        snprintf(u, sizeof u, "https://h/?%c", c);
        u[10] = (char)c;
        split_is(t, u, bad ? HTTP_E_URL : HTTP_OK);
    }
    memset(g_big, 'a', sizeof g_big);
    memcpy(g_big, "https://h/", 10);
    g_big[HTTP_MAX_URL] = 0;
    split_is(t, g_big, HTTP_OK);
    g_big[HTTP_MAX_URL] = 'a';
    g_big[HTTP_MAX_URL + 1] = 0;
    split_is(t, g_big, HTTP_E_URL);
    g_big[HTTP_MAX_URL - 1] = 0;
    split_is(t, g_big, HTTP_OK);
    /* the byte check also covers the last byte */
    memset(g_big, 'a', sizeof g_big);
    memcpy(g_big, "https://h/", 10);
    g_big[HTTP_MAX_URL - 1] = ' ';
    g_big[HTTP_MAX_URL] = 0;
    split_is(t, g_big, HTTP_E_URL);
    /* length is checked before the authority: oversize with a bad host is still E_URL, scheme first */
    memset(g_big, 'a', sizeof g_big);
    memcpy(g_big, "http://h/", 9);
    g_big[HTTP_MAX_URL + 1] = 0;
    split_is(t, g_big, HTTP_E_SCHEME);
}

static void t_ports(ctest_t *t) {
    http_url_t u;
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:1/"), &u), HTTP_OK);
    CHECK_EQ_U64(t, u.port, 1);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:9/"), &u), HTTP_OK);
    CHECK_EQ_U64(t, u.port, 9);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:10/"), &u), HTTP_OK);
    CHECK_EQ_U64(t, u.port, 10);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:65535/"), &u), HTTP_OK);
    CHECK_EQ_U64(t, u.port, 65535);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:99999/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:65536/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:65540/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:70000/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:123456/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:0/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:01/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:1a/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:a1/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:1:2/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:/x:1"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://:80/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:80?x:1"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h/x:1"), &u), HTTP_OK);
    CHECK_EQ_U64(t, u.port, 443);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h/a@b"), &u), HTTP_OK);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h/a[b"), &u), HTTP_OK);
    CHECK_EQ_I64(t, http_url_split(LIT("https://u@h/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h:80@h/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://[::1]/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h[/"), &u), HTTP_E_URL);
}

CTEST_SUITE("http_survivor_url",
            { "octets", t_octets },
            { "labels", t_labels },
            { "scheme", t_scheme },
            { "bytes_and_length", t_bytes_and_length },
            { "ports", t_ports })
