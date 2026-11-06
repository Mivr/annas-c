/* wire: tests that pin what the mutation run (R004 T7) showed unpinned: state left over in the memory of an earlier run,
 * an empty or unreadable `.env`, a one-byte SSL_CERT_FILE, the exact scrubbing of the key from a usage error, the exact
 * stderr text of a failed stdout, a last line of one byte, and the libc seam with descriptor 0 closed. Everything is
 * observed through wire_run: the seam calls the kit records, stdout, stderr and the exit code. */
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "app/app.h"
#include "ctest/ctest.h"
#include "wire/tests/kit.h"

#define UNKNOWN_HEAD "annas-c: E_ARG: unknown command: "
#define UNKNOWN_TAIL "\nTry 'annas-c --help' for usage.\n"

static app_cmd_t g_cmd;
static app_usage_error_t g_uerr;

static void base(void) {
    kw_reset();
    kw_net_env();
}

/* Make the already registered file `path` fail with `neg_errno` from now on (kw_file_err would add a second entry that
 * the first one shadows). */
static void fail_file(const char *path, int neg_errno) {
    for (size_t i = 0; i < kw()->n_files; i++) {
        if (strcmp(kw()->files[i].path, path) == 0) {
            kw()->files[i].err = neg_errno;
        }
    }
}

/* ---- config.c: `.env` ------------------------------------------------------------------------------------------ */

static void t_a_missing_dotenv_forgets_the_entries_of_the_previous_run(ctest_t *t) {
    base();
    kw_file(".env", "ANNAS_BASE_URL=bad_host\n");
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 2); /* control: the entry is used */
    CHECK(t, kw_contains(kw_err(), "bad_host"));
    fail_file(".env", -ENOENT);
    kw_clear_output();
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 5); /* the same memory, no .env: the default host is tried */
    CHECK(t, !kw_contains(kw_err(), "bad_host"));
    CHECK_EQ_U64(t, kw_out_len(), 0);
}

static void t_an_empty_dotenv_is_a_file_without_entries(ctest_t *t) {
    base();
    kw_file(".env", "");
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 5);
    CHECK_EQ_U64(t, kw_out_len(), 0);
    CHECK(t, kw_contains(kw_err(), "annas-c: E_NET: "));
    CHECK(t, !kw_contains(kw_err(), "E_CONFIG"));
    CHECK_EQ_U64(t, kw()->last_cap, WIRE_CA_MAX + 1u); /* the CA read came after the .env read */
}

static void t_every_read_error_of_dotenv_but_enoent_is_the_read_text(ctest_t *t) {
    static const int errs[] = {-EIO, -EACCES, -EISDIR, -EPERM, -ENOMEM};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        base();
        kw_file_err(".env", errs[i]);
        kw_check(t, KW_CLI("download-url", KW_MD5), 2, "", WIRE_ERR_DOTENV_READ);
        CHECK_EQ_U64(t, kw_reads(), 1);
        CHECK_EQ_U64(t, kw()->refused, 0);
    }
}

static void t_a_failed_dotenv_of_a_usage_error_forgets_the_earlier_entries(ctest_t *t) {
    static const char *const argv[] = {"annas-c", KW_KEY};
    CHECK(t, !app_cli_parse(2, argv, &g_cmd, &g_uerr));
    kw_reset();
    kw_file(".env", "ANNAS_SECRET_KEY=" KW_KEY "\n");
    CHECK_EQ_I64(t, kw_run(WIRE_CLI, 2, argv), 2); /* control: the key of .env is scrubbed */
    CHECK(t, kw_contains(kw_err(), "REDACTED"));
    kw_check_no_key(t, KW_KEY);
    fail_file(".env", -EIO);
    kw_clear_output();
    /* .env now fails: it is ignored, the key is unknown, the text is app's own, verbatim */
    kw_check(t, kw_run(WIRE_CLI, 2, argv), 2, "", g_uerr.text);
    CHECK(t, kw_contains(kw_err(), KW_KEY));
}

/* A seam that answers more bytes than it was asked for is a failure of the seam, not a file that is too big. */
static int64_t over_read(void *ctx, const char *path, void *buf, size_t cap) {
    (void)ctx;
    (void)path;
    (void)buf;
    return (int64_t)cap + 1;
}

static void t_a_dotenv_read_that_overruns_its_cap_is_the_read_text(ctest_t *t) {
    base();
    wire_sys_t sys = *kw_io()->sys;
    sys.read_file = over_read; /* cap is WIRE_DOTENV_MAX + 1: the answer is WIRE_DOTENV_MAX + 2 */
    wire_io_t io = *kw_io();
    io.sys = &sys;
    const char *const argv[] = {"annas-c", "download-url", KW_MD5};
    CHECK_EQ_I64(t, wire_run(kw_mem(), WIRE_CLI, &io, 3, argv, kw()->envp), 2);
    CHECK_EQ_U64(t, kw_out_len(), 0);
    CHECK_EQ_STR(t, kw_err(), WIRE_ERR_DOTENV_READ);
    CHECK_EQ_U64(t, kw_err_len(), strlen(WIRE_ERR_DOTENV_READ));
}

/* ---- config.c: SSL_CERT_FILE ------------------------------------------------------------------------------------- */

static void t_a_one_byte_ssl_cert_file_is_a_path(ctest_t *t) {
    kw_reset();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("ANNAS_BASE_URL", "bad_host");
    kw_env("SSL_CERT_FILE", "c");
    kw_file("c", KW_CA_FAKE);
    kw_file(WIRE_CA_PATH_1, KW_CA_FAKE); /* a good system bundle must not be what gets read */
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 2);
    CHECK(t, kw_contains(kw_err(), "bad_host"));
    CHECK_EQ_U64(t, kw_reads(), 2);
    CHECK_EQ_STR(t, kw_read_path(1), "c");
    kw_reset(); /* and when it cannot be read there is no fallback */
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("SSL_CERT_FILE", "c");
    kw_file(WIRE_CA_PATH_1, KW_CA_FAKE);
    kw_check(t, KW_CLI("download-url", KW_MD5), 2, "", WIRE_ERR_CA_READ);
    CHECK_EQ_U64(t, kw_reads(), 2);
}

/* ---- loop.c ---------------------------------------------------------------------------------------------------- */

static void t_a_failed_stdout_in_the_loop_writes_exactly_the_text(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ "\n");
    kw()->w_fail_fd = WIRE_STDOUT;
    kw()->w_fail_nth = 1;
    kw()->w_fail_err = -EPIPE;
    CHECK_EQ_I64(t, KW_MCP0(), WIRE_EXIT_IO);
    CHECK_EQ_U64(t, kw_out_len(), 0);
    CHECK_EQ_U64(t, kw_err_len(), strlen(WIRE_ERR_STDOUT));
    CHECK_EQ_STR(t, kw_err(), WIRE_ERR_STDOUT);
}

static void t_a_last_line_of_one_byte_is_handled(ctest_t *t) {
    base();
    kw_in("x"); /* no newline, one byte: not blank, not JSON */
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 1);
    CHECK(t, kw_contains(kw_out_line(0), "\"error\":{\"code\":-32700,"));
    CHECK_EQ_U64(t, kw_err_len(), 0);
}

static void t_a_last_line_without_newline_that_cannot_be_written(ctest_t *t) {
    base();
    kw_in(KW_PING_REQ); /* no newline */
    kw()->w_fail_fd = WIRE_STDOUT;
    kw()->w_fail_nth = 1;
    kw()->w_fail_err = -EIO;
    CHECK_EQ_I64(t, KW_MCP0(), WIRE_EXIT_IO);
    CHECK_EQ_U64(t, kw_out_len(), 0);
    kw_check(t, WIRE_EXIT_IO, WIRE_EXIT_IO, "", WIRE_ERR_STDOUT);
    base(); /* control: the same line is answered when stdout works */
    kw_in(KW_PING_REQ);
    kw_check(t, KW_MCP0(), 0, KW_PING_REPLY "\n", "");
}

/* ---- run.c ----------------------------------------------------------------------------------------------------- */

static void scrubbed(ctest_t *t, const char *key, const char *arg, const char *shown) {
    char want[256];
    const char *const argv[] = {"annas-c", arg};
    kw_reset();
    kw_env("ANNAS_SECRET_KEY", key);
    CHECK(t, strlen(UNKNOWN_HEAD) + strlen(shown) + strlen(UNKNOWN_TAIL) < sizeof want);
    strcpy(want, UNKNOWN_HEAD);
    strcat(want, shown);
    strcat(want, UNKNOWN_TAIL);
    kw_check(t, kw_run(WIRE_CLI, 2, argv), 2, "", want);
}

static void t_the_key_is_replaced_everywhere_in_one_pass(ctest_t *t) {
    scrubbed(t, "ab", "1ab2ab3", "1REDACTED2REDACTED3");
    scrubbed(t, "ab", "abab", "REDACTEDREDACTED");
    scrubbed(t, "ab", "xabx", "xREDACTEDx");
    scrubbed(t, "aa", "aaa", "REDACTEDa");   /* non-overlapping, left to right */
    scrubbed(t, "aa", "aaaa", "REDACTEDREDACTED");
    scrubbed(t, "ab", "aab", "aREDACTED");
    scrubbed(t, KW_KEY, "<" KW_KEY "|" KW_KEY ">", "<REDACTED|REDACTED>");
    scrubbed(t, "ab", "b", "b"); /* control: nothing to replace */
    scrubbed(t, "ab", "a", "a");
}

static void t_a_one_byte_key_is_scrubbed(ctest_t *t) {
    scrubbed(t, "Q", "xQx", "xREDACTEDx");
    scrubbed(t, "Q", "QQQ", "REDACTEDREDACTEDREDACTED");
    scrubbed(t, "Q", "x", "x");
}

static void t_invalid_dependencies_are_an_internal_error(ctest_t *t) {
    static const posix_clock_sys_t bad = {NULL};
    base();
    wire_io_t io = *kw_io();
    io.clock_sys = &bad;
    const char *const argv[] = {"annas-c", "download-url", KW_MD5};
    CHECK_EQ_I64(t, wire_run(kw_mem(), WIRE_CLI, &io, 3, argv, kw()->envp), WIRE_EXIT_INTERNAL);
    CHECK_EQ_U64(t, kw_out_len(), 0);
    CHECK(t, kw_err_len() > 0u);
    CHECK(t, kw_contains(kw_err(), "annas-c: "));
    CHECK_EQ_U64(t, kw()->refused, 0);
    kw_check_no_key(t, KW_KEY);
    base(); /* control: the same call with the working clock reaches the network */
    CHECK_EQ_I64(t, KW_CLI("download-url", KW_MD5), 5);
}

static void t_help_of_the_mcp_binary_reports_a_failed_stdout(ctest_t *t) {
    base();
    kw()->w_fail_fd = WIRE_STDOUT;
    kw()->w_fail_nth = 1;
    kw()->w_fail_err = -EPIPE;
    CHECK_EQ_I64(t, KW_RUN(WIRE_MCP, "annas-c-mcp", "--help"), WIRE_EXIT_IO);
    CHECK_EQ_U64(t, kw_out_len(), 0);
    CHECK_EQ_STR(t, kw_err(), WIRE_ERR_STDOUT);
    base();
    CHECK_EQ_I64(t, KW_RUN(WIRE_MCP, "annas-c-mcp", "--version"), 0); /* control */
    CHECK_EQ_STR(t, kw_out(), "annas-c 0.1\n");
}

/* ---- sys.c ----------------------------------------------------------------------------------------------------- */

static void t_the_libc_seam_reads_a_file_that_opens_as_descriptor_zero(ctest_t *t) {
    unsigned char buf[8];
    CHECK_EQ_I64(t, wire_sys_libc.read_file(NULL, "/proc/self/exe", buf, 4), 4); /* control */
    CHECK_EQ_I64(t, close(0), 0);
    CHECK_EQ_I64(t, wire_sys_libc.read_file(NULL, "/proc/self/exe", buf, 4), 4); /* opens as fd 0 */
    CHECK_EQ_MEM(t, buf, 4, "\x7f" "ELF", 4);
}

static void t_a_write_of_zero_bytes_is_zero(ctest_t *t) {
    char c = 'x';
    errno = 0;
    CHECK_EQ_I64(t, wire_sys_libc.write_out(NULL, WIRE_STDOUT, &c, 0), 0);
    errno = EPIPE;
    CHECK_EQ_I64(t, wire_sys_libc.write_out(NULL, WIRE_STDERR, &c, 0), 0);
    CHECK_EQ_I64(t, wire_sys_libc.write_out(NULL, 0, &c, 1), -EBADF);
    CHECK_EQ_I64(t, wire_sys_libc.write_out(NULL, 3, &c, 1), -EBADF);
}

CTEST_SUITE("wire_survivor",
    {"missing_dotenv_forgets_earlier_entries", t_a_missing_dotenv_forgets_the_entries_of_the_previous_run},
    {"an_empty_dotenv_is_a_file_without_entries", t_an_empty_dotenv_is_a_file_without_entries},
    {"dotenv_read_errors_give_the_read_text", t_every_read_error_of_dotenv_but_enoent_is_the_read_text},
    {"usage_error_failed_dotenv_forgets_entries",
     t_a_failed_dotenv_of_a_usage_error_forgets_the_earlier_entries},
    {"dotenv_read_overrunning_its_cap_is_the_read_text", t_a_dotenv_read_that_overruns_its_cap_is_the_read_text},
    {"a_one_byte_ssl_cert_file_is_a_path", t_a_one_byte_ssl_cert_file_is_a_path},
    {"loop_failed_stdout_text_is_exact", t_a_failed_stdout_in_the_loop_writes_exactly_the_text},
    {"a_last_line_of_one_byte_is_handled", t_a_last_line_of_one_byte_is_handled},
    {"last_line_without_newline_failed_stdout", t_a_last_line_without_newline_that_cannot_be_written},
    {"key_replaced_everywhere_in_one_pass", t_the_key_is_replaced_everywhere_in_one_pass},
    {"a_one_byte_key_is_scrubbed", t_a_one_byte_key_is_scrubbed},
    {"invalid_dependencies_are_internal_error", t_invalid_dependencies_are_an_internal_error},
    {"mcp_help_reports_failed_stdout", t_help_of_the_mcp_binary_reports_a_failed_stdout},
    {"libc_seam_reads_file_opened_as_fd_zero",
     t_the_libc_seam_reads_a_file_that_opens_as_descriptor_zero},
    {"a_write_of_zero_bytes_is_zero", t_a_write_of_zero_bytes_is_zero})
