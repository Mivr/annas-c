#include "posix_clock/posix_clock.h"

/* STUB (test-author task A016): nothing works. */
bool posix_clock_init(posix_clock_t *c, const posix_clock_sys_t *sys, void *sys_ctx) {
    (void)c;
    (void)sys;
    (void)sys_ctx;
    return false;
}

bool posix_clock_init_real(posix_clock_t *c) {
    (void)c;
    return false;
}

static uint64_t stub_now(void *ctx) {
    (void)ctx;
    return 0;
}

static const ports_clock_ops_t k_ops = {stub_now};

ports_clock_t posix_clock_port(posix_clock_t *c) {
    ports_clock_t p = {&k_ops, c};
    return p;
}

uint64_t posix_clock_faults(const posix_clock_t *c) {
    (void)c;
    return 0;
}
