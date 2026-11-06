/* tls_stream_init validation, the unusable-object rules, and the NULL-safe accessors. Positive controls:
 * the unmodified config is accepted before each rejection. */
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;

static tls_stream_config_t good(void)
{
    kit_reset();
    return kit_config(kit_ca_root());
}

static void expect_rejected(ctest_t *t, tls_stream_config_t bad)
{
    CHECK(t, !tls_stream_init(&g_s, &bad));
}

static void control(ctest_t *t, tls_stream_config_t c)
{
    CHECK(t, tls_stream_init(&g_s, &c)); /* positive control: the unmodified config is accepted */
    tls_stream_deinit(&g_s);
}

static void t_valid_config_is_accepted(ctest_t *t)
{
    tls_stream_config_t c = good();
    CHECK(t, tls_stream_init(&g_s, &c));
    tls_stream_deinit(&g_s);
}

static void t_null_arguments_are_rejected(ctest_t *t)
{
    tls_stream_config_t c = good();
    CHECK(t, tls_stream_init(&g_s, &c));
    tls_stream_deinit(&g_s);
    CHECK(t, !tls_stream_init(NULL, &c));
    CHECK(t, !tls_stream_init(&g_s, NULL));
}

static void with_op_cleared(ctest_t *t, int which)
{
    static tls_stream_transport_ops_t ops;
    tls_stream_config_t c = good();
    ops = *c.transport.ops;
    c.transport.ops = &ops;
    CHECK(t, tls_stream_init(&g_s, &c)); /* control: the copy is valid */
    tls_stream_deinit(&g_s);
    switch (which) {
    case 0: ops.connect = NULL; break;
    case 1: ops.send = NULL; break;
    case 2: ops.recv = NULL; break;
    case 3: ops.wait = NULL; break;
    default: ops.close = NULL; break;
    }
    expect_rejected(t, c);
}

static void t_missing_connect_op(ctest_t *t) { with_op_cleared(t, 0); }
static void t_missing_send_op(ctest_t *t) { with_op_cleared(t, 1); }
static void t_missing_recv_op(ctest_t *t) { with_op_cleared(t, 2); }
static void t_missing_wait_op(ctest_t *t) { with_op_cleared(t, 3); }
static void t_missing_close_op(ctest_t *t) { with_op_cleared(t, 4); }

static void t_missing_transport_table(ctest_t *t)
{
    tls_stream_config_t c = good();
    control(t, c);
    c.transport.ops = NULL;
    expect_rejected(t, c);
}

static void t_missing_clock(ctest_t *t)
{
    static const ports_clock_ops_t no_now = {NULL};
    tls_stream_config_t c = good();
    control(t, c);
    c.clock.ops = &no_now;
    expect_rejected(t, c);
    c.clock.ops = NULL;
    expect_rejected(t, c);
}

static void t_missing_wall_clock(ctest_t *t)
{
    tls_stream_config_t c = good();
    control(t, c);
    c.wall.unix_seconds = NULL;
    expect_rejected(t, c);
}

static void t_ca_bundle_rules(ctest_t *t)
{
    tls_stream_config_t c = good();
    core_str_t root = kit_ca_root();
    CHECK(t, root.len > 100 && root.ptr[root.len - 1] == '\0'); /* the control really ends in NUL */
    CHECK(t, tls_stream_init(&g_s, &c));
    tls_stream_deinit(&g_s);
    c.ca_pem.len = root.len - 1; /* last byte is now the PEM's final newline, not NUL */
    expect_rejected(t, c);
    c.ca_pem.len = 0;
    expect_rejected(t, c);
    c.ca_pem = root;
    c.ca_pem.ptr = NULL;
    expect_rejected(t, c);
}

static void t_one_byte_nul_bundle_is_a_valid_config(ctest_t *t)
{
    static const char nul[1] = {0};
    tls_stream_config_t c = good();
    c.ca_pem.ptr = nul;
    c.ca_pem.len = 1; /* an empty bundle is a legal config; connect() will refuse to trust anything */
    CHECK(t, tls_stream_init(&g_s, &c));
    tls_stream_deinit(&g_s);
}

static void t_heap_rules(ctest_t *t)
{
    tls_stream_config_t c = good();
    c.heap_size = TLS_STREAM_HEAP_MIN;
    CHECK(t, tls_stream_init(&g_s, &c)); /* exactly the minimum is accepted */
    tls_stream_deinit(&g_s);
    c.heap_size = TLS_STREAM_HEAP_MIN - 1U;
    expect_rejected(t, c);
    c.heap_size = 0;
    expect_rejected(t, c);
    c = good();
    c.heap = NULL;
    expect_rejected(t, c);
}

static void all_port_calls_are_arg(ctest_t *t, ports_stream_t p)
{
    unsigned char b[4] = {1, 2, 3, 4};
    size_t n = 7;
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, core_str_from_cstr(TLS_PEER_HOST), 443, 0), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, b, sizeof b), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, b, sizeof b, &n), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_ARG);
    CHECK_EQ_U64(t, n, 7);
}

static void t_object_that_failed_init_is_unusable(ctest_t *t)
{
    tls_stream_config_t c = good();
    c.heap = NULL;
    CHECK(t, !tls_stream_init(&g_s, &c));
    all_port_calls_are_arg(t, tls_stream_port(&g_s));
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CONNECT], 0);
}

static void t_zeroed_object_is_unusable(ctest_t *t)
{
    kit_reset();
    memset(&g_s, 0, sizeof g_s);
    all_port_calls_are_arg(t, tls_stream_port(&g_s));
    CHECK(t, tls_stream_version(&g_s) == NULL);
}

static void t_null_object_port_is_arg(ctest_t *t)
{
    kit_reset();
    all_port_calls_are_arg(t, tls_stream_port(NULL));
}

static void t_deinit_makes_the_object_unusable(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK(t, kit_init(&g_s, kit_ca_root())); /* control: usable before */
    p = tls_stream_port(&g_s);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
    tls_stream_deinit(&g_s);
    all_port_calls_are_arg(t, p);
    tls_stream_deinit(&g_s); /* twice is harmless */
    tls_stream_deinit(NULL);
}

static void t_accessors_are_null_safe(ctest_t *t)
{
    kit_reset();
    CHECK(t, tls_stream_version(NULL) == NULL);
    CHECK_EQ_U64(t, tls_stream_heap_used(NULL), 0);
    CHECK_EQ_U64(t, tls_stream_heap_peak(NULL), 0);
    CHECK(t, kit_init(&g_s, kit_ca_root()));
    CHECK(t, tls_stream_version(&g_s) == NULL); /* not open */
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    CHECK_EQ_U64(t, tls_stream_heap_peak(&g_s), 0);
    tls_stream_deinit(&g_s);
}

static void t_deinit_while_open_closes_the_connection(ctest_t *t)
{
    ports_stream_t p;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, false, kit_ca_root(), TLS_PEER_HOST, 0), PORTS_OK);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 0);
    tls_stream_deinit(&g_s);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    CHECK(t, kit_peer_client_closed());
    kit_peer_stop();
}

CTEST_SUITE("tls_stream_api",
    { "valid_config_is_accepted", t_valid_config_is_accepted },
    { "null_arguments_are_rejected", t_null_arguments_are_rejected },
    { "missing_connect_op", t_missing_connect_op },
    { "missing_send_op", t_missing_send_op },
    { "missing_recv_op", t_missing_recv_op },
    { "missing_wait_op", t_missing_wait_op },
    { "missing_close_op", t_missing_close_op },
    { "missing_transport_table", t_missing_transport_table },
    { "missing_clock", t_missing_clock },
    { "missing_wall_clock", t_missing_wall_clock },
    { "ca_bundle_rules", t_ca_bundle_rules },
    { "one_byte_nul_bundle_is_a_valid_config", t_one_byte_nul_bundle_is_a_valid_config },
    { "heap_rules", t_heap_rules },
    { "object_that_failed_init_is_unusable", t_object_that_failed_init_is_unusable },
    { "zeroed_object_is_unusable", t_zeroed_object_is_unusable },
    { "null_object_port_is_arg", t_null_object_port_is_arg },
    { "deinit_makes_the_object_unusable", t_deinit_makes_the_object_unusable },
    { "accessors_are_null_safe", t_accessors_are_null_safe },
    { "deinit_while_open_closes_the_connection", t_deinit_while_open_closes_the_connection })
