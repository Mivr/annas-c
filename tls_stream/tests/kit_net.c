/* Test scaffolding: the fault-injecting transport, the manual clock and the pipe to the peer. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ports/fakes.h"
#include "tls_stream/tests/kit.h"

static kit_t g_kit;
static ports_fake_clock_t g_clock;
static unsigned char g_inbox[KIT_IO_MAX];
static size_t g_inbox_len;
static bool g_ready_next;
static bool g_first_recv_done;
static _Alignas(16) unsigned char g_heap[KIT_HEAP_BYTES];

kit_t *kit(void)
{
    return &g_kit;
}

void kit_reset(void)
{
    kit_peer_stop();
    kit_peer_data_clear();
    memset(&g_kit, 0, sizeof g_kit);
    g_kit.wall_s = KIT_TIME_VALID;
    g_kit.inbound_limit = SIZE_MAX;
    g_inbox_len = 0;
    g_ready_next = false;
    g_first_recv_done = false;
    (void) ports_fake_clock_init(&g_clock, KIT_T0);
}

ports_clock_t kit_clock(void)
{
    return ports_fake_clock_port(&g_clock);
}

uint64_t kit_now(void)
{
    return g_clock.now;
}

void kit_clock_advance(uint64_t ms)
{
    ports_fake_clock_advance(&g_clock, ms);
}

void kit_split_next_recv(size_t k)
{
    g_kit.first_recv_max = k;
    g_first_recv_done = false;
}

size_t kit_inbox_len(void)
{
    return g_inbox_len;
}

void kit_inbox_truncate(size_t keep)
{
    if (keep < g_inbox_len) {
        g_inbox_len = keep;
    }
}

void kit_inbox_corrupt(size_t index)
{
    if (index < g_inbox_len) {
        g_inbox[index] ^= 0x01U;
    }
}

void kit_inbox_push(const void *data, size_t len)
{
    if (len <= sizeof g_inbox - g_inbox_len) {
        memcpy(g_inbox + g_inbox_len, data, len);
        g_inbox_len += len;
    }
}

/* 0 = no fault; else the errno to inject into this call of `op`. */
static int inject(kit_op_t op)
{
    uint32_t n = ++g_kit.calls[op];
    if (g_kit.fail_nth != 0U && g_kit.fail_op == op &&
        (n == g_kit.fail_nth || (g_kit.fail_persist && n > g_kit.fail_nth))) {
        if (g_kit.fail_err == EAGAIN) {
            g_ready_next = true;
        }
        return g_kit.fail_err;
    }
    return 0;
}

static int t_connect(void *ctx, core_str_t host, uint16_t port, uint64_t deadline_ms)
{
    int err = inject(KIT_OP_CONNECT);
    (void) ctx;
    g_kit.port = port;
    g_kit.connect_deadline = deadline_ms;
    memset(g_kit.host, 0, sizeof g_kit.host);
    memcpy(g_kit.host, host.ptr, host.len < sizeof g_kit.host - 1U ? host.len : sizeof g_kit.host - 1U);
    kit_clock_advance(g_kit.connect_advance_ms);
    return err;
}

static long t_send(void *ctx, const void *buf, size_t len)
{
    int err = inject(KIT_OP_SEND);
    size_t n = len;
    (void) ctx;
    if (err != 0) {
        return -(long) err;
    }
    if (g_kit.send_zero) {
        return 0;
    }
    if (g_kit.send_max != 0U && n > g_kit.send_max) {
        n = g_kit.send_max;
    }
    if (g_kit.sent_first_len < sizeof g_kit.sent_first) {
        size_t room = sizeof g_kit.sent_first - g_kit.sent_first_len;
        size_t k = n < room ? n : room;
        memcpy(g_kit.sent_first + g_kit.sent_first_len, buf, k);
        g_kit.sent_first_len += k;
    }
    g_kit.sent_total += n;
    if (!g_kit.mute) {
        kit_peer_ingest(buf, n);
    }
    return g_kit.send_over ? (long) len + 1L : (long) n;
}

static size_t recv_quota(size_t cap)
{
    size_t n = cap < g_inbox_len ? cap : g_inbox_len;
    size_t left = g_kit.inbound_limit - g_kit.delivered_total;
    if (g_kit.recv_max != 0U && n > g_kit.recv_max) {
        n = g_kit.recv_max;
    }
    if (!g_first_recv_done && g_kit.first_recv_max != 0U && n > g_kit.first_recv_max) {
        n = g_kit.first_recv_max;
    }
    return n < left ? n : left;
}

static long t_recv(void *ctx, void *buf, size_t cap)
{
    int err = inject(KIT_OP_RECV);
    size_t n;
    (void) ctx;
    if (err != 0) {
        return -(long) err;
    }
    if (g_kit.recv_over) {
        return (long) cap + 1L;
    }
    n = recv_quota(cap);
    if (g_kit.delivered_total >= g_kit.inbound_limit) {
        return 0;
    }
    if (g_inbox_len == 0U) {
        return g_kit.eof_when_idle ? 0 : -(long) EAGAIN;
    }
    memcpy(buf, g_inbox, n);
    memmove(g_inbox, g_inbox + n, g_inbox_len - n);
    g_inbox_len -= n;
    g_kit.delivered_total += n;
    g_first_recv_done = true;
    return (long) n;
}

static int t_wait(void *ctx, bool for_write, uint64_t deadline_ms)
{
    int err = inject(KIT_OP_WAIT);
    (void) ctx;
    g_kit.wait_deadline = deadline_ms;
    if (for_write) {
        g_kit.wait_write_calls++;
    }
    if (err != 0) {
        return err;
    }
    if (g_kit.wait_eintr_first && g_kit.calls[KIT_OP_WAIT] == 1U) {
        return EINTR;
    }
    if (g_kit.wait_timeout && deadline_ms != 0U) {
        if (g_clock.now < deadline_ms) {
            g_clock.now = deadline_ms;
        }
        return ETIMEDOUT;
    }
    if (g_kit.wait_ready) {
        kit_clock_advance(g_kit.wait_advance_ms);
        return 0;
    }
    if (g_ready_next) {
        g_ready_next = false;
        return 0;
    }
    if (deadline_ms == 0U) {
        return EIO; /* a silent peer and no deadline would block forever: report a failure instead */
    }
    if (g_clock.now < deadline_ms) {
        g_clock.now = deadline_ms;
    }
    return ETIMEDOUT;
}

static void t_close(void *ctx)
{
    (void) ctx;
    g_kit.calls[KIT_OP_CLOSE]++;
}

static const tls_stream_transport_ops_t k_ops = {t_connect, t_send, t_recv, t_wait, t_close};

static uint64_t wall(void *ctx)
{
    (void) ctx;
    return g_kit.wall_s;
}

tls_stream_config_t kit_config(core_str_t ca_pem)
{
    tls_stream_config_t c;
    memset(&c, 0, sizeof c);
    c.transport.ops = &k_ops;
    c.clock = kit_clock();
    c.wall.unix_seconds = wall;
    c.ca_pem = ca_pem;
    c.heap = g_heap;
    c.heap_size = g_kit.heap_size != 0U ? g_kit.heap_size : sizeof g_heap;
    return c;
}

bool kit_init(tls_stream_t *s, core_str_t ca_pem)
{
    tls_stream_config_t c = kit_config(ca_pem);
    return tls_stream_init(s, &c);
}

ports_err_t kit_open(tls_stream_t *s, ports_stream_t *p, int identity, bool tls12_only, core_str_t ca_pem,
                     const char *host, uint64_t deadline)
{
    if (kit_peer_start(identity, tls12_only) != 0 || !kit_init(s, ca_pem)) {
        return PORTS_E_ARG;
    }
    *p = tls_stream_port(s);
    return p->ops->connect(p->ctx, core_str_from_cstr(host), (uint16_t) KIT_PORT, deadline);
}

void kit_done(tls_stream_t *s)
{
    tls_stream_deinit(s);
    kit_peer_stop();
}

void kit_fill(unsigned char *buf, size_t len, unsigned seed)
{
    size_t i;
    for (i = 0; i < len; i++) {
        buf[i] = (unsigned char) ((i * 31U + (i >> 8) * 7U + seed) & 0xFFU);
    }
}

bool kit_same(const unsigned char *buf, size_t len, unsigned seed)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if (buf[i] != (unsigned char) ((i * 31U + (i >> 8) * 7U + seed) & 0xFFU)) {
            return false;
        }
    }
    return true;
}
