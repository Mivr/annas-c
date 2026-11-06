/* Tests for http/http.h: parser rejections (Content-Length, Transfer-Encoding, smuggling shapes), each with a positive control. Public header only. */
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

static void t_content_length(ctest_t *t) {
    static const case_t cs[] = {
        R(HEAD "Content-Length: 5\r\nContent-Length: 5\r\n\r\nhello", S_OK H_("content-length", "5"), HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 5\r\nContent-Length: 6\r\n\r\nhello", S_OK H_("content-length", "5"), HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 0\r\nContent-Length: 0\r\n\r\n", S_OK H_("content-length", "0"), HTTP_E_CONTENT_LENGTH),
        R(HEAD "content-length: 5\r\nCONTENT-LENGTH: 5\r\n\r\nhello", S_OK H_("content-length", "5"), HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 5\r\nX: y\r\nContent-Length: 5\r\n\r\nhello", S_OK H_("content-length", "5") H_("x", "y"),
          HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length:\r\n\r\n", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length:    \r\n\r\n", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: +5\r\n\r\nhello", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: -0\r\n\r\n", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: -5\r\n\r\n", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 5 6\r\n\r\nhello", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 5,5\r\n\r\nhello", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 5, 5\r\n\r\nhello", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 5\t6\r\n\r\nhello", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 0x5\r\n\r\nhello", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 5a\r\n\r\nhello", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: a5\r\n\r\nhello", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 1.5\r\n\r\nhello", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 1e1\r\n\r\nhello", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: \xd9\xa5\r\n\r\nhello", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 18446744073709551616\r\n\r\n", S_OK, HTTP_E_CONTENT_LENGTH),
        R(HEAD "Content-Length: 99999999999999999999999999999\r\n\r\n", S_OK, HTTP_E_CONTENT_LENGTH),
        R("HTTP/1.1 204 x\r\nContent-Length: abc\r\n\r\n", "S204.1[x]\n", HTTP_E_CONTENT_LENGTH),
        C(HEAD "Content-Length: 5\r\n\r\nhello", S_OK H_("content-length", "5") E_LEN(5) B_("hello") Z_),
        C(HEAD "Content-Length: 18446744073709551615\r\n\r\n",
          S_OK H_("content-length", "18446744073709551615") E_LEN(18446744073709551615)),
        C(HEAD "Content-Length-X: abc\r\n\r\n", S_OK H_("content-length-x", "abc") E_CLOSE),
        C(HEAD "X-Content-Length: 5\r\n\r\n", S_OK H_("x-content-length", "5") E_CLOSE),
        C(HEAD "Content-Length: 1\r\nX-Content-Length: 5\r\n\r\nz", S_OK H_("content-length", "1") H_("x-content-length", "5") E_LEN(1) B_("z") Z_),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

static void t_transfer_encoding(ctest_t *t) {
    static const case_t cs[] = {
        R(HEAD "Transfer-Encoding: chunked, identity\r\n\r\n0\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: identity, chunked\r\n\r\n0\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: gzip\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: identity\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding:\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: chunkedx\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: xchunked\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: chunked;q=1\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: chunked chunked\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: chunked,chunked\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: \"chunked\"\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: chunk ed\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD TE TE "\r\n0\r\n\r\n", S_OK H_TE, HTTP_E_FRAMING),
        R(HEAD TE "Transfer-Encoding: identity\r\n\r\n0\r\n\r\n", S_OK H_TE, HTTP_E_FRAMING),
        R(HEAD "Transfer-Encoding: identity\r\n" TE "\r\n0\r\n\r\n", S_OK, HTTP_E_FRAMING),
        R(HEAD "Content-Length: 4\r\n" TE "\r\n5\r\nhello\r\n0\r\n\r\n", S_OK H_("content-length", "4"), HTTP_E_FRAMING),
        R(HEAD TE "Content-Length: 4\r\n\r\n5\r\nhello\r\n0\r\n\r\n", S_OK H_TE, HTTP_E_FRAMING),
        R(HEAD "Content-Length: 0\r\n" TE "\r\n0\r\n\r\n", S_OK H_("content-length", "0"), HTTP_E_FRAMING),
        R(HEAD "Content-Length: 4\r\nX: y\r\n" TE "\r\n", S_OK H_("content-length", "4") H_("x", "y"), HTTP_E_FRAMING),
        R(HEAD TE "X: y\r\nContent-Length: 4\r\n\r\n", S_OK H_TE H_("x", "y"), HTTP_E_FRAMING),
        R("HTTP/1.0 200 OK\r\n" TE "\r\n0\r\n\r\n", "S200.0[OK]\n", HTTP_E_FRAMING),
        R("HTTP/1.1 204 x\r\nContent-Length: 5\r\n\r\n", "S204.1[x]\n", HTTP_E_FRAMING),
        R("HTTP/1.1 204 x\r\n" TE "\r\n", "S204.1[x]\n", HTTP_E_FRAMING),
        R("HTTP/1.1 304 x\r\nContent-Length: 1\r\n\r\n", "S304.1[x]\n", HTTP_E_FRAMING),
        R("HTTP/1.1 304 x\r\n" TE "\r\n", "S304.1[x]\n", HTTP_E_FRAMING),
        C(HEAD "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n", S_OK H_TE E_CHUNKED Z_),
        C(HEAD "Transfer-Encoding: CHUNKED\r\n\r\n0\r\n\r\n", S_OK H_("transfer-encoding", "CHUNKED") E_CHUNKED Z_),
        C(HEAD "Transfer-Encoding-X: gzip\r\n\r\n", S_OK H_("transfer-encoding-x", "gzip") E_CLOSE),
        C("HTTP/1.0 200 OK\r\nContent-Length: 1\r\n\r\nx", "S200.0[OK]\n" H_("content-length", "1") E_LEN(1) B_("x") Z_),
        C("HTTP/1.1 204 x\r\nContent-Length: 0\r\n\r\n", "S204.1[x]\n" H_("content-length", "0") E_NONE Z_),
        C("HTTP/1.1 304 x\r\nContent-Length: 0\r\n\r\n", "S304.1[x]\n" H_("content-length", "0") E_NONE Z_),
        C(HEAD "Content-Length: 4\r\nX: y\r\n\r\nabcd", S_OK H_("content-length", "4") H_("x", "y") E_LEN(4) B_("abcd") Z_),
    };
    run_cases(t, cs, sizeof cs / sizeof cs[0]);
}

CTEST_SUITE("http_parse_reject_framing",
            { "content_length", t_content_length },
            { "transfer_encoding", t_transfer_encoding })
