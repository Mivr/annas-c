/* Reuse and leaks: one object serves many connections, each success and each kind of failure leaves it ready
 * for the next, and the arena is empty after every close (nothing accumulates across cycles). */
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;
static ports_stream_t g_p;

static ports_err_t connect_again(int identity, bool tls12, uint64_t deadline)
{
    if (kit_peer_start(identity, tls12) != 0) {
        return PORTS_E_ARG;
    }
    return g_p.ops->connect(g_p.ctx, core_str_from_cstr(TLS_PEER_HOST), 443, deadline);
}

static void start(ctest_t *t)
{
    kit_reset();
    CHECK(t, kit_init(&g_s, kit_ca_root()));
    g_p = tls_stream_port(&g_s);
}

static void t_reconnect_after_close_on_both_versions(ctest_t *t)
{
    start(t);
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EC_VALID, false, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_STR(t, tls_stream_version(&g_s), "TLSv1.3");
    CHECK(t, kit_exchange(g_p, 50, 50, 50));
    CHECK_EQ_I64(t, g_p.ops->close(g_p.ctx), PORTS_OK);
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EC_VALID, true, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_STR(t, tls_stream_version(&g_s), "TLSv1.2");
    CHECK(t, kit_exchange(g_p, 50, 50, 50));
    CHECK_EQ_I64(t, g_p.ops->close(g_p.ctx), PORTS_OK);
    CHECK_EQ_I64(t, connect_again(TLS_PEER_RSA_VALID, false, KIT_DEADLINE), PORTS_OK);
    CHECK(t, kit_exchange(g_p, 50, 50, 50));
    CHECK_EQ_I64(t, g_p.ops->close(g_p.ctx), PORTS_OK);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CONNECT], 3);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 3);
    tls_stream_deinit(&g_s);
    kit_peer_stop();
}

static void t_no_state_leaks_from_one_connection_into_the_next(ctest_t *t)
{
    unsigned char b[8];
    size_t n = 9;
    start(t);
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EC_VALID, false, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, kit_peer_close_notify(), 0);
    CHECK_EQ_I64(t, g_p.ops->read(g_p.ctx, b, sizeof b, &n), PORTS_OK); /* EOF on connection 1 */
    CHECK_EQ_U64(t, n, 0);
    CHECK_EQ_I64(t, g_p.ops->close(g_p.ctx), PORTS_OK);
    kit()->eof_when_idle = false;
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EC_VALID, false, KIT_DEADLINE), PORTS_OK);
    CHECK(t, kit_exchange(g_p, 30, 30, 30)); /* connection 2 is not at EOF */
    CHECK_EQ_I64(t, g_p.ops->close(g_p.ctx), PORTS_OK);
    tls_stream_deinit(&g_s);
    kit_peer_stop();
}

static void t_sticky_error_does_not_survive_a_reconnect(ctest_t *t)
{
    unsigned char b[8];
    size_t n = 9;
    start(t);
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EC_VALID, false, 0), PORTS_OK);
    kit()->eof_when_idle = true;
    CHECK_EQ_I64(t, g_p.ops->read(g_p.ctx, b, sizeof b, &n), PORTS_E_NET);
    CHECK_EQ_I64(t, g_p.ops->close(g_p.ctx), PORTS_OK);
    kit()->eof_when_idle = false;
    kit()->inbound_limit = SIZE_MAX;
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EC_VALID, false, 0), PORTS_OK);
    CHECK(t, kit_exchange(g_p, 30, 30, 30));
    CHECK_EQ_I64(t, g_p.ops->close(g_p.ctx), PORTS_OK);
    tls_stream_deinit(&g_s);
    kit_peer_stop();
}

static void t_every_failure_kind_leaves_the_object_reusable(ctest_t *t)
{
    start(t);
    kit()->wall_s = KIT_TIME_VALID;
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EXPIRED, false, KIT_DEADLINE), PORTS_E_TLS);
    CHECK_EQ_I64(t, connect_again(TLS_PEER_UNKNOWN_CA, true, KIT_DEADLINE), PORTS_E_TLS);
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EC_VALID, false, KIT_T0), PORTS_E_TIMEOUT);
    kit()->inbound_limit = 100;
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EC_VALID, false, KIT_DEADLINE), PORTS_E_NET);
    kit()->inbound_limit = SIZE_MAX;
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EC_VALID, false, KIT_DEADLINE), PORTS_OK);
    CHECK(t, kit_exchange(g_p, 60, 60, 60));
    CHECK_EQ_I64(t, g_p.ops->close(g_p.ctx), PORTS_OK);
    tls_stream_deinit(&g_s);
    kit_peer_stop();
}

static void t_forty_cycles_leave_the_arena_empty_and_the_peak_flat(ctest_t *t)
{
    size_t peak1 = 0;
    size_t i;
    start(t);
    for (i = 0; i < 40; i++) {
        CHECK_EQ_I64(t, connect_again(i % 3 == 0 ? TLS_PEER_RSA_VALID : TLS_PEER_EC_VALID, (i % 2) == 1, KIT_DEADLINE), PORTS_OK);
        CHECK(t, kit_exchange(g_p, 700, 700, 700));
        CHECK_EQ_I64(t, g_p.ops->close(g_p.ctx), PORTS_OK);
        CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
        if (i == 5) {
            peak1 = tls_stream_heap_peak(&g_s); /* by now every identity/version combination has run */
        }
    }
    CHECK(t, peak1 > 0);
    CHECK_EQ_U64(t, tls_stream_heap_peak(&g_s), peak1);
    tls_stream_deinit(&g_s);
    kit_peer_stop();
}

static void t_deinit_then_init_again(ctest_t *t)
{
    start(t);
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EC_VALID, false, KIT_DEADLINE), PORTS_OK);
    tls_stream_deinit(&g_s);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    start(t);
    CHECK_EQ_I64(t, connect_again(TLS_PEER_EC_VALID, true, KIT_DEADLINE), PORTS_OK);
    CHECK(t, kit_exchange(g_p, 20, 20, 20));
    CHECK_EQ_I64(t, g_p.ops->close(g_p.ctx), PORTS_OK);
    tls_stream_deinit(&g_s);
    kit_peer_stop();
}

CTEST_SUITE("tls_stream_reuse",
    { "reconnect_after_close_on_both_versions", t_reconnect_after_close_on_both_versions },
    { "no_state_leaks_from_one_connection_into_the_next", t_no_state_leaks_from_one_connection_into_the_next },
    { "sticky_error_does_not_survive_a_reconnect", t_sticky_error_does_not_survive_a_reconnect },
    { "every_failure_kind_leaves_the_object_reusable", t_every_failure_kind_leaves_the_object_reusable },
    { "forty_cycles_leave_the_arena_empty_and_the_peak_flat", t_forty_cycles_leave_the_arena_empty_and_the_peak_flat },
    { "deinit_then_init_again", t_deinit_then_init_again })
