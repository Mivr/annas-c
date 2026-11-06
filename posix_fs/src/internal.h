/* posix_fs/src/internal.h: private declarations shared by fs.c, io.c and sys.c (implementer-owned). */
#ifndef POSIX_FS_SRC_INTERNAL_H
#define POSIX_FS_SRC_INTERNAL_H

#include "posix_fs/posix_fs.h"

/* Seam calls with the -EINTR retry (every op except close). */
int64_t pfs_pwrite(const posix_fs_t *fs, int fd, const void *buf, size_t len, uint64_t off);
int64_t pfs_ftruncate(const posix_fs_t *fs, int fd, uint64_t len);
int64_t pfs_fsync(const posix_fs_t *fs, int fd);
int64_t pfs_link_tmp(const posix_fs_t *fs, int fd, const char *name);
int64_t pfs_open_tmp(const posix_fs_t *fs);

/* Map a negative seam result to a port error; `link` enables EEXIST -> PORTS_FS_E_EXISTS. */
ports_fs_err_t pfs_map_err(int64_t rc, bool link);

/* The live slot holding `file`, or NULL (also for a NULL `fs`, a closed object or handle 0). */
posix_fs_slot_t *pfs_find(posix_fs_t *fs, ports_fs_file_t file);

/* Close a slot's descriptor once and free the slot; true iff close did not fail (-EINTR counts as success). */
bool pfs_release(posix_fs_t *fs, posix_fs_slot_t *slot);

/* Port operations implemented in io.c. */
ports_fs_err_t pfs_op_write(void *ctx, ports_fs_file_t file, const void *buf, size_t len);
ports_fs_err_t pfs_op_commit(void *ctx, ports_fs_file_t file, core_str_t final_name);

#endif
