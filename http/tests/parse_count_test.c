/* Tests for http/http.h: parser header and trailer count limit (100) at N-1/N/N+1, counter wrap-around. Public header only. */
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

/* ---- header counts --------------------------------------------------------------------------------------- */

/* A message with `nh` lines "X:v" (plus Transfer-Encoding when chunked) and, when chunked, `nt` trailer lines.
 * Builds the expectation as: every line up to HTTP_MAX_HEADERS yields its event; the first one beyond it is the
 * error. Returns that error code, or -1 when the whole message is accepted. */
static int counted(size_t nh, bool chunked, size_t nt) {
    size_t seen = 0, i;
    int code = -1;
    reset();
    m_str(HEAD);
    p_str(S_OK);
    for (i = 0; i < nh; i++) {
        m_str("X:v\r\n");
        if (code < 0 && seen < HTTP_MAX_HEADERS) {
            p_str(H_("x", "v"));
            seen++;
        } else if (code < 0) {
            code = HTTP_E_TOO_MANY_HEADERS;
        }
    }
    if (chunked) {
        m_str(TE);
        if (code < 0 && seen < HTTP_MAX_HEADERS) {
            p_str(H_TE);
            seen++;
        } else if (code < 0) {
            code = HTTP_E_TOO_MANY_HEADERS;
        }
    }
    m_str("\r\n");
    if (code < 0) {
        p_str(chunked ? E_CHUNKED : E_CLOSE);
    }
    if (!chunked) {
        return code;
    }
    m_str("0\r\n");
    for (i = 0; i < nt; i++) {
        m_str("Y:w\r\n");
        if (code < 0 && seen < HTTP_MAX_HEADERS) {
            p_str(T_("y", "w"));
            seen++;
        } else if (code < 0) {
            code = HTTP_E_TOO_MANY_HEADERS;
        }
    }
    m_str("\r\n");
    if (code < 0) {
        p_str(Z_);
    }
    return code;
}

static void count_case(ctest_t *t, size_t nh, bool chunked, size_t nt, int want_code) {
    int code = counted(nh, chunked, nt);
    CHECK_EQ_I64(t, code, want_code); /* the expectation builder agrees with the stated limit */
    check_msg(t, 0, code);
}

static void t_header_count(ctest_t *t) {
    count_case(t, 0, false, 0, -1);
    count_case(t, HTTP_MAX_HEADERS - 1, false, 0, -1);
    count_case(t, HTTP_MAX_HEADERS, false, 0, -1);
    count_case(t, HTTP_MAX_HEADERS + 1, false, 0, HTTP_E_TOO_MANY_HEADERS);
    count_case(t, HTTP_MAX_HEADERS + 2, false, 0, HTTP_E_TOO_MANY_HEADERS);
    count_case(t, 300, false, 0, HTTP_E_TOO_MANY_HEADERS);
}

static void t_header_count_wraps(ctest_t *t) {
    /* far past the cap (and past 2^8 / 2^16): a narrow counter that keeps counting would wrap back under it */
    count_case(t, 256 + 50, false, 0, HTTP_E_TOO_MANY_HEADERS);
    count_case(t, 256 + 100, false, 0, HTTP_E_TOO_MANY_HEADERS);
    count_case(t, 65536 + 50, false, 0, HTTP_E_TOO_MANY_HEADERS);
    count_case(t, 65536 + 100, false, 0, HTTP_E_TOO_MANY_HEADERS);
    count_case(t, 65536 + 101, false, 0, HTTP_E_TOO_MANY_HEADERS);
}

static void t_trailers_share_the_cap(ctest_t *t) {
    count_case(t, HTTP_MAX_HEADERS - 1, true, 0, -1);  /* 99 + TE = 100 lines */
    count_case(t, HTTP_MAX_HEADERS - 1, true, 1, HTTP_E_TOO_MANY_HEADERS);
    count_case(t, HTTP_MAX_HEADERS, true, 0, HTTP_E_TOO_MANY_HEADERS);
    count_case(t, 50, true, 49, -1);                   /* 50 + TE + 49 = 100 */
    count_case(t, 50, true, 50, HTTP_E_TOO_MANY_HEADERS);
    count_case(t, 0, true, HTTP_MAX_HEADERS - 1, -1);  /* TE + 99 trailers */
    count_case(t, 0, true, HTTP_MAX_HEADERS, HTTP_E_TOO_MANY_HEADERS);
}

CTEST_SUITE("http_parse_count",
            { "header_count", t_header_count },
            { "header_count_wraps", t_header_count_wraps },
            { "trailers_share_the_cap", t_trailers_share_the_cap })
