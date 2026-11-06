/* Tests for ports/fakes.h: scripted-stream construction (script validation) and the port table. */
#include <stdint.h>
#include <string.h>

#include "ctest/ctest.h"
#include "ports/fakes.h"
#include "ports/ports.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static ports_fake_step_t step(ports_fake_step_kind_t k, const char *data, uint16_t port, size_t chunk, ports_err_t err) {
    ports_fake_step_t s = {k, core_str_from_cstr(data), port, chunk, err};
    return s;
}

static bool init1(ports_fake_stream_t *s, ports_fake_step_t st) { return ports_fake_stream_init(s, &st, 1, NULL, 0); }

static void t_valid_steps_of_every_kind_are_accepted(ctest_t *t) {
    ports_fake_stream_t s;
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_CONNECT, "h", 443, 0, PORTS_OK)));
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_CONNECT, "h", 65535, 0, PORTS_E_TLS)));
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_EXPECT, "x", 0, 0, PORTS_OK)));
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_RESPOND, "x", 0, 0, PORTS_OK)));
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_RESPOND, "x", 0, 7, PORTS_OK)));
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_EOF, "", 0, 0, PORTS_OK)));
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_FAIL_WRITE, "", 0, 0, PORTS_E_NET)));
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_FAIL_READ, "", 0, 0, PORTS_E_ARG)));
}

static void t_invalid_connect_steps_are_rejected(ctest_t *t) {
    ports_fake_stream_t s;
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_CONNECT, "h", 1, 0, PORTS_OK))); /* positive control */
    CHECK(t, !init1(&s, step(PORTS_FAKE_STEP_CONNECT, "", 443, 0, PORTS_OK)));
    CHECK(t, !init1(&s, step(PORTS_FAKE_STEP_CONNECT, "h", 0, 0, PORTS_OK)));
    CHECK(t, !init1(&s, step(PORTS_FAKE_STEP_CONNECT, "h", 443, 0, (ports_err_t)99)));
    ports_fake_step_t bad = step(PORTS_FAKE_STEP_CONNECT, "h", 443, 0, PORTS_OK);
    bad.data.ptr = NULL;
    bad.data.len = 1;
    CHECK(t, !init1(&s, bad));
}

static void t_invalid_data_steps_are_rejected(ctest_t *t) {
    ports_fake_stream_t s;
    ports_fake_step_t bad = step(PORTS_FAKE_STEP_EXPECT, "x", 0, 0, PORTS_OK);
    CHECK(t, init1(&s, bad)); /* positive control */
    CHECK(t, !init1(&s, step(PORTS_FAKE_STEP_EXPECT, "", 0, 0, PORTS_OK)));
    CHECK(t, !init1(&s, step(PORTS_FAKE_STEP_RESPOND, "", 0, 0, PORTS_OK)));
    bad.data.ptr = NULL;
    CHECK(t, !init1(&s, bad));
    bad = step(PORTS_FAKE_STEP_RESPOND, "x", 0, 0, PORTS_OK);
    CHECK(t, init1(&s, bad));
    bad.data.ptr = NULL;
    CHECK(t, !init1(&s, bad));
}

static void t_invalid_failure_steps_are_rejected(ctest_t *t) {
    ports_fake_stream_t s;
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_FAIL_WRITE, "", 0, 0, PORTS_E_NET))); /* positive controls */
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_FAIL_READ, "", 0, 0, PORTS_E_NET)));
    CHECK(t, !init1(&s, step(PORTS_FAKE_STEP_FAIL_WRITE, "", 0, 0, PORTS_OK)));
    CHECK(t, !init1(&s, step(PORTS_FAKE_STEP_FAIL_READ, "", 0, 0, PORTS_OK)));
    CHECK(t, !init1(&s, step(PORTS_FAKE_STEP_FAIL_WRITE, "", 0, 0, (ports_err_t)99)));
    CHECK(t, !init1(&s, step(PORTS_FAKE_STEP_FAIL_READ, "", 0, 0, (ports_err_t)99)));
}

static void t_unknown_step_kind_is_rejected(ctest_t *t) {
    ports_fake_stream_t s;
    CHECK(t, init1(&s, step(PORTS_FAKE_STEP_EOF, "", 0, 0, PORTS_OK))); /* positive control */
    CHECK(t, !init1(&s, step((ports_fake_step_kind_t)6, "x", 1, 0, PORTS_OK)));
    CHECK(t, !init1(&s, step((ports_fake_step_kind_t)100, "x", 1, 0, PORTS_OK)));
}

static void t_one_bad_step_among_good_ones_rejects_the_script(ctest_t *t) {
    static ports_fake_stream_t s;
    ports_fake_step_t good[] = {step(PORTS_FAKE_STEP_CONNECT, "h", 1, 0, PORTS_OK), step(PORTS_FAKE_STEP_EXPECT, "x", 0, 0, PORTS_OK),
                                step(PORTS_FAKE_STEP_RESPOND, "y", 0, 0, PORTS_OK), step(PORTS_FAKE_STEP_EOF, "", 0, 0, PORTS_OK)};
    ports_fake_step_t bad[] = {step(PORTS_FAKE_STEP_CONNECT, "h", 1, 0, PORTS_OK), step(PORTS_FAKE_STEP_EXPECT, "x", 0, 0, PORTS_OK),
                               step(PORTS_FAKE_STEP_RESPOND, "y", 0, 0, PORTS_OK), step(PORTS_FAKE_STEP_FAIL_READ, "", 0, 0, PORTS_OK)};
    CHECK(t, ports_fake_stream_init(&s, good, COUNT(good), NULL, 0));
    CHECK(t, !ports_fake_stream_init(&s, bad, COUNT(bad), NULL, 0));
    CHECK(t, ports_fake_stream_init(&s, bad, 3, NULL, 0)); /* the bad step is outside count */
}

static void t_init_rejects_null_arguments(ctest_t *t) {
    ports_fake_stream_t s;
    ports_fake_step_t ok = step(PORTS_FAKE_STEP_EOF, "", 0, 0, PORTS_OK);
    CHECK(t, ports_fake_stream_init(&s, &ok, 1, NULL, 0));
    CHECK(t, ports_fake_stream_init(&s, NULL, 0, NULL, 0));
    CHECK(t, !ports_fake_stream_init(NULL, &ok, 1, NULL, 0));
    CHECK(t, !ports_fake_stream_init(&s, NULL, 1, NULL, 0));
}

static void t_initial_state(ctest_t *t) {
    ports_fake_stream_t s;
    ports_fake_mismatch_t m = {PORTS_FAKE_MM_REQUEST_BYTES, 5, 5};
    ports_fake_step_t sc[] = {step(PORTS_FAKE_STEP_CONNECT, "h", 1, 0, PORTS_OK), step(PORTS_FAKE_STEP_EOF, "", 0, 0, PORTS_OK)};
    CHECK(t, ports_fake_stream_init(&s, sc, COUNT(sc), NULL, 0));
    CHECK(t, !ports_fake_stream_done(&s));
    CHECK(t, !ports_fake_stream_is_open(&s));
    CHECK_EQ_U64(t, ports_fake_stream_pos(&s), 0);
    CHECK_EQ_U64(t, ports_fake_stream_closes(&s), 0);
    CHECK(t, !ports_fake_stream_mismatch(&s, &m));
    CHECK_EQ_I64(t, m.kind, PORTS_FAKE_MM_REQUEST_BYTES); /* untouched */
    CHECK_EQ_U64(t, m.step, 5);
}

static void t_port_is_a_const_table_with_this_context(ctest_t *t) {
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, NULL, 0, NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    CHECK(t, p.ops != NULL);
    CHECK(t, p.ops->connect != NULL && p.ops->write != NULL && p.ops->read != NULL && p.ops->close != NULL);
    CHECK(t, p.ctx == (void *)&s);
    ports_fake_stream_t s2;
    CHECK(t, ports_fake_stream_init(&s2, NULL, 0, NULL, 0));
    CHECK(t, ports_fake_stream_port(&s2).ops == p.ops);
    CHECK(t, ports_fake_stream_port(&s2).ctx != p.ctx);
}

static void t_step_array_is_borrowed_not_copied(ctest_t *t) {
    ports_fake_step_t sc[] = {step(PORTS_FAKE_STEP_CONNECT, "a", 1, 0, PORTS_OK)};
    ports_fake_stream_t s;
    CHECK(t, ports_fake_stream_init(&s, sc, 1, NULL, 0));
    ports_stream_t p = ports_fake_stream_port(&s);
    sc[0] = step(PORTS_FAKE_STEP_CONNECT, "b", 2, 0, PORTS_OK); /* edited after init */
    CHECK_EQ_I64(t, p.ops->connect(p.ctx, core_str_from_cstr("b"), 2, 0), PORTS_OK);
}

CTEST_SUITE("stream_init",
    {"valid_steps_of_every_kind_are_accepted", t_valid_steps_of_every_kind_are_accepted},
    {"invalid_connect_steps_are_rejected", t_invalid_connect_steps_are_rejected},
    {"invalid_data_steps_are_rejected", t_invalid_data_steps_are_rejected},
    {"invalid_failure_steps_are_rejected", t_invalid_failure_steps_are_rejected},
    {"unknown_step_kind_is_rejected", t_unknown_step_kind_is_rejected},
    {"one_bad_step_among_good_ones_rejects_the_script", t_one_bad_step_among_good_ones_rejects_the_script},
    {"init_rejects_null_arguments", t_init_rejects_null_arguments},
    {"initial_state", t_initial_state},
    {"port_is_a_const_table_with_this_context", t_port_is_a_const_table_with_this_context},
    {"step_array_is_borrowed_not_copied", t_step_array_is_borrowed_not_copied})
