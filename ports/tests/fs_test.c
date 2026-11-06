/* Tests for ports/fakes.h: the in-memory fs, used only through ports_fs_t (plus the fake's inspection API). */
#include <stdint.h>
#include <string.h>

#include "ctest/ctest.h"
#include "ports/fakes.h"
#include "ports/ports.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define SLOT 64u

static unsigned char g_store[8u * SLOT];
static ports_fake_fs_t g_fs;
static char g_name[300];

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
    CHECK(t, f != PORTS_FS_NO_FILE);
    return f;
}

static core_str_t name_of_len(size_t n) {
    memset(g_name, 'n', n);
    return (core_str_t){g_name, n};
}

static void t_error_codes_distinct(ctest_t *t) {
    (void)fresh(t); /* the fake must at least initialise */
    CHECK_EQ_I64(t, PORTS_FS_OK, 0);
    CHECK(t, PORTS_FS_E_IO != PORTS_FS_OK && PORTS_FS_E_EXISTS != PORTS_FS_OK);
    CHECK(t, PORTS_FS_E_NOSPACE != PORTS_FS_OK && PORTS_FS_E_ARG != PORTS_FS_OK);
    CHECK(t, PORTS_FS_E_IO != PORTS_FS_E_EXISTS && PORTS_FS_E_IO != PORTS_FS_E_NOSPACE);
    CHECK(t, PORTS_FS_E_IO != PORTS_FS_E_ARG && PORTS_FS_E_EXISTS != PORTS_FS_E_NOSPACE);
    CHECK(t, PORTS_FS_E_EXISTS != PORTS_FS_E_ARG && PORTS_FS_E_NOSPACE != PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, PORTS_FS_NO_FILE, 0);
}

static void t_port_is_a_const_table_with_this_context(ctest_t *t) {
    ports_fs_t p = fresh(t);
    CHECK(t, p.ops != NULL && p.ops->create_temp != NULL && p.ops->write != NULL);
    CHECK(t, p.ops->commit != NULL && p.ops->discard != NULL);
    CHECK(t, p.ctx == (void *)&g_fs);
}

static void t_create_gives_distinct_nonzero_handles(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t a = mk(p, t);
    ports_fs_file_t b = mk(p, t);
    ports_fs_file_t c = mk(p, t);
    CHECK(t, a != b && b != c && a != c);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 3);
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 0);
}

static void t_new_temp_is_empty_and_invisible(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    core_str_t c = {"x", 1};
    CHECK(t, ports_fake_fs_temp_content(&g_fs, f, &c));
    CHECK_EQ_U64(t, c.len, 0);
    CHECK(t, !ports_fake_fs_find(&g_fs, LIT("a"), NULL));
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 0);
}

static void t_write_appends_and_is_invisible_before_commit(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    core_str_t c;
    CHECK_EQ_I64(t, wr(p, f, "abc"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, f, "de"), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_temp_content(&g_fs, f, &c));
    CHECK(t, core_str_eq(c, LIT("abcde")));
    CHECK(t, !ports_fake_fs_find(&g_fs, LIT("book.pdf"), NULL));
    CHECK(t, !ports_fake_fs_find(&g_fs, LIT("abcde"), NULL));
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_commits(&g_fs), 0);
}

static void t_write_zero_length_is_ok_noop(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    core_str_t c;
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, NULL, 0), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, f, "z"), PORTS_FS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, "ignored", 0), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_temp_content(&g_fs, f, &c));
    CHECK(t, core_str_eq(c, LIT("z")));
}

static void t_commit_makes_content_visible_and_consumes_handle(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    core_str_t c = {NULL, 0};
    CHECK_EQ_I64(t, wr(p, f, "%PDF-1.7 data"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, f, LIT("Dune.pdf")), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("Dune.pdf"), &c));
    CHECK(t, core_str_eq(c, LIT("%PDF-1.7 data")));
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("Dune.pdf"), NULL));
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 1);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_commits(&g_fs), 1);
    CHECK_EQ_U64(t, ports_fake_fs_discards(&g_fs), 0);
    CHECK(t, !ports_fake_fs_temp_content(&g_fs, f, &c));
}

static void t_stale_handle_after_commit_is_arg_error(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    core_str_t c;
    CHECK_EQ_I64(t, wr(p, f, "keep"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, f, LIT("a.bin")), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, f, "more"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, LIT("b.bin")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, discard(p, f), PORTS_FS_E_ARG);
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("a.bin"), &c));
    CHECK(t, core_str_eq(c, LIT("keep"))); /* the committed file was not touched */
    CHECK(t, !ports_fake_fs_find(&g_fs, LIT("b.bin"), NULL));
    CHECK_EQ_U64(t, ports_fake_fs_discards(&g_fs), 0);
}

static void t_discard_leaves_nothing(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    core_str_t c;
    CHECK_EQ_I64(t, wr(p, f, "partial"), PORTS_FS_OK);
    CHECK_EQ_I64(t, discard(p, f), PORTS_FS_OK);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_discards(&g_fs), 1);
    CHECK_EQ_U64(t, ports_fake_fs_commits(&g_fs), 0);
    CHECK(t, !ports_fake_fs_temp_content(&g_fs, f, &c));
    CHECK_EQ_I64(t, wr(p, f, "x"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, LIT("p.bin")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, discard(p, f), PORTS_FS_E_ARG);
    CHECK(t, !ports_fake_fs_find(&g_fs, LIT("p.bin"), NULL));
}

static void t_commit_refuses_existing_name_and_keeps_temp(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t a = mk(p, t);
    ports_fs_file_t b = mk(p, t);
    core_str_t c;
    CHECK_EQ_I64(t, wr(p, a, "first"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, b, "second"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, a, LIT("book.epub")), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, b, LIT("book.epub")), PORTS_FS_E_EXISTS);
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("book.epub"), &c));
    CHECK(t, core_str_eq(c, LIT("first"))); /* never overwritten */
    CHECK_EQ_U64(t, ports_fake_fs_commits(&g_fs), 1);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 1); /* b is still an open temp file */
    CHECK(t, ports_fake_fs_temp_content(&g_fs, b, &c));
    CHECK(t, core_str_eq(c, LIT("second")));
    /* the caller can still pick another name (positive control) or discard */
    CHECK_EQ_I64(t, commit(p, b, LIT("book (1).epub")), PORTS_FS_OK);
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 2);
}

static void t_exists_then_discard_leaves_only_the_original(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t a = mk(p, t);
    CHECK(t, ports_fake_fs_preload(&g_fs, LIT("x.bin"), LIT("old")));
    CHECK_EQ_I64(t, wr(p, a, "new"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, a, LIT("x.bin")), PORTS_FS_E_EXISTS);
    CHECK_EQ_I64(t, discard(p, a), PORTS_FS_OK);
    core_str_t c;
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("x.bin"), &c));
    CHECK(t, core_str_eq(c, LIT("old")));
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 1);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 0);
}

static void t_names_are_case_sensitive(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t a = mk(p, t);
    ports_fs_file_t b = mk(p, t);
    CHECK_EQ_I64(t, commit(p, a, LIT("Name")), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, b, LIT("name")), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("Name"), NULL));
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("name"), NULL));
    CHECK(t, !ports_fake_fs_find(&g_fs, LIT("NAME"), NULL));
    CHECK(t, !ports_fake_fs_find(&g_fs, LIT("Nam"), NULL));
}

static void t_invalid_names_are_arg_errors_and_keep_the_temp(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    static const char nul_name[] = {'a', '\0', 'b'};
    CHECK_EQ_I64(t, commit(p, f, (core_str_t){NULL, 0}), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, LIT("")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, LIT("a/b")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, LIT("/abs")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, LIT("dir/")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, LIT("../x")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, LIT(".")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, LIT("..")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, (core_str_t){nul_name, 3}), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, (core_str_t){NULL, 3}), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, f, name_of_len(256)), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 1);
    CHECK_EQ_U64(t, ports_fake_fs_commits(&g_fs), 0);
    CHECK_EQ_I64(t, commit(p, f, name_of_len(255)), PORTS_FS_OK); /* positive control: the longest name */
    CHECK(t, ports_fake_fs_find(&g_fs, name_of_len(255), NULL));
}

static void t_names_that_look_odd_but_are_plain_are_accepted(ctest_t *t) {
    ports_fs_t p = fresh(t);
    static const char *const names[] = {"...", "..x", "x..", ".hidden", "a b.pdf", "\xd0\x9a\xd0\xbd\xd0\xb8\xd0\xb3\xd0\xb0.epub", "1"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        ports_fs_file_t f = mk(p, t);
        CHECK_EQ_I64(t, commit(p, f, core_str_from_cstr(names[i])), PORTS_FS_OK);
        CHECK(t, ports_fake_fs_find(&g_fs, core_str_from_cstr(names[i]), NULL));
    }
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), 7);
}

static void t_unknown_handles_are_arg_errors(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    ports_fs_file_t bogus = f + 1000u;
    CHECK_EQ_I64(t, wr(p, PORTS_FS_NO_FILE, "x"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, wr(p, bogus, "x"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, PORTS_FS_NO_FILE, LIT("n")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(p, bogus, LIT("n")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, discard(p, PORTS_FS_NO_FILE), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, discard(p, bogus), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 1);
    CHECK_EQ_I64(t, wr(p, f, "x"), PORTS_FS_OK); /* positive control */
}

static void t_null_arguments_are_arg_errors(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    core_str_t c;
    CHECK_EQ_I64(t, create(p, NULL), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 1); /* nothing was created */
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, NULL, 4), PORTS_FS_E_ARG);
    CHECK(t, ports_fake_fs_temp_content(&g_fs, f, &c));
    CHECK_EQ_U64(t, c.len, 0);
    CHECK_EQ_I64(t, wr(p, f, "okay"), PORTS_FS_OK); /* positive control */
}

static void t_write_is_all_or_nothing_at_capacity(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    static const char big[SLOT + 1u] = "0123456789012345678901234567890123456789012345678901234567890123";
    core_str_t c;
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, big, 40), PORTS_FS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, big, 25), PORTS_FS_E_NOSPACE); /* 40 + 25 > 64 */
    CHECK(t, ports_fake_fs_temp_content(&g_fs, f, &c));
    CHECK_EQ_U64(t, c.len, 40); /* nothing from the failed write */
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, big, 24), PORTS_FS_OK); /* exactly full */
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, big, 1), PORTS_FS_E_NOSPACE);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, NULL, 0), PORTS_FS_OK); /* zero bytes still fit */
    CHECK(t, ports_fake_fs_temp_content(&g_fs, f, &c));
    CHECK_EQ_U64(t, c.len, 64);
    CHECK_EQ_I64(t, commit(p, f, LIT("full.bin")), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("full.bin"), &c) && c.len == 64);
}

static void t_slots_run_out_and_discard_frees_one(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f[PORTS_FAKE_FS_SLOTS];
    ports_fs_file_t extra = PORTS_FS_NO_FILE;
    for (size_t i = 0; i < PORTS_FAKE_FS_SLOTS; i++) {
        f[i] = mk(p, t);
    }
    CHECK_EQ_I64(t, create(p, &extra), PORTS_FS_E_NOSPACE);
    CHECK_EQ_U64(t, extra, 0); /* untouched on failure */
    CHECK_EQ_I64(t, discard(p, f[3]), PORTS_FS_OK);
    CHECK_EQ_I64(t, create(p, &extra), PORTS_FS_OK);
    CHECK(t, extra != PORTS_FS_NO_FILE);
}

static void t_committed_files_keep_their_slot(ctest_t *t) {
    ports_fs_t p = fresh(t);
    char nm[2] = {'a', 0};
    ports_fs_file_t extra = PORTS_FS_NO_FILE;
    for (size_t i = 0; i < PORTS_FAKE_FS_SLOTS; i++) {
        ports_fs_file_t f = mk(p, t);
        CHECK_EQ_I64(t, commit(p, f, (core_str_t){nm, 1}), PORTS_FS_OK);
        nm[0]++;
    }
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&g_fs), PORTS_FAKE_FS_SLOTS);
    CHECK_EQ_I64(t, create(p, &extra), PORTS_FS_E_NOSPACE);
}

static void t_handles_are_never_reused(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t a = mk(p, t);
    CHECK_EQ_I64(t, discard(p, a), PORTS_FS_OK);
    ports_fs_file_t b = mk(p, t);
    CHECK(t, a != b);
    CHECK_EQ_I64(t, wr(p, a, "x"), PORTS_FS_E_ARG); /* the old handle does not alias the new file */
    CHECK_EQ_I64(t, wr(p, b, "x"), PORTS_FS_OK);
}

static void t_files_are_independent(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t a = mk(p, t);
    ports_fs_file_t b = mk(p, t);
    core_str_t c;
    CHECK_EQ_I64(t, wr(p, a, "AA"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, b, "bb"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, a, "AA"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, b, LIT("b")), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(p, a, "!"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(p, a, LIT("a")), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("a"), &c) && core_str_eq(c, LIT("AAAA!")));
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("b"), &c) && core_str_eq(c, LIT("bb")));
}

static void t_empty_file_can_be_committed(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    core_str_t c = {"x", 1};
    CHECK_EQ_I64(t, commit(p, f, LIT("empty")), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_find(&g_fs, LIT("empty"), &c));
    CHECK_EQ_U64(t, c.len, 0);
}

static void t_inspection_api_rejects_bad_arguments(ctest_t *t) {
    ports_fs_t p = fresh(t);
    ports_fs_file_t f = mk(p, t);
    CHECK(t, ports_fake_fs_temp_content(&g_fs, f, NULL)); /* NULL content is allowed */
    CHECK(t, !ports_fake_fs_temp_content(&g_fs, PORTS_FS_NO_FILE, NULL));
    CHECK(t, !ports_fake_fs_find(&g_fs, LIT(""), NULL));
    CHECK(t, !ports_fake_fs_find(&g_fs, (core_str_t){NULL, 0}, NULL));
}

CTEST_SUITE("fs",
    {"error_codes_distinct", t_error_codes_distinct},
    {"port_is_a_const_table_with_this_context", t_port_is_a_const_table_with_this_context},
    {"create_gives_distinct_nonzero_handles", t_create_gives_distinct_nonzero_handles},
    {"new_temp_is_empty_and_invisible", t_new_temp_is_empty_and_invisible},
    {"write_appends_and_is_invisible_before_commit", t_write_appends_and_is_invisible_before_commit},
    {"write_zero_length_is_ok_noop", t_write_zero_length_is_ok_noop},
    {"commit_makes_content_visible_and_consumes_handle", t_commit_makes_content_visible_and_consumes_handle},
    {"stale_handle_after_commit_is_arg_error", t_stale_handle_after_commit_is_arg_error},
    {"discard_leaves_nothing", t_discard_leaves_nothing},
    {"commit_refuses_existing_name_and_keeps_temp", t_commit_refuses_existing_name_and_keeps_temp},
    {"exists_then_discard_leaves_only_the_original", t_exists_then_discard_leaves_only_the_original},
    {"names_are_case_sensitive", t_names_are_case_sensitive},
    {"invalid_names_are_arg_errors_and_keep_the_temp", t_invalid_names_are_arg_errors_and_keep_the_temp},
    {"names_that_look_odd_but_are_plain_are_accepted", t_names_that_look_odd_but_are_plain_are_accepted},
    {"unknown_handles_are_arg_errors", t_unknown_handles_are_arg_errors},
    {"null_arguments_are_arg_errors", t_null_arguments_are_arg_errors},
    {"write_is_all_or_nothing_at_capacity", t_write_is_all_or_nothing_at_capacity},
    {"slots_run_out_and_discard_frees_one", t_slots_run_out_and_discard_frees_one},
    {"committed_files_keep_their_slot", t_committed_files_keep_their_slot},
    {"handles_are_never_reused", t_handles_are_never_reused},
    {"files_are_independent", t_files_are_independent},
    {"empty_file_can_be_committed", t_empty_file_can_be_committed},
    {"inspection_api_rejects_bad_arguments", t_inspection_api_rejects_bad_arguments})
