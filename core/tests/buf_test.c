/* Tests for core/buf.h: bounded byte builder. Public header only. */
#include <stdint.h>
#include <string.h>

#include "core/buf.h"
#include "ctest/ctest.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define SENT 'Z' /* sentinel byte used to detect stray writes */

typedef struct {
    char guard_lo[8];
    char s[64];
    char guard_hi[8];
} store_t;

static void store_fill(store_t *st) { memset(st, SENT, sizeof *st); }

/* The sentinels outside s[0..cap) and the guards must be intact. */
static void check_untouched_from(ctest_t *t, const store_t *st, size_t from) {
    size_t i;
    for (i = 0; i < sizeof st->guard_lo; i++) {
        CHECK_EQ_U64(t, (unsigned char)st->guard_lo[i], SENT);
    }
    for (i = from; i < sizeof st->s; i++) {
        CHECK_EQ_U64(t, (unsigned char)st->s[i], SENT);
    }
    for (i = 0; i < sizeof st->guard_hi; i++) {
        CHECK_EQ_U64(t, (unsigned char)st->guard_hi[i], SENT);
    }
}

/* Content is exactly want[0..wlen), NUL-terminated, length and view consistent. */
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
    if (wlen > 0) {
        CHECK(t, v.ptr != NULL);
        if (v.ptr != NULL) CHECK_EQ_MEM(t, v.ptr, v.len, want, wlen);
    }
}

static void t_init_ok(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 8));
    CHECK_EQ_U64(t, core_buf_len(&b), 0);
    CHECK(t, !core_buf_overflow(&b));
    CHECK_EQ_U64(t, (unsigned char)st.s[0], 0);       /* NUL written */
    CHECK_EQ_U64(t, (unsigned char)st.s[1], SENT);    /* nothing else touched */
    CHECK_EQ_STR(t, core_buf_cstr(&b), "");
    CHECK(t, core_buf_cstr(&b) == st.s);
    check_untouched_from(t, &st, 1);
}

static void t_init_clears_previous_state(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 4));
    core_buf_put(&b, "abcdefgh", 8);
    CHECK(t, core_buf_overflow(&b));
    CHECK(t, core_buf_init(&b, st.s, 6));
    CHECK(t, !core_buf_overflow(&b));
    CHECK_EQ_U64(t, core_buf_len(&b), 0);
    core_buf_put(&b, "12345", 5);   /* cap 6 now allows 5 bytes */
    CHECK(t, !core_buf_overflow(&b));
    check_content(t, &b, "12345", 5);
}

static void t_init_errors(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, !core_buf_init(NULL, st.s, 8));
    CHECK(t, core_buf_init(&b, st.s, 8));
    core_buf_put(&b, "abc", 3);
    CHECK(t, !core_buf_init(&b, NULL, 8));    /* rejected: b unchanged */
    CHECK(t, !core_buf_init(&b, NULL, 1));
    check_content(t, &b, "abc", 3);
    CHECK(t, !core_buf_overflow(&b));
}

static void t_append_simple(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 16));
    core_buf_put(&b, "abc", 3);
    check_content(t, &b, "abc", 3);
    core_buf_put(&b, "de", 2);
    check_content(t, &b, "abcde", 5);
    core_buf_put_str(&b, LIT("fgh"));
    check_content(t, &b, "abcdefgh", 8);
    core_buf_put_char(&b, 'i');
    check_content(t, &b, "abcdefghi", 9);
    core_buf_put_u64(&b, 42);
    check_content(t, &b, "abcdefghi42", 11);
    CHECK(t, !core_buf_overflow(&b));
    check_untouched_from(t, &st, 12);
}

static void t_view_and_cstr_point_into_storage(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 16));
    core_buf_put_str(&b, LIT("xy"));
    CHECK(t, core_buf_str(&b).ptr == st.s);
    CHECK(t, core_buf_cstr(&b) == st.s);
    CHECK_EQ_STR(t, core_buf_cstr(&b), "xy");
}

static void t_fill_to_exact_capacity_minus_one(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 8));
    core_buf_put(&b, "abcdefg", 7);     /* cap - 1 content bytes: fits */
    CHECK(t, !core_buf_overflow(&b));
    check_content(t, &b, "abcdefg", 7);
    CHECK_EQ_U64(t, (unsigned char)st.s[7], 0);
    check_untouched_from(t, &st, 8);
}

static void t_one_byte_past_the_limit_overflows(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 8));
    core_buf_put(&b, "abcdefg", 7);
    core_buf_put_char(&b, 'h');
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "abcdefg", 7);   /* unchanged, still terminated */
    check_untouched_from(t, &st, 8);
}

static void t_append_of_cap_bytes_overflows_without_writing(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 8));
    core_buf_put(&b, "abcdefgh", 8);      /* cap bytes: no room for the NUL */
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "", 0);
    check_untouched_from(t, &st, 1);
    CHECK(t, core_buf_init(&b, st.s, 8));
    core_buf_put(&b, "abcdefghijkl", 12); /* way over */
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "", 0);
    check_untouched_from(t, &st, 1);
}

static void t_overflow_is_all_or_nothing(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 8));
    core_buf_put(&b, "ab", 2);
    core_buf_put(&b, "cdefgh", 6);    /* 2 + 6 = 8 > 7: nothing of it is written */
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "ab", 2);
    check_untouched_from(t, &st, 3);
}

static void t_overflow_is_sticky(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 8));
    core_buf_put(&b, "abc", 3);
    core_buf_put(&b, "0123456789", 10);
    CHECK(t, core_buf_overflow(&b));
    /* Later appends that would fit are ignored. */
    core_buf_put_char(&b, 'x');
    core_buf_put(&b, "yz", 2);
    core_buf_put_str(&b, LIT("w"));
    core_buf_put_u64(&b, 7);
    core_buf_put(&b, NULL, 0);
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "abc", 3);
    check_untouched_from(t, &st, 4);
}

static void t_zero_length_appends(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 4));
    core_buf_put(&b, NULL, 0);
    core_buf_put(&b, "abc", 0);
    core_buf_put_str(&b, (core_str_t){NULL, 0});
    core_buf_put_str(&b, LIT(""));
    CHECK(t, !core_buf_overflow(&b));
    check_content(t, &b, "", 0);
    core_buf_put(&b, "abc", 3);                 /* now exactly full (cap 4) */
    core_buf_put(&b, NULL, 0);                  /* zero-length on a full buffer is fine */
    core_buf_put_str(&b, LIT(""));
    CHECK(t, !core_buf_overflow(&b));
    check_content(t, &b, "abc", 3);
}

static void t_null_pointer_with_length_is_an_error(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 16));
    core_buf_put(&b, "ab", 2);
    core_buf_put(&b, NULL, 3);
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "ab", 2);
    check_untouched_from(t, &st, 3);
}

static void t_binary_content_with_nul_bytes(ctest_t *t) {
    static const char raw[] = {'a', '\0', 'b', '\xff', '\x80'};
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 16));
    core_buf_put(&b, raw, sizeof raw);
    CHECK_EQ_U64(t, core_buf_len(&b), 5);
    CHECK_EQ_MEM(t, st.s, 5, raw, sizeof raw);
    CHECK_EQ_U64(t, (unsigned char)st.s[5], 0);
    core_buf_put_char(&b, '\0');
    CHECK_EQ_U64(t, core_buf_len(&b), 6);
    core_buf_put_char(&b, 'q');
    CHECK_EQ_U64(t, core_buf_len(&b), 7);
    CHECK_EQ_U64(t, (unsigned char)st.s[5], 0);
    CHECK_EQ_U64(t, (unsigned char)st.s[6], 'q');
    CHECK_EQ_U64(t, (unsigned char)st.s[7], 0);
    CHECK_EQ_U64(t, core_buf_str(&b).len, 7);
}

static void t_put_char_values(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 8));
    core_buf_put_char(&b, 'a');
    core_buf_put_char(&b, (char)0xFF);
    core_buf_put_char(&b, (char)0x80);
    core_buf_put_char(&b, '\n');
    CHECK_EQ_U64(t, core_buf_len(&b), 4);
    CHECK_EQ_U64(t, (unsigned char)st.s[0], 'a');
    CHECK_EQ_U64(t, (unsigned char)st.s[1], 0xFF);
    CHECK_EQ_U64(t, (unsigned char)st.s[2], 0x80);
    CHECK_EQ_U64(t, (unsigned char)st.s[3], '\n');
    CHECK_EQ_U64(t, (unsigned char)st.s[4], 0);
}

static void t_put_str_copies_exactly_len_bytes(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 16));
    core_buf_put_str(&b, (core_str_t){"abcdef", 3});
    check_content(t, &b, "abc", 3);
    core_buf_put_str(&b, (core_str_t){"x\0y", 3});
    CHECK_EQ_U64(t, core_buf_len(&b), 6);
    CHECK_EQ_MEM(t, st.s, 6, "abcx\0y", 6);
}

static void u64_case(ctest_t *t, uint64_t v, const char *want) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 32));
    core_buf_put_u64(&b, v);
    CHECK(t, !core_buf_overflow(&b));
    CHECK_EQ_STR(t, core_buf_cstr(&b), want);
    CHECK_EQ_U64(t, core_buf_len(&b), strlen(want));
}

static void t_put_u64_values(ctest_t *t) {
    u64_case(t, 0, "0");
    u64_case(t, 1, "1");
    u64_case(t, 9, "9");
    u64_case(t, 10, "10");
    u64_case(t, 99, "99");
    u64_case(t, 100, "100");
    u64_case(t, 101, "101");
    u64_case(t, 1000, "1000");
    u64_case(t, 1234567890, "1234567890");
    u64_case(t, 4294967295u, "4294967295");
    u64_case(t, 4294967296u, "4294967296");
    u64_case(t, 1000000000000000000u, "1000000000000000000");
    u64_case(t, 9223372036854775807u, "9223372036854775807");
    u64_case(t, 9223372036854775808u, "9223372036854775808");
    u64_case(t, 10000000000000000000u, "10000000000000000000");
    u64_case(t, 18446744073709551614u, "18446744073709551614");
    u64_case(t, UINT64_MAX, "18446744073709551615");
}

static void t_put_u64_appends_after_existing_text(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 32));
    core_buf_put_str(&b, LIT("Book "));
    core_buf_put_u64(&b, 12);
    core_buf_put_char(&b, ':');
    core_buf_put_u64(&b, 0);
    check_content(t, &b, "Book 12:0", 9);
}

static void t_put_u64_capacity_boundaries(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    /* 20 digits need cap 21. */
    CHECK(t, core_buf_init(&b, st.s, 21));
    core_buf_put_u64(&b, UINT64_MAX);
    CHECK(t, !core_buf_overflow(&b));
    check_content(t, &b, "18446744073709551615", 20);
    CHECK(t, core_buf_init(&b, st.s, 20));
    core_buf_put_u64(&b, UINT64_MAX);
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "", 0);
    /* "ab" + "123" needs cap 6. */
    CHECK(t, core_buf_init(&b, st.s, 6));
    core_buf_put(&b, "ab", 2);
    core_buf_put_u64(&b, 123);
    CHECK(t, !core_buf_overflow(&b));
    check_content(t, &b, "ab123", 5);
    CHECK(t, core_buf_init(&b, st.s, 5));
    core_buf_put(&b, "ab", 2);
    core_buf_put_u64(&b, 123);
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "ab", 2);
    /* A single digit into cap 1 (no room) and cap 2 (room). */
    CHECK(t, core_buf_init(&b, st.s, 1));
    core_buf_put_u64(&b, 0);
    CHECK(t, core_buf_overflow(&b));
    CHECK(t, core_buf_init(&b, st.s, 2));
    core_buf_put_u64(&b, 7);
    CHECK(t, !core_buf_overflow(&b));
    check_content(t, &b, "7", 1);
}

static void t_capacity_zero(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 0));
    CHECK(t, !core_buf_overflow(&b));
    CHECK_EQ_U64(t, core_buf_len(&b), 0);
    CHECK_EQ_STR(t, core_buf_cstr(&b), "");
    CHECK(t, core_buf_str(&b).ptr == NULL);
    CHECK_EQ_U64(t, core_buf_str(&b).len, 0);
    core_buf_put(&b, NULL, 0);
    core_buf_put(&b, "x", 0);
    core_buf_put_str(&b, LIT(""));
    CHECK(t, !core_buf_overflow(&b));        /* zero-byte appends never overflow */
    core_buf_put_char(&b, 'a');
    CHECK(t, core_buf_overflow(&b));
    CHECK_EQ_U64(t, core_buf_len(&b), 0);
    core_buf_put_u64(&b, 5);
    core_buf_put(&b, "abc", 3);
    check_untouched_from(t, &st, 0);          /* nothing was ever written, not even a NUL */
}

static void t_capacity_zero_each_append_overflows(ctest_t *t) {
    core_buf_t b = {0};
    CHECK(t, core_buf_init(&b, NULL, 0));
    core_buf_put(&b, "a", 1);
    CHECK(t, core_buf_overflow(&b));
    CHECK(t, core_buf_init(&b, NULL, 0));
    core_buf_put_str(&b, LIT("a"));
    CHECK(t, core_buf_overflow(&b));
    CHECK(t, core_buf_init(&b, NULL, 0));
    core_buf_put_u64(&b, 0);
    CHECK(t, core_buf_overflow(&b));
    CHECK(t, core_buf_init(&b, NULL, 0));
    CHECK(t, !core_buf_overflow(&b));
    CHECK_EQ_STR(t, core_buf_cstr(&b), "");
}

static void t_capacity_one(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 1));
    CHECK_EQ_U64(t, (unsigned char)st.s[0], 0);
    core_buf_put(&b, NULL, 0);
    CHECK(t, !core_buf_overflow(&b));
    core_buf_put_char(&b, 'a');
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "", 0);
    check_untouched_from(t, &st, 1);
}

static void t_capacity_two_holds_one_byte(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 2));
    core_buf_put_char(&b, 'a');
    CHECK(t, !core_buf_overflow(&b));
    check_content(t, &b, "a", 1);
    core_buf_put_char(&b, 'b');
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "a", 1);
    check_untouched_from(t, &st, 2);
}

static void t_char_by_char_until_full(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    size_t i;
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, 5));
    for (i = 0; i < 4; i++) {
        core_buf_put_char(&b, (char)('a' + (int)i));
        CHECK(t, !core_buf_overflow(&b));
        CHECK_EQ_U64(t, core_buf_len(&b), i + 1);
    }
    check_content(t, &b, "abcd", 4);
    core_buf_put_char(&b, 'e');
    CHECK(t, core_buf_overflow(&b));
    check_content(t, &b, "abcd", 4);
    check_untouched_from(t, &st, 5);
}

static void t_null_builder_is_harmless(ctest_t *t) {
    core_buf_put(NULL, "a", 1);
    core_buf_put_str(NULL, LIT("a"));
    core_buf_put_char(NULL, 'a');
    core_buf_put_u64(NULL, 1);
    CHECK_EQ_U64(t, core_buf_len(NULL), 0);
    CHECK(t, !core_buf_overflow(NULL));
    CHECK(t, core_buf_str(NULL).ptr == NULL);
    CHECK_EQ_U64(t, core_buf_str(NULL).len, 0);
    CHECK_EQ_STR(t, core_buf_cstr(NULL), "");
}

static void t_build_a_realistic_line(ctest_t *t) {
    store_t st;
    core_buf_t b = {0};
    static const char want[] = "Book 3:\nTitle: Dune\nHash: 0a1b\n\n";
    store_fill(&st);
    CHECK(t, core_buf_init(&b, st.s, sizeof st.s));
    core_buf_put_str(&b, LIT("Book "));
    core_buf_put_u64(&b, 3);
    core_buf_put_str(&b, LIT(":\nTitle: "));
    core_buf_put_str(&b, core_str_from_cstr("Dune"));
    core_buf_put_str(&b, LIT("\nHash: 0a1b\n"));
    core_buf_put_char(&b, '\n');
    CHECK(t, !core_buf_overflow(&b));
    check_content(t, &b, want, sizeof want - 1);
}

CTEST_SUITE("buf",
    {"init_ok", t_init_ok},
    {"init_clears_previous_state", t_init_clears_previous_state},
    {"init_errors", t_init_errors},
    {"append_simple", t_append_simple},
    {"view_and_cstr_point_into_storage", t_view_and_cstr_point_into_storage},
    {"fill_to_exact_capacity_minus_one", t_fill_to_exact_capacity_minus_one},
    {"one_byte_past_the_limit_overflows", t_one_byte_past_the_limit_overflows},
    {"append_of_cap_bytes_overflows_without_writing", t_append_of_cap_bytes_overflows_without_writing},
    {"overflow_is_all_or_nothing", t_overflow_is_all_or_nothing},
    {"overflow_is_sticky", t_overflow_is_sticky},
    {"zero_length_appends", t_zero_length_appends},
    {"null_pointer_with_length_is_an_error", t_null_pointer_with_length_is_an_error},
    {"binary_content_with_nul_bytes", t_binary_content_with_nul_bytes},
    {"put_char_values", t_put_char_values},
    {"put_str_copies_exactly_len_bytes", t_put_str_copies_exactly_len_bytes},
    {"put_u64_values", t_put_u64_values},
    {"put_u64_appends_after_existing_text", t_put_u64_appends_after_existing_text},
    {"put_u64_capacity_boundaries", t_put_u64_capacity_boundaries},
    {"capacity_zero", t_capacity_zero},
    {"capacity_zero_each_append_overflows", t_capacity_zero_each_append_overflows},
    {"capacity_one", t_capacity_one},
    {"capacity_two_holds_one_byte", t_capacity_two_holds_one_byte},
    {"char_by_char_until_full", t_char_by_char_until_full},
    {"null_builder_is_harmless", t_null_builder_is_harmless},
    {"build_a_realistic_line", t_build_a_realistic_line})
