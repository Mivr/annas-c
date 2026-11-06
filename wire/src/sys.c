/* STUB (A024 test-author task): compiles, does nothing useful, so that the tests fail. The implementer replaces this
 * file (the libc seam: open/read/close, read, write, clock_gettime(CLOCK_REALTIME)). */
#include <errno.h>

#include "wire/wire.h"

static int64_t stub_read_file(void *ctx, const char *path, void *buf, size_t cap)
{
    (void) ctx;
    (void) path;
    (void) buf;
    (void) cap;
    return -ENOSYS;
}

static int64_t stub_read_in(void *ctx, void *buf, size_t cap)
{
    (void) ctx;
    (void) buf;
    (void) cap;
    return -ENOSYS;
}

static int64_t stub_write_out(void *ctx, int fd, const void *buf, size_t len)
{
    (void) ctx;
    (void) fd;
    (void) buf;
    (void) len;
    return -ENOSYS;
}

static uint64_t stub_unix_seconds(void *ctx)
{
    (void) ctx;
    return 0;
}

const wire_sys_t wire_sys_libc = {stub_read_file, stub_read_in, stub_write_out, stub_unix_seconds};

const wire_io_t wire_io_libc = {&wire_sys_libc, NULL, NULL, NULL, NULL, NULL, NULL};
