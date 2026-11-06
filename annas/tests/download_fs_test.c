/* annas_download: fs port failures at every step, name collisions, and timeouts during the transfer. */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define DLURL "https://dl.example.org/f/1?x=1"
#define NAME KIT_MD5 ".bin"
#define BODY "0123456789ABCDEFGHIJ"

static void api_ok(kit_t *k) {
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, NULL)));
}

static annas_code_t run(kit_t *k) {
    annas_request_t r = kit_req(KIT_MD5);
    return kit_download(k, &r);
}

/* API + GET; head in its own read, then the 20-byte BODY delivered `chunk` bytes per read. */
static void script(kit_t *k, size_t chunk) {
    api_ok(k);
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1?x=1");
    kit_respond(k, kit_head(k, 200, "OK", "", 20), 0);
    kit_respond(k, BODY, chunk);
}

static const ports_fs_err_t k_errs[] = {PORTS_FS_E_IO, PORTS_FS_E_NOSPACE, PORTS_FS_E_ARG, PORTS_FS_E_EXISTS};

static void t_create_temp_failures(ctest_t *t) {
    for (size_t i = 0; i < COUNT(k_errs); i++) {
        kit_t *k = kit_begin();
        script(k, 0);
        CHECK(t, kit_prepare(k));
        CHECK(t, ports_fake_fs_inject(&k->fs, PORTS_FAKE_FS_OP_CREATE, 1, k_errs[i]));
        k->allow_unused = true; /* annas may give up before reading the body */
        kit_check_err(t, k, run(k), ANNAS_E_FS, 0, "could not create a temporary file");
        kit_check_no_partial(t, k, 0);
    }
}

static void t_create_temp_without_free_slot(ctest_t *t) {
    kit_t *k = kit_begin();
    script(k, 0);
    for (int i = 0; i < 8; i++) {
        char name[8];
        snprintf(name, sizeof name, "f%d", i);
        CHECK(t, kit_preload(k, name, "x")); /* all 8 slots hold files */
    }
    k->allow_unused = true;
    kit_check_err(t, k, run(k), ANNAS_E_FS, 0, "could not create a temporary file");
    CHECK_EQ_U64(t, ports_fake_fs_commits(&k->fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&k->fs), 8);
}

static void t_every_write_can_fail(ctest_t *t) {
    for (uint32_t nth = 1; nth <= 4; nth++) {
        for (size_t i = 0; i < 2; i++) {
            kit_t *k = kit_begin();
            script(k, 5); /* 20 bytes in 4 reads: 4 writes */
            CHECK(t, kit_prepare(k));
            CHECK(t, ports_fake_fs_inject(&k->fs, PORTS_FAKE_FS_OP_WRITE, nth, k_errs[i]));
            k->allow_unused = true;
            kit_check_err(t, k, run(k), ANNAS_E_FS, 0, "could not write the file");
            kit_check_no_partial(t, k, 1);
        }
    }
}

static void t_exactly_one_write_per_read(ctest_t *t) {
    kit_t *k = kit_begin();
    script(k, 5);
    CHECK(t, kit_prepare(k));
    CHECK(t, ports_fake_fs_inject(&k->fs, PORTS_FAKE_FS_OP_WRITE, 5, PORTS_FS_E_IO)); /* the 5th write never happens */
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, NAME, BODY, 20);
    k = kit_begin(); /* one read, one write */
    script(k, 0);
    CHECK(t, kit_prepare(k));
    CHECK(t, ports_fake_fs_inject(&k->fs, PORTS_FAKE_FS_OP_WRITE, 2, PORTS_FS_E_IO));
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, NAME, BODY, 20);
}

static void t_commit_failures(ctest_t *t) {
    static const ports_fs_err_t errs[] = {PORTS_FS_E_IO, PORTS_FS_E_NOSPACE, PORTS_FS_E_ARG};
    for (size_t i = 0; i < COUNT(errs); i++) {
        kit_t *k = kit_begin();
        script(k, 0);
        CHECK(t, kit_prepare(k));
        CHECK(t, ports_fake_fs_inject(&k->fs, PORTS_FAKE_FS_OP_COMMIT, 1, errs[i]));
        kit_check_err(t, k, run(k), ANNAS_E_FS, 0, "could not save the file");
        kit_check_no_partial(t, k, 1);
    }
}

static void t_commit_reports_an_existing_file(ctest_t *t) {
    kit_t *k = kit_begin(); /* injected */
    script(k, 0);
    CHECK(t, kit_prepare(k));
    CHECK(t, ports_fake_fs_inject(&k->fs, PORTS_FAKE_FS_OP_COMMIT, 1, PORTS_FS_E_EXISTS));
    kit_check_err(t, k, run(k), ANNAS_E_FS, 0, "file already exists: " NAME);
    kit_check_no_partial(t, k, 1);
    k = kit_begin(); /* a real collision leaves the old file alone */
    script(k, 0);
    CHECK(t, kit_preload(k, NAME, "old content"));
    kit_check_err(t, k, run(k), ANNAS_E_FS, 0, "file already exists: " NAME);
    kit_check_no_partial(t, k, 1);
    core_str_t c;
    CHECK(t, ports_fake_fs_find(&k->fs, core_str_from_cstr(NAME), &c));
    CHECK_EQ_MEM(t, c.ptr, c.len, "old content", 11);
    k = kit_begin(); /* a sanitised name collides too; another name does not */
    script(k, 0);
    CHECK(t, kit_preload(k, "other.bin", "old"));
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, NAME, BODY, 20);
}

static void t_collision_uses_the_sanitised_name(ctest_t *t) {
    kit_t *k = kit_begin();
    script(k, 0);
    CHECK(t, kit_preload(k, "A_B.pdf", "old"));
    annas_request_t r = kit_req(KIT_MD5);
    r.title = core_str_from_cstr("A/B");
    r.format = core_str_from_cstr("PDF");
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_FS, 0, "file already exists: A_B.pdf");
    kit_check_no_partial(t, k, 1);
}

static void t_discard_failure_keeps_the_first_error(ctest_t *t) {
    kit_t *k = kit_begin();
    script(k, 5);
    CHECK(t, kit_prepare(k));
    CHECK(t, ports_fake_fs_inject(&k->fs, PORTS_FAKE_FS_OP_WRITE, 2, PORTS_FS_E_IO));
    CHECK(t, ports_fake_fs_inject(&k->fs, PORTS_FAKE_FS_OP_DISCARD, 1, PORTS_FS_E_IO));
    k->allow_unused = true;
    kit_check_err(t, k, run(k), ANNAS_E_FS, 0, "could not write the file");
    CHECK_EQ_U64(t, ports_fake_fs_commits(&k->fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&k->fs), 0);
}

static void t_a_full_fs_fails_the_write(ctest_t *t) {
    static unsigned char small[8 * 16];
    kit_t *k = kit_begin();
    kit_use_fs(k, small, sizeof small); /* every file may hold 16 bytes */
    script(k, 0);
    k->allow_unused = true;
    kit_check_err(t, k, run(k), ANNAS_E_FS, 0, "could not write the file");
    kit_check_no_partial(t, k, 1);
    k = kit_begin(); /* control: exactly 16 bytes fit */
    kit_use_fs(k, small, sizeof small);
    api_ok(k);
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1?x=1");
    kit_respond(k, kit_http(k, 200, "OK", "", "0123456789ABCDEF"), 0);
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, NAME, "0123456789ABCDEF", 16);
}

/* ---- timeouts during the transfer --------------------------------------------------------------------- */

static annas_code_t timed(kit_t *k, uint64_t advance, size_t chunk) {
    k->read_advance = advance;
    kit_set_timeout_s(k, 1);
    script(k, chunk);
    return run(k);
}

static void t_timeout_before_connecting_to_the_download_host(ctest_t *t) {
    kit_t *k = kit_begin();
    k->read_advance = 1000;
    kit_set_timeout_s(k, 1);
    api_ok(k);
    kit_connect(k, "dl.example.org", 443);
    k->ok_connects--; /* this connection is never opened */
    k->allow_unused = true;
    kit_check_err(t, k, run(k), ANNAS_E_TIMEOUT, 0, "operation timed out");
    kit_check_fs_untouched(t, k);
}

static void t_timeout_between_head_and_temp_file(ctest_t *t) {
    kit_t *k = kit_begin();
    api_ok(k);
    k->read_advance = 500; /* API read ends at 500, the head read at 1000: no temp file is created */
    kit_set_timeout_s(k, 1);
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1?x=1");
    kit_respond(k, kit_http(k, 200, "OK", "", "data"), 0);
    k->allow_unused = true;
    kit_check_err(t, k, run(k), ANNAS_E_TIMEOUT, 0, "operation timed out");
    kit_check_fs_untouched(t, k);
    k = kit_begin(); /* control: one tick shorter */
    api_ok(k);
    k->read_advance = 499;
    kit_set_timeout_s(k, 1);
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1?x=1");
    kit_respond(k, kit_http(k, 200, "OK", "", "data"), 0);
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, NAME, "data", 4);
}

static void t_timeout_before_a_write(ctest_t *t) {
    kit_t *k = kit_begin(); /* reads end at 334 (API), 668 (head), 1002 (body): the write finds the deadline passed */
    k->allow_unused = true;
    kit_check_err(t, k, timed(k, 334, 0), ANNAS_E_TIMEOUT, 0, "operation timed out");
    kit_check_no_partial(t, k, 1);
    k = kit_begin(); /* control: 333, 666, 999 */
    kit_check_ok(t, k, timed(k, 333, 0));
    kit_check_file(t, k, NAME, BODY, 20);
}

static void t_timeout_between_body_reads(ctest_t *t) {
    kit_t *k = kit_begin(); /* API 250, head 500, body reads end at 750, 1000: the third read is too late */
    k->allow_unused = true;
    kit_check_err(t, k, timed(k, 250, 5), ANNAS_E_TIMEOUT, 0, "operation timed out");
    kit_check_no_partial(t, k, 1);
    k = kit_begin(); /* control: a faster clock */
    kit_check_ok(t, k, timed(k, 100, 5));
    kit_check_file(t, k, NAME, BODY, 20);
}

static void t_timeout_on_a_redirect_hop(ctest_t *t) {
    kit_t *k = kit_begin();
    k->read_advance = 500;
    kit_set_timeout_s(k, 1);
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, "https://a.example.org/x", NULL)));
    kit_get(k, "a.example.org", 443, "/x", kit_http(k, 302, "F", "Location: https://b.example.org/y\r\n", ""));
    kit_connect(k, "b.example.org", 443);
    k->ok_connects--; /* never opened */
    k->allow_unused = true;
    kit_check_err(t, k, run(k), ANNAS_E_TIMEOUT, 0, "operation timed out");
    kit_check_fs_untouched(t, k);
    k = kit_begin(); /* control: 333 per read */
    k->read_advance = 333;
    kit_set_timeout_s(k, 1);
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, "https://a.example.org/x", NULL)));
    kit_get(k, "a.example.org", 443, "/x", kit_http(k, 302, "F", "Location: https://b.example.org/y\r\n", ""));
    kit_get(k, "b.example.org", 443, "/y", kit_http(k, 200, "OK", "", "ok"));
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, NAME, "ok", 2);
}

CTEST_SUITE("download_fs",
    {"create_temp_failures", t_create_temp_failures},
    {"create_temp_without_free_slot", t_create_temp_without_free_slot},
    {"every_write_can_fail", t_every_write_can_fail},
    {"exactly_one_write_per_read", t_exactly_one_write_per_read},
    {"commit_failures", t_commit_failures},
    {"commit_reports_an_existing_file", t_commit_reports_an_existing_file},
    {"collision_uses_the_sanitised_name", t_collision_uses_the_sanitised_name},
    {"discard_failure_keeps_the_first_error", t_discard_failure_keeps_the_first_error},
    {"a_full_fs_fails_the_write", t_a_full_fs_fails_the_write},
    {"timeout_before_connecting_to_the_download_host", t_timeout_before_connecting_to_the_download_host},
    {"timeout_between_head_and_temp_file", t_timeout_between_head_and_temp_file},
    {"timeout_before_a_write", t_timeout_before_a_write},
    {"timeout_between_body_reads", t_timeout_between_body_reads},
    {"timeout_on_a_redirect_hop", t_timeout_on_a_redirect_hop})
