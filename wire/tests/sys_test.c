/* wire: the real libc seam (wire_sys_libc) where it can be exercised without touching the process's descriptors: reading
 * files, the wall clock, and the production io table. */
#include <errno.h>
#include <string.h>

#include "ctest/ctest.h"
#include "wire/wire.h"

static unsigned char g_buf[256];

static int64_t rd(const char *path, size_t cap) {
    memset(g_buf, 0x5a, sizeof g_buf);
    return wire_sys_libc.read_file(NULL, path, g_buf, cap);
}

static void t_the_table_is_complete(ctest_t *t) {
    CHECK(t, wire_sys_libc.read_file != NULL);
    CHECK(t, wire_sys_libc.read_in != NULL);
    CHECK(t, wire_sys_libc.write_out != NULL);
    CHECK(t, wire_sys_libc.unix_seconds != NULL);
    CHECK(t, wire_io_libc.sys == &wire_sys_libc);
    CHECK(t, wire_io_libc.fs_sys == NULL);
    CHECK(t, wire_io_libc.clock_sys == NULL);
    CHECK(t, wire_io_libc.transport == NULL);
}

static void t_read_a_regular_file(ctest_t *t) {
    /* the running program's own image: a regular file that always exists and starts with the ELF magic */
    CHECK_EQ_I64(t, rd("/proc/self/exe", 4), 4);
    CHECK_EQ_MEM(t, g_buf, 4, "\x7f" "ELF", 4);
    CHECK_EQ_U64(t, g_buf[4], 0x5a); /* nothing written beyond the count */
    CHECK_EQ_I64(t, rd("/proc/self/exe", 1), 1);
    CHECK_EQ_U64(t, g_buf[0], 0x7f);
    CHECK_EQ_U64(t, g_buf[1], 0x5a);
    CHECK_EQ_I64(t, rd("/proc/self/exe", sizeof g_buf), (int64_t)sizeof g_buf);
}

static void t_cap_limits_the_read(ctest_t *t) {
    CHECK_EQ_I64(t, rd("/dev/zero", 100), 100);
    for (size_t i = 0; i < 100; i++) {
        CHECK_EQ_U64(t, g_buf[i], 0);
    }
    CHECK_EQ_U64(t, g_buf[100], 0x5a);
    CHECK_EQ_I64(t, rd("/dev/zero", 1), 1);
}

static void t_an_empty_file_reads_zero_bytes(ctest_t *t) {
    CHECK_EQ_I64(t, rd("/dev/null", 64), 0);
    CHECK_EQ_U64(t, g_buf[0], 0x5a);
}

static void t_errors_are_negative_errno(ctest_t *t) {
    CHECK_EQ_I64(t, rd("/no/such/dir/file", 8), -ENOENT);
    CHECK_EQ_I64(t, rd("no-such-file.env", 8), -ENOENT);
    CHECK_EQ_I64(t, rd(".env.does.not.exist", 8), -ENOENT);
    CHECK_EQ_I64(t, rd("", 8), -ENOENT);
    CHECK_EQ_I64(t, rd("/", 8), -EISDIR);
    CHECK_EQ_I64(t, rd(".", 8), -EISDIR);
    CHECK_EQ_I64(t, rd("/dev/null/x", 8), -ENOTDIR);
}

static void t_the_wall_clock(ctest_t *t) {
    uint64_t a = wire_sys_libc.unix_seconds(NULL);
    uint64_t b = wire_sys_libc.unix_seconds(NULL);
    CHECK(t, a > 1700000000u); /* after November 2023 */
    CHECK(t, a < 4000000000u); /* before 2096 */
    CHECK(t, b >= a);
    CHECK(t, b - a < 5u);
}

CTEST_SUITE("wire_sys", {"the_table_is_complete", t_the_table_is_complete},
            {"read_a_regular_file", t_read_a_regular_file}, {"cap_limits_the_read", t_cap_limits_the_read},
            {"an_empty_file_reads_zero_bytes", t_an_empty_file_reads_zero_bytes},
            {"errors_are_negative_errno", t_errors_are_negative_errno}, {"the_wall_clock", t_the_wall_clock})
