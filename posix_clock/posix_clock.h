/* posix_clock/posix_clock.h: the production ports_clock_t, over CLOCK_MONOTONIC and a syscall seam.
 *
 * Only a seam (a const table of one function) touches the OS, so tests drive the adapter with scripted readings and
 * a fault-injecting fake, and with the real table. This header needs no POSIX feature macro: the seam uses plain
 * C types. The adapter never allocates and never sleeps.
 *
 * Seam contract: gettime(ctx, &sec, &nsec) returns 0 and stores the monotonic reading, or returns a NEGATIVE errno
 * value (-EINVAL, ...) and stores nothing. The real table (`posix_clock_sys_libc`) calls
 * clock_gettime(CLOCK_MONOTONIC) and ignores `ctx`.
 *
 * Reading -> milliseconds: ms = sec * 1000 + nsec / 1000000 (integer division: sub-millisecond parts are dropped,
 * never rounded up). A reading is VALID iff sec >= 0 and 0 <= nsec <= 999999999; anything else is a fault. The
 * conversion saturates at UINT64_MAX instead of wrapping (sec * 1000 + ... > UINT64_MAX gives UINT64_MAX; the
 * largest exact value is sec = 18446744073709551, nsec = 615000000).
 *
 * Monotonic clamp: the adapter remembers the largest value it ever returned (initially 0) and now_ms never returns
 * less: a smaller (valid) reading returns the remembered value. A FAULT (seam error or invalid reading) also returns
 * the remembered value and increments the fault counter; it is not retried (one seam call per now_ms call).
 * posix_clock_init makes no seam call. */
#ifndef POSIX_CLOCK_POSIX_CLOCK_H
#define POSIX_CLOCK_POSIX_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "ports/ports.h"

typedef struct {
    int64_t (*gettime)(void *ctx, int64_t *sec, int64_t *nsec);
} posix_clock_sys_t;

/* The real libc table (clock_gettime on CLOCK_MONOTONIC). */
extern const posix_clock_sys_t posix_clock_sys_libc;

/* Caller-provided storage. Treat the fields as opaque; use only the functions. */
typedef struct {
    const posix_clock_sys_t *sys;
    void *sys_ctx;
    uint64_t last_ms;
    uint64_t faults;
} posix_clock_t;

/* Initialise over `sys`/`sys_ctx` (sys_ctx is passed to the seam unchanged and may be NULL). Returns false and
 * leaves *c untouched when `c` or `sys` is NULL or sys->gettime is NULL. */
bool posix_clock_init(posix_clock_t *c, const posix_clock_sys_t *sys, void *sys_ctx);

/* Same as posix_clock_init(c, &posix_clock_sys_libc, NULL): false only when `c` is NULL. */
bool posix_clock_init_real(posix_clock_t *c);

/* The port view of `c` (valid while `c` lives; one shared const ops table). A NULL `c` gives a port whose now_ms
 * returns 0. */
ports_clock_t posix_clock_port(posix_clock_t *c);

/* How many now_ms calls so far hit a fault (seam error or invalid reading). NULL gives 0. */
uint64_t posix_clock_faults(const posix_clock_t *c);

#endif
