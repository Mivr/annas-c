/* STUB (test-author task A009): compiles, always fails. Replaced by the implementation task. */
#include "ports/fakes.h"

static ports_err_t stub_connect(void *ctx, core_str_t host, uint16_t port, uint64_t deadline_ms) {
    (void)ctx;
    (void)host;
    (void)port;
    (void)deadline_ms;
    return PORTS_E_ARG;
}

static ports_err_t stub_write(void *ctx, const void *buf, size_t len) {
    (void)ctx;
    (void)buf;
    (void)len;
    return PORTS_E_ARG;
}

static ports_err_t stub_read(void *ctx, void *buf, size_t cap, size_t *n) {
    (void)ctx;
    (void)buf;
    (void)cap;
    (void)n;
    return PORTS_E_ARG;
}

static ports_err_t stub_close(void *ctx) {
    (void)ctx;
    return PORTS_E_ARG;
}

static const ports_stream_ops_t k_ops = {stub_connect, stub_write, stub_read, stub_close};

bool ports_fake_stream_init(ports_fake_stream_t *s, const ports_fake_step_t *steps, size_t count,
                            ports_fake_clock_t *clock, uint64_t read_advance_ms) {
    (void)s;
    (void)steps;
    (void)count;
    (void)clock;
    (void)read_advance_ms;
    return false;
}

ports_stream_t ports_fake_stream_port(ports_fake_stream_t *s) {
    ports_stream_t p = {&k_ops, s};
    return p;
}

bool ports_fake_stream_done(const ports_fake_stream_t *s) {
    (void)s;
    return false;
}

bool ports_fake_stream_mismatch(const ports_fake_stream_t *s, ports_fake_mismatch_t *out) {
    (void)s;
    (void)out;
    return false;
}

bool ports_fake_stream_is_open(const ports_fake_stream_t *s) {
    (void)s;
    return false;
}

uint32_t ports_fake_stream_closes(const ports_fake_stream_t *s) {
    (void)s;
    return UINT32_MAX;
}

size_t ports_fake_stream_pos(const ports_fake_stream_t *s) {
    (void)s;
    return SIZE_MAX;
}
