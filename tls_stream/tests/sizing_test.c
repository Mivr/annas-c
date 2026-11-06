/* Memory: the object and arena sizes the header promises are enough for every identity and version with bulk
 * data, a too-small arena fails cleanly (and returns what it took), and the sizing macros are consistent. */
#include <stdalign.h>
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;

static void t_object_storage_is_what_the_header_says(ctest_t *t)
{
    CHECK_EQ_U64(t, sizeof(tls_stream_t), TLS_STREAM_OBJECT_BYTES);
    CHECK(t, alignof(tls_stream_t) >= alignof(max_align_t));
    CHECK(t, TLS_STREAM_HEAP_MIN >= 65536U);
    CHECK_EQ_U64(t, TLS_STREAM_HEAP_BYTES(0), TLS_STREAM_HEAP_MIN);
    CHECK(t, TLS_STREAM_HEAP_BYTES(1000) > TLS_STREAM_HEAP_BYTES(999));
    CHECK(t, TLS_STREAM_HEAP_BYTES(619) >= TLS_STREAM_HEAP_MIN + 2U * 619U);
}

static void exact_minimum(ctest_t *t, int identity, bool tls12)
{
    ports_stream_t p;
    size_t peak;
    kit_reset();
    kit()->heap_size = TLS_STREAM_HEAP_MIN;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, identity, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK(t, kit_exchange(p, 50000, 50000, 5000));
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    peak = tls_stream_heap_peak(&g_s);
    CHECK(t, peak > 20000);
    CHECK(t, peak <= TLS_STREAM_HEAP_MIN);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

static void t_minimum_arena_ec_tls13(ctest_t *t) { exact_minimum(t, TLS_PEER_EC_VALID, false); }
static void t_minimum_arena_ec_tls12(ctest_t *t) { exact_minimum(t, TLS_PEER_EC_VALID, true); }
static void t_minimum_arena_rsa_tls13(ctest_t *t) { exact_minimum(t, TLS_PEER_RSA_VALID, false); }
static void t_minimum_arena_rsa_tls12(ctest_t *t) { exact_minimum(t, TLS_PEER_RSA_VALID, true); }
static void t_minimum_arena_chain_tls13(ctest_t *t) { exact_minimum(t, TLS_PEER_CHAIN, false); }
static void t_minimum_arena_chain_tls12(ctest_t *t) { exact_minimum(t, TLS_PEER_CHAIN, true); }

static void bundle_of(ctest_t *t, size_t copies)
{
    ports_stream_t p;
    core_str_t ca;
    kit_reset();
    ca = kit_ca_repeated(copies);
    CHECK(t, ca.len > 0);
    kit()->heap_size = TLS_STREAM_HEAP_BYTES(ca.len);
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_CHAIN, false, ca, TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK(t, kit_exchange(p, 3000, 3000, 3000));
    CHECK(t, tls_stream_heap_peak(&g_s) <= TLS_STREAM_HEAP_BYTES(ca.len));
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

static void t_bundle_of_one_certificate_fits_its_formula(ctest_t *t) { bundle_of(t, 1); }
static void t_bundle_of_ten_certificates_fits_its_formula(ctest_t *t) { bundle_of(t, 10); }
static void t_bundle_of_two_hundred_certificates_fits_its_formula(ctest_t *t) { bundle_of(t, 200); }

static void t_arena_too_small_for_the_bundle_fails_cleanly(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    kit()->heap_size = TLS_STREAM_HEAP_MIN;
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_repeated(300), TLS_PEER_HOST, KIT_DEADLINE), PORTS_E_NET);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], kit()->calls[KIT_OP_CONNECT]); /* whatever connected was closed */
    /* the same object, the same small arena, a sane bundle: works (the arena was fully given back) */
    tls_stream_deinit(&g_s);
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_arena_exhausted_mid_session_object_stays_usable(ctest_t *t)
{
    ports_stream_t p;
    tls_stream_config_t c;
    kit_reset();
    kit()->heap_size = TLS_STREAM_HEAP_MIN;
    c = kit_config(kit_ca_root());
    CHECK(t, kit_peer_start(TLS_PEER_EC_VALID, false) == 0);
    CHECK(t, tls_stream_init(&g_s, &c));
    p = tls_stream_port(&g_s);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, core_str_from_cstr(TLS_PEER_HOST), 443, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

CTEST_SUITE("tls_stream_sizing",
    { "object_storage_is_what_the_header_says", t_object_storage_is_what_the_header_says },
    { "minimum_arena_ec_tls13", t_minimum_arena_ec_tls13 },
    { "minimum_arena_ec_tls12", t_minimum_arena_ec_tls12 },
    { "minimum_arena_rsa_tls13", t_minimum_arena_rsa_tls13 },
    { "minimum_arena_rsa_tls12", t_minimum_arena_rsa_tls12 },
    { "minimum_arena_chain_tls13", t_minimum_arena_chain_tls13 },
    { "minimum_arena_chain_tls12", t_minimum_arena_chain_tls12 },
    { "bundle_of_one_certificate_fits_its_formula", t_bundle_of_one_certificate_fits_its_formula },
    { "bundle_of_ten_certificates_fits_its_formula", t_bundle_of_ten_certificates_fits_its_formula },
    { "bundle_of_two_hundred_certificates_fits_its_formula", t_bundle_of_two_hundred_certificates_fits_its_formula },
    { "arena_too_small_for_the_bundle_fails_cleanly", t_arena_too_small_for_the_bundle_fails_cleanly },
    { "arena_exhausted_mid_session_object_stays_usable", t_arena_exhausted_mid_session_object_stays_usable })
