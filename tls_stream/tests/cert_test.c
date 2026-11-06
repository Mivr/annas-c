/* Certificate verification against every identity of the test PKI, on TLS 1.3 and 1.2. Each rejection has its
 * positive control (the same identity accepted under the condition that makes it valid). After every
 * rejection: the stream is not open, the transport was closed once, nothing is left in the arena. */
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;

static void run(ctest_t *t, int identity, const char *host, uint64_t wall, ports_err_t want, bool tls12)
{
    ports_stream_t p;
    ports_err_t got;
    unsigned char b[4];
    size_t n = 0;
    kit_reset();
    kit()->wall_s = wall;
    got = kit_open(&g_s, &p, identity, tls12, kit_ca_root(), host, KIT_DEADLINE);
    CHECK_EQ_I64(t, got, want);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CONNECT], 1);
    if (want == PORTS_OK) {
        CHECK(t, kit_exchange(p, 16, 16, 16));
        CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    } else {
        CHECK_EQ_I64(t, p.ops->read(p.ctx, b, sizeof b, &n), PORTS_E_CLOSED);
        CHECK_EQ_I64(t, p.ops->write(p.ctx, b, sizeof b), PORTS_E_CLOSED);
        CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
        CHECK(t, tls_stream_version(&g_s) == NULL);
    }
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

#define CASE(name, identity, host, wall, want)                                           \
    static void t_##name##_tls13(ctest_t *t) { run(t, identity, host, wall, want, false); } \
    static void t_##name##_tls12(ctest_t *t) { run(t, identity, host, wall, want, true); }

CASE(ec_valid_is_accepted, TLS_PEER_EC_VALID, TLS_PEER_HOST, KIT_TIME_VALID, PORTS_OK)
CASE(rsa_valid_is_accepted, TLS_PEER_RSA_VALID, TLS_PEER_HOST, KIT_TIME_VALID, PORTS_OK)
CASE(chain_with_intermediate_is_accepted, TLS_PEER_CHAIN, TLS_PEER_HOST, KIT_TIME_VALID, PORTS_OK)
CASE(expired_is_rejected, TLS_PEER_EXPIRED, TLS_PEER_HOST, KIT_TIME_VALID, PORTS_E_TLS)
CASE(expired_is_accepted_inside_its_window, TLS_PEER_EXPIRED, TLS_PEER_HOST, KIT_TIME_EXPIRED_WINDOW, PORTS_OK)
CASE(not_yet_valid_is_rejected, TLS_PEER_NOT_YET_VALID, TLS_PEER_HOST, KIT_TIME_VALID, PORTS_E_TLS)
CASE(not_yet_valid_is_accepted_inside_its_window, TLS_PEER_NOT_YET_VALID, TLS_PEER_HOST, KIT_TIME_FUTURE_WINDOW, PORTS_OK)
CASE(wrong_host_cert_is_rejected, TLS_PEER_WRONG_HOST, TLS_PEER_HOST, KIT_TIME_VALID, PORTS_E_TLS)
CASE(wrong_host_cert_is_accepted_for_its_own_name, TLS_PEER_WRONG_HOST, TLS_PEER_HOST_WRONG, KIT_TIME_VALID, PORTS_OK)
CASE(unknown_ca_is_rejected, TLS_PEER_UNKNOWN_CA, TLS_PEER_HOST, KIT_TIME_VALID, PORTS_E_TLS)
CASE(valid_cert_for_another_name_is_rejected, TLS_PEER_EC_VALID, TLS_PEER_HOST_WRONG, KIT_TIME_VALID, PORTS_E_TLS)
CASE(valid_time_window_of_valid_cert_far_future, TLS_PEER_EC_VALID, TLS_PEER_HOST, KIT_TIME_FUTURE_WINDOW, PORTS_OK)
CASE(valid_cert_before_its_start_is_rejected, TLS_PEER_EC_VALID, TLS_PEER_HOST, KIT_TIME_EXPIRED_WINDOW, PORTS_E_TLS)

#define ENTRY(name) {#name "_tls13", t_##name##_tls13}, {#name "_tls12", t_##name##_tls12}

CTEST_SUITE("tls_stream_cert",
    ENTRY(ec_valid_is_accepted),
    ENTRY(rsa_valid_is_accepted),
    ENTRY(chain_with_intermediate_is_accepted),
    ENTRY(expired_is_rejected),
    ENTRY(expired_is_accepted_inside_its_window),
    ENTRY(not_yet_valid_is_rejected),
    ENTRY(not_yet_valid_is_accepted_inside_its_window),
    ENTRY(wrong_host_cert_is_rejected),
    ENTRY(wrong_host_cert_is_accepted_for_its_own_name),
    ENTRY(unknown_ca_is_rejected),
    ENTRY(valid_cert_for_another_name_is_rejected),
    ENTRY(valid_time_window_of_valid_cert_far_future),
    ENTRY(valid_cert_before_its_start_is_rejected))
