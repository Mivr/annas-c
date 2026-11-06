/* Tests for http/http.h: parser feed-split exhaustive and mutation-differential tests. Public header only. */
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

#define TE "Transfer-Encoding: chunked\r\n"
#define H_TE H_("transfer-encoding", "chunked")

typedef struct {
    const char *in;
    size_t len;
    const char *want;
    int eof;
} msg_t;

#define MSG(input, want, eof) {(input), sizeof(input) - 1, (want), (eof)}

static const msg_t g_msgs[] = {
    /* 0: Content-Length */
    MSG("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nX-A:  v \r\nContent-Length: 11\r\n\r\nhello world",
        S_OK H_("content-type", "text/plain") H_("x-a", "v") H_("content-length", "11") E_LEN(11) B_("hello world") Z_, 0),
    /* 1: chunked with an extension, trailers and a multi-chunk body */
    MSG("HTTP/1.1 200 OK\r\n" TE "Server: s\r\n\r\n5;e=1\r\nhello\r\n6\r\n world\r\n0\r\nX-Sum: abc\r\nY: z\r\n\r\n",
        S_OK H_TE H_("server", "s") E_CHUNKED B_("hello world") T_("x-sum", "abc") T_("y", "z") Z_, 0),
    /* 2: read until close */
    MSG("HTTP/1.0 200 OK\r\nServer: x\r\n\r\nab\r\ncd", "S200.0[OK]\n" H_("server", "x") E_CLOSE B_("ab\r\ncd") Z_, 1),
    /* 3: no body */
    MSG("HTTP/1.1 204 No Content\r\nX: y\r\n\r\n", "S204.1[No Content]\n" H_("x", "y") E_NONE Z_, 0),
    /* 4: chunk data holding CRLF and high bytes */
    MSG("HTTP/1.1 200 OK\r\n" TE "\r\n4\r\n\r\n\r\n\r\n3\r\n\x01\xff\x7f\r\n0\r\n\r\n",
        S_OK H_TE E_CHUNKED B_("\r\n\r\n\x01\xff\x7f") Z_, 0),
};
#define NMSG (sizeof g_msgs / sizeof g_msgs[0])

static void expect_run(ctest_t *t, const msg_t *m, size_t c1, size_t c2, size_t step) {
    run(m->in, m->len, c1, c2, step, m->eof, &g_ta);
    tr_expect(t, &g_ta, m->want, -1);
}

static void t_whole_and_bytewise_exact(ctest_t *t) {
    size_t i;
    for (i = 0; i < NMSG; i++) {
        expect_run(t, &g_msgs[i], 0, 0, 0);
        expect_run(t, &g_msgs[i], 0, 0, 1);
        expect_run(t, &g_msgs[i], 0, 0, 2);
        expect_run(t, &g_msgs[i], 0, 0, 3);
        expect_run(t, &g_msgs[i], 0, 0, 5);
        expect_run(t, &g_msgs[i], 0, 0, 7);
    }
}

static void t_every_two_way_split(ctest_t *t) {
    size_t i, c;
    for (i = 0; i < NMSG; i++) {
        for (c = 0; c <= g_msgs[i].len; c++) {
            expect_run(t, &g_msgs[i], c, c, 0);
        }
    }
}

static void t_every_split_then_bytewise(ctest_t *t) {
    size_t i, c;
    for (i = 0; i < NMSG; i++) {
        for (c = 0; c <= g_msgs[i].len; c++) {
            expect_run(t, &g_msgs[i], c, c, 1);
        }
    }
}

static void t_every_three_way_split(ctest_t *t) {
    static const size_t pick[] = {1, 3, 4};
    size_t i, c1, c2;
    for (i = 0; i < sizeof pick / sizeof pick[0]; i++) {
        const msg_t *m = &g_msgs[pick[i]];
        for (c1 = 0; c1 <= m->len; c1++) {
            for (c2 = c1; c2 <= m->len; c2++) {
                expect_run(t, m, c1, c2, 0);
            }
        }
    }
}

/* A message cut short is TRUNCATED at end of stream, never silently accepted. */
static void t_every_prefix_is_truncated(ctest_t *t) {
    static const size_t pick[] = {0, 1, 3, 4};
    char tail[32];
    size_t i, l;
    snprintf(tail, sizeof tail, "!%d\n", (int)HTTP_E_TRUNCATED);
    for (i = 0; i < sizeof pick / sizeof pick[0]; i++) {
        const msg_t *m = &g_msgs[pick[i]];
        for (l = 0; l < m->len; l++) {
            size_t tl = strlen(tail);
            run(m->in, l, 0, 0, 0, 1, &g_ta);
            CHECK(t, !g_ta.full && g_ta.n >= tl && memcmp(g_ta.b + g_ta.n - tl, tail, tl) == 0);
            run(m->in, l, 0, 0, 1, 1, &g_tb);
            CHECK(t, tr_same(&g_ta, &g_tb));
        }
        run(m->in, m->len, 0, 0, 0, 1, &g_ta);
        CHECK(t, strchr(g_ta.b, '!') == NULL);
    }
}

/* Close-delimited: every prefix beyond the head is a complete message once the stream ends. */
static void t_close_prefixes_complete_at_eof(ctest_t *t) {
    static const char head[] = "HTTP/1.0 200 OK\r\nServer: x\r\n\r\n";
    const msg_t *m = &g_msgs[2];
    size_t l;
    for (l = sizeof head - 1; l <= m->len; l++) {
        run(m->in, l, 0, 0, 0, 1, &g_ta);
        CHECK(t, !g_ta.full && g_ta.n >= 2 && memcmp(g_ta.b + g_ta.n - 2, "Z\n", 2) == 0);
        CHECK(t, strchr(g_ta.b, '!') == NULL);
        run(m->in, l, 0, 0, 1, 1, &g_tb);
        CHECK(t, tr_same(&g_ta, &g_tb));
    }
    for (l = 0; l < sizeof head - 1; l++) {
        run(m->in, l, 0, 0, 0, 1, &g_ta);
        CHECK(t, strstr(g_ta.b, "!") != NULL);
    }
}

static void tally(const tr_t *r, size_t *rejected, size_t *accepted) {
    if (strchr(r->b, '!') != NULL) {
        (*rejected)++;
    } else {
        (*accepted)++;
    }
}

/* Every single-byte mutation of every message: the verdict (events and error) is the same however the bytes
 * are cut. Whole, byte-wise, and cut at the mutated byte. */
static char g_mut[256];

static void t_mutations_are_split_invariant(ctest_t *t) {
    static const unsigned char muts[] = {'\r', '\n', ' ', '\t', ':', ';', '0', '5', 'a', 'F', 'x', '-', 0, 0x01, 0x7f, 0x80, 0xff};
    size_t i, p, k, rejected = 0, accepted = 0;
    for (i = 0; i < NMSG; i++) {
        const msg_t *m = &g_msgs[i];
        CHECK(t, m->len < sizeof g_mut);
        for (p = 0; p < m->len; p++) {
            for (k = 0; k < sizeof muts; k++) {
                memcpy(g_mut, m->in, m->len);
                g_mut[p] = (char)muts[k];
                run(g_mut, m->len, 0, 0, 0, m->eof, &g_ta);
                run(g_mut, m->len, 0, 0, 1, m->eof, &g_tb);
                CHECK(t, tr_same(&g_ta, &g_tb));
                run(g_mut, m->len, p, p, 0, m->eof, &g_tb);
                CHECK(t, tr_same(&g_ta, &g_tb));
                run(g_mut, m->len, p + 1, p + 1, 1, m->eof, &g_tb);
                CHECK(t, tr_same(&g_ta, &g_tb));
                CHECK(t, strstr(g_ta.b, "!emptybody") == NULL && strstr(g_ta.b, "!notinput") == NULL);
                CHECK(t, strstr(g_ta.b, "!none_with_input") == NULL && strstr(g_ta.b, "!loop") == NULL);
                CHECK(t, strstr(g_ta.b, "!errevent") == NULL && strstr(g_ta.b, "!init") == NULL);
                tally(&g_ta, &rejected, &accepted);
            }
        }
    }
    CHECK(t, rejected > 500);
    CHECK(t, accepted > 100);
}

/* Same, with a second mutation to reach error-after-error shapes: the first violation decides. */
static void t_double_mutations_are_split_invariant(ctest_t *t) {
    static const unsigned char muts[] = {'\r', '\n', ':', 0x01, 'g'};
    size_t i, p, q, k, rejected = 0, accepted = 0;
    for (i = 0; i < 2; i++) {
        const msg_t *m = &g_msgs[i == 0 ? 0 : 1];
        for (p = 0; p < m->len; p += 3) {
            for (q = p + 1; q < m->len; q += 5) {
                for (k = 0; k < sizeof muts; k++) {
                    memcpy(g_mut, m->in, m->len);
                    g_mut[p] = (char)muts[k];
                    g_mut[q] = (char)muts[(k + 2) % sizeof muts];
                    run(g_mut, m->len, 0, 0, 0, m->eof, &g_ta);
                    run(g_mut, m->len, 0, 0, 1, m->eof, &g_tb);
                    CHECK(t, tr_same(&g_ta, &g_tb));
                    run(g_mut, m->len, q, q, 0, m->eof, &g_tb);
                    CHECK(t, tr_same(&g_ta, &g_tb));
                    tally(&g_ta, &rejected, &accepted);
                }
            }
        }
    }
    CHECK(t, rejected > 200);
    CHECK(t, accepted > 50);
}

/* Deletions and insertions shift every later byte: same invariance. */
static void t_edits_are_split_invariant(ctest_t *t) {
    size_t i, p, rejected = 0, accepted = 0;
    for (i = 0; i < NMSG; i++) {
        const msg_t *m = &g_msgs[i];
        for (p = 0; p < m->len; p++) {
            size_t n;
            /* delete byte p */
            memcpy(g_mut, m->in, p);
            memcpy(g_mut + p, m->in + p + 1, m->len - p - 1);
            n = m->len - 1;
            run(g_mut, n, 0, 0, 0, m->eof, &g_ta);
            run(g_mut, n, 0, 0, 1, m->eof, &g_tb);
            CHECK(t, tr_same(&g_ta, &g_tb));
            run(g_mut, n, p, p, 0, m->eof, &g_tb);
            CHECK(t, tr_same(&g_ta, &g_tb));
            tally(&g_ta, &rejected, &accepted);
            /* duplicate byte p */
            memcpy(g_mut, m->in, p + 1);
            memcpy(g_mut + p + 1, m->in + p, m->len - p);
            n = m->len + 1;
            run(g_mut, n, 0, 0, 0, m->eof, &g_ta);
            run(g_mut, n, 0, 0, 1, m->eof, &g_tb);
            CHECK(t, tr_same(&g_ta, &g_tb));
            run(g_mut, n, p, p, 0, m->eof, &g_tb);
            CHECK(t, tr_same(&g_ta, &g_tb));
            tally(&g_ta, &rejected, &accepted);
        }
    }
    CHECK(t, rejected > 50);
    CHECK(t, accepted > 50);
}

static void t_two_messages_back_to_back_rejected(ctest_t *t) {
    /* pipelined second response: always EXTRA, whatever the cut */
    static const char two[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nokHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    static const char want[] = S_OK H_("content-length", "2") E_LEN(2) B_("ok") Z_;
    size_t c;
    for (c = 0; c <= sizeof two - 1; c++) {
        run(two, sizeof two - 1, c, c, 0, 0, &g_ta);
        tr_expect(t, &g_ta, want, HTTP_E_EXTRA);
    }
    run(two, sizeof two - 1, 0, 0, 1, 0, &g_ta);
    tr_expect(t, &g_ta, want, HTTP_E_EXTRA);
}

CTEST_SUITE("http_parse_split",
            { "whole_and_bytewise_exact", t_whole_and_bytewise_exact },
            { "every_two_way_split", t_every_two_way_split },
            { "every_split_then_bytewise", t_every_split_then_bytewise },
            { "every_three_way_split", t_every_three_way_split },
            { "every_prefix_is_truncated", t_every_prefix_is_truncated },
            { "close_prefixes_complete_at_eof", t_close_prefixes_complete_at_eof },
            { "mutations_are_split_invariant", t_mutations_are_split_invariant },
            { "double_mutations_are_split_invariant", t_double_mutations_are_split_invariant },
            { "edits_are_split_invariant", t_edits_are_split_invariant },
            { "two_messages_back_to_back_rejected", t_two_messages_back_to_back_rejected })
