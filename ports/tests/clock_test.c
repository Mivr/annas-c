/* Tests for ports/fakes.h: the manual clock, used only through ports_clock_t. */
#include <stdint.h>

#include "ctest/ctest.h"
#include "ports/fakes.h"
#include "ports/ports.h"

static uint64_t now(ports_clock_t c) { return c.ops->now_ms(c.ctx); }

static void t_init_shows_start_value(ctest_t *t) {
    ports_fake_clock_t a;
    ports_fake_clock_t b;
    CHECK(t, ports_fake_clock_init(&a, 0));
    CHECK(t, ports_fake_clock_init(&b, 123456789u));
    CHECK_EQ_U64(t, now(ports_fake_clock_port(&a)), 0);
    CHECK_EQ_U64(t, now(ports_fake_clock_port(&b)), 123456789u);
}

static void t_reading_does_not_advance(ctest_t *t) {
    ports_fake_clock_t c;
    CHECK(t, ports_fake_clock_init(&c, 40));
    ports_clock_t p = ports_fake_clock_port(&c);
    CHECK_EQ_U64(t, now(p), 40);
    CHECK_EQ_U64(t, now(p), 40);
    CHECK_EQ_U64(t, now(p), 40);
}

static void t_advance_accumulates(ctest_t *t) {
    ports_fake_clock_t c;
    CHECK(t, ports_fake_clock_init(&c, 10));
    ports_clock_t p = ports_fake_clock_port(&c);
    ports_fake_clock_advance(&c, 5);
    CHECK_EQ_U64(t, now(p), 15);
    ports_fake_clock_advance(&c, 0);
    CHECK_EQ_U64(t, now(p), 15);
    ports_fake_clock_advance(&c, 1000);
    CHECK_EQ_U64(t, now(p), 1015);
    ports_fake_clock_advance(&c, 1);
    CHECK_EQ_U64(t, now(p), 1016);
}

static void t_advance_saturates_never_wraps(ctest_t *t) {
    ports_fake_clock_t c;
    CHECK(t, ports_fake_clock_init(&c, UINT64_MAX - 5u));
    ports_clock_t p = ports_fake_clock_port(&c);
    ports_fake_clock_advance(&c, 5);
    CHECK_EQ_U64(t, now(p), UINT64_MAX); /* exact fit is not saturation */
    ports_fake_clock_advance(&c, 1);
    CHECK_EQ_U64(t, now(p), UINT64_MAX);
    ports_fake_clock_advance(&c, UINT64_MAX);
    CHECK_EQ_U64(t, now(p), UINT64_MAX);

    CHECK(t, ports_fake_clock_init(&c, 10));
    ports_fake_clock_advance(&c, UINT64_MAX); /* 10 + MAX overflows */
    CHECK_EQ_U64(t, now(p), UINT64_MAX);
}

static void t_never_decreases_over_many_steps(ctest_t *t) {
    ports_fake_clock_t c;
    CHECK(t, ports_fake_clock_init(&c, 0));
    ports_clock_t p = ports_fake_clock_port(&c);
    uint64_t prev = now(p);
    int ok = 1;
    for (uint64_t i = 0; i < 200; i++) {
        ports_fake_clock_advance(&c, i % 7u);
        uint64_t cur = now(p);
        ok = ok && cur >= prev && cur - prev == i % 7u;
        prev = cur;
    }
    CHECK(t, ok);
    CHECK_EQ_U64(t, prev, 594);
}

static void t_set_forward_and_equal(ctest_t *t) {
    ports_fake_clock_t c;
    CHECK(t, ports_fake_clock_init(&c, 100));
    ports_clock_t p = ports_fake_clock_port(&c);
    CHECK(t, ports_fake_clock_set(&c, 250));
    CHECK_EQ_U64(t, now(p), 250);
    CHECK(t, ports_fake_clock_set(&c, 250)); /* equal is allowed */
    CHECK_EQ_U64(t, now(p), 250);
    CHECK(t, ports_fake_clock_set(&c, UINT64_MAX));
    CHECK_EQ_U64(t, now(p), UINT64_MAX);
}

static void t_set_backwards_is_refused(ctest_t *t) {
    ports_fake_clock_t c;
    CHECK(t, ports_fake_clock_init(&c, 100));
    ports_clock_t p = ports_fake_clock_port(&c);
    CHECK(t, !ports_fake_clock_set(&c, 99));
    CHECK_EQ_U64(t, now(p), 100); /* unchanged */
    CHECK(t, !ports_fake_clock_set(&c, 0));
    CHECK_EQ_U64(t, now(p), 100);
    CHECK(t, ports_fake_clock_set(&c, 100)); /* positive control: the boundary itself is fine */
    CHECK(t, ports_fake_clock_set(&c, 101));
    CHECK_EQ_U64(t, now(p), 101);
}

static void t_clocks_are_independent(ctest_t *t) {
    ports_fake_clock_t a;
    ports_fake_clock_t b;
    CHECK(t, ports_fake_clock_init(&a, 1));
    CHECK(t, ports_fake_clock_init(&b, 1));
    ports_fake_clock_advance(&a, 9);
    CHECK_EQ_U64(t, now(ports_fake_clock_port(&a)), 10);
    CHECK_EQ_U64(t, now(ports_fake_clock_port(&b)), 1);
    CHECK(t, ports_fake_clock_set(&b, 77));
    CHECK_EQ_U64(t, now(ports_fake_clock_port(&a)), 10);
    CHECK_EQ_U64(t, now(ports_fake_clock_port(&b)), 77);
}

static void t_null_clock_is_safe(ctest_t *t) {
    ports_fake_clock_t c;
    CHECK(t, !ports_fake_clock_init(NULL, 5));
    CHECK(t, ports_fake_clock_init(&c, 5)); /* positive control */
    ports_fake_clock_advance(NULL, 3);
    CHECK(t, !ports_fake_clock_set(NULL, 3));
    CHECK_EQ_U64(t, now(ports_fake_clock_port(NULL)), 0);
    CHECK_EQ_U64(t, now(ports_fake_clock_port(&c)), 5);
}

static void t_port_is_a_const_table_with_this_context(ctest_t *t) {
    ports_fake_clock_t c;
    CHECK(t, ports_fake_clock_init(&c, 8));
    ports_clock_t p = ports_fake_clock_port(&c);
    CHECK(t, p.ops != NULL);
    CHECK(t, p.ops->now_ms != NULL);
    CHECK(t, p.ctx == (void *)&c);
    ports_clock_t q = ports_fake_clock_port(&c);
    CHECK(t, q.ops == p.ops); /* one shared const table */
}

CTEST_SUITE("clock",
    {"init_shows_start_value", t_init_shows_start_value},
    {"reading_does_not_advance", t_reading_does_not_advance},
    {"advance_accumulates", t_advance_accumulates},
    {"advance_saturates_never_wraps", t_advance_saturates_never_wraps},
    {"never_decreases_over_many_steps", t_never_decreases_over_many_steps},
    {"set_forward_and_equal", t_set_forward_and_equal},
    {"set_backwards_is_refused", t_set_backwards_is_refused},
    {"clocks_are_independent", t_clocks_are_independent},
    {"null_clock_is_safe", t_null_clock_is_safe},
    {"port_is_a_const_table_with_this_context", t_port_is_a_const_table_with_this_context})
