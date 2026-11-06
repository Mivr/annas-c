#include "posix_clock/posix_clock.h"

#define NSEC_PER_MS 1000000
#define MS_PER_SEC 1000u

bool posix_clock_init(posix_clock_t *c, const posix_clock_sys_t *sys, void *sys_ctx) {
    if (c == NULL || sys == NULL || sys->gettime == NULL) {
        return false;
    }
    c->sys = sys;
    c->sys_ctx = sys_ctx;
    c->last_ms = 0;
    c->faults = 0;
    return true;
}

bool posix_clock_init_real(posix_clock_t *c) {
    return posix_clock_init(c, &posix_clock_sys_libc, NULL);
}

/* Saturating sec * 1000 + nsec / 1e6 for a valid reading. */
static uint64_t to_ms(int64_t sec, int64_t nsec) {
    uint64_t s = (uint64_t)sec;
    uint64_t extra = (uint64_t)(nsec / NSEC_PER_MS);
    if (s > (UINT64_MAX - extra) / MS_PER_SEC) {
        return UINT64_MAX;
    }
    return s * MS_PER_SEC + extra;
}

static uint64_t clock_now(void *ctx) {
    posix_clock_t *c = (posix_clock_t *)ctx;
    if (c == NULL) {
        return 0;
    }
    int64_t sec = 0;
    int64_t nsec = 0;
    int64_t rc = c->sys->gettime(c->sys_ctx, &sec, &nsec);
    if (rc != 0 || sec < 0 || nsec < 0 || nsec > 999999999) {
        c->faults++;
        return c->last_ms;
    }
    uint64_t ms = to_ms(sec, nsec);
    if (ms > c->last_ms) {
        c->last_ms = ms;
    }
    return c->last_ms;
}

static const ports_clock_ops_t k_ops = {clock_now};

ports_clock_t posix_clock_port(posix_clock_t *c) {
    ports_clock_t p = {&k_ops, c};
    return p;
}

uint64_t posix_clock_faults(const posix_clock_t *c) {
    return c == NULL ? 0 : c->faults;
}
