/* wire/src/io.c: the bounded write and read loops over the wire_sys_t seam. */
#include <errno.h>

#include "wire/src/internal.h"

/* One write_out call, repeated while it returns -EINTR: at most WIRE_EINTR_MAX times in a row (then the -EINTR is the result). */
static int64_t write_retry(const wire_io_t *io, int fd, const char *buf, size_t len)
{
    int eintr = 0;
    int64_t r;
    do {
        r = io->sys->write_out(io->sys_ctx, fd, buf, len);
    } while (r == -EINTR && eintr++ < WIRE_EINTR_MAX);
    return r;
}

bool wire_write_all(const wire_io_t *io, int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        int64_t r = write_retry(io, fd, buf + off, len - off);
        if (r <= 0 || (uint64_t)r > (uint64_t)(len - off)) {
            return false;
        }
        off += (size_t)r;
    }
    return true;
}

int64_t wire_read_some(const wire_io_t *io, void *buf, size_t cap)
{
    int eintr = 0;
    for (;;) {
        int64_t r = io->sys->read_in(io->sys_ctx, buf, cap);
        if (r == -EINTR) {
            eintr++;
            if (eintr > WIRE_EINTR_MAX) {
                return -1;
            }
            continue;
        }
        if (r < 0 || (uint64_t)r > (uint64_t)cap) {
            return -1;
        }
        return r;
    }
}
