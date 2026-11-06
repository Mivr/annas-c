/* wire/src/lazyfs.c: the download directory as a ports_fs_t, opened at the first create_temp of the run. */
#include "wire/src/internal.h"

static bool lazy_open(wire_lazyfs_t *l)
{
    if (l->open) {
        return true;
    }
    core_str_t v = {NULL, 0};
    if (!l->env.ops->get(l->env.ctx, core_str_from_cstr("ANNAS_DOWNLOAD_PATH"), &v) || v.ptr == NULL ||
        v.len == 0 || v.len > ANNAS_DIR_MAX) {
        return false;
    }
    for (size_t i = 0; i < v.len; i++) {
        if (v.ptr[i] == '\0') {
            return false;
        }
        l->path[i] = v.ptr[i];
    }
    l->path[v.len] = '\0';
    if (posix_fs_open_sys(&l->fs, l->sys, l->sys_ctx, l->path) != PORTS_FS_OK) {
        return false;
    }
    l->open = true;
    return true;
}

static ports_fs_err_t lz_create_temp(void *ctx, ports_fs_file_t *file)
{
    wire_lazyfs_t *l = ctx;
    if (!lazy_open(l)) {
        return PORTS_FS_E_IO;
    }
    ports_fs_t p = posix_fs_port(&l->fs);
    return p.ops->create_temp(p.ctx, file);
}

static ports_fs_err_t lz_write(void *ctx, ports_fs_file_t file, const void *buf, size_t len)
{
    wire_lazyfs_t *l = ctx;
    if (!l->open) {
        return PORTS_FS_E_ARG;
    }
    ports_fs_t p = posix_fs_port(&l->fs);
    return p.ops->write(p.ctx, file, buf, len);
}

static ports_fs_err_t lz_commit(void *ctx, ports_fs_file_t file, core_str_t final_name)
{
    wire_lazyfs_t *l = ctx;
    if (!l->open) {
        return PORTS_FS_E_ARG;
    }
    ports_fs_t p = posix_fs_port(&l->fs);
    return p.ops->commit(p.ctx, file, final_name);
}

static ports_fs_err_t lz_discard(void *ctx, ports_fs_file_t file)
{
    wire_lazyfs_t *l = ctx;
    if (!l->open) {
        return PORTS_FS_E_ARG;
    }
    ports_fs_t p = posix_fs_port(&l->fs);
    return p.ops->discard(p.ctx, file);
}

static const ports_fs_ops_t k_lazy_ops = {lz_create_temp, lz_write, lz_commit, lz_discard};

void wire_lazyfs_init(wire_lazyfs_t *l, const wire_io_t *io, ports_env_t env)
{
    l->open = false;
    l->sys = io->fs_sys != NULL ? io->fs_sys : &posix_fs_sys_libc;
    l->sys_ctx = io->fs_sys_ctx;
    l->env = env;
    l->path[0] = '\0';
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
