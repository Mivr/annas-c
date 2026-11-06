/* Tests for core/arena.h: bump allocator over a caller buffer. Public header only. */
#include <stdint.h>
#include <string.h>

#include "core/arena.h"
#include "ctest/ctest.h"

#define BUFSZ 256u

/* A 64-aligned buffer so that offsets and addresses are predictable. */
typedef struct {
    _Alignas(64) unsigned char b[BUFSZ];
} abuf_t;

static uintptr_t addr(const void *p) { return (uintptr_t)p; }

static void t_init_ok(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    CHECK(t, core_arena_init(&a, ab.b, 100));
    CHECK_EQ_U64(t, core_arena_used(&a), 0);
    CHECK_EQ_U64(t, core_arena_capacity(&a), 100);
    CHECK_EQ_U64(t, core_arena_mark(&a), 0);
    CHECK_EQ_U64(t, CORE_ARENA_MAX_ALIGN, 64);
}

static void t_init_resets_previous_state(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    CHECK(t, core_arena_init(&a, ab.b, 100));
    CHECK(t, core_arena_alloc(&a, 40, 1) != NULL);
    CHECK_EQ_U64(t, core_arena_used(&a), 40);
    CHECK(t, core_arena_init(&a, ab.b, 50));
    CHECK_EQ_U64(t, core_arena_used(&a), 0);
    CHECK_EQ_U64(t, core_arena_capacity(&a), 50);
    CHECK(t, core_arena_alloc(&a, 50, 1) == ab.b);
}

static void t_init_errors(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    CHECK(t, !core_arena_init(NULL, ab.b, 10));
    CHECK(t, core_arena_init(&a, ab.b, 16));
    CHECK(t, core_arena_alloc(&a, 4, 1) != NULL);
    /* NULL buffer with a non-zero capacity is rejected and leaves the arena unchanged. */
    CHECK(t, !core_arena_init(&a, NULL, 1));
    CHECK(t, !core_arena_init(&a, NULL, SIZE_MAX));
    CHECK_EQ_U64(t, core_arena_capacity(&a), 16);
    CHECK_EQ_U64(t, core_arena_used(&a), 4);
}

static void t_init_null_buffer_zero_cap(ctest_t *t) {
    core_arena_t a = {0};
    CHECK(t, core_arena_init(&a, NULL, 0));
    CHECK_EQ_U64(t, core_arena_capacity(&a), 0);
    CHECK(t, core_arena_alloc(&a, 1, 1) == NULL);
    CHECK_EQ_U64(t, core_arena_used(&a), 0);
}

static void t_zero_initialised_arena_rejects(ctest_t *t) {
    core_arena_t a = {0};
    CHECK(t, core_arena_alloc(&a, 1, 1) == NULL);
    CHECK_EQ_U64(t, core_arena_used(&a), 0);
    CHECK_EQ_U64(t, core_arena_capacity(&a), 0);
    CHECK(t, core_arena_reset_to(&a, 0));
    CHECK(t, !core_arena_reset_to(&a, 1));
}

static void t_null_arena(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    /* Control: a real arena works, so "always NULL" cannot pass this test. */
    CHECK(t, core_arena_init(&a, ab.b, 8));
    CHECK(t, core_arena_alloc(&a, 8, 1) == ab.b);
    CHECK(t, core_arena_alloc(NULL, 1, 1) == NULL);
    CHECK_EQ_U64(t, core_arena_used(NULL), 0);
    CHECK_EQ_U64(t, core_arena_capacity(NULL), 0);
    CHECK_EQ_U64(t, core_arena_mark(NULL), 0);
    CHECK(t, !core_arena_reset_to(NULL, 0));
}

static void t_alloc_sequential_exact_addresses(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    CHECK(t, core_arena_init(&a, ab.b, 64));
    CHECK(t, core_arena_alloc(&a, 10, 1) == ab.b);
    CHECK_EQ_U64(t, core_arena_used(&a), 10);
    CHECK(t, core_arena_alloc(&a, 5, 1) == ab.b + 10);
    CHECK_EQ_U64(t, core_arena_used(&a), 15);
    CHECK(t, core_arena_alloc(&a, 1, 1) == ab.b + 15);
    CHECK_EQ_U64(t, core_arena_used(&a), 16);
    CHECK_EQ_U64(t, core_arena_mark(&a), 16);
    CHECK_EQ_U64(t, core_arena_capacity(&a), 64);
}

static void t_blocks_do_not_overlap(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    unsigned char *p1;
    unsigned char *p2;
    unsigned char *p3;
    unsigned char want1[7];
    unsigned char want3[9];
    CHECK(t, core_arena_init(&a, ab.b, 64));
    p1 = core_arena_alloc(&a, 7, 1);
    p2 = core_arena_alloc(&a, 8, 1);
    p3 = core_arena_alloc(&a, 9, 1);
    CHECK(t, p1 != NULL && p2 != NULL && p3 != NULL);
    if (p1 == NULL || p2 == NULL || p3 == NULL) return;
    memset(p1, 0xA1, 7);
    memset(p2, 0xB2, 8);
    memset(p3, 0xC3, 9);
    memset(want1, 0xA1, sizeof want1);
    memset(want3, 0xC3, sizeof want3);
    CHECK_EQ_MEM(t, p1, 7, want1, sizeof want1);
    CHECK_EQ_MEM(t, p3, 9, want3, sizeof want3);
    CHECK_EQ_U64(t, p2[0], 0xB2);
    CHECK_EQ_U64(t, p2[7], 0xB2);
}

static void t_alignment_of_returned_addresses(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    size_t al;
    /* Base is 64-aligned + 1, so every alignment needs padding after the first byte. */
    CHECK(t, core_arena_init(&a, ab.b + 1, 200));
    for (al = 1; al <= 64; al *= 2) {
        void *p = core_arena_alloc(&a, 1, al);
        CHECK(t, p != NULL);
        if (p != NULL) {
            CHECK_EQ_U64(t, addr(p) % al, 0);
        }
    }
}

static void t_alignment_padding_exact(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    /* base = ab.b + 1. */
    CHECK(t, core_arena_init(&a, ab.b + 1, 200));
    CHECK(t, core_arena_alloc(&a, 1, 1) == ab.b + 1);
    CHECK_EQ_U64(t, core_arena_used(&a), 1);
    CHECK(t, core_arena_alloc(&a, 1, 2) == ab.b + 2);
    CHECK_EQ_U64(t, core_arena_used(&a), 2);
    CHECK(t, core_arena_alloc(&a, 1, 4) == ab.b + 4);
    CHECK_EQ_U64(t, core_arena_used(&a), 4);
    CHECK(t, core_arena_alloc(&a, 1, 8) == ab.b + 8);
    CHECK_EQ_U64(t, core_arena_used(&a), 8);
    CHECK(t, core_arena_alloc(&a, 1, 16) == ab.b + 16);
    CHECK_EQ_U64(t, core_arena_used(&a), 16);
    CHECK(t, core_arena_alloc(&a, 1, 32) == ab.b + 32);
    CHECK_EQ_U64(t, core_arena_used(&a), 32);
    CHECK(t, core_arena_alloc(&a, 1, 64) == ab.b + 64);
    CHECK_EQ_U64(t, core_arena_used(&a), 64);
    /* Current end is ab.b + 65: align 1 adds no padding. */
    CHECK(t, core_arena_alloc(&a, 3, 1) == ab.b + 65);
    CHECK_EQ_U64(t, core_arena_used(&a), 67);
}

static void t_alignment_is_of_address_not_offset(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    /* Base is 3 bytes past a 64-aligned address: offset 0 is NOT 8-aligned, offset 5 is. */
    CHECK(t, core_arena_init(&a, ab.b + 3, 100));
    CHECK(t, core_arena_alloc(&a, 1, 8) == ab.b + 8);
    CHECK_EQ_U64(t, core_arena_used(&a), 6);
}

static void t_alignment_no_padding_when_already_aligned(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    CHECK(t, core_arena_init(&a, ab.b, 100));
    CHECK(t, core_arena_alloc(&a, 8, 8) == ab.b);
    CHECK(t, core_arena_alloc(&a, 8, 8) == ab.b + 8);
    CHECK_EQ_U64(t, core_arena_used(&a), 16);
    CHECK(t, core_arena_alloc(&a, 3, 1) == ab.b + 16);
    CHECK(t, core_arena_alloc(&a, 1, 4) == ab.b + 20);
    CHECK_EQ_U64(t, core_arena_used(&a), 21);
}

static void t_alignment_invalid_values(ctest_t *t) {
    static const size_t bad[] = {0, 3, 5, 6, 7, 9, 12, 24, 63, 65, 96, 100, 127, 128, 256,
                                 (size_t)1 << 63, SIZE_MAX};
    abuf_t ab;
    core_arena_t a = {0};
    size_t i;
    CHECK(t, core_arena_init(&a, ab.b, 200));
    CHECK(t, core_arena_alloc(&a, 5, 1) != NULL);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(t, core_arena_alloc(&a, 1, bad[i]) == NULL);
        CHECK_EQ_U64(t, core_arena_used(&a), 5);
    }
    /* 64 is the largest valid alignment, 2 the smallest power of two above 1. */
    CHECK(t, core_arena_alloc(&a, 1, 64) == ab.b + 64);
    CHECK(t, core_arena_alloc(&a, 1, 2) == ab.b + 66);
}

static void t_size_zero_is_rejected_without_effect(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    CHECK(t, core_arena_init(&a, ab.b + 1, 100));
    CHECK(t, core_arena_alloc(&a, 3, 1) != NULL);
    CHECK(t, core_arena_alloc(&a, 0, 1) == NULL);
    CHECK(t, core_arena_alloc(&a, 0, 64) == NULL);
    CHECK_EQ_U64(t, core_arena_used(&a), 3);
}

static void t_exhaustion_boundaries(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    /* cap + 1 fails, cap succeeds exactly, then nothing fits. */
    CHECK(t, core_arena_init(&a, ab.b, 16));
    CHECK(t, core_arena_alloc(&a, 17, 1) == NULL);
    CHECK_EQ_U64(t, core_arena_used(&a), 0);
    CHECK(t, core_arena_alloc(&a, 16, 1) == ab.b);
    CHECK_EQ_U64(t, core_arena_used(&a), 16);
    CHECK(t, core_arena_alloc(&a, 1, 1) == NULL);
    CHECK_EQ_U64(t, core_arena_used(&a), 16);
    /* cap - 1 then 2 fails, then 1 succeeds. */
    CHECK(t, core_arena_init(&a, ab.b, 16));
    CHECK(t, core_arena_alloc(&a, 15, 1) == ab.b);
    CHECK(t, core_arena_alloc(&a, 2, 1) == NULL);
    CHECK_EQ_U64(t, core_arena_used(&a), 15);
    CHECK(t, core_arena_alloc(&a, 1, 1) == ab.b + 15);
    CHECK_EQ_U64(t, core_arena_used(&a), 16);
}

static void t_capacity_one_and_filled_one_by_one(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    size_t i;
    CHECK(t, core_arena_init(&a, ab.b, 1));
    CHECK(t, core_arena_alloc(&a, 2, 1) == NULL);
    CHECK(t, core_arena_alloc(&a, 1, 1) == ab.b);
    CHECK(t, core_arena_alloc(&a, 1, 1) == NULL);
    CHECK(t, core_arena_init(&a, ab.b, 64));
    for (i = 0; i < 64; i++) {
        CHECK(t, core_arena_alloc(&a, 1, 1) == ab.b + i);
    }
    CHECK_EQ_U64(t, core_arena_used(&a), 64);
    CHECK(t, core_arena_alloc(&a, 1, 1) == NULL);
}

static void t_size_overflow_is_detected(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    CHECK(t, core_arena_init(&a, ab.b, 100));
    CHECK(t, core_arena_alloc(&a, 8, 1) != NULL);
    CHECK(t, core_arena_alloc(&a, SIZE_MAX, 1) == NULL);
    CHECK(t, core_arena_alloc(&a, SIZE_MAX - 3, 1) == NULL);  /* 8 + size wraps to 4 */
    CHECK(t, core_arena_alloc(&a, SIZE_MAX - 7, 1) == NULL);  /* 8 + size wraps to 0 */
    CHECK(t, core_arena_alloc(&a, SIZE_MAX - 8, 1) == NULL);  /* 8 + size == SIZE_MAX */
    CHECK(t, core_arena_alloc(&a, (size_t)1 << 63, 1) == NULL);
    CHECK_EQ_U64(t, core_arena_used(&a), 8);
    /* Still usable afterwards at the same place. */
    CHECK(t, core_arena_alloc(&a, 4, 1) == ab.b + 8);
}

static void t_padding_plus_size_overflow_is_detected(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    CHECK(t, core_arena_init(&a, ab.b + 1, 200));
    /* Block at ab.b + 1, used 1. The next 64-aligned address is ab.b + 64, i.e. offset 63. */
    CHECK(t, core_arena_alloc(&a, 1, 1) != NULL);
    CHECK(t, core_arena_alloc(&a, SIZE_MAX - 10, 64) == NULL);  /* 63 + size wraps */
    CHECK(t, core_arena_alloc(&a, SIZE_MAX - 62, 64) == NULL);  /* 63 + size wraps to 0 */
    CHECK(t, core_arena_alloc(&a, SIZE_MAX - 63, 64) == NULL);  /* 63 + size == SIZE_MAX */
    CHECK_EQ_U64(t, core_arena_used(&a), 1);
}

static void t_padding_counts_against_capacity(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    /* base = ab.b + 1, cap 8: an 8-aligned block starts at offset 7. */
    CHECK(t, core_arena_init(&a, ab.b + 1, 8));
    CHECK(t, core_arena_alloc(&a, 1, 1) == ab.b + 1);
    CHECK(t, core_arena_alloc(&a, 2, 8) == NULL);          /* 7 + 2 = 9 > 8 */
    CHECK_EQ_U64(t, core_arena_used(&a), 1);
    CHECK(t, core_arena_alloc(&a, 1, 64) == NULL);         /* padding alone exceeds cap */
    CHECK_EQ_U64(t, core_arena_used(&a), 1);
    CHECK(t, core_arena_alloc(&a, 1, 8) == ab.b + 8);      /* 7 + 1 = 8 fits exactly */
    CHECK_EQ_U64(t, core_arena_used(&a), 8);
    CHECK(t, core_arena_alloc(&a, 1, 1) == NULL);
}

static void t_failed_alloc_leaves_state_untouched(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    CHECK(t, core_arena_init(&a, ab.b, 20));
    CHECK(t, core_arena_alloc(&a, 10, 1) == ab.b);
    CHECK(t, core_arena_alloc(&a, 11, 1) == NULL);
    CHECK(t, core_arena_alloc(&a, 5, 3) == NULL);
    CHECK_EQ_U64(t, core_arena_used(&a), 10);
    CHECK_EQ_U64(t, core_arena_mark(&a), 10);
    CHECK_EQ_U64(t, core_arena_capacity(&a), 20);
    CHECK(t, core_arena_alloc(&a, 10, 1) == ab.b + 10);
}

static void t_mark_and_reset(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    size_t m;
    CHECK(t, core_arena_init(&a, ab.b, 100));
    CHECK(t, core_arena_alloc(&a, 10, 1) == ab.b);
    m = core_arena_mark(&a);
    CHECK_EQ_U64(t, m, 10);
    CHECK(t, core_arena_alloc(&a, 20, 1) == ab.b + 10);
    CHECK_EQ_U64(t, core_arena_used(&a), 30);
    CHECK(t, core_arena_reset_to(&a, m));
    CHECK_EQ_U64(t, core_arena_used(&a), 10);
    CHECK_EQ_U64(t, core_arena_mark(&a), 10);
    CHECK(t, core_arena_alloc(&a, 20, 1) == ab.b + 10);   /* the space is reused */
}

static void t_reset_to_zero_reclaims_everything(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    CHECK(t, core_arena_init(&a, ab.b, 32));
    CHECK(t, core_arena_alloc(&a, 32, 1) == ab.b);
    CHECK(t, core_arena_alloc(&a, 1, 1) == NULL);
    CHECK(t, core_arena_reset_to(&a, 0));
    CHECK_EQ_U64(t, core_arena_used(&a), 0);
    CHECK(t, core_arena_alloc(&a, 32, 1) == ab.b);
    CHECK_EQ_U64(t, core_arena_capacity(&a), 32);
}

static void t_reset_boundaries(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    CHECK(t, core_arena_init(&a, ab.b, 100));
    CHECK(t, core_arena_alloc(&a, 10, 1) != NULL);
    CHECK(t, core_arena_reset_to(&a, 10));            /* current mark: no-op, success */
    CHECK_EQ_U64(t, core_arena_used(&a), 10);
    CHECK(t, !core_arena_reset_to(&a, 11));           /* above used */
    CHECK_EQ_U64(t, core_arena_used(&a), 10);
    CHECK(t, !core_arena_reset_to(&a, 100));          /* capacity, but above used */
    CHECK(t, !core_arena_reset_to(&a, SIZE_MAX));
    CHECK_EQ_U64(t, core_arena_used(&a), 10);
    CHECK(t, core_arena_reset_to(&a, 9));
    CHECK_EQ_U64(t, core_arena_used(&a), 9);
    CHECK(t, core_arena_alloc(&a, 1, 1) == ab.b + 9);
}

static void t_mark_includes_padding_and_nested_marks(ctest_t *t) {
    abuf_t ab;
    core_arena_t a = {0};
    size_t m1;
    size_t m2;
    CHECK(t, core_arena_init(&a, ab.b + 1, 200));
    CHECK(t, core_arena_alloc(&a, 1, 16) == ab.b + 16);   /* padding 15 + 1 */
    m1 = core_arena_mark(&a);
    CHECK_EQ_U64(t, m1, 16);
    CHECK(t, core_arena_alloc(&a, 4, 1) == ab.b + 17);
    m2 = core_arena_mark(&a);
    CHECK_EQ_U64(t, m2, 20);
    CHECK(t, core_arena_alloc(&a, 4, 1) != NULL);
    CHECK(t, core_arena_reset_to(&a, m2));
    CHECK(t, core_arena_reset_to(&a, m1));
    CHECK_EQ_U64(t, core_arena_used(&a), 16);
    CHECK(t, !core_arena_reset_to(&a, m2));               /* m2 is above used now */
    CHECK_EQ_U64(t, core_arena_used(&a), 16);
}

static void t_writes_stay_inside_buffer(ctest_t *t) {
    unsigned char raw[64 + 128];
    unsigned char *buf = raw + 64;
    core_arena_t a = {0};
    unsigned char *p;
    size_t i;
    memset(raw, 0x5A, sizeof raw);
    CHECK(t, core_arena_init(&a, buf, 40));
    p = core_arena_alloc(&a, 40, 1);
    CHECK(t, p == buf);
    if (p == NULL) return;
    memset(p, 0xFF, 40);
    for (i = 0; i < 64; i++) {
        CHECK_EQ_U64(t, raw[i], 0x5A);
    }
    for (i = 104; i < sizeof raw; i++) {
        CHECK_EQ_U64(t, raw[i], 0x5A);
    }
}

CTEST_SUITE("arena",
    {"init_ok", t_init_ok},
    {"init_resets_previous_state", t_init_resets_previous_state},
    {"init_errors", t_init_errors},
    {"init_null_buffer_zero_cap", t_init_null_buffer_zero_cap},
    {"zero_initialised_arena_rejects", t_zero_initialised_arena_rejects},
    {"null_arena", t_null_arena},
    {"alloc_sequential_exact_addresses", t_alloc_sequential_exact_addresses},
    {"blocks_do_not_overlap", t_blocks_do_not_overlap},
    {"alignment_of_returned_addresses", t_alignment_of_returned_addresses},
    {"alignment_padding_exact", t_alignment_padding_exact},
    {"alignment_is_of_address_not_offset", t_alignment_is_of_address_not_offset},
    {"alignment_no_padding_when_already_aligned", t_alignment_no_padding_when_already_aligned},
    {"alignment_invalid_values", t_alignment_invalid_values},
    {"size_zero_is_rejected_without_effect", t_size_zero_is_rejected_without_effect},
    {"exhaustion_boundaries", t_exhaustion_boundaries},
    {"capacity_one_and_filled_one_by_one", t_capacity_one_and_filled_one_by_one},
    {"size_overflow_is_detected", t_size_overflow_is_detected},
    {"padding_plus_size_overflow_is_detected", t_padding_plus_size_overflow_is_detected},
    {"padding_counts_against_capacity", t_padding_counts_against_capacity},
    {"failed_alloc_leaves_state_untouched", t_failed_alloc_leaves_state_untouched},
    {"mark_and_reset", t_mark_and_reset},
    {"reset_to_zero_reclaims_everything", t_reset_to_zero_reclaims_everything},
    {"reset_boundaries", t_reset_boundaries},
    {"mark_includes_padding_and_nested_marks", t_mark_includes_padding_and_nested_marks},
    {"writes_stay_inside_buffer", t_writes_stay_inside_buffer})
