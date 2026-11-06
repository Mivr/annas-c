/* Tests for http/http.h: response parser framing (Content-Length, chunked, close, none), end of stream. Public header only. */
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


#define HEAD "HTTP/1.1 200 OK\r\n"
#define CL0 "Content-Length: 0\r\n\r\n"
#define TE "Transfer-Encoding: chunked\r\n"
#define H_TE H_("transfer-encoding", "chunked")

static void t_framing_length(ctest_t *t) {
    static const case_t cs[] = {
        C(HEAD CL0, S_OK H_("content-length", "0") E_LEN(0) Z_),
        C(HEAD "Content-Length: 3\r\n\r\nabc", S_OK H_("content-length", "3") E_LEN(3) B_("abc") Z_),
        C(HEAD "Content-Length: 0007\r\n\r\n1234567", S_OK H_("content-length", "0007") E_LEN(7) B_("1234567") Z_),
        C(HEAD "Content-Length: 18446744073709551615\r\n\r\n",
          S_OK H_("content-length", "18446744073709551615") E_LEN(18446744073709551615)),
        C(HEAD "Content-Length: 000000000000000000000000000000000000005\r\n\r\n12345",
          S_OK H_("content-length", "000000000000000000000000000000000000005") E_LEN(5) B_("12345") Z_),
        C(HEAD "Content-Length: 4\r\n\r\n\r\n\r\n", S_OK H_("content-length", "4") E_LEN(4) B_("\r\n\r\n") Z_),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_framing_close(ctest_t *t) {
    static const case_t cs[] = {
        CE(HEAD "\r\nabc", S_OK E_CLOSE B_("abc") Z_),
        CE(HEAD "\r\n", S_OK E_CLOSE Z_),
        CE("HTTP/1.0 200 OK\r\nServer: x\r\n\r\nab\r\ncd\x01" "ef\r\n\r\n", "S200.0[OK]\n" H_("server", "x") E_CLOSE B_("ab\r\ncd\x01" "ef\r\n\r\n") Z_),
        C(HEAD "\r\nabc", S_OK E_CLOSE B_("abc")),
        C(HEAD "\r\n", S_OK E_CLOSE),
        CE("HTTP/1.1 404 x\r\n\r\nnot found", "S404.1[x]\n" E_CLOSE B_("not found") Z_),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_framing_none(ctest_t *t) {
    static const case_t cs[] = {
        C("HTTP/1.1 204 No Content\r\n\r\n", "S204.1[No Content]\n" E_NONE Z_),
        C("HTTP/1.1 204 No Content\r\n" CL0, "S204.1[No Content]\n" H_("content-length", "0") E_NONE Z_),
        C("HTTP/1.1 304 Not Modified\r\nETag: x\r\n\r\n", "S304.1[Not Modified]\n" H_("etag", "x") E_NONE Z_),
        C("HTTP/1.1 304 x\r\n" CL0, "S304.1[x]\n" H_("content-length", "0") E_NONE Z_),
        CE("HTTP/1.1 204 No Content\r\n\r\n", "S204.1[No Content]\n" E_NONE Z_ "eofnone\n"),
        CX("HTTP/1.1 204 x\r\n\r\nX", "S204.1[x]\n" E_NONE Z_, HTTP_E_EXTRA),
        CX("HTTP/1.1 304 x\r\n\r\nHTTP/1.1 200 OK\r\n\r\n", "S304.1[x]\n" E_NONE Z_, HTTP_E_EXTRA),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_framing_chunked(ctest_t *t) {
    static const case_t cs[] = {
        C(HEAD TE "\r\n5\r\nhello\r\n6;ext=1;x\r\n world\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED B_("hello world") Z_),
        C(HEAD TE "\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED Z_),
        C(HEAD TE "\r\n0;x\r\n\r\n", S_OK H_TE E_CHUNKED Z_),
        C(HEAD TE "\r\n00\r\n\r\n", S_OK H_TE E_CHUNKED Z_),
        C(HEAD TE "\r\nA\r\n0123456789\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED B_("0123456789") Z_),
        C(HEAD TE "\r\na\r\n0123456789\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED B_("0123456789") Z_),
        C(HEAD TE "\r\n0A\r\n0123456789\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED B_("0123456789") Z_),
        C(HEAD TE "\r\n0000000000000000000a\r\n0123456789\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED B_("0123456789") Z_),
        C(HEAD TE "\r\n5;name=\"va;lue\"\r\nhello\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED B_("hello") Z_),
        C(HEAD TE "\r\n5;\r\nhello\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED B_("hello") Z_),
        C(HEAD TE "\r\n5;a\tb\r\nhello\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED B_("hello") Z_),
        C(HEAD TE "\r\n1\r\na\r\n1\r\nb\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED B_("ab") Z_),
        C(HEAD TE "\r\n5\r\n\x01\r\n\xff\x7f\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED B_("\x01\r\n\xff\x7f") Z_),
        C(HEAD TE "\r\n0\r\nX-Sum: abc\r\nY:  z \r\n\r\n", S_OK H_TE E_CHUNKED T_("x-sum", "abc") T_("y", "z") Z_),
        C(HEAD TE "\r\n3\r\nabc\r\n0\r\nContent-Lengthx: 1\r\n\r\n",
          S_OK H_TE E_CHUNKED B_("abc") T_("content-lengthx", "1") Z_),
        C(HEAD "Transfer-Encoding:  CHUNKED \r\n\r\n0\r\n\r\n", S_OK H_("transfer-encoding", "CHUNKED") E_CHUNKED Z_),
        C(HEAD "transfer-encoding: Chunked\r\n\r\n0\r\n\r\n", S_OK H_("transfer-encoding", "Chunked") E_CHUNKED Z_),
        C(HEAD TE "\r\nFFFFFFFFFFFFFFFF\r\nab", S_OK H_TE E_CHUNKED B_("ab")),
        C(HEAD TE "\r\n5\r\nhel", S_OK H_TE E_CHUNKED B_("hel")),
        CX(HEAD TE "\r\n0\r\n\r\nX", S_OK H_TE E_CHUNKED Z_, HTTP_E_EXTRA),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_truncated(ctest_t *t) {
    static const case_t cs[] = {
        CX("", "", HTTP_E_TRUNCATED),
        CX("HTTP/1.1 20", "", HTTP_E_TRUNCATED),
        CX(HEAD, S_OK, HTTP_E_TRUNCATED),
        CX(HEAD "X: y", S_OK, HTTP_E_TRUNCATED),
        CX(HEAD "X: y\r\n", S_OK H_("x", "y"), HTTP_E_TRUNCATED),
        CX(HEAD "X: y\r\n\r", S_OK H_("x", "y"), HTTP_E_TRUNCATED),
        CX(HEAD "Content-Length: 5\r\n\r\n", S_OK H_("content-length", "5") E_LEN(5), HTTP_E_TRUNCATED),
        CX(HEAD "Content-Length: 5\r\n\r\nhell", S_OK H_("content-length", "5") E_LEN(5) B_("hell"), HTTP_E_TRUNCATED),
        CX(HEAD TE "\r\n", S_OK H_TE E_CHUNKED, HTTP_E_TRUNCATED),
        CX(HEAD TE "\r\n5", S_OK H_TE E_CHUNKED, HTTP_E_TRUNCATED),
        CX(HEAD TE "\r\n5\r\nhel", S_OK H_TE E_CHUNKED B_("hel"), HTTP_E_TRUNCATED),
        CX(HEAD TE "\r\n5\r\nhello", S_OK H_TE E_CHUNKED B_("hello"), HTTP_E_TRUNCATED),
        CX(HEAD TE "\r\n5\r\nhello\r\n", S_OK H_TE E_CHUNKED B_("hello"), HTTP_E_TRUNCATED),
        CX(HEAD TE "\r\n5\r\nhello\r\n0\r\n", S_OK H_TE E_CHUNKED B_("hello"), HTTP_E_TRUNCATED),
        CX(HEAD TE "\r\n0\r\nX: y\r\n", S_OK H_TE E_CHUNKED T_("x", "y"), HTTP_E_TRUNCATED),
        CX(HEAD TE "\r\n0\r\n\r", S_OK H_TE E_CHUNKED, HTTP_E_TRUNCATED),
        CE(HEAD CL0, S_OK H_("content-length", "0") E_LEN(0) Z_ "eofnone\n"),
        CE(HEAD "Content-Length: 1\r\n\r\nx", S_OK H_("content-length", "1") E_LEN(1) B_("x") Z_ "eofnone\n"),
        CE(HEAD TE "\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED Z_ "eofnone\n"),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_extra_bytes(ctest_t *t) {
    static const case_t cs[] = {
        CX(HEAD "Content-Length: 3\r\n\r\nabcd", S_OK H_("content-length", "3") E_LEN(3) B_("abc") Z_, HTTP_E_EXTRA),
        CX(HEAD "Content-Length: 3\r\n\r\nabc\r\n", S_OK H_("content-length", "3") E_LEN(3) B_("abc") Z_, HTTP_E_EXTRA),
        CX(HEAD "Content-Length: 0\r\n\r\nx", S_OK H_("content-length", "0") E_LEN(0) Z_, HTTP_E_EXTRA),
        CX(HEAD "Content-Length: 1\r\n\r\nxHTTP/1.1 200 OK\r\n\r\n", S_OK H_("content-length", "1") E_LEN(1) B_("x") Z_,
           HTTP_E_EXTRA),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_extra_is_sticky_and_empty_is_fine(ctest_t *t) {
    static const char d[] = HEAD "Content-Length: 1\r\n\r\nx";
    core_str_t in = {d, sizeof d - 1}, extra = {"y", 1}, none = {NULL, 0};
    CHECK(t, http_parser_init(&g_p));
    while (stepf(&in) == HTTP_OK && g_ev.kind != HTTP_EV_END) {
    }
    CHECK_EQ_I64(t, g_ev.kind, HTTP_EV_END);
    CHECK_EQ_U64(t, in.len, 0);
    (void)want_kind(t, &none, HTTP_EV_NONE);
    (void)want_kind(t, &none, HTTP_EV_NONE);
    CHECK_EQ_I64(t, stepf(&extra), HTTP_E_EXTRA);
    CHECK_EQ_I64(t, g_ev.kind, HTTP_EV_NONE);
    CHECK_EQ_I64(t, stepf(&none), HTTP_E_EXTRA);
    CHECK_EQ_I64(t, stepf(&extra), HTTP_E_EXTRA);
    CHECK_EQ_U64(t, extra.len, 1);
}

static void t_eof_with_pending_end(ctest_t *t) {
    static const char d[] = HEAD CL0;
    core_str_t in = {d, sizeof d - 1};
    http_event_t ev;
    CHECK(t, http_parser_init(&g_p));
    (void)want_kind(t, &in, HTTP_EV_STATUS);
    (void)want_kind(t, &in, HTTP_EV_HEADER);
    (void)want_kind(t, &in, HTTP_EV_HEADERS_END);
    memset(&ev, 0xA5, sizeof ev);
    CHECK_EQ_I64(t, http_parser_eof(&g_p, &ev), HTTP_OK);
    CHECK_EQ_I64(t, ev.kind, HTTP_EV_END);
    memset(&ev, 0xA5, sizeof ev);
    CHECK_EQ_I64(t, http_parser_eof(&g_p, &ev), HTTP_OK);
    CHECK_EQ_I64(t, ev.kind, HTTP_EV_NONE);
    in = (core_str_t){NULL, 0};
    (void)want_kind(t, &in, HTTP_EV_NONE);
}

static void t_eof_close_is_not_end_before_eof(ctest_t *t) {
    static const char d[] = HEAD "\r\nabc";
    core_str_t in = {d, sizeof d - 1}, none = {NULL, 0};
    http_event_t ev;
    CHECK(t, http_parser_init(&g_p));
    (void)want_kind(t, &in, HTTP_EV_STATUS);
    (void)want_kind(t, &in, HTTP_EV_HEADERS_END);
    (void)want_kind(t, &in, HTTP_EV_BODY);
    (void)want_kind(t, &none, HTTP_EV_NONE);
    (void)want_kind(t, &none, HTTP_EV_NONE);
    memset(&ev, 0xA5, sizeof ev);
    CHECK_EQ_I64(t, http_parser_eof(&g_p, &ev), HTTP_OK);
    CHECK_EQ_I64(t, ev.kind, HTTP_EV_END);
    CHECK_EQ_U64(t, ev.data.len, 0);
    (void)want_kind(t, &none, HTTP_EV_NONE);
}

static void t_truncated_is_sticky(ctest_t *t) {
    static const char d[] = HEAD "Content-Length: 5\r\n\r\nab";
    core_str_t in = {d, sizeof d - 1}, more = {"cde", 3};
    http_event_t ev;
    CHECK(t, http_parser_init(&g_p));
    while (stepf(&in) == HTTP_OK && in.len > 0) {
    }
    memset(&ev, 0xA5, sizeof ev);
    CHECK_EQ_I64(t, http_parser_eof(&g_p, &ev), HTTP_E_TRUNCATED);
    CHECK_EQ_I64(t, ev.kind, HTTP_EV_NONE);
    CHECK_EQ_I64(t, http_parser_eof(&g_p, &ev), HTTP_E_TRUNCATED);
    CHECK_EQ_I64(t, stepf(&more), HTTP_E_TRUNCATED);
    CHECK_EQ_U64(t, more.len, 3);
}

CTEST_SUITE("http_parse_framing",
            { "framing_length", t_framing_length },
            { "framing_close", t_framing_close },
            { "framing_none", t_framing_none },
            { "framing_chunked", t_framing_chunked },
            { "truncated", t_truncated },
            { "extra_bytes", t_extra_bytes },
            { "extra_is_sticky_and_empty_is_fine", t_extra_is_sticky_and_empty_is_fine },
            { "eof_with_pending_end", t_eof_with_pending_end },
            { "eof_close_is_not_end_before_eof", t_eof_close_is_not_end_before_eof },
            { "truncated_is_sticky", t_truncated_is_sticky })
