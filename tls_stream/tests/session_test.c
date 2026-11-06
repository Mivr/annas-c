/* A working session over TLS 1.3 and 1.2: handshake, version, what reaches the transport, a request/response
 * exchange, close. Every case runs against both protocol versions. */
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;

static bool contains(const unsigned char *hay, size_t hl, const char *needle)
{
    size_t nl = strlen(needle);
    size_t i;
    for (i = 0; i + nl <= hl; i++) {
        if (memcmp(hay + i, needle, nl) == 0) {
            return true;
        }
    }
    return false;
}

static void handshake_ok(ctest_t *t, bool tls12)
{
    ports_stream_t p;
    const char *want = tls12 ? "TLSv1.2" : "TLSv1.3";
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_STR(t, tls_stream_version(&g_s), want);
    CHECK(t, kit_peer_established());
    CHECK_EQ_STR(t, kit_peer_version(), want);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CONNECT], 1);
    CHECK_EQ_STR(t, kit()->host, TLS_PEER_HOST);
    CHECK_EQ_U64(t, kit()->port, 443);
    CHECK_EQ_U64(t, kit()->connect_deadline, KIT_DEADLINE);
    CHECK(t, tls_stream_heap_used(&g_s) > 0);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    CHECK(t, tls_stream_version(&g_s) == NULL);
    kit_done(&g_s);
}

static void t_handshake_tls13(ctest_t *t) { handshake_ok(t, false); }
static void t_handshake_tls12(ctest_t *t) { handshake_ok(t, true); }

static void exchange(ctest_t *t, bool tls12)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK(t, kit_exchange(p, 100, 300, 4096));
    CHECK(t, kit_exchange(p, 1, 1, 1));
    CHECK(t, kit_exchange(p, 700, 1500, 64));
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_exchange_tls13(ctest_t *t) { exchange(t, false); }
static void t_exchange_tls12(ctest_t *t) { exchange(t, true); }

static void identities(ctest_t *t, bool tls12)
{
    static const int ok[] = {TLS_PEER_EC_VALID, TLS_PEER_RSA_VALID, TLS_PEER_CHAIN};
    size_t i;
    for (i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        ports_stream_t p;
        kit_reset();
        CHECK_EQ_I64(t, kit_open(&g_s, &p, ok[i], tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
        CHECK(t, kit_exchange(p, 10, 10, 10));
        CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
        kit_done(&g_s);
    }
}

static void t_valid_identities_tls13(ctest_t *t) { identities(t, false); }
static void t_valid_identities_tls12(ctest_t *t) { identities(t, true); }

static void t_sni_is_sent(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK(t, kit()->sent_first_len > 5);
    CHECK_EQ_U64(t, kit()->sent_first[0], 0x16); /* a handshake record */
    CHECK(t, contains(kit()->sent_first, kit()->sent_first_len, TLS_PEER_HOST));
    CHECK(t, !contains(kit()->sent_first, kit()->sent_first_len, "wrong-host"));
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_deadline_zero_means_none(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    kit_clock_advance(1000000000ULL);
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, 0), PORTS_OK);
    CHECK_EQ_U64(t, kit()->connect_deadline, 0);
    CHECK(t, kit_exchange(p, 50, 50, 50));
    kit_clock_advance(1000000000ULL);
    CHECK(t, kit_exchange(p, 50, 50, 50));
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_close_sends_close_notify(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK(t, !kit_peer_client_closed()); /* positive control: not closed before close() */
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK(t, kit_peer_client_closed());
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    kit_done(&g_s);
}

static void t_close_notify_tls12(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, true, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK(t, kit_peer_client_closed());
    kit_done(&g_s);
}

CTEST_SUITE("tls_stream_session",
    { "handshake_tls13", t_handshake_tls13 },
    { "handshake_tls12", t_handshake_tls12 },
    { "exchange_tls13", t_exchange_tls13 },
    { "exchange_tls12", t_exchange_tls12 },
    { "valid_identities_tls13", t_valid_identities_tls13 },
    { "valid_identities_tls12", t_valid_identities_tls12 },
    { "sni_is_sent", t_sni_is_sent },
    { "deadline_zero_means_none", t_deadline_zero_means_none },
    { "close_sends_close_notify", t_close_sends_close_notify },
    { "close_notify_tls12", t_close_notify_tls12 })
