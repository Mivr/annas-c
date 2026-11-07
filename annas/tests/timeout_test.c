/* Deadline placement and saturation (annas.h "Timeout"): the deadline is start + timeout_ms, saturating at UINT64_MAX,
 * and the clock is compared with it before EVERY port call (connect, stream write, stream read, fs create_temp, fs
 * write, fs commit) and not after the last one. A test clock advances 1 ms after every now_ms call and the scripted
 * stream gets no clock, so with timeout_ms == J the J-th deadline check (the J+1-th clock read) is the first to fail.
 *
 * Download script (one body write, everything delivered by one read per message), checks in order:
 *   1 before the API connect          2 after it, before the request write     3 before the API read
 *   4 before the download connect     5 after it, before the request write     6 before the download read
 *   7 before create_temp              8 before the fs write                    9 before the fs commit */
#include <stdint.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

#define DLURL "https://dl.example.org/f/1?x=1"
#define NAME KIT_MD5 ".bin"
#define TIMEOUT_MSG "operation timed out"

static void script(kit_t *k) {
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, NULL)));
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1?x=1");
    kit_respond(k, kit_http(k, 200, "OK", "", "data"), 0);
}

static annas_code_t run(kit_t *k) {
    annas_request_t r = kit_req(KIT_MD5);
    return kit_download(k, &r);
}

/* inject_op < 0: none; otherwise that fs op would fail with IO on its first call if annas made it. */
static void fails_at(ctest_t *t, uint64_t j, unsigned opened, size_t pos, uint32_t discards, int inject_op) {
    kit_t *k = kit_begin();
    kit_step_clock(k, 1);
    k->cfg.timeout_ms = j;
    script(k);
    k->ok_connects = opened; /* the connections the run really opens */
    k->allow_unused = true;
    CHECK(t, kit_prepare(k));
    if (inject_op >= 0) {
        CHECK(t, ports_fake_fs_inject(&k->fs, (ports_fake_fs_op_t)inject_op, 1, PORTS_FS_E_IO));
    }
    kit_check_err(t, k, run(k), ANNAS_E_TIMEOUT, 0, TIMEOUT_MSG);
    CHECK_EQ_U64(t, ports_fake_stream_pos(&k->stream), pos);
    kit_check_no_partial(t, k, discards);
    CHECK_EQ_U64(t, kit_clock_calls(k), j + 1u); /* the start read and j checks: no clock read after the failure */
}

static void t_deadline_before_the_api_connect(ctest_t *t) { fails_at(t, 1, 0, 0, 0, -1); }
static void t_deadline_after_the_api_connect_before_the_request_write(ctest_t *t) { fails_at(t, 2, 1, 1, 0, -1); }
static void t_deadline_before_the_api_read(ctest_t *t) { fails_at(t, 3, 1, 2, 0, -1); }
static void t_deadline_before_the_download_connect(ctest_t *t) { fails_at(t, 4, 1, 3, 0, -1); }
static void t_deadline_after_the_download_connect_before_the_request_write(ctest_t *t) {
    fails_at(t, 5, 2, 4, 0, -1); /* the position equals the connect step: the request EXPECT is not consumed */
}
static void t_deadline_before_the_download_read(ctest_t *t) { fails_at(t, 6, 2, 5, 0, -1); }
static void t_deadline_before_create_temp(ctest_t *t) {
    fails_at(t, 7, 2, 6, 0, -1); /* no temp file, nothing to discard */
    fails_at(t, 7, 2, 6, 0, PORTS_FAKE_FS_OP_CREATE); /* create_temp is not even called */
}
static void t_deadline_before_an_fs_write(ctest_t *t) {
    fails_at(t, 8, 2, 6, 1, -1); /* the temp file exists and is discarded once */
    fails_at(t, 8, 2, 6, 1, PORTS_FAKE_FS_OP_WRITE); /* write is not called: no "could not write the file" */
}
static void t_deadline_between_the_last_write_and_the_commit(ctest_t *t) {
    fails_at(t, 9, 2, 6, 1, -1); /* discards == 1, commits == 0 */
    fails_at(t, 9, 2, 6, 1, PORTS_FAKE_FS_OP_COMMIT); /* commit is not called: no "could not save the file" */
}

static void t_no_deadline_check_after_the_last_port_call(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_step_clock(k, 1);
    k->cfg.timeout_ms = 10; /* nine checks pass (the 9th reads start + 9 < start + 10) */
    script(k);
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, NAME, "data", 4);
    CHECK_EQ_U64(t, kit_clock_calls(k), 10);
}

/* fast_download_url: three checks only (before connect, after connect, before the read), none after the read. */
static annas_code_t fast_at(kit_t *k, uint64_t j) {
    kit_step_clock(k, 1);
    k->cfg.timeout_ms = j;
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, NULL)));
    annas_request_t r = kit_req(KIT_MD5);
    return kit_fast(k, &r);
}

static void t_fast_deadline_placements(ctest_t *t) {
    static const struct {
        uint64_t j;
        unsigned opened;
        size_t pos;
    } c[] = {{1, 0, 0}, {2, 1, 1}, {3, 1, 2}};
    for (size_t i = 0; i < 3; i++) {
        kit_t *k = kit_begin();
        annas_code_t got = fast_at(k, c[i].j);
        k->ok_connects = c[i].opened;
        k->allow_unused = true;
        kit_check_err(t, k, got, ANNAS_E_TIMEOUT, 0, TIMEOUT_MSG);
        CHECK_EQ_U64(t, ports_fake_stream_pos(&k->stream), c[i].pos);
        CHECK_EQ_U64(t, kit_clock_calls(k), c[i].j + 1u);
    }
    kit_t *k = kit_begin(); /* three checks pass at timeout 4; none follows the read */
    kit_check_ok(t, k, fast_at(k, 4));
    CHECK_EQ_U64(t, kit_clock_calls(k), 4);
}

/* ---- saturation: start + timeout wraps past UINT64_MAX unless it saturates ----------------------------- */

static void t_fast_succeeds_with_the_clock_near_the_maximum(ctest_t *t) {
    kit_t *k = kit_begin();
    CHECK(t, ports_fake_clock_init(&k->clock, UINT64_MAX - 5u));
    kit_set_timeout_s(k, 3600);
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, "{\"downloads_left\":2}")));
    annas_request_t r = kit_req(KIT_MD5);
    annas_code_t got = kit_fast(k, &r);
    kit_check_ok(t, k, got);
    CHECK_EQ_STR(t, k->fast.url, DLURL);
    CHECK_EQ_STR(t, k->fast.account, "{\"downloads_left\":2}");
}

static void t_download_succeeds_with_the_clock_near_the_maximum(ctest_t *t) {
    kit_t *k = kit_begin();
    CHECK(t, ports_fake_clock_init(&k->clock, UINT64_MAX - 5u));
    kit_set_timeout_s(k, 3600);
    script(k);
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, NAME, "data", 4);
    CHECK_EQ_U64(t, k->dl.bytes, 4);
}

static void t_saturated_deadline_is_the_maximum_not_a_wrap(ctest_t *t) {
    kit_t *k = kit_begin(); /* start = MAX - 1000, timeout 2000 ms: a wrapped deadline would be 999 */
    CHECK(t, ports_fake_clock_init(&k->clock, UINT64_MAX - 1000u));
    k->cfg.timeout_ms = 2000;
    script(k);
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, NAME, "data", 4);
}

/* A hand-built configuration with timeout_ms == 0 (annas_config_load never makes one): the deadline is the start
 * itself, so the first deadline check (now >= deadline) is a timeout, before the connect. */
static void t_zero_timeout_expires_at_the_first_check(ctest_t *t) {
    kit_t *k = kit_begin();
    k->cfg.timeout_ms = 0;
    script(k);
    k->ok_connects = 0; /* the run opens no connection */
    k->allow_unused = true;
    CHECK(t, kit_prepare(k));
    kit_check_err(t, k, run(k), ANNAS_E_TIMEOUT, 0, TIMEOUT_MSG);
    CHECK_EQ_U64(t, ports_fake_stream_pos(&k->stream), 0);
    kit_check_fs_untouched(t, k);
    k = kit_begin(); /* the same for the API call alone */
    k->cfg.timeout_ms = 0;
    script(k);
    k->ok_connects = 0;
    k->allow_unused = true;
    CHECK(t, kit_prepare(k));
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_TIMEOUT, 0, TIMEOUT_MSG);
    CHECK_EQ_U64(t, ports_fake_stream_pos(&k->stream), 0);
}

CTEST_SUITE("timeout",
    {"deadline_before_the_api_connect", t_deadline_before_the_api_connect},
    {"deadline_after_the_api_connect_before_the_request_write", t_deadline_after_the_api_connect_before_the_request_write},
    {"deadline_before_the_api_read", t_deadline_before_the_api_read},
    {"deadline_before_the_download_connect", t_deadline_before_the_download_connect},
    {"deadline_after_the_download_connect_before_the_request_write", t_deadline_after_the_download_connect_before_the_request_write},
    {"deadline_before_the_download_read", t_deadline_before_the_download_read},
    {"deadline_before_create_temp", t_deadline_before_create_temp},
    {"deadline_before_an_fs_write", t_deadline_before_an_fs_write},
    {"deadline_between_the_last_write_and_the_commit", t_deadline_between_the_last_write_and_the_commit},
    {"no_deadline_check_after_the_last_port_call", t_no_deadline_check_after_the_last_port_call},
    {"fast_deadline_placements", t_fast_deadline_placements},
    {"fast_succeeds_with_the_clock_near_the_maximum", t_fast_succeeds_with_the_clock_near_the_maximum},
    {"download_succeeds_with_the_clock_near_the_maximum", t_download_succeeds_with_the_clock_near_the_maximum},
    {"saturated_deadline_is_the_maximum_not_a_wrap", t_saturated_deadline_is_the_maximum_not_a_wrap},
    {"zero_timeout_expires_at_the_first_check", t_zero_timeout_expires_at_the_first_check})
