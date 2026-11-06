/* posix_fs: commit and discard sequencing against the fault-injecting seam. */
#define _GNU_SOURCE
#include <errno.h>
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
static ports_fs_err_t commit(ports_fs_file_t f, core_str_t n) { return g_p.ops->commit(g_p.ctx, f, n); }
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

static void arm(kf_op_t op, unsigned times, int err) { kf_fail(&g_kf, op, g_kf.calls[op] + 1u, times, err); }

static void finish(ctest_t *t) {
    CHECK_EQ_I64(t, posix_fs_close(&g_fs), PORTS_FS_OK);
    CHECK_EQ_U64(t, kf_open_fds(&g_kf), 0);
    CHECK_EQ_U64(t, g_kf.bad_calls, 0);
    CHECK_EQ_U64(t, g_kf.holes, 0);
}

static void t_commit_syncs_before_the_name_appears(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, wr(f, "some "), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(f, "bytes"), PORTS_FS_OK);
    CHECK_EQ_STR(t, kf_log(&g_kf), "TPP");
    CHECK_EQ_I64(t, commit(f, LIT("out.pdf")), PORTS_FS_OK);
    CHECK_EQ_STR(t, kf_log(&g_kf), "TPPSLC"); /* fsync, then link, then close */
    CHECK_EQ_STR(t, g_kf.last_name, "out.pdf");
    const kf_entry_t *e = kf_entry(&g_kf, "out.pdf");
    CHECK(t, e != NULL);
    if (e != NULL) {
        CHECK(t, e->was_synced); /* the whole content was on stable storage when the name appeared */
        CHECK_EQ_U64(t, e->mode, POSIX_FS_FILE_MODE);
    }
    CHECK(t, kf_entry_is(&g_kf, "out.pdf", "some bytes", 10));
    CHECK_EQ_U64(t, g_kf.calls[KF_FSYNC], 1);
    CHECK_EQ_U64(t, g_kf.calls[KF_FTRUNCATE], 0);
    finish(t);
}

static void t_empty_file_is_synced_and_linked(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, commit(f, LIT("e")), PORTS_FS_OK);
    CHECK_EQ_STR(t, kf_log(&g_kf), "TSLC");
    CHECK(t, kf_entry_is(&g_kf, "e", "", 0));
    finish(t);
}

static void t_name_is_passed_as_an_exact_c_string(ctest_t *t) {
    static char big[300];
    begin(t);
    memset(big, 'n', sizeof big - 1u);
    big[299] = '\0';
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, commit(f, (core_str_t){"abcdef", 3}), PORTS_FS_OK); /* a slice of a longer string */
    CHECK_EQ_STR(t, g_kf.last_name, "abc");
    CHECK(t, kf_entry(&g_kf, "abc") != NULL && kf_entry(&g_kf, "abcdef") == NULL);
    f = mk(t);
    CHECK_EQ_I64(t, commit(f, (core_str_t){big, 255}), PORTS_FS_OK);
    CHECK_EQ_U64(t, strlen(g_kf.last_name), 255);
    CHECK_EQ_I64(t, memcmp(g_kf.last_name, big, 255), 0);
    f = mk(t);
    CHECK_EQ_I64(t, commit(f, (core_str_t){big, 256}), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(f, (core_str_t){big, 1}), PORTS_FS_OK);
    CHECK_EQ_STR(t, g_kf.last_name, "n");
    finish(t);
}

static void t_invalid_name_or_handle_makes_no_seam_call(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, commit(f, LIT("a/b")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(f, LIT("..")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(f, LIT(".")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(f, (core_str_t){"", 0}), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(f, (core_str_t){"a\0b", 3}), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(f + 1u, LIT("fine")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(PORTS_FS_NO_FILE, LIT("fine")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(f + 1u, LIT("a/b")), PORTS_FS_E_ARG);
    CHECK_EQ_STR(t, kf_log(&g_kf), "T"); /* only the create reached the seam */
    CHECK_EQ_I64(t, commit(f, LIT("fine")), PORTS_FS_OK); /* control */
    CHECK_EQ_STR(t, kf_log(&g_kf), "TSLC");
    finish(t);
}

static void t_exists_keeps_the_temp_file_and_the_old_file(ctest_t *t) {
    begin(t);
    CHECK(t, kf_preload(&g_kf, "taken", "old", 3));
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, wr(f, "new"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(f, LIT("taken")), PORTS_FS_E_EXISTS);
    CHECK(t, kf_entry_is(&g_kf, "taken", "old", 3));
    CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 0);
    CHECK_EQ_U64(t, kf_open_tmp(&g_kf), 1);
    CHECK_EQ_I64(t, discard(f), PORTS_FS_OK); /* discard after a failed commit */
    CHECK_EQ_U64(t, kf_open_tmp(&g_kf), 0);
    CHECK_EQ_U64(t, kf_entry_count(&g_kf), 1);
    CHECK(t, kf_entry_is(&g_kf, "taken", "old", 3));
    CHECK_EQ_I64(t, commit(f, LIT("other")), PORTS_FS_E_ARG);
    finish(t);
}

static void t_repeated_failures_then_success(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, wr(f, "v"), PORTS_FS_OK);
    arm(KF_FSYNC, 1, EIO);
    kf_fail2(&g_kf, KF_LINK, g_kf.calls[KF_LINK] + 1u, ENOSPC);
    CHECK_EQ_I64(t, commit(f, LIT("n")), PORTS_FS_E_IO);      /* fsync fails */
    CHECK_EQ_I64(t, commit(f, LIT("n")), PORTS_FS_E_NOSPACE); /* fsync fine, link fails */
    CHECK_EQ_I64(t, commit(f, LIT("n")), PORTS_FS_OK);
    CHECK_EQ_STR(t, kf_log(&g_kf), "TPSSLSLC");
    CHECK(t, kf_entry_is(&g_kf, "n", "v", 1));
    finish(t);
}

static void t_close_failure_after_link_is_still_success(ctest_t *t) {
    static const int errs[] = {EIO, EINTR, EBADF, ENOSPC};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        begin(t);
        ports_fs_file_t f = mk(t);
        CHECK_EQ_I64(t, wr(f, "kept"), PORTS_FS_OK);
        arm(KF_CLOSE, 1, errs[i]);
        CHECK_EQ_I64(t, commit(f, LIT("k")), PORTS_FS_OK); /* the name is visible: the commit happened */
        CHECK(t, kf_entry_is(&g_kf, "k", "kept", 4));
        CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 1); /* never closed twice, never retried */
        CHECK_EQ_I64(t, commit(f, LIT("again")), PORTS_FS_E_ARG); /* the handle is consumed */
        ports_fs_file_t h[POSIX_FS_SLOTS];
        for (size_t k = 0; k < POSIX_FS_SLOTS; k++) { /* and its slot is free */
            CHECK_EQ_I64(t, create(&h[k]), PORTS_FS_OK);
        }
        finish(t);
    }
}

static void t_discard_only_closes(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, wr(f, "gone"), PORTS_FS_OK);
    kf_log_reset(&g_kf);
    CHECK_EQ_I64(t, discard(f), PORTS_FS_OK);
    CHECK_EQ_STR(t, kf_log(&g_kf), "C"); /* no fsync, no truncate, no link */
    CHECK_EQ_U64(t, kf_entry_count(&g_kf), 0);
    finish(t);
}

static void t_discard_close_errors(ctest_t *t) {
    static const int errs[] = {EIO, EBADF, ENOSPC, EDQUOT};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        begin(t);
        ports_fs_file_t f = mk(t);
        arm(KF_CLOSE, 1, errs[i]);
        CHECK_EQ_I64(t, discard(f), PORTS_FS_E_IO);
        CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 1);
        CHECK_EQ_I64(t, discard(f), PORTS_FS_E_ARG); /* consumed anyway */
        CHECK_EQ_I64(t, wr(f, "x"), PORTS_FS_E_ARG);
        CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 1);
        ports_fs_file_t h[POSIX_FS_SLOTS];
        for (size_t k = 0; k < POSIX_FS_SLOTS; k++) {
            CHECK_EQ_I64(t, create(&h[k]), PORTS_FS_OK); /* slot released */
        }
        finish(t);
    }
}

static void t_discard_eintr_counts_as_success_without_retry(ctest_t *t) {
    begin(t);
    ports_fs_file_t f = mk(t);
    arm(KF_CLOSE, 1, EINTR);
    CHECK_EQ_I64(t, discard(f), PORTS_FS_OK);
    CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 1);
    CHECK_EQ_U64(t, kf_open_tmp(&g_kf), 0);
    finish(t);
}

static void t_directory_descriptor_survives_commits(ctest_t *t) {
    begin(t);
    for (unsigned i = 0; i < 4u; i++) {
        ports_fs_file_t f = mk(t);
        char name[4] = {'f', (char)('0' + i), '\0', '\0'};
        CHECK_EQ_I64(t, commit(f, (core_str_t){name, 2}), PORTS_FS_OK);
    }
    CHECK_EQ_U64(t, g_kf.calls[KF_CLOSE], 4); /* temp descriptors only */
    CHECK_EQ_U64(t, kf_open_fds(&g_kf), 1);   /* the directory descriptor is still open */
    CHECK_EQ_U64(t, kf_entry_count(&g_kf), 4);
    finish(t);
}

static void t_directory_full_is_nospace_and_recoverable(ctest_t *t) {
    char name[4] = {'d', '0', '\0', '\0'};
    begin(t);
    for (unsigned i = 0; i < KF_ENTRIES; i++) {
        ports_fs_file_t f = mk(t);
        name[1] = (char)('a' + i);
        CHECK_EQ_I64(t, commit(f, (core_str_t){name, 2}), PORTS_FS_OK);
    }
    ports_fs_file_t f = mk(t);
    CHECK_EQ_I64(t, commit(f, LIT("overflow")), PORTS_FS_E_NOSPACE); /* the fake's link reports ENOSPC */
    CHECK_EQ_U64(t, kf_open_tmp(&g_kf), 1);
    CHECK_EQ_I64(t, discard(f), PORTS_FS_OK);
    finish(t);
}

CTEST_SUITE("posix_fs_commit",
    {"commit_syncs_before_the_name_appears", t_commit_syncs_before_the_name_appears},
    {"empty_file_is_synced_and_linked", t_empty_file_is_synced_and_linked},
    {"name_is_passed_as_an_exact_c_string", t_name_is_passed_as_an_exact_c_string},
    {"invalid_name_or_handle_makes_no_seam_call", t_invalid_name_or_handle_makes_no_seam_call},
    {"exists_keeps_the_temp_file_and_the_old_file", t_exists_keeps_the_temp_file_and_the_old_file},
    {"repeated_failures_then_success", t_repeated_failures_then_success},
    {"close_failure_after_link_is_still_success", t_close_failure_after_link_is_still_success},
    {"discard_only_closes", t_discard_only_closes},
    {"discard_close_errors", t_discard_close_errors},
    {"discard_eintr_counts_as_success_without_retry", t_discard_eintr_counts_as_success_without_retry},
    {"directory_descriptor_survives_commits", t_directory_descriptor_survives_commits},
    {"directory_full_is_nospace_and_recoverable", t_directory_full_is_nospace_and_recoverable})
