/* posix_fs: A068, inputs that tell apart small defects of the final-name rule and of the libc table (posix_fs.h,
 * ports.h). Real descriptors under $TMPDIR; every test removes what it made. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "posix_fs/posix_fs.h"
#include "posix_fs/tests/kit.h"
#include "ports/ports.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static posix_fs_t g_fs;

static void t_dot_names_are_refused_but_dot_prefixed_names_are_not(ctest_t *t) {
    static char dir[KIT_PATH_MAX];
    ports_fs_file_t f = 0;
    kit_tmpdir_make(t, dir);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, dir), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_create_temp(&g_fs, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_commit(&g_fs, f, LIT(".")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_commit(&g_fs, f, LIT("..")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_commit(&g_fs, f, LIT("...")), PORTS_FS_OK); /* the handle stayed valid */
    CHECK_EQ_I64(t, posix_fs_create_temp(&g_fs, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_commit(&g_fs, f, LIT(".x")), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_create_temp(&g_fs, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_commit(&g_fs, f, LIT("..x")), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, kit_tmpdir_remove(t, dir), 3);
}

static void t_open_dir_returns_descriptor_zero_as_zero(ctest_t *t) {
    static char dir[KIT_PATH_MAX];
    kit_tmpdir_make(t, dir);
    CHECK_EQ_I64(t, close(0), 0);
    errno = ENOENT; /* a stale errno must not leak into a successful result */
    CHECK_EQ_I64(t, posix_fs_sys_libc.open_dir(NULL, dir), 0);
    CHECK_EQ_I64(t, posix_fs_sys_libc.close(NULL, 0), 0);
    CHECK_EQ_U64(t, kit_tmpdir_remove(t, dir), 0);
}

static void t_open_tmp_returns_descriptor_zero_as_zero(ctest_t *t) {
    static char dir[KIT_PATH_MAX];
    kit_tmpdir_make(t, dir);
    int64_t d = posix_fs_sys_libc.open_dir(NULL, dir);
    CHECK(t, d > 0);
    CHECK_EQ_I64(t, close(0), 0);
    errno = ENOENT;
    CHECK_EQ_I64(t, posix_fs_sys_libc.open_tmp(NULL, (int)d, 0644u), 0);
    CHECK_EQ_I64(t, posix_fs_sys_libc.close(NULL, 0), 0);
    CHECK_EQ_I64(t, posix_fs_sys_libc.close(NULL, (int)d), 0);
    CHECK_EQ_U64(t, kit_tmpdir_remove(t, dir), 0);
}

static void t_libc_calls_on_a_real_file_return_exactly_zero(ctest_t *t) {
    static char dir[KIT_PATH_MAX];
    static unsigned char got[8];
    size_t len = 0;
    const posix_fs_sys_t *s = &posix_fs_sys_libc;
    kit_tmpdir_make(t, dir);
    int64_t d = s->open_dir(NULL, dir);
    CHECK(t, d >= 0);
    int64_t fd = s->open_tmp(NULL, (int)d, 0644u);
    CHECK(t, fd >= 0);
    CHECK_EQ_I64(t, s->pwrite(NULL, (int)fd, "abc", 0, 0), 0);
    CHECK_EQ_I64(t, s->pwrite(NULL, (int)fd, "abcdef", 6, 0), 6);
    CHECK_EQ_I64(t, s->ftruncate(NULL, (int)fd, 3), 0);
    CHECK_EQ_I64(t, s->fsync(NULL, (int)fd), 0);
    CHECK_EQ_I64(t, s->link_tmp(NULL, (int)fd, (int)d, "named"), 0);
    CHECK_EQ_I64(t, s->link_tmp(NULL, (int)fd, (int)d, "named"), -EEXIST);
    CHECK_EQ_I64(t, s->close(NULL, (int)fd), 0);
    CHECK_EQ_I64(t, s->close(NULL, (int)d), 0);
    CHECK(t, kit_dir_read(dir, "named", got, sizeof got, &len));
    CHECK_EQ_MEM(t, got, len, "abc", 3);
    CHECK_EQ_U64(t, kit_tmpdir_remove(t, dir), 1);
}

CTEST_SUITE("posix_fs_a068",
    {"dot_names_are_refused_but_dot_prefixed_names_are_not", t_dot_names_are_refused_but_dot_prefixed_names_are_not},
    {"open_dir_returns_descriptor_zero_as_zero", t_open_dir_returns_descriptor_zero_as_zero},
    {"open_tmp_returns_descriptor_zero_as_zero", t_open_tmp_returns_descriptor_zero_as_zero},
    {"libc_calls_on_a_real_file_return_exactly_zero", t_libc_calls_on_a_real_file_return_exactly_zero})
