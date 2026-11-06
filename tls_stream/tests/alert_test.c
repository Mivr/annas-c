/* Fatal alerts from the peer (PORTS_E_TLS) and bytes that are not TLS at all, during and after the handshake.
 * Controls: the same session without the alert succeeds; a close_notify is EOF, not an error. */
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;
static unsigned char g_buf[1024];

static const int k_alerts[] = {TLS_PEER_ALERT_UNEXPECTED_MESSAGE, TLS_PEER_ALERT_BAD_RECORD_MAC,
                               TLS_PEER_ALERT_HANDSHAKE_FAILURE, TLS_PEER_ALERT_DECODE_ERROR,
                               TLS_PEER_ALERT_PROTOCOL_VERSION, TLS_PEER_ALERT_INTERNAL_ERROR};

#define N_ALERTS (sizeof k_alerts / sizeof k_alerts[0])

static ports_err_t connect_after_alert(int alert, bool tls12, ports_stream_t *p)
{
    kit_reset();
    if (kit_peer_start(TLS_PEER_EC_VALID, tls12) != 0) {
        return PORTS_E_ARG;
    }
    if (alert != 0 && kit_peer_alert(alert) != 0) {
        return PORTS_E_ARG;
    }
    if (!kit_init(&g_s, kit_ca_root())) {
        return PORTS_E_ARG;
    }
    *p = tls_stream_port(&g_s);
    return p->ops->connect(p->ctx, core_str_from_cstr(TLS_PEER_HOST), 443, KIT_DEADLINE);
}

static void alert_during_handshake(ctest_t *t, bool tls12)
{
    ports_stream_t p;
    size_t i;
    size_t n = 9;
    CHECK_EQ_I64(t, connect_after_alert(0, tls12, &p), PORTS_OK); /* control */
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
    for (i = 0; i < N_ALERTS; i++) {
        CHECK_EQ_I64(t, connect_after_alert(k_alerts[i], tls12, &p), PORTS_E_TLS);
        CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
        CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
        CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_CLOSED);
        kit_done(&g_s);
    }
}

static void t_alert_instead_of_server_hello_tls13(ctest_t *t) { alert_during_handshake(t, false); }
static void t_alert_instead_of_server_hello_tls12(ctest_t *t) { alert_during_handshake(t, true); }

static void alert_then_eof(ctest_t *t, bool tls12)
{
    ports_stream_t p;
    kit_reset();
    kit()->eof_when_idle = true; /* the peer alerts, then the connection ends: the alert is the reason */
    CHECK(t, kit_peer_start(TLS_PEER_EC_VALID, tls12) == 0);
    CHECK_EQ_I64(t, kit_peer_alert(TLS_PEER_ALERT_HANDSHAKE_FAILURE), 0);
    CHECK(t, kit_init(&g_s, kit_ca_root()));
    p = tls_stream_port(&g_s);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, core_str_from_cstr(TLS_PEER_HOST), 443, KIT_DEADLINE), PORTS_E_TLS);
    kit_done(&g_s);
}

static void t_alert_then_eof_is_tls_not_net_tls13(ctest_t *t) { alert_then_eof(t, false); }
static void t_alert_then_eof_is_tls_not_net_tls12(ctest_t *t) { alert_then_eof(t, true); }

static ports_stream_t established(ctest_t *t, bool tls12)
{
    ports_stream_t p = {NULL, NULL};
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    return p;
}

static void alert_after_handshake(ctest_t *t, bool tls12)
{
    size_t i;
    for (i = 0; i < N_ALERTS; i++) {
        ports_stream_t p = established(t, tls12);
        size_t n = 0;
        kit_fill(g_buf + 500, 50, 8);
        CHECK_EQ_I64(t, kit_peer_send(g_buf + 500, 50), 0);
        CHECK_EQ_I64(t, kit_peer_alert(k_alerts[i]), 0);
        /* the data that preceded the alert is delivered first */
        CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, 50, &n), PORTS_OK);
        CHECK_EQ_U64(t, n, 50);
        CHECK(t, kit_same(g_buf, 50, 8));
        n = 9;
        CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_TLS);
        CHECK_EQ_U64(t, n, 9);
        CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_TLS); /* sticky */
        CHECK_EQ_I64(t, p.ops->write(p.ctx, g_buf, 4), PORTS_E_TLS);
        CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
        CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
        CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
        kit_done(&g_s);
    }
}

static void t_alert_after_handshake_is_tls_tls13(ctest_t *t) { alert_after_handshake(t, false); }
static void t_alert_after_handshake_is_tls_tls12(ctest_t *t) { alert_after_handshake(t, true); }

static void alert_then_eof_after_handshake(ctest_t *t, bool tls12)
{
    ports_stream_t p = established(t, tls12);
    size_t n = 0;
    CHECK_EQ_I64(t, kit_peer_alert(TLS_PEER_ALERT_INTERNAL_ERROR), 0);
    kit()->eof_when_idle = true;
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_TLS);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_alert_then_eof_after_handshake_is_tls_tls13(ctest_t *t) { alert_then_eof_after_handshake(t, false); }
static void t_alert_then_eof_after_handshake_is_tls_tls12(ctest_t *t) { alert_then_eof_after_handshake(t, true); }

static void t_close_notify_is_not_an_error(ctest_t *t)
{
    ports_stream_t p = established(t, false);
    size_t n = 9;
    CHECK_EQ_I64(t, kit_peer_close_notify(), 0);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 0);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static const char k_http[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";

static void t_plain_http_instead_of_tls_is_tls_error(ctest_t *t)
{
    ports_stream_t p;
    size_t n = 9;
    kit_reset();
    kit()->mute = true; /* nobody answers the hello: a non-TLS server just talks */
    CHECK(t, kit_peer_start(TLS_PEER_EC_VALID, false) == 0);
    kit_inbox_push(k_http, sizeof k_http - 1U);
    CHECK(t, kit_init(&g_s, kit_ca_root()));
    p = tls_stream_port(&g_s);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, core_str_from_cstr(TLS_PEER_HOST), 443, KIT_DEADLINE), PORTS_E_TLS);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_CLOSED);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    kit_done(&g_s);
}

static void garbage_after_handshake(ctest_t *t, bool tls12)
{
    ports_stream_t p = established(t, tls12);
    size_t n = 9;
    kit_inbox_push(k_http, sizeof k_http - 1U);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_TLS);
    CHECK_EQ_U64(t, n, 9);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_TLS);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_garbage_record_after_handshake_is_tls_tls13(ctest_t *t) { garbage_after_handshake(t, false); }
static void t_garbage_record_after_handshake_is_tls_tls12(ctest_t *t) { garbage_after_handshake(t, true); }

static void t_corrupted_record_is_tls(ctest_t *t)
{
    ports_stream_t p = established(t, true);
    size_t n = 9;
    size_t len;
    kit_fill(g_buf + 500, 60, 8);
    CHECK_EQ_I64(t, kit_peer_send(g_buf + 500, 60), 0);
    len = kit_inbox_len();
    CHECK(t, len > 30);
    kit_inbox_corrupt(len - 3U);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_TLS); /* the MAC fails */
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

CTEST_SUITE("tls_stream_alert",
    { "alert_instead_of_server_hello_tls13", t_alert_instead_of_server_hello_tls13 },
    { "alert_instead_of_server_hello_tls12", t_alert_instead_of_server_hello_tls12 },
    { "alert_then_eof_is_tls_not_net_tls13", t_alert_then_eof_is_tls_not_net_tls13 },
    { "alert_then_eof_is_tls_not_net_tls12", t_alert_then_eof_is_tls_not_net_tls12 },
    { "alert_after_handshake_is_tls_tls13", t_alert_after_handshake_is_tls_tls13 },
    { "alert_after_handshake_is_tls_tls12", t_alert_after_handshake_is_tls_tls12 },
    { "alert_then_eof_after_handshake_is_tls_tls13", t_alert_then_eof_after_handshake_is_tls_tls13 },
    { "alert_then_eof_after_handshake_is_tls_tls12", t_alert_then_eof_after_handshake_is_tls_tls12 },
    { "close_notify_is_not_an_error", t_close_notify_is_not_an_error },
    { "plain_http_instead_of_tls_is_tls_error", t_plain_http_instead_of_tls_is_tls_error },
    { "garbage_record_after_handshake_is_tls_tls13", t_garbage_record_after_handshake_is_tls_tls13 },
    { "garbage_record_after_handshake_is_tls_tls12", t_garbage_record_after_handshake_is_tls_tls12 },
    { "corrupted_record_is_tls", t_corrupted_record_is_tls })
