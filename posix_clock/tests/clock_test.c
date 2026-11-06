/* posix_clock: the ports_clock_t adapter, through its port, over scripted readings and over the real table. */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "ctest/ctest.h"
#include "posix_clock/posix_clock.h"
#include "ports/ports.h"

typedef struct {
    int64_t sec;
    int64_t nsec;
    int64_t rc; /* 0 = reading valid, else returned as the error */
} reading_t;

typedef struct {
    const reading_t *script;
    size_t n;
    size_t calls;
    void *seen_ctx;
} fake_t;

static int64_t fake_gettime(void *ctx, int64_t *sec, int64_t *nsec) {
    fake_t *f = ctx;
    const reading_t *r = &f->script[f->calls < f->n ? f->calls : f->n - 1u];
    f->calls++;
    if (r->rc != 0) {
        return r->rc;
    }
    *sec = r->sec;
    *nsec = r->nsec;
    return 0;
}

static const posix_clock_sys_t k_fake = {fake_gettime};

static posix_clock_t g_clk;
static fake_t g_fake;

static ports_clock_t start(ctest_t *t, const reading_t *script, size_t n) {
    g_fake.script = script;
    g_fake.n = n;
    g_fake.calls = 0;
    CHECK(t, posix_clock_init(&g_clk, &k_fake, &g_fake));
    return posix_clock_port(&g_clk);
}

static uint64_t now(ports_clock_t c) { return c.ops->now_ms(c.ctx); }

static void t_init_validates_and_makes_no_seam_call(ctest_t *t) {
    static const reading_t one[] = {{1, 0, 0}};
    posix_clock_t c;
    posix_clock_sys_t hole = {NULL};
    memset(&c, 0x77, sizeof c);
    g_fake.script = one;
    g_fake.n = 1;
    g_fake.calls = 0;
    CHECK(t, !posix_clock_init(NULL, &k_fake, &g_fake));
    CHECK(t, !posix_clock_init(&c, NULL, &g_fake));
    CHECK(t, !posix_clock_init(&c, &hole, &g_fake));
    CHECK(t, !posix_clock_init_real(NULL));
    CHECK_EQ_U64(t, ((unsigned char *)&c)[0], 0x77); /* untouched on failure */
    CHECK(t, posix_clock_init(&c, &k_fake, &g_fake));
    CHECK(t, posix_clock_init(&c, &k_fake, NULL)); /* a NULL context is fine */
    CHECK_EQ_U64(t, g_fake.calls, 0);
    CHECK_EQ_U64(t, posix_clock_faults(&c), 0);
    CHECK_EQ_U64(t, posix_clock_faults(NULL), 0);
}

static void t_converts_seconds_and_nanoseconds_to_floor_milliseconds(ctest_t *t) {
    static const reading_t s[] = {{0, 0, 0},         {0, 999999, 0},      {0, 1000000, 0},     {5, 123456789, 0},
                                  {5, 999999999, 0}, {6, 0, 0},           {1000, 500000000, 0}, {86400, 1000000, 0}};
    static const uint64_t want[] = {0, 0, 1, 5123, 5999, 6000, 1000500, 86400001};
    ports_clock_t c = start(t, s, sizeof s / sizeof s[0]);
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        CHECK_EQ_U64(t, now(c), want[i]);
    }
    CHECK_EQ_U64(t, g_fake.calls, sizeof want / sizeof want[0]); /* exactly one seam call per now_ms */
    CHECK_EQ_U64(t, posix_clock_faults(&g_clk), 0);
}

static void t_context_reaches_the_seam(ctest_t *t) {
    static const reading_t s[] = {{2, 0, 0}};
    ports_clock_t c = start(t, s, 1);
    CHECK_EQ_U64(t, now(c), 2000);
    CHECK_EQ_U64(t, g_fake.calls, 1); /* the fake dereferences its ctx: a wrong ctx would have crashed */
    CHECK(t, c.ctx == (void *)&g_clk);
    CHECK(t, c.ops != NULL && c.ops->now_ms != NULL);
    ports_clock_t d = posix_clock_port(&g_clk);
    CHECK(t, d.ops == c.ops);
}

static void t_never_decreases(ctest_t *t) {
    static const reading_t s[] = {{10, 0, 0}, {9, 999000000, 0}, {10, 0, 0}, {10, 1000000, 0}, {3, 0, 0}, {11, 0, 0}};
    static const uint64_t want[] = {10000, 10000, 10000, 10001, 10001, 11000};
    ports_clock_t c = start(t, s, 6);
    for (size_t i = 0; i < 6; i++) {
        CHECK_EQ_U64(t, now(c), want[i]);
    }
    CHECK_EQ_U64(t, posix_clock_faults(&g_clk), 0); /* going backwards is clamped, not a fault */
}

static void t_seam_error_returns_the_last_value_and_counts_a_fault(ctest_t *t) {
    static const int errs[] = {EINVAL, EFAULT, EPERM, EINTR};
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        const reading_t s[] = {{0, 0, -errs[i]}, {4, 0, 0}, {0, 0, -errs[i]}, {0, 0, -errs[i]}, {4, 5000000, 0}};
        ports_clock_t c = start(t, s, 5);
        CHECK_EQ_U64(t, now(c), 0); /* before any success: 0 */
        CHECK_EQ_U64(t, posix_clock_faults(&g_clk), 1);
        CHECK_EQ_U64(t, now(c), 4000); /* control: recovers */
        CHECK_EQ_U64(t, now(c), 4000);
        CHECK_EQ_U64(t, now(c), 4000);
        CHECK_EQ_U64(t, posix_clock_faults(&g_clk), 3);
        CHECK_EQ_U64(t, now(c), 4005);
        CHECK_EQ_U64(t, g_fake.calls, 5); /* no retry on a fault, even for EINTR */
    }
}

static void t_invalid_readings_are_faults(ctest_t *t) {
    static const reading_t s[] = {{7, 0, 0},           {-1, 0, 0},         {8, -1, 0},       {8, 1000000000, 0},
                                  {INT64_MIN, 0, 0},   {9, INT64_MAX, 0},  {8, 999999999, 0}, {-5, 500000000, 0}};
    static const uint64_t want[] = {7000, 7000, 7000, 7000, 7000, 7000, 8999, 8999};
    ports_clock_t c = start(t, s, sizeof s / sizeof s[0]);
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        CHECK_EQ_U64(t, now(c), want[i]);
    }
    CHECK_EQ_U64(t, posix_clock_faults(&g_clk), 6); /* -1 sec, -1 nsec, 1e9 nsec, INT64_MIN, huge nsec, -5 sec */
}

static void t_first_reading_may_be_zero_and_zero_nsec_boundary_is_valid(ctest_t *t) {
    static const reading_t s[] = {{0, 0, 0}, {0, 999999999, 0}, {1, 0, 0}};
    ports_clock_t c = start(t, s, 3);
    CHECK_EQ_U64(t, now(c), 0);
    CHECK_EQ_U64(t, now(c), 999);
    CHECK_EQ_U64(t, now(c), 1000);
    CHECK_EQ_U64(t, posix_clock_faults(&g_clk), 0);
}

static void t_conversion_saturates_instead_of_wrapping(ctest_t *t) {
    /* UINT64_MAX ms = 18446744073709551 s + 615 ms */
    static const reading_t s[] = {{18446744073709550LL, 999000000, 0},
                                  {18446744073709551LL, 0, 0},
                                  {18446744073709551LL, 614999999, 0},
                                  {18446744073709551LL, 615000000, 0},
                                  {18446744073709551LL, 616000000, 0},
                                  {18446744073709551LL, 999999999, 0},
                                  {18446744073709552LL, 0, 0},
                                  {INT64_MAX, 999999999, 0}};
    static const uint64_t want[] = {UINT64_C(18446744073709550999), UINT64_C(18446744073709551000),
                                    UINT64_C(18446744073709551614), UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
                                    UINT64_MAX};
    ports_clock_t c = start(t, s, sizeof s / sizeof s[0]);
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        CHECK_EQ_U64(t, now(c), want[i]);
    }
    CHECK_EQ_U64(t, posix_clock_faults(&g_clk), 0);
}

static void t_saturated_clock_stays_saturated(ctest_t *t) {
    static const reading_t s[] = {{INT64_MAX, 0, 0}, {1, 0, 0}, {INT64_MAX, 0, 0}};
    ports_clock_t c = start(t, s, 3);
    CHECK_EQ_U64(t, now(c), UINT64_MAX);
    CHECK_EQ_U64(t, now(c), UINT64_MAX);
    CHECK_EQ_U64(t, now(c), UINT64_MAX);
}

static void t_null_clock_port_returns_zero(ctest_t *t) {
    ports_clock_t c = posix_clock_port(NULL);
    CHECK(t, c.ops != NULL && c.ops->now_ms != NULL);
    CHECK_EQ_U64(t, now(c), 0);
    CHECK_EQ_U64(t, now(c), 0);
    static const reading_t s[] = {{3, 0, 0}};
    ports_clock_t d = start(t, s, 1); /* control: a real object reads */
    CHECK_EQ_U64(t, now(d), 3000);
}

static void t_two_clocks_are_independent(ctest_t *t) {
    static const reading_t sa[] = {{100, 0, 0}, {1, 0, 0}};
    static const reading_t sb[] = {{5, 0, 0}, {6, 0, 0}};
    fake_t fa = {sa, 2, 0, NULL};
    fake_t fb = {sb, 2, 0, NULL};
    posix_clock_t a;
    posix_clock_t b;
    CHECK(t, posix_clock_init(&a, &k_fake, &fa));
    CHECK(t, posix_clock_init(&b, &k_fake, &fb));
    ports_clock_t pa = posix_clock_port(&a);
    ports_clock_t pb = posix_clock_port(&b);
    CHECK_EQ_U64(t, now(pa), 100000);
    CHECK_EQ_U64(t, now(pb), 5000); /* b's clamp is not a's */
    CHECK_EQ_U64(t, now(pa), 100000);
    CHECK_EQ_U64(t, now(pb), 6000);
}

/* ---- the real table ----------------------------------------------------------------------------------- */

static void t_real_table_reads_a_valid_monotonic_time(ctest_t *t) {
    int64_t sec = -1;
    int64_t nsec = -1;
    CHECK(t, posix_clock_sys_libc.gettime != NULL);
    CHECK_EQ_I64(t, posix_clock_sys_libc.gettime(NULL, &sec, &nsec), 0);
    CHECK(t, sec >= 0);
    CHECK(t, nsec >= 0 && nsec <= 999999999);
    CHECK(t, sec < 1000000000); /* CLOCK_MONOTONIC counts from boot (< 31 years); CLOCK_REALTIME is ~1.7e9 s */
}

static void t_real_clock_never_decreases_and_advances(ctest_t *t) {
    posix_clock_t c;
    CHECK(t, posix_clock_init_real(&c));
    ports_clock_t p = posix_clock_port(&c);
    uint64_t first = now(p);
    uint64_t prev = first;
    for (int i = 0; i < 10000; i++) {
        uint64_t v = now(p);
        CHECK(t, v >= prev);
        prev = v;
    }
    while (now(p) < first + 2u) { /* busy-wait for two ticks; the framework's 10 s limit bounds it */
    }
    CHECK(t, now(p) >= first + 2u);
    CHECK_EQ_U64(t, posix_clock_faults(&c), 0);
    CHECK(t, first < UINT64_C(1000000000000)); /* ms since boot, not since 1970 */
}

CTEST_SUITE("posix_clock",
    {"init_validates_and_makes_no_seam_call", t_init_validates_and_makes_no_seam_call},
    {"converts_seconds_and_nanoseconds_to_floor_milliseconds", t_converts_seconds_and_nanoseconds_to_floor_milliseconds},
    {"context_reaches_the_seam", t_context_reaches_the_seam},
    {"never_decreases", t_never_decreases},
    {"seam_error_returns_the_last_value_and_counts_a_fault", t_seam_error_returns_the_last_value_and_counts_a_fault},
    {"invalid_readings_are_faults", t_invalid_readings_are_faults},
    {"first_reading_may_be_zero_and_zero_nsec_boundary_is_valid", t_first_reading_may_be_zero_and_zero_nsec_boundary_is_valid},
    {"conversion_saturates_instead_of_wrapping", t_conversion_saturates_instead_of_wrapping},
    {"saturated_clock_stays_saturated", t_saturated_clock_stays_saturated},
    {"null_clock_port_returns_zero", t_null_clock_port_returns_zero},
    {"two_clocks_are_independent", t_two_clocks_are_independent},
    {"real_table_reads_a_valid_monotonic_time", t_real_table_reads_a_valid_monotonic_time},
    {"real_clock_never_decreases_and_advances", t_real_clock_never_decreases_and_advances})
