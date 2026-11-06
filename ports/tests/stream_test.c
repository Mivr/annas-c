/* Tests for ports/fakes.h: the scripted stream, happy paths and clock/deadline behaviour, through ports_stream_t. */
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
static uint64_t clk(ports_fake_clock_t *c) {
    ports_clock_t k = ports_fake_clock_port(c);
    return k.ops->now_ms(k.ctx);
}

static bool mm_none(const ports_fake_stream_t *s) {
    ports_fake_mismatch_t m = {PORTS_FAKE_MM_UNEXPECTED_CALL, 9, 9};
    return !ports_fake_stream_mismatch(s, &m) && m.step == 9 && m.offset == 9; /* out untouched when none */
}

static void t_full_exchange(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("annas-archive.gl", 443, PORTS_OK), st_expect("GET /x HTTP/1.1\r\n\r\n"),
                              st_respond("HTTP/1.1 200 OK\r\n\r\nhello", 0), st_eof()};
    ports_fake_stream_t s;
    char buf[100];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK(t, !ports_fake_stream_is_open(&s));
    CHECK_EQ_I64(t, do_connect(p, "annas-archive.gl", 443, 0), PORTS_OK);
    CHECK(t, ports_fake_stream_is_open(&s));
    CHECK_EQ_I64(t, do_send(p, "GET /x HTTP/1.1\r\n\r\n"), PORTS_OK);
    CHECK_EQ_I64(t, do_recv(p, buf, sizeof buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 24);
    CHECK_EQ_MEM(t, buf, n, "HTTP/1.1 200 OK\r\n\r\nhello", 24);
    CHECK(t, !ports_fake_stream_done(&s));
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 0); /* EOF */
    CHECK(t, ports_fake_stream_done(&s));
    CHECK(t, mm_none(&s));
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 4);
    CHECK_EQ_I64(t, do_close(p), PORTS_OK);
    CHECK(t, !ports_fake_stream_is_open(&s));
    CHECK_EQ_U64(t, ports_fake_stream_closes(&s), 1);
    CHECK(t, ports_fake_stream_done(&s)); /* closing does not undo completion */
}

static void t_request_may_arrive_in_pieces(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 80, PORTS_OK), st_expect("abcdef"), st_respond("r", 0)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 80, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "ab"), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "cde"), PORTS_OK);
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 1); /* still inside the EXPECT step */
    CHECK_EQ_I64(t, do_send(p, "f"), PORTS_OK);
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 2); /* all bytes arrived */
    CHECK(t, mm_none(&s));
    char buf[4];
    size_t n;
    CHECK_EQ_I64(t, do_recv(p, buf, sizeof buf, &n), PORTS_OK);
    CHECK(t, ports_fake_stream_done(&s));
}

static void t_chunk_limits_each_read(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("0123456789", 4)};
    ports_fake_stream_t s;
    char buf[64];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 4);
    CHECK_EQ_MEM(t, buf, n, "0123", 4);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_MEM(t, buf, n, "4567", 4);
    CHECK(t, !ports_fake_stream_done(&s));
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 2); /* the remainder */
    CHECK_EQ_MEM(t, buf, n, "89", 2);
    CHECK(t, ports_fake_stream_done(&s));
}

static void t_cap_limits_each_read_when_no_chunk(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("abcdefghij", 0)};
    ports_fake_stream_t s;
    char buf[64];
    size_t n = 0;
    size_t got[4] = {0, 0, 0, 0};
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    for (size_t i = 0; i < 4; i++) {
        CHECK_EQ_I64(t, do_recv(p, buf, 3, &n), PORTS_OK);
        got[i] = n;
    }
    CHECK(t, got[0] == 3 && got[1] == 3 && got[2] == 3 && got[3] == 1);
    CHECK(t, ports_fake_stream_done(&s));
}

static void t_cap_smaller_than_chunk_wins_and_chunk_smaller_than_cap_wins(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("0123456789", 8)};
    ports_fake_stream_t s;
    char buf[64];
    size_t n = 0;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_recv(p, buf, 5, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 5); /* cap 5 < chunk 8 */
    CHECK_EQ_I64(t, do_recv(p, buf, 64, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 5); /* remaining 5 < chunk 8 */
    CHECK_EQ_MEM(t, buf, n, "56789", 5);
}

static void t_consecutive_respond_steps_are_never_merged(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("ab", 0), st_respond("cde", 0), st_eof()};
    ports_fake_stream_t s;
    char buf[64];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 2);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 3);
    CHECK_EQ_MEM(t, buf, n, "cde", 3);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 0);
}

static void t_two_exchanges_on_one_connection(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 443, PORTS_OK), st_expect("one"), st_respond("A", 0),
                              st_expect("two"), st_respond("B", 0), st_eof()};
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 443, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "one"), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_MEM(t, buf, n, "A", 1);
    CHECK_EQ_I64(t, do_send(p, "two"), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_MEM(t, buf, n, "B", 1);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 0);
    CHECK(t, ports_fake_stream_done(&s));
    CHECK(t, mm_none(&s));
}

static void t_eof_is_sticky_until_close_and_reconnect_resets_it(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("a", 0), st_eof(),
                              st_connect("h", 1, PORTS_OK), st_respond("b", 0), st_eof()};
    static ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 1);
    for (int i = 0; i < 3; i++) {
        CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
        CHECK_EQ_U64(t, n, 0);
    }
    CHECK(t, mm_none(&s));
    CHECK_EQ_I64(t, do_close(p), PORTS_OK);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 1); /* data again, not a stale EOF */
    CHECK_EQ_MEM(t, buf, n, "b", 1);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 0);
    CHECK(t, ports_fake_stream_done(&s));
    CHECK_EQ_U64(t, ports_fake_stream_closes(&s), 1);
}

static void t_eof_without_data(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_eof()};
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK(t, !ports_fake_stream_done(&s));
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 0);
    CHECK(t, ports_fake_stream_done(&s));
}

static void t_clock_advances_after_each_data_read_only(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("q"), st_respond("abcd", 2), st_eof()};
    ports_fake_clock_t c;
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_clock_init(&c, 1000));
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), &c, 25));
    ports_stream_t p = ports_fake_stream_port(&s);
    ports_clock_t ck = ports_fake_clock_port(&c);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_U64(t, ck.ops->now_ms(ck.ctx), 1000);
    CHECK_EQ_I64(t, do_send(p, "q"), PORTS_OK);
    CHECK_EQ_U64(t, ck.ops->now_ms(ck.ctx), 1000);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, ck.ops->now_ms(ck.ctx), 1025);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, ck.ops->now_ms(ck.ctx), 1050);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK); /* EOF */
    CHECK_EQ_U64(t, n, 0);
    CHECK_EQ_U64(t, ck.ops->now_ms(ck.ctx), 1050);
}

static void t_zero_advance_leaves_the_clock_alone(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("ab", 1)};
    ports_fake_clock_t c;
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_clock_init(&c, 5));
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), &c, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, clk(&c), 5);
}

static void t_deadline_expires_between_reads(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("aaaabbbbcccc", 4)};
    ports_fake_clock_t c;
    ports_fake_stream_t s;
    char buf[16];
    size_t n;
    CHECK(t, ports_fake_clock_init(&c, 0));
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), &c, 60));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 100), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK); /* now 0 -> 60 */
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK); /* now 60 -> 120 */
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_E_TIMEOUT);
    CHECK_EQ_U64(t, n, SENTINEL_N); /* out-parameter untouched */
    CHECK_EQ_U64(t, clk(&c), 120);    /* a timed-out read does not advance */
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_E_TIMEOUT);
    CHECK(t, mm_none(&s)); /* a timeout is not a script deviation */
    CHECK(t, !ports_fake_stream_done(&s));
    CHECK(t, ports_fake_stream_is_open(&s));
    CHECK_EQ_I64(t, do_close(p), PORTS_OK);
}

static void t_deadline_boundary_is_exclusive(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("abc", 1)};
    ports_fake_clock_t c;
    ports_fake_stream_t s;
    char buf[16];
    size_t n;
    CHECK(t, ports_fake_clock_init(&c, 98));
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), &c, 1));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 100), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK); /* now 98 < 100 */
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK); /* now 99 < 100 */
    CHECK_EQ_U64(t, clk(&c), 100);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_E_TIMEOUT); /* now == deadline */
}

static void t_deadline_zero_means_none(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("abc", 1)};
    ports_fake_clock_t c;
    ports_fake_stream_t s;
    char buf[16];
    size_t n;
    CHECK(t, ports_fake_clock_init(&c, 0));
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), &c, 1000000));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    for (int i = 0; i < 3; i++) {
        CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    }
    CHECK(t, ports_fake_stream_done(&s));
}

static void t_connect_with_passed_deadline_times_out_and_can_retry(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK)};
    ports_fake_clock_t c;
    ports_fake_stream_t s;
    CHECK(t, ports_fake_clock_init(&c, 500));
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), &c, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 500), PORTS_E_TIMEOUT); /* now >= deadline */
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 1), PORTS_E_TIMEOUT);
    CHECK(t, !ports_fake_stream_is_open(&s));
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 0); /* the step was not consumed */
    CHECK(t, mm_none(&s));
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 501), PORTS_OK); /* positive control */
    CHECK(t, ports_fake_stream_done(&s));
}

static void t_write_after_deadline_times_out_but_empty_write_is_ok(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("req")};
    ports_fake_clock_t c;
    ports_fake_stream_t s;
    CHECK(t, ports_fake_clock_init(&c, 0));
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), &c, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 50), PORTS_OK);
    ports_fake_clock_advance(&c, 50);
    CHECK_EQ_I64(t, do_send(p, "req"), PORTS_E_TIMEOUT);
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 1); /* nothing consumed */
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 0), PORTS_OK);
    CHECK(t, mm_none(&s));
}

static void t_write_before_deadline_succeeds(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("req")};
    ports_fake_clock_t c;
    ports_fake_stream_t s;
    CHECK(t, ports_fake_clock_init(&c, 0));
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), &c, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 50), PORTS_OK);
    ports_fake_clock_advance(&c, 49);
    CHECK_EQ_I64(t, do_send(p, "req"), PORTS_OK);
    CHECK(t, ports_fake_stream_done(&s));
}

static void t_deadlines_are_ignored_without_a_clock(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("a", 0)};
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 1000));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 1), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK(t, ports_fake_stream_done(&s));
}

static void t_a_new_connect_replaces_the_deadline(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_connect("h", 1, PORTS_OK), st_respond("a", 0)};
    ports_fake_clock_t c;
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_clock_init(&c, 0));
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), &c, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 10), PORTS_OK);
    CHECK_EQ_I64(t, do_close(p), PORTS_OK);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK); /* no deadline now */
    ports_fake_clock_advance(&c, 1000);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
}

CTEST_SUITE("stream",
    {"full_exchange", t_full_exchange},
    {"request_may_arrive_in_pieces", t_request_may_arrive_in_pieces},
    {"chunk_limits_each_read", t_chunk_limits_each_read},
    {"cap_limits_each_read_when_no_chunk", t_cap_limits_each_read_when_no_chunk},
    {"cap_smaller_than_chunk_wins_and_chunk_smaller_than_cap_wins", t_cap_smaller_than_chunk_wins_and_chunk_smaller_than_cap_wins},
    {"consecutive_respond_steps_are_never_merged", t_consecutive_respond_steps_are_never_merged},
    {"two_exchanges_on_one_connection", t_two_exchanges_on_one_connection},
    {"eof_is_sticky_until_close_and_reconnect_resets_it", t_eof_is_sticky_until_close_and_reconnect_resets_it},
    {"eof_without_data", t_eof_without_data},
    {"clock_advances_after_each_data_read_only", t_clock_advances_after_each_data_read_only},
    {"zero_advance_leaves_the_clock_alone", t_zero_advance_leaves_the_clock_alone},
    {"deadline_expires_between_reads", t_deadline_expires_between_reads},
    {"deadline_boundary_is_exclusive", t_deadline_boundary_is_exclusive},
    {"deadline_zero_means_none", t_deadline_zero_means_none},
    {"connect_with_passed_deadline_times_out_and_can_retry", t_connect_with_passed_deadline_times_out_and_can_retry},
    {"write_after_deadline_times_out_but_empty_write_is_ok", t_write_after_deadline_times_out_but_empty_write_is_ok},
    {"write_before_deadline_succeeds", t_write_before_deadline_succeeds},
    {"deadlines_are_ignored_without_a_clock", t_deadlines_are_ignored_without_a_clock},
    {"a_new_connect_replaces_the_deadline", t_a_new_connect_replaces_the_deadline})
