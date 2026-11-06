/* wire, end to end (real TLS handshake with the in-memory server): the lazily opened download directory seen only
 * through wire_run. The posix_fs seam is the kit's, wrapped to inject one fault at a time and to record how many
 * descriptors had been closed whenever stdin is read; the observations are the seam calls, stdout, stderr and the exit
 * code. Each case has the plain download as positive control. */
#include <errno.h>
#include <string.h>

#include "ctest/ctest.h"
#include "wire/tests/kit.h"

#define FILE_URL "https://" KW_PEER_HOST "/files/b.bin"
#define DL_API KW_API_OK_BODY(FILE_URL)
#define CALL(id, name, args) \
    "{\"jsonrpc\":\"2.0\",\"id\":" id ",\"method\":\"tools/call\",\"params\":{\"name\":\"" name "\",\"arguments\":" args "}}\n"

static int g_open_tmp_err;
static int g_pwrite_err;
static int g_link_err;
static unsigned g_closes_at_read[8];
static unsigned g_reads;

static const posix_fs_sys_t *real_fs(void) {
    return kw_io()->fs_sys;
}
static int64_t w_open_dir(void *c, const char *p) {
    return real_fs()->open_dir(c, p);
}
static int64_t w_open_tmp(void *c, int d, unsigned m) {
    return g_open_tmp_err != 0 ? g_open_tmp_err : real_fs()->open_tmp(c, d, m);
}
static int64_t w_pwrite(void *c, int fd, const void *b, size_t n, uint64_t off) {
    return g_pwrite_err != 0 ? g_pwrite_err : real_fs()->pwrite(c, fd, b, n, off);
}
static int64_t w_ftruncate(void *c, int fd, uint64_t n) {
    return real_fs()->ftruncate(c, fd, n);
}
static int64_t w_fsync(void *c, int fd) {
    return real_fs()->fsync(c, fd);
}
static int64_t w_link(void *c, int fd, int d, const char *name) {
    return g_link_err != 0 ? g_link_err : real_fs()->link_tmp(c, fd, d, name);
}
static int64_t w_close(void *c, int fd) {
    return real_fs()->close(c, fd);
}
static const posix_fs_sys_t k_wrap = {w_open_dir, w_open_tmp, w_pwrite, w_ftruncate, w_fsync, w_link, w_close};

static int64_t w_read_in(void *c, void *buf, size_t cap) {
    if (g_reads < sizeof g_closes_at_read / sizeof g_closes_at_read[0]) {
        g_closes_at_read[g_reads] = kw()->fs.closes;
    }
    g_reads++;
    return kw_io()->sys->read_in(c, buf, cap);
}

/* wire_run with the wrapped posix_fs and the recording stdin. */
static int run(wire_mode_t mode, int argc, const char *const *argv) {
    wire_sys_t sys = *kw_io()->sys;
    sys.read_in = w_read_in;
    wire_io_t io = *kw_io();
    io.sys = &sys;
    io.fs_sys = &k_wrap;
    g_reads = 0;
    memset(g_closes_at_read, 0, sizeof g_closes_at_read);
    return wire_run(kw_mem(), mode, &io, argc, argv, kw()->envp);
}
#define RUN_CLI(...) run(WIRE_CLI, KW_ARGC("annas-c", __VA_ARGS__), (const char *const[]){"annas-c", __VA_ARGS__})

/* The e2e setup with ANNAS_DOWNLOAD_PATH `dir` (annas requires an absolute path) and the two scripted responses of a download. */
static void setup(const char *dir) {
    kw_e2e_setup();
    kw_env_clear();
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("ANNAS_BASE_URL", KW_PEER_HOST);
    kw_env("SSL_CERT_FILE", KW_CA_PATH);
    if (dir != NULL) {
        kw_env("ANNAS_DOWNLOAD_PATH", dir);
    }
    g_open_tmp_err = 0;
    g_pwrite_err = 0;
    g_link_err = 0;
    kw_peer_respond(0, kw_http(200, "OK", DL_API));
    kw_peer_respond(1, kw_http(200, "OK", "BOOKDATA"));
}

static void failed_with(ctest_t *t, int rc, const char *text) {
    CHECK_EQ_I64(t, rc, 6);
    CHECK_EQ_U64(t, kw_out_len(), 0);
    CHECK(t, kw_contains(kw_err(), "annas-c: E_FS: "));
    CHECK(t, kw_contains(kw_err(), text));
    CHECK_EQ_U64(t, kw()->fs.links, 0);
    kw_check_no_key(t, KW_KEY);
}

static void t_a_one_byte_download_path_is_opened(ctest_t *t) {
    setup("/dl");
    int rc0 = RUN_CLI("download", KW_MD5);
    CHECK_EQ_STR(t, kw_err(), "");
    CHECK_EQ_I64(t, rc0, 0); /* control */
    CHECK_EQ_STR(t, kw()->fs.dir, "/dl");
    setup("/");
    CHECK_EQ_I64(t, RUN_CLI("download", KW_MD5), 0);
    CHECK_EQ_U64(t, kw()->fs.opens, 1);
    CHECK_EQ_STR(t, kw()->fs.dir, "/");
    CHECK_EQ_U64(t, kw()->fs.links, 1);
}

static void t_a_directory_that_cannot_be_opened_fails_at_the_temp_file(ctest_t *t) {
    setup("/dl");
    kw()->fs.open_err = -EACCES;
    failed_with(t, RUN_CLI("download", KW_MD5), "could not create a temporary file");
    CHECK_EQ_U64(t, kw()->fs.opens, 1);
}

static void t_a_failing_temp_file_is_reported_as_such(ctest_t *t) {
    static const int errs[] = {-ENOSPC, -EIO, -EMFILE};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        setup("/dl");
        g_open_tmp_err = errs[i];
        failed_with(t, RUN_CLI("download", KW_MD5), "could not create a temporary file");
        CHECK_EQ_U64(t, kw()->fs.opens, 1);
    }
}

static void t_a_failing_write_is_reported_as_such(ctest_t *t) {
    static const int errs[] = {-ENOSPC, -EIO};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        setup("/dl");
        g_pwrite_err = errs[i];
        failed_with(t, RUN_CLI("download", KW_MD5), "could not write the file");
    }
}

static void t_a_name_that_exists_is_reported_as_such(ctest_t *t) {
    setup("/dl");
    g_link_err = -EEXIST;
    failed_with(t, RUN_CLI("download", KW_MD5, "--name", "Book", "--format", "pdf"), "file already exists: Book.pdf");
    setup("/dl");
    g_link_err = -EIO;
    failed_with(t, RUN_CLI("download", KW_MD5), "could not save the file");
}

static void t_a_failed_call_discards_its_temp_file_before_the_next_line(ctest_t *t) {
    setup("/dl");
    g_pwrite_err = -ENOSPC;
    kw_in(CALL("1", "book_download", "{\"hash\":\"" KW_MD5 "\",\"title\":\"A\"}"));
    kw_in(KW_PING_REQ "\n");
    CHECK_EQ_I64(t, run(WIRE_MCP, 1, (const char *const[]){"annas-c-mcp"}), 0);
    CHECK(t, kw_contains(kw_out_line(0), "\"isError\":true"));
    CHECK_EQ_U64(t, g_closes_at_read[1], 1);
    CHECK_EQ_U64(t, kw()->fs.closes, 2);
}

CTEST_SUITE("wire_survivor_e2e",
    {"a_one_byte_download_path_is_opened", t_a_one_byte_download_path_is_opened},
    {"a_directory_that_cannot_be_opened_fails_at_the_temp_file", t_a_directory_that_cannot_be_opened_fails_at_the_temp_file},
    {"a_failing_temp_file_is_reported_as_such", t_a_failing_temp_file_is_reported_as_such},
    {"a_failing_write_is_reported_as_such", t_a_failing_write_is_reported_as_such},
    {"a_name_that_exists_is_reported_as_such", t_a_name_that_exists_is_reported_as_such},
    {"a_failed_call_discards_its_temp_file_before_the_next_line",
     t_a_failed_call_discards_its_temp_file_before_the_next_line})
