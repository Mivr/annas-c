/* Tests for ports/fakes.h: the scripted stream's strictness: every deviation from the script is detected,
 * recorded (first one only) and answered with an error. Through ports_stream_t. */
#include <stdint.h>
#include <string.h>

#include "ctest/ctest.h"
#include "ports/fakes.h"
#include "ports/ports.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define SENTINEL_N ((size_t)777)

static ports_fake_step_t st_connect(const char *host, uint16_t port, ports_err_t err) {
    ports_fake_step_t s = {PORTS_FAKE_STEP_CONNECT, core_str_from_cstr(host), port, 0, err};
    return s;
}
static ports_fake_step_t st_expect(const char *data) {
    ports_fake_step_t s = {PORTS_FAKE_STEP_EXPECT, core_str_from_cstr(data), 0, 0, PORTS_OK};
    return s;
}
static ports_fake_step_t st_respond(const char *data, size_t chunk) {
    ports_fake_step_t s = {PORTS_FAKE_STEP_RESPOND, core_str_from_cstr(data), 0, chunk, PORTS_OK};
    return s;
}
static ports_fake_step_t st_eof(void) {
    ports_fake_step_t s = {PORTS_FAKE_STEP_EOF, {NULL, 0}, 0, 0, PORTS_OK};
    return s;
}
static ports_fake_step_t st_fail_write(ports_err_t err) {
    ports_fake_step_t s = {PORTS_FAKE_STEP_FAIL_WRITE, {NULL, 0}, 0, 0, err};
    return s;
}
static ports_fake_step_t st_fail_read(ports_err_t err) {
    ports_fake_step_t s = {PORTS_FAKE_STEP_FAIL_READ, {NULL, 0}, 0, 0, err};
    return s;
}

static ports_err_t do_connect(ports_stream_t p, const char *host, uint16_t port, uint64_t deadline) {
    return p.ops->connect(p.ctx, core_str_from_cstr(host), port, deadline);
}
static ports_err_t do_send(ports_stream_t p, const char *s) { return p.ops->write(p.ctx, s, strlen(s)); }
static ports_err_t do_recv(ports_stream_t p, char *buf, size_t cap, size_t *n) { return p.ops->read(p.ctx, buf, cap, n); }
static ports_err_t do_close(ports_stream_t p) { return p.ops->close(p.ctx); }

/* Reads once with a 64-byte buffer; returns the status, stores the count (SENTINEL_N if untouched). */
static ports_err_t recv_n(ports_stream_t p, char *buf, size_t *n) {
    *n = SENTINEL_N;
    return do_recv(p, buf, 64, n);
}

static bool mm_is(const ports_fake_stream_t *s, ports_fake_mismatch_kind_t kind, size_t step, size_t offset) {
    ports_fake_mismatch_t m = {PORTS_FAKE_MM_NONE, 0, 0};
    return ports_fake_stream_mismatch(s, &m) && m.kind == kind && m.step == step && m.offset == offset;
}

static bool mm_none(const ports_fake_stream_t *s) {
    ports_fake_mismatch_t m = {PORTS_FAKE_MM_UNEXPECTED_CALL, 9, 9};
    return !ports_fake_stream_mismatch(s, &m) && m.kind == PORTS_FAKE_MM_UNEXPECTED_CALL && m.step == 9 && m.offset == 9;
}

static void t_kind_values_are_distinct_and_none_is_zero(ctest_t *t) {
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, NULL, 0, NULL, 0));
    CHECK_EQ_I64(t, PORTS_FAKE_MM_NONE, 0);
    CHECK(t, PORTS_FAKE_MM_CONNECT_TARGET != PORTS_FAKE_MM_REQUEST_BYTES);
    CHECK(t, PORTS_FAKE_MM_CONNECT_TARGET != PORTS_FAKE_MM_UNEXPECTED_CALL);
    CHECK(t, PORTS_FAKE_MM_REQUEST_BYTES != PORTS_FAKE_MM_UNEXPECTED_CALL);
    CHECK(t, PORTS_FAKE_MM_CONNECT_TARGET != PORTS_FAKE_MM_NONE && PORTS_FAKE_MM_REQUEST_BYTES != PORTS_FAKE_MM_NONE);
    CHECK(t, PORTS_FAKE_MM_UNEXPECTED_CALL != PORTS_FAKE_MM_NONE);
}

static void t_connect_host_mismatch_offsets(ctest_t *t) {
    struct { const char *host; size_t off; } cases[] = {
        {"example.org", 8}, {"Example.com", 0}, {"exam", 4}, {"example.com.evil", 11}, {"xexample.com", 0}, {"other", 0}};
    for (size_t i = 0; i < COUNT(cases); i++) {
        ports_fake_step_t sc[] = {st_connect("example.com", 443, PORTS_OK), st_expect("x")};
        ports_fake_stream_t s;
        CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
        ports_stream_t p = ports_fake_stream_port(&s);
        CHECK_EQ_I64(t, do_connect(p, cases[i].host, 443, 0), PORTS_E_NET);
        CHECK(t, mm_is(&s, PORTS_FAKE_MM_CONNECT_TARGET, 0, cases[i].off));
        CHECK(t, !ports_fake_stream_is_open(&s));
        CHECK(t, !ports_fake_stream_done(&s));
        CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 0);
    }
}

static void t_connect_port_mismatch_offset_is_host_length(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("example.com", 443, PORTS_OK)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "example.com", 80, 0), PORTS_E_NET);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_CONNECT_TARGET, 0, 11));
}

static void t_connect_exact_target_is_accepted(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("example.com", 443, PORTS_OK)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    CHECK_EQ_I64(t, do_connect(ports_fake_stream_port(&s), "example.com", 443, 0), PORTS_OK);
    CHECK(t, mm_none(&s));
    CHECK(t, ports_fake_stream_done(&s));
}

static void t_request_byte_mismatch_offsets(ctest_t *t) {
    struct { const char *req; size_t off; } cases[] = {
        {"GET /b HTTP/1.1\r\n", 5}, {"get /a HTTP/1.1\r\n", 0}, {"GET /a HTTP/1.1\r\r", 16}, {"GET /a HTTP/1.1\r\nX", 17},
        {"XGET /a HTTP/1.1\r\n", 0}};
    for (size_t i = 0; i < COUNT(cases); i++) {
        ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("GET /a HTTP/1.1\r\n"), st_respond("ok", 0)};
        ports_fake_stream_t s;
        CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
        ports_stream_t p = ports_fake_stream_port(&s);
        CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
        CHECK_EQ_I64(t, do_send(p, cases[i].req), PORTS_E_NET);
        CHECK(t, mm_is(&s, PORTS_FAKE_MM_REQUEST_BYTES, 1, cases[i].off));
        CHECK(t, !ports_fake_stream_done(&s));
    }
}

static void t_request_mismatch_offset_counts_earlier_writes(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("GET /a HTTP/1.1")};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "GET "), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "/"), PORTS_OK);
    CHECK(t, mm_none(&s));
    CHECK_EQ_I64(t, do_send(p, "bX"), PORTS_E_NET); /* 'b' is the 6th byte, index 5 */
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_REQUEST_BYTES, 1, 5));
}

static void t_request_with_nul_bytes_is_compared_bytewise(ctest_t *t) {
    static const char want[] = {'a', '\0', 'b'};
    static const char bad[] = {'a', '\0', 'c'};
    static const char good[] = {'a', '\0', 'b'};
    ports_fake_step_t ex = {PORTS_FAKE_STEP_EXPECT, {want, 3}, 0, 0, PORTS_OK};
    ports_fake_step_t sc[2];
    ports_fake_stream_t s;
    sc[0] = st_connect("h", 1, PORTS_OK);
    sc[1] = ex;
    CHECK(t, ports_fake_stream_init(&s, sc, 2, NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, bad, 3), PORTS_E_NET);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_REQUEST_BYTES, 1, 2));
    CHECK(t, ports_fake_stream_init(&s, sc, 2, NULL, 0)); /* positive control on a fresh fake */
    p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, good, 3), PORTS_OK);
    CHECK(t, ports_fake_stream_done(&s));
}

static void t_excess_request_bytes_in_one_write_are_request_mismatch(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("abc"), st_respond("r", 0)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "abcd"), PORTS_E_NET);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_REQUEST_BYTES, 1, 3));
}

static void t_write_after_request_complete_is_unexpected_call(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("abc"), st_respond("r", 0)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "abc"), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "d"), PORTS_E_NET);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_UNEXPECTED_CALL, 2, 0));
}

static void t_mismatch_step_index_in_later_exchange(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("one"), st_respond("A", 0), st_expect("two")};
    ports_fake_stream_t s;
    char buf[4];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "one"), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "twO"), PORTS_E_NET);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_REQUEST_BYTES, 3, 2));
}

static void t_after_a_mismatch_everything_but_close_is_net_error(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("abc"), st_respond("resp", 0), st_eof()};
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "abX"), PORTS_E_NET);
    CHECK_EQ_I64(t, do_send(p, "abc"), PORTS_E_NET); /* even the correct request is refused now */
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_E_NET);
    CHECK_EQ_U64(t, n, SENTINEL_N);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_E_NET);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_REQUEST_BYTES, 1, 2)); /* the first mismatch is kept */
    CHECK(t, !ports_fake_stream_done(&s));
    CHECK_EQ_I64(t, do_close(p), PORTS_OK);
    CHECK_EQ_U64(t, ports_fake_stream_closes(&s), 1);
    CHECK(t, !ports_fake_stream_is_open(&s));
    CHECK_EQ_I64(t, do_send(p, "abc"), PORTS_E_NET);
    CHECK_EQ_I64(t, do_close(p), PORTS_E_CLOSED);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_REQUEST_BYTES, 1, 2));
}

static void t_first_mismatch_wins_over_a_different_later_one(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("example.com", 443, PORTS_OK)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "example.org", 443, 0), PORTS_E_NET);
    CHECK_EQ_I64(t, do_send(p, "x"), PORTS_E_NET); /* failed state answers E_NET, not E_CLOSED */
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_CONNECT_TARGET, 0, 8));
}

static void t_read_before_request_complete_is_unexpected(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("abc"), st_respond("r", 0)};
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "ab"), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_E_NET);
    CHECK_EQ_U64(t, n, SENTINEL_N);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_UNEXPECTED_CALL, 1, 0));
}

static void t_read_before_any_write_is_unexpected(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("abc")};
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_E_NET);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_UNEXPECTED_CALL, 1, 0));
}

static void t_write_while_response_pending_is_unexpected(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("resp", 2), st_expect("next")};
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK); /* half of the response only */
    CHECK_EQ_I64(t, do_send(p, "next"), PORTS_E_NET);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_UNEXPECTED_CALL, 1, 0));
}

static void t_wrong_direction_against_injected_failures(ctest_t *t) {
    ports_fake_step_t w[] = {st_connect("h", 1, PORTS_OK), st_fail_write(PORTS_E_NET)};
    ports_fake_step_t r[] = {st_connect("h", 1, PORTS_OK), st_fail_read(PORTS_E_NET)};
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, w, COUNT(w), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_E_NET); /* read where a write failure is scripted */
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_UNEXPECTED_CALL, 1, 0));
    CHECK(t, ports_fake_stream_init(&s, r, COUNT(r), NULL, 0));
    p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "x"), PORTS_E_NET);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_UNEXPECTED_CALL, 1, 0));
}

static void t_calls_after_the_script_ends_are_unexpected(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("a")};
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "a"), PORTS_OK);
    CHECK(t, ports_fake_stream_done(&s));
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_E_NET); /* no EOF scripted, so a read is a deviation */
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_UNEXPECTED_CALL, 2, 0));
    CHECK(t, !ports_fake_stream_done(&s));
}

static void t_empty_script_allows_nothing_but_close(ctest_t *t) {
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, NULL, 0, NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK(t, ports_fake_stream_done(&s));
    CHECK_EQ_I64(t, do_close(p), PORTS_E_CLOSED);
    CHECK(t, mm_none(&s) && ports_fake_stream_done(&s));
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_E_NET);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_UNEXPECTED_CALL, 0, 0));
    CHECK(t, !ports_fake_stream_done(&s));
}

static void t_connect_while_open_is_unexpected_call(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_connect("h", 1, PORTS_OK)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_E_NET);
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_UNEXPECTED_CALL, 1, 0));
    CHECK(t, ports_fake_stream_is_open(&s)); /* the connection is untouched */
}

static void t_connect_when_script_wants_something_else(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("a")};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_close(p), PORTS_OK);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_E_NET); /* reconnect not in the script */
    CHECK(t, mm_is(&s, PORTS_FAKE_MM_UNEXPECTED_CALL, 1, 0));
}

static void t_io_before_connect_is_closed_error_and_flagged(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK)};
    ports_fake_stream_t a;
    ports_fake_stream_t b;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&a, sc, COUNT(sc), NULL, 0));
    CHECK(t, ports_fake_stream_init(&b, sc, COUNT(sc), NULL, 0));
    CHECK_EQ_I64(t, do_send(ports_fake_stream_port(&a), "x"), PORTS_E_CLOSED);
    CHECK(t, mm_is(&a, PORTS_FAKE_MM_UNEXPECTED_CALL, 0, 0));
    CHECK_EQ_I64(t, recv_n(ports_fake_stream_port(&b), buf, &n), PORTS_E_CLOSED);
    CHECK_EQ_U64(t, n, SENTINEL_N);
    CHECK(t, mm_is(&b, PORTS_FAKE_MM_UNEXPECTED_CALL, 0, 0));
    CHECK_EQ_I64(t, do_connect(ports_fake_stream_port(&a), "h", 1, 0), PORTS_E_NET); /* failed state */
}

static void t_done_requires_every_step_and_no_mismatch(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK(t, !ports_fake_stream_done(&s));
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK(t, ports_fake_stream_done(&s));
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_E_NET);
    CHECK(t, !ports_fake_stream_done(&s)); /* all steps consumed, but a deviation happened */
}

static void t_re_init_clears_the_record(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    CHECK_EQ_I64(t, do_connect(ports_fake_stream_port(&s), "x", 1, 0), PORTS_E_NET);
    CHECK(t, !mm_none(&s));
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    CHECK(t, mm_none(&s));
    CHECK(t, !ports_fake_stream_is_open(&s));
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 0);
    CHECK_EQ_U64(t, ports_fake_stream_closes(&s), 0);
    CHECK_EQ_I64(t, do_connect(ports_fake_stream_port(&s), "h", 1, 0), PORTS_OK);
}

CTEST_SUITE("stream_mismatch",
    {"kind_values_are_distinct_and_none_is_zero", t_kind_values_are_distinct_and_none_is_zero},
    {"connect_host_mismatch_offsets", t_connect_host_mismatch_offsets},
    {"connect_port_mismatch_offset_is_host_length", t_connect_port_mismatch_offset_is_host_length},
    {"connect_exact_target_is_accepted", t_connect_exact_target_is_accepted},
    {"request_byte_mismatch_offsets", t_request_byte_mismatch_offsets},
    {"request_mismatch_offset_counts_earlier_writes", t_request_mismatch_offset_counts_earlier_writes},
    {"request_with_nul_bytes_is_compared_bytewise", t_request_with_nul_bytes_is_compared_bytewise},
    {"excess_request_bytes_in_one_write_are_request_mismatch", t_excess_request_bytes_in_one_write_are_request_mismatch},
    {"write_after_request_complete_is_unexpected_call", t_write_after_request_complete_is_unexpected_call},
    {"mismatch_step_index_in_later_exchange", t_mismatch_step_index_in_later_exchange},
    {"after_a_mismatch_everything_but_close_is_net_error", t_after_a_mismatch_everything_but_close_is_net_error},
    {"first_mismatch_wins_over_a_different_later_one", t_first_mismatch_wins_over_a_different_later_one},
    {"read_before_request_complete_is_unexpected", t_read_before_request_complete_is_unexpected},
    {"read_before_any_write_is_unexpected", t_read_before_any_write_is_unexpected},
    {"write_while_response_pending_is_unexpected", t_write_while_response_pending_is_unexpected},
    {"wrong_direction_against_injected_failures", t_wrong_direction_against_injected_failures},
    {"calls_after_the_script_ends_are_unexpected", t_calls_after_the_script_ends_are_unexpected},
    {"empty_script_allows_nothing_but_close", t_empty_script_allows_nothing_but_close},
    {"connect_while_open_is_unexpected_call", t_connect_while_open_is_unexpected_call},
    {"connect_when_script_wants_something_else", t_connect_when_script_wants_something_else},
    {"io_before_connect_is_closed_error_and_flagged", t_io_before_connect_is_closed_error_and_flagged},
    {"done_requires_every_step_and_no_mismatch", t_done_requires_every_step_and_no_mismatch},
    {"re_init_clears_the_record", t_re_init_clears_the_record})
