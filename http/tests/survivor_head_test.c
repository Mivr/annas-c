/* Tests for http/http.h: status-line digits and reason, Transfer-Encoding spelling, chunk-size lines. Each pins an
 * exact status code or event value. Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static http_parser_t g_p;

/* Feeds `msg` whole; returns the first error, else HTTP_OK. *status gets the STATUS event's code (or -1), *minor its minor. */
static http_status_t run(core_str_t msg, int *status, int *minor, core_str_t *reason) {
    http_event_t ev;
    http_status_t rc = HTTP_OK;
    int guard;
    (void)http_parser_init(&g_p);
    *status = -1;
    *minor = -1;
    for (guard = 0; guard < 64 && rc == HTTP_OK; guard++) {
        rc = http_parser_next(&g_p, &msg, &ev);
        if (rc == HTTP_OK && ev.kind == HTTP_EV_STATUS) {
            *status = ev.status;
            *minor = ev.minor;
            if (reason != NULL) {
                static char keep[128];
                memcpy(keep, ev.reason.ptr, ev.reason.len);
                *reason = (core_str_t){keep, ev.reason.len};
            }
        }
        if (rc == HTTP_OK && (ev.kind == HTTP_EV_NONE || ev.kind == HTTP_EV_END)) {
            break;
        }
    }
    return rc;
}

static void line_ok(ctest_t *t, const char *line, int want, int want_minor, const char *want_reason) {
    char buf[128];
    int st, mi;
    core_str_t reason = {NULL, 0};
    snprintf(buf, sizeof buf, "%s\r\nContent-Length: 0\r\n\r\n", line);
    CHECK_EQ_I64(t, run(core_str_from_cstr(buf), &st, &mi, &reason), HTTP_OK);
    CHECK_EQ_I64(t, st, want);
    CHECK_EQ_I64(t, mi, want_minor);
    CHECK(t, core_str_eq(reason, core_str_from_cstr(want_reason)));
}

static void line_bad(ctest_t *t, const char *line) {
    char buf[128];
    int st, mi;
    snprintf(buf, sizeof buf, "%s\r\n\r\n", line);
    CHECK_EQ_I64(t, run(core_str_from_cstr(buf), &st, &mi, NULL), HTTP_E_STATUS_LINE);
}

static void t_status_digits(ctest_t *t) {
    int a, b, c;
    char line[32];
    for (a = 2; a <= 5; a++) {
        for (b = 0; b <= 9; b += 3) {
            for (c = 0; c <= 9; c += 4) {
                snprintf(line, sizeof line, "HTTP/1.1 %d%d%d OK", a, b, c);
                line_ok(t, line, a * 100 + b * 10 + c, 1, "OK");
            }
        }
    }
    line_ok(t, "HTTP/1.1 210 X", 210, 1, "X");
    line_ok(t, "HTTP/1.1 201 X", 201, 1, "X");
    line_ok(t, "HTTP/1.1 220 X", 220, 1, "X");
    line_ok(t, "HTTP/1.1 502 X", 502, 1, "X");
    line_ok(t, "HTTP/1.1 599 X", 599, 1, "X");
    line_ok(t, "HTTP/1.0 599 X", 599, 0, "X");
    line_ok(t, "HTTP/1.1 200 X", 200, 1, "X");
}

static void t_status_digit_positions(ctest_t *t) {
    line_bad(t, "HTTP/1.1 x00 OK");
    line_bad(t, "HTTP/1.1 2x0 OK");
    line_bad(t, "HTTP/1.1 20x OK");
    line_bad(t, "HTTP/1.1 /00 OK");
    line_bad(t, "HTTP/1.1 2/0 OK");
    line_bad(t, "HTTP/1.1 20/ OK");
    line_bad(t, "HTTP/1.1 :00 OK");
    line_bad(t, "HTTP/1.1 2:0 OK");
    line_bad(t, "HTTP/1.1 20: OK");
    line_bad(t, "HTTP/1.1 x00");
    line_bad(t, "HTTP/1.1 2x0");
    line_bad(t, "HTTP/1.1 20x");
    line_bad(t, "HTTP/1.1 199 OK");
    line_bad(t, "HTTP/1.1 100 OK");
    line_bad(t, "HTTP/1.1 600 OK");
    line_bad(t, "HTTP/1.1 999 OK");
    line_bad(t, "HTTP/1.1 000 OK");
    line_bad(t, "HTTP/1.1 2000 OK");
    line_bad(t, "HTTP/1.1 20");
    line_bad(t, "HTTP/1.1 2");
    line_bad(t, "HTTP/1.1 ");
    line_bad(t, "HTTP/1.1");
}

static void t_status_prefix(ctest_t *t) {
    line_bad(t, "HTTP/1.2 200 OK");
    line_bad(t, "HTTP/1.9 200 OK");
    line_bad(t, "HTTP/1./ 200 OK");
    line_bad(t, "HTTP/2.0 200 OK");
    line_bad(t, "HTTP/0.1 200 OK");
    line_bad(t, "HTTP/1,1 200 OK");
    line_bad(t, "hTTP/1.1 200 OK");
    line_bad(t, "HTTP/1.1  200 OK");
    line_bad(t, "HTTP/1.1\t200 OK");
    line_bad(t, "HTTP/1.1200 OK");
    line_bad(t, "XTTP/1.1 200 OK");
    line_bad(t, "HTTPX1.1 200 OK");
    line_bad(t, "HTTP/1.1 200OK");
    line_bad(t, "HTTP/1.1 200\tOK");
    line_ok(t, "HTTP/1.0 200 OK", 200, 0, "OK");
    line_ok(t, "HTTP/1.1 200 OK", 200, 1, "OK");
}

static void t_status_reason(ctest_t *t) {
    line_ok(t, "HTTP/1.1 200", 200, 1, "");
    line_ok(t, "HTTP/1.1 200 ", 200, 1, "");
    line_ok(t, "HTTP/1.1 200  ", 200, 1, " ");
    line_ok(t, "HTTP/1.1 200 A", 200, 1, "A");
    line_ok(t, "HTTP/1.1 200 A B", 200, 1, "A B");
    line_ok(t, "HTTP/1.1 200 \tA", 200, 1, "\tA");
    line_ok(t, "HTTP/1.1 200 ~", 200, 1, "~");
    line_ok(t, "HTTP/1.1 200 !", 200, 1, "!");
    line_ok(t, "HTTP/1.1 200 \x80\xff", 200, 1, "\x80\xff");
    line_bad(t, "HTTP/1.1 200 \x01");
    line_bad(t, "HTTP/1.1 200 \x1f");
    line_bad(t, "HTTP/1.1 200 \x7f");
    line_bad(t, "HTTP/1.1 200 A\x01");
    line_bad(t, "HTTP/1.1 200 \x01" "A");
    line_bad(t, "HTTP/1.1 200 AB\x7f");
    line_bad(t, "HTTP/1.1 200 A\x1fZ");
}

/* ---- Transfer-Encoding spelling ---- */
static http_status_t te(const char *v, http_framing_t *fr) {
    char buf[128];
    http_event_t ev;
    core_str_t in;
    http_status_t rc;
    int guard;
    snprintf(buf, sizeof buf, "HTTP/1.1 200 OK\r\nTransfer-Encoding: %s\r\n\r\n", v);
    in = core_str_from_cstr(buf);
    (void)http_parser_init(&g_p);
    *fr = HTTP_FRAMING_NONE;
    for (guard = 0; guard < 8; guard++) {
        rc = http_parser_next(&g_p, &in, &ev);
        if (rc != HTTP_OK) {
            return rc;
        }
        if (ev.kind == HTTP_EV_HEADERS_END) {
            *fr = ev.framing;
            return HTTP_OK;
        }
    }
    return HTTP_E_ARG;
}

static void te_good(ctest_t *t, const char *v) {
    http_framing_t fr;
    CHECK_EQ_I64(t, te(v, &fr), HTTP_OK);
    CHECK_EQ_I64(t, fr, HTTP_FRAMING_CHUNKED);
}

static void te_bad(ctest_t *t, const char *v) {
    http_framing_t fr;
    CHECK_EQ_I64(t, te(v, &fr), HTTP_E_FRAMING);
}

static void t_te_spelling(ctest_t *t) {
    static const char k[] = "chunked";
    char v[8];
    size_t i;
    te_good(t, "chunked");
    te_good(t, "CHUNKED");
    te_good(t, "Chunked");
    te_good(t, "cHuNkEd");
    for (i = 0; i < 7; i++) {
        memcpy(v, k, 8);
        v[i] = (char)(v[i] - 'a' + 'A');
        te_good(t, v);
        memcpy(v, k, 8);
        v[i] = (char)(v[i] + 1);
        te_bad(t, v);
        memcpy(v, k, 8);
        v[i] = (char)(v[i] - 1);
        te_bad(t, v);
        memcpy(v, k, 8);
        v[i] = (char)(v[i] - 'a' + 'A' + 1);
        te_bad(t, v);
        memcpy(v, k, 8);
        v[i] = (char)(v[i] - 'a' + 'A' - 1);
        te_bad(t, v);
        memcpy(v, k, 8);
        v[i] = (char)(v[i] - 0x20 + 0x80);
        te_bad(t, v);
        memcpy(v, k, 8);
        v[i] = (char)(v[i] + 0x80);
        te_bad(t, v);
    }
    te_bad(t, "chunke");
    te_bad(t, "hunked");
    te_bad(t, "chunkedd");
    te_bad(t, "chunked,");
    te_bad(t, "xchunked");
    te_bad(t, "gzip");
    te_bad(t, "identity");
    te_bad(t, "gzip, chunked");
    te_bad(t, "@@@@@@@");
    te_bad(t, "[[[[[[[");
    te_bad(t, "{{{{{{{");
}

/* ---- chunk-size lines ---- */
static http_status_t chunked(const char *size_line, const char *data, uint64_t *got) {
    static char buf[512];
    http_event_t ev;
    core_str_t in;
    http_status_t rc = HTTP_OK;
    int guard;
    snprintf(buf, sizeof buf, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n%s\r\n%s", size_line, data);
    in = core_str_from_cstr(buf);
    (void)http_parser_init(&g_p);
    *got = 0;
    for (guard = 0; guard < 32 && rc == HTTP_OK; guard++) {
        rc = http_parser_next(&g_p, &in, &ev);
        if (rc == HTTP_OK && ev.kind == HTTP_EV_BODY) {
            *got += ev.data.len;
        }
        if (rc == HTTP_OK && (ev.kind == HTTP_EV_NONE || ev.kind == HTTP_EV_END)) {
            break;
        }
    }
    return rc;
}

static void ch_ok(ctest_t *t, const char *size_line, uint64_t bytes) {
    char data[64];
    uint64_t got;
    size_t n = bytes < 40 ? (size_t)bytes : 0;
    memset(data, 'x', n);
    memcpy(data + n, "\r\n0\r\n\r\n", 8);
    if (bytes == 0) {
        memcpy(data, "\r\n", 3);
    }
    CHECK_EQ_I64(t, chunked(size_line, bytes == 0 ? "\r\n" : data, &got), HTTP_OK);
    CHECK_EQ_U64(t, got, bytes);
}

static void ch_bad(ctest_t *t, const char *size_line) {
    uint64_t got;
    CHECK_EQ_I64(t, chunked(size_line, "xxxxxxxxxxxxxxxx\r\n0\r\n\r\n", &got), HTTP_E_CHUNK);
}

static void t_chunk_size_digits(ctest_t *t) {
    ch_ok(t, "1", 1);
    ch_ok(t, "a", 10);
    ch_ok(t, "A", 10);
    ch_ok(t, "f", 15);
    ch_ok(t, "F", 15);
    ch_ok(t, "9", 9);
    ch_ok(t, "0a", 10);
    ch_ok(t, "000000000000000000000a", 10);
    ch_ok(t, "10", 16);
    ch_ok(t, "1F", 31);
    ch_ok(t, "1f", 31);
    ch_ok(t, "0", 0);
    ch_ok(t, "00", 0);
    ch_bad(t, "");
    ch_bad(t, "g");
    ch_bad(t, "G");
    ch_bad(t, "1g");
    ch_bad(t, "/");
    ch_bad(t, ":");
    ch_bad(t, "@");
    ch_bad(t, "`");
    ch_bad(t, "1 ");
    ch_bad(t, " 1");
    ch_bad(t, "+1");
    ch_bad(t, "-1");
    ch_bad(t, "0x1");
    ch_bad(t, ";");
    ch_bad(t, ";x");
    ch_bad(t, "1:");
    ch_bad(t, "1\t");
}

static void t_chunk_size_overflow(ctest_t *t) {
    uint64_t got;
    /* 16 hex digits is the most that fits; the 17th significant digit overflows. */
    CHECK_EQ_I64(t, chunked("fffffffffffffffff", "", &got), HTTP_E_CHUNK);
    CHECK_EQ_I64(t, chunked("10000000000000000", "", &got), HTTP_E_CHUNK);
    CHECK_EQ_I64(t, chunked("1000000000000000", "", &got), HTTP_OK);   /* 2^60: accepted, body pending */
    CHECK_EQ_I64(t, chunked("0ffffffffffffffff", "", &got), HTTP_OK);
    CHECK_EQ_I64(t, chunked("ffffffffffffffff", "", &got), HTTP_OK);
    CHECK_EQ_I64(t, chunked("0000000000000000000000000ff", "", &got), HTTP_OK);
    CHECK_EQ_I64(t, chunked("8000000000000000", "", &got), HTTP_OK);
    CHECK_EQ_I64(t, chunked("7fffffffffffffff", "", &got), HTTP_OK);
    CHECK_EQ_I64(t, chunked("fffffffffffffffg", "", &got), HTTP_E_CHUNK);
}

static void t_chunk_extensions(ctest_t *t) {
    ch_ok(t, "1;", 1);
    ch_ok(t, "1;x", 1);
    ch_ok(t, "1;x=y", 1);
    ch_ok(t, "1; x = y ", 1);
    ch_ok(t, "1;\tx", 1);
    ch_ok(t, "1;a;b;c", 1);
    ch_ok(t, "1;\x80\xff", 1);
    ch_ok(t, "1;~", 1);
    ch_ok(t, "1; ", 1);
    ch_ok(t, "0;x", 0);
    ch_bad(t, "1;\x01");
    ch_bad(t, "1;\x1f");
    ch_bad(t, "1;x\x01");
    ch_bad(t, "1;\x01x");
    ch_bad(t, "1;x\x1fy");
    ch_bad(t, "1;\x7f");
    ch_bad(t, "1;x\x7f");
    ch_bad(t, "1;x\x7fy");
    ch_bad(t, "1 ;x");
    ch_bad(t, "1x;x");
    ch_bad(t, "1,x");
}

static void t_chunk_crlf_after_data(ctest_t *t) {
    uint64_t got;
    CHECK_EQ_I64(t, chunked("2", "ab\r\n0\r\n\r\n", &got), HTTP_OK);
    CHECK_EQ_U64(t, got, 2);
    CHECK_EQ_I64(t, chunked("2", "abX\n0\r\n\r\n", &got), HTTP_E_CHUNK);
    CHECK_EQ_I64(t, chunked("2", "ab\rX0\r\n\r\n", &got), HTTP_E_CHUNK);
    CHECK_EQ_I64(t, chunked("2", "ab\n\r0\r\n\r\n", &got), HTTP_E_CHUNK);
    CHECK_EQ_I64(t, chunked("2", "ab\n0\r\n\r\n", &got), HTTP_E_CHUNK);
    CHECK_EQ_I64(t, chunked("2", "ab\r\r\n0\r\n\r\n", &got), HTTP_E_CHUNK);
    CHECK_EQ_I64(t, chunked("2", "abab\r\n0\r\n\r\n", &got), HTTP_E_CHUNK);
    CHECK_EQ_I64(t, chunked("2", "ab  0\r\n\r\n", &got), HTTP_E_CHUNK);
}

CTEST_SUITE("http_survivor_head",
            { "status_digits", t_status_digits },
            { "status_digit_positions", t_status_digit_positions },
            { "status_prefix", t_status_prefix },
            { "status_reason", t_status_reason },
            { "te_spelling", t_te_spelling },
            { "chunk_size_digits", t_chunk_size_digits },
            { "chunk_size_overflow", t_chunk_size_overflow },
            { "chunk_extensions", t_chunk_extensions },
            { "chunk_crlf_after_data", t_chunk_crlf_after_data })
