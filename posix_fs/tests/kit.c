#define _GNU_SOURCE
#include "posix_fs/tests/kit.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- fake seam ---------------------------------------------------------------------------------------- */

static const char k_letters[KF_NOPS] = {'D', 'T', 'P', 'R', 'S', 'L', 'C'};

void kf_init(kf_t *f, int dir_fd, int fd_base) {
    memset(f, 0, sizeof *f);
    f->dir_fd = dir_fd;
    f->fd_base = fd_base;
    f->dir_open = false;
}

void kf_fail(kf_t *f, kf_op_t op, unsigned nth, unsigned times, int err) {
    f->fail_nth[op] = nth;
    f->fail_times[op] = times;
    f->fail_err[op] = err;
}

void kf_fail2(kf_t *f, kf_op_t op, unsigned nth, int err) {
    f->fail2_nth[op] = nth;
    f->fail2_err[op] = err;
}

/* Counts the call, logs it, and returns -errno if this call is armed to fail, else 0. */
static int64_t begin(kf_t *f, kf_op_t op) {
    f->calls[op]++;
    if (f->nlog < KF_LOG_MAX) {
        f->log[f->nlog++] = k_letters[op];
        f->log[f->nlog] = '\0';
    }
    if (f->fail_nth[op] != 0u && f->calls[op] >= f->fail_nth[op] &&
        f->calls[op] < f->fail_nth[op] + f->fail_times[op]) {
        return -(int64_t)f->fail_err[op];
    }
    if (f->fail2_nth[op] != 0u && f->calls[op] == f->fail2_nth[op]) {
        return -(int64_t)f->fail2_err[op];
    }
    return 0;
}

static kf_file_t *find_file(kf_t *f, int fd) {
    for (size_t i = 0; i < KF_FDS; i++) {
        if (f->files[i].used && f->files[i].fd == fd) {
            return &f->files[i];
        }
    }
    f->bad_calls++;
    return NULL;
}

static int64_t kf_open_dir(void *ctx, const char *path) {
    kf_t *f = ctx;
    int64_t r = begin(f, KF_OPEN_DIR);
    if (r != 0) {
        return r;
    }
    snprintf(f->last_path, sizeof f->last_path, "%s", path);
    if (strcmp(path, "/fake") != 0 && strcmp(path, "rel/fake") != 0) {
        return -ENOENT;
    }
    f->dir_open = true;
    return f->dir_fd;
}

static int64_t kf_sys_open_tmp(void *ctx, int dir_fd, unsigned mode) {
    kf_t *f = ctx;
    int64_t r = begin(f, KF_OPEN_TMP);
    if (r != 0) {
        return r;
    }
    if (dir_fd != f->dir_fd || !f->dir_open) {
        f->bad_calls++;
        return -EBADF;
    }
    f->last_mode = mode;
    for (size_t i = 0; i < KF_FDS; i++) {
        if (!f->files[i].used) {
            memset(&f->files[i], 0, sizeof f->files[i]);
            f->files[i].used = true;
            f->files[i].fd = f->fd_base + (int)i;
            return f->files[i].fd;
        }
    }
    return -EMFILE;
}

static int64_t kf_pwrite(void *ctx, int fd, const void *buf, size_t len, uint64_t off) {
    kf_t *f = ctx;
    int64_t r = begin(f, KF_PWRITE);
    f->last_off = off;
    f->last_len = len;
    f->last_fd = fd;
    if (r != 0) {
        return r;
    }
    if (f->pw_ret_nth != 0u && f->calls[KF_PWRITE] == f->pw_ret_nth) {
        return f->pw_ret_val;
    }
    if (f->sink) {
        return (int64_t)len;
    }
    kf_file_t *file = find_file(f, fd);
    if (file == NULL) {
        return -EBADF;
    }
    if (off > file->len) {
        f->holes++;
        return -EINVAL;
    }
    uint64_t n = len;
    if (f->chunk != 0u && n > f->chunk) {
        n = f->chunk;
    }
    if (n > KF_CAP - off) {
        n = (off >= KF_CAP) ? 0u : KF_CAP - off;
    }
    if (n == 0u && len > 0u) {
        return -ENOSPC;
    }
    memcpy(file->data + off, buf, (size_t)n);
    if (off + n > file->len) {
        file->len = off + n;
    }
    file->synced = false;
    return (int64_t)n;
}

static int64_t kf_ftruncate(void *ctx, int fd, uint64_t len) {
    kf_t *f = ctx;
    int64_t r = begin(f, KF_FTRUNCATE);
    f->last_len = len;
    if (r != 0) {
        return r;
    }
    kf_file_t *file = find_file(f, fd);
    if (file == NULL || len > file->len) {
        return -EINVAL;
    }
    file->len = len;
    return 0;
}

static int64_t kf_fsync(void *ctx, int fd) {
    kf_t *f = ctx;
    int64_t r = begin(f, KF_FSYNC);
    if (r != 0) {
        return r;
    }
    kf_file_t *file = find_file(f, fd);
    if (file == NULL) {
        return -EBADF;
    }
    file->synced = true;
    file->synced_len = file->len;
    return 0;
}

static int64_t kf_link(void *ctx, int fd, int dir_fd, const char *name) {
    kf_t *f = ctx;
    int64_t r = begin(f, KF_LINK);
    snprintf(f->last_name, sizeof f->last_name, "%s", name);
    if (r != 0) {
        return r;
    }
    kf_file_t *file = find_file(f, fd);
    if (file == NULL || dir_fd != f->dir_fd || !f->dir_open) {
        f->bad_calls++;
        return -EBADF;
    }
    size_t free_i = KF_ENTRIES;
    for (size_t i = 0; i < KF_ENTRIES; i++) {
        if (f->entries[i].used && strcmp(f->entries[i].name, name) == 0) {
            return -EEXIST;
        }
        if (!f->entries[i].used && free_i == KF_ENTRIES) {
            free_i = i;
        }
    }
    if (free_i == KF_ENTRIES) {
        return -ENOSPC;
    }
    kf_entry_t *e = &f->entries[free_i];
    e->used = true;
    snprintf(e->name, sizeof e->name, "%s", name);
    memcpy(e->data, file->data, (size_t)file->len);
    e->len = file->len;
    e->was_synced = file->synced && file->synced_len == file->len;
    e->mode = f->last_mode;
    return 0;
}

static int64_t kf_close(void *ctx, int fd) {
    kf_t *f = ctx;
    int64_t r = begin(f, KF_CLOSE);
    if (f->dir_open && fd == f->dir_fd) {
        f->dir_open = false; /* released even when the injected close fails, like Linux */
        return r;
    }
    kf_file_t *file = find_file(f, fd);
    if (file == NULL) {
        return -EBADF;
    }
    file->used = false;
    return r;
}

static const posix_fs_sys_t k_fake_sys = {kf_open_dir, kf_sys_open_tmp, kf_pwrite, kf_ftruncate,
                                          kf_fsync,    kf_link,     kf_close};

const posix_fs_sys_t *kf_sys(void) { return &k_fake_sys; }

bool kf_preload(kf_t *f, const char *name, const char *content, size_t len) {
    for (size_t i = 0; i < KF_ENTRIES; i++) {
        if (!f->entries[i].used && len <= KF_CAP) {
            f->entries[i].used = true;
            snprintf(f->entries[i].name, sizeof f->entries[i].name, "%s", name);
            memcpy(f->entries[i].data, content, len);
            f->entries[i].len = len;
            return true;
        }
    }
    return false;
}

size_t kf_entry_count(const kf_t *f) {
    size_t n = 0;
    for (size_t i = 0; i < KF_ENTRIES; i++) {
        n += f->entries[i].used ? 1u : 0u;
    }
    return n;
}

const kf_entry_t *kf_entry(const kf_t *f, const char *name) {
    for (size_t i = 0; i < KF_ENTRIES; i++) {
        if (f->entries[i].used && strcmp(f->entries[i].name, name) == 0) {
            return &f->entries[i];
        }
    }
    return NULL;
}

bool kf_entry_is(const kf_t *f, const char *name, const void *data, size_t len) {
    const kf_entry_t *e = kf_entry(f, name);
    return e != NULL && e->len == len && (len == 0u || memcmp(e->data, data, len) == 0);
}

size_t kf_open_tmp(const kf_t *f) {
    size_t n = 0;
    for (size_t i = 0; i < KF_FDS; i++) {
        n += f->files[i].used ? 1u : 0u;
    }
    return n;
}

size_t kf_open_fds(const kf_t *f) { return kf_open_tmp(f) + (f->dir_open ? 1u : 0u); }

const char *kf_log(const kf_t *f) { return f->log; }

void kf_log_reset(kf_t *f) {
    f->nlog = 0;
    f->log[0] = '\0';
}

void kf_open_fs(ctest_t *t, kf_t *f, posix_fs_t *fs) {
    kf_init(f, 3, 10);
    CHECK_EQ_I64(t, posix_fs_open_sys(fs, kf_sys(), f, "/fake"), PORTS_FS_OK);
    kf_log_reset(f);
    memset(f->calls, 0, sizeof f->calls);
}

/* ---- real filesystem helpers -------------------------------------------------------------------------- */

void kit_tmpdir_make(ctest_t *t, char path[KIT_PATH_MAX]) {
    const char *base = getenv("TMPDIR");
    if (base == NULL || base[0] == '\0') {
        base = ".";
    }
    int n = snprintf(path, KIT_PATH_MAX, "%s/posixfs.XXXXXX", base);
    CHECK(t, n > 0 && (size_t)n < KIT_PATH_MAX);
    CHECK(t, mkdtemp(path) != NULL);
}

size_t kit_dir_count(const char *path) {
    DIR *d = opendir(path);
    size_t n = 0;
    if (d == NULL) {
        return SIZE_MAX;
    }
    for (const struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            n++;
        }
    }
    closedir(d);
    return n;
}

static bool join(char out[KIT_PATH_MAX], const char *dir, const char *name) {
    int n = snprintf(out, KIT_PATH_MAX, "%s/%s", dir, name);
    return n > 0 && (size_t)n < KIT_PATH_MAX;
}

bool kit_dir_read(const char *dir, const char *name, unsigned char *out, size_t cap, size_t *len) {
    static char p[KIT_PATH_MAX];
    if (!join(p, dir, name)) {
        return false;
    }
    FILE *fp = fopen(p, "rb");
    if (fp == NULL) {
        return false;
    }
    size_t n = fread(out, 1, cap, fp);
    bool ok = ferror(fp) == 0 && fgetc(fp) == EOF; /* nothing beyond `cap` bytes */
    fclose(fp);
    *len = n;
    return ok;
}

bool kit_dir_put(const char *dir, const char *name, const void *data, size_t len) {
    static char p[KIT_PATH_MAX];
    if (!join(p, dir, name)) {
        return false;
    }
    FILE *fp = fopen(p, "wb");
    if (fp == NULL) {
        return false;
    }
    bool ok = len == 0u || fwrite(data, 1, len, fp) == len;
    return fclose(fp) == 0 && ok;
}

size_t kit_tmpdir_remove(ctest_t *t, const char *path) {
    size_t removed = 0;
    DIR *d = opendir(path);
    CHECK(t, d != NULL);
    if (d != NULL) {
        for (const struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
            static char p[KIT_PATH_MAX];
            if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0 && join(p, path, e->d_name)) {
                CHECK_EQ_I64(t, unlink(p), 0);
                removed++;
            }
        }
        closedir(d);
    }
    CHECK_EQ_I64(t, rmdir(path), 0);
    return removed;
}

size_t kit_fd_count(void) {
    DIR *d = opendir("/proc/self/fd");
    size_t n = 0;
    if (d == NULL) {
        return SIZE_MAX;
    }
    for (const struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            n++;
        }
    }
    closedir(d);
    return n;
}

/* ---- contract environments ---------------------------------------------------------------------------- */

static size_t fake_count(kit_fs_env_t *e) { return kf_entry_count(e->kf); }

static bool fake_read(kit_fs_env_t *e, const char *name, unsigned char *out, size_t cap, size_t *len) {
    const kf_entry_t *en = kf_entry(e->kf, name);
    if (en == NULL || en->len > cap) {
        return false;
    }
    memcpy(out, en->data, (size_t)en->len);
    *len = (size_t)en->len;
    return true;
}

static bool fake_put(kit_fs_env_t *e, const char *name, const void *data, size_t len) {
    return kf_preload(e->kf, name, data, len);
}

static size_t real_count(kit_fs_env_t *e) { return kit_dir_count(e->dir); }

static bool real_read(kit_fs_env_t *e, const char *name, unsigned char *out, size_t cap, size_t *len) {
    return kit_dir_read(e->dir, name, out, cap, len);
}

static bool real_put(kit_fs_env_t *e, const char *name, const void *data, size_t len) {
    return kit_dir_put(e->dir, name, data, len);
}

void kit_env_begin_fake(ctest_t *t, kit_fs_env_t *e, kf_t *backing) {
    memset(e, 0, sizeof *e);
    e->kf = backing;
    e->count = fake_count;
    e->read = fake_read;
    e->put = fake_put;
    kf_open_fs(t, backing, &e->pfs);
    e->fs = posix_fs_port(&e->pfs);
}

void kit_env_begin_real(ctest_t *t, kit_fs_env_t *e) {
    memset(e, 0, sizeof *e);
    e->count = real_count;
    e->read = real_read;
    e->put = real_put;
    kit_tmpdir_make(t, e->dir);
    e->fds_before = kit_fd_count();
    CHECK_EQ_I64(t, posix_fs_open(&e->pfs, e->dir), PORTS_FS_OK);
    e->fs = posix_fs_port(&e->pfs);
}

void kit_env_end(ctest_t *t, kit_fs_env_t *e) {
    CHECK_EQ_I64(t, posix_fs_close(&e->pfs), PORTS_FS_OK);
    if (e->kf != NULL) {
        CHECK_EQ_U64(t, kf_open_fds(e->kf), 0);
        CHECK_EQ_U64(t, e->kf->bad_calls, 0);
        CHECK_EQ_U64(t, e->kf->holes, 0);
    } else {
        CHECK_EQ_U64(t, kit_fd_count(), e->fds_before);
        (void)kit_tmpdir_remove(t, e->dir);
        struct stat st;
        CHECK(t, stat(e->dir, &st) != 0); /* nothing is left behind */
    }
}
