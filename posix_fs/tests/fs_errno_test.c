/* posix_fs: errno mapping, EINTR retry, short writes and write rollback, driven through the fault-injecting seam. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "posix_fs/posix_fs.h"
#include "posix_fs/tests/kit.h"
#include "ports/ports.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static kf_t g_kf;
static posix_fs_t g_fs;
static ports_fs_t g_p;

static ports_fs_err_t create(ports_fs_file_t *f) { return g_p.ops->create_temp(g_p.ctx, f); }
static ports_fs_err_t wr(ports_fs_file_t f, const char *s) { return g_p.ops->write(g_p.ctx, f, s, strlen(s)); }
static ports_fs_err_t commit(ports_fs_file_t f, const char *n) {
    return g_p.ops->commit(g_p.ctx, f, (core_str_t){n, strlen(n)});
}
static ports_fs_err_t discard(ports_fs_file_t f) { return g_p.ops->discard(g_p.ctx, f); }

static void begin(ctest_t *t) {
    kf_open_fs(t, &g_kf, &g_fs);
    g_p = posix_fs_port(&g_fs);
}

static ports_fs_file_t mk(ctest_t *t) {
    ports_fs_file_t f = PORTS_FS_NO_FILE;
    CHECK_EQ_I64(t, create(&f), PORTS_FS_OK);
    return f;
}

/* Arm the NEXT `times` calls of `op` to fail with errno `err`. */
static void arm(kf_op_t op, unsigned times, int err) { kf_fail(&g_kf, op, g_kf.calls[op] + 1u, times, err); }

static void finish(ctest_t *t) {
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, kf_open_fds(&g_kf), 0);
    CHECK_EQ_U64(t, g_kf.bad_calls, 0);
    CHECK_EQ_U64(t, g_kf.holes, 0);
}

typedef struct {
    int err;
    ports_fs_err_t want;
} emap_t;

#define NOSP PORTS_FS_E_NOSPACE
#define IO PORTS_FS_E_IO

static const emap_t k_common[] = {
    {ENOSPC, NOSP}, {EDQUOT, NOSP}, {EFBIG, NOSP}, {EIO, IO},     {EACCES, IO}, {EPERM, IO},
    {EROFS, IO},    {EBADF, IO},    {EINVAL, IO},  {ENOMEM, IO},  {EAGAIN, IO}, {EMFILE, IO},
    {ENFILE, IO},   {EEXIST, IO},   {ENOENT, IO},  {EISDIR, IO},  {EOPNOTSUPP, IO}, {EXDEV, IO},
    {EPIPE, IO},    {ELOOP, IO}};

static void t_write_errno_mapping_and_unchanged_content(ctest_t *t) {
    for (size_t i = 0; i < sizeof k_common / sizeof k_common[0]; i++) {
        begin(t);
        ports_fs_file_t f = mk(t);
        CHECK_EQ_I64(t, wr(f, "base"), PORTS_FS_OK);
        arm(KF_PWRITE, 1, k_common[i].err);
        CHECK_EQ_I64(t, wr(f, "XYZ"), k_common[i].want);
        CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 2);
        CHECK(t, strchr(kf_log(&g_kf), 'R') == NULL); /* nothing reached the file: no truncate */
        CHECK_EQ_I64(t, wr(f, "XYZ"), PORTS_FS_OK);   /* the handle is still valid */
        CHECK_EQ_I64(t, commit(f, "n"), PORTS_FS_OK);
        CHECK(t, kf_entry_is(&g_kf, "n", "baseXYZ", 7));
        CHECK_EQ_U64(t, g_kf.last_off, 4); /* the failed write did not advance the offset */
        finish(t);
    }
}

static void t_create_errno_mapping(ctest_t *t) {
    for (size_t i = 0; i < sizeof k_common / sizeof k_common[0]; i++) {
        begin(t);
        arm(KF_OPEN_TMP, 1, k_common[i].err);
        ports_fs_file_t f = 424242;
        CHECK_EQ_I64(t, create(&f), k_common[i].want);
        CHECK_EQ_U64(t, f, 424242);
        CHECK_EQ_U64(t, kf_open_tmp(&g_kf), 0);
        ports_fs_file_t h[POSIX_FS_SLOTS];
        for (size_t k = 0; k < POSIX_FS_SLOTS; k++) { /* the failed attempt did not consume a slot */
            CHECK_EQ_I64(t, create(&h[k]), PORTS_FS_OK);
        }
        finish(t);
    }
}

static void commit_fault(ctest_t *t, kf_op_t op, int err, ports_fs_err_t want) {
    begin(t);
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, wr(f, "payload"), PORTS_FS_OK);
    arm(op, 1, err);
    CHECK_EQ_I64(t, commit(f, "out.bin"), want);
    CHECK_EQ_U64(t, kf_entry_count(&g_kf), 0);
    CHECK_EQ_U64(t, kf_open_tmp(&g_kf), 1); /* temp file still open */
    CHECK_EQ_I64(t, commit(f, "out.bin"), PORTS_FS_OK); /* and valid: retry works */
    CHECK(t, kf_entry_is(&g_kf, "out.bin", "payload", 7));
    finish(t);
}

static void t_fsync_errno_mapping(ctest_t *t) {
    for (size_t i = 0; i < sizeof k_common / sizeof k_common[0]; i++) {
        commit_fault(t, KF_FSYNC, k_common[i].err, k_common[i].want);
    }
}

static void t_link_errno_mapping(ctest_t *t) {
    for (size_t i = 0; i < sizeof k_common / sizeof k_common[0]; i++) {
        commit_fault(t, KF_LINK, k_common[i].err,
                     k_common[i].err == EEXIST ? PORTS_FS_E_EXISTS : k_common[i].want);
    }
}

static void t_link_failure_does_not_reach_close_or_free_the_slot(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    arm(KF_LINK, 1, EIO);
    CHECK_EQ_I64(t, commit(f, "a"), PORTS_FS_E_IO);
    CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 0);
    CHECK_EQ_STR(t, kf_log(&g_kf), "TSL");
    ports_fs_file_t other;
    for (size_t i = 1; i < POSIX_FS_SLOTS; i++) {
        CHECK_EQ_I64(t, create(&other), PORTS_FS_OK);
    }
    CHECK_EQ_I64(t, create(&other), PORTS_FS_E_NOSPACE); /* the failed commit kept its slot: pool is full */
    CHECK_EQ_I64(t, discard(f), PORTS_FS_OK);
    finish(t);
}

static void t_eintr_is_retried_for_every_call_but_close(ctest_t *t) {
    ports_fs_file_t f;
    begin(t);
    arm(KF_OPEN_TMP, 3, EINTR);
    CHECK_EQ_I64(t, create(&f), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.calls[KF_OPEN_TMP], 4);
    arm(KF_PWRITE, 3, EINTR);
    CHECK_EQ_I64(t, wr(f, "abc"), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 4);
    CHECK_EQ_U64(t, g_kf.last_off, 0);
    CHECK_EQ_U64(t, g_kf.last_len, 3);
    CHECK(t, strchr(kf_log(&g_kf), 'R') == NULL);
    arm(KF_FSYNC, 2, EINTR);
    arm(KF_LINK, 2, EINTR);
    kf_log_reset(&g_kf);
    CHECK_EQ_I64(t, commit(f, "e"), PORTS_FS_OK);
    CHECK_EQ_STR(t, kf_log(&g_kf), "SSSLLLC");
    CHECK(t, kf_entry_is(&g_kf, "e", "abc", 3));
    finish(t);
}

static void t_eintr_retry_does_not_mask_other_errors(ctest_t *t) {
    ports_fs_file_t f;
    begin(t);
    kf_fail(&g_kf, KF_OPEN_TMP, 1, 1, EINTR);
    kf_fail2(&g_kf, KF_OPEN_TMP, 2, EIO); /* EINTR, then EIO */
    CHECK_EQ_I64(t, create(&f), PORTS_FS_E_IO);
    CHECK_EQ_U64(t, g_kf.calls[KF_OPEN_TMP], 2);
    f = mk(t);
    kf_fail(&g_kf, KF_PWRITE, 1, 1, EINTR);
    CHECK_EQ_I64(t, wr(f, "a"), PORTS_FS_OK);
    kf_fail(&g_kf, KF_FSYNC, 1, 1, EINTR);
    kf_fail(&g_kf, KF_LINK, 1, 1, EINTR);
    CHECK_EQ_I64(t, commit(f, "a"), PORTS_FS_OK);
    finish(t);
}

static void t_short_writes_continue_at_the_new_offset(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    g_kf.chunk = 3;
    CHECK_EQ_I64(t, wr(f, "abcdefg"), PORTS_FS_OK); /* 7 bytes in 3 + 3 + 1 */
    CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 3);
    CHECK_EQ_U64(t, g_kf.last_off, 6);
    CHECK_EQ_U64(t, g_kf.last_len, 1); /* each call was given everything still to write */
    g_kf.chunk = 2;
    CHECK_EQ_I64(t, wr(f, "hijkl"), PORTS_FS_OK); /* starts at offset 7 */
    CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 6);
    CHECK_EQ_U64(t, g_kf.last_off, 11);
    CHECK_EQ_U64(t, g_kf.last_len, 1);
    g_kf.chunk = 1;
    CHECK_EQ_I64(t, wr(f, "mn"), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 8);
    CHECK_EQ_I64(t, commit(f, "s"), PORTS_FS_OK);
    CHECK(t, kf_entry_is(&g_kf, "s", "abcdefghijklmn", 14));
    finish(t);
}

static void t_single_call_when_the_seam_writes_everything(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, wr(f, "12345"), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 1);
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, f, "", 0), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 1); /* a zero-length write makes no seam call */
    CHECK_EQ_I64(t, wr(f, "6"), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.last_off, 5);
    CHECK_EQ_U64(t, g_kf.last_len, 1);
    CHECK_EQ_I64(t, discard(f), PORTS_FS_OK);
    finish(t);
}

static void t_failure_after_partial_progress_rolls_back(ctest_t *t) {
    static const emap_t cases[] = {{ENOSPC, NOSP}, {EDQUOT, NOSP}, {EIO, IO}};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        begin(t);
        ports_fs_file_t f = mk(t);
        CHECK_EQ_I64(t, wr(f, "base"), PORTS_FS_OK);
        g_kf.chunk = 3;
        kf_fail(&g_kf, KF_PWRITE, g_kf.calls[KF_PWRITE] + 3u, 1, cases[i].err); /* third chunk fails */
        CHECK_EQ_I64(t, wr(f, "ABCDEFGHI"), cases[i].want);
        CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 4); /* 3 + 3 delivered, the third call failed */
        CHECK_EQ_U64(t, g_kf.calls[KF_FTRUNCATE], 1);
        CHECK_EQ_U64(t, g_kf.last_len, 4); /* truncated back to the size before the write */
        g_kf.chunk = 0;
        CHECK_EQ_I64(t, wr(f, "XYZ"), PORTS_FS_OK);
        CHECK_EQ_U64(t, g_kf.last_off, 4);
        CHECK_EQ_I64(t, commit(f, "n"), PORTS_FS_OK);
        CHECK(t, kf_entry_is(&g_kf, "n", "baseXYZ", 7));
        finish(t);
    }
}

static void t_zero_progress_is_an_io_error(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, wr(f, "base"), PORTS_FS_OK);
    g_kf.pw_ret_nth = g_kf.calls[KF_PWRITE] + 1u;
    g_kf.pw_ret_val = 0;
    CHECK_EQ_I64(t, wr(f, "XYZ"), PORTS_FS_E_IO); /* no progress: must not loop forever */
    CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 2);
    CHECK_EQ_U64(t, g_kf.calls[KF_FTRUNCATE], 0);
    g_kf.chunk = 2;
    g_kf.pw_ret_nth = g_kf.calls[KF_PWRITE] + 2u; /* second call of the next write returns 0 */
    CHECK_EQ_I64(t, wr(f, "12345"), PORTS_FS_E_IO);
    CHECK_EQ_U64(t, g_kf.calls[KF_FTRUNCATE], 1);
    CHECK_EQ_U64(t, g_kf.last_len, 4); /* 2 bytes had arrived: rolled back to "base" */
    g_kf.pw_ret_nth = 0;
    g_kf.chunk = 0;
    CHECK_EQ_I64(t, wr(f, "!"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(f, "n"), PORTS_FS_OK);
    CHECK(t, kf_entry_is(&g_kf, "n", "base!", 5));
    finish(t);
}

static void t_overlong_return_is_an_io_error(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    g_kf.pw_ret_nth = 1;
    g_kf.pw_ret_val = 4; /* asked for 3 */
    CHECK_EQ_I64(t, wr(f, "abc"), PORTS_FS_E_IO);
    g_kf.pw_ret_nth = 2;
    g_kf.pw_ret_val = 3; /* control: exactly what was asked is a success */
    CHECK_EQ_I64(t, wr(f, "abc"), PORTS_FS_OK);
    CHECK_EQ_I64(t, discard(f), PORTS_FS_OK);
    finish(t);
}

static void t_failed_rollback_breaks_the_temp_file(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    ports_fs_file_t g = mk(t);
    CHECK_EQ_I64(t, wr(f, "base"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(g, "other"), PORTS_FS_OK);
    g_kf.chunk = 3;
    kf_fail(&g_kf, KF_PWRITE, g_kf.calls[KF_PWRITE] + 2u, 1, ENOSPC);
    arm(KF_FTRUNCATE, 1, EIO);
    CHECK_EQ_I64(t, wr(f, "ABCDEF"), PORTS_FS_E_NOSPACE); /* the write's own error is reported */
    g_kf.chunk = 0;
    CHECK_EQ_U64(t, g_kf.calls[KF_FTRUNCATE], 1);
    CHECK_EQ_I64(t, wr(f, "x"), PORTS_FS_E_IO); /* content unknown: broken */
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, f, NULL, 3), PORTS_FS_E_ARG); /* argument errors come first */
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, f, NULL, 0), PORTS_FS_OK);   /* zero length is a no-op */
    kf_log_reset(&g_kf);
    CHECK_EQ_I64(t, commit(f, "n"), PORTS_FS_E_IO);
    CHECK_EQ_STR(t, kf_log(&g_kf), ""); /* no fsync, no link for a broken file */
    CHECK_EQ_I64(t, commit(f, "bad/name"), PORTS_FS_E_ARG); /* name errors come before the broken check */
    CHECK_EQ_I64(t, wr(g, "!"), PORTS_FS_OK);               /* other files are unaffected */
    CHECK_EQ_I64(t, commit(g, "g"), PORTS_FS_OK);
    CHECK(t, kf_entry_is(&g_kf, "g", "other!", 6));
    CHECK_EQ_U64(t, kf_entry_count(&g_kf), 1);
    CHECK_EQ_I64(t, discard(f), PORTS_FS_OK); /* a broken file can still be discarded */
    CHECK_EQ_U64(t, kf_open_tmp(&g_kf), 0);
    finish(t);
}

static void t_rollback_retries_eintr_and_is_not_broken_by_it(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, wr(f, "base"), PORTS_FS_OK);
    g_kf.chunk = 2;
    kf_fail(&g_kf, KF_PWRITE, g_kf.calls[KF_PWRITE] + 2u, 1, EIO);
    arm(KF_FTRUNCATE, 2, EINTR);
    CHECK_EQ_I64(t, wr(f, "ABCD"), PORTS_FS_E_IO);
    CHECK_EQ_U64(t, g_kf.calls[KF_FTRUNCATE], 3);
    g_kf.chunk = 0;
    CHECK_EQ_I64(t, wr(f, "Z"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(f, "n"), PORTS_FS_OK);
    CHECK(t, kf_entry_is(&g_kf, "n", "baseZ", 5));
    finish(t);
}

static void t_size_limits_are_checked_without_wrapping(ctest_t *t) {
    ports_fs_file_t f;
    ports_fs_file_t g;
    ports_fs_file_t h;
    begin(t);
    f = mk(t);
    g = mk(t);
    h = mk(t);
    g_kf.sink = true;
    const char b[8] = "01234567";
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, f, b, (size_t)INT64_MAX), PORTS_FS_OK); /* exactly the limit */
    CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 1);
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, f, b, 1), PORTS_FS_E_NOSPACE); /* one more does not fit */
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, f, b, SIZE_MAX), PORTS_FS_E_NOSPACE);
    CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 1); /* refused without a seam call */
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, g, b, (size_t)INT64_MAX + 1u), PORTS_FS_E_NOSPACE);
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, g, b, SIZE_MAX), PORTS_FS_E_NOSPACE);
    CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 1);
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, h, b, 5), PORTS_FS_OK);
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, h, b, SIZE_MAX), PORTS_FS_E_NOSPACE); /* 5 + SIZE_MAX wraps in 64 bits */
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, h, b, (size_t)INT64_MAX - 4u), PORTS_FS_E_NOSPACE);
    CHECK_EQ_U64(t, g_kf.calls[KF_PWRITE], 2);
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, h, b, (size_t)INT64_MAX - 5u), PORTS_FS_OK); /* 5 + this == INT64_MAX */
    CHECK_EQ_U64(t, g_kf.last_off, 5);
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, h, b, 1), PORTS_FS_E_NOSPACE);
    CHECK_EQ_I64(t, g_p.ops->write(g_p.ctx, h, b, 0), PORTS_FS_OK); /* zero is still fine when full */
    CHECK_EQ_I64(t, discard(f), PORTS_FS_OK);
    CHECK_EQ_I64(t, discard(g), PORTS_FS_OK);
    CHECK_EQ_I64(t, discard(h), PORTS_FS_OK);
    finish(t);
}

CTEST_SUITE("posix_fs_errno",
    {"write_errno_mapping_and_unchanged_content", t_write_errno_mapping_and_unchanged_content},
    {"create_errno_mapping", t_create_errno_mapping},
    {"fsync_errno_mapping", t_fsync_errno_mapping},
    {"link_errno_mapping", t_link_errno_mapping},
    {"link_failure_does_not_reach_close_or_free_the_slot", t_link_failure_does_not_reach_close_or_free_the_slot},
    {"eintr_is_retried_for_every_call_but_close", t_eintr_is_retried_for_every_call_but_close},
    {"eintr_retry_does_not_mask_other_errors", t_eintr_retry_does_not_mask_other_errors},
    {"short_writes_continue_at_the_new_offset", t_short_writes_continue_at_the_new_offset},
    {"single_call_when_the_seam_writes_everything", t_single_call_when_the_seam_writes_everything},
    {"failure_after_partial_progress_rolls_back", t_failure_after_partial_progress_rolls_back},
    {"zero_progress_is_an_io_error", t_zero_progress_is_an_io_error},
    {"overlong_return_is_an_io_error", t_overlong_return_is_an_io_error},
    {"failed_rollback_breaks_the_temp_file", t_failed_rollback_breaks_the_temp_file},
    {"rollback_retries_eintr_and_is_not_broken_by_it", t_rollback_retries_eintr_and_is_not_broken_by_it},
    {"size_limits_are_checked_without_wrapping", t_size_limits_are_checked_without_wrapping})
