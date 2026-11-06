/* The errno mapping of the transport seam, one case per class, in the handshake and in an established session.
 * Controls: the clean session succeeds. */
#include <errno.h>
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;
static unsigned char g_buf[64];

static ports_err_t open_with(kit_op_t op, int err)
{
    ports_stream_t p;
    ports_err_t e;
    kit_reset();
    kit()->fail_op = op;
    kit()->fail_nth = op == KIT_OP_WAIT ? 0U : 1U;
    kit()->fail_err = err;
    e = kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE);
    if (e == PORTS_OK) {
        (void) p.ops->close(p.ctx);
    }
    kit_done(&g_s);
    return e;
}

static void t_control_clean_session(ctest_t *t)
{
    CHECK_EQ_I64(t, open_with(KIT_OP_SEND, 0), PORTS_OK);
}

static void t_connect_errors_are_net(ctest_t *t)
{
    static const int errs[] = {ECONNREFUSED, EHOSTUNREACH, ENETUNREACH, ENOENT, EACCES, EADDRNOTAVAIL, ECONNRESET, EIO, EINVAL};
    size_t i;
    for (i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        CHECK_EQ_I64(t, open_with(KIT_OP_CONNECT, errs[i]), PORTS_E_NET);
        CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 0);
    }
}

static void t_connect_etimedout_is_timeout(ctest_t *t)
{
    CHECK_EQ_I64(t, open_with(KIT_OP_CONNECT, ETIMEDOUT), PORTS_E_TIMEOUT);
}

static void t_send_errors_in_handshake_are_net(ctest_t *t)
{
    static const int errs[] = {ECONNRESET, EPIPE, EIO, ETIMEDOUT, ENOBUFS, ENOTCONN, EBADF, ECONNABORTED, ENETDOWN};
    size_t i;
    for (i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        CHECK_EQ_I64(t, open_with(KIT_OP_SEND, errs[i]), PORTS_E_NET);
        CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    }
}

static void t_recv_errors_in_handshake_are_net(ctest_t *t)
{
    static const int errs[] = {ECONNRESET, ECONNABORTED, EIO, ETIMEDOUT, ENOTCONN, EBADF, ENETDOWN, EHOSTUNREACH};
    size_t i;
    for (i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        CHECK_EQ_I64(t, open_with(KIT_OP_RECV, errs[i]), PORTS_E_NET);
        CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    }
}

static ports_err_t established_then(kit_op_t op, int err, bool do_write)
{
    ports_stream_t p;
    size_t n = 0;
    ports_err_t e;
    kit_reset();
    if (kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE) != PORTS_OK) {
        return PORTS_E_ARG;
    }
    kit()->fail_op = op;
    kit()->fail_nth = kit()->calls[op] + 1U;
    kit()->fail_err = err;
    if (do_write) {
        e = p.ops->write(p.ctx, g_buf, 8);
    } else {
        (void) kit_peer_send(g_buf, 8);
        e = p.ops->read(p.ctx, g_buf, sizeof g_buf, &n);
    }
    (void) p.ops->close(p.ctx);
    kit_done(&g_s);
    return e;
}

static void t_send_errors_after_handshake_are_net(ctest_t *t)
{
    static const int errs[] = {ECONNRESET, EPIPE, EIO, ETIMEDOUT, ENOBUFS};
    size_t i;
    CHECK_EQ_I64(t, established_then(KIT_OP_SEND, 0, true), PORTS_OK); /* control */
    for (i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        CHECK_EQ_I64(t, established_then(KIT_OP_SEND, errs[i], true), PORTS_E_NET);
    }
}

static void t_recv_errors_after_handshake_are_net(ctest_t *t)
{
    static const int errs[] = {ECONNRESET, ECONNABORTED, EIO, ETIMEDOUT, ENOTCONN};
    size_t i;
    CHECK_EQ_I64(t, established_then(KIT_OP_RECV, 0, false), PORTS_OK); /* control */
    for (i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        CHECK_EQ_I64(t, established_then(KIT_OP_RECV, errs[i], false), PORTS_E_NET);
    }
}

static ports_err_t wait_fails(int err)
{
    ports_stream_t p;
    ports_err_t e;
    kit_reset();
    kit()->mute = true;
    kit()->wait_ready = false;
    kit()->fail_op = KIT_OP_WAIT;
    kit()->fail_nth = 1;
    kit()->fail_err = err;
    e = kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE);
    kit_done(&g_s);
    return e;
}

static void t_wait_errors(ctest_t *t)
{
    CHECK_EQ_I64(t, wait_fails(ETIMEDOUT), PORTS_E_TIMEOUT);
    CHECK_EQ_I64(t, wait_fails(EIO), PORTS_E_NET);
    CHECK_EQ_I64(t, wait_fails(EBADF), PORTS_E_NET);
    CHECK_EQ_I64(t, wait_fails(ECONNRESET), PORTS_E_NET);
}

static void t_send_returning_zero_is_net(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    kit()->send_zero = true;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_E_NET);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_SEND], 1); /* it did not spin */
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    kit_done(&g_s);
}

static void t_send_claiming_more_than_given_is_net(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    kit()->send_over = true;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_E_NET);
    kit_done(&g_s);
}

static void t_recv_claiming_more_than_room_is_net(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    kit()->recv_over = true;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_E_NET);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    kit_done(&g_s);
}

CTEST_SUITE("tls_stream_errno",
    { "control_clean_session", t_control_clean_session },
    { "connect_errors_are_net", t_connect_errors_are_net },
    { "connect_etimedout_is_timeout", t_connect_etimedout_is_timeout },
    { "send_errors_in_handshake_are_net", t_send_errors_in_handshake_are_net },
    { "recv_errors_in_handshake_are_net", t_recv_errors_in_handshake_are_net },
    { "send_errors_after_handshake_are_net", t_send_errors_after_handshake_are_net },
    { "recv_errors_after_handshake_are_net", t_recv_errors_after_handshake_are_net },
    { "wait_errors", t_wait_errors },
    { "send_returning_zero_is_net", t_send_returning_zero_is_net },
    { "send_claiming_more_than_given_is_net", t_send_claiming_more_than_given_is_net },
    { "recv_claiming_more_than_room_is_net", t_recv_claiming_more_than_room_is_net })
