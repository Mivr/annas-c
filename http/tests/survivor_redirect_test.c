/* Tests for http/http.h: http_redirect_resolve argument checks, error precedence and reference forms that pin the
 * exact result of each branch. Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/buf.h"
#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static char g_store[4096];
static core_buf_t g_b;
static http_url_t g_cur;
static http_redirects_t g_r;

static void fresh(const char *cur) {
    (void)core_buf_init(&g_b, g_store, sizeof g_store);
    memset(&g_r, 0, sizeof g_r);
    (void)http_url_split(core_str_from_cstr(cur), &g_cur);
}

static http_status_t res(int status, core_str_t loc) { return http_redirect_resolve(&g_r, &g_cur, status, loc, &g_b); }

static void ok(ctest_t *t, const char *cur, const char *loc, const char *want) {
    fresh(cur);
    CHECK_EQ_I64(t, res(302, core_str_from_cstr(loc)), HTTP_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&g_b), want);
    CHECK_EQ_U64(t, g_r.count, 1);
}

static void bad(ctest_t *t, const char *cur, const char *loc, http_status_t code) {
    fresh(cur);
    CHECK_EQ_I64(t, res(302, core_str_from_cstr(loc)), code);
    CHECK_EQ_U64(t, core_buf_len(&g_b), 0);
    CHECK_EQ_U64(t, g_r.count, 0);
}

static void t_args(ctest_t *t) {
    http_url_t cur;
    http_redirects_t r = {0};
    core_buf_t b;
    char store[256];
    (void)http_url_split(LIT("https://h/p"), &cur);
    (void)core_buf_init(&b, store, sizeof store);
    CHECK_EQ_I64(t, http_redirect_resolve(NULL, &cur, 302, LIT("/x"), &b), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_redirect_resolve(&r, NULL, 302, LIT("/x"), &b), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 302, LIT("/x"), NULL), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_redirect_resolve(NULL, NULL, 302, LIT("/x"), NULL), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 302, (core_str_t){NULL, 1}, &b), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 302, (core_str_t){NULL, 5}, &b), HTTP_E_ARG);
    /* ARG outranks every other error */
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 200, (core_str_t){NULL, 1}, &b), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_redirect_resolve(NULL, &cur, 200, LIT(""), &b), HTTP_E_ARG);
    cur.host = (core_str_t){NULL, 3};
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 302, LIT("/x"), &b), HTTP_E_ARG);
    (void)http_url_split(LIT("https://h/p"), &cur);
    cur.target = (core_str_t){NULL, 2};
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 302, LIT("/x"), &b), HTTP_E_ARG);
    CHECK_EQ_U64(t, r.count, 0);
    CHECK_EQ_U64(t, core_buf_len(&b), 0);
    /* a NULL pointer with length zero is an empty string, not an argument error */
    (void)http_url_split(LIT("https://h/p"), &cur);
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 302, (core_str_t){NULL, 0}, &b), HTTP_E_REDIRECT);
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 302, (core_str_t){"abc", 0}, &b), HTTP_E_REDIRECT);
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 200, (core_str_t){"abc", 0}, &b), HTTP_E_REDIRECT);
}

static void t_empty_host_or_target_with_pointer(ctest_t *t) {
    http_url_t cur;
    http_redirects_t r = {0};
    core_buf_t b;
    char store[256];
    (void)http_url_split(LIT("https://h/p"), &cur);
    cur.host = (core_str_t){"h", 0};
    (void)core_buf_init(&b, store, sizeof store);
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 302, LIT("/x"), &b), HTTP_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&b), "https:///x");
    (void)http_url_split(LIT("https://h/p"), &cur);
    cur.host = (core_str_t){NULL, 0};
    (void)core_buf_init(&b, store, sizeof store);
    r.count = 0;
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 302, LIT("/x"), &b), HTTP_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&b), "https:///x");
    (void)http_url_split(LIT("https://h/p"), &cur);
    cur.target = (core_str_t){"/p", 0};
    (void)core_buf_init(&b, store, sizeof store);
    r.count = 0;
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 302, LIT("/x"), &b), HTTP_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&b), "https://h/x");
    cur.target = (core_str_t){NULL, 0};
    (void)core_buf_init(&b, store, sizeof store);
    r.count = 0;
    CHECK_EQ_I64(t, http_redirect_resolve(&r, &cur, 302, LIT("/x"), &b), HTTP_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&b), "https://h/x");
}

static void t_counter_and_order(ctest_t *t) {
    unsigned c;
    fresh("https://h/a");
    for (c = 0; c < HTTP_MAX_REDIRECTS; c++) {
        CHECK_EQ_U64(t, g_r.count, c);
        CHECK_EQ_I64(t, res(301, LIT("/b")), HTTP_OK);
    }
    CHECK_EQ_U64(t, g_r.count, HTTP_MAX_REDIRECTS);
    CHECK_EQ_I64(t, res(301, LIT("/b")), HTTP_E_TOO_MANY_REDIRECTS);
    CHECK_EQ_U64(t, g_r.count, HTTP_MAX_REDIRECTS);
    g_r.count = HTTP_MAX_REDIRECTS - 1u;
    (void)core_buf_init(&g_b, g_store, sizeof g_store);
    CHECK_EQ_I64(t, res(301, LIT("/b")), HTTP_OK);
    /* REDIRECT outranks TOO_MANY, TOO_MANY outranks SCHEME and URL */
    g_r.count = HTTP_MAX_REDIRECTS;
    CHECK_EQ_I64(t, res(200, LIT("/b")), HTTP_E_REDIRECT);
    CHECK_EQ_I64(t, res(301, LIT("")), HTTP_E_REDIRECT);
    CHECK_EQ_I64(t, res(301, LIT("ftp://x/")), HTTP_E_TOO_MANY_REDIRECTS);
    CHECK_EQ_I64(t, res(301, LIT("//h:0/")), HTTP_E_TOO_MANY_REDIRECTS);
    g_r.count = 0;
    CHECK_EQ_I64(t, res(301, LIT("ftp://x/")), HTTP_E_SCHEME);
    CHECK_EQ_I64(t, res(301, LIT("//h:0/")), HTTP_E_URL);
    /* an overflowed buffer reports OVERFLOW only after the reference parsed */
    (void)core_buf_init(&g_b, g_store, 4);
    core_buf_put_str(&g_b, LIT("abcdefgh"));
    CHECK(t, core_buf_overflow(&g_b));
    CHECK_EQ_I64(t, res(301, LIT("/b")), HTTP_E_OVERFLOW);
    CHECK_EQ_I64(t, res(301, LIT("ftp://x")), HTTP_E_SCHEME);
    CHECK_EQ_I64(t, res(301, LIT("//h:0")), HTTP_E_URL);
    CHECK_EQ_U64(t, g_r.count, 0);
}

static void t_scheme_forms(ctest_t *t) {
    static const char *const schemes[] = {"ftp:", "h:", "a1:", "a+:", "a-:", "a.:", "A:", "javascript:alert(1)",
                                          "mailto:x@y", "httpss://h/", "http://h/", "https-x://h/", "https+://h/",
                                          "https.://h/", "https1://h/", "data:text/plain,x", "x:/", "x:?", "x:#"};
    static const char *const relative[] = {"1:x", "_:x", "-:x", "+:x", ".:x", ":x", "a_b:x", "a b:x", "a/b:c", "a?b:c",
                                           "a#b:c", "/a:b", "?a:b", "#a:b"};
    size_t i;
    for (i = 0; i < sizeof schemes / sizeof schemes[0]; i++) {
        bad(t, "https://h/d/p", schemes[i], HTTP_E_SCHEME);
    }
    ok(t, "https://h/d/p", "1:x", "https://h/d/1:x");
    ok(t, "https://h/d/p", "_:x", "https://h/d/_:x");
    ok(t, "https://h/d/p", "-:x", "https://h/d/-:x");
    ok(t, "https://h/d/p", "+:x", "https://h/d/+:x");
    ok(t, "https://h/d/p", ".:x", "https://h/d/.:x");
    ok(t, "https://h/d/p", ":x", "https://h/d/:x");
    ok(t, "https://h/d/p", "a_b:x", "https://h/d/a_b:x");
    ok(t, "https://h/d/p", "a/b:c", "https://h/d/a/b:c");
    ok(t, "https://h/d/p", "a?b:c", "https://h/d/a?b:c");
    ok(t, "https://h/d/p", "a#b:c", "https://h/d/a");
    ok(t, "https://h/d/p", "/a:b", "https://h/a:b");
    ok(t, "https://h/d/p", "?a:b", "https://h/d/p?a:b");
    ok(t, "https://h/d/p", "#a:b", "https://h/d/p");
    (void)relative;
    ok(t, "https://h/d/p", "https://o/x", "https://o/x");
    ok(t, "https://h/d/p", "HTTPS://o/x", "https://o/x");
    ok(t, "https://h/d/p", "hTtPs://o/x", "https://o/x");
    bad(t, "https://h/d/p", "https:", HTTP_E_URL);
    bad(t, "https://h/d/p", "https:/", HTTP_E_URL);
    bad(t, "https://h/d/p", "https:/x", HTTP_E_URL);
    bad(t, "https://h/d/p", "https:x/x", HTTP_E_URL);
    bad(t, "https://h/d/p", "https:\\\\o/", HTTP_E_URL);
    bad(t, "https://h/d/p", "https:///x", HTTP_E_URL);
    bad(t, "https://h/d/p", "https://", HTTP_E_URL);
    bad(t, "https://h/d/p", "//", HTTP_E_URL);
    bad(t, "https://h/d/p", "///x", HTTP_E_URL);
    bad(t, "https://h/d/p", "//u@o/x", HTTP_E_URL);
    bad(t, "https://h/d/p", "//o:0/x", HTTP_E_URL);
    bad(t, "https://h/d/p", "//o:65536/x", HTTP_E_URL);
    bad(t, "https://h/d/p", "//[::1]/x", HTTP_E_URL);
    bad(t, "https://h/d/p", "//-o/x", HTTP_E_URL);
    bad(t, "https://h/d/p", "//1.2.3/x", HTTP_E_URL);
    bad(t, "https://h/d/p", "/a b", HTTP_E_URL);
    bad(t, "https://h/d/p", "/a\\b", HTTP_E_URL);
    bad(t, "https://h/d/p", "/a\x7f", HTTP_E_URL);
    bad(t, "https://h/d/p", "/a\x80", HTTP_E_URL);
    bad(t, "https://h/d/p", "/a\x01", HTTP_E_URL);
    bad(t, "https://h/d/p", "a b", HTTP_E_URL);
    bad(t, "https://h/d/p", "?a b", HTTP_E_URL);
    bad(t, "https://h/d/p", "#a b", HTTP_E_URL);
    bad(t, "https://h/d/p", "x#\x01", HTTP_E_URL);
}

static void t_path_bases(ctest_t *t) {
    ok(t, "https://h/", "x", "https://h/x");
    ok(t, "https://h/a", "x", "https://h/x");
    ok(t, "https://h/a/", "x", "https://h/a/x");
    ok(t, "https://h/a/b", "x", "https://h/a/x");
    ok(t, "https://h/a/b/", "x", "https://h/a/b/x");
    ok(t, "https://h//", "x", "https://h//x");
    ok(t, "https://h//a", "x", "https://h//x");
    ok(t, "https://h/a//b", "x", "https://h/a//x");
    ok(t, "https://h/a/b?q=/z", "x", "https://h/a/x");
    ok(t, "https://h/a/b?q", "?r", "https://h/a/b?r");
    ok(t, "https://h/a/b?q", "?", "https://h/a/b?");
    ok(t, "https://h/a/b?q", "#f", "https://h/a/b?q");
    ok(t, "https://h/a/b?", "#f", "https://h/a/b?");
    ok(t, "https://h/a/b", "#f", "https://h/a/b");
    ok(t, "https://h/a/b", "?r", "https://h/a/b?r");
    ok(t, "https://h/a/b", "x?", "https://h/a/x?");
    ok(t, "https://h/a/b", "x?y#z", "https://h/a/x?y");
    ok(t, "https://h/a/b", "/?y", "https://h/?y");
    ok(t, "https://h/a/b", "/", "https://h/");
    ok(t, "https://h:8443/a/b", "x", "https://h:8443/a/x");
    ok(t, "https://h:8443/a/b", "//o/x", "https://o/x");
    ok(t, "https://h:8443/a/b", "//o:8443/x", "https://o:8443/x");
    ok(t, "https://h:8443/a/b", "//o:443/x", "https://o/x");
    ok(t, "https://h:8443/a/b", "//o:444/x", "https://o:444/x");
    ok(t, "https://h:8443/a/b", "//o:1/x", "https://o:1/x");
    ok(t, "https://h:8443/a/b", "//o:65535/x", "https://o:65535/x");
    ok(t, "https://h:8443/a/b", "//o", "https://o/");
    ok(t, "https://h:8443/a/b", "//o?q", "https://o/?q");
    ok(t, "https://h:8443/a/b", "//o#f", "https://o/");
    ok(t, "https://h:8443/a/b", "//o/", "https://o/");
}

static void t_dots(ctest_t *t) {
    ok(t, "https://h/a/b/c", "..", "https://h/a/");
    ok(t, "https://h/a/b/c", "../", "https://h/a/");
    ok(t, "https://h/a/b/c", "../..", "https://h/");
    ok(t, "https://h/a/b/c", "../../..", "https://h/");
    ok(t, "https://h/a/b/c", "../../../..", "https://h/");
    ok(t, "https://h/a/b/c", "../x", "https://h/a/x");
    ok(t, "https://h/a/b/c", "../../x", "https://h/x");
    ok(t, "https://h/a/b/c", "../../../x", "https://h/x");
    ok(t, "https://h/a/b/c", ".", "https://h/a/b/");
    ok(t, "https://h/a/b/c", "./", "https://h/a/b/");
    ok(t, "https://h/a/b/c", "./x", "https://h/a/b/x");
    ok(t, "https://h/a/b/c", "./../x", "https://h/a/x");
    ok(t, "https://h/a/b/c", "x/./y", "https://h/a/b/x/y");
    ok(t, "https://h/a/b/c", "x/../y", "https://h/a/b/y");
    ok(t, "https://h/a/b/c", "x/y/../../z", "https://h/a/b/z");
    ok(t, "https://h/a/b/c", "x/y/../../../z", "https://h/a/z");
    ok(t, "https://h/a/b/c", "x/..", "https://h/a/b/");
    ok(t, "https://h/a/b/c", "x/.", "https://h/a/b/x/");
    ok(t, "https://h/a/b/c", "/x/./y/../z", "https://h/x/z");
    ok(t, "https://h/a/b/c", "/..", "https://h/");
    ok(t, "https://h/a/b/c", "/.", "https://h/");
    ok(t, "https://h/a/b/c", "/../x", "https://h/x");
    ok(t, "https://h/a/b/c", "/a/../../x", "https://h/x");
    ok(t, "https://h/a/b/c", "/a//../x", "https://h/a/x");
    ok(t, "https://h/a/b/c", "/a//x", "https://h/a//x");
    ok(t, "https://h/a/b/c", "/a/", "https://h/a/");
    ok(t, "https://h/a/b/c", "/a//", "https://h/a//");
    ok(t, "https://h/a/b/c", "/.a", "https://h/.a");
    ok(t, "https://h/a/b/c", "/a.", "https://h/a.");
    ok(t, "https://h/a/b/c", "/...", "https://h/...");
    ok(t, "https://h/a/b/c", "/.../x", "https://h/.../x");
    ok(t, "https://h/a/b/c", "/..a", "https://h/..a");
    ok(t, "https://h/a/b/c", "/a..", "https://h/a..");
    ok(t, "https://h/a/b/c", "/%2e%2e/x", "https://h/%2e%2e/x");
    ok(t, "https://h/a/b/c", "/a/..?q", "https://h/?q");
    ok(t, "https://h/a/b/c", "/a/.?q", "https://h/a/?q");
    ok(t, "https://h/a/b/c", "/a/../b/..", "https://h/");
    ok(t, "https://h/a/b/c", "/a/b/../..", "https://h/");
    ok(t, "https://h/a/b/c", "/a/b/../../..", "https://h/");
    ok(t, "https://h/a/b/c", "https://o/a/../b", "https://o/b");
    ok(t, "https://h/a/b/c", "//o/a/./b", "https://o/a/b");
}

CTEST_SUITE("http_survivor_redirect",
            { "args", t_args },
            { "empty_host_or_target_with_pointer", t_empty_host_or_target_with_pointer },
            { "counter_and_order", t_counter_and_order },
            { "scheme_forms", t_scheme_forms },
            { "path_bases", t_path_bases },
            { "dots", t_dots })
