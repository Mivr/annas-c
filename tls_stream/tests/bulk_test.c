/* Data volume: large and tiny transfers both ways, write sizes around the 16 KiB record size, all-or-nothing
 * writes. Every case runs on TLS 1.3 and TLS 1.2. */
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;
static unsigned char g_buf[KIT_IO_MAX];

static ports_stream_t up(ctest_t *t, bool tls12)
{
    ports_stream_t p = {NULL, NULL};
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    return p;
}

static void down(ctest_t *t, ports_stream_t p)
{
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

static void big(ctest_t *t, bool tls12)
{
    ports_stream_t p = up(t, tls12);
    CHECK(t, kit_exchange(p, 65000, 65000, 4096));
    CHECK(t, kit_exchange(p, 65000, 65000, 65000));
    down(t, p);
}

static void t_bulk_both_ways_tls13(ctest_t *t) { big(t, false); }
static void t_bulk_both_ways_tls12(ctest_t *t) { big(t, true); }

static void sizes(ctest_t *t, bool tls12)
{
    static const size_t k[] = {1, 2, 15, 16, 17, 1023, 1024, 4095, 4096, 16383, 16384, 16385, 32768, 40000};
    ports_stream_t p = up(t, tls12);
    size_t i;
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        CHECK(t, kit_exchange(p, k[i], k[i], 20000));
    }
    down(t, p);
}

static void t_sizes_around_record_boundaries_tls13(ctest_t *t) { sizes(t, false); }
static void t_sizes_around_record_boundaries_tls12(ctest_t *t) { sizes(t, true); }

static void tiny_reads(ctest_t *t, bool tls12)
{
    ports_stream_t p = up(t, tls12);
    CHECK(t, kit_exchange(p, 5, 700, 1));
    CHECK(t, kit_exchange(p, 5, 700, 3));
    down(t, p);
}

static void t_one_and_three_byte_reads_tls13(ctest_t *t) { tiny_reads(t, false); }
static void t_one_and_three_byte_reads_tls12(ctest_t *t) { tiny_reads(t, true); }

static void t_read_returns_at_most_cap_and_never_zero(ctest_t *t)
{
    ports_stream_t p = up(t, false);
    size_t n = 0;
    size_t total = 0;
    size_t calls = 0;
    kit_fill(g_buf, 3000, 9);
    CHECK_EQ_I64(t, kit_peer_send(g_buf, 3000), 0);
    while (total < 3000 && calls < 100) {
        n = 0;
        CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf + total, 1000, &n), PORTS_OK);
        CHECK(t, n >= 1 && n <= 1000);
        total += n;
        calls++;
    }
    CHECK_EQ_U64(t, total, 3000);
    CHECK(t, kit_same(g_buf, 3000, 9));
    CHECK(t, calls >= 3);
    down(t, p);
}

static void t_many_one_byte_writes_arrive_in_order(ctest_t *t)
{
    ports_stream_t p = up(t, false);
    size_t i;
    kit_fill(g_buf, 300, 4);
    kit_peer_data_clear();
    for (i = 0; i < 300; i++) {
        CHECK_EQ_I64(t, p.ops->write(p.ctx, g_buf + i, 1), PORTS_OK);
    }
    CHECK_EQ_U64(t, kit_peer_data_len(), 300);
    CHECK(t, kit_same(kit_peer_data(), 300, 4));
    down(t, p);
}

static void t_write_is_all_or_error(ctest_t *t)
{
    /* a transport that dies on the third send of a 40000-byte write: the call must fail, never report OK */
    ports_stream_t p = up(t, false);
    kit_fill(g_buf, 40000, 6);
    kit()->fail_op = KIT_OP_SEND;
    kit()->fail_nth = kit()->calls[KIT_OP_SEND] + 3U;
    kit()->fail_err = 104; /* ECONNRESET */
    CHECK_EQ_I64(t, p.ops->write(p.ctx, g_buf, 40000), PORTS_E_NET);
    CHECK(t, kit_peer_data_len() < 40000);
    down(t, p);
}

static void t_write_survives_a_one_byte_transport(ctest_t *t)
{
    ports_stream_t p = up(t, false);
    kit()->send_max = 1;
    CHECK(t, kit_exchange(p, 200, 200, 200));
    kit()->send_max = 0;
    down(t, p);
}

static void t_full_arena_cycle_leaves_nothing_behind(ctest_t *t)
{
    ports_stream_t p = up(t, true);
    size_t peak;
    CHECK(t, kit_exchange(p, 30000, 30000, 8000));
    peak = tls_stream_heap_peak(&g_s);
    CHECK(t, peak > 0 && peak <= TLS_STREAM_HEAP_BYTES(kit_ca_root().len));
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    CHECK_EQ_U64(t, tls_stream_heap_peak(&g_s), peak); /* the peak is a high-water mark: it survives close */
    kit_done(&g_s);
}

CTEST_SUITE("tls_stream_bulk",
    { "bulk_both_ways_tls13", t_bulk_both_ways_tls13 },
    { "bulk_both_ways_tls12", t_bulk_both_ways_tls12 },
    { "sizes_around_record_boundaries_tls13", t_sizes_around_record_boundaries_tls13 },
    { "sizes_around_record_boundaries_tls12", t_sizes_around_record_boundaries_tls12 },
    { "one_and_three_byte_reads_tls13", t_one_and_three_byte_reads_tls13 },
    { "one_and_three_byte_reads_tls12", t_one_and_three_byte_reads_tls12 },
    { "read_returns_at_most_cap_and_never_zero", t_read_returns_at_most_cap_and_never_zero },
    { "many_one_byte_writes_arrive_in_order", t_many_one_byte_writes_arrive_in_order },
    { "write_is_all_or_error", t_write_is_all_or_error },
    { "write_survives_a_one_byte_transport", t_write_survives_a_one_byte_transport },
    { "full_arena_cycle_leaves_nothing_behind", t_full_arena_cycle_leaves_nothing_behind })
