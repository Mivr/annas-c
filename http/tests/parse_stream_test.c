/* Tests for http/http.h: parser streams large bodies in bounded memory. Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/buf.h"
#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

#define HEAD "HTTP/1.1 200 OK\r\n"
#define TE "Transfer-Encoding: chunked\r\n"

static http_parser_t g_p;
static char g_msg[1u << 19];
static size_t g_len;
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
static void reset(void) {
    g_len = 0;
    g_over = false;
}

/* ---- streaming: memory does not grow with the body --------------------------------------------------------- */
static unsigned char g_piece[4096];

static unsigned char pat(uint64_t i) { return (unsigned char)((i * 7u + 3u) & 0xffu); }

/* Feeds head[0..hl), then `total` pattern bytes in pieces of `step`; counts and verifies BODY data. */
static uint64_t stream_body(ctest_t *t, const char *head, size_t hl, uint64_t total, size_t step, int *saw_end) {
    core_str_t in = {head, hl};
    http_event_t ev;
    uint64_t got = 0, fed = 0;
    unsigned guard = 0;
    bool bad = false;
    *saw_end = 0;
    CHECK(t, http_parser_init(&g_p));
    for (;;) {
        http_status_t rc = http_parser_next(&g_p, &in, &ev);
        size_t k;
        if (rc != HTTP_OK) {
            CHECK_EQ_I64(t, rc, HTTP_OK);
            return got;
        }
        if (ev.kind == HTTP_EV_BODY) {
            for (k = 0; k < ev.data.len; k++) {
                bad = bad || (unsigned char)ev.data.ptr[k] != pat(got + k);
            }
            got += ev.data.len;
        } else if (ev.kind == HTTP_EV_END) {
            *saw_end = 1;
            break;
        } else if (ev.kind == HTTP_EV_NONE) {
            size_t n = step;
            if (fed >= total) {
                break;
            }
            if (n > total - fed) {
                n = (size_t)(total - fed);
            }
            for (k = 0; k < n; k++) {
                g_piece[k] = pat(fed + k);
            }
            fed += n;
            in = (core_str_t){(const char *)g_piece, n};
        }
        if (++guard > 8000000u) {
            CHECK(t, 0);
            break;
        }
    }
    CHECK(t, !bad);
    return got;
}

static void t_stream_content_length(ctest_t *t) {
    static const char head[] = HEAD "Content-Length: 3145728\r\n\r\n";
    int end;
    uint64_t got = stream_body(t, head, sizeof head - 1, 3145728u, sizeof g_piece, &end);
    CHECK_EQ_U64(t, got, 3145728u);
    CHECK_EQ_I64(t, end, 1);
    got = stream_body(t, head, sizeof head - 1, 3145728u, 1000, &end);
    CHECK_EQ_U64(t, got, 3145728u);
    CHECK_EQ_I64(t, end, 1);
}

static void t_stream_byte_wise(ctest_t *t) {
    static const char head[] = HEAD "Content-Length: 300000\r\n\r\n";
    int end;
    uint64_t got = stream_body(t, head, sizeof head - 1, 300000u, 1, &end);
    CHECK_EQ_U64(t, got, 300000u);
    CHECK_EQ_I64(t, end, 1);
}

static void t_stream_huge_declared_length(ctest_t *t) {
    /* a length near 2^64 must not wrap or be rejected, and must not end early */
    static const char head[] = HEAD "Content-Length: 18446744073709551615\r\n\r\n";
    int end;
    uint64_t got = stream_body(t, head, sizeof head - 1, 1048576u, sizeof g_piece, &end);
    CHECK_EQ_U64(t, got, 1048576u);
    CHECK_EQ_I64(t, end, 0);
    {
        static const char head2[] = HEAD "Content-Length: 4294967296\r\n\r\n";
        got = stream_body(t, head2, sizeof head2 - 1, 1048576u, sizeof g_piece, &end);
        CHECK_EQ_U64(t, got, 1048576u);
        CHECK_EQ_I64(t, end, 0);
    }
}

static void t_stream_close(ctest_t *t) {
    static const char head[] = HEAD "\r\n";
    int end;
    uint64_t got = stream_body(t, head, sizeof head - 1, 2097152u, sizeof g_piece, &end);
    CHECK_EQ_U64(t, got, 2097152u);
    CHECK_EQ_I64(t, end, 0); /* only http_parser_eof ends a read-until-close body */
}

/* A chunked body of `chunks` chunks of 1024 bytes, delivered in pieces of `step`. */
static void chunked_stream(ctest_t *t, size_t chunks, size_t step) {
    static const char head[] = HEAD TE "\r\n";
    core_str_t in = {head, sizeof head - 1};
    http_event_t ev;
    uint64_t got = 0;
    size_t sent = 0, off = 0, total, k;
    bool bad = false, ended = false;
    unsigned guard = 0;
    reset();
    for (k = 0; k < chunks; k++) {
        m_str("400\r\n");
        m_rep('p', 1024);
        m_str("\r\n");
    }
    m_str("0\r\n\r\n");
    CHECK(t, !g_over);
    total = g_len;
    CHECK(t, http_parser_init(&g_p));
    while (guard++ < 4000000u) {
        http_status_t rc = http_parser_next(&g_p, &in, &ev);
        if (rc != HTTP_OK) {
            CHECK_EQ_I64(t, rc, HTTP_OK);
            return;
        }
        if (ev.kind == HTTP_EV_BODY) {
            for (k = 0; k < ev.data.len; k++) {
                bad = bad || ev.data.ptr[k] != 'p';
            }
            got += ev.data.len;
        } else if (ev.kind == HTTP_EV_END) {
            ended = true;
            break;
        } else if (ev.kind == HTTP_EV_NONE) {
            size_t n = step < total - off ? step : total - off;
            if (n == 0) {
                break;
            }
            in = (core_str_t){g_msg + off, n};
            off += n;
            sent++;
        }
    }
    CHECK(t, sent > 0);
    CHECK(t, !bad);
    CHECK(t, ended);
    CHECK_EQ_U64(t, got, (uint64_t)chunks * 1024u);
}

static void t_stream_chunked(ctest_t *t) {
    chunked_stream(t, 300, 4096);
    chunked_stream(t, 300, 1000);
    chunked_stream(t, 64, 1);
    chunked_stream(t, 1, 7);
}

CTEST_SUITE("http_parse_stream",
            { "stream_content_length", t_stream_content_length },
            { "stream_byte_wise", t_stream_byte_wise },
            { "stream_huge_declared_length", t_stream_huge_declared_length },
            { "stream_close", t_stream_close },
            { "stream_chunked", t_stream_chunked })
