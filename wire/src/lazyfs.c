/* wire/src/lazyfs.c: the download directory as a ports_fs_t, opened at the first create_temp of the run. */
#include "wire/src/internal.h"

/* The layered ANNAS_DOWNLOAD_PATH as a C string in l->path; false when it is unset, empty, longer than ANNAS_DIR_MAX
 * or holds a NUL byte. */
static bool path_load(wire_lazyfs_t *l)
{
    core_str_t v;
    bool ok = wire_env_get(&l->env, "ANNAS_DOWNLOAD_PATH", &v) && v.ptr != NULL && v.len > 0 && v.len <= ANNAS_DIR_MAX;
    size_t i = 0;
    for (; ok && i < v.len; i++) {
        ok = v.ptr[i] != '\0';
        l->path[i] = v.ptr[i];
    }
    l->path[i] = '\0'; /* i <= ANNAS_DIR_MAX; the string is only used when ok */
    return ok;
}

/* Opens the directory once; a directory that is not open leaves l->fs closed, where create_temp is PORTS_FS_E_IO. */
static void lazy_open(wire_lazyfs_t *l)
{
    if (!l->open) {
        l->open = path_load(l) && posix_fs_open_sys(&l->fs, l->sys, l->sys_ctx, l->path) == PORTS_FS_OK;
    }
}

static ports_fs_err_t lz_create_temp(void *ctx, ports_fs_file_t *file)
{
    wire_lazyfs_t *l = ctx;
    lazy_open(l);
    return posix_fs_create_temp(&l->fs, file);
}

/* write, commit and discard only get the handles that create_temp returned, so the directory is open. */
static ports_fs_err_t lz_write(void *ctx, ports_fs_file_t file, const void *buf, size_t len)
{
    wire_lazyfs_t *l = ctx;
    return posix_fs_write(&l->fs, file, buf, len);
}

static ports_fs_err_t lz_commit(void *ctx, ports_fs_file_t file, core_str_t final_name)
{
    wire_lazyfs_t *l = ctx;
    return posix_fs_commit(&l->fs, file, final_name);
}

static ports_fs_err_t lz_discard(void *ctx, ports_fs_file_t file)
{
    wire_lazyfs_t *l = ctx;
    return posix_fs_discard(&l->fs, file);
}

static const ports_fs_ops_t k_lazy_ops = {lz_create_temp, lz_write, lz_commit, lz_discard};

void wire_lazyfs_init(wire_lazyfs_t *l, const wire_io_t *io, ports_env_t env)
{
    l->open = false;
    l->sys = io->fs_sys != NULL ? io->fs_sys : &posix_fs_sys_libc;
    l->sys_ctx = io->fs_sys_ctx;
    l->env = env;
    /* A NULL path is PORTS_FS_E_ARG, and posix_fs leaves l->fs fully initialised and CLOSED, where create_temp is
     * PORTS_FS_E_IO and write, commit and discard are PORTS_FS_E_ARG, whatever m held on entry. */
    (void)posix_fs_open_sys(&l->fs, l->sys, l->sys_ctx, NULL);
}

ports_fs_t wire_lazyfs_port(wire_lazyfs_t *l)
{
    ports_fs_t p = {&k_lazy_ops, l};
    return p;
}

void wire_lazyfs_close(wire_lazyfs_t *l)
{
    if (l->open) {
        l->open = false;
        (void)posix_fs_close(&l->fs); /* released whatever it returns; nothing more to do at the end of a run */
    }
}
