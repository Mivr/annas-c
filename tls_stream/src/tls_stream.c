/* STUB (A017 test-author task): compiles, does nothing useful, so that every test of tests/ fails.
 * The implementer replaces this file. */
#include <stddef.h>

#include "tls_stream/tls_stream.h"

static ports_err_t stub_connect(void *ctx, core_str_t host, uint16_t port, uint64_t deadline_ms)
{
    (void) ctx;
    (void) host;
    (void) port;
    (void) deadline_ms;
    return PORTS_E_NET;
}

static ports_err_t stub_write(void *ctx, const void *buf, size_t len)
{
    (void) ctx;
    (void) buf;
    (void) len;
    return PORTS_E_NET;
}

static ports_err_t stub_read(void *ctx, void *buf, size_t cap, size_t *n)
{
    (void) ctx;
    (void) buf;
    (void) cap;
    (void) n;
    return PORTS_E_NET;
}

static ports_err_t stub_close(void *ctx)
{
    (void) ctx;
    return PORTS_E_NET;
}

static const ports_stream_ops_t k_stub_ops = {stub_connect, stub_write, stub_read, stub_close};

bool tls_stream_init(tls_stream_t *s, const tls_stream_config_t *cfg)
{
    (void) s;
    (void) cfg;
    return false;
}

ports_stream_t tls_stream_port(tls_stream_t *s)
{
    ports_stream_t p;
    p.ops = &k_stub_ops;
    p.ctx = s;
    return p;
}

void tls_stream_deinit(tls_stream_t *s)
{
    (void) s;
}

const char *tls_stream_version(const tls_stream_t *s)
{
    (void) s;
    return NULL;
}

size_t tls_stream_heap_used(const tls_stream_t *s)
{
    (void) s;
    return 0u;
}

size_t tls_stream_heap_peak(const tls_stream_t *s)
{
    (void) s;
    return 0u;
}
