/* STUB (A017 test-author task): the implementer replaces this file (the POSIX socket transport). */
#include <errno.h>
#include <stddef.h>

#include "tls_stream/tls_stream.h"

static int stub_connect(void *ctx, core_str_t host, uint16_t port, uint64_t deadline_ms)
{
    (void) ctx;
    (void) host;
    (void) port;
    (void) deadline_ms;
    return ENOSYS;
}

static long stub_send(void *ctx, const void *buf, size_t len)
{
    (void) ctx;
    (void) buf;
    (void) len;
    return -(long) ENOSYS;
}

static long stub_recv(void *ctx, void *buf, size_t cap)
{
    (void) ctx;
    (void) buf;
    (void) cap;
    return -(long) ENOSYS;
}

static int stub_wait(void *ctx, bool for_write, uint64_t deadline_ms)
{
    (void) ctx;
    (void) for_write;
    (void) deadline_ms;
    return ENOSYS;
}

static void stub_close(void *ctx)
{
    (void) ctx;
}

static const tls_stream_transport_ops_t k_stub_ops = {stub_connect, stub_send, stub_recv, stub_wait, stub_close};

bool tls_stream_sock_init(tls_stream_sock_t *s, ports_clock_t clock)
{
    (void) s;
    (void) clock;
    return false;
}

tls_stream_transport_t tls_stream_sock_transport(tls_stream_sock_t *s)
{
    tls_stream_transport_t t;
    t.ops = &k_stub_ops;
    t.ctx = s;
    return t;
}
