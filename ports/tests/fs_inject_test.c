/* Tests for ports/fakes.h: fs construction, preload and error injection ("at the Nth write"), through ports_fs_t. */
#include <stdint.h>
#include <string.h>

#include "ctest/ctest.h"
#include "ports/fakes.h"
#include "ports/ports.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define SLOT 64u

static unsigned char g_store[8u * SLOT];
static ports_fake_fs_t g_fs;

static ports_fs_t fresh(ctest_t *t) {
    CHECK(t, ports_fake_fs_init(&g_fs, g_store, sizeof g_store));
    return ports_fake_fs_port(&g_fs);
}

static ports_fs_err_t create(ports_fs_t p, ports_fs_file_t *f) { return p.ops->create_temp(p.ctx, f); }
static ports_fs_err_t wr(ports_fs_t p, ports_fs_file_t f, const char *s) { return p.ops->write(p.ctx, f, s, strlen(s)); }
static ports_fs_err_t commit(ports_fs_t p, ports_fs_file_t f, core_str_t n) { return p.ops->commit(p.ctx, f, n); }
static ports_fs_err_t discard(ports_fs_t p, ports_fs_file_t f) { return p.ops->discard(p.ctx, f); }

static ports_fs_file_t mk(ports_fs_t p, ctest_t *t) {
    ports_fs_file_t f = PORTS_FS_NO_FILE;
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_OK);
    return f;
}

static void t_init_rejects_bad_arguments(ctest_t *t) {
    static unsigned char small[16];
    CHECK(t, ports_fake_fs_init(&g_fs, g_store, sizeof g_store)); /* positive control */
    CHECK(t, !ports_fake_fs_init(NULL, g_store, sizeof g_store));
    CHECK(t, !ports_fake_fs_init(&g_fs, NULL, sizeof g_store));
    CHECK(t, !ports_fake_fs_init(&g_fs, small, PORTS_FAKE_FS_SLOTS - 1u));
    CHECK(t, ports_fake_fs_init(&g_fs, small, PORTS_FAKE_FS_SLOTS)); /* slot capacity 1 */
}

static void t_capacity_per_file_is_cap_divided_by_slots(ctest_t *t) {
    static unsigned char odd[8u * 12u + 7u]; /* 103 / 8 = 12 */
    CHECK(t, ports_fake_fs_init(&g_fs, odd, sizeof odd));
    ports_fs_t p = ports_fake_fs_port(&g_fs);
    ports_fs_file_t f = mk(p, t);
    CHECK_EQ_I64(t, wr(p, f, "123456789012"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, f, "x"), PORTS_FS_E_NOSPACE);
    ports_fs_file_t g = mk(p, t);
    CHECK_EQ_I64(t, wr(p, g, "1234567890123"), PORTS_FS_E_NOSPACE);
    CHECK_EQ_I64(t, wr(p, g, "123456789012"), PORTS_FS_OK);
}

static void t_slot_capacity_of_one_byte(ctest_t *t) {
    static unsigned char tiny[PORTS_FAKE_FS_SLOTS];
    CHECK(t, ports_fake_fs_init(&g_fs, tiny, sizeof tiny));
    ports_fs_t p = ports_fake_fs_port(&g_fs);
    ports_fs_file_t f = mk(p, t);
    CHECK_EQ_I64(t, wr(p, f, "ab"), PORTS_FS_E_NOSPACE);
    CHECK_EQ_I64(t, wr(p, f, "a"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, f, "b"), PORTS_FS_E_NOSPACE);
}

static void t_inject_create_nth_call_only(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = PORTS_FS_NO_FILE;
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_CREATE, 2, PORTS_FS_E_IO));
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_OK);
    ports_fs_file_t g = PORTS_FS_NO_FILE;
    CHECK_EQ_I64(t, create(p, &g), PORTS_FS_E_IO);
    CHECK_EQ_U64(t, g, 0); /* untouched */
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 1); /* no state change */
    CHECK_EQ_I64(t, create(p, &g), PORTS_FS_OK);          /* one-shot */
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 2);
    CHECK_EQ_I64(t, create(p, &g), PORTS_FS_OK);
}

static void t_inject_first_call(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = PORTS_FS_NO_FILE;
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_CREATE, 1, PORTS_FS_E_NOSPACE));
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_E_NOSPACE);
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_OK);
}

static void t_inject_write_at_nth_write(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    core_str_t c;
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_WRITE, 3, PORTS_FS_E_NOSPACE));
    CHECK_EQ_I64(t, wr(p, f, "one"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, f, "two"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, f, "three"), PORTS_FS_E_NOSPACE);
    CHECK(t, ports_fake_fs_temp_content(&g_fs, f, &c));
    CHECK(t, core_str_eq(c, LIT("onetwo"))); /* the failed write left no bytes */
    CHECK_EQ_I64(t, wr(p, f, "four"), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_temp_content(&g_fs, f, &c));
    CHECK(t, core_str_eq(c, LIT("onetwofour")));
}

static void t_inject_write_counts_across_files_and_ignores_other_ops(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t a = mk(p, t);
    ports_fs_file_t b = mk(p, t); /* creates do not count as writes */
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_WRITE, 2, PORTS_FS_E_IO));
    CHECK_EQ_I64(t, wr(p, a, "x"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, b, "y"), PORTS_FS_E_IO); /* second write call overall */
    CHECK_EQ_I64(t, wr(p, b, "y"), PORTS_FS_OK);
}

static void t_inject_commit_keeps_temp_valid_for_retry(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    core_str_t c;
    CHECK_EQ_I64(t, wr(p, f, "body"), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_COMMIT, 1, PORTS_FS_E_IO));
    CHECK_EQ_I64(t, commit(p, f, LIT("r.bin")), PORTS_FS_E_IO);
    CHECK(t, !ports_fake_fs_find(&g_fs, LIT("r.bin"), NULL));
    CHECK_EQ_U64(t, ports_fake_fs_commits(&g_fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 1);
    CHECK_EQ_I64(t, commit(p, f, LIT("r.bin")), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("r.bin"), &c) && core_str_eq(c, LIT("body")));
    CHECK_EQ_U64(t, ports_fake_fs_commits(&g_fs), 1);
}

static void t_inject_commit_second_call_and_exists_code(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t a = mk(p, t);
    ports_fs_file_t b = mk(p, t);
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_COMMIT, 2, PORTS_FS_E_EXISTS));
    CHECK_EQ_I64(t, commit(p, a, LIT("a")), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, b, LIT("b")), PORTS_FS_E_EXISTS); /* injected although "b" is free */
    CHECK(t, !ports_fake_fs_find(&g_fs, LIT("b"), NULL));
    CHECK_EQ_I64(t, commit(p, b, LIT("b")), PORTS_FS_OK);
}

static void t_inject_discard_keeps_the_handle(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_DISCARD, 1, PORTS_FS_E_IO));
    CHECK_EQ_I64(t, discard(p, f), PORTS_FS_E_IO);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 1);
    CHECK_EQ_U64(t, ports_fake_fs_discards(&g_fs), 0);
    CHECK_EQ_I64(t, discard(p, f), PORTS_FS_OK);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_discards(&g_fs), 1);
}

static void t_inject_counts_failed_calls_and_wins_over_arg_errors(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_WRITE, 2, PORTS_FS_E_NOSPACE));
    CHECK_EQ_I64(t, wr(p, 999u, "x"), PORTS_FS_E_ARG);      /* call 1: a failed call still counts */
    CHECK_EQ_I64(t, wr(p, 999u, "x"), PORTS_FS_E_NOSPACE);  /* call 2: the injection wins */
    CHECK_EQ_I64(t, wr(p, f, "x"), PORTS_FS_OK);
}

static void t_inject_cancel_with_zero(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_WRITE, 1, PORTS_FS_E_IO));
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_WRITE, 0, PORTS_FS_E_IO));
    CHECK_EQ_I64(t, wr(p, f, "x"), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_WRITE, 2, PORTS_FS_E_IO)); /* control: armed again */
    CHECK_EQ_I64(t, wr(p, f, "x"), PORTS_FS_E_IO);
}

static void t_inject_second_replaces_first(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_WRITE, 1, PORTS_FS_E_IO));
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_WRITE, 2, PORTS_FS_E_NOSPACE));
    CHECK_EQ_I64(t, wr(p, f, "x"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, f, "x"), PORTS_FS_E_NOSPACE);
}

static void t_inject_different_ops_are_independent(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = PORTS_FS_NO_FILE;
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_CREATE, 1, PORTS_FS_E_IO));
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_COMMIT, 1, PORTS_FS_E_NOSPACE));
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_E_IO);
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, f, "x"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, f, LIT("n")), PORTS_FS_E_NOSPACE);
    CHECK_EQ_I64(t, commit(p, f, LIT("n")), PORTS_FS_OK);
}

static void t_inject_rejects_bad_arguments(ctest_t *t) {
    (void)fresh(t);
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_DISCARD, 1, PORTS_FS_E_IO)); /* positive control */
    CHECK(t, !ports_fake_fs_inject(NULL, PORTS_FAKE_FS_OP_WRITE, 1, PORTS_FS_E_IO));
    CHECK(t, !ports_fake_fs_inject(&g_fs, (ports_fake_fs_op_t)4, 1, PORTS_FS_E_IO));
    CHECK(t, !ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_WRITE, 1, PORTS_FS_OK));
}

static void t_preload_makes_a_visible_file(ctest_t *t) {
    ports_fs_t p = fresh(t);
    core_str_t c;
    CHECK(t, ports_fake_fs_preload(&g_fs, LIT("old.pdf"), LIT("old bytes")));
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("old.pdf"), &c) && core_str_eq(c, LIT("old bytes")));
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 1);
    CHECK_EQ_U64(t, ports_fake_fs_commits(&g_fs), 0); /* preload is not a commit call */
    ports_fs_file_t f = mk(p, t);
    CHECK_EQ_I64(t, commit(p, f, LIT("old.pdf")), PORTS_FS_E_EXISTS);
}

static void t_preload_rejections_with_positive_controls(ctest_t *t) {
    (void)fresh(t);
    static const char big[SLOT + 2u] = "01234567890123456789012345678901234567890123456789012345678901234";
    CHECK(t, !ports_fake_fs_preload(&g_fs, LIT(""), LIT("x")));
    CHECK(t, !ports_fake_fs_preload(&g_fs, LIT("a/b"), LIT("x")));
    CHECK(t, !ports_fake_fs_preload(&g_fs, LIT(".."), LIT("x")));
    CHECK(t, !ports_fake_fs_preload(&g_fs, LIT("big"), (core_str_t){big, SLOT + 1u}));
    CHECK(t, !ports_fake_fs_preload(NULL, LIT("n"), LIT("x")));
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 0);
    CHECK(t, ports_fake_fs_preload(&g_fs, LIT("big"), (core_str_t){big, SLOT})); /* exactly slot_cap fits */
    CHECK(t, !ports_fake_fs_preload(&g_fs, LIT("big"), LIT("x")));                /* duplicate name */
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 1);
}

static void t_preload_needs_a_free_slot(ctest_t *t) {
    ports_fs_t p = fresh(t);
    for (size_t i = 0; i < PORTS_FAKE_FS_SLOTS; i++) {
        (void)mk(p, t);
    }
    CHECK(t, !ports_fake_fs_preload(&g_fs, LIT("n"), LIT("x")));
    ports_fs_file_t f = PORTS_FS_NO_FILE;
    CHECK_EQ_I64(t, create(p, &f), PORTS_FS_E_NOSPACE); /* all slots in use */
    CHECK_EQ_U64(t, f, 0);
}

static void t_init_resets_everything(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    CHECK_EQ_I64(t, commit(p, f, LIT("x")), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_inject(&g_fs, PORTS_FAKE_FS_OP_CREATE, 1, PORTS_FS_E_IO));
    p = fresh(t);
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_commits(&g_fs), 0);
    CHECK(t, !ports_fake_fs_find(&g_fs, LIT("x"), NULL));
    (void)mk(p, t); /* the old injection is gone */
}

CTEST_SUITE("fs_inject",
    {"init_rejects_bad_arguments", t_init_rejects_bad_arguments},
    {"capacity_per_file_is_cap_divided_by_slots", t_capacity_per_file_is_cap_divided_by_slots},
    {"slot_capacity_of_one_byte", t_slot_capacity_of_one_byte},
    {"inject_create_nth_call_only", t_inject_create_nth_call_only},
    {"inject_first_call", t_inject_first_call},
    {"inject_write_at_nth_write", t_inject_write_at_nth_write},
    {"inject_write_counts_across_files_and_ignores_other_ops", t_inject_write_counts_across_files_and_ignores_other_ops},
    {"inject_commit_keeps_temp_valid_for_retry", t_inject_commit_keeps_temp_valid_for_retry},
    {"inject_commit_second_call_and_exists_code", t_inject_commit_second_call_and_exists_code},
    {"inject_discard_keeps_the_handle", t_inject_discard_keeps_the_handle},
    {"inject_counts_failed_calls_and_wins_over_arg_errors", t_inject_counts_failed_calls_and_wins_over_arg_errors},
    {"inject_cancel_with_zero", t_inject_cancel_with_zero},
    {"inject_second_replaces_first", t_inject_second_replaces_first},
    {"inject_different_ops_are_independent", t_inject_different_ops_are_independent},
    {"inject_rejects_bad_arguments", t_inject_rejects_bad_arguments},
    {"preload_makes_a_visible_file", t_preload_makes_a_visible_file},
    {"preload_rejections_with_positive_controls", t_preload_rejections_with_positive_controls},
    {"preload_needs_a_free_slot", t_preload_needs_a_free_slot},
    {"init_resets_everything", t_init_resets_everything})
