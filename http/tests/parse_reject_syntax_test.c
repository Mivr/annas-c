/* Tests for http/http.h: parser rejections (status line, header syntax, folding, line endings), each with a positive control. Public header only. */
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
#define P_CH S_OK H_TE E_CHUNKED /* transcript prefix of a chunked message up to its body */
#define R(input, prefix, code) CX(input, prefix, code)

static void t_status_line(ctest_t *t) {
    static const case_t cs[] = {
        R("HTTP/1.1 200 OK\nContent-Length: 0\r\n\r\n", "", HTTP_E_LINE_ENDING),
        R("HTTP/1.1 200 OK\rContent-Length: 0\r\n\r\n", "", HTTP_E_LINE_ENDING),
        R("HTTP/1.1 200 OK\r\r\n", "", HTTP_E_LINE_ENDING),
        R("HTTP/1.1 200 O\nK\r\n", "", HTTP_E_LINE_ENDING),
        R("HTTP/1.1  200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/2 200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.2 200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.11 200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/0.9 200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1. 200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.x 200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("http/1.1 200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1\t200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R(" HTTP/1.1 200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("\r\n", "", HTTP_E_STATUS_LINE),
        R("GET / HTTP/1.1\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 20 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 2000 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 20x OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 +200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 -200 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 200OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 OK\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 \r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 000 x\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 100 Continue\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 101 Switching\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 199 x\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 600 x\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 999 x\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 200 O\0K\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 200 O\x01K\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 200 O\x1f" "K\r\n", "", HTTP_E_STATUS_LINE),
        R("HTTP/1.1 200 O\x7f" "K\r\n", "", HTTP_E_STATUS_LINE),
        /* controls: the nearest valid neighbours */
        C("HTTP/1.0 200 OK\r\n" CL0, "S200.0[OK]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200 OK\r\n" CL0, S_OK H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200\r\n" CL0, "S200.1[]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200 \r\n" CL0, "S200.1[]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200 O K\r\n" CL0, "S200.1[O K]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200 O\tK\r\n" CL0, "S200.1[O\tK]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200 O~K\r\n" CL0, "S200.1[O~K]\n" H_("content-length", "0") E_LEN(0) Z_),
        C("HTTP/1.1 200 O\x80K\r\n" CL0, "S200.1[O\x80K]\n" H_("content-length", "0") E_LEN(0) Z_),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_header_syntax(ctest_t *t) {
    static const case_t cs[] = {
        R(HEAD "Foo\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Foo bar: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Foo : x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Foo\t: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD ": x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD ":\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Fo o: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Fo(o: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Fo)o: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Fo/o: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Fo\"o: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "F@o: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Fo<o: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Fo[o]: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Foo;bar: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Foo,: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Fo\0o: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Fo\x01o: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Fo\x7fo: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Fo\x80o: x\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Transfer-Encoding : chunked\r\n\r\n0\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Transfer-Encoding\t: chunked\r\n\r\n0\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "Content-Length : 5\r\n\r\nhello", S_OK, HTTP_E_HEADER),
        R(HEAD "X: a\0b\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "X: a\x01" "b\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "X: a\x08" "b\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "X: a\x0b" "b\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "X: a\x0c" "b\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "X: a\x1f" "b\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "X: a\x7f" "b\r\n\r\n", S_OK, HTTP_E_HEADER),
        R(HEAD "X: ok\r\nY: a\0b\r\n\r\n", S_OK H_("x", "ok"), HTTP_E_HEADER),
        /* controls */
        C(HEAD "Foo: x\r\n\r\n", S_OK H_("foo", "x") E_CLOSE),
        C(HEAD "Fo-o: x\r\n\r\n", S_OK H_("fo-o", "x") E_CLOSE),
        C(HEAD "X: a\tb\r\n\r\n", S_OK H_("x", "a\tb") E_CLOSE),
        C(HEAD "X: a\x80\xff" "b\r\n\r\n", S_OK H_("x", "a\x80\xff" "b") E_CLOSE),
        C(HEAD "X: a~\x21\r\n\r\n", S_OK H_("x", "a~!") E_CLOSE),
        C(HEAD "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED Z_),
        C(HEAD "Content-Length: 5\r\n\r\nhello", S_OK H_("content-length", "5") E_LEN(5) B_("hello") Z_),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_folding(ctest_t *t) {
    static const case_t cs[] = {
        R(HEAD "Foo: a\r\n b\r\n\r\n", S_OK H_("foo", "a"), HTTP_E_FOLDING),
        R(HEAD "Foo: a\r\n\tb\r\n\r\n", S_OK H_("foo", "a"), HTTP_E_FOLDING),
        R(HEAD "Foo: a\r\n \r\n\r\n", S_OK H_("foo", "a"), HTTP_E_FOLDING),
        R(HEAD " Foo: a\r\n\r\n", S_OK, HTTP_E_FOLDING),
        R(HEAD "\tFoo: a\r\n\r\n", S_OK, HTTP_E_FOLDING),
        R(HEAD " \r\n\r\n", S_OK, HTTP_E_FOLDING),
        R(HEAD " Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n", S_OK, HTTP_E_FOLDING),
        R(HEAD "X: y\r\n Content-Length: 5\r\n\r\nhello", S_OK H_("x", "y"), HTTP_E_FOLDING),
        R(HEAD TE "\r\n0\r\nX: a\r\n b\r\n\r\n", P_CH T_("x", "a"), HTTP_E_FOLDING),
        R(HEAD TE "\r\n0\r\n b: c\r\n\r\n", P_CH, HTTP_E_FOLDING),
        C(HEAD "Foo: a\r\nBar: b\r\n\r\n", S_OK H_("foo", "a") H_("bar", "b") E_CLOSE),
        C(HEAD "Foo: a b\r\n\r\n", S_OK H_("foo", "a b") E_CLOSE),
        C(HEAD TE "\r\n0\r\nX: a\r\nY: b\r\n\r\n", P_CH T_("x", "a") T_("y", "b") Z_),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_line_endings(ctest_t *t) {
    static const case_t cs[] = {
        R(HEAD "X: a\rb\r\n\r\n", S_OK, HTTP_E_LINE_ENDING),
        R(HEAD "X: a\nb\r\n\r\n", S_OK, HTTP_E_LINE_ENDING),
        R(HEAD "X: a\nY: b\r\n\r\n", S_OK, HTTP_E_LINE_ENDING),
        R(HEAD "X: a\r\n\n", S_OK H_("x", "a"), HTTP_E_LINE_ENDING),
        R(HEAD "X: a\r\n\r\r", S_OK H_("x", "a"), HTTP_E_LINE_ENDING),
        R(HEAD "X: a\r\r\n", S_OK, HTTP_E_LINE_ENDING),
        R(HEAD "\n", S_OK, HTTP_E_LINE_ENDING),
        R(HEAD "\rX: a\r\n\r\n", S_OK, HTTP_E_LINE_ENDING),
        R(HEAD "Content-Length: 5\n\r\nhello", S_OK, HTTP_E_LINE_ENDING),
        R(HEAD TE "\r\n5\nhello\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED, HTTP_E_LINE_ENDING),
        R(HEAD TE "\r\n5\rhello\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED, HTTP_E_LINE_ENDING),
        R(HEAD TE "\r\n5\r\r\nhello\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED, HTTP_E_LINE_ENDING),
        R(HEAD TE "\r\n0\n\r\n", S_OK H_TE E_CHUNKED, HTTP_E_LINE_ENDING),
        R(HEAD TE "\r\n0\r\nX: a\nY: b\r\n\r\n", P_CH, HTTP_E_LINE_ENDING),
        R(HEAD TE "\r\n0\r\nX: a\r\n\n", P_CH T_("x", "a"), HTTP_E_LINE_ENDING),
        C(HEAD "X: a\r\n\r\n", S_OK H_("x", "a") E_CLOSE),
        C(HEAD TE "\r\n5\r\nhello\r\n0\r\n\r\n", P_CH B_("hello") Z_),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

CTEST_SUITE("http_parse_reject_syntax",
            { "status_line", t_status_line },
            { "header_syntax", t_header_syntax },
            { "folding", t_folding },
            { "line_endings", t_line_endings })
