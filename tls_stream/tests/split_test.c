/* Chunked delivery: the transport hands the bytes over in pieces of every size, split at every point of the
 * first records (handshake) and of application records, and accepts few bytes per send. */
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;
static unsigned char g_buf[8192];

static ports_err_t open_split(ports_stream_t *p, bool tls12, size_t first, size_t every)
{
    kit_reset();
    kit()->first_recv_max = first;
    kit()->recv_max = every;
    return kit_open(&g_s, p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE);
}

static void close_up(ctest_t *t, ports_stream_t p)
{
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

static void handshake_first_chunk(ctest_t *t, bool tls12)
{
    ports_stream_t p;
    size_t k;
    for (k = 1; k <= 140; k++) {
        CHECK_EQ_I64(t, open_split(&p, tls12, k, 0), PORTS_OK);
        CHECK(t, kit_exchange(p, 40, 40, 40));
        CHECK(t, kit()->calls[KIT_OP_RECV] >= 2);
        close_up(t, p);
    }
}

static void t_handshake_split_at_every_early_byte_tls13(ctest_t *t) { handshake_first_chunk(t, false); }
static void t_handshake_split_at_every_early_byte_tls12(ctest_t *t) { handshake_first_chunk(t, true); }

static void handshake_later_points(ctest_t *t, bool tls12)
{
    ports_stream_t p;
    size_t k;
    for (k = 141; k < 3000; k += 97) {
        CHECK_EQ_I64(t, open_split(&p, tls12, k, 0), PORTS_OK);
        CHECK(t, kit_exchange(p, 40, 40, 40));
        close_up(t, p);
    }
}

static void t_handshake_split_later_points_tls13(ctest_t *t) { handshake_later_points(t, false); }
static void t_handshake_split_later_points_tls12(ctest_t *t) { handshake_later_points(t, true); }

static void t_whole_session_one_byte_at_a_time_tls13(ctest_t *t)
{
    ports_stream_t p;
    CHECK_EQ_I64(t, open_split(&p, false, 0, 1), PORTS_OK);
    CHECK(t, kit_exchange(p, 100, 600, 128));
    CHECK(t, kit()->calls[KIT_OP_RECV] > 1000);
    close_up(t, p);
}

static void t_whole_session_one_byte_at_a_time_tls12(ctest_t *t)
{
    ports_stream_t p;
    CHECK_EQ_I64(t, open_split(&p, true, 0, 1), PORTS_OK);
    CHECK(t, kit_exchange(p, 100, 600, 128));
    CHECK(t, kit()->calls[KIT_OP_RECV] > 1000);
    close_up(t, p);
}

static size_t read_record(ctest_t *t, ports_stream_t p, size_t want)
{
    size_t got = 0;
    size_t calls = 0;
    while (got < want && calls < 400) {
        size_t n = 0;
        CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf + 4000 + got, 4000, &n), PORTS_OK);
        if (n == 0 || got + n > want) {
            break;
        }
        got += n;
        calls++;
    }
    return got;
}

static void app_record_splits(ctest_t *t, bool tls12)
{
    ports_stream_t p;
    size_t k;
    CHECK_EQ_I64(t, open_split(&p, tls12, 0, 0), PORTS_OK);
    kit_fill(g_buf, 300, 3);
    for (k = 1; k <= 330; k++) {
        CHECK_EQ_I64(t, kit_peer_send(g_buf, 300), 0);
        kit_split_next_recv(k);
        CHECK_EQ_U64(t, read_record(t, p, 300), 300);
        CHECK_EQ_I64(t, memcmp(g_buf, g_buf + 4000, 300), 0);
    }
    close_up(t, p);
}

static void t_application_record_split_everywhere_tls13(ctest_t *t) { app_record_splits(t, false); }
static void t_application_record_split_everywhere_tls12(ctest_t *t) { app_record_splits(t, true); }

static void sends(ctest_t *t, bool tls12)
{
    static const size_t caps[] = {1, 2, 3, 5, 7, 64, 1000};
    ports_stream_t p;
    size_t i;
    for (i = 0; i < sizeof caps / sizeof caps[0]; i++) {
        kit_reset();
        kit()->send_max = caps[i];
        CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
        CHECK(t, kit_exchange(p, 300, 300, 300));
        CHECK(t, kit()->calls[KIT_OP_SEND] >= (caps[i] == 1000 ? 2U : 3U));
        close_up(t, p);
    }
}

static void t_transport_accepting_few_bytes_per_send_tls13(ctest_t *t) { sends(t, false); }
static void t_transport_accepting_few_bytes_per_send_tls12(ctest_t *t) { sends(t, true); }

CTEST_SUITE("tls_stream_split",
    { "handshake_split_at_every_early_byte_tls13", t_handshake_split_at_every_early_byte_tls13 },
    { "handshake_split_at_every_early_byte_tls12", t_handshake_split_at_every_early_byte_tls12 },
    { "handshake_split_later_points_tls13", t_handshake_split_later_points_tls13 },
    { "handshake_split_later_points_tls12", t_handshake_split_later_points_tls12 },
    { "whole_session_one_byte_at_a_time_tls13", t_whole_session_one_byte_at_a_time_tls13 },
    { "whole_session_one_byte_at_a_time_tls12", t_whole_session_one_byte_at_a_time_tls12 },
    { "application_record_split_everywhere_tls13", t_application_record_split_everywhere_tls13 },
    { "application_record_split_everywhere_tls12", t_application_record_split_everywhere_tls12 },
    { "transport_accepting_few_bytes_per_send_tls13", t_transport_accepting_few_bytes_per_send_tls13 },
    { "transport_accepting_few_bytes_per_send_tls12", t_transport_accepting_few_bytes_per_send_tls12 })
