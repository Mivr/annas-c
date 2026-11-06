/* wire: writing the texts and reading stdin through the seams: short counts, -EINTR, zero progress, counts above len,
 * failures of stdout and of stderr, failures of stdin. Every rejection has a positive control. */
#include <errno.h>
#include <string.h>

#include "ctest/ctest.h"
#include "wire/tests/kit.h"

#define VERSION_TEXT "annas-c 0.1\n"

static void base(void) {
    kw_reset();
    kw_net_env();
}

static void t_short_writes_continue(ctest_t *t) {
    base();
    kw_check(t, KW_CLI("--version"), 0, VERSION_TEXT, "");
    CHECK_EQ_U64(t, kw()->w_real[1], 1);
    static const size_t chunks[] = {1, 2, 5, 11};
    static const unsigned calls[] = {12, 6, 3, 2};
    for (size_t i = 0; i < 4; i++) {
        base();
        kw()->accept_max = chunks[i];
        kw_check(t, KW_CLI("--version"), 0, VERSION_TEXT, "");
        CHECK_EQ_U64(t, kw()->w_real[1], calls[i]);
        kw_check_no_empty_writes(t);
    }
}

static void t_short_writes_of_stderr(ctest_t *t) {
    base();
    kw()->accept_max = 3;
    CHECK_EQ_I64(t, KW_CLI("bogus"), 2);
    CHECK(t, kw_contains(kw_err(), "unknown command: bogus"));
    CHECK(t, kw_err()[kw_err_len() - 1u] == '\n');
    CHECK_EQ_U64(t, kw_out_len(), 0);
    CHECK_EQ_U64(t, kw()->w_calls[1], 0);
}

static void t_eintr_is_retried_up_to_the_limit(ctest_t *t) {
    base();
    kw()->w_eintr = 3;
    kw_check(t, KW_CLI("--version"), 0, VERSION_TEXT, "");
    CHECK_EQ_U64(t, kw()->w_calls[1], 4);
    base();
    kw()->w_eintr = WIRE_EINTR_MAX; /* exactly the limit still works */
    kw_check(t, KW_CLI("--version"), 0, VERSION_TEXT, "");
    CHECK_EQ_U64(t, kw()->w_calls[1], (uint64_t)WIRE_EINTR_MAX + 1u);
    base();
    kw()->w_eintr = WIRE_EINTR_MAX + 1u; /* one more is a failure, not a hang */
    kw()->w_eintr_fd = WIRE_STDOUT;
    kw_check(t, KW_CLI("--version"), WIRE_EXIT_IO, "", WIRE_ERR_STDOUT);
    CHECK(t, kw()->w_calls[1] <= (uint64_t)WIRE_EINTR_MAX + 2u);
    CHECK(t, kw()->w_calls[1] >= (uint64_t)WIRE_EINTR_MAX + 1u);
}

static void t_eintr_between_short_writes(ctest_t *t) {
    base();
    kw()->accept_max = 4;
    kw()->w_eintr = 2;
    kw_check(t, KW_CLI("--version"), 0, VERSION_TEXT, "");
    CHECK_EQ_U64(t, kw()->w_real[1], 3);
    CHECK_EQ_U64(t, kw()->w_calls[1], 9);
}

static void expect_stdout_failure(ctest_t *t) {
    CHECK_EQ_I64(t, KW_CLI("--version"), WIRE_EXIT_IO);
    CHECK_EQ_STR(t, kw_err(), WIRE_ERR_STDOUT);
    kw_check_no_empty_writes(t);
}

static void t_stdout_failures(ctest_t *t) {
    static const int errs[] = {-EPIPE, -EIO, -EBADF, -ENOSPC, -EAGAIN, -EINVAL};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        base();
        kw()->w_fail_fd = WIRE_STDOUT;
        kw()->w_fail_nth = 1;
        kw()->w_fail_err = errs[i];
        expect_stdout_failure(t);
        CHECK_EQ_U64(t, kw_out_len(), 0);
        CHECK_EQ_U64(t, kw()->w_real[1], 1); /* no retry of a hard failure */
    }
}

static void t_zero_progress_and_overlong_counts_are_failures(ctest_t *t) {
    base();
    kw()->w_zero_fd = WIRE_STDOUT;
    expect_stdout_failure(t);
    CHECK_EQ_U64(t, kw()->w_calls[1], 1); /* not a loop */
    base();
    kw()->w_over_fd = WIRE_STDOUT;
    expect_stdout_failure(t);
    CHECK_EQ_U64(t, kw()->w_calls[1], 1);
}

static void t_failure_after_a_partial_write(ctest_t *t) {
    base();
    kw()->accept_max = 4;
    kw()->w_fail_fd = WIRE_STDOUT;
    kw()->w_fail_nth = 2;
    kw()->w_fail_err = -EIO;
    expect_stdout_failure(t);
    CHECK_EQ_U64(t, kw_out_len(), 4);
    CHECK_EQ_U64(t, kw()->w_real[1], 2);
}

static void t_stderr_failure(ctest_t *t) {
    base(); /* control: the usage error is written and the exit code is the usage code */
    kw_check(t, KW_CLI("bogus"), 2, "", "annas-c: E_ARG: unknown command: bogus\nTry 'annas-c --help' for usage.\n");
    static const int errs[] = {-EPIPE, -EIO};
    for (size_t i = 0; i < 2; i++) {
        base();
        kw()->w_fail_fd = WIRE_STDERR;
        kw()->w_fail_nth = 1;
        kw()->w_fail_err = errs[i];
        CHECK_EQ_I64(t, KW_CLI("bogus"), WIRE_EXIT_IO);
        CHECK_EQ_U64(t, kw_err_len(), 0);
        CHECK_EQ_U64(t, kw_out_len(), 0);
        CHECK_EQ_U64(t, kw()->w_real[2], 1);
    }
}

static void t_both_streams_failing(ctest_t *t) {
    base();
    kw()->w_fail_fd = WIRE_STDOUT;
    kw()->w_fail_nth = 1;
    kw()->w_fail_err = -EPIPE;
    CHECK_EQ_I64(t, KW_CLI("--version"), WIRE_EXIT_IO);
    base(); /* stdout failing does not hide the failure of the message either: nothing to crash on */
    kw()->w_fail_fd = WIRE_STDOUT;
    kw()->w_fail_nth = 1;
    kw()->w_fail_err = -EPIPE;
    kw()->w_zero_fd = WIRE_STDERR;
    CHECK_EQ_I64(t, KW_CLI("--version"), WIRE_EXIT_IO);
    CHECK_EQ_U64(t, kw_out_len() + kw_err_len(), 0);
}

static void t_nothing_is_written_for_an_empty_text(ctest_t *t) {
    base();
    CHECK_EQ_I64(t, KW_CLI("--version"), 0);
    CHECK_EQ_U64(t, kw()->w_calls[2], 0);
    base();
    CHECK_EQ_I64(t, KW_CLI("bogus"), 2);
    CHECK_EQ_U64(t, kw()->w_calls[1], 0);
    kw_check_no_empty_writes(t);
}

/* ---- stdin ---------------------------------------------------------------------------------------------------- */

static void t_stdin_failure(ctest_t *t) {
    static const int errs[] = {-EIO, -EBADF, -EISDIR, -EAGAIN};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        base();
        kw()->in_err = errs[i];
        kw_check(t, KW_MCP0(), WIRE_EXIT_IO, "", WIRE_ERR_STDIN);
        CHECK_EQ_U64(t, kw()->in_reads, 1);
    }
    base(); /* control: end of input is a clean exit */
    kw_check(t, KW_MCP0(), 0, "", "");
}

static void t_stdin_failure_after_input(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ "\n");
    kw()->in_err = -EIO;
    kw_check(t, KW_MCP0(), WIRE_EXIT_IO, KW_PING_REPLY "\n", WIRE_ERR_STDIN);
}

static void t_stdin_eintr(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ "\n");
    kw()->in_eintr = 5;
    kw_check(t, KW_MCP0(), 0, KW_PING_REPLY "\n", "");
    base();
    kw_in(KW_PING_REQ "\n");
    kw()->in_eintr = WIRE_EINTR_MAX;
    kw_check(t, KW_MCP0(), 0, KW_PING_REPLY "\n", "");
    base();
    kw_in(KW_PING_REQ "\n");
    kw()->in_eintr = WIRE_EINTR_MAX + 1u;
    kw_check(t, KW_MCP0(), WIRE_EXIT_IO, "", WIRE_ERR_STDIN);
    CHECK(t, kw()->in_calls <= (uint64_t)WIRE_EINTR_MAX + 2u);
}

static void t_stdin_count_above_cap_is_a_failure(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ "\n");
    kw()->in_over = true;
    kw_check(t, KW_MCP0(), WIRE_EXIT_IO, "", WIRE_ERR_STDIN);
    CHECK_EQ_U64(t, kw()->in_reads, 1);
}

static void t_stdin_requests_are_bounded_and_nonzero(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ "\n");
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK(t, kw()->last_in_cap >= 1u);
    CHECK(t, kw()->last_in_cap <= WIRE_READ_CHUNK);
}

static void t_stdout_failure_in_the_loop_stops_it(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ "\n");
    kw_in(KW_PING_REQ "\n");
    kw_in(KW_PING_REQ "\n");
    kw()->w_fail_fd = WIRE_STDOUT;
    kw()->w_fail_nth = 2; /* the second reply cannot be written */
    CHECK_EQ_I64(t, KW_MCP0(), WIRE_EXIT_IO);
    CHECK_EQ_STR(t, kw_out(), KW_PING_REPLY "\n");
    CHECK_EQ_STR(t, kw_err(), WIRE_ERR_STDOUT);
    CHECK_EQ_U64(t, kw()->in_reads, 2); /* it did not go on reading */
}

CTEST_SUITE("wire_io", {"short_writes_continue", t_short_writes_continue},
            {"short_writes_of_stderr", t_short_writes_of_stderr},
            {"eintr_is_retried_up_to_the_limit", t_eintr_is_retried_up_to_the_limit},
            {"eintr_between_short_writes", t_eintr_between_short_writes}, {"stdout_failures", t_stdout_failures},
            {"zero_progress_and_overlong_counts_are_failures", t_zero_progress_and_overlong_counts_are_failures},
            {"failure_after_a_partial_write", t_failure_after_a_partial_write},
            {"stderr_failure", t_stderr_failure}, {"both_streams_failing", t_both_streams_failing},
            {"nothing_is_written_for_an_empty_text", t_nothing_is_written_for_an_empty_text},
            {"stdin_failure", t_stdin_failure}, {"stdin_failure_after_input", t_stdin_failure_after_input},
            {"stdin_eintr", t_stdin_eintr}, {"stdin_count_above_cap_is_a_failure", t_stdin_count_above_cap_is_a_failure},
            {"stdin_requests_are_bounded_and_nonzero", t_stdin_requests_are_bounded_and_nonzero},
            {"stdout_failure_in_the_loop_stops_it", t_stdout_failure_in_the_loop_stops_it})
