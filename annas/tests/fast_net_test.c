/* annas_fast_download_url: transport failures, malformed responses and timeouts (API phase). */
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define JSONOK "{\"download_url\":\"https://dl.example.org/f/1\"}"

static annas_code_t run(kit_t *k) {
    annas_request_t r = kit_req(KIT_MD5);
    return kit_fast(k, &r);
}

typedef struct {
    ports_err_t err;
    annas_code_t code;
    const char *msg;
} errmap_t;

static const errmap_t k_rw[] = {{PORTS_E_NET, ANNAS_E_NET, "network error"},
                                {PORTS_E_CLOSED, ANNAS_E_NET, "network error"},
                                {PORTS_E_ARG, ANNAS_E_NET, "network error"},
                                {PORTS_E_TLS, ANNAS_E_NET, "TLS error"},
                                {PORTS_E_TIMEOUT, ANNAS_E_TIMEOUT, "operation timed out"}};

static void t_connect_failures(ctest_t *t) {
    static const errmap_t m[] = {{PORTS_E_NET, ANNAS_E_NET, "connection failed"},
                                 {PORTS_E_CLOSED, ANNAS_E_NET, "connection failed"},
                                 {PORTS_E_ARG, ANNAS_E_NET, "connection failed"},
                                 {PORTS_E_TLS, ANNAS_E_NET, "TLS error"},
                                 {PORTS_E_TIMEOUT, ANNAS_E_TIMEOUT, "operation timed out"}};
    for (size_t i = 0; i < COUNT(m); i++) {
        kit_t *k = kit_begin();
        kit_connect_err(k, "annas-archive.gl", 443, m[i].err);
        kit_check_err(t, k, run(k), m[i].code, 0, m[i].msg);
    }
}

static void t_write_failures(ctest_t *t) {
    for (size_t i = 0; i < COUNT(k_rw); i++) {
        kit_t *k = kit_begin();
        kit_connect(k, "annas-archive.gl", 443);
        kit_fail_write(k, k_rw[i].err);
        kit_check_err(t, k, run(k), k_rw[i].code, 0, k_rw[i].msg);
    }
}

static void t_read_failures(ctest_t *t) {
    for (size_t i = 0; i < COUNT(k_rw); i++) {
        kit_t *k = kit_begin();
        kit_connect(k, "annas-archive.gl", 443);
        kit_expect_api(k, KIT_MD5, "");
        kit_fail_read(k, k_rw[i].err);
        kit_check_err(t, k, run(k), k_rw[i].code, 0, k_rw[i].msg);
    }
}

static void t_read_failure_midway(ctest_t *t) {
    for (size_t i = 0; i < COUNT(k_rw); i++) {
        kit_t *k = kit_begin();
        kit_connect(k, "annas-archive.gl", 443);
        kit_expect_api(k, KIT_MD5, "");
        kit_respond(k, "HTTP/1.1 200 OK\r\nContent-Length: 45\r\n\r\n{\"download", 0);
        kit_fail_read(k, k_rw[i].err);
        kit_check_err(t, k, run(k), k_rw[i].code, 0, k_rw[i].msg);
    }
}

#define EARLY "connection closed before the response was complete"

static void t_eof_before_the_message_is_complete(ctest_t *t) {
    static const char *const partial[] = {"", "HTTP/1.1 2", "HTTP/1.1 200 OK\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 45\r\n\r\n",
                                          "HTTP/1.1 200 OK\r\nContent-Length: 45\r\n\r\n{\"download_url\":\"https://dl.exam",
                                          "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\n{\"dow",
                                          "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\n{\"dow\r\n",
                                          "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n"};
    for (size_t i = 0; i < COUNT(partial); i++) {
        kit_t *k = kit_begin();
        kit_connect(k, "annas-archive.gl", 443);
        kit_expect_api(k, KIT_MD5, "");
        if (partial[i][0] != '\0') {
            kit_respond(k, partial[i], 0);
        }
        kit_eof(k);
        kit_check_err(t, k, run(k), ANNAS_E_NET, 0, EARLY);
    }
    kit_t *k = kit_begin(); /* control: the same bytes, complete, are fine */
    kit_connect(k, "annas-archive.gl", 443);
    kit_expect_api(k, KIT_MD5, "");
    kit_respond(k, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2d\r\n" JSONOK "\r\n0\r\n\r\n", 0);
    kit_check_ok(t, k, run(k));
}

static void t_malformed_responses(ctest_t *t) {
    static const char *const bad[] = {
        "garbage\r\n\r\n", "HTTP/2 200 OK\r\n\r\n", "HTTP/1.1 100 Continue\r\n\r\n", "HTTP/1.1 199 X\r\n\r\n",
        "HTTP/1.1 600 X\r\n\r\n", "HTTP/1.1 200 OK\n\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nContent-Length: x\r\n\r\n{}", "HTTP/1.1 200 OK\r\nBad Header\r\n\r\n",
        "HTTP/1.1 200 OK\r\nA: b\r\n folded\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n{}\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n"};
    for (size_t i = 0; i < COUNT(bad); i++) {
        kit_t *k = kit_begin();
        kit_connect(k, "annas-archive.gl", 443);
        kit_expect_api(k, KIT_MD5, "");
        kit_respond(k, bad[i], 0);
        k->allow_unused = true;
        kit_check_err(t, k, run(k), ANNAS_E_PARSE, 0, "malformed HTTP response");
    }
}

static const char *resp(kit_t *k) { return kit_http(k, 200, "OK", "", JSONOK); }

static annas_code_t timed(kit_t *k, uint64_t advance, size_t chunk) {
    k->read_advance = advance;
    kit_connect(k, "annas-archive.gl", 443);
    kit_expect_api(k, KIT_MD5, "");
    kit_respond(k, resp(k), chunk);
    return run(k);
}

static size_t resp_len(kit_t *k) { return strlen(resp(k)); }

static void t_timeout_in_the_response(ctest_t *t) {
    kit_t *k = kit_begin();
    k->allow_unused = true;
    kit_set_timeout_s(k, 1);
    CHECK(t, resp_len(k) > 90);
    kit_check_err(t, k, timed(k, 600, 30), ANNAS_E_TIMEOUT, 0, "operation timed out");
    k = kit_begin(); /* control: faster clock ticks */
    kit_set_timeout_s(k, 1);
    kit_check_ok(t, k, timed(k, 100, 30));
}

static void t_timeout_boundary_is_exclusive(ctest_t *t) {
    kit_t *k = kit_begin();
    size_t len = resp_len(k);
    size_t c2 = (len + 1u) / 2u;
    size_t c3 = (len + 2u) / 3u;
    CHECK_EQ_U64(t, (len + c2 - 1u) / c2, 2);
    CHECK_EQ_U64(t, (len + c3 - 1u) / c3, 3);
    kit_set_timeout_s(k, 1);
    kit_check_ok(t, k, timed(k, 500, c2)); /* reads at 0 and 500; the clock shows 1000 after the last one */
    k = kit_begin();
    kit_set_timeout_s(k, 1);
    k->allow_unused = true;
    kit_check_err(t, k, timed(k, 500, c3), ANNAS_E_TIMEOUT, 0, "operation timed out"); /* third read at 1000 */
    k = kit_begin();
    kit_set_timeout_s(k, 1);
    kit_check_ok(t, k, timed(k, 499, c3)); /* third read at 998 */
}

static void t_timeout_scales_with_the_configured_seconds(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_set_timeout_s(k, 2);
    size_t c3 = (resp_len(k) + 2u) / 3u;
    kit_check_ok(t, k, timed(k, 500, c3));
    k = kit_begin();
    kit_set_timeout_s(k, 1);
    k->allow_unused = true;
    kit_check_err(t, k, timed(k, 500, c3), ANNAS_E_TIMEOUT, 0, "operation timed out");
}

static void t_deadline_is_relative_to_the_operation_start(ctest_t *t) {
    kit_t *k = kit_begin();
    ports_fake_clock_advance(&k->clock, 10000000); /* the clock origin is irrelevant */
    kit_set_timeout_s(k, 1);
    size_t c3 = (resp_len(k) + 2u) / 3u;
    kit_check_ok(t, k, timed(k, 499, c3));
}

static void t_each_call_uses_its_own_connection(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", resp(k));
    kit_api(k, KIT_MD5, "", kit_http(k, 404, "NF", "", "{\"error\":\"gone\"}"));
    annas_request_t r = kit_req(KIT_MD5);
    CHECK_EQ_I64(t, kit_fast(k, &r), ANNAS_OK);
    CHECK_EQ_U64(t, ports_fake_stream_closes(&k->stream), 1);
    CHECK_EQ_I64(t, annas_fast_download_url(&k->ctx, &r, &k->fast, &k->err), ANNAS_E_API);
    CHECK_EQ_STR(t, k->err.message, "404: gone");
    CHECK_EQ_I64(t, k->err.http_status, 404);
    CHECK(t, ports_fake_stream_done(&k->stream));
    CHECK(t, !ports_fake_stream_is_open(&k->stream));
    CHECK_EQ_U64(t, ports_fake_stream_closes(&k->stream), 2);
}

CTEST_SUITE("fast_net",
    {"connect_failures", t_connect_failures},
    {"write_failures", t_write_failures},
    {"read_failures", t_read_failures},
    {"read_failure_midway", t_read_failure_midway},
    {"eof_before_the_message_is_complete", t_eof_before_the_message_is_complete},
    {"malformed_responses", t_malformed_responses},
    {"timeout_in_the_response", t_timeout_in_the_response},
    {"timeout_boundary_is_exclusive", t_timeout_boundary_is_exclusive},
    {"timeout_scales_with_the_configured_seconds", t_timeout_scales_with_the_configured_seconds},
    {"deadline_is_relative_to_the_operation_start", t_deadline_is_relative_to_the_operation_start},
    {"each_call_uses_its_own_connection", t_each_call_uses_its_own_connection})
