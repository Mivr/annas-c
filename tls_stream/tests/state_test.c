/* ports.h's stream state rules, argument rules and the one-connection rules, over a live TLS session. */
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;
static tls_stream_t g_s2;

static ports_err_t connect_to(ports_stream_t p, core_str_t host, uint16_t port)
{
    return p.ops->connect(p.ctx, host, port, KIT_DEADLINE);
}

static void expect_all_closed(ctest_t *t, ports_stream_t p)
{
    unsigned char b[8] = {0};
    size_t n = 99;
    CHECK_EQ_I64(t, p.ops->read(p.ctx, b, sizeof b, &n), PORTS_E_CLOSED);
    CHECK_EQ_U64(t, n, 99);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, b, sizeof b), PORTS_E_CLOSED);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 0), PORTS_E_CLOSED);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
    CHECK(t, tls_stream_version(&g_s) == NULL);
}

static void t_never_connected_is_closed(ctest_t *t)
{
    kit_reset();
    CHECK(t, kit_init(&g_s, kit_ca_root()));
    expect_all_closed(t, tls_stream_port(&g_s));
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 0);
    tls_stream_deinit(&g_s);
}

static void t_closed_after_close(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, 0), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    expect_all_closed(t, p);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1); /* the transport was closed exactly once */
    kit_done(&g_s);
}

static void t_closed_after_failed_connect(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_WRONG_HOST, false, kit_ca_root(), TLS_PEER_HOST, 0), PORTS_E_TLS);
    expect_all_closed(t, p);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1); /* it had connected, so it is closed, once */
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

static void t_closed_checks_precede_argument_checks(ctest_t *t)
{
    ports_stream_t p;
    size_t n = 5;
    kit_reset();
    CHECK(t, kit_init(&g_s, kit_ca_root()));
    p = tls_stream_port(&g_s);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, NULL, 0, NULL), PORTS_E_CLOSED);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, NULL, 4, &n), PORTS_E_CLOSED);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 3), PORTS_E_CLOSED);
    tls_stream_deinit(&g_s);
}

static void t_read_and_write_argument_errors(ctest_t *t)
{
    ports_stream_t p;
    unsigned char b[16] = {0};
    size_t n = 77;
    size_t sent;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    sent = kit()->sent_total;
    CHECK_EQ_I64(t, p.ops->read(p.ctx, b, 0, &n), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, NULL, sizeof b, &n), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, b, sizeof b, NULL), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 1), PORTS_E_ARG);
    CHECK_EQ_U64(t, n, 77);
    CHECK_EQ_U64(t, kit()->sent_total, sent);
    /* an argument error does not break the connection */
    CHECK(t, kit_exchange(p, 20, 20, 20));
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_zero_length_write_is_a_no_op(ctest_t *t)
{
    ports_stream_t p;
    unsigned char b[4] = {1, 2, 3, 4};
    size_t sent;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    sent = kit()->sent_total;
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 0), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, b, 0), PORTS_OK);
    CHECK_EQ_U64(t, kit()->sent_total, sent);
    CHECK_EQ_U64(t, kit_peer_data_len(), 0);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, b, 1), PORTS_OK); /* positive control: one byte does send */
    CHECK(t, kit()->sent_total > sent);
    CHECK_EQ_U64(t, kit_peer_data_len(), 1);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void bad_connect_args(ctest_t *t, core_str_t host, uint16_t port)
{
    ports_stream_t p;
    kit_reset();
    CHECK(t, kit_init(&g_s, kit_ca_root()));
    p = tls_stream_port(&g_s);
    CHECK_EQ_I64(t, connect_to(p, host, port), PORTS_E_ARG);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CONNECT], 0);
    expect_all_closed(t, p);
    tls_stream_deinit(&g_s);
}

static void t_connect_rejects_empty_host(ctest_t *t)
{
    core_str_t empty = {"x", 0};
    core_str_t null_ptr = {NULL, 0};
    bad_connect_args(t, empty, 443);
    bad_connect_args(t, null_ptr, 443);
}

static void t_connect_rejects_null_host_with_length(ctest_t *t)
{
    core_str_t bad = {NULL, 5};
    bad_connect_args(t, bad, 443);
}

static void t_connect_rejects_port_zero(ctest_t *t)
{
    bad_connect_args(t, core_str_from_cstr(TLS_PEER_HOST), 0);
}

static void t_connect_rejects_host_with_nul(ctest_t *t)
{
    core_str_t h = {"tls.test.invalid\0x", 18};
    bad_connect_args(t, h, 443);
}

static void t_connect_rejects_overlong_host(ctest_t *t)
{
    static char big[300];
    core_str_t h = {big, 256};
    memset(big, 'a', sizeof big);
    bad_connect_args(t, h, 443);
}

static void t_connect_accepts_max_length_host_then_fails_verification(ctest_t *t)
{
    static char big[300];
    ports_stream_t p;
    core_str_t h = {big, 255};
    memset(big, 'a', sizeof big);
    kit_reset();
    CHECK(t, kit_peer_start(TLS_PEER_EC_VALID, false) == 0);
    CHECK(t, kit_init(&g_s, kit_ca_root()));
    p = tls_stream_port(&g_s);
    CHECK_EQ_I64(t, connect_to(p, h, 443), PORTS_E_TLS); /* 255 bytes is legal: it got as far as the certificate */
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CONNECT], 1);
    CHECK_EQ_U64(t, strlen(kit()->host) >= 100, 1);
    kit_done(&g_s);
}

static void t_connect_accepts_port_65535(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK(t, kit_peer_start(TLS_PEER_EC_VALID, false) == 0);
    CHECK(t, kit_init(&g_s, kit_ca_root()));
    p = tls_stream_port(&g_s);
    CHECK_EQ_I64(t, connect_to(p, core_str_from_cstr(TLS_PEER_HOST), 65535), PORTS_OK);
    CHECK_EQ_U64(t, kit()->port, 65535);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_connect_while_open_is_net_and_harmless(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, connect_to(p, core_str_from_cstr(TLS_PEER_HOST), 443), PORTS_E_NET);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CONNECT], 1);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 0);
    CHECK(t, kit_exchange(p, 30, 30, 30)); /* the first connection is untouched */
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_one_connected_object_per_process(ctest_t *t)
{
    ports_stream_t p1;
    ports_stream_t p2;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p1, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK(t, kit_init(&g_s2, kit_ca_root()));
    p2 = tls_stream_port(&g_s2);
    CHECK_EQ_I64(t, connect_to(p2, core_str_from_cstr(TLS_PEER_HOST), 443), PORTS_E_NET);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CONNECT], 1);
    CHECK_EQ_I64(t, p2.ops->close(p2.ctx), PORTS_E_CLOSED);
    CHECK(t, kit_exchange(p1, 40, 40, 40)); /* the first one is undisturbed */
    CHECK_EQ_I64(t, p1.ops->close(p1.ctx), PORTS_OK);
    /* once the first is closed the second may connect (a fresh peer, as a new server would be) */
    CHECK(t, kit_peer_start(TLS_PEER_EC_VALID, false) == 0);
    CHECK_EQ_I64(t, connect_to(p2, core_str_from_cstr(TLS_PEER_HOST), 443), PORTS_OK);
    CHECK(t, kit_exchange(p2, 40, 40, 40));
    CHECK_EQ_I64(t, p2.ops->close(p2.ctx), PORTS_OK);
    tls_stream_deinit(&g_s2);
    kit_done(&g_s);
}

static void t_failed_connect_frees_the_slot_for_another_object(ctest_t *t)
{
    ports_stream_t p1;
    ports_stream_t p2;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p1, TLS_PEER_EXPIRED, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_E_TLS);
    CHECK_EQ_I64(t, kit_open(&g_s2, &p2, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, p2.ops->close(p2.ctx), PORTS_OK);
    tls_stream_deinit(&g_s2);
    kit_done(&g_s);
}

CTEST_SUITE("tls_stream_state",
    { "never_connected_is_closed", t_never_connected_is_closed },
    { "closed_after_close", t_closed_after_close },
    { "closed_after_failed_connect", t_closed_after_failed_connect },
    { "closed_checks_precede_argument_checks", t_closed_checks_precede_argument_checks },
    { "read_and_write_argument_errors", t_read_and_write_argument_errors },
    { "zero_length_write_is_a_no_op", t_zero_length_write_is_a_no_op },
    { "connect_rejects_empty_host", t_connect_rejects_empty_host },
    { "connect_rejects_null_host_with_length", t_connect_rejects_null_host_with_length },
    { "connect_rejects_port_zero", t_connect_rejects_port_zero },
    { "connect_rejects_host_with_nul", t_connect_rejects_host_with_nul },
    { "connect_rejects_overlong_host", t_connect_rejects_overlong_host },
    { "connect_accepts_max_length_host_then_fails_verification", t_connect_accepts_max_length_host_then_fails_verification },
    { "connect_accepts_port_65535", t_connect_accepts_port_65535 },
    { "connect_while_open_is_net_and_harmless", t_connect_while_open_is_net_and_harmless },
    { "one_connected_object_per_process", t_one_connected_object_per_process },
    { "failed_connect_frees_the_slot_for_another_object", t_failed_connect_frees_the_slot_for_another_object })
