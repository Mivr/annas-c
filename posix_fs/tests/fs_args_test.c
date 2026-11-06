/* posix_fs: object lifecycle and argument handling, through posix_fs.h / ports.h over the fake seam. */
#define _GNU_SOURCE
#include <errno.h>
#include <string.h>

#include "posix_fs/posix_fs.h"
#include "posix_fs/tests/kit.h"
#include "ports/ports.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

static kf_t g_kf;
static posix_fs_t g_fs;

static ports_fs_err_t create(ports_fs_t p, ports_fs_file_t *f) { return p.ops->create_temp(p.ctx, f); }
static ports_fs_err_t wr(ports_fs_t p, ports_fs_file_t f, const char *s) { return p.ops->write(p.ctx, f, s, strlen(s)); }
static ports_fs_err_t commit(ports_fs_t p, ports_fs_file_t f, core_str_t n) { return p.ops->commit(p.ctx, f, n); }
static ports_fs_err_t discard(ports_fs_t p, ports_fs_file_t f) { return p.ops->discard(p.ctx, f); }

static void t_open_validates_arguments(ctest_t *t) {
    kf_init(&g_kf, 3, 10);
    CHECK_EQ_I64(t, posix_fs_open_sys(NULL, kf_sys(), &g_kf, "/fake"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, NULL, &g_kf, "/fake"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, kf_sys(), &g_kf, NULL), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, kf_sys(), &g_kf, ""), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_open(NULL, "/x"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, NULL), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, posix_fs_open(&g_fs, ""), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, g_kf.calls[KF_OPEN_DIR], 0); /* nothing reached the seam */
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, kf_sys(), &g_kf, "/fake"), PORTS_FS_OK); /* control */
    CHECK_EQ_U64(t, g_kf.calls[KF_OPEN_DIR], 1);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

static void t_open_rejects_a_seam_table_with_a_hole(ctest_t *t) {
    posix_fs_sys_t s = *kf_sys();
    kf_init(&g_kf, 3, 10);
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, &s, &g_kf, "/fake"), PORTS_FS_OK); /* control: the complete copy */
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    s.open_dir = NULL;
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, &s, &g_kf, "/fake"), PORTS_FS_E_ARG);
    s = *kf_sys();
    s.open_tmp = NULL;
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, &s, &g_kf, "/fake"), PORTS_FS_E_ARG);
    s = *kf_sys();
    s.pwrite = NULL;
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, &s, &g_kf, "/fake"), PORTS_FS_E_ARG);
    s = *kf_sys();
    s.ftruncate = NULL;
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, &s, &g_kf, "/fake"), PORTS_FS_E_ARG);
    s = *kf_sys();
    s.fsync = NULL;
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, &s, &g_kf, "/fake"), PORTS_FS_E_ARG);
    s = *kf_sys();
    s.link_tmp = NULL;
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, &s, &g_kf, "/fake"), PORTS_FS_E_ARG);
    s = *kf_sys();
    s.close = NULL;
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, &s, &g_kf, "/fake"), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, g_kf.calls[KF_OPEN_DIR], 1); /* only the control reached the seam */
    CHECK_EQ_U64(t, kf_open_fds(&g_kf), 0);
}

static void t_open_passes_the_path_and_context(ctest_t *t) {
    kf_init(&g_kf, 3, 10);
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, kf_sys(), &g_kf, "rel/fake"), PORTS_FS_OK);
    CHECK_EQ_STR(t, g_kf.last_path, "rel/fake");
    CHECK_EQ_U64(t, kf_open_fds(&g_kf), 1);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, kf_open_fds(&g_kf), 0);
}

static void t_open_failure_maps_to_io_and_leaves_a_closed_object(ctest_t *t) {
    kf_init(&g_kf, 3, 10);
    memset(&g_fs, 0xA5, sizeof g_fs); /* garbage: open must fully initialise it */
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, kf_sys(), &g_kf, "/nowhere"), PORTS_FS_E_IO);
    ports_fs_t p = posix_fs_port(&g_fs);
    ports_fs_file_t f = 55;
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_E_IO);
    CHECK_EQ_U64(t, f, 55);
    CHECK_EQ_I64(t, wr(p, 1, "x"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, 1, LIT("n")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, discard(p, 1), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, g_kf.calls[KF_OPEN_TMP], 0);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 0); /* nothing was open: nothing to close */
    memset(&g_fs, 0x5A, sizeof g_fs);
    kf_fail(&g_kf, KF_OPEN_DIR, 2, 1, EACCES);
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, kf_sys(), &g_kf, "/fake"), PORTS_FS_E_IO); /* call 2 fails */
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 0);
}

static void t_open_failures_all_map_to_io_even_enospc(ctest_t *t) {
    static const int errs[] = {ENOENT, ENOTDIR, EACCES, EMFILE, ENOSPC, EEXIST, EIO};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        kf_init(&g_kf, 3, 10);
        kf_fail(&g_kf, KF_OPEN_DIR, 1, 1, errs[i]);
        CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, kf_sys(), &g_kf, "/fake"), PORTS_FS_E_IO);
        CHECK_EQ_U64(t, g_kf.calls[KF_OPEN_DIR], 1);
    }
    kf_init(&g_kf, 3, 10);
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, kf_sys(), &g_kf, "/fake"), PORTS_FS_OK); /* control */
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

static void t_open_retries_eintr(ctest_t *t) {
    kf_init(&g_kf, 3, 10);
    kf_fail(&g_kf, KF_OPEN_DIR, 1, 3, EINTR);
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, kf_sys(), &g_kf, "/fake"), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.calls[KF_OPEN_DIR], 4);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

static void t_close_is_idempotent_and_closes_everything(ctest_t *t) {
    ports_fs_file_t a;
    ports_fs_file_t b;
    kf_open_fs(t, &g_kf, &g_fs);
    ports_fs_t p = posix_fs_port(&g_fs);
    CHECK_EQ_I64(t, create(p, &a), PORTS_FS_OK);
    CHECK_EQ_I64(t, create(p, &b), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, a, "x"), PORTS_FS_OK);
    CHECK_EQ_U64(t, kf_open_fds(&g_kf), 3);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, kf_open_fds(&g_kf), 0);
    CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 3);
    CHECK_EQ_U64(t, kf_entry_count(&g_kf), 0); /* live temp files were discarded, not committed */
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 3); /* no second close of anything */
    CHECK_EQ_I64(t, posix_fs_close(NULL), PORTS_FS_E_ARG);
    ports_fs_file_t c = 9;
    CHECK_EQ_I64(t, create(p, &c), PORTS_FS_E_IO); /* closed object */
    CHECK_EQ_U64(t, c, 9);
    CHECK_EQ_I64(t, wr(p, a, "x"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, b, LIT("n")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, discard(p, a), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 3);
}

static void t_close_reports_failures_but_releases_everything(ctest_t *t) {
    ports_fs_file_t a;
    ports_fs_file_t b;
    kf_open_fs(t, &g_kf, &g_fs);
    ports_fs_t p = posix_fs_port(&g_fs);
    CHECK_EQ_I64(t, create(p, &a), PORTS_FS_OK);
    CHECK_EQ_I64(t, create(p, &b), PORTS_FS_OK);
    kf_fail(&g_kf, KF_CLOSE, 1, 1, EIO);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_E_IO);
    CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 3); /* kept going after the failure */
    CHECK_EQ_U64(t, kf_open_fds(&g_kf), 0);
    kf_open_fs(t, &g_kf, &g_fs);
    kf_fail(&g_kf, KF_CLOSE, 1, 1, EIO); /* directory descriptor failing alone */
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_E_IO);
    kf_open_fs(t, &g_kf, &g_fs);
    kf_fail(&g_kf, KF_CLOSE, 1, 1, EINTR); /* EINTR counts as success and is not retried */
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 1);
}

static void t_null_object_port_fails_every_call_with_arg(ctest_t *t) {
    ports_fs_t p = posix_fs_port(NULL);
    ports_fs_file_t f = 31;
    CHECK(t, p.ops != NULL);
    CHECK(t, p.ops->create_temp != NULL && p.ops->write != NULL && p.ops->commit != NULL && p.ops->discard != NULL);
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, f, 31);
    CHECK_EQ_I64(t, wr(p, 1, "x"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, 1, LIT("n")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, discard(p, 1), PORTS_FS_E_ARG);
    kf_open_fs(t, &g_kf, &g_fs);
    ports_fs_t q = posix_fs_port(&g_fs);
    CHECK(t, q.ctx == (void *)&g_fs);
    CHECK(t, q.ops == p.ops); /* one shared const table */
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

static void t_stale_handle_never_aliases_a_recycled_slot(ctest_t *t) {
    ports_fs_file_t a;
    ports_fs_file_t b;
    kf_open_fs(t, &g_kf, &g_fs);
    ports_fs_t p = posix_fs_port(&g_fs);
    CHECK_EQ_I64(t, create(p, &a), PORTS_FS_OK);
    CHECK_EQ_I64(t, discard(p, a), PORTS_FS_OK);
    CHECK_EQ_I64(t, create(p, &b), PORTS_FS_OK); /* very likely the same slot and descriptor */
    CHECK(t, a != b);
    CHECK_EQ_I64(t, wr(p, a, "stale"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, a, LIT("stale")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, discard(p, a), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, kf_open_tmp(&g_kf), 1); /* b's descriptor survived the stale discard */
    CHECK_EQ_I64(t, wr(p, b, "live"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, b, LIT("b")), PORTS_FS_OK);
    CHECK(t, kf_entry_is(&g_kf, "b", "live", 4));
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

static void t_handles_are_never_reused(ctest_t *t) {
    ports_fs_file_t seen[40];
    kf_open_fs(t, &g_kf, &g_fs);
    ports_fs_t p = posix_fs_port(&g_fs);
    for (size_t i = 0; i < 40u; i++) {
        CHECK_EQ_I64(t, create(p, &seen[i]), PORTS_FS_OK);
        CHECK(t, seen[i] != PORTS_FS_NO_FILE);
        for (size_t j = 0; j < i; j++) {
            CHECK(t, seen[i] != seen[j]);
        }
        if (i % 2u == 0u) {
            CHECK_EQ_I64(t, discard(p, seen[i]), PORTS_FS_OK);
        } else {
            CHECK_EQ_I64(t, commit(p, seen[i], (core_str_t){"f", 1}), i == 1u ? PORTS_FS_OK : PORTS_FS_E_EXISTS);
            CHECK_EQ_I64(t, discard(p, seen[i]), i == 1u ? PORTS_FS_E_ARG : PORTS_FS_OK);
        }
    }
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

static void t_descriptor_zero_is_a_valid_descriptor(ctest_t *t) {
    ports_fs_file_t f;
    kf_init(&g_kf, 7, 0); /* temp descriptors start at 0 */
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, kf_sys(), &g_kf, "/fake"), PORTS_FS_OK);
    ports_fs_t p = posix_fs_port(&g_fs);
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, f, "zero"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, f, LIT("z")), PORTS_FS_OK);
    CHECK(t, kf_entry_is(&g_kf, "z", "zero", 4));
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, discard(p, f), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.bad_calls, 0);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, kf_open_fds(&g_kf), 0);
    kf_init(&g_kf, 0, 5); /* and the directory itself may be descriptor 0 */
    CHECK_EQ_I64(t, posix_fs_open_sys(&g_fs, kf_sys(), &g_kf, "/fake"), PORTS_FS_OK);
    p = posix_fs_port(&g_fs);
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, f, LIT("d")), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.bad_calls, 0);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, kf_open_fds(&g_kf), 0);
}

static void t_pool_exhaustion_makes_no_seam_call(ctest_t *t) {
    ports_fs_file_t h[POSIX_FS_SLOTS];
    ports_fs_file_t x = 5;
    kf_open_fs(t, &g_kf, &g_fs);
    ports_fs_t p = posix_fs_port(&g_fs);
    for (size_t i = 0; i < POSIX_FS_SLOTS; i++) {
        CHECK_EQ_I64(t, create(p, &h[i]), PORTS_FS_OK);
    }
    CHECK_EQ_U64(t, g_kf.calls[KF_OPEN_TMP], POSIX_FS_SLOTS);
    CHECK_EQ_I64(t, create(p, &x), PORTS_FS_E_NOSPACE);
    CHECK_EQ_U64(t, g_kf.calls[KF_OPEN_TMP], POSIX_FS_SLOTS); /* refused before asking the OS */
    CHECK_EQ_U64(t, x, 5);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

static void t_create_passes_dir_fd_and_mode(ctest_t *t) {
    ports_fs_file_t f;
    kf_open_fs(t, &g_kf, &g_fs);
    ports_fs_t p = posix_fs_port(&g_fs);
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.last_mode, POSIX_FS_FILE_MODE);
    CHECK_EQ_U64(t, POSIX_FS_FILE_MODE, 0644);
    CHECK_EQ_U64(t, g_kf.bad_calls, 0); /* the fake rejects a wrong directory descriptor */
    CHECK_EQ_U64(t, POSIX_FS_SLOTS, 8);
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
}

CTEST_SUITE("posix_fs_args",
    {"open_validates_arguments", t_open_validates_arguments},
    {"open_rejects_a_seam_table_with_a_hole", t_open_rejects_a_seam_table_with_a_hole},
    {"open_passes_the_path_and_context", t_open_passes_the_path_and_context},
    {"open_failure_maps_to_io_and_leaves_a_closed_object", t_open_failure_maps_to_io_and_leaves_a_closed_object},
    {"open_failures_all_map_to_io_even_enospc", t_open_failures_all_map_to_io_even_enospc},
    {"open_retries_eintr", t_open_retries_eintr},
    {"close_is_idempotent_and_closes_everything", t_close_is_idempotent_and_closes_everything},
    {"close_reports_failures_but_releases_everything", t_close_reports_failures_but_releases_everything},
    {"null_object_port_fails_every_call_with_arg", t_null_object_port_fails_every_call_with_arg},
    {"stale_handle_never_aliases_a_recycled_slot", t_stale_handle_never_aliases_a_recycled_slot},
    {"handles_are_never_reused", t_handles_are_never_reused},
    {"descriptor_zero_is_a_valid_descriptor", t_descriptor_zero_is_a_valid_descriptor},
    {"pool_exhaustion_makes_no_seam_call", t_pool_exhaustion_makes_no_seam_call},
    {"create_passes_dir_fd_and_mode", t_create_passes_dir_fd_and_mode})
