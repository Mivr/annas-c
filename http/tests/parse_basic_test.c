/* Tests for http/http.h: response parser, valid messages, exact events, status lines, headers. Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/buf.h"
#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

/* ---- transcript recorder (duplicated in each parser test file: tests have no shared header) ------------ */
#define TR_CAP 16384u
typedef struct {
    char b[TR_CAP];
    size_t n;
    char body[TR_CAP];
    size_t bn;
    bool full;
} tr_t;

static http_parser_t g_p;
static tr_t g_ta, g_tb;

static void tr_put(tr_t *r, const void *s, size_t n) {
    if (n > TR_CAP - 1 - r->n) {
        r->full = true;
        return;
    }
    if (n > 0) {
        memcpy(r->b + r->n, s, n);
    }
    r->n += n;
    r->b[r->n] = 0;
}

static void tr_str(tr_t *r, const char *s) { tr_put(r, s, strlen(s)); }

static void tr_slice(tr_t *r, core_str_t s) {
    if (s.len > 0) {
        tr_put(r, s.ptr, s.len);
    }
}

static void tr_flush(tr_t *r) {
    if (r->bn > 0) {
        tr_str(r, "B[");
        tr_put(r, r->body, r->bn);
        tr_str(r, "]\n");
        r->bn = 0;
    }
}

static void tr_body(tr_t *r, core_str_t d) {
    if (d.len == 0) {
        tr_flush(r);
        tr_str(r, "!emptybody\n");
    } else if (d.len > TR_CAP - r->bn) {
        r->full = true;
    } else {
        memcpy(r->body + r->bn, d.ptr, d.len);
        r->bn += d.len;
    }
}

static void tr_pair(tr_t *r, const char *tag, const http_event_t *ev) {
    tr_str(r, tag);
    tr_str(r, "[");
    tr_slice(r, ev->name);
    tr_str(r, "]=[");
    tr_slice(r, ev->value);
    tr_str(r, "]\n");
}

static void tr_event(tr_t *r, const http_event_t *ev) {
    char num[64];
    if (ev->kind == HTTP_EV_BODY) {
        tr_body(r, ev->data);
        return;
    }
    tr_flush(r);
    switch (ev->kind) {
    case HTTP_EV_STATUS:
        snprintf(num, sizeof num, "S%d.%d[", ev->status, ev->minor);
        tr_str(r, num);
        tr_slice(r, ev->reason);
        tr_str(r, "]\n");
        break;
    case HTTP_EV_HEADER:
        tr_pair(r, "H", ev);
        break;
    case HTTP_EV_TRAILER:
        tr_pair(r, "T", ev);
        break;
    case HTTP_EV_HEADERS_END:
        snprintf(num, sizeof num, "E%d:%llu\n", (int)ev->framing, (unsigned long long)ev->content_length);
        tr_str(r, num);
        break;
    case HTTP_EV_END:
        tr_str(r, "Z\n");
        break;
    default:
        tr_str(r, "?\n");
        break;
    }
}

static void tr_err(tr_t *r, int code) {
    char num[32];
    tr_flush(r);
    snprintf(num, sizeof num, "!%d\n", code);
    tr_str(r, num);
}

/* Pulls events from one piece of input until the parser wants more. False after an error (recorded). */
static bool pump(core_str_t in, tr_t *r) {
    const char *lo = in.ptr, *hi = in.ptr + in.len;
    http_event_t ev;
    unsigned guard;
    for (guard = 0; guard < 200000u; guard++) {
        http_status_t rc;
        memset(&ev, 0xA5, sizeof ev);
        rc = http_parser_next(&g_p, &in, &ev);
        if (rc != HTTP_OK) {
            tr_err(r, (int)rc);
            if (ev.kind != HTTP_EV_NONE) {
                tr_str(r, "!errevent\n");
            }
            return false;
        }
        if (ev.kind == HTTP_EV_NONE) {
            if (in.len != 0) {
                tr_str(r, "!none_with_input\n");
            }
            return true;
        }
        if (ev.kind == HTTP_EV_BODY && (ev.data.ptr < lo || ev.data.ptr + ev.data.len > hi)) {
            tr_str(r, "!notinput\n");
        }
        tr_event(r, &ev);
    }
    tr_str(r, "!loop\n");
    return false;
}

/* Feeds d[0..n) as pieces [0,c1) [c1,c2) and then `step`-byte pieces (0 = the rest in one piece),
 * optionally signals end of stream, and leaves the merged transcript in *r. */
static void run(const char *d, size_t n, size_t c1, size_t c2, size_t step, int eof, tr_t *r) {
    size_t off, take;
    http_event_t ev;
    http_status_t rc;
    r->n = 0;
    r->bn = 0;
    r->full = false;
    r->b[0] = 0;
    if (!http_parser_init(&g_p)) {
        tr_str(r, "!init\n");
        return;
    }
    c1 = c1 < n ? c1 : n;
    c2 = c2 < c1 ? c1 : (c2 < n ? c2 : n);
    if (!pump((core_str_t){d, c1}, r) || !pump((core_str_t){d + c1, c2 - c1}, r)) {
        return;
    }
    for (off = c2; off < n; off += take) {
        take = (step == 0 || step > n - off) ? n - off : step;
        if (!pump((core_str_t){d + off, take}, r)) {
            return;
        }
    }
    tr_flush(r);
    if (eof) {
        memset(&ev, 0xA5, sizeof ev);
        rc = http_parser_eof(&g_p, &ev);
        if (rc != HTTP_OK) {
            tr_err(r, (int)rc);
        } else if (ev.kind == HTTP_EV_END) {
            tr_str(r, "Z\n");
        } else if (ev.kind == HTTP_EV_NONE) {
            tr_str(r, "eofnone\n");
        } else {
            tr_str(r, "?\n");
        }
    }
}

static bool tr_same(const tr_t *a, const tr_t *b) {
    return !a->full && !b->full && a->n == b->n && memcmp(a->b, b->b, a->n) == 0;
}

/* Expected transcript pieces. */
#define S_OK "S200.1[OK]\n"
#define H_(n, v) "H[" n "]=[" v "]\n"
#define T_(n, v) "T[" n "]=[" v "]\n"
#define B_(d) "B[" d "]\n"
#define E_NONE "E0:0\n"
#define E_CHUNKED "E2:0\n"
#define E_CLOSE "E3:0\n"
#define E_LEN(n) "E1:" #n "\n"
#define Z_ "Z\n"

static char g_want[TR_CAP + 64];

/* The transcript must be exactly `prefix`, followed by "!<code>" when code >= 0. */
static void tr_expect(ctest_t *t, const tr_t *r, const char *prefix, int code) {
    char num[32];
    size_t n = strlen(prefix);
    memcpy(g_want, prefix, n);
    if (code >= 0) {
        snprintf(num, sizeof num, "!%d\n", code);
        memcpy(g_want + n, num, strlen(num));
        n += strlen(num);
    }
    g_want[n] = 0;
    CHECK(t, !r->full);
    CHECK_EQ_MEM(t, r->b, r->n, g_want, n);
}

/* ---- raw event stepping (exact consumption) ------------------------------------------------------------ */
static http_event_t g_ev;

static http_status_t stepf(core_str_t *in) {
    memset(&g_ev, 0xA5, sizeof g_ev);
    return http_parser_next(&g_p, in, &g_ev);
}

static bool want_kind(ctest_t *t, core_str_t *in, http_ev_kind_t k) {
    http_status_t rc = stepf(in);
    CHECK_EQ_I64(t, rc, HTTP_OK);
    CHECK_EQ_I64(t, g_ev.kind, k);
    return rc == HTTP_OK && g_ev.kind == k;
}


/* ---- table of valid messages ----------------------------------------------------------------------------- */
typedef struct {
    const char *in;
    size_t len;
    const char *want; /* transcript up to (excluding) the error line */
    int code;         /* -1: no error */
    int eof;
} case_t;

#define C(input, want) {(input), sizeof(input) - 1, (want), -1, 0}
#define CE(input, want) {(input), sizeof(input) - 1, (want), -1, 1}
#define CX(input, want, code) {(input), sizeof(input) - 1, (want), (int)(code), 1}

static void run_case(ctest_t *t, const case_t *c) {
    size_t i;
    run(c->in, c->len, 0, 0, 0, c->eof, &g_ta);
    tr_expect(t, &g_ta, c->want, c->code);
    run(c->in, c->len, 0, 0, 1, c->eof, &g_tb);
    CHECK(t, tr_same(&g_ta, &g_tb));
    for (i = 1; i < c->len; i += 1 + c->len / 40) {
        run(c->in, c->len, i, i, 0, c->eof, &g_tb);
        CHECK(t, tr_same(&g_ta, &g_tb));
    }
}

static void run_cases(ctest_t *t, const case_t *cs, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        run_case(t, &cs[i]);
    }
}


static bool is(core_str_t s, const char *w) { return core_str_eq(s, core_str_from_cstr(w)); }

#define HEAD "HTTP/1.1 200 OK\r\n"
#define CL0 "Content-Length: 0\r\n\r\n"

/* Fields that do not belong to the event kind must be zero. */
static void only_header_fields(ctest_t *t) {
    CHECK_EQ_I64(t, g_ev.status, 0);
    CHECK_EQ_I64(t, g_ev.minor, 0);
    CHECK_EQ_U64(t, g_ev.reason.len, 0);
    CHECK_EQ_I64(t, g_ev.framing, 0);
    CHECK_EQ_U64(t, g_ev.content_length, 0);
    CHECK_EQ_U64(t, g_ev.data.len, 0);
}

static void t_raw_content_length(ctest_t *t) {
    static const char d[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-A:  v  \r\n\r\nhello";
    core_str_t in = {d, sizeof d - 1};
    CHECK(t, http_parser_init(&g_p));
    if (want_kind(t, &in, HTTP_EV_STATUS)) {
        CHECK_EQ_I64(t, g_ev.status, 200);
        CHECK_EQ_I64(t, g_ev.minor, 1);
        CHECK(t, is(g_ev.reason, "OK"));
        CHECK_EQ_U64(t, g_ev.name.len + g_ev.value.len + g_ev.data.len, 0);
        CHECK_EQ_U64(t, g_ev.framing, 0);
        CHECK_EQ_U64(t, g_ev.content_length, 0);
    }
    CHECK(t, in.ptr == d + 17 && in.len == sizeof d - 1 - 17);
    if (want_kind(t, &in, HTTP_EV_HEADER)) {
        CHECK(t, is(g_ev.name, "content-length"));
        CHECK(t, is(g_ev.value, "5"));
        only_header_fields(t);
    }
    CHECK(t, in.ptr == d + 17 + 19);
    if (want_kind(t, &in, HTTP_EV_HEADER)) {
        CHECK(t, is(g_ev.name, "x-a"));
        CHECK(t, is(g_ev.value, "v"));
    }
    CHECK_EQ_U64(t, in.len, 2 + 5);
    if (want_kind(t, &in, HTTP_EV_HEADERS_END)) {
        CHECK_EQ_I64(t, g_ev.framing, HTTP_FRAMING_LENGTH);
        CHECK_EQ_U64(t, g_ev.content_length, 5);
        CHECK_EQ_U64(t, g_ev.name.len + g_ev.value.len + g_ev.data.len + g_ev.reason.len, 0);
    }
    CHECK_EQ_U64(t, in.len, 5);
    if (want_kind(t, &in, HTTP_EV_BODY)) {
        CHECK(t, is(g_ev.data, "hello"));
        CHECK(t, g_ev.data.ptr == d + sizeof d - 1 - 5);
    }
    CHECK_EQ_U64(t, in.len, 0);
    (void)want_kind(t, &in, HTTP_EV_END);
    CHECK_EQ_U64(t, g_ev.data.len + g_ev.name.len, 0);
    (void)want_kind(t, &in, HTTP_EV_NONE);
    (void)want_kind(t, &in, HTTP_EV_NONE);
}

static void t_raw_chunked(ctest_t *t) {
    static const char d[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                            "5\r\nhello\r\n6;ext=1;x\r\n world\r\n0\r\nX-Sum: abc\r\n\r\n";
    core_str_t in = {d, sizeof d - 1};
    CHECK(t, http_parser_init(&g_p));
    (void)want_kind(t, &in, HTTP_EV_STATUS);
    (void)want_kind(t, &in, HTTP_EV_HEADER);
    if (want_kind(t, &in, HTTP_EV_HEADERS_END)) {
        CHECK_EQ_I64(t, g_ev.framing, HTTP_FRAMING_CHUNKED);
        CHECK_EQ_U64(t, g_ev.content_length, 0);
    }
    CHECK(t, core_str_starts_with(in, LIT("5\r\nhello")));
    if (want_kind(t, &in, HTTP_EV_BODY)) {
        CHECK(t, is(g_ev.data, "hello"));
    }
    CHECK(t, core_str_starts_with(in, LIT("\r\n6;ext=1;x\r\n")));
    if (want_kind(t, &in, HTTP_EV_BODY)) {
        CHECK(t, is(g_ev.data, " world"));
    }
    CHECK(t, core_str_starts_with(in, LIT("\r\n0\r\nX-Sum")));
    if (want_kind(t, &in, HTTP_EV_TRAILER)) {
        CHECK(t, is(g_ev.name, "x-sum"));
        CHECK(t, is(g_ev.value, "abc"));
        only_header_fields(t);
    }
    CHECK(t, core_str_eq(in, LIT("\r\n")));
    (void)want_kind(t, &in, HTTP_EV_END);
    CHECK_EQ_U64(t, in.len, 0);
    (void)want_kind(t, &in, HTTP_EV_NONE);
}

static void t_input_is_consumed_exactly(ctest_t *t) {
    static const char d[] = "HTTP/1.1 200 OK\r\nA: b\r\n\r\n";
    core_str_t in = {d, 10}; /* "HTTP/1.1 2" */
    CHECK(t, http_parser_init(&g_p));
    (void)want_kind(t, &in, HTTP_EV_NONE);
    CHECK_EQ_U64(t, in.len, 0);
    in = (core_str_t){d + 10, 6}; /* "00 OK\r" */
    (void)want_kind(t, &in, HTTP_EV_NONE);
    CHECK_EQ_U64(t, in.len, 0);
    in = (core_str_t){d + 16, 6}; /* "\nA: b\r" */
    if (want_kind(t, &in, HTTP_EV_STATUS)) {
        CHECK(t, is(g_ev.reason, "OK"));
    }
    CHECK_EQ_U64(t, in.len, 5); /* only the LF was needed */
    (void)want_kind(t, &in, HTTP_EV_NONE);
    CHECK_EQ_U64(t, in.len, 0);
    in = (core_str_t){d + 22, sizeof d - 1 - 22}; /* "\n\r\n" */
    if (want_kind(t, &in, HTTP_EV_HEADER)) {
        CHECK(t, is(g_ev.name, "a") && is(g_ev.value, "b"));
    }
    CHECK_EQ_U64(t, in.len, 2);
}

static void t_ev_zeroed_without_event(ctest_t *t) {
    core_str_t in = {NULL, 0};
    static const char d[] = "HTTP/1.1 20";
    CHECK(t, http_parser_init(&g_p));
    (void)want_kind(t, &in, HTTP_EV_NONE);
    CHECK_EQ_U64(t, g_ev.name.len + g_ev.value.len + g_ev.data.len + g_ev.reason.len, 0);
    CHECK_EQ_I64(t, g_ev.status, 0);
    in = (core_str_t){d, sizeof d - 1};
    (void)want_kind(t, &in, HTTP_EV_NONE);
    CHECK_EQ_U64(t, g_ev.name.len + g_ev.value.len + g_ev.data.len + g_ev.reason.len, 0);
    CHECK_EQ_I64(t, g_ev.status, 0);
    CHECK_EQ_I64(t, g_ev.framing, 0);
}

static void t_status_lines(ctest_t *t) {
    static const case_t cs[] = {
        C(HEAD "Content-Length: 5\r\n\r\nhello", S_OK H_("content-length", "5") E_LEN(5) B_("hello") Z_),
        C("HTTP/1.0 404 Not Found\r\n" CL0, "S404.0[Not Found]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200 \r\n" CL0, "S200.1[]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200\r\n" CL0, "S200.1[]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200  OK\r\n" CL0, "S200.1[ OK]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 599 a\tb  c\r\n" CL0, "S599.1[a\tb  c]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200 \xc3\xa9\xff\r\n" CL0, "S200.1[\xc3\xa9\xff]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 299 x\r\n" CL0, "S299.1[x]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 300 x\r\n" CL0, "S300.1[x]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 399 x\r\n" CL0, "S399.1[x]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 400 x\r\n" CL0, "S400.1[x]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 500 x\r\n" CL0, "S500.1[x]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 599 x\r\n" CL0, "S599.1[x]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200 ~!\"#\r\n" CL0, "S200.1[~!\"#]\n" H_("content-length", "0") E_LEN(0) Z_),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_header_normalisation(ctest_t *t) {
    static const case_t cs[] = {
        C(HEAD "X-Foo:   a  b \t\r\n" CL0, S_OK H_("x-foo", "a  b") H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "UPPER-case: V\r\n" CL0, S_OK H_("upper-case", "V") H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "X-Empty:\r\n" CL0, S_OK H_("x-empty", "") H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "X-Sp:    \r\n" CL0, S_OK H_("x-sp", "") H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "X-Tab:\t\tv\t\r\n" CL0, S_OK H_("x-tab", "v") H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "A:b:c\r\n" CL0, S_OK H_("a", "b:c") H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "!#$%&'*+-.^_`|~09azAZ: v\r\n" CL0,
          S_OK H_("!#$%&'*+-.^_`|~09azaz", "v") H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "X: a\tb\x80\xff~\r\n" CL0, S_OK H_("x", "a\tb\x80\xff~") H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "Set-Cookie: a=1\r\nSet-Cookie: a=2\r\n" CL0,
          S_OK H_("set-cookie", "a=1") H_("set-cookie", "a=2") H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "content-LENGTH: 0\r\n\r\n", S_OK H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "X: y\r\nZ: w\r\n" CL0, S_OK H_("x", "y") H_("z", "w") H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "Transfer_Encoding: chunked\r\n\r\nabc", S_OK H_("transfer_encoding", "chunked") E_CLOSE B_("abc")),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_args_and_reinit(ctest_t *t) {
    http_event_t ev;
    core_str_t in = {"x", 1}, bad = {NULL, 3};
    CHECK(t, !http_parser_init(NULL));
    CHECK(t, http_parser_init(&g_p));
    CHECK_EQ_I64(t, http_parser_next(NULL, &in, &ev), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_parser_next(&g_p, NULL, &ev), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_parser_next(&g_p, &in, NULL), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_parser_next(&g_p, &bad, &ev), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_parser_eof(NULL, &ev), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_parser_eof(&g_p, NULL), HTTP_E_ARG);
    /* ARG is not sticky and changes nothing: a normal parse still works */
    {
        static const char d[] = HEAD CL0;
        core_str_t full = {d, sizeof d - 1};
        (void)want_kind(t, &full, HTTP_EV_STATUS);
        CHECK(t, is(g_ev.reason, "OK"));
    }
    /* re-init after a sticky error gives a clean parser */
    in = (core_str_t){"garbage\r\n", 9};
    CHECK(t, http_parser_init(&g_p));
    CHECK_EQ_I64(t, stepf(&in), HTTP_E_STATUS_LINE);
    CHECK_EQ_I64(t, stepf(&in), HTTP_E_STATUS_LINE);
    CHECK(t, http_parser_init(&g_p));
    in = (core_str_t){HEAD, sizeof HEAD - 1};
    (void)want_kind(t, &in, HTTP_EV_STATUS);
}

static void t_error_is_sticky(ctest_t *t) {
    core_str_t in = {"HTTP/9 200 OK\r\n", 15}, good = {HEAD CL0, sizeof(HEAD CL0) - 1};
    CHECK(t, http_parser_init(&g_p));
    CHECK_EQ_I64(t, stepf(&in), HTTP_E_STATUS_LINE);
    CHECK_EQ_I64(t, g_ev.kind, HTTP_EV_NONE);
    CHECK_EQ_I64(t, stepf(&good), HTTP_E_STATUS_LINE);
    CHECK_EQ_U64(t, good.len, sizeof(HEAD CL0) - 1);
    CHECK_EQ_I64(t, g_ev.kind, HTTP_EV_NONE);
    {
        core_str_t none = {NULL, 0};
        CHECK_EQ_I64(t, stepf(&none), HTTP_E_STATUS_LINE);
    }
    {
        http_event_t ev;
        CHECK_EQ_I64(t, http_parser_eof(&g_p, &ev), HTTP_E_STATUS_LINE);
    }
}

CTEST_SUITE("http_parse_basic",
            { "raw_content_length", t_raw_content_length },
            { "raw_chunked", t_raw_chunked },
            { "input_is_consumed_exactly", t_input_is_consumed_exactly },
            { "ev_zeroed_without_event", t_ev_zeroed_without_event },
            { "status_lines", t_status_lines },
            { "header_normalisation", t_header_normalisation },
            { "args_and_reinit", t_args_and_reinit },
            { "error_is_sticky", t_error_is_sticky })
