/* Deadlines, driven by the manual clock: during connect (before and after the transport connects), the
 * handshake, read and write; exact boundaries; and sticky timeouts. Controls: the same operation inside the
 * deadline succeeds. */
#include <errno.h>
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

#define D (KIT_T0 + 5000U) /* the deadline of these tests: 5 s after the clock start */

static tls_stream_t g_s;
static unsigned char g_buf[256];

static ports_stream_t up(ctest_t *t, bool tls12)
{
    ports_stream_t p = {NULL, NULL};
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, D), PORTS_OK);
    return p;
}

static void expect_not_open(ctest_t *t, ports_stream_t p)
{
    size_t n = 0;
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_CLOSED);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
}

static void t_deadline_already_passed_is_timeout_without_io(ctest_t *t)
{
    ports_stream_t p;
    uint64_t when[3];
    size_t i;
    when[0] = KIT_T0;      /* now == deadline */
    when[1] = KIT_T0 - 1U; /* in the past */
    when[2] = 1U;
    for (i = 0; i < 3; i++) {
        kit_reset();
        CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, when[i]), PORTS_E_TIMEOUT);
        CHECK_EQ_U64(t, kit()->calls[KIT_OP_CONNECT], 0);
        CHECK_EQ_U64(t, kit()->calls[KIT_OP_SEND], 0);
        CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 0);
        expect_not_open(t, p);
        kit_done(&g_s);
    }
    kit_reset(); /* control: one millisecond later is not yet a timeout */
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_T0 + 1U), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_transport_connect_timeout_is_timeout(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    kit()->fail_op = KIT_OP_CONNECT;
    kit()->fail_nth = 1;
    kit()->fail_err = ETIMEDOUT;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, D), PORTS_E_TIMEOUT);
    CHECK_EQ_U64(t, kit()->connect_deadline, D);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 0); /* it never connected: nothing to close */
    expect_not_open(t, p);
    kit_done(&g_s);
}

static void t_deadline_passes_while_the_transport_connects(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    kit()->connect_advance_ms = 5000; /* connect() succeeds, but the clock is now exactly at the deadline */
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, D), PORTS_E_TIMEOUT);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_SEND], 0); /* not a byte of ClientHello is sent */
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    expect_not_open(t, p);
    kit_done(&g_s);
    kit_reset();
    kit()->connect_advance_ms = 4999; /* control: one millisecond to spare */
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, D), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void silent_server(ctest_t *t, bool tls12)
{
    ports_stream_t p;
    kit_reset();
    kit()->mute = true;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, D), PORTS_E_TIMEOUT);
    CHECK_EQ_U64(t, kit()->wait_deadline, D); /* the wait was given the same absolute deadline */
    CHECK(t, kit_now() >= D);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    expect_not_open(t, p);
    kit_done(&g_s);
}

static void t_silent_server_times_out_in_the_handshake_tls13(ctest_t *t) { silent_server(t, false); }
static void t_silent_server_times_out_in_the_handshake_tls12(ctest_t *t) { silent_server(t, true); }

static void t_wait_ready_but_clock_past_deadline_is_timeout(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    kit()->mute = true;
    kit()->wait_ready = true; /* wait() reports "ready" after moving the clock beyond the deadline */
    kit()->wait_advance_ms = 6000;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, D), PORTS_E_TIMEOUT);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_WAIT], 1);
    expect_not_open(t, p);
    kit_done(&g_s);
}

static void t_clock_jump_between_handshake_flights_is_timeout(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    /* the first recv works; the clock then jumps past the deadline while the handshake is still going */
    kit()->first_recv_max = 60;
    kit()->wait_ready = true;
    kit()->wait_advance_ms = 6000;
    kit()->fail_op = KIT_OP_RECV;
    kit()->fail_nth = 2;
    kit()->fail_err = EAGAIN;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, D), PORTS_E_TIMEOUT);
    expect_not_open(t, p);
    kit_done(&g_s);
}

static void quiet_read(ctest_t *t, bool tls12)
{
    ports_stream_t p = up(t, tls12);
    size_t n = 9;
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_TIMEOUT); /* the peer says nothing */
    CHECK_EQ_U64(t, n, 9);
    CHECK_EQ_U64(t, kit()->wait_deadline, D);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_TIMEOUT); /* sticky */
    CHECK_EQ_I64(t, p.ops->write(p.ctx, g_buf, 4), PORTS_E_TIMEOUT);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

static void t_read_times_out_on_a_quiet_peer_tls13(ctest_t *t) { quiet_read(t, false); }
static void t_read_times_out_on_a_quiet_peer_tls12(ctest_t *t) { quiet_read(t, true); }

static void t_read_after_deadline_times_out_even_with_data_waiting(ctest_t *t)
{
    ports_stream_t p = up(t, false);
    size_t n = 9;
    uint32_t recvs;
    kit_fill(g_buf, 100, 3);
    CHECK_EQ_I64(t, kit_peer_send(g_buf, 100), 0);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, 50, &n), PORTS_OK); /* control: before the deadline it is read */
    CHECK_EQ_U64(t, n, 50);
    kit_clock_advance(5000); /* now == deadline */
    recvs = kit()->calls[KIT_OP_RECV];
    n = 9;
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, 50, &n), PORTS_E_TIMEOUT); /* buffered data does not matter */
    CHECK_EQ_U64(t, n, 9);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_RECV], recvs);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_one_millisecond_before_the_deadline_is_still_fine(ctest_t *t)
{
    ports_stream_t p = up(t, false);
    kit_clock_advance(4999);
    CHECK(t, kit_exchange(p, 20, 20, 20));
    kit_clock_advance(1);
    CHECK_EQ_U64(t, kit_now(), D);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, g_buf, 4), PORTS_E_TIMEOUT);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_write_after_deadline_sends_nothing(ctest_t *t)
{
    ports_stream_t p = up(t, true);
    size_t sent;
    CHECK_EQ_I64(t, p.ops->write(p.ctx, g_buf, 4), PORTS_OK); /* control */
    sent = kit()->sent_total;
    kit_clock_advance(10000);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, g_buf, 4), PORTS_E_TIMEOUT);
    CHECK_EQ_U64(t, kit()->sent_total, sent);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_zero_length_write_after_deadline_is_a_no_op(ctest_t *t)
{
    ports_stream_t p = up(t, false);
    kit_clock_advance(10000);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 0), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_write_blocked_until_the_deadline_is_timeout(ctest_t *t)
{
    ports_stream_t p = up(t, false);
    size_t sent = kit()->sent_total;
    kit()->fail_op = KIT_OP_SEND; /* the transport never becomes writable */
    kit()->fail_nth = kit()->calls[KIT_OP_SEND] + 1U;
    kit()->fail_err = EAGAIN;
    kit()->fail_persist = true;
    kit()->wait_timeout = true;
    CHECK_EQ_I64(t, p.ops->write(p.ctx, g_buf, 100), PORTS_E_TIMEOUT);
    CHECK_EQ_U64(t, kit()->wait_write_calls, 1); /* it waited for writability, once, with the deadline */
    CHECK_EQ_U64(t, kit()->wait_deadline, D);
    CHECK_EQ_U64(t, kit()->sent_total, sent);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_deadline_zero_never_times_out(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, 0), PORTS_OK);
    kit_clock_advance(1000000000000ULL);
    CHECK(t, kit_exchange(p, 20, 20, 20));
    kit()->fail_op = KIT_OP_RECV; /* and waits get the 0 = none deadline */
    kit()->fail_nth = kit()->calls[KIT_OP_RECV] + 1U;
    kit()->fail_err = EAGAIN;
    CHECK(t, kit_exchange(p, 20, 20, 20));
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_WAIT], 1);
    CHECK_EQ_U64(t, kit()->wait_deadline, 0);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

CTEST_SUITE("tls_stream_deadline",
    { "deadline_already_passed_is_timeout_without_io", t_deadline_already_passed_is_timeout_without_io },
    { "transport_connect_timeout_is_timeout", t_transport_connect_timeout_is_timeout },
    { "deadline_passes_while_the_transport_connects", t_deadline_passes_while_the_transport_connects },
    { "silent_server_times_out_in_the_handshake_tls13", t_silent_server_times_out_in_the_handshake_tls13 },
    { "silent_server_times_out_in_the_handshake_tls12", t_silent_server_times_out_in_the_handshake_tls12 },
    { "wait_ready_but_clock_past_deadline_is_timeout", t_wait_ready_but_clock_past_deadline_is_timeout },
    { "clock_jump_between_handshake_flights_is_timeout", t_clock_jump_between_handshake_flights_is_timeout },
    { "read_times_out_on_a_quiet_peer_tls13", t_read_times_out_on_a_quiet_peer_tls13 },
    { "read_times_out_on_a_quiet_peer_tls12", t_read_times_out_on_a_quiet_peer_tls12 },
    { "read_after_deadline_times_out_even_with_data_waiting", t_read_after_deadline_times_out_even_with_data_waiting },
    { "one_millisecond_before_the_deadline_is_still_fine", t_one_millisecond_before_the_deadline_is_still_fine },
    { "write_after_deadline_sends_nothing", t_write_after_deadline_sends_nothing },
    { "zero_length_write_after_deadline_is_a_no_op", t_zero_length_write_after_deadline_is_a_no_op },
    { "write_blocked_until_the_deadline_is_timeout", t_write_blocked_until_the_deadline_is_timeout },
    { "deadline_zero_never_times_out", t_deadline_zero_never_times_out })
