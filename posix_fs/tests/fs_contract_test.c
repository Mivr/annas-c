/* posix_fs: the ports.h fs contract, one set of cases run twice: over the fake seam and over the real libc table in
 * a fresh directory. Every case ends with kit_env_end, which also proves no descriptor and no file is left behind. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include "posix_fs/posix_fs.h"
#include "posix_fs/tests/kit.h"
#include "ports/ports.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define SENTINEL ((ports_fs_file_t)0xDEADu)

static kf_t g_kf;
static kit_fs_env_t g_env;
static unsigned char g_buf[KF_CAP];

static ports_fs_err_t create(kit_fs_env_t *e, ports_fs_file_t *f) { return e->fs.ops->create_temp(e->fs.ctx, f); }
static ports_fs_err_t wr(kit_fs_env_t *e, ports_fs_file_t f, const char *s) {
    return e->fs.ops->write(e->fs.ctx, f, s, strlen(s));
}
static ports_fs_err_t commit(kit_fs_env_t *e, ports_fs_file_t f, core_str_t n) {
    return e->fs.ops->commit(e->fs.ctx, f, n);
}
static ports_fs_err_t discard(kit_fs_env_t *e, ports_fs_file_t f) { return e->fs.ops->discard(e->fs.ctx, f); }

static ports_fs_file_t mk(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t f = PORTS_FS_NO_FILE;
    CHECK_EQ_I64(t, create(e, &f), PORTS_FS_OK);
    CHECK(t, f != PORTS_FS_NO_FILE);
    return f;
}

static bool has(kit_fs_env_t *e, const char *name, const char *want) {
    size_t n = 0;
    return e->read(e, name, g_buf, sizeof g_buf, &n) && n == strlen(want) && memcmp(g_buf, want, n) == 0;
}

/* ---- cases -------------------------------------------------------------------------------------------- */

static void c_roundtrip_two_writes(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t f = mk(t, e);
    CHECK_EQ_I64(t, wr(e, f, "hello "), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(e, f, "world"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, f, LIT("book.pdf")), PORTS_FS_OK);
    CHECK_EQ_U64(t, e->count(e), 1);
    CHECK(t, has(e, "book.pdf", "hello world"));
}

static void c_temp_is_invisible(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t a = mk(t, e);
    ports_fs_file_t b = mk(t, e);
    CHECK_EQ_I64(t, wr(e, a, "aaa"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(e, b, "bbb"), PORTS_FS_OK);
    CHECK_EQ_U64(t, e->count(e), 0);
    CHECK_EQ_I64(t, commit(e, a, LIT("a.bin")), PORTS_FS_OK);
    CHECK_EQ_U64(t, e->count(e), 1);
    CHECK_EQ_I64(t, discard(e, b), PORTS_FS_OK);
    CHECK_EQ_U64(t, e->count(e), 1); /* the discarded one never appears */
    CHECK(t, has(e, "a.bin", "aaa"));
}

static void c_empty_file_commits(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t f = mk(t, e);
    CHECK_EQ_I64(t, commit(e, f, LIT("empty")), PORTS_FS_OK);
    CHECK_EQ_U64(t, e->count(e), 1);
    CHECK(t, has(e, "empty", ""));
}

static void c_zero_length_write_is_a_noop(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t f = mk(t, e);
    CHECK_EQ_I64(t, e->fs.ops->write(e->fs.ctx, f, NULL, 0), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(e, f, "a"), PORTS_FS_OK);
    CHECK_EQ_I64(t, e->fs.ops->write(e->fs.ctx, f, "zz", 0), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(e, f, "b"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, f, LIT("z")), PORTS_FS_OK);
    CHECK(t, has(e, "z", "ab"));
}

static void c_commit_never_overwrites(ctest_t *t, kit_fs_env_t *e) {
    CHECK(t, e->put(e, "x", "old", 3));
    CHECK_EQ_U64(t, e->count(e), 1);
    ports_fs_file_t f = mk(t, e);
    CHECK_EQ_I64(t, wr(e, f, "new"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, f, LIT("x")), PORTS_FS_E_EXISTS);
    CHECK(t, has(e, "x", "old"));
    CHECK_EQ_U64(t, e->count(e), 1);
    CHECK_EQ_I64(t, commit(e, f, LIT("y")), PORTS_FS_OK); /* the handle is still valid: positive control */
    CHECK(t, has(e, "y", "new"));
    CHECK(t, has(e, "x", "old"));
    ports_fs_file_t g = mk(t, e);
    CHECK_EQ_I64(t, wr(e, g, "third"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, g, LIT("y")), PORTS_FS_E_EXISTS); /* also against a name we committed ourselves */
    CHECK(t, has(e, "y", "new"));
    CHECK_EQ_I64(t, discard(e, g), PORTS_FS_OK);
    CHECK_EQ_U64(t, e->count(e), 2);
}

static void c_discard_leaves_nothing(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t f = mk(t, e);
    CHECK_EQ_I64(t, wr(e, f, "data"), PORTS_FS_OK);
    CHECK_EQ_I64(t, discard(e, f), PORTS_FS_OK);
    CHECK_EQ_U64(t, e->count(e), 0);
    CHECK_EQ_I64(t, discard(e, f), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, wr(e, f, "x"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(e, f, LIT("n")), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, e->count(e), 0);
}

static void c_handles_are_independent(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t a = mk(t, e);
    ports_fs_file_t b = mk(t, e);
    ports_fs_file_t c = mk(t, e);
    CHECK(t, a != b && b != c && a != c);
    CHECK_EQ_I64(t, wr(e, a, "A1"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(e, b, "B1"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(e, c, "C1"), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(e, a, "A2"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, c, LIT("c")), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, a, LIT("a")), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(e, b, "B2"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, b, LIT("b")), PORTS_FS_OK);
    CHECK(t, has(e, "a", "A1A2") && has(e, "b", "B1B2") && has(e, "c", "C1"));
    CHECK_EQ_U64(t, e->count(e), 3);
}

static void c_committed_handle_is_stale(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t f = mk(t, e);
    CHECK_EQ_I64(t, wr(e, f, "one"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, f, LIT("one.bin")), PORTS_FS_OK);
    CHECK_EQ_I64(t, wr(e, f, "more"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(e, f, LIT("two.bin")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, discard(e, f), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, e->count(e), 1);
    CHECK(t, has(e, "one.bin", "one")); /* a stale write did not touch the committed file */
    ports_fs_file_t g = mk(t, e);       /* positive control */
    CHECK_EQ_I64(t, wr(e, g, "ok"), PORTS_FS_OK);
    CHECK_EQ_I64(t, discard(e, g), PORTS_FS_OK);
}

static void c_zero_and_unknown_handles(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t f = mk(t, e);
    CHECK_EQ_I64(t, wr(e, PORTS_FS_NO_FILE, "x"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(e, PORTS_FS_NO_FILE, LIT("n")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, discard(e, PORTS_FS_NO_FILE), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, wr(e, f + 1000u, "x"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, commit(e, f + 1000u, LIT("n")), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, discard(e, f + 1000u), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, wr(e, 0xFFFFFFFFu, "x"), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, e->fs.ops->write(e->fs.ctx, PORTS_FS_NO_FILE, NULL, 0), PORTS_FS_E_ARG); /* even for len 0 */
    CHECK_EQ_I64(t, wr(e, f, "x"), PORTS_FS_OK); /* the real handle is unharmed */
    CHECK_EQ_I64(t, discard(e, f), PORTS_FS_OK);
}

static void commit_bad_name(ctest_t *t, kit_fs_env_t *e, core_str_t name) {
    static unsigned serial;
    char good[16];
    size_t before = e->count(e);
    ports_fs_file_t f = mk(t, e);
    CHECK_EQ_I64(t, wr(e, f, "body"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, f, name), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, e->count(e), before);
    (void)snprintf(good, sizeof good, "good%u", serial++);
    CHECK_EQ_I64(t, commit(e, f, (core_str_t){good, strlen(good)}), PORTS_FS_OK); /* the temp file stayed valid */
    CHECK_EQ_U64(t, e->count(e), before + 1u);
    CHECK(t, has(e, good, "body"));
    CHECK_EQ_I64(t, e->fs.ops->discard(e->fs.ctx, f), PORTS_FS_E_ARG);
}

static void c_invalid_names_rejected_with_controls(ctest_t *t, kit_fs_env_t *e) {
    commit_bad_name(t, e, (core_str_t){"", 0});
    commit_bad_name(t, e, (core_str_t){NULL, 0});
    commit_bad_name(t, e, LIT("."));
    commit_bad_name(t, e, LIT(".."));
    commit_bad_name(t, e, LIT("a/b"));
    commit_bad_name(t, e, LIT("/abs"));
    commit_bad_name(t, e, LIT("dir/"));
    commit_bad_name(t, e, LIT("../x"));
    commit_bad_name(t, e, (core_str_t){"a\0b", 3});
    commit_bad_name(t, e, (core_str_t){"\0", 1});
    commit_bad_name(t, e, (core_str_t){NULL, 3});
}

static void c_valid_names_accepted(ctest_t *t, kit_fs_env_t *e) {
    static const char *const names[] = {"...", ".hidden", "a b", "x.", "..x", "-rf", "e\xc3\xa9.pdf", "a\nb"};
    static char long255[256];
    static char long256[257];
    memset(long255, 'n', 255);
    memset(long256, 'n', 256);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        ports_fs_file_t f = mk(t, e);
        CHECK_EQ_I64(t, commit(e, f, (core_str_t){names[i], strlen(names[i])}), PORTS_FS_OK);
    }
    ports_fs_file_t f = mk(t, e);
    CHECK_EQ_I64(t, commit(e, f, (core_str_t){long256, 256}), PORTS_FS_E_ARG); /* one too long */
    CHECK_EQ_I64(t, commit(e, f, (core_str_t){long255, 255}), PORTS_FS_OK);    /* longest allowed */
    CHECK_EQ_U64(t, e->count(e), sizeof names / sizeof names[0] + 1u);
    CHECK(t, has(e, "...", "") && has(e, "a b", ""));
}

static void c_slot_pool_is_finite_and_recycles(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t h[POSIX_FS_SLOTS];
    for (size_t i = 0; i < POSIX_FS_SLOTS; i++) {
        h[i] = mk(t, e);
    }
    ports_fs_file_t extra = SENTINEL;
    CHECK_EQ_I64(t, create(e, &extra), PORTS_FS_E_NOSPACE);
    CHECK_EQ_U64(t, extra, SENTINEL); /* untouched on failure */
    CHECK_EQ_I64(t, discard(e, h[3]), PORTS_FS_OK);
    CHECK_EQ_I64(t, create(e, &extra), PORTS_FS_OK); /* one slot is free again */
    CHECK(t, extra != SENTINEL && extra != PORTS_FS_NO_FILE);
    ports_fs_file_t more = SENTINEL;
    CHECK_EQ_I64(t, create(e, &more), PORTS_FS_E_NOSPACE);
    CHECK_EQ_I64(t, wr(e, h[0], "z"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, h[0], LIT("z")), PORTS_FS_OK); /* a commit frees a slot too */
    CHECK_EQ_I64(t, create(e, &more), PORTS_FS_OK);
    for (size_t i = 1; i < POSIX_FS_SLOTS; i++) {
        if (i != 3u) {
            CHECK_EQ_I64(t, discard(e, h[i]), PORTS_FS_OK);
        }
    }
    CHECK_EQ_I64(t, discard(e, extra), PORTS_FS_OK);
    CHECK_EQ_I64(t, discard(e, more), PORTS_FS_OK);
    CHECK_EQ_U64(t, e->count(e), 1);
}

static void c_null_arguments(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t f = mk(t, e);
    CHECK_EQ_I64(t, create(e, NULL), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, e->fs.ops->write(e->fs.ctx, f, NULL, 4), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, wr(e, f, "ok"), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, f, LIT("k")), PORTS_FS_OK);
    CHECK(t, has(e, "k", "ok")); /* the rejected write stored nothing */
}

static void c_binary_content_and_boundaries(ctest_t *t, kit_fs_env_t *e) {
    static unsigned char all[600];
    static unsigned char got[600];
    for (size_t i = 0; i < sizeof all; i++) {
        all[i] = (unsigned char)(i * 7u + (i >> 8));
    }
    ports_fs_file_t f = mk(t, e);
    CHECK_EQ_I64(t, e->fs.ops->write(e->fs.ctx, f, all, 1), PORTS_FS_OK);
    CHECK_EQ_I64(t, e->fs.ops->write(e->fs.ctx, f, all + 1, 255), PORTS_FS_OK);
    CHECK_EQ_I64(t, e->fs.ops->write(e->fs.ctx, f, all + 256, 344), PORTS_FS_OK);
    CHECK_EQ_I64(t, commit(e, f, LIT("bin")), PORTS_FS_OK);
    size_t n = 0;
    CHECK(t, e->read(e, "bin", got, sizeof got, &n));
    CHECK_EQ_MEM(t, got, n, all, sizeof all);
}

static void c_content_pattern_6000(ctest_t *t, kit_fs_env_t *e) {
    static unsigned char src[6000];
    for (size_t i = 0; i < sizeof src; i++) {
        src[i] = (unsigned char)((i * 31u) ^ (i >> 5));
    }
    ports_fs_file_t f = mk(t, e);
    size_t off = 0;
    const size_t steps[] = {1, 4095, 1, 1, 1902};
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        CHECK_EQ_I64(t, e->fs.ops->write(e->fs.ctx, f, src + off, steps[i]), PORTS_FS_OK);
        off += steps[i];
    }
    CHECK_EQ_U64(t, off, sizeof src);
    CHECK_EQ_I64(t, commit(e, f, LIT("big")), PORTS_FS_OK);
    size_t n = 0;
    CHECK(t, e->read(e, "big", g_buf, sizeof g_buf, &n));
    CHECK_EQ_MEM(t, g_buf, n, src, sizeof src);
}

static void c_failed_create_leaves_out_untouched(ctest_t *t, kit_fs_env_t *e) {
    ports_fs_file_t keep[POSIX_FS_SLOTS];
    for (size_t i = 0; i < POSIX_FS_SLOTS; i++) {
        keep[i] = mk(t, e);
    }
    ports_fs_file_t out = 77;
    CHECK_EQ_I64(t, create(e, &out), PORTS_FS_E_NOSPACE);
    CHECK_EQ_U64(t, out, 77);
    for (size_t i = 0; i < POSIX_FS_SLOTS; i++) {
        CHECK_EQ_I64(t, discard(e, keep[i]), PORTS_FS_OK);
    }
}

/* ---- the two runners ---------------------------------------------------------------------------------- */

typedef void (*case_fn)(ctest_t *, kit_fs_env_t *);

static void run_fake(ctest_t *t, case_fn fn) {
    kit_env_begin_fake(t, &g_env, &g_kf);
    fn(t, &g_env);
    kit_env_end(t, &g_env);
}

static void run_real(ctest_t *t, case_fn fn) {
    kit_env_begin_real(t, &g_env);
    fn(t, &g_env);
    kit_env_end(t, &g_env);
}

#define CASES(X)                                                                                              \
    X(roundtrip_two_writes) X(temp_is_invisible) X(empty_file_commits) X(zero_length_write_is_a_noop)        \
    X(commit_never_overwrites) X(discard_leaves_nothing) X(handles_are_independent)                           \
    X(committed_handle_is_stale) X(zero_and_unknown_handles) X(invalid_names_rejected_with_controls)          \
    X(valid_names_accepted) X(slot_pool_is_finite_and_recycles) X(null_arguments)                             \
    X(binary_content_and_boundaries) X(content_pattern_6000) X(failed_create_leaves_out_untouched)

#define WRAP(n)                                                      \
    static void t_fake_##n(ctest_t *t) { run_fake(t, c_##n); }       \
    static void t_real_##n(ctest_t *t) { run_real(t, c_##n); }
CASES(WRAP)

#define ENTRY(n) {"fake_" #n, t_fake_##n}, {"real_" #n, t_real_##n},
CTEST_SUITE("posix_fs_contract", CASES(ENTRY))
