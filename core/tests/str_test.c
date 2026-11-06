/* Tests for core/str.h: byte slices. Public header only. */
#include <stdint.h>
#include <string.h>

#include "core/str.h"
#include "ctest/ctest.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define EMPTY ((core_str_t){NULL, 0})

/* got must be exactly the bytes want[0..wlen). */
static void check_bytes(ctest_t *t, core_str_t got, const char *want, size_t wlen) {
    CHECK_EQ_U64(t, got.len, wlen);
    if (got.len == wlen && wlen > 0 && got.ptr != NULL) {
        CHECK_EQ_MEM(t, got.ptr, got.len, want, wlen);
    } else if (wlen > 0) {
        CHECK(t, got.ptr != NULL && got.len == wlen);
    }
}

static void t_npos_is_size_max(ctest_t *t) {
    CHECK_EQ_U64(t, CORE_NPOS, SIZE_MAX);
    CHECK_EQ_U64(t, core_str_find(LIT("abc"), LIT("d")), SIZE_MAX);
    CHECK_EQ_U64(t, core_str_find(LIT("abc"), LIT("c")), 2);
}

static void t_from_cstr(ctest_t *t) {
    const char *h = "hello";
    core_str_t s = core_str_from_cstr(h);
    CHECK(t, s.ptr == h);
    CHECK_EQ_U64(t, s.len, 5);
    s = core_str_from_cstr("");
    CHECK_EQ_U64(t, s.len, 0);
    s = core_str_from_cstr("a");
    CHECK_EQ_U64(t, s.len, 1);
    s = core_str_from_cstr("ab\0cd");  /* stops at the first NUL */
    CHECK_EQ_U64(t, s.len, 2);
}

static void t_from_cstr_null(ctest_t *t) {
    core_str_t s = core_str_from_cstr("x");
    CHECK_EQ_U64(t, s.len, 1);
    s = core_str_from_cstr(NULL);
    CHECK(t, s.ptr == NULL);
    CHECK_EQ_U64(t, s.len, 0);
}

static void t_eq_equal_cases(ctest_t *t) {
    char copy[4] = {'a', 'b', 'c', 'd'};
    core_str_t sub = {copy, 3};
    CHECK(t, core_str_eq(LIT("abc"), LIT("abc")));
    CHECK(t, core_str_eq(LIT("a"), LIT("a")));
    CHECK(t, core_str_eq(sub, LIT("abc")));        /* only len bytes count */
    CHECK(t, core_str_eq(LIT("abc"), sub));
    CHECK(t, core_str_eq(LIT("a\0b"), LIT("a\0b")));
}

static void t_eq_empty_slices_are_equal(ctest_t *t) {
    CHECK(t, core_str_eq(EMPTY, EMPTY));
    CHECK(t, core_str_eq(LIT(""), EMPTY));
    CHECK(t, core_str_eq(EMPTY, LIT("")));
    CHECK(t, core_str_eq(LIT(""), LIT("")));
    CHECK(t, core_str_eq((core_str_t){"xyz", 0}, EMPTY));
}

static void t_eq_unequal_cases(ctest_t *t) {
    CHECK(t, core_str_eq(LIT("abc"), LIT("abc")));       /* control */
    CHECK(t, !core_str_eq(LIT("abc"), LIT("abd")));      /* last byte */
    CHECK(t, !core_str_eq(LIT("abc"), LIT("xbc")));      /* first byte */
    CHECK(t, !core_str_eq(LIT("abc"), LIT("axc")));      /* middle byte */
    CHECK(t, !core_str_eq(LIT("abc"), LIT("ab")));       /* shorter */
    CHECK(t, !core_str_eq(LIT("ab"), LIT("abc")));       /* longer */
    CHECK(t, !core_str_eq(LIT("abc"), EMPTY));
    CHECK(t, !core_str_eq(EMPTY, LIT("abc")));
    CHECK(t, !core_str_eq(LIT("a"), LIT("A")));          /* case sensitive */
    CHECK(t, !core_str_eq(LIT("a\0b"), LIT("a\0c")));    /* difference after an embedded NUL */
    CHECK(t, !core_str_eq(LIT("a\0b"), LIT("a")));
    CHECK(t, !core_str_eq(LIT("\x80"), LIT("\x81")));
    CHECK(t, !core_str_eq(LIT("\xff"), LIT("\x7f")));
}

static void t_starts_with(ctest_t *t) {
    CHECK(t, core_str_starts_with(LIT("abcdef"), LIT("abc")));
    CHECK(t, core_str_starts_with(LIT("abcdef"), LIT("a")));
    CHECK(t, core_str_starts_with(LIT("abcdef"), LIT("abcdef")));
    CHECK(t, core_str_starts_with(LIT("a\0bc"), LIT("a\0b")));
    CHECK(t, !core_str_starts_with(LIT("abcdef"), LIT("abd")));    /* last prefix byte differs */
    CHECK(t, !core_str_starts_with(LIT("abcdef"), LIT("xbc")));    /* first byte differs */
    CHECK(t, !core_str_starts_with(LIT("abcdef"), LIT("bcd")));    /* not at the start */
    CHECK(t, !core_str_starts_with(LIT("abcdef"), LIT("abcdefg")));/* longer than s */
    CHECK(t, !core_str_starts_with(LIT("abc"), LIT("ABC")));
    CHECK(t, !core_str_starts_with(LIT("a\0bc"), LIT("a\0c")));
    CHECK(t, !core_str_starts_with(EMPTY, LIT("a")));
}

static void t_starts_with_empty_prefix(ctest_t *t) {
    CHECK(t, core_str_starts_with(LIT("abc"), EMPTY));
    CHECK(t, core_str_starts_with(LIT("abc"), LIT("")));
    CHECK(t, core_str_starts_with(EMPTY, EMPTY));
    CHECK(t, core_str_starts_with(LIT(""), LIT("")));
}

static void t_starts_with_respects_slice_length(ctest_t *t) {
    /* s is "abc" followed by 'd' that is NOT part of the slice. */
    core_str_t s = {"abcd", 3};
    CHECK(t, !core_str_starts_with(s, LIT("abcd")));
    CHECK(t, core_str_starts_with(s, LIT("abc")));
    CHECK(t, core_str_starts_with(LIT("abcd"), (core_str_t){"abXX", 2}));
}

static void t_find_positions(ctest_t *t) {
    CHECK_EQ_U64(t, core_str_find(LIT("abcdef"), LIT("abc")), 0);
    CHECK_EQ_U64(t, core_str_find(LIT("abcdef"), LIT("cd")), 2);
    CHECK_EQ_U64(t, core_str_find(LIT("abcdef"), LIT("ef")), 4);   /* at the very end */
    CHECK_EQ_U64(t, core_str_find(LIT("abcdef"), LIT("f")), 5);
    CHECK_EQ_U64(t, core_str_find(LIT("abcdef"), LIT("a")), 0);
    CHECK_EQ_U64(t, core_str_find(LIT("abcdef"), LIT("abcdef")), 0);
    CHECK_EQ_U64(t, core_str_find(LIT("x"), LIT("x")), 0);
    CHECK_EQ_U64(t, core_str_find(LIT("a\0b\0c"), LIT("\0c")), 3);
}

static void t_find_first_of_several(ctest_t *t) {
    CHECK_EQ_U64(t, core_str_find(LIT("abab"), LIT("ab")), 0);
    CHECK_EQ_U64(t, core_str_find(LIT("abab"), LIT("ba")), 1);
    CHECK_EQ_U64(t, core_str_find(LIT("xaxa"), LIT("a")), 1);
    CHECK_EQ_U64(t, core_str_find(LIT("aaab"), LIT("aab")), 1);     /* overlapping candidates */
    CHECK_EQ_U64(t, core_str_find(LIT("aaaaab"), LIT("aaab")), 2);
    CHECK_EQ_U64(t, core_str_find(LIT("abcabd"), LIT("abd")), 3);   /* partial match first */
    CHECK_EQ_U64(t, core_str_find(LIT("aaa"), LIT("aa")), 0);
}

static void t_find_not_found(ctest_t *t) {
    CHECK_EQ_U64(t, core_str_find(LIT("abcdef"), LIT("x")), CORE_NPOS);
    CHECK_EQ_U64(t, core_str_find(LIT("abcdef"), LIT("ac")), CORE_NPOS);
    CHECK_EQ_U64(t, core_str_find(LIT("abcdef"), LIT("ABC")), CORE_NPOS);   /* case sensitive */
    CHECK_EQ_U64(t, core_str_find(LIT("abcdef"), LIT("abcdefg")), CORE_NPOS);
    CHECK_EQ_U64(t, core_str_find(LIT("ab"), LIT("abc")), CORE_NPOS);       /* partial at end */
    CHECK_EQ_U64(t, core_str_find(LIT("abc"), LIT("cd")), CORE_NPOS);
    CHECK_EQ_U64(t, core_str_find(EMPTY, LIT("a")), CORE_NPOS);
    CHECK_EQ_U64(t, core_str_find(LIT("a\0b"), LIT("\0c")), CORE_NPOS);
}

static void t_find_empty_needle(ctest_t *t) {
    CHECK_EQ_U64(t, core_str_find(LIT("abc"), LIT("c")), 2);   /* control */
    CHECK_EQ_U64(t, core_str_find(LIT("abc"), EMPTY), 0);
    CHECK_EQ_U64(t, core_str_find(LIT("abc"), LIT("")), 0);
    CHECK_EQ_U64(t, core_str_find(EMPTY, EMPTY), 0);
    CHECK_EQ_U64(t, core_str_find(LIT(""), LIT("")), 0);
}

static void t_find_respects_slice_length(ctest_t *t) {
    core_str_t hay = {"abcX", 3};   /* 'X' is outside the slice */
    CHECK_EQ_U64(t, core_str_find(hay, LIT("cX")), CORE_NPOS);
    CHECK_EQ_U64(t, core_str_find(hay, LIT("X")), CORE_NPOS);
    CHECK_EQ_U64(t, core_str_find(hay, LIT("c")), 2);
    CHECK_EQ_U64(t, core_str_find(LIT("abcX"), (core_str_t){"cXY", 2}), 2);
}

static void t_trim_basic(ctest_t *t) {
    core_str_t r = core_str_trim(LIT("  a b  "));
    check_bytes(t, r, "a b", 3);
    r = core_str_trim(LIT("   abc"));
    check_bytes(t, r, "abc", 3);
    r = core_str_trim(LIT("abc   "));
    check_bytes(t, r, "abc", 3);
    r = core_str_trim(LIT("abc"));
    check_bytes(t, r, "abc", 3);
    r = core_str_trim(LIT(" x "));
    check_bytes(t, r, "x", 1);
    r = core_str_trim(LIT("x"));
    check_bytes(t, r, "x", 1);
}

static void t_trim_returns_view_into_input(ctest_t *t) {
    const char *in = "  hi  ";
    core_str_t s = {in, 6};
    core_str_t r = core_str_trim(s);
    CHECK(t, r.ptr == in + 2);
    CHECK_EQ_U64(t, r.len, 2);
    r = core_str_trim(LIT("abc"));
    CHECK_EQ_U64(t, r.len, 3);
}

static void t_trim_each_whitespace_byte(ctest_t *t) {
    static const char ws[] = {' ', '\t', '\n', '\v', '\f', '\r'};
    size_t i;
    for (i = 0; i < sizeof ws; i++) {
        char in[5];
        core_str_t r;
        in[0] = ws[i];
        in[1] = 'a';
        in[2] = 'b';
        in[3] = ws[i];
        in[4] = ws[i];
        r = core_str_trim((core_str_t){in, 5});
        CHECK_EQ_U64(t, r.len, 2);
        CHECK(t, r.ptr == in + 1);
    }
}

static void t_trim_mixed_whitespace(ctest_t *t) {
    core_str_t r = core_str_trim(LIT(" \t\r\n\v\f x y \f\v\n\r\t "));
    check_bytes(t, r, "x y", 3);
}

static void t_trim_all_or_no_whitespace(ctest_t *t) {
    check_bytes(t, core_str_trim(LIT("  a ")), "a", 1);   /* control */
    CHECK_EQ_U64(t, core_str_trim(LIT("   ")).len, 0);
    CHECK_EQ_U64(t, core_str_trim(LIT(" ")).len, 0);
    CHECK_EQ_U64(t, core_str_trim(LIT("\t\n\v\f\r ")).len, 0);
    CHECK_EQ_U64(t, core_str_trim(LIT("")).len, 0);
    CHECK_EQ_U64(t, core_str_trim(EMPTY).len, 0);
}

static void t_trim_interior_whitespace_kept(ctest_t *t) {
    core_str_t r = core_str_trim(LIT(" a  \t b "));
    check_bytes(t, r, "a  \t b", 6);
}

static void t_trim_only_ascii_whitespace(ctest_t *t) {
    /* NUL, 0x85, 0xA0 (NBSP in Latin-1), 0x1c..0x1f and '_' are not whitespace. */
    core_str_t r = core_str_trim(LIT("\0 a \0"));
    check_bytes(t, r, "\0 a \0", 5);
    r = core_str_trim(LIT("\x85" "a\x85"));
    check_bytes(t, r, "\x85" "a\x85", 3);
    r = core_str_trim(LIT("\xa0" "a\xa0"));
    check_bytes(t, r, "\xa0" "a\xa0", 3);
    r = core_str_trim(LIT("\x1f" "a\x1c"));
    check_bytes(t, r, "\x1f" "a\x1c", 3);
    r = core_str_trim(LIT("\x08" "a\x0e"));   /* just outside 0x09..0x0d */
    check_bytes(t, r, "\x08" "a\x0e", 3);
    r = core_str_trim(LIT("!a!"));            /* just above space */
    check_bytes(t, r, "!a!", 3);
}

static void t_trim_respects_slice_length(ctest_t *t) {
    core_str_t r = core_str_trim((core_str_t){" a  b", 3});   /* slice is " a " */
    check_bytes(t, r, "a", 1);
}

static void t_split_found_basic(ctest_t *t) {
    core_str_t h = {"junk", 99};
    core_str_t tl = {"junk", 99};
    CHECK(t, core_str_split_once(LIT("key=value"), LIT("="), &h, &tl));
    check_bytes(t, h, "key", 3);
    check_bytes(t, tl, "value", 5);
}

static void t_split_points_into_input(ctest_t *t) {
    const char *in = "ab::cd";
    core_str_t s = {in, 6};
    core_str_t h = EMPTY;
    core_str_t tl = EMPTY;
    CHECK(t, core_str_split_once(s, LIT("::"), &h, &tl));
    CHECK(t, h.ptr == in);
    CHECK_EQ_U64(t, h.len, 2);
    CHECK(t, tl.ptr == in + 4);
    CHECK_EQ_U64(t, tl.len, 2);
}

static void t_split_first_occurrence_only(ctest_t *t) {
    core_str_t h = EMPTY;
    core_str_t tl = EMPTY;
    CHECK(t, core_str_split_once(LIT("a=b=c"), LIT("="), &h, &tl));
    check_bytes(t, h, "a", 1);
    check_bytes(t, tl, "b=c", 3);
}

static void t_split_multibyte_separator(ctest_t *t) {
    core_str_t h = EMPTY;
    core_str_t tl = EMPTY;
    CHECK(t, core_str_split_once(LIT("en \xc2\xb7 EPUB \xc2\xb7 1MB"), LIT(" \xc2\xb7 "), &h, &tl));
    check_bytes(t, h, "en", 2);
    check_bytes(t, tl, "EPUB \xc2\xb7 1MB", 11);
    /* Only a prefix of the separator present: not found. */
    CHECK(t, !core_str_split_once(LIT("a \xc2 b"), LIT(" \xc2\xb7 "), &h, &tl));
}

static void t_split_separator_at_start_end_and_whole(ctest_t *t) {
    core_str_t h = LIT("junk");
    core_str_t tl = LIT("junk");
    CHECK(t, core_str_split_once(LIT("=abc"), LIT("="), &h, &tl));
    CHECK_EQ_U64(t, h.len, 0);
    check_bytes(t, tl, "abc", 3);
    h = LIT("junk");
    tl = LIT("junk");
    CHECK(t, core_str_split_once(LIT("abc="), LIT("="), &h, &tl));
    check_bytes(t, h, "abc", 3);
    CHECK_EQ_U64(t, tl.len, 0);
    h = LIT("junk");
    tl = LIT("junk");
    CHECK(t, core_str_split_once(LIT("=="), LIT("=="), &h, &tl));
    CHECK_EQ_U64(t, h.len, 0);
    CHECK_EQ_U64(t, tl.len, 0);
    h = LIT("junk");
    tl = LIT("junk");
    CHECK(t, core_str_split_once(LIT("="), LIT("="), &h, &tl));
    CHECK_EQ_U64(t, h.len, 0);
    CHECK_EQ_U64(t, tl.len, 0);
}

static void t_split_not_found(ctest_t *t) {
    core_str_t s = LIT("abc");
    core_str_t h = LIT("junk");
    core_str_t tl = LIT("junk");
    CHECK(t, !core_str_split_once(s, LIT("="), &h, &tl));
    CHECK(t, h.ptr == s.ptr);
    CHECK_EQ_U64(t, h.len, 3);
    CHECK_EQ_U64(t, tl.len, 0);
}

static void t_split_not_found_cases(ctest_t *t) {
    core_str_t h = LIT("junk");
    core_str_t tl = LIT("junk");
    CHECK(t, !core_str_split_once(LIT("ab"), LIT("abc"), &h, &tl));  /* separator longer than s */
    check_bytes(t, h, "ab", 2);
    CHECK_EQ_U64(t, tl.len, 0);
    h = LIT("junk");
    tl = LIT("junk");
    CHECK(t, !core_str_split_once(EMPTY, LIT("="), &h, &tl));
    CHECK_EQ_U64(t, h.len, 0);
    CHECK_EQ_U64(t, tl.len, 0);
    CHECK(t, !core_str_split_once(LIT("ab="), LIT("=="), &h, &tl));  /* partial at end */
    check_bytes(t, h, "ab=", 3);
    CHECK_EQ_U64(t, tl.len, 0);
}

static void t_split_empty_separator(ctest_t *t) {
    core_str_t s = LIT("abc");
    core_str_t h = LIT("junk");
    core_str_t tl = LIT("junk");
    CHECK(t, !core_str_split_once(s, EMPTY, &h, &tl));
    CHECK(t, h.ptr == s.ptr);
    CHECK_EQ_U64(t, h.len, 3);
    CHECK_EQ_U64(t, tl.len, 0);
    CHECK(t, !core_str_split_once(EMPTY, EMPTY, &h, &tl));
    CHECK_EQ_U64(t, h.len, 0);
    CHECK_EQ_U64(t, tl.len, 0);
}

static void t_split_null_outputs(ctest_t *t) {
    core_str_t h = EMPTY;
    core_str_t tl = EMPTY;
    CHECK(t, core_str_split_once(LIT("a=b"), LIT("="), NULL, &tl));
    check_bytes(t, tl, "b", 1);
    CHECK(t, core_str_split_once(LIT("a=b"), LIT("="), &h, NULL));
    check_bytes(t, h, "a", 1);
    CHECK(t, core_str_split_once(LIT("a=b"), LIT("="), NULL, NULL));
    CHECK(t, !core_str_split_once(LIT("ab"), LIT("="), NULL, NULL));
    CHECK(t, !core_str_split_once(LIT("ab"), LIT("="), NULL, &tl));
    CHECK_EQ_U64(t, tl.len, 0);
    CHECK(t, !core_str_split_once(LIT("ab"), LIT("="), &h, NULL));
    check_bytes(t, h, "ab", 2);
}

static void t_split_respects_slice_length(ctest_t *t) {
    core_str_t h = EMPTY;
    core_str_t tl = EMPTY;
    core_str_t s = {"a=b=c", 3};   /* "a=b" */
    CHECK(t, core_str_split_once(s, LIT("="), &h, &tl));
    check_bytes(t, h, "a", 1);
    check_bytes(t, tl, "b", 1);
    CHECK(t, !core_str_split_once((core_str_t){"ab=", 2}, LIT("="), &h, &tl));
    check_bytes(t, h, "ab", 2);
}

CTEST_SUITE("str",
    {"npos_is_size_max", t_npos_is_size_max},
    {"from_cstr", t_from_cstr},
    {"from_cstr_null", t_from_cstr_null},
    {"eq_equal_cases", t_eq_equal_cases},
    {"eq_empty_slices_are_equal", t_eq_empty_slices_are_equal},
    {"eq_unequal_cases", t_eq_unequal_cases},
    {"starts_with", t_starts_with},
    {"starts_with_empty_prefix", t_starts_with_empty_prefix},
    {"starts_with_respects_slice_length", t_starts_with_respects_slice_length},
    {"find_positions", t_find_positions},
    {"find_first_of_several", t_find_first_of_several},
    {"find_not_found", t_find_not_found},
    {"find_empty_needle", t_find_empty_needle},
    {"find_respects_slice_length", t_find_respects_slice_length},
    {"trim_basic", t_trim_basic},
    {"trim_returns_view_into_input", t_trim_returns_view_into_input},
    {"trim_each_whitespace_byte", t_trim_each_whitespace_byte},
    {"trim_mixed_whitespace", t_trim_mixed_whitespace},
    {"trim_all_or_no_whitespace", t_trim_all_or_no_whitespace},
    {"trim_interior_whitespace_kept", t_trim_interior_whitespace_kept},
    {"trim_only_ascii_whitespace", t_trim_only_ascii_whitespace},
    {"trim_respects_slice_length", t_trim_respects_slice_length},
    {"split_found_basic", t_split_found_basic},
    {"split_points_into_input", t_split_points_into_input},
    {"split_first_occurrence_only", t_split_first_occurrence_only},
    {"split_multibyte_separator", t_split_multibyte_separator},
    {"split_separator_at_start_end_and_whole", t_split_separator_at_start_end_and_whole},
    {"split_not_found", t_split_not_found},
    {"split_not_found_cases", t_split_not_found_cases},
    {"split_empty_separator", t_split_empty_separator},
    {"split_null_outputs", t_split_null_outputs},
    {"split_respects_slice_length", t_split_respects_slice_length})
