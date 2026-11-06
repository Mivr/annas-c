/* posix_fs: the direct entry points (posix_fs_create_temp/write/commit/discard) over the fake seam. */
#define _GNU_SOURCE
#include <errno.h>
#include <string.h>

#include "posix_fs/posix_fs.h"
#include "posix_fs/tests/kit.h"
#include "ports/ports.h"

static kf_t g_kf;
static posix_fs_t g_fs;

static void t_direct_create_write_commit(ctest_t *t) {
    ports_fs_file_t f = 0;
    kf_init(&g_kf, 3, 10);
    kf_open_fs(t, &g_kf, &g_fs);
    CHECK_EQ_I64(t, posix_fs_create_temp(&g_fs, &f), PORTS_FS_OK);
    CHECK(t, f != PORTS_FS_NO_FILE);
    CHECK_EQ_I64(t, posix_fs_write(&g_fs, f, "abc", 3), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_commit(&g_fs, f, (core_str_t){"out", 3}), PORTS_FS_OK);
    CHECK(t, kf_entry_is(&g_kf, "out", "abc", 3));
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

static void t_direct_create_errors(ctest_t *t) {
    ports_fs_file_t f = 77;
    kf_init(&g_kf, 3, 10);
    kf_open_fs(t, &g_kf, &g_fs);
    CHECK_EQ_I64(t, posix_fs_create_temp(NULL, &f), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_create_temp(&g_fs, NULL), PORTS_FS_E_ARG);
    kf_fail(&g_kf, KF_OPEN_TMP, 1, 1, ENOSPC);
    CHECK_EQ_I64(t, posix_fs_create_temp(&g_fs, &f), PORTS_FS_E_NOSPACE);
    CHECK_EQ_U64(t, f, 77);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

static void t_direct_write_errors(ctest_t *t) {
    ports_fs_file_t f = 0;
    kf_init(&g_kf, 3, 10);
    kf_open_fs(t, &g_kf, &g_fs);
    CHECK_EQ_I64(t, posix_fs_write(NULL, 1, "a", 1), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_write(&g_fs, 99, "a", 1), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_create_temp(&g_fs, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_write(&g_fs, f, NULL, 1), PORTS_FS_E_ARG);
    kf_fail(&g_kf, KF_PWRITE, 1, 1, EIO);
    CHECK_EQ_I64(t, posix_fs_write(&g_fs, f, "a", 1), PORTS_FS_E_IO);
    CHECK_EQ_I64(t, posix_fs_discard(&g_fs, f), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

static void t_direct_commit_errors(ctest_t *t) {
    ports_fs_file_t f = 0;
    kf_init(&g_kf, 3, 10);
    kf_open_fs(t, &g_kf, &g_fs);
    CHECK_EQ_I64(t, posix_fs_commit(NULL, 1, (core_str_t){"x", 1}), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_create_temp(&g_fs, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_commit(&g_fs, f, (core_str_t){"a/b", 3}), PORTS_FS_E_ARG);
    CHECK(t, kf_preload(&g_kf, "taken", "z", 1));
    CHECK_EQ_I64(t, posix_fs_commit(&g_fs, f, (core_str_t){"taken", 5}), PORTS_FS_E_EXISTS);
    CHECK_EQ_I64(t, posix_fs_discard(&g_fs, f), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

static void t_direct_discard(ctest_t *t) {
    ports_fs_file_t f = 0;
    kf_init(&g_kf, 3, 10);
    kf_open_fs(t, &g_kf, &g_fs);
    CHECK_EQ_I64(t, posix_fs_discard(NULL, 1), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_discard(&g_fs, 99), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_create_temp(&g_fs, &f), PORTS_FS_OK);
    CHECK_EQ_U64(t, kf_open_tmp(&g_kf), 1);
    CHECK_EQ_I64(t, posix_fs_discard(&g_fs, f), PORTS_FS_OK);
    CHECK_EQ_U64(t, kf_open_tmp(&g_kf), 0);
    CHECK_EQ_I64(t, posix_fs_discard(&g_fs, f), PORTS_FS_E_ARG); /* consumed */
    CHECK_EQ_I64(t, posix_fs_create_temp(&g_fs, &f), PORTS_FS_OK);
    kf_fail(&g_kf, KF_CLOSE, 2, 1, EIO); /* the first discard above was close call 1 */
    CHECK_EQ_I64(t, posix_fs_discard(&g_fs, f), PORTS_FS_E_IO);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

CTEST_SUITE("posix_fs_direct",
    {"direct_create_write_commit", t_direct_create_write_commit},
    {"direct_create_errors", t_direct_create_errors},
    {"direct_write_errors", t_direct_write_errors},
    {"direct_commit_errors", t_direct_commit_errors},
    {"direct_discard", t_direct_discard})
