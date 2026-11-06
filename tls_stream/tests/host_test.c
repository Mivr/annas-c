/* Host-name checking and SNI: the name given to connect() must match the certificate, and it is the name that is
 * sent. Each rejection runs against the same identity that is accepted for the right name (the control). */
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

static void ask(ctest_t *t, int identity, const char *host, ports_err_t want, bool tls12)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, identity, tls12, kit_ca_root(), host, KIT_DEADLINE), want);
    CHECK(t, contains(kit()->sent_first, kit()->sent_first_len, host)); /* SNI carries the asked name */
    if (want == PORTS_OK) {
        CHECK(t, kit_exchange(p, 8, 8, 8));
        CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    } else {
        CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
    }
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

static void t_right_name_is_accepted(ctest_t *t)
{
    ask(t, TLS_PEER_EC_VALID, TLS_PEER_HOST, PORTS_OK, false);
    ask(t, TLS_PEER_EC_VALID, TLS_PEER_HOST, PORTS_OK, true);
}

static void t_other_name_of_another_cert_is_rejected(ctest_t *t)
{
    ask(t, TLS_PEER_EC_VALID, TLS_PEER_HOST_WRONG, PORTS_E_TLS, false);
    ask(t, TLS_PEER_EC_VALID, TLS_PEER_HOST_WRONG, PORTS_E_TLS, true);
}

static void t_parent_domain_is_rejected(ctest_t *t)
{
    ask(t, TLS_PEER_EC_VALID, "test.invalid", PORTS_E_TLS, false);
    ask(t, TLS_PEER_EC_VALID, "invalid", PORTS_E_TLS, true);
}

static void t_prefix_of_the_name_is_rejected(ctest_t *t)
{
    ask(t, TLS_PEER_EC_VALID, "tls.test.invali", PORTS_E_TLS, false);
    ask(t, TLS_PEER_EC_VALID, "tls", PORTS_E_TLS, true);
}

static void t_name_with_extra_prefix_is_rejected(ctest_t *t)
{
    ask(t, TLS_PEER_EC_VALID, "xtls.test.invalid", PORTS_E_TLS, false);
    ask(t, TLS_PEER_EC_VALID, "evil.tls.test.invalid", PORTS_E_TLS, true);
}

static void t_name_with_extra_suffix_is_rejected(ctest_t *t)
{
    ask(t, TLS_PEER_EC_VALID, "tls.test.invalidx", PORTS_E_TLS, false);
    ask(t, TLS_PEER_EC_VALID, "tls.test.invalid.evil.example", PORTS_E_TLS, true);
}

static void t_wildcard_text_is_not_a_name(ctest_t *t)
{
    ask(t, TLS_PEER_EC_VALID, "*.test.invalid", PORTS_E_TLS, false);
    ask(t, TLS_PEER_EC_VALID, "*", PORTS_E_TLS, true);
}

static void t_ip_literal_is_rejected(ctest_t *t)
{
    ask(t, TLS_PEER_EC_VALID, "127.0.0.1", PORTS_E_TLS, false);
    ask(t, TLS_PEER_EC_VALID, "::1", PORTS_E_TLS, true);
}

static void t_name_comparison_ignores_case(ctest_t *t)
{
    ask(t, TLS_PEER_EC_VALID, "TLS.Test.Invalid", PORTS_OK, false);
    ask(t, TLS_PEER_EC_VALID, "TLS.TEST.INVALID", PORTS_OK, true);
}

static void t_wrong_host_cert_only_matches_its_own_name(ctest_t *t)
{
    ask(t, TLS_PEER_WRONG_HOST, TLS_PEER_HOST_WRONG, PORTS_OK, false);
    ask(t, TLS_PEER_WRONG_HOST, TLS_PEER_HOST, PORTS_E_TLS, false);
    ask(t, TLS_PEER_WRONG_HOST, TLS_PEER_HOST_WRONG, PORTS_OK, true);
    ask(t, TLS_PEER_WRONG_HOST, TLS_PEER_HOST, PORTS_E_TLS, true);
}

static void t_sni_is_the_connect_name_not_a_constant(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_WRONG_HOST, false, kit_ca_root(), TLS_PEER_HOST_WRONG, KIT_DEADLINE), PORTS_OK);
    CHECK(t, contains(kit()->sent_first, kit()->sent_first_len, TLS_PEER_HOST_WRONG));
    CHECK(t, !contains(kit()->sent_first, kit()->sent_first_len, TLS_PEER_HOST));
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_host_is_not_cached_between_connects(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK(t, kit_peer_start(TLS_PEER_EC_VALID, false) == 0);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, core_str_from_cstr(TLS_PEER_HOST_WRONG), 443, KIT_DEADLINE), PORTS_E_TLS);
    CHECK_EQ_STR(t, kit()->host, TLS_PEER_HOST_WRONG);
    CHECK(t, kit_peer_start(TLS_PEER_EC_VALID, false) == 0);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, core_str_from_cstr(TLS_PEER_HOST), 443, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

CTEST_SUITE("tls_stream_host",
    { "right_name_is_accepted", t_right_name_is_accepted },
    { "other_name_of_another_cert_is_rejected", t_other_name_of_another_cert_is_rejected },
    { "parent_domain_is_rejected", t_parent_domain_is_rejected },
    { "prefix_of_the_name_is_rejected", t_prefix_of_the_name_is_rejected },
    { "name_with_extra_prefix_is_rejected", t_name_with_extra_prefix_is_rejected },
    { "name_with_extra_suffix_is_rejected", t_name_with_extra_suffix_is_rejected },
    { "wildcard_text_is_not_a_name", t_wildcard_text_is_not_a_name },
    { "ip_literal_is_rejected", t_ip_literal_is_rejected },
    { "name_comparison_ignores_case", t_name_comparison_ignores_case },
    { "wrong_host_cert_only_matches_its_own_name", t_wrong_host_cert_only_matches_its_own_name },
    { "sni_is_the_connect_name_not_a_constant", t_sni_is_the_connect_name_not_a_constant },
    { "host_is_not_cached_between_connects", t_host_is_not_cached_between_connects })
