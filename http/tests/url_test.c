/* Tests for http/http.h: http_host_valid and http_url_split. Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static char g_big[HTTP_MAX_URL + 64];

static bool hv(const char *s) { return http_host_valid(core_str_from_cstr(s)); }

static bool eq(core_str_t s, const char *want) { return core_str_eq(s, core_str_from_cstr(want)); }

/* Splits `url` expecting success with the given parts. */
static void ok(ctest_t *t, const char *url, const char *host, unsigned port, const char *target) {
    http_url_t u;
    memset(&u, 0x5a, sizeof u);
    CHECK_EQ_I64(t, http_url_split(core_str_from_cstr(url), &u), HTTP_OK);
    CHECK(t, eq(u.host, host));
    CHECK_EQ_U64(t, u.port, port);
    CHECK(t, eq(u.target, target));
}

/* Splits `url` expecting `code`, and that *out is untouched. */
static void bad(ctest_t *t, core_str_t url, http_status_t code) {
    http_url_t u, before;
    memset(&u, 0x5a, sizeof u);
    before = u;
    CHECK_EQ_I64(t, http_url_split(url, &u), code);
    CHECK(t, memcmp(&u, &before, sizeof u) == 0);
}

static void badc(ctest_t *t, const char *url, http_status_t code) { bad(t, core_str_from_cstr(url), code); }

static void t_split_basic(ctest_t *t) {
    ok(t, "https://example.org/", "example.org", 443, "/");
    ok(t, "https://annas-archive.gl/dyn/api/fast_download.json?md5=abc&key=k", "annas-archive.gl", 443,
       "/dyn/api/fast_download.json?md5=abc&key=k");
    ok(t, "https://example.org:8443/a/b?c=d", "example.org", 8443, "/a/b?c=d");
    ok(t, "https://example.org:443/x", "example.org", 443, "/x");
    ok(t, "https://example.org:1/x", "example.org", 1, "/x");
    ok(t, "https://example.org:65535/x", "example.org", 65535, "/x");
    ok(t, "https://127.0.0.1:9000/p", "127.0.0.1", 9000, "/p");
    ok(t, "https://Example.ORG/Path", "Example.ORG", 443, "/Path");
    ok(t, "https://h/?", "h", 443, "/?");
    ok(t, "https://h//double//slash", "h", 443, "//double//slash");
    ok(t, "https://h/a?b?c=d/e", "h", 443, "/a?b?c=d/e");
    ok(t, "https://h/p%20q", "h", 443, "/p%20q");
}

static void t_split_scheme_case(ctest_t *t) {
    ok(t, "HTTPS://h/x", "h", 443, "/x");
    ok(t, "HtTpS://h/x", "h", 443, "/x");
}

static void t_split_slices_point_into_input(ctest_t *t) {
    static const char in[] = "https://ex.org:81/q?z";
    http_url_t u;
    CHECK_EQ_I64(t, http_url_split((core_str_t){in, sizeof in - 1}, &u), HTTP_OK);
    CHECK(t, u.host.ptr == in + 8 && u.host.len == 6);
    CHECK(t, u.target.ptr == in + 8 + 6 + 3 && u.target.len == 4);
}

static void t_split_fragment_dropped(ctest_t *t) {
    ok(t, "https://h/p?q#frag", "h", 443, "/p?q");
    ok(t, "https://h/#", "h", 443, "/");
    ok(t, "https://h/a#b#c", "h", 443, "/a");
}

static void t_split_scheme_errors(ctest_t *t) {
    badc(t, "http://example.org/", HTTP_E_SCHEME);
    badc(t, "HTTP://example.org/", HTTP_E_SCHEME);
    badc(t, "ftp://example.org/", HTTP_E_SCHEME);
    badc(t, "//example.org/", HTTP_E_SCHEME);
    badc(t, "example.org/", HTTP_E_SCHEME);
    badc(t, "", HTTP_E_SCHEME);
    badc(t, "https:/example.org/", HTTP_E_SCHEME);
    badc(t, "https:example.org/", HTTP_E_SCHEME);
    badc(t, "httpss://example.org/", HTTP_E_SCHEME);
    badc(t, " https://example.org/", HTTP_E_SCHEME);
    badc(t, "javascript:alert(1)", HTTP_E_SCHEME);
    /* scheme wins over a bad host / bad bytes */
    badc(t, "http://bad host/", HTTP_E_SCHEME);
    ok(t, "https://example.org/", "example.org", 443, "/");
}

static void t_split_userinfo(ctest_t *t) {
    badc(t, "https://user@example.org/", HTTP_E_URL);
    badc(t, "https://user:pw@example.org/", HTTP_E_URL);
    badc(t, "https://@example.org/", HTTP_E_URL);
    badc(t, "https://example.org@evil.example/", HTTP_E_URL);
    badc(t, "https://example.org:443@evil.example/", HTTP_E_URL);
    /* '@' after the authority is part of the target and fine */
    ok(t, "https://example.org/u@x", "example.org", 443, "/u@x");
    ok(t, "https://example.org/?q=a@b", "example.org", 443, "/?q=a@b");
}

static void t_split_bad_bytes(ctest_t *t) {
    badc(t, "https://exa mple.org/", HTTP_E_URL);
    badc(t, "https://example.org/a b", HTTP_E_URL);
    badc(t, "https://example.org/a\tb", HTTP_E_URL);
    badc(t, "https://example.org/a\rb", HTTP_E_URL);
    badc(t, "https://example.org/a\nb", HTTP_E_URL);
    badc(t, "https://example.org/a\x01", HTTP_E_URL);
    badc(t, "https://example.org/a\x7f", HTTP_E_URL);
    badc(t, "https://example.org/a\xc3\xa9", HTTP_E_URL);
    badc(t, "https://example.org/a\xff", HTTP_E_URL);
    badc(t, "https://example.org\\@evil/", HTTP_E_URL);
    badc(t, "https://example.org/a\\b", HTTP_E_URL);
    badc(t, "https://example.org/x#a b", HTTP_E_URL);
    bad(t, LIT("https://example.org/a\0b"), HTTP_E_URL);
    bad(t, LIT("https://exam\0ple.org/"), HTTP_E_URL);
    ok(t, "https://example.org/a~b-c_d.e!$&'()*+,;=:@", "example.org", 443, "/a~b-c_d.e!$&'()*+,;=:@");
}

static void t_split_host_errors(ctest_t *t) {
    badc(t, "https:///x", HTTP_E_URL);
    badc(t, "https://", HTTP_E_URL);
    badc(t, "https://:443/x", HTTP_E_URL);
    badc(t, "https://[::1]/x", HTTP_E_URL);
    badc(t, "https://[::1]:443/x", HTTP_E_URL);
    badc(t, "https://a_b.org/x", HTTP_E_URL);
    badc(t, "https://-a.org/x", HTTP_E_URL);
    badc(t, "https://a-.org/x", HTTP_E_URL);
    badc(t, "https://a..org/x", HTTP_E_URL);
    badc(t, "https://.org/x", HTTP_E_URL);
    badc(t, "https://org./x", HTTP_E_URL);
    badc(t, "https://a%2eorg/x", HTTP_E_URL);
    badc(t, "https://1.2.3/x", HTTP_E_URL);
    badc(t, "https://256.1.1.1/x", HTTP_E_URL);
    ok(t, "https://255.1.1.1/x", "255.1.1.1", 443, "/x");
}

static void t_split_no_path(ctest_t *t) {
    badc(t, "https://example.org", HTTP_E_URL);
    badc(t, "https://example.org?x=1", HTTP_E_URL);
    badc(t, "https://example.org#f", HTTP_E_URL);
    badc(t, "https://example.org:443", HTTP_E_URL);
    ok(t, "https://example.org/", "example.org", 443, "/");
}

static void t_split_port(ctest_t *t) {
    badc(t, "https://h:/x", HTTP_E_URL);
    badc(t, "https://h:0/x", HTTP_E_URL);
    badc(t, "https://h:00443/x", HTTP_E_URL);
    badc(t, "https://h:0443/x", HTTP_E_URL);
    badc(t, "https://h:65536/x", HTTP_E_URL);
    badc(t, "https://h:99999/x", HTTP_E_URL);
    badc(t, "https://h:100000/x", HTTP_E_URL);
    badc(t, "https://h:4294967297/x", HTTP_E_URL); /* wraps to 1 in 32 bits */
    badc(t, "https://h:80a/x", HTTP_E_URL);
    badc(t, "https://h:+80/x", HTTP_E_URL);
    badc(t, "https://h:-1/x", HTTP_E_URL);
    badc(t, "https://h:8 0/x", HTTP_E_URL);
    badc(t, "https://h:80:80/x", HTTP_E_URL);
    ok(t, "https://h:65535/x", "h", 65535, "/x");
    ok(t, "https://h:10/x", "h", 10, "/x");
}

static void t_split_url_length(ctest_t *t) {
    static const char pre[] = "https://h/";
    const size_t pl = sizeof pre - 1;
    http_url_t u;
    size_t n;
    memcpy(g_big, pre, pl);
    memset(g_big + pl, 'a', sizeof g_big - pl);
    n = HTTP_MAX_URL - 1;
    bad(t, (core_str_t){g_big, n + 2}, HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split((core_str_t){g_big, n}, &u), HTTP_OK);
    CHECK_EQ_I64(t, http_url_split((core_str_t){g_big, HTTP_MAX_URL}, &u), HTTP_OK);
    CHECK_EQ_U64(t, u.target.len, HTTP_MAX_URL - 8 - 1);
    bad(t, (core_str_t){g_big, HTTP_MAX_URL + 1}, HTTP_E_URL);
    bad(t, (core_str_t){g_big, HTTP_MAX_URL + 40}, HTTP_E_URL);
}

static void t_split_args(ctest_t *t) {
    http_url_t u;
    CHECK_EQ_I64(t, http_url_split(LIT("https://h/"), NULL), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_url_split((core_str_t){NULL, 5}, &u), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_url_split((core_str_t){NULL, 0}, &u), HTTP_E_SCHEME);
}

static void t_host_dns(ctest_t *t) {
    CHECK(t, hv("example.org"));
    CHECK(t, hv("localhost"));
    CHECK(t, hv("a"));
    CHECK(t, hv("A-b.C-d.example"));
    CHECK(t, hv("xn--bcher-kva.example"));
    CHECK(t, hv("a1.b2"));
    CHECK(t, hv("1a.example"));
    CHECK(t, hv("1.2.3.4a"));
    CHECK(t, !hv(""));
    CHECK(t, !hv("."));
    CHECK(t, !hv(".example.org"));
    CHECK(t, !hv("example.org."));
    CHECK(t, !hv("a..b"));
    CHECK(t, !hv("-a"));
    CHECK(t, !hv("a-"));
    CHECK(t, !hv("a-.b"));
    CHECK(t, !hv("a.-b"));
    CHECK(t, !hv("a_b"));
    CHECK(t, !hv("a b"));
    CHECK(t, !hv("a/b"));
    CHECK(t, !hv("a:80"));
    CHECK(t, !hv("a@b"));
    CHECK(t, !hv("[::1]"));
    CHECK(t, !hv("a\xc3\xa9"));
    CHECK(t, !http_host_valid(LIT("a\0b")));
    CHECK(t, !http_host_valid((core_str_t){NULL, 0}));
}

static core_str_t build_host(const size_t *labels, size_t n) {
    size_t i, off = 0, k;
    for (i = 0; i < n; i++) {
        if (i > 0) {
            g_big[off++] = '.';
        }
        for (k = 0; k < labels[i]; k++) {
            g_big[off++] = 'a';
        }
    }
    return (core_str_t){g_big, off};
}

static void t_host_label_and_total_length(ctest_t *t) {
    size_t one[1] = {63}, two[1] = {64}, a253[4] = {63, 63, 63, 61}, a254[4] = {63, 63, 63, 62};
    size_t a255[4] = {63, 63, 63, 63};
    CHECK(t, http_host_valid(build_host(one, 1)));
    CHECK(t, !http_host_valid(build_host(two, 1)));
    CHECK_EQ_U64(t, build_host(a253, 4).len, 253);
    CHECK(t, http_host_valid(build_host(a253, 4)));
    CHECK_EQ_U64(t, build_host(a254, 4).len, 254);
    CHECK(t, !http_host_valid(build_host(a254, 4)));
    CHECK(t, !http_host_valid(build_host(a255, 4)));
}

static void t_host_ipv4(ctest_t *t) {
    CHECK(t, hv("1.2.3.4"));
    CHECK(t, hv("0.0.0.0"));
    CHECK(t, hv("255.255.255.255"));
    CHECK(t, hv("192.168.0.10"));
    CHECK(t, hv("10.0.0.1"));
    CHECK(t, !hv("256.0.0.1"));
    CHECK(t, !hv("1.2.3.256"));
    CHECK(t, !hv("1.2.3.999"));
    CHECK(t, !hv("1.2.3"));
    CHECK(t, !hv("1.2"));
    CHECK(t, !hv("1"));
    CHECK(t, !hv("123"));
    CHECK(t, !hv("1.2.3.4.5"));
    CHECK(t, !hv("01.2.3.4"));
    CHECK(t, !hv("1.2.3.04"));
    CHECK(t, !hv("1.2.3.00"));
    CHECK(t, !hv("0x7f.0.0.1"));
    CHECK(t, !hv("0x7f.0.0.0x1"));
    CHECK(t, !hv("example.123"));
    CHECK(t, !hv("2130706433"));
    CHECK(t, !hv("1.2.3.-4"));
    CHECK(t, !hv("0x1"));
    CHECK(t, !hv("a.0x1f"));
    CHECK(t, !hv("a.0X1F"));
    CHECK(t, !hv("1.2.3.0x4"));
    CHECK(t, hv("a.0x"));
    CHECK(t, hv("a.0xg"));
    CHECK(t, hv("a.0x1g"));
    CHECK(t, hv("a.x1"));
    CHECK(t, hv("0x1.example"));
}

CTEST_SUITE("http_url",
            { "split_basic", t_split_basic },
            { "split_scheme_case", t_split_scheme_case },
            { "split_slices_point_into_input", t_split_slices_point_into_input },
            { "split_fragment_dropped", t_split_fragment_dropped },
            { "split_scheme_errors", t_split_scheme_errors },
            { "split_userinfo", t_split_userinfo },
            { "split_bad_bytes", t_split_bad_bytes },
            { "split_host_errors", t_split_host_errors },
            { "split_no_path", t_split_no_path },
            { "split_port", t_split_port },
            { "split_url_length", t_split_url_length },
            { "split_args", t_split_args },
            { "host_dns", t_host_dns },
            { "host_label_and_total_length", t_host_label_and_total_length },
            { "host_ipv4", t_host_ipv4 })
