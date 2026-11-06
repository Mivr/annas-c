#include <errno.h>
#include <string.h>

#include "posix_fs/src/internal.h"

posix_fs_slot_t *pfs_find(posix_fs_t *fs, ports_fs_file_t file) {
    if (fs == NULL || !fs->is_open || file == PORTS_FS_NO_FILE) {
        return NULL;
    }
    for (size_t i = 0; i < POSIX_FS_SLOTS; i++) {
        if (fs->slots[i].id == file) {
            return &fs->slots[i];
        }
    }
    return NULL;
}

bool pfs_release(posix_fs_t *fs, posix_fs_slot_t *slot) {
    int64_t rc = fs->sys->close(fs->sys_ctx, slot->fd);
    memset(slot, 0, sizeof *slot);
    return rc >= 0 || rc == -EINTR;
}

static void reset_closed(posix_fs_t *fs, const posix_fs_sys_t *sys, void *sys_ctx) {
    memset(fs, 0, sizeof *fs);
    fs->sys = sys;
    fs->sys_ctx = sys_ctx;
    fs->dir_fd = -1;
    fs->next_id = 1u;
}

static bool sys_complete(const posix_fs_sys_t *s) {
    return s != NULL && s->open_dir != NULL && s->open_tmp != NULL && s->pwrite != NULL && s->ftruncate != NULL &&
           s->fsync != NULL && s->link_tmp != NULL && s->close != NULL;
}

ports_fs_err_t posix_fs_open_sys(posix_fs_t *fs, const posix_fs_sys_t *sys, void *sys_ctx, const char *dir_path) {
    if (fs == NULL) {
        return PORTS_FS_E_ARG;
    }
    reset_closed(fs, sys, sys_ctx);
    if (!sys_complete(sys) || dir_path == NULL || dir_path[0] == '\0') {
        return PORTS_FS_E_ARG;
    }
    int64_t rc;
    do {
        rc = sys->open_dir(sys_ctx, dir_path);
    } while (rc == -EINTR);
    if (rc < 0) {
        return PORTS_FS_E_IO;
    }
    fs->dir_fd = (int)rc;
    fs->is_open = true;
    return PORTS_FS_OK;
}

ports_fs_err_t posix_fs_open(posix_fs_t *fs, const char *dir_path) {
    return posix_fs_open_sys(fs, &posix_fs_sys_libc, NULL, dir_path);
}

ports_fs_err_t posix_fs_close(posix_fs_t *fs) {
    if (fs == NULL) {
        return PORTS_FS_E_ARG;
    }
    if (!fs->is_open) {
        return PORTS_FS_OK;
    }
    bool ok = true;
    for (size_t i = 0; i < POSIX_FS_SLOTS; i++) {
        if (fs->slots[i].id != PORTS_FS_NO_FILE) {
            ok = pfs_release(fs, &fs->slots[i]) && ok;
        }
    }
    int64_t rc = fs->sys->close(fs->sys_ctx, fs->dir_fd);
    ok = (rc >= 0 || rc == -EINTR) && ok;
    fs->dir_fd = -1;
    fs->is_open = false;
    return ok ? PORTS_FS_OK : PORTS_FS_E_IO;
}

static posix_fs_slot_t *free_slot(posix_fs_t *fs) {
    for (size_t i = 0; i < POSIX_FS_SLOTS; i++) {
        if (fs->slots[i].id == PORTS_FS_NO_FILE) {
            return &fs->slots[i];
        }
    }
    return NULL;
}

ports_fs_err_t posix_fs_create_temp(posix_fs_t *fs, ports_fs_file_t *file) {
    if (fs == NULL || file == NULL) {
        return PORTS_FS_E_ARG;
    }
    if (!fs->is_open) {
        return PORTS_FS_E_IO;
    }
    posix_fs_slot_t *slot = free_slot(fs);
    if (slot == NULL) {
        return PORTS_FS_E_NOSPACE;
    }
    int64_t rc = pfs_open_tmp(fs);
    if (rc < 0) {
        return pfs_map_err(rc, false);
    }
    slot->id = fs->next_id;
    slot->fd = (int)rc;
    slot->size = 0;
    slot->broken = false;
    fs->next_id = fs->next_id + 1u == 0u ? 1u : fs->next_id + 1u;
    *file = slot->id;
    return PORTS_FS_OK;
}

ports_fs_err_t posix_fs_discard(posix_fs_t *fs, ports_fs_file_t file) {
    posix_fs_slot_t *slot = pfs_find(fs, file);
    if (slot == NULL) {
        return PORTS_FS_E_ARG;
    }
    return pfs_release(fs, slot) ? PORTS_FS_OK : PORTS_FS_E_IO;
}

static ports_fs_err_t op_create_temp(void *ctx, ports_fs_file_t *file) {
    return posix_fs_create_temp((posix_fs_t *)ctx, file);
}

static ports_fs_err_t op_write(void *ctx, ports_fs_file_t file, const void *buf, size_t len) {
    return posix_fs_write((posix_fs_t *)ctx, file, buf, len);
}

static ports_fs_err_t op_commit(void *ctx, ports_fs_file_t file, core_str_t final_name) {
    return posix_fs_commit((posix_fs_t *)ctx, file, final_name);
}

static ports_fs_err_t op_discard(void *ctx, ports_fs_file_t file) {
    return posix_fs_discard((posix_fs_t *)ctx, file);
}

static const ports_fs_ops_t k_ops = {op_create_temp, op_write, op_commit, op_discard};

ports_fs_t posix_fs_port(posix_fs_t *fs) {
    ports_fs_t p = {&k_ops, fs};
    return p;
}
