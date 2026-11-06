/* Tests for ports/fakes.h that pin the corners the other suites leave open: a NULL context on every port function,
 * the NULL-safe observers, one-byte names, names that merely contain dots, the bounds of the env table and the exact
 * offsets of the stream mismatch record when the bytes just beyond a slice happen to match. */
#include <stdint.h>
#include <string.h>

#include "ctest/ctest.h"
#include "ports/fakes.h"
#include "ports/ports.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* ---- env ------------------------------------------------------------------------------------------------------ */

static void t_env_one_byte_name_is_found(ctest_t *t) {
    static const ports_fake_env_entry_t e[] = {{{"A", 1}, {"v", 1}}, {{"B", 1}, {"", 0}}};
    ports_fake_env_t env;
    core_str_t v = {NULL, 0};
    CHECK(t, ports_fake_env_init(&env, e, COUNT(e)));
    ports_env_t p = ports_fake_env_port(&env);
    CHECK(t, p.ops->get(p.ctx, (core_str_t){"A", 1}, &v));
    CHECK_EQ_U64(t, v.len, 1);
    CHECK_EQ_I64(t, v.ptr[0], 'v');
    CHECK(t, p.ops->get(p.ctx, (core_str_t){"B", 1}, &v));
    CHECK_EQ_U64(t, v.len, 0);
    CHECK(t, !p.ops->get(p.ctx, (core_str_t){"C", 1}, &v));
}

static void t_env_only_the_first_count_entries_are_validated(ctest_t *t) {
    /* entries[2] is invalid (an empty name) but lies beyond count: it is never looked at */
    static const ports_fake_env_entry_t e[] = {{{"A", 1}, {"1", 1}}, {{"B", 1}, {"2", 1}}, {{"", 0}, {"3", 1}}};
    ports_fake_env_t env;
    CHECK(t, ports_fake_env_init(&env, e, 2));
    CHECK_EQ_U64(t, env.count, 2);
    ports_env_t p = ports_fake_env_port(&env);
    core_str_t v = {NULL, 0};
    CHECK(t, p.ops->get(p.ctx, (core_str_t){"B", 1}, &v));
    CHECK(t, !ports_fake_env_init(&env, e, 3)); /* control: with count 3 the bad entry is seen */
}

/* ---- fs ------------------------------------------------------------------------------------------------------- */

static unsigned char g_store[8u * 64u];
static ports_fake_fs_t g_fs;
static ports_fake_fs_t g_fs2;

static void t_fs_null_context_is_an_arg_error(ctest_t *t) {
    ports_fs_t p = ports_fake_fs_port(NULL);
    ports_fs_file_t f = 0;
    CHECK_EQ_I64(t, p.ops->create_temp(p.ctx, &f), PORTS_FS_E_ARG);
    CHECK_EQ_U64(t, f, 0);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, 1, "x", 1), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, p.ops->commit(p.ctx, 1, (core_str_t){"n", 1}), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, p.ops->discard(p.ctx, 1), PORTS_FS_E_ARG);
}

static void t_fs_observers_of_null_are_zero(ctest_t *t) {
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(NULL), 0);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(NULL), 0);
    CHECK_EQ_U64(t, ports_fake_fs_commits(NULL), 0);
    CHECK_EQ_U64(t, ports_fake_fs_discards(NULL), 0);
}

static void t_fs_write_null_buffer_needs_length_zero(ctest_t *t) {
    ports_fake_fs_t *fs_p = &g_fs;
    CHECK(t, ports_fake_fs_init(fs_p, g_store, sizeof g_store));
    ports_fs_t p = ports_fake_fs_port(fs_p);
    ports_fs_file_t f = 0;
    CHECK_EQ_I64(t, p.ops->create_temp(p.ctx, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, NULL, 1), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, NULL, 2), PORTS_FS_E_ARG);
    core_str_t c;
    CHECK(t, ports_fake_fs_temp_content(fs_p, f, &c));
    CHECK_EQ_U64(t, c.len, 0);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, NULL, 0), PORTS_FS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, f, "z", 1), PORTS_FS_OK);
    CHECK(t, ports_fake_fs_temp_content(fs_p, f, &c));
    CHECK_EQ_U64(t, c.len, 1);
}

static void t_fs_names_with_a_dot_in_one_place_are_plain(ctest_t *t) {
    static const char *const names[] = {".a", "a.", "..a", "a..", ".a.", "...", "a.b"};
    ports_fake_fs_t *fs_p = &g_fs;
    CHECK(t, ports_fake_fs_init(fs_p, g_store, sizeof g_store));
    ports_fs_t p = ports_fake_fs_port(fs_p);
    for (size_t i = 0; i < COUNT(names); i++) {
        ports_fs_file_t f = 0;
        CHECK_EQ_I64(t, p.ops->create_temp(p.ctx, &f), PORTS_FS_OK);
        CHECK_EQ_I64(t, p.ops->commit(p.ctx, f, core_str_from_cstr(names[i])), PORTS_FS_OK);
        CHECK(t, ports_fake_fs_find(fs_p, core_str_from_cstr(names[i]), NULL));
    }
    /* the two reserved names stay refused */
    ports_fs_file_t f = 0;
    ports_fake_fs_t *fs2_p = &g_fs2;
    CHECK(t, ports_fake_fs_init(fs2_p, g_store, sizeof g_store));
    ports_fs_t q = ports_fake_fs_port(fs2_p);
    CHECK_EQ_I64(t, q.ops->create_temp(q.ctx, &f), PORTS_FS_OK);
    CHECK_EQ_I64(t, q.ops->commit(q.ctx, f, (core_str_t){".", 1}), PORTS_FS_E_ARG);
    CHECK_EQ_I64(t, q.ops->commit(q.ctx, f, (core_str_t){"..", 2}), PORTS_FS_E_ARG);
}

/* ---- stream --------------------------------------------------------------------------------------------------- */

static ports_fake_step_t st_connect(const char *host, size_t host_len, uint16_t port) {
    ports_fake_step_t s = {PORTS_FAKE_STEP_CONNECT, {host, host_len}, port, 0, PORTS_OK};
    return s;
}
static ports_fake_step_t st_expect(const char *data, size_t len) {
    ports_fake_step_t s = {PORTS_FAKE_STEP_EXPECT, {data, len}, 0, 0, PORTS_OK};
    return s;
}

static void t_stream_null_context_is_an_arg_error(ctest_t *t) {
    ports_stream_t p = ports_fake_stream_port(NULL);
    char buf[4];
    size_t n = 99;
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, (core_str_t){"h", 1}, 1, 0), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, "x", 1), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->read(p.ctx, buf, sizeof buf, &n), PORTS_E_ARG);
    CHECK_EQ_U64(t, n, 99);
    CHECK_EQ_I64(t, p.ops->close(p.ctx), PORTS_E_ARG);
}

static void t_stream_observers_of_null(ctest_t *t) {
    CHECK_EQ_U64(t, ports_fake_stream_closes(NULL), 0);
    CHECK_EQ_U64(t, ports_fake_stream_pos(NULL), 0);
    CHECK(t, !ports_fake_stream_is_open(NULL));
}

static void t_stream_write_null_buffer_needs_length_zero(ctest_t *t) {
    ports_fake_step_t sc[] = {st_connect("h", 1, 1), st_expect("ab", 2)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, (core_str_t){"h", 1}, 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 1), PORTS_E_ARG);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 2), PORTS_E_ARG);
    ports_fake_mismatch_t m;
    CHECK(t, !ports_fake_stream_mismatch(&s, &m));
    CHECK_EQ_I64(t, p.ops->write(p.ctx, NULL, 0), PORTS_OK);
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 1);
}

static void t_stream_excess_byte_equal_to_the_next_byte_in_memory(ctest_t *t) {
    /* The expectation is a slice of a literal: the byte behind it is the NUL of the literal. A write that sends that
     * very NUL as an excess byte is still an excess byte. */
    static const char exp[] = "ab";
    ports_fake_step_t sc[] = {st_connect("h", 1, 1), st_expect(exp, 2)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, (core_str_t){"h", 1}, 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, "ab\0", 3), PORTS_E_NET);
    ports_fake_mismatch_t m;
    CHECK(t, ports_fake_stream_mismatch(&s, &m));
    CHECK_EQ_I64(t, m.kind, PORTS_FAKE_MM_REQUEST_BYTES);
    CHECK_EQ_U64(t, m.step, 1);
    CHECK_EQ_U64(t, m.offset, 2);
}

static void t_stream_excess_after_earlier_writes(ctest_t *t) {
    static const char exp[] = "abcd";
    ports_fake_step_t sc[] = {st_connect("h", 1, 1), st_expect(exp, 4)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, (core_str_t){"h", 1}, 1, 0), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, "ab", 2), PORTS_OK);
    CHECK_EQ_I64(t, p.ops->write(p.ctx, "cd\0", 3), PORTS_E_NET);
    ports_fake_mismatch_t m;
    CHECK(t, ports_fake_stream_mismatch(&s, &m));
    CHECK_EQ_I64(t, m.kind, PORTS_FAKE_MM_REQUEST_BYTES);
    CHECK_EQ_U64(t, m.offset, 4);
    CHECK(t, !ports_fake_stream_done(&s));
}

static void t_stream_connect_offset_when_a_slice_is_a_prefix(ctest_t *t) {
    /* host "ab" (a slice of "abc") against the script's "abc": the first difference is at 2, the shorter length, even
     * though the byte behind the slice is 'c', the same as the script's third byte. And the other way round. */
    static const char buf[] = "abc";
    ports_fake_step_t sc[] = {st_connect(buf, 3, 1)};
    ports_fake_stream_t s;
    ports_fake_mismatch_t m;
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, (core_str_t){buf, 2}, 1, 0), PORTS_E_NET);
    CHECK(t, ports_fake_stream_mismatch(&s, &m));
    CHECK_EQ_I64(t, m.kind, PORTS_FAKE_MM_CONNECT_TARGET);
    CHECK_EQ_U64(t, m.offset, 2);
    ports_fake_step_t sd[] = {st_connect(buf, 2, 1)};
    CHECK(t, ports_fake_stream_init(&s, sd, COUNT(sd), NULL, 0));
    p = ports_fake_stream_port(&s);
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, (core_str_t){buf, 3}, 1, 0), PORTS_E_NET);
    CHECK(t, ports_fake_stream_mismatch(&s, &m));
    CHECK_EQ_I64(t, m.kind, PORTS_FAKE_MM_CONNECT_TARGET);
    CHECK_EQ_U64(t, m.offset, 2);
}

CTEST_SUITE("survivor",
    {"env_one_byte_name_is_found", t_env_one_byte_name_is_found},
    {"env_only_the_first_count_entries_are_validated", t_env_only_the_first_count_entries_are_validated},
    {"fs_null_context_is_an_arg_error", t_fs_null_context_is_an_arg_error},
    {"fs_observers_of_null_are_zero", t_fs_observers_of_null_are_zero},
    {"fs_write_null_buffer_needs_length_zero", t_fs_write_null_buffer_needs_length_zero},
    {"fs_names_with_a_dot_in_one_place_are_plain", t_fs_names_with_a_dot_in_one_place_are_plain},
    {"stream_null_context_is_an_arg_error", t_stream_null_context_is_an_arg_error},
    {"stream_observers_of_null", t_stream_observers_of_null},
    {"stream_write_null_buffer_needs_length_zero", t_stream_write_null_buffer_needs_length_zero},
    {"stream_excess_byte_equal_to_the_next_byte_in_memory", t_stream_excess_byte_equal_to_the_next_byte_in_memory},
    {"stream_excess_after_earlier_writes", t_stream_excess_after_earlier_writes},
    {"stream_connect_offset_when_a_slice_is_a_prefix", t_stream_connect_offset_when_a_slice_is_a_prefix})
