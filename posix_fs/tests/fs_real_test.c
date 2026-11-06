/* posix_fs: behaviour of the real libc table in fresh directories under $TMPDIR (cwd if unset). Every test removes
 * what it made and checks that nothing else is left. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "posix_fs/posix_fs.h"
#include "posix_fs/tests/kit.h"
#include "ports/ports.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define BIG ((size_t)(1536u * 1024u))

static kit_fs_env_t g_env;
static posix_fs_t g_fs;
static unsigned char g_src[BIG];
static unsigned char g_got[BIG];

static ports_fs_err_t create(ports_fs_t p, ports_fs_file_t *f) { return p.ops->create_temp(p.ctx, f); }
static ports_fs_err_t wr(ports_fs_t p, ports_fs_file_t f, const char *s) { return p.ops->write(p.ctx, f, s, strlen(s)); }
static ports_fs_err_t commit(ports_fs_t p, ports_fs_file_t f, core_str_t n) { return p.ops->commit(p.ctx, f, n); }
static ports_fs_err_t discard(ports_fs_t p, ports_fs_file_t f) { return p.ops->discard(p.ctx, f); }

static void t_real_table_is_complete(ctest_t *t) {
    CHECK(t, posix_fs_sys_libc.open_dir != NULL);
    CHECK(t, posix_fs_sys_libc.open_tmp != NULL);
    CHECK(t, posix_fs_sys_libc.pwrite != NULL);
    CHECK(t, posix_fs_sys_libc.ftruncate != NULL);
    CHECK(t, posix_fs_sys_libc.fsync != NULL);
    CHECK(t, posix_fs_sys_libc.link_tmp != NULL);
    CHECK(t, posix_fs_sys_libc.close != NULL);
}

static void t_real_seam_reports_negative_errno(ctest_t *t) {
    const posix_fs_sys_t *s = &posix_fs_sys_libc;
    static char dir[KIT_PATH_MAX];
    kit_tmpdir_make(t, dir);
    int64_t d = s->open_dir(NULL, dir);
    CHECK(t, d >= 0);
    CHECK_EQ_I64(t, s->open_dir(NULL, "/nonexistent-dir-for-posix-fs-test"), -ENOENT);
    CHECK_EQ_I64(t, s->close(NULL, (int)d), 0);
    CHECK_EQ_I64(t, s->close(NULL, (int)d), -EBADF); /* positive control above: the first close worked */
    CHECK_EQ_I64(t, s->fsync(NULL, (int)d), -EBADF);
    CHECK_EQ_I64(t, s->pwrite(NULL, (int)d, "x", 1, 0), -EBADF);
    CHECK_EQ_I64(t, s->ftruncate(NULL, (int)d, 0), -EBADF);
    CHECK_EQ_I64(t, s->open_tmp(NULL, (int)d, 0644u), -EBADF);
    CHECK_EQ_U64(t, kit_tmpdir_remove(t, dir), 0);
}

static void t_open_failures_and_controls(ctest_t *t) {
    static char dir[KIT_PATH_MAX];
    static char file[KIT_PATH_MAX + 8u];
    size_t before = kit_fd_count();
    kit_tmpdir_make(t, dir);
    CHECK(t, kit_dir_put(dir, "plain", "x", 1));
    snprintf(file, sizeof file, "%s/plain", dir);
    memset(&g_fs, 0xA5, sizeof g_fs);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, file), PORTS_FS_E_IO); /* a regular file is not a directory */
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    snprintf(file, sizeof file, "%s/missing", dir);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, file), PORTS_FS_E_IO);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, ""), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, NULL), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, dir), PORTS_FS_OK); /* control: the directory itself */
    ports_fs_file_t f = 0;
    CHECK_EQ_I64(t, create(posix_fs_port(&g_fs), &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, discard(posix_fs_port(&g_fs), f), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, kit_fd_count(), before);
    CHECK_EQ_U64(t, kit_tmpdir_remove(t, dir), 1); /* only the file this test put there */
}

static void t_mode_follows_umask(ctest_t *t) {
    static char dir[KIT_PATH_MAX];
    static char p[KIT_PATH_MAX + 8u];
    struct stat st;
    kit_tmpdir_make(t, dir);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, dir), PORTS_FS_OK);
    ports_fs_t port = posix_fs_port(&g_fs);
    ports_fs_file_t f;
    (void)umask(0);
    CHECK_EQ_I64(t, create(port, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(port, f, LIT("a")), PORTS_FS_OK);
    (void)umask(077);
    CHECK_EQ_I64(t, create(port, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(port, f, LIT("b")), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    snprintf(p, sizeof p, "%s/a", dir);
    CHECK_EQ_I64(t, stat(p, &st), 0);
    CHECK_EQ_U64(t, st.st_mode & 07777u, 0644u);
    CHECK(t, S_ISREG(st.st_mode));
    CHECK_EQ_U64(t, st.st_nlink, 1); /* the temp name never existed */
    snprintf(p, sizeof p, "%s/b", dir);
    CHECK_EQ_I64(t, stat(p, &st), 0);
    CHECK_EQ_U64(t, st.st_mode & 07777u, 0600u);
    CHECK_EQ_U64(t, kit_tmpdir_remove(t, dir), 2);
}

static void t_large_write_roundtrip(ctest_t *t) {
    kit_env_begin_real(t, &g_env);
    for (size_t i = 0; i < BIG; i++) {
        g_src[i] = (unsigned char)((i * 2654435761u) >> 13);
    }
    ports_fs_t p = g_env.fs;
    ports_fs_file_t f;
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_OK);
    size_t off = 0;
    const size_t steps[] = {1, 4096, 100000, 1, 700000, BIG - 1u - 4096u - 100000u - 1u - 700000u};
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        CHECK_EQ_I64(t, p.ops->write(p.ctx, f, g_src + off, steps[i]), PORTS_FS_OK);
        off += steps[i];
    }
    CHECK_EQ_U64(t, off, BIG);
    CHECK_EQ_U64(t, g_env.count(&g_env), 0);
    CHECK_EQ_I64(t, commit(p, f, LIT("big.bin")), PORTS_FS_OK);
    size_t n = 0;
    CHECK(t, g_env.read(&g_env, "big.bin", g_got, sizeof g_got, &n));
    CHECK_EQ_MEM(t, g_got, n, g_src, BIG);
    kit_env_end(t, &g_env);
}

static void t_existing_non_regular_names_are_exists(ctest_t *t) {
    static char dir[KIT_PATH_MAX];
    static char p[KIT_PATH_MAX + 8u];
    kit_tmpdir_make(t, dir);
    snprintf(p, sizeof p, "%s/dangling", dir);
    CHECK_EQ_I64(t, symlink("/nonexistent-target-for-posix-fs-test", p), 0);
    snprintf(p, sizeof p, "%s/subdir", dir);
    CHECK_EQ_I64(t, mkdir(p, 0700), 0);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, dir), PORTS_FS_OK);
    ports_fs_t port = posix_fs_port(&g_fs);
    ports_fs_file_t f;
    CHECK_EQ_I64(t, create(port, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(port, f, LIT("dangling")), PORTS_FS_E_EXISTS); /* a dangling symlink is a name too */
    CHECK_EQ_I64(t, commit(port, f, LIT("subdir")), PORTS_FS_E_EXISTS);
    CHECK_EQ_I64(t, commit(port, f, LIT("fresh")), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    struct stat st;
    snprintf(p, sizeof p, "%s/dangling", dir);
    CHECK_EQ_I64(t, lstat(p, &st), 0);
    CHECK(t, S_ISLNK(st.st_mode)); /* untouched: nothing was written through the link */
    snprintf(p, sizeof p, "%s/subdir", dir);
    CHECK_EQ_I64(t, rmdir(p), 0);
    CHECK_EQ_U64(t, kit_tmpdir_remove(t, dir), 2);
}

static void t_held_descriptor_survives_a_rename_of_the_directory(ctest_t *t) {
    static char dir[KIT_PATH_MAX];
    static char moved[KIT_PATH_MAX + 8u];
    kit_tmpdir_make(t, dir);
    snprintf(moved, sizeof moved, "%s.moved", dir);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, dir), PORTS_FS_OK);
    ports_fs_t port = posix_fs_port(&g_fs);
    ports_fs_file_t f;
    CHECK_EQ_I64(t, create(port, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(port, f, "held"), PORTS_FS_OK);
    CHECK_EQ_I64(t, rename(dir, moved), 0);
    CHECK_EQ_I64(t, commit(port, f, LIT("x")), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    unsigned char b[8];
    size_t n = 0;
    CHECK(t, kit_dir_read(moved, "x", b, sizeof b, &n) && n == 4 && memcmp(b, "held", 4) == 0);
    CHECK_EQ_U64(t, kit_tmpdir_remove(t, moved), 1);
}

static void t_removed_directory_fails_commit_but_keeps_the_handle(ctest_t *t) {
    static char dir[KIT_PATH_MAX];
    size_t before = kit_fd_count();
    kit_tmpdir_make(t, dir);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, dir), PORTS_FS_OK);
    ports_fs_t port = posix_fs_port(&g_fs);
    ports_fs_file_t f;
    CHECK_EQ_I64(t, create(port, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(port, f, "orphan"), PORTS_FS_OK);
    CHECK_EQ_U64(t, kit_dir_count(dir), 0); /* the temp file has no name, so rmdir works */
    CHECK_EQ_I64(t, rmdir(dir), 0);
    CHECK_EQ_I64(t, commit(port, f, LIT("x")), PORTS_FS_E_IO);
    CHECK_EQ_I64(t, wr(port, f, "!"), PORTS_FS_OK); /* the temp file is still valid */
    CHECK_EQ_I64(t, discard(port, f), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, kit_fd_count(), before);
}

static void t_close_with_live_temps_leaves_nothing(ctest_t *t) {
    static char dir[KIT_PATH_MAX];
    size_t before = kit_fd_count();
    kit_tmpdir_make(t, dir);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, dir), PORTS_FS_OK);
    ports_fs_t port = posix_fs_port(&g_fs);
    for (size_t i = 0; i < POSIX_FS_SLOTS; i++) {
        ports_fs_file_t f;
        CHECK_EQ_I64(t, create(port, &f), PORTS_FS_OK);
        CHECK_EQ_I64(t, wr(port, f, "pending"), PORTS_FS_OK);
    }
    CHECK_EQ_U64(t, kit_fd_count(), before + 1u + POSIX_FS_SLOTS);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, kit_fd_count(), before);
    CHECK_EQ_U64(t, kit_dir_count(dir), 0);
    CHECK_EQ_U64(t, kit_tmpdir_remove(t, dir), 0);
}

static void t_relative_and_symlinked_paths(ctest_t *t) {
    static char dir[KIT_PATH_MAX];
    static char link[KIT_PATH_MAX + 8u];
    kit_tmpdir_make(t, dir);
    snprintf(link, sizeof link, "%s.lnk", dir);
    CHECK_EQ_I64(t, symlink(dir, link), 0);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, link), PORTS_FS_OK); /* symlinks are followed */
    ports_fs_t port = posix_fs_port(&g_fs);
    ports_fs_file_t f;
    CHECK_EQ_I64(t, create(port, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(port, f, LIT("via-link")), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_I64(t, unlink(link), 0);
    static char old[KIT_PATH_MAX];
    CHECK(t, getcwd(old, sizeof old) != NULL);
    CHECK_EQ_I64(t, chdir(dir), 0);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, "."), PORTS_FS_OK); /* relative path */
    port = posix_fs_port(&g_fs);
    CHECK_EQ_I64(t, create(port, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(port, f, LIT("via-dot")), PORTS_FS_OK);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_I64(t, chdir(old), 0);
    CHECK_EQ_U64(t, kit_dir_count(dir), 2);
    CHECK_EQ_U64(t, kit_tmpdir_remove(t, dir), 2);
}

CTEST_SUITE("posix_fs_real",
    {"real_table_is_complete", t_real_table_is_complete},
    {"real_seam_reports_negative_errno", t_real_seam_reports_negative_errno},
    {"open_failures_and_controls", t_open_failures_and_controls},
    {"mode_follows_umask", t_mode_follows_umask},
    {"large_write_roundtrip", t_large_write_roundtrip},
    {"existing_non_regular_names_are_exists", t_existing_non_regular_names_are_exists},
    {"held_descriptor_survives_a_rename_of_the_directory", t_held_descriptor_survives_a_rename_of_the_directory},
    {"removed_directory_fails_commit_but_keeps_the_handle", t_removed_directory_fails_commit_but_keeps_the_handle},
    {"close_with_live_temps_leaves_nothing", t_close_with_live_temps_leaves_nothing},
    {"relative_and_symlinked_paths", t_relative_and_symlinked_paths})
