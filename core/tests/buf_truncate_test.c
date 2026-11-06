/* Tests for core_buf_truncate (core/buf.h). Public header only. */
#include <stdint.h>
#include <string.h>

#include "core/buf.h"
#include "ctest/ctest.h"

#define SENT 'Z'

typedef struct {
    char guard_lo[8];
    char s[64];
    char guard_hi[8];
} store_t;

static void store_fill(store_t *st) { memset(st, SENT, sizeof *st); }

/* Guards and everything at or beyond index `from` of s must be sentinel. */
static void check_guards(ctest_t *t, const store_t *st, size_t from) {
    size_t i;
    for (i = 0; i < sizeof st->guard_lo; i++) CHECK_EQ_U64(t, (unsigned char)st->guard_lo[i], SENT);
    for (i = from; i < sizeof st->s; i++) CHECK_EQ_U64(t, (unsigned char)st->s[i], SENT);
    for (i = 0; i < sizeof st->guard_hi; i++) CHECK_EQ_U64(t, (unsigned char)st->guard_hi[i], SENT);
}

static void check_content(ctest_t *t, const core_buf_t *b, const char *want, size_t wlen) {
    core_str_t v = core_buf_str(b);
    const char *c = core_buf_cstr(b);
    CHECK_EQ_U64(t, core_buf_len(b), wlen);
    CHECK_EQ_U64(t, v.len, wlen);
    CHECK(t, c != NULL);
    if (c != NULL) {
        CHECK_EQ_MEM(t, c, wlen, want, wlen);
        CHECK_EQ_U64(t, (unsigned char)c[wlen], 0);
    }
}

static void start(store_t *st, core_buf_t *b, size_t cap, const char *init, size_t n, ctest_t *t) {
    store_fill(st);
    CHECK(t, core_buf_init(b, st->s, cap));
    core_buf_put(b, init, n);
}

static void t_shrinks_to_exact_prefix(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 16, "abcdef", 6, t);
    check_content(t, &b, "abcdef", 6);   /* positive control: content before */
    core_buf_truncate(&b, 3);
    check_content(t, &b, "abc", 3);
    CHECK_EQ_STR(t, core_buf_cstr(&b), "abc");
    CHECK_EQ_U64(t, (unsigned char)st.s[3], 0);
    CHECK(t, core_buf_cstr(&b) == st.s);
    CHECK(t, core_buf_str(&b).ptr == st.s);
    CHECK(t, !core_buf_overflow(&b));
    check_guards(t, &st, 7);   /* old content + its NUL (index 6) may remain */
}

static void t_every_length_from_full_down_to_zero(ctest_t *t) {
    static const char full[] = "0123456789";
    size_t n;
    for (n = 0; n <= 10; n++) {
        store_t st;
        core_buf_t b = {0};
        start(&st, &b, 16, full, 10, t);
        core_buf_truncate(&b, n);
        check_content(t, &b, full, n);
        CHECK(t, !core_buf_overflow(&b));
        check_guards(t, &st, 11);
    }
}

static void t_truncate_by_one(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 8, "xyz", 3, t);
    core_buf_truncate(&b, 2);
    check_content(t, &b, "xy", 2);
    core_buf_truncate(&b, 1);
    check_content(t, &b, "x", 1);
    core_buf_truncate(&b, 0);
    check_content(t, &b, "", 0);
}

static void t_zero_empties(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 8, "abc", 3, t);
    CHECK_EQ_U64(t, core_buf_len(&b), 3);
    core_buf_truncate(&b, 0);
    CHECK_EQ_U64(t, core_buf_len(&b), 0);
    CHECK_EQ_U64(t, core_buf_str(&b).len, 0);
    CHECK_EQ_U64(t, (unsigned char)st.s[0], 0);
    CHECK_EQ_STR(t, core_buf_cstr(&b), "");
    CHECK(t, !core_buf_overflow(&b));
    check_guards(t, &st, 8);
}

static void t_equal_length_is_noop(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 16, "abcde", 5, t);
    core_buf_truncate(&b, 5);
    check_content(t, &b, "abcde", 5);
    CHECK(t, !core_buf_overflow(&b));
    check_guards(t, &st, 6);
}

static void t_longer_than_content_is_noop(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 16, "abcde", 5, t);
    core_buf_truncate(&b, 6);
    check_content(t, &b, "abcde", 5);
    core_buf_truncate(&b, 15);              /* up to cap-1 */
    check_content(t, &b, "abcde", 5);
    core_buf_truncate(&b, 16);              /* == cap */
    check_content(t, &b, "abcde", 5);
    core_buf_truncate(&b, SIZE_MAX);
    check_content(t, &b, "abcde", 5);
    CHECK(t, !core_buf_overflow(&b));
    check_guards(t, &st, 6);
    /* positive control: a real shrink on the same builder does work */
    core_buf_truncate(&b, 4);
    check_content(t, &b, "abcd", 4);
}

static void t_truncate_empty_builder(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 8, "", 0, t);
    core_buf_truncate(&b, 0);
    check_content(t, &b, "", 0);
    core_buf_truncate(&b, 3);
    check_content(t, &b, "", 0);
    CHECK(t, !core_buf_overflow(&b));
    check_guards(t, &st, 1);
}

static void t_idempotent(ctest_t *t) {
    store_t st1, st2;
    core_buf_t b1 = {0}, b2 = {0};
    start(&st1, &b1, 16, "hello world", 11, t);
    start(&st2, &b2, 16, "hello world", 11, t);
    core_buf_truncate(&b1, 5);
    core_buf_truncate(&b2, 5);
    core_buf_truncate(&b2, 5);
    core_buf_truncate(&b2, 5);
    check_content(t, &b1, "hello", 5);
    check_content(t, &b2, "hello", 5);
    CHECK_EQ_MEM(t, st1.s, 6, st2.s, 6);
}

static void t_cstr_terminator_after_shrink(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 16, "abcdef", 6, t);
    core_buf_truncate(&b, 2);
    CHECK_EQ_U64(t, (unsigned char)core_buf_cstr(&b)[2], 0);
    CHECK_EQ_U64(t, strlen(core_buf_cstr(&b)), 2);
    core_buf_truncate(&b, 1);
    CHECK_EQ_U64(t, strlen(core_buf_cstr(&b)), 1);
    core_buf_truncate(&b, 0);
    CHECK_EQ_U64(t, strlen(core_buf_cstr(&b)), 0);
}

static void t_binary_content_with_nul(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    static const char data[] = {'a', 0, 'b', 0, 'c'};
    start(&st, &b, 16, data, 5, t);
    CHECK_EQ_U64(t, core_buf_len(&b), 5);
    core_buf_truncate(&b, 4);
    check_content(t, &b, data, 4);          /* embedded NULs kept */
    core_buf_truncate(&b, 2);
    check_content(t, &b, data, 2);
    core_buf_truncate(&b, 1);
    check_content(t, &b, data, 1);
}

static void t_overflow_not_set_by_truncate(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 8, "abc", 3, t);
    core_buf_truncate(&b, 1);
    core_buf_truncate(&b, 0);
    core_buf_truncate(&b, 100);
    CHECK(t, !core_buf_overflow(&b));
}

static void t_overflow_stays_set(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 8, "abc", 3, t);
    core_buf_put(&b, "too long for it", 15);
    CHECK(t, core_buf_overflow(&b));        /* positive control */
    check_content(t, &b, "abc", 3);         /* all-or-nothing kept the old content */
    core_buf_truncate(&b, 2);
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "ab", 2);
    core_buf_truncate(&b, 0);
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "", 0);
    core_buf_truncate(&b, 50);              /* no-op path */
    CHECK(t, core_buf_overflow(&b));
}

static void t_appends_ignored_after_overflowed_truncate(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 8, "abc", 3, t);
    core_buf_put(&b, "0123456789", 10);
    core_buf_truncate(&b, 1);
    core_buf_put(&b, "x", 1);               /* would fit, but overflow is sticky */
    core_buf_put_char(&b, 'y');
    core_buf_put_u64(&b, 7);
    check_content(t, &b, "a", 1);
    CHECK(t, core_buf_overflow(&b));
    /* init clears everything */
    CHECK(t, core_buf_init(&b, st.s, 8));
    core_buf_put(&b, "x", 1);
    check_content(t, &b, "x", 1);
    CHECK(t, !core_buf_overflow(&b));
}

static void t_append_after_truncate(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 16, "abcdef", 6, t);
    core_buf_truncate(&b, 3);
    core_buf_put(&b, "XY", 2);
    check_content(t, &b, "abcXY", 5);
    core_buf_truncate(&b, 0);
    core_buf_put_str(&b, (core_str_t){"Q", 1});
    core_buf_put_char(&b, '/');
    core_buf_put_u64(&b, 90);
    check_content(t, &b, "Q/90", 4);
    CHECK(t, !core_buf_overflow(&b));
    check_guards(t, &st, 16);
}

static void t_truncate_frees_room_for_append(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 6, "abcde", 5, t);       /* full: 5 = cap-1 */
    core_buf_put(&b, "f", 1);               /* positive control: no room */
    CHECK(t, core_buf_overflow(&b));
    CHECK_EQ_U64(t, core_buf_len(&b), 5);
    CHECK(t, core_buf_init(&b, st.s, 6));
    core_buf_put(&b, "abcde", 5);
    core_buf_truncate(&b, 2);
    core_buf_put(&b, "XYZ", 3);            /* exactly refills to capacity */
    CHECK(t, !core_buf_overflow(&b));
    check_content(t, &b, "abXYZ", 5);
    core_buf_put(&b, "!", 1);
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "abXYZ", 5);
}

static void t_capacity_boundary(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 4, "abc", 3, t);         /* content at cap-1 */
    check_content(t, &b, "abc", 3);
    core_buf_truncate(&b, 3);
    check_content(t, &b, "abc", 3);
    core_buf_truncate(&b, 4);               /* == cap: no-op, must not write st.s[4] */
    check_content(t, &b, "abc", 3);
    CHECK_EQ_U64(t, (unsigned char)st.s[4], SENT);
    core_buf_truncate(&b, 2);
    check_content(t, &b, "ab", 2);
    CHECK_EQ_U64(t, (unsigned char)st.s[4], SENT);
    check_guards(t, &st, 4);
}

static void t_capacity_one(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 1));
    core_buf_truncate(&b, 0);
    check_content(t, &b, "", 0);
    core_buf_truncate(&b, 1);
    check_content(t, &b, "", 0);
    CHECK(t, !core_buf_overflow(&b));
    check_guards(t, &st, 1);
}

static void t_capacity_zero(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 0));
    core_buf_truncate(&b, 0);
    core_buf_truncate(&b, 5);
    CHECK_EQ_U64(t, core_buf_len(&b), 0);
    CHECK(t, !core_buf_overflow(&b));
    CHECK_EQ_STR(t, core_buf_cstr(&b), "");
    check_guards(t, &st, 0);                /* nothing written, not even a NUL */
    core_buf_put(&b, "a", 1);
    CHECK(t, core_buf_overflow(&b));        /* positive control */
    core_buf_truncate(&b, 0);
    CHECK(t, core_buf_overflow(&b));
    check_guards(t, &st, 0);
}

static void t_capacity_zero_null_storage(ctest_t *t) {
    core_buf_t b = {0};
    CHECK(t, core_buf_init(&b, NULL, 0));
    core_buf_truncate(&b, 0);
    core_buf_truncate(&b, 1);
    CHECK_EQ_U64(t, core_buf_len(&b), 0);
    CHECK(t, !core_buf_overflow(&b));
}

static void t_null_builder(ctest_t *t) {
    core_buf_truncate(NULL, 0);
    core_buf_truncate(NULL, 3);
    core_buf_truncate(NULL, SIZE_MAX);
    CHECK_EQ_U64(t, core_buf_len(NULL), 0);
    CHECK(t, !core_buf_overflow(NULL));
    CHECK_EQ_STR(t, core_buf_cstr(NULL), "");
}

static void t_truncate_only_affects_given_builder(ctest_t *t) {
    store_t s1, s2;
    core_buf_t a = {0}, b = {0};
    start(&s1, &a, 16, "aaaa", 4, t);
    start(&s2, &b, 16, "bbbb", 4, t);
    core_buf_truncate(&a, 1);
    check_content(t, &a, "a", 1);
    check_content(t, &b, "bbbb", 4);
}

static void t_dot_segment_cursor_usage(ctest_t *t) {
    /* the intended use: write cursor, on ".." drop back to the previous '/' */
    store_t st;
    core_buf_t b = {0};
    start(&st, &b, 32, "/a/b/c", 6, t);
    core_buf_truncate(&b, 4);               /* drop "/c" -> "/a/b" */
    check_content(t, &b, "/a/b", 4);
    core_buf_truncate(&b, 2);               /* drop "/b" -> "/a" */
    check_content(t, &b, "/a", 2);
    core_buf_put(&b, "/d", 2);
    check_content(t, &b, "/a/d", 4);
}

/* ---- model-checked random put/truncate sequences ---- */

typedef struct {
    char c[64];
    size_t len, cap;
    bool overflow;
} model_t;

static uint32_t rng_next(uint32_t *s) {
    *s = *s * 1664525U + 1013904223U;
    return *s >> 8;
}

static void model_put(model_t *m, const char *p, size_t n) {
    if (n == 0 || m->overflow) return;
    if (m->cap == 0 || n > m->cap - 1 - m->len) {
        m->overflow = true;
        return;
    }
    memcpy(m->c + m->len, p, n);
    m->len += n;
}

static void model_truncate(model_t *m, size_t len) {
    if (m->cap > 0 && len < m->len) m->len = len;
}

static void compare(ctest_t *t, const core_buf_t *b, const model_t *m, const store_t *st) {
    CHECK_EQ_U64(t, core_buf_len(b), m->len);
    CHECK_EQ_U64(t, core_buf_str(b).len, m->len);
    CHECK(t, core_buf_overflow(b) == m->overflow);
    if (m->cap > 0) {
        CHECK_EQ_MEM(t, core_buf_cstr(b), m->len, m->c, m->len);
        CHECK_EQ_U64(t, (unsigned char)core_buf_cstr(b)[m->len], 0);
    }
    check_guards(t, st, m->cap);
}

static void run_sequence(ctest_t *t, uint32_t seed, size_t cap, int steps) {
    static const char alpha[] = "ab/.\0Q9";
    store_t st;
    core_buf_t b = {0};
    model_t m;
    uint32_t rng = seed;
    int i;
    size_t trunc_real = 0;
    memset(&m, 0, sizeof m);
    m.cap = cap;
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, cap));
    for (i = 0; i < steps; i++) {
        uint32_t r = rng_next(&rng);
        if (r % 3 == 0) {
            char tmp[8];
            size_t n = (r >> 4) % 6, k;
            for (k = 0; k < n; k++) tmp[k] = alpha[rng_next(&rng) % 7];
            core_buf_put(&b, tmp, n);
            model_put(&m, tmp, n);
        } else if (r % 3 == 1) {
            size_t len = (r >> 4) % (m.len + 3);   /* below, at and above current len */
            size_t before = m.len;
            core_buf_truncate(&b, len);
            model_truncate(&m, len);
            if (m.len < before) trunc_real++;
        } else {
            char c = alpha[(r >> 4) % 7];
            core_buf_put_char(&b, c);
            model_put(&m, &c, 1);
        }
        compare(t, &b, &m, &st);
    }
    if (cap >= 8) CHECK(t, trunc_real > 0);        /* positive control: real shrinks happened */
}

static void t_model_sequences(ctest_t *t) {
    uint32_t seed;
    for (seed = 1; seed <= 200; seed++) {
        run_sequence(t, seed, 4 + (seed % 20), 60);
    }
}

static void t_model_sequences_cap_edges(ctest_t *t) {
    uint32_t seed;
    for (seed = 1; seed <= 20; seed++) {
        run_sequence(t, seed, 0, 30);
        run_sequence(t, seed, 1, 30);
        run_sequence(t, seed, 2, 30);
        run_sequence(t, seed, 64, 80);
    }
}

static void t_model_sequences_reach_full_and_overflow(ctest_t *t) {
    /* positive control for the model: the generator reaches both a full builder and overflow */
    uint32_t seed;
    bool saw_overflow = false;
    for (seed = 1; seed <= 50; seed++) {
        store_t st;
        core_buf_t b = {0};
        uint32_t rng = seed;
        int i;
        store_fill(&st);
        CHECK(t, core_buf_init(&b, st.s, 6));
        for (i = 0; i < 40; i++) {
            char c[2] = {'k', 'k'};
            core_buf_put(&b, c, (rng_next(&rng) % 2) + 1);
            if (core_buf_overflow(&b)) saw_overflow = true;
        }
    }
    CHECK(t, saw_overflow);
}

CTEST_SUITE("buf_truncate",
    {"shrinks_to_exact_prefix", t_shrinks_to_exact_prefix},
    {"every_length_from_full_down_to_zero", t_every_length_from_full_down_to_zero},
    {"truncate_by_one", t_truncate_by_one},
    {"zero_empties", t_zero_empties},
    {"equal_length_is_noop", t_equal_length_is_noop},
    {"longer_than_content_is_noop", t_longer_than_content_is_noop},
    {"truncate_empty_builder", t_truncate_empty_builder},
    {"idempotent", t_idempotent},
    {"cstr_terminator_after_shrink", t_cstr_terminator_after_shrink},
    {"binary_content_with_nul", t_binary_content_with_nul},
    {"overflow_not_set_by_truncate", t_overflow_not_set_by_truncate},
    {"overflow_stays_set", t_overflow_stays_set},
    {"appends_ignored_after_overflowed_truncate", t_appends_ignored_after_overflowed_truncate},
    {"append_after_truncate", t_append_after_truncate},
    {"truncate_frees_room_for_append", t_truncate_frees_room_for_append},
    {"capacity_boundary", t_capacity_boundary},
    {"capacity_one", t_capacity_one},
    {"capacity_zero", t_capacity_zero},
    {"capacity_zero_null_storage", t_capacity_zero_null_storage},
    {"null_builder", t_null_builder},
    {"truncate_only_affects_given_builder", t_truncate_only_affects_given_builder},
    {"dot_segment_cursor_usage", t_dot_segment_cursor_usage},
    {"model_sequences", t_model_sequences},
    {"model_sequences_cap_edges", t_model_sequences_cap_edges},
    {"model_sequences_reach_full_and_overflow", t_model_sequences_reach_full_and_overflow})
