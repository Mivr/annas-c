/* Tests for http/http.h: the byte just past the edge of a character class. A Content-Length digit run must stop at
 * '9' (the byte after it is ':'), a "0x" label is numeric only for hex digits (the byte after 'F' is 'G'), and an
 * upper-case 'A' (the byte after '@') is an ordinary host letter. Public header only. */
#include <stdint.h>
#include <string.h>

#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static http_parser_t g_p;

/* Feeds `msg` whole and returns the first error (HTTP_OK if none); *cl gets HEADERS_END's content_length. */
static http_status_t run(core_str_t msg, uint64_t *cl) {
    http_event_t ev;
    http_status_t rc = HTTP_OK;
    int guard;
    (void)http_parser_init(&g_p);
    *cl = UINT64_MAX;
    for (guard = 0; guard < 16 && rc == HTTP_OK; guard++) {
        rc = http_parser_next(&g_p, &msg, &ev);
        if (rc == HTTP_OK && ev.kind == HTTP_EV_HEADERS_END) {
            *cl = ev.content_length;
            break;
        }
        if (rc == HTTP_OK && ev.kind == HTTP_EV_NONE) {
            break;
        }
    }
    return rc;
}

static void t_length_digit_edge(ctest_t *t) {
    uint64_t cl;
    CHECK_EQ_I64(t, run(LIT("HTTP/1.1 200 OK\r\nContent-Length: 19\r\n\r\n"), &cl), HTTP_OK);
    CHECK_EQ_U64(t, cl, 19);
    CHECK_EQ_I64(t, run(LIT("HTTP/1.1 200 OK\r\nContent-Length: 1:\r\n\r\n"), &cl), HTTP_E_CONTENT_LENGTH);
    CHECK_EQ_I64(t, run(LIT("HTTP/1.1 200 OK\r\nContent-Length: :\r\n\r\n"), &cl), HTTP_E_CONTENT_LENGTH);
    CHECK_EQ_I64(t, run(LIT("HTTP/1.1 200 OK\r\nContent-Length: :1\r\n\r\n"), &cl), HTTP_E_CONTENT_LENGTH);
    CHECK_EQ_I64(t, run(LIT("HTTP/1.1 200 OK\r\nContent-Length: 0:\r\n\r\n"), &cl), HTTP_E_CONTENT_LENGTH);
    CHECK_EQ_I64(t, run(LIT("HTTP/1.1 200 OK\r\nContent-Length: /\r\n\r\n"), &cl), HTTP_E_CONTENT_LENGTH);
}

static void t_hex_label_edge(ctest_t *t) {
    CHECK(t, !http_host_valid(LIT("a.0xF")));
    /* 'G' follows 'F': not a hex digit, so these last labels are ordinary DNS labels */
    CHECK(t, http_host_valid(LIT("a.0xG")));
    CHECK(t, http_host_valid(LIT("a.0x1G")));
    CHECK(t, http_host_valid(LIT("a.0xFG")));
    CHECK(t, http_host_valid(LIT("a.0xg")));
    CHECK(t, !http_host_valid(LIT("a.0x1F")));
    CHECK(t, !http_host_valid(LIT("a.0xFf")));
}

static void t_upper_a_in_authority(ctest_t *t) {
    http_url_t u;
    memset(&u, 0, sizeof u);
    CHECK_EQ_I64(t, http_url_split(LIT("https://A/x"), &u), HTTP_OK);
    CHECK(t, core_str_eq(u.host, LIT("A")));
    CHECK_EQ_I64(t, http_url_split(LIT("https://Ab.EXAMPLE.com:8443/p?q"), &u), HTTP_OK);
    CHECK(t, core_str_eq(u.host, LIT("Ab.EXAMPLE.com")));
    CHECK_EQ_U64(t, u.port, 8443);
    CHECK_EQ_I64(t, http_url_split(LIT("https://a.com/A"), &u), HTTP_OK);
    CHECK_EQ_I64(t, http_url_split(LIT("https://A@a.com/"), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(LIT("https://a.com@A/"), &u), HTTP_E_URL);
}

CTEST_SUITE("http_survivor_chars",
            { "length_digit_edge", t_length_digit_edge },
            { "hex_label_edge", t_hex_label_edge },
            { "upper_a_in_authority", t_upper_a_in_authority })
