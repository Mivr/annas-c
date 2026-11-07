/* A wall clock above 9999-12-31T23:59:59Z (253402300799) is "unknown": every certificate is refused with
 * PORTS_E_TLS, on every fixture, on TLS 1.3 and 1.2, and mbedTLS never sees a negative or aliasing year. No
 * fixture certificate is valid at the bound itself (they end in 2126), so the bound is shown to be refused
 * there too, and the positive control is a time far in the future that is inside the windows. */
#include <string.h>

#include "mbedtls/platform_time.h"

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;

static void run(ctest_t *t, int identity, uint64_t wall, ports_err_t want, bool tls12)
{
    ports_stream_t p;
    unsigned char b[4];
    size_t n = 0;
    kit_reset();
    kit()->wall_s = wall;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, identity, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), want);
    if (want == PORTS_OK) {
        CHECK(t, kit_exchange(p, 16, 16, 16));
        CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    } else {
        CHECK_EQ_I64(t, p.ops->read(p.ctx, b, sizeof b, &n), PORTS_E_CLOSED);
        CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
    }
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

/* What mbedTLS is told. The transport is wrapped so that its first send (the ClientHello, inside connect) asks
 * mbedTLS for the time, through the trampoline the module installed: the seam's value up to the bound, else 0. */
static tls_stream_transport_ops_t g_ops;
static const tls_stream_transport_ops_t *g_inner;
static long g_seen, g_seen_out;
static int g_asked;

static long spy_send(void *ctx, const void *buf, size_t len)
{
    if (g_asked == 0) {
        mbedtls_time_t out = -1;
        g_seen = (long) mbedtls_time(&out);
        g_seen_out = (long) out;
        g_asked = 1;
    }
    return g_inner->send(ctx, buf, len);
}

static void seen_time(ctest_t *t, uint64_t wall, long want)
{
    ports_stream_t p;
    tls_stream_config_t c;
    kit_reset();
    kit()->wall_s = wall;
    g_asked = 0;
    CHECK_EQ_I64(t, mbedtls_time(NULL), 0); /* no connection active: unknown */
    CHECK(t, kit_peer_start(TLS_PEER_EC_VALID, false) == 0);
    c = kit_config(kit_ca_root());
    g_inner = c.transport.ops;
    g_ops = *c.transport.ops;
    g_ops.send = spy_send;
    c.transport.ops = &g_ops;
    CHECK(t, tls_stream_init(&g_s, &c));
    p = tls_stream_port(&g_s);
    (void) p.ops->connect(p.ctx, core_str_from_cstr(TLS_PEER_HOST), (uint16_t) KIT_PORT, KIT_DEADLINE);
    CHECK_EQ_I64(t, g_asked, 1);
    CHECK_EQ_I64(t, g_seen, want);
    CHECK_EQ_I64(t, g_seen_out, want);
    kit_done(&g_s);
    CHECK_EQ_I64(t, mbedtls_time(NULL), 0);
}

static void t_seen_in_range(ctest_t *t)
{
    seen_time(t, UINT64_C(1), 1);
    seen_time(t, KIT_TIME_VALID, (long) KIT_TIME_VALID);
    seen_time(t, UINT64_C(253402300798), 253402300798L);
    seen_time(t, UINT64_C(253402300799), 253402300799L);
}

static void t_seen_above_bound(ctest_t *t)
{
    seen_time(t, UINT64_C(0), 0);
    seen_time(t, UINT64_C(253402300800), 0);
    seen_time(t, UINT64_C(264720682970604), 0);
    seen_time(t, UINT64_C(18446479356592516588), 0);
    seen_time(t, UINT64_C(18446744073709551615), 0);
}

static void all_identities(ctest_t *t, uint64_t wall, bool tls12)
{
    run(t, TLS_PEER_EC_VALID, wall, PORTS_E_TLS, tls12);
    run(t, TLS_PEER_RSA_VALID, wall, PORTS_E_TLS, tls12);
    run(t, TLS_PEER_CHAIN, wall, PORTS_E_TLS, tls12);
    run(t, TLS_PEER_EXPIRED, wall, PORTS_E_TLS, tls12);
    run(t, TLS_PEER_NOT_YET_VALID, wall, PORTS_E_TLS, tls12);
}

#define CASE(name, wall)                                                          \
    static void t_##name##_tls13(ctest_t *t) { all_identities(t, wall, false); } \
    static void t_##name##_tls12(ctest_t *t) { all_identities(t, wall, true); }

CASE(at_the_bound, UINT64_C(253402300799))
CASE(just_after_the_bound, UINT64_C(253402300800))
CASE(year_aliasing_value, UINT64_C(264720682970604))
CASE(int64_max, UINT64_C(9223372036854775807))
CASE(int64_max_plus_one, UINT64_C(9223372036854775808))
CASE(wrapping_negative_release_hole, UINT64_C(18446479356592516588))
CASE(uint64_max, UINT64_C(18446744073709551615))

static void t_control_far_future_ok_tls13(ctest_t *t) { run(t, TLS_PEER_EC_VALID, KIT_TIME_FUTURE_WINDOW, PORTS_OK, false); }
static void t_control_far_future_ok_tls12(ctest_t *t) { run(t, TLS_PEER_EC_VALID, KIT_TIME_FUTURE_WINDOW, PORTS_OK, true); }

#define ENTRY(name) {#name "_tls13", t_##name##_tls13}, {#name "_tls12", t_##name##_tls12}

CTEST_SUITE("tls_stream_wall_bound",
    ENTRY(at_the_bound),
    ENTRY(just_after_the_bound),
    ENTRY(year_aliasing_value),
    ENTRY(int64_max),
    ENTRY(int64_max_plus_one),
    ENTRY(wrapping_negative_release_hole),
    ENTRY(uint64_max),
    ENTRY(control_far_future_ok),
    {"seen_in_range", t_seen_in_range},
    {"seen_above_bound", t_seen_above_bound})
