/* posix_fs/tests/kit.h: test-only scaffolding for the posix_fs tests (not part of the product).
 *  - kf_t: a fault-injecting, in-memory fake of posix_fs_sys_t that models descriptors, temp files and ONE
 *    directory, and records the sequence of seam calls.
 *  - kit_fs_env_t: one view of "an fs under test" that the shared port-contract cases run against, either over
 *    the fake or over the real table in a fresh directory under $TMPDIR (cwd when unset).
 * Talks to the product only through posix_fs/posix_fs.h and ports/ports.h. */
#ifndef POSIX_FS_TESTS_KIT_H
#define POSIX_FS_TESTS_KIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ctest/ctest.h"
#include "posix_fs/posix_fs.h"
#include "ports/ports.h"

/* ---- fake seam ---------------------------------------------------------------------------------------- */

typedef enum {
    KF_OPEN_DIR = 0,
    KF_OPEN_TMP,
    KF_PWRITE,
    KF_FTRUNCATE,
    KF_FSYNC,
    KF_LINK,
    KF_CLOSE,
    KF_NOPS
} kf_op_t;

#define KF_FDS 12u
#define KF_ENTRIES 16u
#define KF_CAP 8192u
#define KF_LOG_MAX 255u

typedef struct {
    bool used;
    int fd;
    unsigned char data[KF_CAP];
    uint64_t len;
    bool synced;
    uint64_t synced_len;
} kf_file_t;

typedef struct {
    bool used;
    char name[256];
    unsigned char data[KF_CAP];
    uint64_t len;
    bool was_synced; /* the data was fsynced in full before the link */
    unsigned mode;
} kf_entry_t;

typedef struct {
    int dir_fd;
    int fd_base;
    bool dir_open;
    kf_file_t files[KF_FDS];
    kf_entry_t entries[KF_ENTRIES];
    unsigned calls[KF_NOPS];
    unsigned fail_nth[KF_NOPS];   /* 1-based call number of the first injected failure, 0 = none */
    unsigned fail_times[KF_NOPS]; /* how many consecutive calls fail (>= 1 when armed) */
    int fail_err[KF_NOPS];        /* positive errno */
    unsigned fail2_nth[KF_NOPS];  /* one more single failing call number, 0 = none */
    int fail2_err[KF_NOPS];
    unsigned chunk;               /* pwrite stores at most this many bytes per call (0 = no limit) */
    unsigned pw_ret_nth;          /* pwrite call number whose return value is replaced, 0 = none */
    int64_t pw_ret_val;
    bool sink;                    /* pwrite stores nothing and returns len (huge-size tests) */
    unsigned holes;               /* pwrite at an offset beyond the file length */
    unsigned bad_calls;           /* seam call on a descriptor that is not open */
    uint64_t last_off;
    uint64_t last_len;
    unsigned last_mode;
    int last_fd;
    char last_name[256];
    char last_path[64];
    char log[KF_LOG_MAX + 1u]; /* one letter per seam call: D T P R S L C */
    size_t nlog;
} kf_t;

void kf_init(kf_t *f, int dir_fd, int fd_base);
const posix_fs_sys_t *kf_sys(void);
/* Make calls nth .. nth+times-1 of `op` fail with -err. */
void kf_fail(kf_t *f, kf_op_t op, unsigned nth, unsigned times, int err);
/* Additionally make call number `nth` of `op` fail with -err. */
void kf_fail2(kf_t *f, kf_op_t op, unsigned nth, int err);
bool kf_preload(kf_t *f, const char *name, const char *content, size_t len);
size_t kf_entry_count(const kf_t *f);
const kf_entry_t *kf_entry(const kf_t *f, const char *name);
size_t kf_open_fds(const kf_t *f); /* open temp descriptors + the directory descriptor if open */
size_t kf_open_tmp(const kf_t *f); /* open temp descriptors only */
const char *kf_log(const kf_t *f);
void kf_log_reset(kf_t *f);
/* Start an fs over the fake (dir_fd 3, fd_base 10); CHECKs it succeeded. */
void kf_open_fs(ctest_t *t, kf_t *f, posix_fs_t *fs);
/* Memory-compare helper: entry `name` exists with exactly these bytes. */
bool kf_entry_is(const kf_t *f, const char *name, const void *data, size_t len);

/* ---- real filesystem helpers -------------------------------------------------------------------------- */

#define KIT_PATH_MAX 1024u
/* mkdtemp under $TMPDIR (or "."). CHECKs success. */
void kit_tmpdir_make(ctest_t *t, char path[KIT_PATH_MAX]);
/* Number of directory entries other than "." and "..", or SIZE_MAX if it cannot be listed. */
size_t kit_dir_count(const char *path);
/* Read file `dir/name` into out (cap bytes). Returns false if absent or too big. */
bool kit_dir_read(const char *dir, const char *name, unsigned char *out, size_t cap, size_t *len);
/* Create `dir/name` with content. */
bool kit_dir_put(const char *dir, const char *name, const void *data, size_t len);
/* Remove every entry then the directory; CHECKs both. Returns how many entries had to be removed. */
size_t kit_tmpdir_remove(ctest_t *t, const char *path);
/* Number of open file descriptors of this process (counted via /proc/self/fd), SIZE_MAX on error. */
size_t kit_fd_count(void);

/* ---- one view over "an fs under test" for the contract cases ------------------------------------------ */

typedef struct kit_fs_env kit_fs_env_t;
struct kit_fs_env {
    ports_fs_t fs;
    size_t (*count)(kit_fs_env_t *e);                       /* visible files in the directory */
    bool (*read)(kit_fs_env_t *e, const char *name, unsigned char *out, size_t cap, size_t *len);
    bool (*put)(kit_fs_env_t *e, const char *name, const void *data, size_t len); /* pre-existing file */
    posix_fs_t pfs;
    kf_t *kf;       /* fake backend, or NULL */
    char dir[KIT_PATH_MAX];
    size_t fds_before;
};

void kit_env_begin_fake(ctest_t *t, kit_fs_env_t *e, kf_t *backing);
void kit_env_begin_real(ctest_t *t, kit_fs_env_t *e);
/* posix_fs_close, then: no leaked descriptors, real directory removed and empty of surprises. */
void kit_env_end(ctest_t *t, kit_fs_env_t *e);

#endif
