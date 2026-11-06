/* End of stream: a peer close_notify is EOF (OK, n == 0, forever); a transport end of file WITHOUT
 * close_notify is PORTS_E_NET (a truncated message), at a record boundary, inside a record and during the
 * handshake, on TLS 1.3 and 1.2. Never the other way round. */
#include <string.h>

#include "ctest/ctest.h"
#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static tls_stream_t g_s;
static unsigned char g_buf[4096];

static ports_stream_t up(ctest_t *t, bool tls12)
{
    ports_stream_t p = {NULL, NULL};
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    return p;
}

/* read exactly `want` bytes (pieces of any size); returns the count obtained */
static size_t read_all(ctest_t *t, ports_stream_t p, size_t want)
{
    size_t got = 0;
    int calls = 0;
    while (got < want && calls < 64) {
        size_t n = 0;
        CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf + got, sizeof g_buf - got, &n), PORTS_OK);
        if (n == 0) {
            break;
        }
        got += n;
        calls++;
    }
    return got;
}

static void expect_eof(ctest_t *t, ports_stream_t p)
{
    size_t n = 55;
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_OK);
    CHECK_EQ_U64(t, n, 0);
}

static void expect_truncation(ctest_t *t, ports_stream_t p)
{
    size_t n = 55;
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_NET);
    CHECK_EQ_U64(t, n, 55); /* untouched on failure */
}

static void finish(ctest_t *t, ports_stream_t p)
{
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
    kit_done(&g_s);
}

static void close_notify_is_eof(ctest_t *t, bool tls12)
{
    ports_stream_t p = up(t, tls12);
    kit_fill(g_buf + 2000, 100, 5);
    CHECK_EQ_I64(t, kit_peer_send(g_buf + 2000, 100), 0);
    CHECK_EQ_I64(t, kit_peer_close_notify(), 0);
    CHECK_EQ_U64(t, read_all(t, p, 100), 100);
    CHECK(t, kit_same(g_buf, 100, 5));
    expect_eof(t, p);
    expect_eof(t, p);
    kit()->eof_when_idle = true; /* the transport now ends too: still a clean EOF, not a truncation */
    expect_eof(t, p);
    expect_eof(t, p);
    finish(t, p);
}

static void t_close_notify_is_eof_tls13(ctest_t *t) { close_notify_is_eof(t, false); }
static void t_close_notify_is_eof_tls12(ctest_t *t) { close_notify_is_eof(t, true); }

static void close_notify_without_data(ctest_t *t, bool tls12)
{
    ports_stream_t p = up(t, tls12);
    CHECK_EQ_I64(t, kit_peer_close_notify(), 0);
    expect_eof(t, p);
    expect_eof(t, p);
    finish(t, p);
}

static void t_close_notify_without_data_tls13(ctest_t *t) { close_notify_without_data(t, false); }
static void t_close_notify_without_data_tls12(ctest_t *t) { close_notify_without_data(t, true); }

static void eof_at_record_boundary(ctest_t *t, bool tls12)
{
    ports_stream_t p = up(t, tls12);
    kit_fill(g_buf + 2000, 100, 5);
    CHECK_EQ_I64(t, kit_peer_send(g_buf + 2000, 100), 0);
    kit()->eof_when_idle = true; /* the record is complete, then the transport ends without close_notify */
    CHECK_EQ_U64(t, read_all(t, p, 100), 100); /* the data that did arrive is delivered */
    CHECK(t, kit_same(g_buf, 100, 5));
    expect_truncation(t, p);
    expect_truncation(t, p); /* it stays an error: never turns into EOF */
    CHECK_EQ_I64(t, p.ops->write(p.ctx, g_buf, 4), PORTS_E_NET);
    finish(t, p);
}

static void t_eof_at_record_boundary_is_net_tls13(ctest_t *t) { eof_at_record_boundary(t, false); }
static void t_eof_at_record_boundary_is_net_tls12(ctest_t *t) { eof_at_record_boundary(t, true); }

static void eof_with_nothing_sent(ctest_t *t, bool tls12)
{
    ports_stream_t p = up(t, tls12);
    kit()->eof_when_idle = true;
    expect_truncation(t, p);
    expect_truncation(t, p);
    finish(t, p);
}

static void t_eof_right_after_handshake_is_net_tls13(ctest_t *t) { eof_with_nothing_sent(t, false); }
static void t_eof_right_after_handshake_is_net_tls12(ctest_t *t) { eof_with_nothing_sent(t, true); }

static void eof_inside_record(ctest_t *t, bool tls12, size_t keep)
{
    ports_stream_t p = up(t, tls12);
    size_t n = 55;
    kit_fill(g_buf + 2000, 600, 5);
    CHECK_EQ_I64(t, kit_peer_send(g_buf + 2000, 600), 0);
    CHECK(t, kit_inbox_len() > keep);
    kit_inbox_truncate(keep);
    kit()->eof_when_idle = true;
    CHECK_EQ_I64(t, p.ops->read(p.ctx, g_buf, sizeof g_buf, &n), PORTS_E_NET);
    CHECK_EQ_U64(t, n, 55); /* not a single byte of the unfinished record is released */
    expect_truncation(t, p);
    finish(t, p);
}

static void t_eof_inside_record_header_tls13(ctest_t *t)
{
    size_t k;
    for (k = 1; k <= 5; k++) {
        eof_inside_record(t, false, k);
    }
}

static void t_eof_inside_record_header_tls12(ctest_t *t)
{
    size_t k;
    for (k = 1; k <= 5; k++) {
        eof_inside_record(t, true, k);
    }
}

static void t_eof_inside_record_body_tls13(ctest_t *t)
{
    eof_inside_record(t, false, 6);
    eof_inside_record(t, false, 100);
    eof_inside_record(t, false, 400);
}

static void t_eof_inside_record_body_tls12(ctest_t *t)
{
    eof_inside_record(t, true, 6);
    eof_inside_record(t, true, 100);
    eof_inside_record(t, true, 400);
}

static void t_eof_one_byte_short_of_a_record_tls13(ctest_t *t)
{
    ports_stream_t p = up(t, false);
    size_t whole;
    kit_fill(g_buf + 2000, 600, 5);
    CHECK_EQ_I64(t, kit_peer_send(g_buf + 2000, 600), 0);
    whole = kit_inbox_len();
    kit_inbox_truncate(whole - 1);
    kit()->eof_when_idle = true;
    expect_truncation(t, p);
    finish(t, p);
}

static void t_complete_record_before_the_cut_is_delivered_tls12(ctest_t *t)
{
    ports_stream_t p = up(t, true);
    size_t first;
    kit_fill(g_buf + 2000, 200, 5);
    CHECK_EQ_I64(t, kit_peer_send(g_buf + 2000, 200), 0);
    first = kit_inbox_len();
    CHECK_EQ_I64(t, kit_peer_send(g_buf + 2000, 200), 0);
    kit_inbox_truncate(first + 20); /* second record cut inside */
    kit()->eof_when_idle = true;
    CHECK_EQ_U64(t, read_all(t, p, 200), 200);
    CHECK(t, kit_same(g_buf, 200, 5));
    expect_truncation(t, p);
    finish(t, p);
}

static void truncated_handshake(ctest_t *t, bool tls12)
{
    ports_stream_t p;
    size_t total;
    size_t k;
    kit_reset();
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    total = kit()->delivered_total; /* everything the server sends during a complete handshake */
    CHECK(t, total > 200);
    finish(t, p);
    /* the signature length varies by a byte or two from run to run: stay clear of the very end */
    for (k = 0; k + 16U < total; k += (k < 120 ? 1 : 83)) {
        kit_reset();
        kit()->inbound_limit = k;
        CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_E_NET);
        CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
        CHECK_EQ_U64(t, tls_stream_heap_used(&g_s), 0);
        CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_CLOSED);
        kit_done(&g_s);
    }
    kit_reset();
    kit()->inbound_limit = total + 64U; /* control: a limit beyond the whole handshake changes nothing */
    CHECK_EQ_I64(t, kit_open(&g_s, &p, TLS_PEER_EC_VALID, tls12, kit_ca_root(), TLS_PEER_HOST, KIT_DEADLINE), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    kit_done(&g_s);
}

static void t_truncated_handshake_is_net_tls13(ctest_t *t) { truncated_handshake(t, false); }
static void t_truncated_handshake_is_net_tls12(ctest_t *t) { truncated_handshake(t, true); }

static void t_close_notify_then_close_notifies_back(ctest_t *t)
{
    ports_stream_t p = up(t, false);
    CHECK_EQ_I64(t, kit_peer_close_notify(), 0);
    expect_eof(t, p);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_OK);
    CHECK_EQ_U64(t, kit()->calls[KIT_OP_CLOSE], 1);
    kit_done(&g_s);
}

CTEST_SUITE("tls_stream_eof",
    { "close_notify_is_eof_tls13", t_close_notify_is_eof_tls13 },
    { "close_notify_is_eof_tls12", t_close_notify_is_eof_tls12 },
    { "close_notify_without_data_tls13", t_close_notify_without_data_tls13 },
    { "close_notify_without_data_tls12", t_close_notify_without_data_tls12 },
    { "eof_at_record_boundary_is_net_tls13", t_eof_at_record_boundary_is_net_tls13 },
    { "eof_at_record_boundary_is_net_tls12", t_eof_at_record_boundary_is_net_tls12 },
    { "eof_right_after_handshake_is_net_tls13", t_eof_right_after_handshake_is_net_tls13 },
    { "eof_right_after_handshake_is_net_tls12", t_eof_right_after_handshake_is_net_tls12 },
    { "eof_inside_record_header_tls13", t_eof_inside_record_header_tls13 },
    { "eof_inside_record_header_tls12", t_eof_inside_record_header_tls12 },
    { "eof_inside_record_body_tls13", t_eof_inside_record_body_tls13 },
    { "eof_inside_record_body_tls12", t_eof_inside_record_body_tls12 },
    { "eof_one_byte_short_of_a_record_tls13", t_eof_one_byte_short_of_a_record_tls13 },
    { "complete_record_before_the_cut_is_delivered_tls12", t_complete_record_before_the_cut_is_delivered_tls12 },
    { "truncated_handshake_is_net_tls13", t_truncated_handshake_is_net_tls13 },
    { "truncated_handshake_is_net_tls12", t_truncated_handshake_is_net_tls12 },
    { "close_notify_then_close_notifies_back", t_close_notify_then_close_notifies_back })
