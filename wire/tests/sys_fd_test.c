/* wire: the real libc seam on descriptors 0, 1 and 2. Every ctest case runs in its own forked child, which reports
 * through shared memory and not through these descriptors, so a case may point them at pipes. */
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "ctest/ctest.h"
#include "wire/wire.h"

static char g_buf[64];

/* Point descriptor `fd` at the write end of a new pipe; returns the read end (or -1). */
static int redirect_out(int fd) {
    int p[2];
    if (pipe(p) != 0 || dup2(p[1], fd) < 0) {
        return -1;
    }
    close(p[1]);
    return p[0];
}

static void t_write_out_goes_to_the_descriptor(ctest_t *t) {
    for (int fd = 1; fd <= 2; fd++) {
        int r = redirect_out(fd);
        CHECK(t, r >= 0);
        CHECK_EQ_I64(t, wire_sys_libc.write_out(NULL, fd, "hello", 5), 5);
        memset(g_buf, 0, sizeof g_buf);
        CHECK_EQ_I64(t, read(r, g_buf, sizeof g_buf), 5);
        CHECK_EQ_STR(t, g_buf, "hello");
        close(r);
    }
}

static void t_write_out_only_accepts_stdout_and_stderr(ctest_t *t) {
    int r = redirect_out(1);
    CHECK(t, r >= 0);
    CHECK_EQ_I64(t, wire_sys_libc.write_out(NULL, 0, "x", 1), -EBADF);
    CHECK_EQ_I64(t, wire_sys_libc.write_out(NULL, 3, "x", 1), -EBADF);
    CHECK_EQ_I64(t, wire_sys_libc.write_out(NULL, 1000, "x", 1), -EBADF);
    CHECK_EQ_I64(t, wire_sys_libc.write_out(NULL, -1, "x", 1), -EBADF);
    CHECK_EQ_I64(t, wire_sys_libc.write_out(NULL, 1, "x", 1), 1); /* control */
    close(r);
}

static void t_write_out_to_a_closed_descriptor(ctest_t *t) {
    close(2);
    CHECK_EQ_I64(t, wire_sys_libc.write_out(NULL, 2, "x", 1), -EBADF);
}

static void t_read_in_reads_descriptor_zero(ctest_t *t) {
    int p[2];
    CHECK_EQ_I64(t, pipe(p), 0);
    CHECK_EQ_I64(t, dup2(p[0], 0), 0);
    CHECK_EQ_I64(t, write(p[1], "abc\ndef", 7), 7);
    memset(g_buf, 0x5a, sizeof g_buf);
    CHECK_EQ_I64(t, wire_sys_libc.read_in(NULL, g_buf, 4), 4);
    CHECK_EQ_MEM(t, g_buf, 4, "abc\n", 4);
    CHECK_EQ_U64(t, (unsigned char)g_buf[4], 0x5a);
    CHECK_EQ_I64(t, wire_sys_libc.read_in(NULL, g_buf, sizeof g_buf), 3);
    CHECK_EQ_MEM(t, g_buf, 3, "def", 3);
    close(p[1]);
    CHECK_EQ_I64(t, wire_sys_libc.read_in(NULL, g_buf, sizeof g_buf), 0); /* end of input */
    CHECK_EQ_I64(t, wire_sys_libc.read_in(NULL, g_buf, sizeof g_buf), 0);
}

static void t_read_in_error(ctest_t *t) {
    close(0);
    CHECK_EQ_I64(t, wire_sys_libc.read_in(NULL, g_buf, sizeof g_buf), -EBADF);
}

CTEST_SUITE("wire_sys_fd", {"write_out_goes_to_the_descriptor", t_write_out_goes_to_the_descriptor},
            {"write_out_only_accepts_stdout_and_stderr", t_write_out_only_accepts_stdout_and_stderr},
            {"write_out_to_a_closed_descriptor", t_write_out_to_a_closed_descriptor},
            {"read_in_reads_descriptor_zero", t_read_in_reads_descriptor_zero}, {"read_in_error", t_read_in_error})
