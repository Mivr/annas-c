/* Tests for ports/fakes.h: the scripted stream, error injection, close and argument handling, through ports_stream_t. */
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

static bool mm_none(const ports_fake_stream_t *s) {
    ports_fake_mismatch_t m = {PORTS_FAKE_MM_UNEXPECTED_CALL, 9, 9};
    return !ports_fake_stream_mismatch(s, &m) && m.step == 9 && m.offset == 9;
}

static void t_error_codes_distinct(ctest_t *t) {
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, NULL, 0, NULL, 0)); /* the fake must at least initialise */
    CHECK_EQ_I64(t, PORTS_OK, 0);
    const ports_err_t all[] = {PORTS_OK, PORTS_E_NET, PORTS_E_TIMEOUT, PORTS_E_TLS, PORTS_E_CLOSED, PORTS_E_ARG};
    int distinct = 1;
    for (size_t i = 0; i < COUNT(all); i++) {
        for (size_t j = i + 1; j < COUNT(all); j++) {
            distinct = distinct && all[i] != all[j];
        }
    }
    CHECK(t, distinct);
}

static void t_connect_failure_codes_pass_through(ctest_t *t) {
    const ports_err_t errs[] = {PORTS_E_NET, PORTS_E_TIMEOUT, PORTS_E_TLS, PORTS_E_CLOSED, PORTS_E_ARG};
    for (size_t i = 0; i < COUNT(errs); i++) {
        ports_fake_step_t sc[] = {st_connect("h", 443, errs[i])};
        ports_fake_stream_t s;
        CHECK(t, ports_fake_stream_init(&s, sc, 1, NULL, 0));
        ports_stream_t p = ports_fake_stream_port(&s);
        CHECK_EQ_I64(t, do_connect(p, "h", 443, 0), errs[i]);
        CHECK(t, !ports_fake_stream_is_open(&s));
        CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 1);
        CHECK(t, ports_fake_stream_done(&s)); /* a scripted failure is not a deviation */
        CHECK_EQ_I64(t, do_close(p), PORTS_E_CLOSED);
    }
}

static void t_write_after_failed_connect_is_closed_and_flagged(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 443, PORTS_E_NET), st_connect("h", 443, PORTS_OK), st_expect("x")};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 443, 0), PORTS_E_NET);
    CHECK_EQ_I64(t, do_send(p, "x"), PORTS_E_CLOSED); /* not open: a consumer bug, flagged below */
    ports_fake_mismatch_t m;
    CHECK(t, ports_fake_stream_mismatch(&s, &m));
    CHECK_EQ_I64(t, m.kind, PORTS_FAKE_MM_UNEXPECTED_CALL);
    CHECK_EQ_U64(t, m.step, 1);
}

static void t_connect_failure_then_retry_succeeds(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 443, PORTS_E_TLS), st_connect("h", 443, PORTS_OK), st_expect("x")};
    ports_fake_stream_t s;
    ports_fake_mismatch_t m;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 443, 0), PORTS_E_TLS);
    CHECK_EQ_I64(t, do_connect(p, "h", 443, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "x"), PORTS_OK);
    CHECK(t, ports_fake_stream_done(&s));
    CHECK(t, !ports_fake_stream_mismatch(&s, &m));
}

static void t_injected_write_error_keeps_connection_and_discards_bytes(ctest_t *t) {
    const ports_err_t errs[] = {PORTS_E_NET, PORTS_E_TIMEOUT, PORTS_E_TLS, PORTS_E_CLOSED};
    for (size_t i = 0; i < COUNT(errs); i++) {
        ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_fail_write(errs[i]), st_expect("retry")};
        ports_fake_stream_t s;
        CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
        ports_stream_t p = ports_fake_stream_port(&s);
        CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
        CHECK_EQ_I64(t, do_send(p, "first"), errs[i]); /* these bytes are not compared with anything */
        CHECK(t, ports_fake_stream_is_open(&s));
        CHECK_EQ_I64(t, do_send(p, "retry"), PORTS_OK);
        CHECK(t, ports_fake_stream_done(&s));
        CHECK(t, mm_none(&s));
    }
}

static void t_zero_length_write_does_not_consume_a_write_failure(ctest_t *t) {
    /* a zero-length write is a no-op: it must not consume the FAIL_WRITE step */
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_fail_write(PORTS_E_NET)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 0), PORTS_OK);
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 1);
    CHECK_EQ_I64(t, do_send(p, "x"), PORTS_E_NET); /* positive control: a real write fails */
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 2);
}

static void t_injected_read_error_between_chunks(ctest_t *t) {
    const ports_err_t errs[] = {PORTS_E_NET, PORTS_E_TIMEOUT, PORTS_E_TLS, PORTS_E_CLOSED};
    for (size_t i = 0; i < COUNT(errs); i++) {
        ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("ab", 0), st_fail_read(errs[i]),
                                  st_respond("cd", 0), st_eof()};
        ports_fake_stream_t s;
        char buf[8];
        size_t n;
        CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
        ports_stream_t p = ports_fake_stream_port(&s);
        CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
        CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
        CHECK_EQ_MEM(t, buf, n, "ab", 2);
        CHECK_EQ_I64(t, recv_n(p, buf, &n), errs[i]);
        CHECK_EQ_U64(t, n, SENTINEL_N); /* untouched on error */
        CHECK(t, ports_fake_stream_is_open(&s));
        CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
        CHECK_EQ_MEM(t, buf, n, "cd", 2);
        CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
        CHECK_EQ_U64(t, n, 0);
        CHECK(t, ports_fake_stream_done(&s));
    }
}

static void t_injected_read_error_right_after_request(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("GET"), st_fail_read(PORTS_E_TLS)};
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_send(p, "GET"), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_E_TLS);
    CHECK(t, ports_fake_stream_done(&s));
    CHECK_EQ_I64(t, do_close(p), PORTS_OK);
}

static void t_truncated_response_then_close_is_not_done(ctest_t *t) {
    /* the consumer gives up half way: the script is incomplete, and that is observable */
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("abcdef", 3), st_eof()};
    ports_fake_stream_t s;
    char buf[8];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_I64(t, do_close(p), PORTS_OK);
    CHECK(t, !ports_fake_stream_done(&s));
    CHECK(t, mm_none(&s));
}

static void t_close_semantics(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, do_close(p), PORTS_E_CLOSED); /* never opened */
    CHECK_EQ_U64(t, ports_fake_stream_closes(&s), 0);
    CHECK(t, mm_none(&s)); /* a harmless close is not a deviation */
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_close(p), PORTS_OK);
    CHECK_EQ_U64(t, ports_fake_stream_closes(&s), 1);
    CHECK(t, !ports_fake_stream_is_open(&s));
    CHECK_EQ_I64(t, do_close(p), PORTS_E_CLOSED); /* twice */
    CHECK_EQ_U64(t, ports_fake_stream_closes(&s), 1);
    CHECK(t, mm_none(&s));
}

static void t_io_after_close_is_closed_error(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_respond("x", 0)};
    ports_fake_stream_t s;
    ports_fake_stream_t w;
    char buf[4];
    size_t n;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    CHECK(t, ports_fake_stream_init(&w, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    ports_stream_t q = ports_fake_stream_port(&w);
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, do_close(p), PORTS_OK);
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_E_CLOSED);
    CHECK_EQ_U64(t, n, SENTINEL_N);
    CHECK_EQ_I64(t, do_connect(q, "h", 1, 0), PORTS_OK); /* a second fake, to see write on its own */
    CHECK_EQ_I64(t, do_close(q), PORTS_OK);
    CHECK_EQ_I64(t, do_send(q, "x"), PORTS_E_CLOSED);
}

static void t_bad_arguments_are_arg_errors_without_side_effects(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK), st_expect("ab"), st_respond("xy", 0)};
    ports_fake_stream_t s;
    char buf[8];
    size_t n = SENTINEL_N;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, (core_str_t){NULL, 0}, 1, 0), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, LIT(""), 1, 0), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, LIT("h"), 0, 0), PORTS_E_ARG);
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 0);
    CHECK(t, !ports_fake_stream_is_open(&s));
    CHECK_EQ_I64(t, do_connect(p, "h", 1, 0), PORTS_OK); /* the same step still works: nothing consumed */
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 2), PORTS_E_ARG);
    CHECK_EQ_I64(t, do_recv(p, buf, 0, &n), PORTS_E_ARG);
    CHECK_EQ_I64(t, do_recv(p, NULL, 8, &n), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, buf, 8, NULL), PORTS_E_ARG);
    CHECK_EQ_U64(t, n, SENTINEL_N);
    CHECK(t, mm_none(&s));
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 1);
    CHECK_EQ_I64(t, do_send(p, "ab"), PORTS_OK); /* still on script */
    CHECK_EQ_I64(t, recv_n(p, buf, &n), PORTS_OK);
    CHECK_EQ_MEM(t, buf, n, "xy", 2);
    CHECK(t, ports_fake_stream_done(&s));
}

static void t_distinct_streams_do_not_share_state(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, PORTS_OK)};
    ports_fake_stream_t a;
    ports_fake_stream_t b;
    CHECK(t, ports_fake_stream_init(&a, sc, COUNT(sc), NULL, 0));
    CHECK(t, ports_fake_stream_init(&b, sc, COUNT(sc), NULL, 0));
    CHECK_EQ_I64(t, do_connect(ports_fake_stream_port(&a), "h", 1, 0), PORTS_OK);
    CHECK(t, ports_fake_stream_is_open(&a));
    CHECK(t, !ports_fake_stream_is_open(&b));
    CHECK_EQ_U64(t, ports_fake_stream_pos(&b), 0);
}

CTEST_SUITE("stream_error",
    {"error_codes_distinct", t_error_codes_distinct},
    {"connect_failure_codes_pass_through", t_connect_failure_codes_pass_through},
    {"write_after_failed_connect_is_closed_and_flagged", t_write_after_failed_connect_is_closed_and_flagged},
    {"connect_failure_then_retry_succeeds", t_connect_failure_then_retry_succeeds},
    {"injected_write_error_keeps_connection_and_discards_bytes", t_injected_write_error_keeps_connection_and_discards_bytes},
    {"zero_length_write_does_not_consume_a_write_failure", t_zero_length_write_does_not_consume_a_write_failure},
    {"injected_read_error_between_chunks", t_injected_read_error_between_chunks},
    {"injected_read_error_right_after_request", t_injected_read_error_right_after_request},
    {"truncated_response_then_close_is_not_done", t_truncated_response_then_close_is_not_done},
    {"close_semantics", t_close_semantics},
    {"io_after_close_is_closed_error", t_io_after_close_is_closed_error},
    {"bad_arguments_are_arg_errors_without_side_effects", t_bad_arguments_are_arg_errors_without_side_effects},
    {"distinct_streams_do_not_share_state", t_distinct_streams_do_not_share_state})
