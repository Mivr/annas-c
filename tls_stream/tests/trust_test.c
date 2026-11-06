/* The trust anchors and the certificate clock: which CA bundles accept or refuse, what an unusable bundle does,
 * and that certificate validity follows the injected wall clock (never the monotonic clock, never ambient
 * time). */
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;

static ports_err_t try_ca(core_str_t ca, int identity, bool tls12)
{
    ports_stream_t p;
    ports_err_t e;
    kit_reset();
    e = kit_open(&g_s, &p, identity, tls12, ca, TLS_PEER_HOST, KIT_DEADLINE);
    if (e == PORTS_OK) {
        (void) p.ops->close(p.ctx);
    }
    kit_done(&g_s);
    return e;
}

static void t_root_ca_trusts_every_valid_identity(ctest_t *t)
{
    CHECK_EQ_I64(t, try_ca(kit_ca_root(), TLS_PEER_EC_VALID, false), PORTS_OK);
    CHECK_EQ_I64(t, try_ca(kit_ca_root(), TLS_PEER_RSA_VALID, true), PORTS_OK);
    CHECK_EQ_I64(t, try_ca(kit_ca_root(), TLS_PEER_CHAIN, false), PORTS_OK);
}

static void other_ca_alone(ctest_t *t, bool tls12)
{
    int id;
    for (id = 0; id < TLS_PEER_IDENTITY_COUNT; id++) {
        CHECK_EQ_I64(t, try_ca(kit_ca_other(), id, tls12), PORTS_E_TLS);
    }
}

static void t_unrelated_ca_alone_rejects_every_identity_tls13(ctest_t *t) { other_ca_alone(t, false); }
static void t_unrelated_ca_alone_rejects_every_identity_tls12(ctest_t *t) { other_ca_alone(t, true); }

static void t_both_cas_accept_in_either_order(ctest_t *t)
{
    CHECK_EQ_I64(t, try_ca(kit_ca_both(true), TLS_PEER_EC_VALID, false), PORTS_OK);
    CHECK_EQ_I64(t, try_ca(kit_ca_both(false), TLS_PEER_EC_VALID, false), PORTS_OK);
    CHECK_EQ_I64(t, try_ca(kit_ca_both(false), TLS_PEER_CHAIN, true), PORTS_OK);
    CHECK_EQ_I64(t, try_ca(kit_ca_both(true), TLS_PEER_UNKNOWN_CA, false), PORTS_E_TLS); /* still not trusted */
}

static core_str_t bad_ca(int kind)
{
    static const char nul[1] = {0};
    core_str_t empty = {nul, 1};
    return kind == 0 ? kit_ca_garbage() : (kind == 1 ? kit_ca_blank() : empty);
}

static void no_usable_ca(ctest_t *t, int kind)
{
    ports_stream_t p;
    CHECK_EQ_I64(t, try_ca(kit_ca_root(), TLS_PEER_EC_VALID, false), PORTS_OK); /* control */
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, bad_ca(kind), TLS_PEER_HOST, KIT_DEADLINE), PORTS_E_TLS);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CONNECT], 0); /* no connection is made when nothing can be verified */
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_SEND], 0);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

static void t_garbage_bundle_trusts_nothing(ctest_t *t) { no_usable_ca(t, 0); }
static void t_blank_bundle_trusts_nothing(ctest_t *t) { no_usable_ca(t, 1); }

static void t_empty_bundle_trusts_nothing(ctest_t *t)
{
    no_usable_ca(t, 2);
}

static void t_unparsable_block_is_skipped_when_a_usable_one_remains(ctest_t *t)
{
    CHECK_EQ_I64(t, try_ca(kit_ca_mixed(), TLS_PEER_EC_VALID, false), PORTS_OK);
    CHECK_EQ_I64(t, try_ca(kit_ca_mixed(), TLS_PEER_UNKNOWN_CA, false), PORTS_E_TLS);
}

static void t_unknown_wall_clock_fails_closed(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    kit()->wall_s = 0;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_E_TLS);
    kit_done(&g_s);
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, true, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_monotonic_clock_is_not_the_certificate_clock(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    kit_clock_advance(4000000000000ULL); /* a monotonic clock far past any certificate: irrelevant to validity */
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, 0), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
    kit_reset();
    kit()->wall_s = KIT_TIME_EXPIRED_WINDOW;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EXPIRED, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static uint64_t g_wall_calls;

static uint64_t counting_wall(void *ctx)
{
    g_wall_calls++;
    return *(const uint64_t *) ctx;
}

static void t_wall_clock_context_is_passed_and_used(ctest_t *t)
{
    ports_stream_t p;
    uint64_t when = KIT_TIME_VALID;
    tls_stream_config_t c;
    kit_reset();
    CHECK(t, kit_peer_start(TLS_PEER_EXPIRED, false) == 0);
    c = kit_config(kit_ca_root());
    c.wall.unix_seconds = counting_wall;
    c.wall.ctx = &when;
    CHECK(t, tls_stream_init(&g_s, &c));
    p = tls_stream_port(&g_s);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, core_str_from_cstr(TLS_PEER_HOST), 443, KIT_DEADLINE), PORTS_E_TLS);
    CHECK(t, g_wall_calls >= 1);
    /* the same object, the same ctx, a different time: now the expired identity is inside its window */
    when = KIT_TIME_EXPIRED_WINDOW;
    CHECK(t, kit_peer_start(TLS_PEER_EXPIRED, false) == 0);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, core_str_from_cstr(TLS_PEER_HOST), 443, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

CTEST_SUITE("tls_stream_trust",
    { "root_ca_trusts_every_valid_identity", t_root_ca_trusts_every_valid_identity },
    { "unrelated_ca_alone_rejects_every_identity_tls13", t_unrelated_ca_alone_rejects_every_identity_tls13 },
    { "unrelated_ca_alone_rejects_every_identity_tls12", t_unrelated_ca_alone_rejects_every_identity_tls12 },
    { "both_cas_accept_in_either_order", t_both_cas_accept_in_either_order },
    { "garbage_bundle_trusts_nothing", t_garbage_bundle_trusts_nothing },
    { "blank_bundle_trusts_nothing", t_blank_bundle_trusts_nothing },
    { "empty_bundle_trusts_nothing", t_empty_bundle_trusts_nothing },
    { "unparsable_block_is_skipped_when_a_usable_one_remains", t_unparsable_block_is_skipped_when_a_usable_one_remains },
    { "unknown_wall_clock_fails_closed", t_unknown_wall_clock_fails_closed },
    { "monotonic_clock_is_not_the_certificate_clock", t_monotonic_clock_is_not_the_certificate_clock },
    { "wall_clock_context_is_passed_and_used", t_wall_clock_context_is_passed_and_used })
