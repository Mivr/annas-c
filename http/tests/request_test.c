/* Tests for http/http.h: http_request_write. Public header only. */
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

static void fresh(void) { (void)core_buf_init(&g_b, g_store, sizeof g_store); }

static http_url_t mk(core_str_t host, unsigned port, core_str_t target) {
    http_url_t u;
    u.host = host;
    u.port = (uint16_t)port;
    u.target = target;
    return u;
}

/* Writes and expects exactly `want` appended (and nothing else). */
static void expect_req(ctest_t *t, const http_url_t *u, core_str_t ver, const char *want) {
    fresh();
    CHECK_EQ_I64(t, http_request_write(&g_b, u, ver), HTTP_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&g_b), want);
    CHECK_EQ_U64(t, core_buf_len(&g_b), strlen(want));
    CHECK(t, !core_buf_overflow(&g_b));
}

/* Expects `code` and that nothing was appended. */
static void expect_err(ctest_t *t, const http_url_t *u, core_str_t ver, http_status_t code) {
    fresh();
    core_buf_put(&g_b, "pre", 3);
    CHECK_EQ_I64(t, http_request_write(&g_b, u, ver), code);
    CHECK_EQ_STR(t, core_buf_cstr(&g_b), "pre");
    CHECK(t, !core_buf_overflow(&g_b));
}

static void t_exact_bytes_default_port(ctest_t *t) {
    http_url_t u = mk(LIT("annas-archive.gl"), 443, LIT("/dyn/api/fast_download.json?md5=abc&key=k"));
    expect_req(t, &u, LIT("0.1"),
               "GET /dyn/api/fast_download.json?md5=abc&key=k HTTP/1.1\r\n"
               "Host: annas-archive.gl\r\n"
               "User-Agent: annas-c/0.1\r\n"
               "Accept: */*\r\n"
               "Connection: close\r\n"
               "\r\n");
}

static void t_exact_bytes_other_port(ctest_t *t) {
    http_url_t u = mk(LIT("127.0.0.1"), 8443, LIT("/"));
    expect_req(t, &u, LIT("1.2.3-rc.4+x_y"),
               "GET / HTTP/1.1\r\nHost: 127.0.0.1:8443\r\nUser-Agent: annas-c/1.2.3-rc.4+x_y\r\n"
               "Accept: */*\r\nConnection: close\r\n\r\n");
    u = mk(LIT("h"), 1, LIT("/a"));
    expect_req(t, &u, LIT("v"),
               "GET /a HTTP/1.1\r\nHost: h:1\r\nUser-Agent: annas-c/v\r\nAccept: */*\r\nConnection: close\r\n\r\n");
    u = mk(LIT("h"), 65535, LIT("/a"));
    expect_req(t, &u, LIT("v"),
               "GET /a HTTP/1.1\r\nHost: h:65535\r\nUser-Agent: annas-c/v\r\nAccept: */*\r\nConnection: close\r\n\r\n");
}

static void t_host_case_and_target_verbatim(ctest_t *t) {
    http_url_t u = mk(LIT("Example.ORG"), 443, LIT("//a%20b/c?d=e&f=%2F~"));
    expect_req(t, &u, LIT("0"),
               "GET //a%20b/c?d=e&f=%2F~ HTTP/1.1\r\nHost: Example.ORG\r\nUser-Agent: annas-c/0\r\n"
               "Accept: */*\r\nConnection: close\r\n\r\n");
}

static void t_appends_after_existing(ctest_t *t) {
    http_url_t u = mk(LIT("h"), 443, LIT("/"));
    const char *req = "GET / HTTP/1.1\r\nHost: h\r\nUser-Agent: annas-c/0\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    fresh();
    core_buf_put(&g_b, "xx", 2);
    CHECK_EQ_I64(t, http_request_write(&g_b, &u, LIT("0")), HTTP_OK);
    CHECK_EQ_U64(t, core_buf_len(&g_b), 2 + strlen(req));
    CHECK(t, memcmp(core_buf_cstr(&g_b), "xx", 2) == 0);
    CHECK(t, memcmp(core_buf_cstr(&g_b) + 2, req, strlen(req)) == 0);
}

static void t_with_split_url(ctest_t *t) {
    http_url_t u;
    CHECK_EQ_I64(t, http_url_split(LIT("https://files.example.net:8080/a/b.pdf?x=1#f"), &u), HTTP_OK);
    expect_req(t, &u, LIT("0.1"),
               "GET /a/b.pdf?x=1 HTTP/1.1\r\nHost: files.example.net:8080\r\nUser-Agent: annas-c/0.1\r\n"
               "Accept: */*\r\nConnection: close\r\n\r\n");
}

/* Forbidden bytes in the host, target and version, one at a time, each with a positive control above. */
static void t_forbidden_bytes_target(ctest_t *t) {
    static const char bad[] = {'\r', '\n', '\0', ' ', '\t', '\x01', '\x1f', '\x7f', '\x80', '\xff'};
    char tg[8] = "/a?b=c";
    size_t i, pos;
    for (i = 0; i < sizeof bad; i++) {
        for (pos = 1; pos < 6; pos++) {
            http_url_t u;
            char saved = tg[pos];
            tg[pos] = bad[i];
            u = mk(LIT("h"), 443, (core_str_t){tg, 6});
            expect_err(t, &u, LIT("0"), HTTP_E_INVALID);
            tg[pos] = saved;
        }
    }
    {
        http_url_t u = mk(LIT("h"), 443, (core_str_t){tg, 6});
        expect_req(t, &u, LIT("0"), "GET /a?b=c HTTP/1.1\r\nHost: h\r\nUser-Agent: annas-c/0\r\nAccept: */*\r\nConnection: close\r\n\r\n");
    }
}

static void t_forbidden_bytes_host(ctest_t *t) {
    static const char bad[] = {'\r', '\n', '\0', ' ', '\t', '/', ':', '@', '\x7f', '\xff'};
    char h[8] = "abc.org";
    size_t i, pos;
    for (i = 0; i < sizeof bad; i++) {
        for (pos = 0; pos < 7; pos++) {
            http_url_t u;
            char saved = h[pos];
            h[pos] = bad[i];
            u = mk((core_str_t){h, 7}, 443, LIT("/"));
            expect_err(t, &u, LIT("0"), HTTP_E_INVALID);
            h[pos] = saved;
        }
    }
    {
        http_url_t u = mk(LIT("abc.org\r\nX: y"), 443, LIT("/"));
        expect_err(t, &u, LIT("0"), HTTP_E_INVALID);
        u = mk((core_str_t){h, 7}, 443, LIT("/"));
        expect_req(t, &u, LIT("0"), "GET / HTTP/1.1\r\nHost: abc.org\r\nUser-Agent: annas-c/0\r\nAccept: */*\r\nConnection: close\r\n\r\n");
    }
}

static void t_forbidden_bytes_version(ctest_t *t) {
    static const char bad[] = {'\r', '\n', '\0', ' ', '\t', '/', ';', ',', '"', '\x7f', '\xff'};
    char v[8] = "1.2.3";
    size_t i, pos;
    http_url_t u = mk(LIT("h"), 443, LIT("/"));
    for (i = 0; i < sizeof bad; i++) {
        for (pos = 0; pos < 5; pos++) {
            char saved = v[pos];
            v[pos] = bad[i];
            expect_err(t, &u, (core_str_t){v, 5}, HTTP_E_INVALID);
            v[pos] = saved;
        }
    }
    expect_req(t, &u, (core_str_t){v, 5},
               "GET / HTTP/1.1\r\nHost: h\r\nUser-Agent: annas-c/1.2.3\r\nAccept: */*\r\nConnection: close\r\n\r\n");
    expect_err(t, &u, LIT("0\r\nX-Injected: 1"), HTTP_E_INVALID);
}

static void t_ill_formed_components(ctest_t *t) {
    http_url_t u = mk(LIT("h"), 443, LIT("/"));
    expect_err(t, &u, LIT(""), HTTP_E_INVALID);                            /* empty version */
    u = mk(LIT(""), 443, LIT("/"));
    expect_err(t, &u, LIT("0"), HTTP_E_INVALID);                           /* empty host */
    u = mk(LIT("h"), 443, LIT(""));
    expect_err(t, &u, LIT("0"), HTTP_E_INVALID);                           /* empty target */
    u = mk(LIT("h"), 443, LIT("a/b"));
    expect_err(t, &u, LIT("0"), HTTP_E_INVALID);                           /* no leading slash */
    u = mk(LIT("h"), 443, LIT("*"));
    expect_err(t, &u, LIT("0"), HTTP_E_INVALID);
    u = mk(LIT("h"), 443, LIT("http://evil/"));
    expect_err(t, &u, LIT("0"), HTTP_E_INVALID);
    u = mk(LIT("h"), 0, LIT("/"));
    expect_err(t, &u, LIT("0"), HTTP_E_INVALID);                           /* port 0 */
    u = mk(LIT("-h"), 443, LIT("/"));
    expect_err(t, &u, LIT("0"), HTTP_E_INVALID);                           /* not a valid host */
    u = mk(LIT("1.2.3"), 443, LIT("/"));
    expect_err(t, &u, LIT("0"), HTTP_E_INVALID);
    u = mk(LIT("h"), 1, LIT("/"));
    expect_req(t, &u, LIT("0"), "GET / HTTP/1.1\r\nHost: h:1\r\nUser-Agent: annas-c/0\r\nAccept: */*\r\nConnection: close\r\n\r\n");
}

static void t_version_length(ctest_t *t) {
    char v[80];
    http_url_t u = mk(LIT("h"), 443, LIT("/"));
    memset(v, 'v', sizeof v);
    fresh();
    CHECK_EQ_I64(t, http_request_write(&g_b, &u, (core_str_t){v, 64}), HTTP_OK);
    {
        static const char head[] = "\r\nUser-Agent: annas-c/";
        const char *p = strstr(core_buf_cstr(&g_b), head);
        CHECK(t, p != NULL);
        if (p != NULL) {
            p += sizeof head - 1;
            CHECK(t, memcmp(p, v, 64) == 0);
            CHECK(t, memcmp(p + 64, "\r\nAccept", 8) == 0);
        }
    }
    expect_err(t, &u, (core_str_t){v, 65}, HTTP_E_INVALID);
    expect_err(t, &u, (core_str_t){v, 80}, HTTP_E_INVALID);
    fresh();
    CHECK_EQ_I64(t, http_request_write(&g_b, &u, (core_str_t){v, 1}), HTTP_OK);
}

static void t_overflow_boundary(ctest_t *t) {
    http_url_t u = mk(LIT("h"), 443, LIT("/"));
    const char *want = "GET / HTTP/1.1\r\nHost: h\r\nUser-Agent: annas-c/0\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    size_t n = strlen(want);
    static char small[200];
    core_buf_t b;
    CHECK(t, core_buf_init(&b, small, n + 1));
    CHECK_EQ_I64(t, http_request_write(&b, &u, LIT("0")), HTTP_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&b), want);
    CHECK(t, core_buf_init(&b, small, n));
    CHECK_EQ_I64(t, http_request_write(&b, &u, LIT("0")), HTTP_E_OVERFLOW);
    CHECK(t, core_buf_overflow(&b));
    CHECK(t, core_buf_init(&b, small, 1));
    CHECK_EQ_I64(t, http_request_write(&b, &u, LIT("0")), HTTP_E_OVERFLOW);
    CHECK(t, core_buf_init(&b, NULL, 0));
    CHECK_EQ_I64(t, http_request_write(&b, &u, LIT("0")), HTTP_E_OVERFLOW);
    /* an already-overflowed buffer stays an overflow even for a valid request */
    fresh();
    core_buf_put(&g_b, NULL, 1);
    CHECK(t, core_buf_overflow(&g_b));
    CHECK_EQ_I64(t, http_request_write(&g_b, &u, LIT("0")), HTTP_E_OVERFLOW);
    /* invalid beats overflow */
    CHECK(t, core_buf_init(&b, small, 4));
    u = mk(LIT("h"), 443, LIT("/\r\n"));
    CHECK_EQ_I64(t, http_request_write(&b, &u, LIT("0")), HTTP_E_INVALID);
}

static void t_args(ctest_t *t) {
    http_url_t u = mk(LIT("h"), 443, LIT("/"));
    fresh();
    CHECK_EQ_I64(t, http_request_write(NULL, &u, LIT("0")), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_request_write(&g_b, NULL, LIT("0")), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_request_write(&g_b, &u, (core_str_t){NULL, 1}), HTTP_E_ARG);
    u = mk((core_str_t){NULL, 1}, 443, LIT("/"));
    CHECK_EQ_I64(t, http_request_write(&g_b, &u, LIT("0")), HTTP_E_ARG);
    u = mk(LIT("h"), 443, (core_str_t){NULL, 1});
    CHECK_EQ_I64(t, http_request_write(&g_b, &u, LIT("0")), HTTP_E_ARG);
    CHECK_EQ_U64(t, core_buf_len(&g_b), 0);
    u = mk(LIT("h"), 443, LIT("/"));
    CHECK_EQ_I64(t, http_request_write(&g_b, &u, LIT("0")), HTTP_OK);
}

/* A bad slice ({NULL, len > 0}) is HTTP_E_ARG and leaves `out` untouched (content, length, overflow flag). */
static void expect_arg(ctest_t *t, const http_url_t *u, core_str_t ver) {
    fresh();
    core_buf_put(&g_b, "pre", 3);
    CHECK_EQ_I64(t, http_request_write(&g_b, u, ver), HTTP_E_ARG);
    CHECK_EQ_STR(t, core_buf_cstr(&g_b), "pre");
    CHECK_EQ_U64(t, core_buf_len(&g_b), 3);
    CHECK(t, !core_buf_overflow(&g_b));
}

static void t_arg_null_host(ctest_t *t) {
    static const size_t lens[] = {1, 2, 7, 64, 253, 4096, (size_t)1 << 20};
    size_t i;
    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        http_url_t u = mk((core_str_t){NULL, lens[i]}, 443, LIT("/"));
        expect_arg(t, &u, LIT("0"));
    }
    /* ARG wins over every other failure in the other components */
    {
        http_url_t u = mk((core_str_t){NULL, 1}, 0, LIT("bad target"));
        expect_arg(t, &u, LIT(""));
    }
}

static void t_arg_null_target(ctest_t *t) {
    static const size_t lens[] = {1, 2, 7, 64, 4096, (size_t)1 << 20};
    size_t i;
    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        http_url_t u = mk(LIT("h"), 443, (core_str_t){NULL, lens[i]});
        expect_arg(t, &u, LIT("0"));
    }
    {
        http_url_t u = mk(LIT("-bad"), 0, (core_str_t){NULL, 1});
        expect_arg(t, &u, LIT(""));
    }
}

static void t_arg_null_version(ctest_t *t) {
    static const size_t lens[] = {1, 2, 7, 64, 65, 4096, (size_t)1 << 20};
    size_t i;
    http_url_t u = mk(LIT("h"), 443, LIT("/"));
    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        expect_arg(t, &u, (core_str_t){NULL, lens[i]});
    }
    u = mk(LIT("-bad"), 0, LIT("no slash"));
    expect_arg(t, &u, (core_str_t){NULL, 1});
}

/* {NULL, 0} is an empty slice, not a bad argument: the components are empty/ill-formed -> HTTP_E_INVALID. */
static void t_empty_null_slices_invalid(ctest_t *t) {
    http_url_t u = mk((core_str_t){NULL, 0}, 443, LIT("/"));
    expect_err(t, &u, LIT("0"), HTTP_E_INVALID);
    u = mk(LIT("h"), 443, (core_str_t){NULL, 0});
    expect_err(t, &u, LIT("0"), HTTP_E_INVALID);
    u = mk(LIT("h"), 443, LIT("/"));
    expect_err(t, &u, (core_str_t){NULL, 0}, HTTP_E_INVALID);
}

/* Each bad slice is reported as ARG even when the other two slices are valid, and a valid call still works. */
static void t_arg_bad_next_to_valid(ctest_t *t) {
    http_url_t u = mk((core_str_t){NULL, 3}, 8443, LIT("/p?q=1"));
    expect_arg(t, &u, LIT("1.0"));
    u = mk(LIT("example.org"), 8443, (core_str_t){NULL, 3});
    expect_arg(t, &u, LIT("1.0"));
    u = mk(LIT("example.org"), 8443, LIT("/p?q=1"));
    expect_arg(t, &u, (core_str_t){NULL, 3});
    expect_req(t, &u, LIT("1.0"),
               "GET /p?q=1 HTTP/1.1\r\nHost: example.org:8443\r\nUser-Agent: annas-c/1.0\r\n"
               "Accept: */*\r\nConnection: close\r\n\r\n");
}

/* Bad slice with an already-overflowed buffer: still ARG, and the flag stays as it was. */
static void t_arg_keeps_overflow_flag(ctest_t *t) {
    http_url_t u = mk(LIT("h"), 443, LIT("/"));
    fresh();
    core_buf_put(&g_b, NULL, 1);
    CHECK(t, core_buf_overflow(&g_b));
    CHECK_EQ_I64(t, http_request_write(&g_b, &u, (core_str_t){NULL, 1}), HTTP_E_ARG);
    CHECK(t, core_buf_overflow(&g_b));
    CHECK_EQ_U64(t, core_buf_len(&g_b), 0);
}

/* Return code of one write into an already-overflowed buffer. */
static http_status_t write_overflowed(const http_url_t *u, core_str_t ver) {
    fresh();
    core_buf_put(&g_b, NULL, 1);
    return http_request_write(&g_b, u, ver);
}

/* A bad slice beats an overflowed `out`, for each of the three slices. */
static void t_arg_beats_overflow_each_slice(ctest_t *t) {
    static const size_t lens[] = {1, 65, (size_t)1 << 20};
    size_t i;
    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        http_url_t u = mk((core_str_t){NULL, lens[i]}, 443, LIT("/"));
        CHECK_EQ_I64(t, write_overflowed(&u, LIT("0")), HTTP_E_ARG);
        u = mk(LIT("h"), 443, (core_str_t){NULL, lens[i]});
        CHECK_EQ_I64(t, write_overflowed(&u, LIT("0")), HTTP_E_ARG);
        u = mk(LIT("h"), 443, LIT("/"));
        CHECK_EQ_I64(t, write_overflowed(&u, (core_str_t){NULL, lens[i]}), HTTP_E_ARG);
    }
}

/* ARG beats INVALID and OVERFLOW whichever other components are invalid, for each of the three slices. */
static void t_arg_beats_invalid_each_position(ctest_t *t) {
    http_url_t u = mk((core_str_t){NULL, 1}, 443, LIT("/"));
    CHECK_EQ_I64(t, write_overflowed(&u, LIT("bad version")), HTTP_E_ARG);
    u = mk((core_str_t){NULL, 1}, 443, LIT("no slash"));
    CHECK_EQ_I64(t, write_overflowed(&u, LIT("0")), HTTP_E_ARG);
    u = mk(LIT("h"), 0, (core_str_t){NULL, 1});
    CHECK_EQ_I64(t, write_overflowed(&u, LIT("0")), HTTP_E_ARG);
    u = mk(LIT("bad host!"), 443, (core_str_t){NULL, 1});
    CHECK_EQ_I64(t, write_overflowed(&u, LIT("0")), HTTP_E_ARG);
    u = mk(LIT("bad host!"), 443, LIT("/"));
    CHECK_EQ_I64(t, write_overflowed(&u, (core_str_t){NULL, 1}), HTTP_E_ARG);
    u = mk(LIT("h"), 443, LIT("/\r\n"));
    CHECK_EQ_I64(t, write_overflowed(&u, (core_str_t){NULL, 1}), HTTP_E_ARG);
    /* two bad slices at once */
    u = mk((core_str_t){NULL, 1}, 443, (core_str_t){NULL, 1});
    CHECK_EQ_I64(t, write_overflowed(&u, (core_str_t){NULL, 1}), HTTP_E_ARG);
    CHECK_EQ_I64(t, write_overflowed(&u, LIT("0")), HTTP_E_ARG);
}

/* {NULL, 0} is an empty component: INVALID, also with an overflowed `out` (INVALID beats OVERFLOW). */
static void t_empty_null_slices_beat_overflow(ctest_t *t) {
    http_url_t u = mk((core_str_t){NULL, 0}, 443, LIT("/"));
    CHECK_EQ_I64(t, write_overflowed(&u, LIT("0")), HTTP_E_INVALID);
    u = mk(LIT("h"), 443, (core_str_t){NULL, 0});
    CHECK_EQ_I64(t, write_overflowed(&u, LIT("0")), HTTP_E_INVALID);
    u = mk(LIT("h"), 443, LIT("/"));
    CHECK_EQ_I64(t, write_overflowed(&u, (core_str_t){NULL, 0}), HTTP_E_INVALID);
}

CTEST_SUITE("http_request",
            { "exact_bytes_default_port", t_exact_bytes_default_port },
            { "exact_bytes_other_port", t_exact_bytes_other_port },
            { "host_case_and_target_verbatim", t_host_case_and_target_verbatim },
            { "appends_after_existing", t_appends_after_existing },
            { "with_split_url", t_with_split_url },
            { "forbidden_bytes_target", t_forbidden_bytes_target },
            { "forbidden_bytes_host", t_forbidden_bytes_host },
            { "forbidden_bytes_version", t_forbidden_bytes_version },
            { "ill_formed_components", t_ill_formed_components },
            { "version_length", t_version_length },
            { "overflow_boundary", t_overflow_boundary },
            { "args", t_args },
            { "arg_null_host", t_arg_null_host },
            { "arg_null_target", t_arg_null_target },
            { "arg_null_version", t_arg_null_version },
            { "empty_null_slices_invalid", t_empty_null_slices_invalid },
            { "arg_bad_next_to_valid", t_arg_bad_next_to_valid },
            { "arg_keeps_overflow_flag", t_arg_keeps_overflow_flag },
            { "arg_beats_overflow_each_slice", t_arg_beats_overflow_each_slice },
            { "arg_beats_invalid_each_position", t_arg_beats_invalid_each_position },
            { "empty_null_slices_beat_overflow", t_empty_null_slices_beat_overflow })
