/* Tests for http/http.h: argument checks, zeroed events after errors, slice boundaries (a slice must never be read
 * past its length, whatever bytes follow it in memory), header-name first bytes, version and target characters,
 * dot-segment look-alikes and scheme character classes. Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/buf.h"
#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static http_parser_t g_p;
static char g_store[1024];

/* ---- parser arguments and the event after an error ---- */
static void t_parser_args(ctest_t *t) {
    http_event_t ev;
    core_str_t in = {NULL, 1};
    core_str_t empty = {NULL, 0};
    CHECK(t, http_parser_init(&g_p));
    CHECK_EQ_I64(t, http_parser_next(&g_p, &in, &ev), HTTP_E_ARG);
    CHECK_EQ_U64(t, in.len, 1);
    in.len = 2;
    CHECK_EQ_I64(t, http_parser_next(&g_p, &in, &ev), HTTP_E_ARG);
    in.len = 100;
    CHECK_EQ_I64(t, http_parser_next(&g_p, &in, &ev), HTTP_E_ARG);
    /* not sticky: the same parser still works */
    in = LIT("HTTP/1.1 200 OK\r\n");
    CHECK_EQ_I64(t, http_parser_next(&g_p, &in, &ev), HTTP_OK);
    CHECK_EQ_I64(t, ev.kind, HTTP_EV_STATUS);
    CHECK_EQ_I64(t, ev.status, 200);
    /* a NULL pointer with no bytes is just "no input" */
    CHECK_EQ_I64(t, http_parser_next(&g_p, &empty, &ev), HTTP_OK);
    CHECK_EQ_I64(t, ev.kind, HTTP_EV_NONE);
    CHECK_EQ_I64(t, http_parser_next(&g_p, &in, NULL), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_parser_next(NULL, &in, &ev), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_parser_next(&g_p, NULL, &ev), HTTP_E_ARG);
}

static void zeroed(ctest_t *t, const http_event_t *ev) {
    CHECK_EQ_I64(t, ev->kind, HTTP_EV_NONE);
    CHECK_EQ_I64(t, ev->status, 0);
    CHECK_EQ_I64(t, ev->minor, 0);
    CHECK(t, ev->reason.ptr == NULL && ev->reason.len == 0);
    CHECK(t, ev->name.ptr == NULL && ev->name.len == 0);
    CHECK(t, ev->value.ptr == NULL && ev->value.len == 0);
    CHECK_EQ_I64(t, ev->framing, 0);
    CHECK_EQ_U64(t, ev->content_length, 0);
    CHECK(t, ev->data.ptr == NULL && ev->data.len == 0);
}

/* After an error the event is all zero, even when the failing line had already filled name and value. */
static void t_event_zero_after_error(ctest_t *t) {
    static const char *const msgs[] = {
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: x\r\n\r\n",
        "HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 1\r\n\r\n",
    };
    size_t i;
    for (i = 0; i < sizeof msgs / sizeof msgs[0]; i++) {
        core_str_t in = core_str_from_cstr(msgs[i]);
        http_event_t ev;
        http_status_t rc = HTTP_OK;
        int guard;
        CHECK(t, http_parser_init(&g_p));
        for (guard = 0; guard < 16 && rc == HTTP_OK; guard++) {
            rc = http_parser_next(&g_p, &in, &ev);
        }
        CHECK(t, rc == HTTP_E_CONTENT_LENGTH || rc == HTTP_E_FRAMING);
        zeroed(t, &ev);
        CHECK_EQ_I64(t, http_parser_next(&g_p, &in, &ev), rc);
        zeroed(t, &ev);
    }
}

/* ---- status line of exactly 13 bytes ---- */
static http_status_t head_rc(const char *line) {
    static char buf[128];
    core_str_t in;
    http_event_t ev;
    http_status_t rc;
    snprintf(buf, sizeof buf, "%s\r\n", line);
    in = core_str_from_cstr(buf);
    (void)http_parser_init(&g_p);
    rc = http_parser_next(&g_p, &in, &ev);
    return rc;
}

static void t_status_line_13_bytes(ctest_t *t) {
    CHECK_EQ_I64(t, head_rc("HTTP/1.1 200X"), HTTP_E_STATUS_LINE);
    CHECK_EQ_I64(t, head_rc("HTTP/1.1 200\t"), HTTP_E_STATUS_LINE);
    CHECK_EQ_I64(t, head_rc("HTTP/1.1 2000"), HTTP_E_STATUS_LINE);
    CHECK_EQ_I64(t, head_rc("HTTP/1.1 200 "), HTTP_OK);
    CHECK_EQ_I64(t, head_rc("HTTP/1.1 200"), HTTP_OK);
}

/* ---- header name first byte ---- */
static http_status_t hdr_rc(const char *line) {
    static char buf[128];
    core_str_t in;
    http_event_t ev;
    http_status_t rc;
    snprintf(buf, sizeof buf, "HTTP/1.1 200 OK\r\n%s\r\n\r\n", line);
    in = core_str_from_cstr(buf);
    (void)http_parser_init(&g_p);
    rc = http_parser_next(&g_p, &in, &ev);
    if (rc == HTTP_OK) {
        rc = http_parser_next(&g_p, &in, &ev);
    }
    return rc;
}

static void t_header_first_byte(ctest_t *t) {
    static const char *const bad[] = {"(a: b", "\x01" "a: b", "\x80" "a: b", "\"a: b", "@a: b", "[a: b", "/a: b",
                                      ":a: b", "a\x01: b", ": b", ":", "a", "a b: c", "a : b", "\x7f" "a: b", "{a: b",
                                      "\x7f: b", "a(: b", "a\"b: c", "a,b: c", "a;b: c", "a<b: c", "a=b: c", "a>b: c",
                                      "a?b: c", "a@b: c", "a[b: c", "a\\b: c", "a]b: c", "a{b: c", "a}b: c"};
    static const char *const good[] = {"a: b", "A: b", "0: b", "9: b", "z: b", "Z: b", "!: b", "#: b", "$: b", "%: b",
                                       "&: b", "'a: b", "*: b", "+: b", "-: b", ".: b", "^: b", "_: b", "`: b", "|: b",
                                       "~: b", "a!#$%&'*+-.^_`|~: b", "a0z9AZ: b"};
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK_EQ_I64(t, hdr_rc(bad[i]), HTTP_E_HEADER);
    }
    for (i = 0; i < sizeof good / sizeof good[0]; i++) {
        CHECK_EQ_I64(t, hdr_rc(good[i]), HTTP_OK);
    }
}

/* ---- challenge: the first byte of the needle ---- */
static void t_challenge_first_byte(ctest_t *t) {
    core_str_t none = {NULL, 0};
    CHECK_EQ_I64(t, http_detect_challenge(403, none, LIT("xdos-guard")), HTTP_OK);
    CHECK_EQ_I64(t, http_detect_challenge(403, none, LIT("Xdos-guard")), HTTP_OK);
    CHECK_EQ_I64(t, http_detect_challenge(403, none, LIT("dxos-guard")), HTTP_OK);
    CHECK_EQ_I64(t, http_detect_challenge(403, none, LIT("ddos-guarx")), HTTP_OK);
    CHECK_EQ_I64(t, http_detect_challenge(403, none, LIT("ddos-guard")), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, http_detect_challenge(403, none, LIT("xddos-guard")), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, http_detect_challenge(403, none, LIT("DDOS-GUARD")), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, http_detect_challenge(403, none, (core_str_t){NULL, 1}), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_detect_challenge(403, (core_str_t){NULL, 1}, none), HTTP_E_ARG);
}

/* ---- request writer ---- */
static void t_request_args_and_chars(ctest_t *t) {
    core_buf_t b;
    http_url_t u;
    char v[2];
    int c;
    (void)http_url_split(LIT("https://h/p"), &u);
    (void)core_buf_init(&b, g_store, sizeof g_store);
    CHECK_EQ_I64(t, http_request_write(&b, &u, (core_str_t){NULL, 0}), HTTP_E_INVALID);
    CHECK_EQ_I64(t, http_request_write(&b, &u, (core_str_t){NULL, 1}), HTTP_E_ARG);
    u.host = (core_str_t){NULL, 0};
    CHECK_EQ_I64(t, http_request_write(&b, &u, LIT("1")), HTTP_E_INVALID);
    u.host = (core_str_t){NULL, 1};
    CHECK_EQ_I64(t, http_request_write(&b, &u, LIT("1")), HTTP_E_ARG);
    (void)http_url_split(LIT("https://h/p"), &u);
    u.target = (core_str_t){NULL, 0};
    CHECK_EQ_I64(t, http_request_write(&b, &u, LIT("1")), HTTP_E_INVALID);
    u.target = (core_str_t){NULL, 1};
    CHECK_EQ_I64(t, http_request_write(&b, &u, LIT("1")), HTTP_E_ARG);
    CHECK_EQ_U64(t, core_buf_len(&b), 0);
    /* every version byte class, and its neighbours */
    for (c = 1; c < 256; c++) {
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' ||
                  c == '+' || c == '-';
        v[0] = (char)c;
        v[1] = 0;
        (void)http_url_split(LIT("https://h/p"), &u);
        (void)core_buf_init(&b, g_store, sizeof g_store);
        CHECK_EQ_I64(t, http_request_write(&b, &u, (core_str_t){v, 1}), ok ? HTTP_OK : HTTP_E_INVALID);
        CHECK_EQ_U64(t, core_buf_len(&b) == 0, ok ? 0 : 1);
    }
    /* every target byte, in the first position after the slash and in the last */
    {
        char tg[4] = {'/', 'a', 'a', 0};
        for (c = 1; c < 256; c++) {
            bool ok = c >= 0x21 && c <= 0x7e;
            tg[1] = (char)c;
            u.target = (core_str_t){tg, 2};
            (void)core_buf_init(&b, g_store, sizeof g_store);
            CHECK_EQ_I64(t, http_request_write(&b, &u, LIT("1")), ok ? HTTP_OK : HTTP_E_INVALID);
            u.target = (core_str_t){tg, 3};
            (void)core_buf_init(&b, g_store, sizeof g_store);
            CHECK_EQ_I64(t, http_request_write(&b, &u, LIT("1")), ok ? HTTP_OK : HTTP_E_INVALID);
        }
    }
}

/* ---- host: hex spellings and slice boundaries ---- */
static void t_host_hex_and_slices(ctest_t *t) {
    CHECK(t, !http_host_valid(LIT("a.0xa")));
    CHECK(t, !http_host_valid(LIT("a.0xA")));
    CHECK(t, !http_host_valid(LIT("a.0xf")));
    CHECK(t, !http_host_valid(LIT("a.0xF")));
    CHECK(t, !http_host_valid(LIT("a.0x0")));
    CHECK(t, !http_host_valid(LIT("a.0x9")));
    CHECK(t, !http_host_valid(LIT("a.0xaF09")));
    CHECK(t, http_host_valid(LIT("a.0xg")));
    CHECK(t, !http_host_valid(LIT("a.0x/")));
    /* the host is the slice, whatever follows it in memory */
    CHECK(t, http_host_valid((core_str_t){"abc.def", 3}));
    CHECK(t, http_host_valid((core_str_t){"abc.", 3}));
    CHECK(t, http_host_valid((core_str_t){"a.b.c", 3}));
    CHECK(t, http_host_valid((core_str_t){"ab.-", 2}));
    CHECK(t, http_host_valid((core_str_t){"1.2.3.4.5", 7}));
    CHECK(t, !http_host_valid((core_str_t){"1.2.3.4.5", 9}));
}

/* ---- url_split: slice boundaries ---- */
static void t_split_slices(ctest_t *t) {
    http_url_t u;
    CHECK_EQ_I64(t, http_url_split((core_str_t){NULL, 1}, &u), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_url_split((core_str_t){NULL, 20}, &u), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_url_split(LIT("https://h/"), NULL), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_url_split((core_str_t){NULL, 0}, &u), HTTP_E_SCHEME);
    /* a path must lie inside the slice */
    CHECK_EQ_I64(t, http_url_split((core_str_t){"https://h/x", 9}, &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split((core_str_t){"https://h/x", 10}, &u), HTTP_OK);
    CHECK_EQ_U64(t, u.target.len, 1);
    CHECK_EQ_I64(t, http_url_split((core_str_t){"https://hab/x", 9}, &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split((core_str_t){"https://h:80/x", 10}, &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split((core_str_t){"https://h@/x", 9}, &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split((core_str_t){"https://", 8}, &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split((core_str_t){"https://a", 8}, &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split((core_str_t){"https:x//h/", 11}, &u), HTTP_E_SCHEME);
    CHECK_EQ_I64(t, http_url_split((core_str_t){"https:/x/h/", 11}, &u), HTTP_E_SCHEME);
    CHECK_EQ_I64(t, http_url_split((core_str_t){"https://h/", 7}, &u), HTTP_E_SCHEME);
    CHECK_EQ_I64(t, http_url_split((core_str_t){"https://h/", 8}, &u), HTTP_E_URL);
}

/* ---- redirect: slice boundaries and look-alikes ---- */
static http_status_t resolve_in(const char *cur, core_str_t loc) {
    static http_url_t u;
    static http_redirects_t r;
    core_buf_t b;
    (void)http_url_split(core_str_from_cstr(cur), &u);
    (void)core_buf_init(&b, g_store, sizeof g_store);
    r.count = 0;
    {
        http_status_t rc = http_redirect_resolve(&r, &u, 302, loc, &b);
        snprintf(g_store + 900, 100, "%s", rc == HTTP_OK ? core_buf_cstr(&b) : "");
        return rc;
    }
}

static void is_url(ctest_t *t, const char *cur, core_str_t loc, const char *want) {
    CHECK_EQ_I64(t, resolve_in(cur, loc), HTTP_OK);
    CHECK_EQ_STR(t, g_store + 900, want);
}

static void t_redirect_slices(ctest_t *t) {
    const char *cur = "https://h/d/p?old";
    is_url(t, cur, (core_str_t){"/a/b/c", 4}, "https://h/a/b");
    is_url(t, cur, (core_str_t){"/a/b//", 4}, "https://h/a/b");
    is_url(t, cur, (core_str_t){"x/y/z", 3}, "https://h/d/x/y");
    is_url(t, cur, (core_str_t){"abc:def", 3}, "https://h/d/abc");
    is_url(t, cur, (core_str_t){"a1:def", 2}, "https://h/d/a1");
    is_url(t, cur, (core_str_t){"/a?b?c", 2}, "https://h/a");
    is_url(t, cur, (core_str_t){"/a#b?c", 2}, "https://h/a");
    is_url(t, cur, (core_str_t){"/a?bb?c", 4}, "https://h/a?b");
    is_url(t, cur, (core_str_t){"//h2@x", 4}, "https://h2/");
    is_url(t, cur, (core_str_t){"//h2[x", 4}, "https://h2/");
    is_url(t, cur, (core_str_t){"//h2:99/x", 4}, "https://h2/");
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){"//a", 2}), HTTP_E_URL);
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){"https://a", 8}), HTTP_E_URL);
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){"https://h2/x", 7}), HTTP_E_URL);
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){"https://h2/x", 6}), HTTP_E_URL);
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){"\x01" "a", 2}), HTTP_E_URL);
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){" a", 2}), HTTP_E_URL);
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){"\\a", 2}), HTTP_E_URL);
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){"\x7f" "a", 2}), HTTP_E_URL);
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){"\x80" "a", 2}), HTTP_E_URL);
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){"a\x01", 2}), HTTP_E_URL);
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){"a b", 3}), HTTP_E_URL);
    CHECK_EQ_I64(t, resolve_in(cur, (core_str_t){"1a:x", 4}), HTTP_OK);
    is_url(t, cur, LIT("1a:x"), "https://h/d/1a:x");
    is_url(t, cur, LIT("-a:x"), "https://h/d/-a:x");
}

static void t_redirect_scheme_chars(ctest_t *t) {
    static const char *const sch[] = {"z:x", "Z:x", "a:x", "A:x", "aZ:x", "az:x", "aA:x", "aa:x", "a0:x", "a9:x",
                                      "a+:x", "a-:x", "a.:x", "zz9:x", "ZZ0:x", "ab:x", "httpx://h/", "xttps://h/",
                                      "hxtps://h/", "htxps://h/", "httxs://h/", "HTTPX://h/", "XTTPS://h/"};
    size_t i;
    for (i = 0; i < sizeof sch / sizeof sch[0]; i++) {
        CHECK_EQ_I64(t, resolve_in("https://h/d/p", core_str_from_cstr(sch[i])), HTTP_E_SCHEME);
    }
    is_url(t, "https://h/d/p", LIT("a/:x"), "https://h/d/a/:x");
    is_url(t, "https://h/d/p", LIT("a_:x"), "https://h/d/a_:x");
    is_url(t, "https://h/d/p", LIT("a@:x"), "https://h/d/a@:x");
    is_url(t, "https://h/d/p", LIT("a`:x"), "https://h/d/a`:x");
    is_url(t, "https://h/d/p", LIT("a{:x"), "https://h/d/a{:x");
    is_url(t, "https://h/d/p", LIT("a,:x"), "https://h/d/a,:x");
    is_url(t, "https://h/d/p", LIT("a[:x"), "https://h/d/a[:x");
    is_url(t, "https://h/d/p", LIT("/:x"), "https://h/:x");
    is_url(t, "https://h/d/p", LIT("@a:x"), "https://h/d/@a:x");
    is_url(t, "https://h/d/p", LIT("[a:x"), "https://h/d/[a:x");
    is_url(t, "https://h/d/p", LIT("`a:x"), "https://h/d/`a:x");
    is_url(t, "https://h/d/p", LIT("{a:x"), "https://h/d/{a:x");
    is_url(t, "https://h/d/p", LIT("0a:x"), "https://h/d/0a:x");
    is_url(t, "https://h/d/p", LIT("9a:x"), "https://h/d/9a:x");
}

static void t_redirect_dot_lookalikes(ctest_t *t) {
    is_url(t, "https://h/a/b", LIT("/.x"), "https://h/.x");
    is_url(t, "https://h/a/b", LIT("/a/.x"), "https://h/a/.x");
    is_url(t, "https://h/a/b", LIT("/a/.x/b"), "https://h/a/.x/b");
    is_url(t, "https://h/a/b", LIT("/a/.b/../c"), "https://h/a/c");
    is_url(t, "https://h/a/b", LIT("/a/x./b"), "https://h/a/x./b");
    is_url(t, "https://h/a/b", LIT("/a/./.x"), "https://h/a/.x");
    is_url(t, "https://h/a/b", LIT("/a/b/..x"), "https://h/a/b/..x");
    is_url(t, "https://h/a/b", LIT("/a/b/.."), "https://h/a/");
    is_url(t, "https://h/a/b", LIT("/a/b/../"), "https://h/a/");
    is_url(t, "https://h/a/b", LIT("/a/b/."), "https://h/a/b/");
    is_url(t, "https://h/a/b", LIT("/x/y/../.x"), "https://h/x/.x");
    is_url(t, "https://h/a/b", LIT("/x/..y/."), "https://h/x/..y/");
    is_url(t, "https://h/a/b", LIT("/x/y.."), "https://h/x/y..");
    is_url(t, "https://h/a/b", LIT("/x/.y"), "https://h/x/.y");
    is_url(t, "https://h/a/b", LIT("/x/y/.."), "https://h/x/");
}

CTEST_SUITE("http_survivor_edges",
            { "parser_args", t_parser_args },
            { "event_zero_after_error", t_event_zero_after_error },
            { "status_line_13_bytes", t_status_line_13_bytes },
            { "header_first_byte", t_header_first_byte },
            { "challenge_first_byte", t_challenge_first_byte },
            { "request_args_and_chars", t_request_args_and_chars },
            { "host_hex_and_slices", t_host_hex_and_slices },
            { "split_slices", t_split_slices },
            { "redirect_slices", t_redirect_slices },
            { "redirect_scheme_chars", t_redirect_scheme_chars },
            { "redirect_dot_lookalikes", t_redirect_dot_lookalikes })
