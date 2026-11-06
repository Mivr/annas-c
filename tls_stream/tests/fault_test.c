/* Transport faults at EVERY transport call of a whole session (connect, handshake, request/response, close),
 * on TLS 1.3 and 1.2: EINTR is retried invisibly, EAGAIN waits and retries, a connection reset anywhere in the
 * handshake is PORTS_E_NET with everything released, and after the handshake it breaks the stream for good. */
#include <errno.h>
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;
static unsigned char g_buf[64];

typedef struct {
    uint32_t connect_phase[KIT_OP_COUNT];
    uint32_t whole[KIT_OP_COUNT];
} counts_t;

static counts_t g_counts;

static void arm(kit_op_t op, uint32_t nth, int err)
{
    kit()->fail_op = op;
    kit()->fail_nth = nth;
    kit()->fail_err = err;
}

/* One clean session; record how many transport calls each phase makes. */
static void measure(ctest_t *t, bool tls12)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    memcpy(g_counts.connect_phase, kit()->calls, sizeof g_counts.connect_phase);
    CHECK(t, kit_exchange(p, 100, 100, 100));
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    memcpy(g_counts.whole, kit()->calls, sizeof g_counts.whole);
    kit_done(&g_s);
    CHECK(t, g_counts.connect_phase[KIT_OP_SEND] >= 2 && g_counts.connect_phase[KIT_OP_RECV] >= 2);
    CHECK(t, g_counts.whole[KIT_OP_SEND] > g_counts.connect_phase[KIT_OP_SEND]);
    CHECK_EQ_U64(t, g_counts.whole[KIT_OP_WAIT], 0);
}

static void handshake_dies_at(ctest_t *t, bool tls12, kit_op_t op, int err)
{
    uint32_t k;
    measure(t, tls12);
    for (k = 1; k <= g_counts.connect_phase[op]; k++) {
        ports_stream_t p;
        kit_reset();
        arm(op, k, err);
        CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_E_NET);
        CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], op == KIT_OP_CONNECT ? 0U : 1U);
        CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
        CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
        kit_done(&g_s);
    }
}

static void t_reset_on_any_send_during_handshake_tls13(ctest_t *t) { handshake_dies_at(t, false, KIT_OP_SEND, ECONNRESET); }
static void t_reset_on_any_send_during_handshake_tls12(ctest_t *t) { handshake_dies_at(t, true, KIT_OP_SEND, ECONNRESET); }
static void t_reset_on_any_recv_during_handshake_tls13(ctest_t *t) { handshake_dies_at(t, false, KIT_OP_RECV, ECONNRESET); }
static void t_reset_on_any_recv_during_handshake_tls12(ctest_t *t) { handshake_dies_at(t, true, KIT_OP_RECV, ECONNRESET); }

static void session_survives(ctest_t *t, bool tls12, kit_op_t op, int err)
{
    uint32_t k;
    measure(t, tls12);
    for (k = 1; k <= g_counts.whole[op]; k++) {
        ports_stream_t p;
        kit_reset();
        arm(op, k, err);
        CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
        CHECK(t, kit_exchange(p, 100, 100, 100));
        CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
        CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
        if (err == EAGAIN) {
            CHECK(t, kit()->calls[KIT_OP_WAIT] >= 1); /* it waited instead of failing */
            CHECK_EQ_U64(t, kit()->wait_write_calls > 0, op == KIT_OP_SEND ? 1U : 0U);
        }
        kit_done(&g_s);
    }
}

static void t_eintr_on_any_send_is_invisible_tls13(ctest_t *t) { session_survives(t, false, KIT_OP_SEND, EINTR); }
static void t_eintr_on_any_send_is_invisible_tls12(ctest_t *t) { session_survives(t, true, KIT_OP_SEND, EINTR); }
static void t_eintr_on_any_recv_is_invisible_tls13(ctest_t *t) { session_survives(t, false, KIT_OP_RECV, EINTR); }
static void t_eintr_on_any_recv_is_invisible_tls12(ctest_t *t) { session_survives(t, true, KIT_OP_RECV, EINTR); }
static void t_eagain_on_any_send_waits_and_retries_tls13(ctest_t *t) { session_survives(t, false, KIT_OP_SEND, EAGAIN); }
static void t_eagain_on_any_send_waits_and_retries_tls12(ctest_t *t) { session_survives(t, true, KIT_OP_SEND, EAGAIN); }
static void t_eagain_on_any_recv_waits_and_retries_tls13(ctest_t *t) { session_survives(t, false, KIT_OP_RECV, EAGAIN); }
static void t_eagain_on_any_recv_waits_and_retries_tls12(ctest_t *t) { session_survives(t, true, KIT_OP_RECV, EAGAIN); }

static void t_eintr_on_transport_connect_is_retried(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    arm(KIT_OP_CONNECT, 1, EINTR);
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CONNECT], 2);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    kit_done(&g_s);
}

static void t_eintr_on_wait_is_retried(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    arm(KIT_OP_RECV, 1, EAGAIN);
    kit()->wait_eintr_first = true;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_WAIT], 2); /* EINTR, then the retry that succeeded */
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void after_handshake_dies(ctest_t *t, bool tls12, kit_op_t op)
{
    ports_stream_t p;
    size_t n = 9;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK(t, kit_exchange(p, 10, 10, 10)); /* control: healthy until now */
    arm(op, kit()->calls[op] + 1U, ECONNRESET);
    if (op == KIT_OP_SEND) {
        CHECK_EQ_I64(t, p.ops->write(p.ctx, g_buf, 8), PORTS_E_NET);
    } else {
        CHECK_EQ_I64(t, kit_peer_send(g_buf, 8), 0);
        CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_NET);
        CHECK_EQ_U64(t, n, 9);
    }
    /* the fault was one-shot, yet the stream stays broken: the TLS state is unknown */
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_NET);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, g_buf, 8), PORTS_E_NET);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

static void t_reset_during_write_breaks_the_stream_tls13(ctest_t *t) { after_handshake_dies(t, false, KIT_OP_SEND); }
static void t_reset_during_write_breaks_the_stream_tls12(ctest_t *t) { after_handshake_dies(t, true, KIT_OP_SEND); }
static void t_reset_during_read_breaks_the_stream_tls13(ctest_t *t) { after_handshake_dies(t, false, KIT_OP_RECV); }
static void t_reset_during_read_breaks_the_stream_tls12(ctest_t *t) { after_handshake_dies(t, true, KIT_OP_RECV); }

static void t_close_with_a_dead_transport_still_releases_everything(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    arm(KIT_OP_SEND, kit()->calls[KIT_OP_SEND] + 1U, EPIPE); /* the close_notify cannot be sent */
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);          /* best effort: close still succeeds */
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
    kit_done(&g_s);
}

CTEST_SUITE("tls_stream_fault",
    { "reset_on_any_send_during_handshake_tls13", t_reset_on_any_send_during_handshake_tls13 },
    { "reset_on_any_send_during_handshake_tls12", t_reset_on_any_send_during_handshake_tls12 },
    { "reset_on_any_recv_during_handshake_tls13", t_reset_on_any_recv_during_handshake_tls13 },
    { "reset_on_any_recv_during_handshake_tls12", t_reset_on_any_recv_during_handshake_tls12 },
    { "eintr_on_any_send_is_invisible_tls13", t_eintr_on_any_send_is_invisible_tls13 },
    { "eintr_on_any_send_is_invisible_tls12", t_eintr_on_any_send_is_invisible_tls12 },
    { "eintr_on_any_recv_is_invisible_tls13", t_eintr_on_any_recv_is_invisible_tls13 },
    { "eintr_on_any_recv_is_invisible_tls12", t_eintr_on_any_recv_is_invisible_tls12 },
    { "eagain_on_any_send_waits_and_retries_tls13", t_eagain_on_any_send_waits_and_retries_tls13 },
    { "eagain_on_any_send_waits_and_retries_tls12", t_eagain_on_any_send_waits_and_retries_tls12 },
    { "eagain_on_any_recv_waits_and_retries_tls13", t_eagain_on_any_recv_waits_and_retries_tls13 },
    { "eagain_on_any_recv_waits_and_retries_tls12", t_eagain_on_any_recv_waits_and_retries_tls12 },
    { "eintr_on_transport_connect_is_retried", t_eintr_on_transport_connect_is_retried },
    { "eintr_on_wait_is_retried", t_eintr_on_wait_is_retried },
    { "reset_during_write_breaks_the_stream_tls13", t_reset_during_write_breaks_the_stream_tls13 },
    { "reset_during_write_breaks_the_stream_tls12", t_reset_during_write_breaks_the_stream_tls12 },
    { "reset_during_read_breaks_the_stream_tls13", t_reset_during_read_breaks_the_stream_tls13 },
    { "reset_during_read_breaks_the_stream_tls12", t_reset_during_read_breaks_the_stream_tls12 },
    { "close_with_a_dead_transport_still_releases_everything", t_close_with_a_dead_transport_still_releases_everything })
