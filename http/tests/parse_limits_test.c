/* Tests for http/http.h: parser line and header-count limits at N-1/N/N+1. Public header only. */
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

#define HEAD "HTTP/1.1 200 OK\r\n"
#define TE "Transfer-Encoding: chunked\r\n"
#define H_TE H_("transfer-encoding", "chunked")
#define P_CH S_OK H_TE E_CHUNKED

/* ---- message and expectation builders (static storage; the checks never use big stack buffers) -------- */
static char g_msg[1u << 19];
static size_t g_len;
static char g_pre[TR_CAP];
static size_t g_plen;
static bool g_over;

static void m_put(const char *s, size_t n) {
    if (n > sizeof g_msg - g_len) {
        g_over = true;
        return;
    }
    memcpy(g_msg + g_len, s, n);
    g_len += n;
}
static void m_str(const char *s) { m_put(s, strlen(s)); }
static void m_rep(char c, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        m_put(&c, 1);
    }
}
static void p_put(const char *s, size_t n) {
    if (n > sizeof g_pre - 1 - g_plen) {
        g_over = true;
        return;
    }
    memcpy(g_pre + g_plen, s, n);
    g_plen += n;
    g_pre[g_plen] = 0;
}
static void p_str(const char *s) { p_put(s, strlen(s)); }
static void p_rep(char c, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        p_put(&c, 1);
    }
}
static void reset(void) {
    g_len = 0;
    g_plen = 0;
    g_pre[0] = 0;
    g_over = false;
}

/* Runs g_msg whole, byte by byte and split in the middle; all three transcripts must be the expectation. */
static void check_msg(ctest_t *t, int eof, int code) {
    CHECK(t, !g_over);
    run(g_msg, g_len, 0, 0, 0, eof, &g_ta);
    tr_expect(t, &g_ta, g_pre, code);
    run(g_msg, g_len, 0, 0, 1, eof, &g_tb);
    CHECK(t, tr_same(&g_ta, &g_tb));
    run(g_msg, g_len, g_len / 2, g_len / 2, 0, eof, &g_tb);
    CHECK(t, tr_same(&g_ta, &g_tb));
}

enum { K_STATUS, K_HEADER, K_CHUNK, K_TRAILER };

/* Start of the message up to the line under test; the expectation of the events before it. */
static void line_prefix(int kind) {
    switch (kind) {
    case K_STATUS:
        break;
    case K_HEADER:
        m_str(HEAD);
        p_str(S_OK);
        break;
    case K_CHUNK:
        m_str(HEAD TE "\r\n");
        p_str(P_CH);
        break;
    default:
        m_str(HEAD TE "\r\n0\r\n");
        p_str(P_CH);
        break;
    }
}

/* A message whose line under test has exactly `len` content bytes (without its CRLF). */
static void build_line(int kind, size_t len, bool complete) {
    reset();
    line_prefix(kind);
    switch (kind) {
    case K_STATUS:
        m_str("HTTP/1.1 200 ");
        m_rep('a', len - 13);
        break;
    case K_HEADER:
        m_str("x-long: ");
        m_rep('a', len - 8);
        break;
    case K_CHUNK:
        m_str("1;");
        m_rep('a', len - 2);
        break;
    default:
        m_str("x: ");
        m_rep('a', len - 3);
        break;
    }
    if (!complete) {
        return;
    }
    m_str("\r\n");
    switch (kind) {
    case K_STATUS:
        m_str("Content-Length: 0\r\n\r\n");
        break;
    case K_HEADER:
        m_str("\r\n");
        break;
    case K_CHUNK:
        m_str("z\r\n0\r\n\r\n");
        break;
    default:
        m_str("\r\n");
        break;
    }
}

/* Expectation for a complete line that is accepted. */
static void expect_ok_line(int kind, size_t len) {
    switch (kind) {
    case K_STATUS:
        p_str("S200.1[");
        p_rep('a', len - 13);
        p_str("]\n" H_("content-length", "0") E_LEN(0) Z_);
        break;
    case K_HEADER:
        p_str("H[x-long]=[");
        p_rep('a', len - 8);
        p_str("]\n" E_CLOSE);
        break;
    case K_CHUNK:
        p_str(B_("z") Z_);
        break;
    default:
        p_str("T[x]=[");
        p_rep('a', len - 3);
        p_str("]\n" Z_);
        break;
    }
}

static void line_limit(ctest_t *t, int kind) {
    size_t n;
    for (n = HTTP_MAX_LINE - 1; n <= HTTP_MAX_LINE + 1; n++) {
        build_line(kind, n, true);
        if (n <= HTTP_MAX_LINE) {
            expect_ok_line(kind, n);
            check_msg(t, 0, -1);
        } else {
            check_msg(t, 0, HTTP_E_LINE_TOO_LONG);
        }
    }
}

/* The limit is enforced as the bytes arrive, not when (if ever) the CRLF comes. */
static void line_limit_early(ctest_t *t, int kind) {
    build_line(kind, HTTP_MAX_LINE, false);
    check_msg(t, 1, HTTP_E_TRUNCATED);
    build_line(kind, HTTP_MAX_LINE + 1, false);
    check_msg(t, 0, HTTP_E_LINE_TOO_LONG);
    build_line(kind, HTTP_MAX_LINE + 1, false);
    check_msg(t, 1, HTTP_E_LINE_TOO_LONG);
}

static void t_status_line_limit(ctest_t *t) { line_limit(t, K_STATUS); }
static void t_header_line_limit(ctest_t *t) { line_limit(t, K_HEADER); }
static void t_chunk_line_limit(ctest_t *t) { line_limit(t, K_CHUNK); }
static void t_trailer_line_limit(ctest_t *t) { line_limit(t, K_TRAILER); }

static void t_line_limit_is_early(ctest_t *t) {
    line_limit_early(t, K_STATUS);
    line_limit_early(t, K_HEADER);
    line_limit_early(t, K_CHUNK);
    line_limit_early(t, K_TRAILER);
}

static void t_line_limit_cr_at_boundary(ctest_t *t) {
    /* content of exactly HTTP_MAX_LINE bytes followed by a lone CR: waiting for the LF, no error yet */
    build_line(K_HEADER, HTTP_MAX_LINE, false);
    m_str("\r");
    check_msg(t, 1, HTTP_E_TRUNCATED);
    /* ... and followed by CR + a non-LF: a line-ending error, not a length error */
    build_line(K_HEADER, HTTP_MAX_LINE, false);
    m_str("\rX");
    check_msg(t, 0, HTTP_E_LINE_ENDING);
    /* content of HTTP_MAX_LINE+1 bytes: the length error wins even if a CR follows */
    build_line(K_HEADER, HTTP_MAX_LINE + 1, false);
    m_str("\r\n");
    check_msg(t, 0, HTTP_E_LINE_TOO_LONG);
}

CTEST_SUITE("http_parse_limits",
            { "status_line_limit", t_status_line_limit },
            { "header_line_limit", t_header_line_limit },
            { "chunk_line_limit", t_chunk_line_limit },
            { "trailer_line_limit", t_trailer_line_limit },
            { "line_limit_is_early", t_line_limit_is_early },
            { "line_limit_cr_at_boundary", t_line_limit_cr_at_boundary })
