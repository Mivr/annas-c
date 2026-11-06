/* Tests for http/http.h: http_is_redirect_status and http_redirect_resolve. Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/buf.h"
#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static char g_store[8192];
static core_buf_t g_b;
static http_url_t g_cur;
static http_redirects_t g_r;

static void fresh(void) {
    (void)core_buf_init(&g_b, g_store, sizeof g_store);
    memset(&g_r, 0, sizeof g_r);
}

/* g_cur := https://example.org/dir/sub/page?old=1 */
static void cur_default(void) { (void)http_url_split(LIT("https://example.org/dir/sub/page?old=1"), &g_cur); }

static http_status_t resolve(int status, const char *loc) {
    return http_redirect_resolve(&g_r, &g_cur, status, core_str_from_cstr(loc), &g_b);
}

/* Resolves with status 302 on a fresh buffer and counter; expects exactly `want`. */
static void ok(ctest_t *t, const char *loc, const char *want) {
    fresh();
    CHECK_EQ_I64(t, resolve(302, loc), HTTP_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&g_b), want);
    CHECK_EQ_U64(t, core_buf_len(&g_b), strlen(want));
    CHECK_EQ_U64(t, g_r.count, 1);
}

/* Expects `code`, nothing appended and the counter unchanged. */
static void bad(ctest_t *t, int status, const char *loc, http_status_t code) {
    fresh();
    core_buf_put(&g_b, "pre", 3);
    CHECK_EQ_I64(t, resolve(status, loc), code);
    CHECK_EQ_STR(t, core_buf_cstr(&g_b), "pre");
    CHECK_EQ_U64(t, g_r.count, 0);
}

static void t_status_set(ctest_t *t) {
    int s;
    for (s = 0; s < 1000; s++) {
        bool want = s == 301 || s == 302 || s == 303 || s == 307 || s == 308;
        CHECK(t, http_is_redirect_status(s) == want);
    }
    CHECK(t, !http_is_redirect_status(-301));
    CHECK(t, !http_is_redirect_status(301 + 65536));
    CHECK(t, !http_is_redirect_status(INT32_MAX));
    CHECK(t, !http_is_redirect_status(INT32_MIN));
}

static void t_status_gate(ctest_t *t) {
    int s;
    cur_default();
    for (s = 0; s < 1000; s++) {
        if (http_is_redirect_status(s)) {
            fresh();
            CHECK_EQ_I64(t, resolve(s, "/x"), HTTP_OK);
        } else {
            bad(t, s, "/x", HTTP_E_REDIRECT);
        }
    }
}

static void t_reference_forms(ctest_t *t) {
    cur_default();
    ok(t, "/abs", "https://example.org/abs");
    ok(t, "/abs?q=1", "https://example.org/abs?q=1");
    ok(t, "next", "https://example.org/dir/sub/next");
    ok(t, "next?q=1", "https://example.org/dir/sub/next?q=1");
    ok(t, "../up", "https://example.org/dir/up");
    ok(t, "../../../../x", "https://example.org/x");
    ok(t, "./x", "https://example.org/dir/sub/x");
    ok(t, ".", "https://example.org/dir/sub/");
    ok(t, "..", "https://example.org/dir/");
    ok(t, "../", "https://example.org/dir/");
    ok(t, "?q=2", "https://example.org/dir/sub/page?q=2");
    ok(t, "#f", "https://example.org/dir/sub/page?old=1");
    ok(t, "x?y=1#z", "https://example.org/dir/sub/x?y=1");
    ok(t, "/abs#frag", "https://example.org/abs");
    ok(t, "//other.net/p", "https://other.net/p");
    ok(t, "//other.net", "https://other.net/");
    ok(t, "//other.net:8443/p?q", "https://other.net:8443/p?q");
    ok(t, "https://other.net/p?q=1", "https://other.net/p?q=1");
    ok(t, "https://other.net", "https://other.net/");
    ok(t, "https://other.net?x=1", "https://other.net/?x=1");
    ok(t, "https://other.net:8443/a/../b?x", "https://other.net:8443/b?x");
    ok(t, "https://other.net:443/x", "https://other.net/x");
    ok(t, "HTTPS://Other.NET/x", "https://Other.NET/x");
    ok(t, "https://127.0.0.1:9000/p", "https://127.0.0.1:9000/p");
    ok(t, "/a%20b?c=%2F", "https://example.org/a%20b?c=%2F");
    ok(t, "https://other.net/a?b=c?d", "https://other.net/a?b=c?d");
}

static void t_current_without_query_and_with_port(ctest_t *t) {
    CHECK_EQ_I64(t, http_url_split(LIT("https://example.org:8443/dir/page"), &g_cur), HTTP_OK);
    ok(t, "#f", "https://example.org:8443/dir/page");
    ok(t, "?q", "https://example.org:8443/dir/page?q");
    ok(t, "/x", "https://example.org:8443/x");
    ok(t, "y", "https://example.org:8443/dir/y");
    ok(t, "//other.net/y", "https://other.net/y");
    CHECK_EQ_I64(t, http_url_split(LIT("https://example.org/"), &g_cur), HTTP_OK);
    ok(t, "y", "https://example.org/y");
    ok(t, "../y", "https://example.org/y");
    CHECK_EQ_I64(t, http_url_split(LIT("https://example.org/a"), &g_cur), HTTP_OK);
    ok(t, "b", "https://example.org/b");
    CHECK_EQ_I64(t, http_url_split(LIT("https://example.org/a/"), &g_cur), HTTP_OK);
    ok(t, "b", "https://example.org/a/b");
    CHECK_EQ_I64(t, http_url_split(LIT("https://example.org/a/b?x=/y/z"), &g_cur), HTTP_OK);
    ok(t, "c", "https://example.org/a/c"); /* the '/' inside the query is not a directory separator */
}

static void t_dot_segments(ctest_t *t) {
    cur_default();
    ok(t, "/a/b/c/..", "https://example.org/a/b/");
    ok(t, "/a/b/c/.", "https://example.org/a/b/c/");
    ok(t, "/a/./b", "https://example.org/a/b");
    ok(t, "/a/b/../c", "https://example.org/a/c");
    ok(t, "/a/b/../../c", "https://example.org/c");
    ok(t, "/a/b/../../../c", "https://example.org/c");
    ok(t, "/../a", "https://example.org/a");
    ok(t, "/..", "https://example.org/");
    ok(t, "/.", "https://example.org/");
    ok(t, "/./.", "https://example.org/");
    ok(t, "/a//b", "https://example.org/a//b");
    ok(t, "/a//../b", "https://example.org/a/b");
    ok(t, "/a/.hidden", "https://example.org/a/.hidden");
    ok(t, "/a/..x", "https://example.org/a/..x");
    ok(t, "/a/x../b", "https://example.org/a/x../b");
    ok(t, "/a/.../b", "https://example.org/a/.../b");
    ok(t, "/a/%2e%2e/b", "https://example.org/a/%2e%2e/b");
    ok(t, "/a/%2E/b", "https://example.org/a/%2E/b");
    ok(t, "/a/b/..?q=/../x", "https://example.org/a/?q=/../x");
    ok(t, "https://h/a/../../b", "https://h/b");
    ok(t, "//h/a/./b/..", "https://h/a/");
    ok(t, "/a/b/c/../../..", "https://example.org/");
    ok(t, "/a/b/c/../../../..", "https://example.org/");
}

static void t_scheme_errors(ctest_t *t) {
    cur_default();
    bad(t, 302, "http://example.org/x", HTTP_E_SCHEME);
    bad(t, 302, "HTTP://example.org/x", HTTP_E_SCHEME);
    bad(t, 302, "ftp://example.org/x", HTTP_E_SCHEME);
    bad(t, 302, "javascript:alert(1)", HTTP_E_SCHEME);
    bad(t, 302, "mailto:a@b.example", HTTP_E_SCHEME);
    bad(t, 302, "data:text/html,x", HTTP_E_SCHEME);
    bad(t, 302, "file:///etc/passwd", HTTP_E_SCHEME);
    bad(t, 302, "a+b.c-d:x", HTTP_E_SCHEME);
    bad(t, 302, "httpss://example.org/x", HTTP_E_SCHEME);
    bad(t, 302, "h:x", HTTP_E_SCHEME);
    /* a colon after a '/', '?' or '#' does not make a scheme */
    ok(t, "/a:b", "https://example.org/a:b");
    ok(t, "?a:b", "https://example.org/dir/sub/page?a:b");
    ok(t, "./a:b", "https://example.org/dir/sub/a:b");
    ok(t, "x/a:b", "https://example.org/dir/sub/x/a:b");
    ok(t, "x?a:b", "https://example.org/dir/sub/x?a:b");
    ok(t, "#a:b", "https://example.org/dir/sub/page?old=1");
}

static void t_url_errors(ctest_t *t) {
    cur_default();
    bad(t, 302, "https://user@example.org/x", HTTP_E_URL);
    bad(t, 302, "https://user:pw@example.org/x", HTTP_E_URL);
    bad(t, 302, "//user@example.org/x", HTTP_E_URL);
    bad(t, 302, "https://-bad.example/x", HTTP_E_URL);
    bad(t, 302, "https://bad_host.example/x", HTTP_E_URL);
    bad(t, 302, "https://1.2.3/x", HTTP_E_URL);
    bad(t, 302, "https://[::1]/x", HTTP_E_URL);
    bad(t, 302, "https://h:0/x", HTTP_E_URL);
    bad(t, 302, "https://h:65536/x", HTTP_E_URL);
    bad(t, 302, "https://h:/x", HTTP_E_URL);
    bad(t, 302, "https:///x", HTTP_E_URL);
    bad(t, 302, "https://", HTTP_E_URL);
    bad(t, 302, "https:/x", HTTP_E_URL);
    bad(t, 302, "https:x", HTTP_E_URL);
    bad(t, 302, "//", HTTP_E_URL);
    bad(t, 302, "a b", HTTP_E_URL);
    bad(t, 302, "/a b", HTTP_E_URL);
    bad(t, 302, "a\tb", HTTP_E_URL);
    bad(t, 302, "a\r\nb", HTTP_E_URL);
    bad(t, 302, "a\nb", HTTP_E_URL);
    bad(t, 302, "/a\x01", HTTP_E_URL);
    bad(t, 302, "/a\x7f", HTTP_E_URL);
    bad(t, 302, "/a\xc3\xa9", HTTP_E_URL);
    bad(t, 302, "/a\\b", HTTP_E_URL);
    bad(t, 302, "https://example.org\\@evil/", HTTP_E_URL);
    bad(t, 302, "\\\\evil\\x", HTTP_E_URL);
    bad(t, 302, "x#a b", HTTP_E_URL);
    fresh();
    CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, LIT("/a\0b"), &g_b), HTTP_E_URL);
    CHECK_EQ_U64(t, core_buf_len(&g_b), 0);
}

static void t_empty_location(ctest_t *t) {
    cur_default();
    bad(t, 302, "", HTTP_E_REDIRECT);
    fresh();
    CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, (core_str_t){NULL, 0}, &g_b), HTTP_E_REDIRECT);
    ok(t, "x", "https://example.org/dir/sub/x");
}

static void t_counter_limit(ctest_t *t) {
    unsigned i;
    cur_default();
    fresh();
    for (i = 1; i <= HTTP_MAX_REDIRECTS; i++) {
        core_buf_t b;
        char s[64];
        (void)core_buf_init(&b, s, sizeof s);
        CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, LIT("/n"), &b), HTTP_OK);
        CHECK_EQ_U64(t, g_r.count, i);
    }
    CHECK_EQ_U64(t, HTTP_MAX_REDIRECTS, 5);
    {
        core_buf_t b;
        char s[64];
        (void)core_buf_init(&b, s, sizeof s);
        CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, LIT("/n"), &b), HTTP_E_TOO_MANY_REDIRECTS);
        CHECK_EQ_U64(t, core_buf_len(&b), 0);
        CHECK_EQ_U64(t, g_r.count, HTTP_MAX_REDIRECTS);
        CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, LIT("/n"), &b), HTTP_E_TOO_MANY_REDIRECTS);
        CHECK_EQ_U64(t, g_r.count, HTTP_MAX_REDIRECTS);
        /* a non-redirect status at the limit is still HTTP_E_REDIRECT (checked first) */
        CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 200, LIT("/n"), &b), HTTP_E_REDIRECT);
        CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, LIT(""), &b), HTTP_E_REDIRECT);
    }
}

static void t_counter_only_counts_successes(ctest_t *t) {
    cur_default();
    fresh();
    CHECK_EQ_I64(t, resolve(200, "/x"), HTTP_E_REDIRECT);
    CHECK_EQ_I64(t, resolve(302, ""), HTTP_E_REDIRECT);
    CHECK_EQ_I64(t, resolve(302, "http://x/"), HTTP_E_SCHEME);
    CHECK_EQ_I64(t, resolve(302, "a b"), HTTP_E_URL);
    CHECK_EQ_U64(t, g_r.count, 0);
    CHECK_EQ_I64(t, resolve(301, "/x"), HTTP_OK);
    CHECK_EQ_U64(t, g_r.count, 1);
    /* an overflow does not count either */
    {
        core_buf_t tiny;
        char s[4];
        (void)core_buf_init(&tiny, s, sizeof s);
        CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, LIT("/long/path"), &tiny), HTTP_E_OVERFLOW);
        CHECK_EQ_U64(t, g_r.count, 1);
    }
    g_r.count = HTTP_MAX_REDIRECTS - 1;
    CHECK_EQ_I64(t, resolve(308, "/y"), HTTP_OK);
    CHECK_EQ_I64(t, resolve(308, "/y"), HTTP_E_TOO_MANY_REDIRECTS);
}

static void t_output_appends_and_overflows(ctest_t *t) {
    const char *want = "https://example.org/abc";
    size_t n = strlen(want);
    static char small[64];
    core_buf_t b;
    cur_default();
    fresh();
    core_buf_put(&g_b, "xx", 2);
    CHECK_EQ_I64(t, resolve(302, "/abc"), HTTP_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&g_b), "xxhttps://example.org/abc");
    CHECK(t, core_buf_init(&b, small, n + 1));
    memset(&g_r, 0, sizeof g_r);
    CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, LIT("/abc"), &b), HTTP_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&b), want);
    CHECK(t, core_buf_init(&b, small, n));
    memset(&g_r, 0, sizeof g_r);
    CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, LIT("/abc"), &b), HTTP_E_OVERFLOW);
    CHECK(t, core_buf_overflow(&b));
    CHECK_EQ_U64(t, g_r.count, 0);
    CHECK(t, core_buf_init(&b, NULL, 0));
    CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, LIT("/abc"), &b), HTTP_E_OVERFLOW);
    /* a bad location is reported as such even when the buffer is too small */
    CHECK(t, core_buf_init(&b, small, 3));
    CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, LIT("http://x/"), &b), HTTP_E_SCHEME);
}

static void t_resolved_url_splits_back(ctest_t *t) {
    static const char *const locs[] = {"/a/b?c", "d/e", "../f", "//o.example:8443/g?h", "https://p.example/q/../r", "?z", "#y"};
    size_t i;
    cur_default();
    for (i = 0; i < sizeof locs / sizeof locs[0]; i++) {
        http_url_t u;
        fresh();
        CHECK_EQ_I64(t, resolve(307, locs[i]), HTTP_OK);
        CHECK_EQ_I64(t, http_url_split(core_buf_str(&g_b), &u), HTTP_OK);
        CHECK(t, u.target.len > 0 && u.target.ptr[0] == '/');
    }
}

static void t_args(ctest_t *t) {
    cur_default();
    fresh();
    CHECK_EQ_I64(t, http_redirect_resolve(NULL, &g_cur, 302, LIT("/x"), &g_b), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_redirect_resolve(&g_r, NULL, 302, LIT("/x"), &g_b), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, LIT("/x"), NULL), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, (core_str_t){NULL, 2}, &g_b), HTTP_E_ARG);
    CHECK_EQ_U64(t, g_r.count, 0);
    CHECK_EQ_U64(t, core_buf_len(&g_b), 0);
    CHECK_EQ_I64(t, resolve(302, "/x"), HTTP_OK);
}

CTEST_SUITE("http_redirect",
            { "status_set", t_status_set },
            { "status_gate", t_status_gate },
            { "reference_forms", t_reference_forms },
            { "current_without_query_and_with_port", t_current_without_query_and_with_port },
            { "dot_segments", t_dot_segments },
            { "scheme_errors", t_scheme_errors },
            { "url_errors", t_url_errors },
            { "empty_location", t_empty_location },
            { "counter_limit", t_counter_limit },
            { "counter_only_counts_successes", t_counter_only_counts_successes },
            { "output_appends_and_overflows", t_output_appends_and_overflows },
            { "resolved_url_splits_back", t_resolved_url_splits_back },
            { "args", t_args })
