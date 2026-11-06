#include "posix_fs/src/internal.h"

/* STUB (test-author task A016): every call fails. */
ports_fs_err_t posix_fs_open(posix_fs_t *fs, const char *dir_path) {
    (void)fs;
    (void)dir_path;
    return PORTS_FS_E_IO;
}

ports_fs_err_t posix_fs_open_sys(posix_fs_t *fs, const posix_fs_sys_t *sys, void *sys_ctx, const char *dir_path) {
    (void)fs;
    (void)sys;
    (void)sys_ctx;
    (void)dir_path;
    return PORTS_FS_E_IO;
}

ports_fs_err_t posix_fs_close(posix_fs_t *fs) {
    (void)fs;
    return PORTS_FS_E_IO;
}

ports_fs_t posix_fs_port(posix_fs_t *fs) {
    ports_fs_t p = {NULL, fs};
    return p;
}
