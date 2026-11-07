/* wire/src/sys.c: the real libc seam: open/read/close, read(2) on fd 0, write(2) on fd 1 and 2, CLOCK_REALTIME. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include "wire/src/internal.h"

static int64_t fail_errno(void)
{
    return -(int64_t)errno; /* POSIX: errno is set by a failed open or read */
}

static int64_t sys_fill(int fd, unsigned char *buf, size_t cap)
{
    size_t got = 0;
    while (got < cap) {
        ssize_t n = read(fd, buf + got, cap - got);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return fail_errno();
        }
        if (n == 0) {
            break;
        }
        got += (size_t)n;
    }
    return (int64_t)got;
}

static int64_t sys_read_file(void *ctx, const char *path, void *buf, size_t cap)
{
    (void)ctx;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return fail_errno();
    }
    int64_t r = sys_fill(fd, buf, cap);
    (void)close(fd); /* a read-only descriptor: nothing can be lost by a failing close */
    return r;
}

static int64_t sys_read_in(void *ctx, void *buf, size_t cap)
{
    (void)ctx;
    ssize_t n = read(0, buf, cap);
    return n < 0 ? fail_errno() : (int64_t)n;
}

static int64_t sys_write_out(void *ctx, int fd, const void *buf, size_t len)
{
    (void)ctx;
    if (fd != WIRE_STDOUT && fd != WIRE_STDERR) {
        return -(int64_t)EBADF;
    }
    ssize_t n = write(fd, buf, len);
    return n < 0 ? fail_errno() : (int64_t)n;
}

static uint64_t sys_unix_seconds(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0 || ts.tv_sec < 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec;
}

const wire_sys_t wire_sys_libc = {sys_read_file, sys_read_in, sys_write_out, sys_unix_seconds};

const wire_io_t wire_io_libc = {&wire_sys_libc, NULL, NULL, NULL, NULL, NULL, NULL};
