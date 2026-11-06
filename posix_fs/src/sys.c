#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "posix_fs/src/internal.h"

/* Every function returns the result (>= 0) or -errno. */
static int64_t fail_errno(void) {
    return errno > 0 ? -(int64_t)errno : -(int64_t)EIO;
}

static int64_t sys_open_dir(void *ctx, const char *path) {
    (void)ctx;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    return fd < 0 ? fail_errno() : (int64_t)fd;
}

static int64_t sys_open_tmp(void *ctx, int dir_fd, unsigned mode) {
    (void)ctx;
    int fd = openat(dir_fd, ".", O_TMPFILE | O_WRONLY | O_CLOEXEC, mode);
    return fd < 0 ? fail_errno() : (int64_t)fd;
}

static int64_t sys_pwrite(void *ctx, int fd, const void *buf, size_t len, uint64_t off) {
    (void)ctx;
    if (off > (uint64_t)INT64_MAX) {
        return -(int64_t)EFBIG;
    }
    ssize_t n = pwrite(fd, buf, len, (off_t)off);
    return n < 0 ? fail_errno() : (int64_t)n;
}

static int64_t sys_ftruncate(void *ctx, int fd, uint64_t len) {
    (void)ctx;
    if (len > (uint64_t)INT64_MAX) {
        return -(int64_t)EFBIG;
    }
    return ftruncate(fd, (off_t)len) != 0 ? fail_errno() : 0;
}

static int64_t sys_fsync(void *ctx, int fd) {
    (void)ctx;
    return fsync(fd) != 0 ? fail_errno() : 0;
}

static int64_t sys_link_tmp(void *ctx, int fd, int dir_fd, const char *name) {
    (void)ctx;
    char path[48];
    int n = snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
    if (n < 0 || (size_t)n >= sizeof path) {
        return -(int64_t)EINVAL;
    }
    return linkat(AT_FDCWD, path, dir_fd, name, AT_SYMLINK_FOLLOW) != 0 ? fail_errno() : 0;
}

static int64_t sys_close(void *ctx, int fd) {
    (void)ctx;
    return close(fd) != 0 ? fail_errno() : 0;
}

const posix_fs_sys_t posix_fs_sys_libc = {sys_open_dir, sys_open_tmp, sys_pwrite, sys_ftruncate,
                                          sys_fsync,    sys_link_tmp, sys_close};
