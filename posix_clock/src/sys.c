#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <time.h>

#include "posix_clock/posix_clock.h"

static int64_t sys_gettime(void *ctx, int64_t *sec, int64_t *nsec) {
    (void)ctx;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return errno > 0 ? -(int64_t)errno : -(int64_t)EINVAL;
    }
    *sec = (int64_t)ts.tv_sec;
    *nsec = (int64_t)ts.tv_nsec;
    return 0;
}

const posix_clock_sys_t posix_clock_sys_libc = {sys_gettime};
