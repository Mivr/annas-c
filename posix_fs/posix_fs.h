/* posix_fs/posix_fs.h: the production ports_fs_t: temp files that are invisible by name inside ONE directory,
 * committed atomically under a final name, over a syscall seam. Linux only (O_TMPFILE, linkat).
 *
 * Seam. Every OS call goes through the const table posix_fs_sys_t; `posix_fs_sys_libc` is the real one. Return
 * convention of EVERY seam function: a value >= 0 is the result (a file descriptor, a byte count, or 0), a negative
 * value is -errno. No seam function touches the global errno from the adapter's point of view. The adapter passes
 * `sys_ctx` unchanged as the first argument. Real-table meaning of each entry:
 *   open_dir(path)            open(path, O_RDONLY|O_DIRECTORY|O_CLOEXEC)                      -> fd
 *   open_tmp(dir_fd, mode)    openat(dir_fd, ".", O_TMPFILE|O_WRONLY|O_CLOEXEC, mode)         -> fd
 *   pwrite(fd, buf, len, off) pwrite(fd, buf, len, off); off <= INT64_MAX                     -> bytes written
 *   ftruncate(fd, len)        ftruncate(fd, len)                                              -> 0
 *   fsync(fd)                 fsync(fd)                                                       -> 0
 *   link_tmp(fd, dir_fd, name) linkat(AT_FDCWD, "/proc/self/fd/<fd>", dir_fd, name, AT_SYMLINK_FOLLOW) -> 0
 *                             (`name` is NUL-terminated, 1..255 bytes, already validated; fails with EEXIST and
 *                              overwrites nothing if the name exists)
 *   close(fd)                 close(fd). The descriptor is released even when close fails (Linux), so the adapter
 *                             never closes a descriptor twice and never retries close.
 *
 * Temp files. create_temp makes an O_TMPFILE: it has NO name at any time, so a crash or a leftover handle can never
 * leave a file in the directory (the kernel frees it when the descriptor closes). There is NO fallback to a named
 * temp file: a filesystem without O_TMPFILE support makes create_temp fail with PORTS_FS_E_IO. File mode on commit is
 * POSIX_FS_FILE_MODE (0644) minus the process umask. The directory descriptor is held for the life of the object, so
 * moving or removing the directory path later does not redirect later commits (a removed directory makes commit fail
 * with PORTS_FS_E_IO).
 *
 * Handles. A handle is never 0 and never reused while the object is open: ids count 1, 2, 3, ... (skipping 0 if the
 * 32-bit counter ever wraps), so a stale handle can never alias a newer temp file whatever the slot. There are
 * POSIX_FS_SLOTS temp files at once; the next create_temp then fails with PORTS_FS_E_NOSPACE without any seam call.
 *
 * Errors. Retry: any seam call except close that returns -EINTR is repeated (no limit). Mapping of -errno from the
 * seam (every op except where noted): ENOSPC, EDQUOT, EFBIG -> PORTS_FS_E_NOSPACE; link_tmp only: EEXIST ->
 * PORTS_FS_E_EXISTS; anything else -> PORTS_FS_E_IO (so EEXIST from any other op is IO).
 *
 * write(file, buf, len), order of checks: NULL ctx / unknown handle -> E_ARG; then NULL buf with len > 0 -> E_ARG;
 * then len == 0 -> OK with no seam call; then a BROKEN temp file -> E_IO; then offset + len > INT64_MAX -> E_NOSPACE (no
 * seam call). Otherwise pwrite is called at offset = bytes stored so far, with ALL remaining bytes each time
 * (never chunked), repeatedly until everything is written (short writes continue at the new offset). A pwrite that
 * returns 0 for len > 0, or more than it was given, is E_IO. On any failure after k >= 1 bytes already reached
 * the file in this call, the adapter calls ftruncate(fd, bytes stored before this call) so the content is exactly
 * as before; if k == 0 it makes no ftruncate call. If that ftruncate fails, the content is unknown: the temp file
 * becomes BROKEN, write returns the error mapped from the write failure, and from then on write and commit on that
 * handle return PORTS_FS_E_IO (discard still works).
 *
 * commit(file, name), order: NULL ctx / unknown handle -> E_ARG; invalid name -> E_ARG (name rules of ports.h; NULL
 * ptr with len > 0 is invalid); broken -> E_IO; then fsync(fd); then link_tmp(fd, dir_fd, name); then close(fd).
 * The data is therefore on stable storage before the name becomes visible; the directory itself is not fsynced.
 * Any failure before the link succeeded leaves the temp file open and valid. After the link succeeded the commit
 * is OK whatever close returns, and the slot is freed.
 *
 * discard(file): NULL ctx / unknown handle -> E_ARG; otherwise close(fd) once, free the slot (the handle is
 * consumed) and return OK; if close returned an error other than -EINTR the result is E_IO (the handle is consumed
 * anyway). -EINTR from close counts as success.
 *
 * create_temp(&file): NULL ctx or NULL file -> E_ARG; closed object -> E_IO; no free slot -> E_NOSPACE; else
 * open_tmp; on failure *file is untouched.
 *
 * Failure of any call leaves every out-parameter as the caller initialised it. No malloc; ~O(1) stack. */
#ifndef POSIX_FS_POSIX_FS_H
#define POSIX_FS_POSIX_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ports/ports.h"

#define POSIX_FS_SLOTS 8u
#define POSIX_FS_FILE_MODE 0644u

typedef struct {
    int64_t (*open_dir)(void *ctx, const char *path);
    int64_t (*open_tmp)(void *ctx, int dir_fd, unsigned mode);
    int64_t (*pwrite)(void *ctx, int fd, const void *buf, size_t len, uint64_t off);
    int64_t (*ftruncate)(void *ctx, int fd, uint64_t len);
    int64_t (*fsync)(void *ctx, int fd);
    int64_t (*link_tmp)(void *ctx, int fd, int dir_fd, const char *name);
    int64_t (*close)(void *ctx, int fd);
} posix_fs_sys_t;

/* The real libc table. */
extern const posix_fs_sys_t posix_fs_sys_libc;

typedef struct {
    ports_fs_file_t id; /* 0 = free slot */
    int fd;
    uint64_t size; /* bytes stored */
    bool broken;
} posix_fs_slot_t;

/* Caller-provided storage; treat the fields as opaque. */
typedef struct {
    const posix_fs_sys_t *sys;
    void *sys_ctx;
    int dir_fd;
    bool is_open;
    uint32_t next_id;
    posix_fs_slot_t slots[POSIX_FS_SLOTS];
} posix_fs_t;

/* Open the download directory `dir_path` (absolute or relative; symlinks followed) and hold its descriptor.
 * Returns PORTS_FS_E_ARG when `fs` is NULL, `dir_path` is NULL or empty (or, for _sys, `sys` is NULL or has a NULL
 * entry), PORTS_FS_E_IO when open_dir fails (-EINTR is retried). Whenever fs != NULL it is fully initialised on
 * return: on failure it is in the CLOSED state (posix_fs_close is then a harmless no-op, port calls fail).
 * Opening an object that is already open leaks its descriptors: close it first. */
ports_fs_err_t posix_fs_open(posix_fs_t *fs, const char *dir_path);
ports_fs_err_t posix_fs_open_sys(posix_fs_t *fs, const posix_fs_sys_t *sys, void *sys_ctx, const char *dir_path);

/* Discard every live temp file (close its descriptor), close the directory descriptor, enter the CLOSED state.
 * Always releases everything and is idempotent. Returns OK when every close succeeded (-EINTR counts as success),
 * PORTS_FS_E_IO when one failed, PORTS_FS_E_ARG for NULL. A closed object: create_temp -> E_IO; write, commit,
 * discard -> E_ARG (every handle is stale); posix_fs_close -> OK and no seam call. */
ports_fs_err_t posix_fs_close(posix_fs_t *fs);

/* The port view of `fs` (valid while `fs` lives; one shared const ops table). A NULL `fs` gives a port whose four
 * operations all return PORTS_FS_E_ARG. */
ports_fs_t posix_fs_port(posix_fs_t *fs);

#endif
