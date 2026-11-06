/* STUB (test-author task A009): compiles, always fails. Replaced by the implementation task. */
#include "ports/fakes.h"

static uint64_t stub_now(void *ctx) {
    (void)ctx;
    return UINT64_MAX;
}

static const ports_clock_ops_t k_ops = {stub_now};

bool ports_fake_clock_init(ports_fake_clock_t *c, uint64_t start_ms) {
    (void)c;
    (void)start_ms;
    return false;
}

ports_clock_t ports_fake_clock_port(ports_fake_clock_t *c) {
    ports_clock_t p = {&k_ops, c};
    return p;
}

void ports_fake_clock_advance(ports_fake_clock_t *c, uint64_t ms) {
    (void)c;
    (void)ms;
}

bool ports_fake_clock_set(ports_fake_clock_t *c, uint64_t ms) {
    (void)c;
    (void)ms;
    return false;
}
