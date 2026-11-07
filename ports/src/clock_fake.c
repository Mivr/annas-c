#include "ports/fakes.h"

static uint64_t clock_now(void *ctx) {
    const ports_fake_clock_t *c = (const ports_fake_clock_t *)ctx;
    if (c == NULL) {
        return 0;
    }
    return c->now;
}

static const ports_clock_ops_t k_ops = {clock_now};

bool ports_fake_clock_init(ports_fake_clock_t *c, uint64_t start_ms) {
    if (c == NULL) {
        return false;
    }
    c->now = start_ms;
    return true;
}

ports_clock_t ports_fake_clock_port(ports_fake_clock_t *c) {
    ports_clock_t p = {&k_ops, c};
    return p;
}

void ports_fake_clock_advance(ports_fake_clock_t *c, uint64_t ms) {
    if (c == NULL) {
        return;
    }
    uint64_t sum = c->now + ms; /* wraps iff the true sum exceeds UINT64_MAX; a wrapped sum is below now */
    c->now = sum < c->now ? UINT64_MAX : sum;
}

bool ports_fake_clock_set(ports_fake_clock_t *c, uint64_t ms) {
    if (c == NULL || ms < c->now) {
        return false;
    }
    c->now = ms;
    return true;
}
