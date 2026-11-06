#include <errno.h>

#include "posix_fs/src/internal.h"

#define NAME_MAX_LEN 255u

int64_t pfs_pwrite(const posix_fs_t *fs, int fd, const void *buf, size_t len, uint64_t off) {
    int64_t rc;
    do {
        rc = fs->sys->pwrite(fs->sys_ctx, fd, buf, len, off);
    } while (rc == -EINTR);
    return rc;
}

int64_t pfs_ftruncate(const posix_fs_t *fs, int fd, uint64_t len) {
    int64_t rc;
    do {
        rc = fs->sys->ftruncate(fs->sys_ctx, fd, len);
    } while (rc == -EINTR);
    return rc;
}

int64_t pfs_fsync(const posix_fs_t *fs, int fd) {
    int64_t rc;
    do {
        rc = fs->sys->fsync(fs->sys_ctx, fd);
    } while (rc == -EINTR);
    return rc;
}

int64_t pfs_link_tmp(const posix_fs_t *fs, int fd, const char *name) {
    int64_t rc;
    do {
        rc = fs->sys->link_tmp(fs->sys_ctx, fd, fs->dir_fd, name);
    } while (rc == -EINTR);
    return rc;
}

int64_t pfs_open_tmp(const posix_fs_t *fs) {
    int64_t rc;
    do {
        rc = fs->sys->open_tmp(fs->sys_ctx, fs->dir_fd, POSIX_FS_FILE_MODE);
    } while (rc == -EINTR);
    return rc;
}

ports_fs_err_t pfs_map_err(int64_t rc, bool link) {
    int64_t e = -rc;
    if (e == ENOSPC || e == EDQUOT || e == EFBIG) {
        return PORTS_FS_E_NOSPACE;
    }
    if (link && e == EEXIST) {
        return PORTS_FS_E_EXISTS;
    }
    return PORTS_FS_E_IO;
}

/* Append all len bytes at slot->size. On failure *written is the count that reached the file. */
static ports_fs_err_t write_all(const posix_fs_t *fs, const posix_fs_slot_t *slot, const char *buf, size_t len,
                                size_t *written) {
    size_t done = 0;
    while (done < len) {
        int64_t rc = pfs_pwrite(fs, slot->fd, buf + done, len - done, slot->size + done);
        *written = done;
        if (rc < 0) {
            return pfs_map_err(rc, false);
        }
        if (rc == 0 || (uint64_t)rc > len - done) {
            return PORTS_FS_E_IO;
        }
        done += (size_t)rc;
    }
    *written = done;
    return PORTS_FS_OK;
}

ports_fs_err_t posix_fs_write(posix_fs_t *fs, ports_fs_file_t file, const void *buf, size_t len) {
    posix_fs_slot_t *slot = pfs_find(fs, file);
    if (slot == NULL || (buf == NULL && len > 0)) {
        return PORTS_FS_E_ARG;
    }
    if (len == 0) {
        return PORTS_FS_OK;
    }
    if (slot->broken) {
        return PORTS_FS_E_IO;
    }
    if (slot->size > (uint64_t)INT64_MAX || len > (uint64_t)INT64_MAX - slot->size) {
        return PORTS_FS_E_NOSPACE;
    }
    size_t written = 0;
    ports_fs_err_t err = write_all(fs, slot, (const char *)buf, len, &written);
    if (err == PORTS_FS_OK) {
        slot->size += len;
        return PORTS_FS_OK;
    }
    if (written > 0 && pfs_ftruncate(fs, slot->fd, slot->size) < 0) {
        slot->broken = true;
    }
    return err;
}

static bool name_valid(core_str_t name) {
    if (name.len < 1u || name.len > NAME_MAX_LEN || name.ptr == NULL) {
        return false;
    }
    for (size_t i = 0; i < name.len; i++) {
        if (name.ptr[i] == '/' || name.ptr[i] == '\0') {
            return false;
        }
    }
    if (name.ptr[0] == '.' && (name.len == 1u || (name.len == 2u && name.ptr[1] == '.'))) {
        return false;
    }
    return true;
}

ports_fs_err_t posix_fs_commit(posix_fs_t *fs, ports_fs_file_t file, core_str_t final_name) {
    posix_fs_slot_t *slot = pfs_find(fs, file);
    if (slot == NULL || !name_valid(final_name)) {
        return PORTS_FS_E_ARG;
    }
    if (slot->broken) {
        return PORTS_FS_E_IO;
    }
    char name[NAME_MAX_LEN + 1u];
    for (size_t i = 0; i < final_name.len; i++) {
        name[i] = final_name.ptr[i];
    }
    name[final_name.len] = '\0';
    int64_t rc = pfs_fsync(fs, slot->fd);
    if (rc < 0) {
        return pfs_map_err(rc, false);
    }
    rc = pfs_link_tmp(fs, slot->fd, name);
    if (rc < 0) {
        return pfs_map_err(rc, true);
    }
    (void)pfs_release(fs, slot); /* the name is visible: the commit succeeded whatever close says */
    return PORTS_FS_OK;
}
