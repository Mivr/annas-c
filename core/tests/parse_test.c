/* Tests for core/parse.h: checked integer parsing and ASCII helpers. Public header only. */
#include <stdint.h>
#include <string.h>

#include "core/parse.h"
#include "ctest/ctest.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define MARKER UINT64_C(0xDEADBEEFCAFEF00D)

/* Parses s and expects success with value v. */
static void ok_case(ctest_t *t, core_str_t s, uint64_t v) {
    uint64_t out = MARKER;
    CHECK(t, core_parse_u64(s, &out));
    CHECK_EQ_U64(t, out, v);
}

/* Parses s and expects failure that leaves *out untouched. */
static void bad_case(ctest_t *t, core_str_t s) {
    uint64_t out = MARKER;
    CHECK(t, !core_parse_u64(s, &out));
    CHECK_EQ_U64(t, out, MARKER);
}

static void t_u64_small_values(ctest_t *t) {
    ok_case(t, LIT("0"), 0);
    ok_case(t, LIT("1"), 1);
    ok_case(t, LIT("9"), 9);
    ok_case(t, LIT("10"), 10);
    ok_case(t, LIT("42"), 42);
    ok_case(t, LIT("99"), 99);
    ok_case(t, LIT("100"), 100);
    ok_case(t, LIT("255"), 255);
    ok_case(t, LIT("256"), 256);
    ok_case(t, LIT("65535"), 65535);
    ok_case(t, LIT("65536"), 65536);
    ok_case(t, LIT("1234567890"), 1234567890u);
}

static void t_u64_every_digit(ctest_t *t) {
    char c;
    for (c = '0'; c <= '9'; c++) {
        char in[1];
        in[0] = c;
        ok_case(t, (core_str_t){in, 1}, (uint64_t)(c - '0'));
    }
    ok_case(t, LIT("9876543210"), 9876543210u);
    ok_case(t, LIT("123456789012345678"), 123456789012345678u);
}

static void t_u64_32_bit_boundary(ctest_t *t) {
    ok_case(t, LIT("2147483647"), 2147483647u);
    ok_case(t, LIT("2147483648"), 2147483648u);
    ok_case(t, LIT("4294967295"), 4294967295u);
    ok_case(t, LIT("4294967296"), 4294967296u);
}

static void t_u64_leading_zeros(ctest_t *t) {
    ok_case(t, LIT("007"), 7);
    ok_case(t, LIT("0000"), 0);
    ok_case(t, LIT("00"), 0);
    ok_case(t, LIT("0123"), 123);
    ok_case(t, LIT("000000000000000000000000000000000000000001"), 1);
    ok_case(t, LIT("000000000000000000000018446744073709551615"), UINT64_MAX);
}

static void t_u64_max_boundary(ctest_t *t) {
    ok_case(t, LIT("9223372036854775807"), UINT64_C(9223372036854775807));
    ok_case(t, LIT("9223372036854775808"), UINT64_C(9223372036854775808));
    ok_case(t, LIT("10000000000000000000"), UINT64_C(10000000000000000000));
    ok_case(t, LIT("18446744073709551609"), UINT64_MAX - 6);
    ok_case(t, LIT("18446744073709551614"), UINT64_MAX - 1);
    ok_case(t, LIT("18446744073709551615"), UINT64_MAX);
}

static void t_u64_overflow_rejected(ctest_t *t) {
    ok_case(t, LIT("5"), 5);   /* control: valid input is accepted */
    bad_case(t, LIT("18446744073709551616"));   /* UINT64_MAX + 1 */
    bad_case(t, LIT("18446744073709551617"));
    bad_case(t, LIT("18446744073709551620"));
    bad_case(t, LIT("18446744073709551625"));
    bad_case(t, LIT("18446744073709552000"));
    bad_case(t, LIT("18446744073709559999"));
    bad_case(t, LIT("18446744073709600000"));
    bad_case(t, LIT("18446744073710000000"));
    bad_case(t, LIT("18446744083709551615"));
    bad_case(t, LIT("18446745073709551615"));
    bad_case(t, LIT("18456744073709551615"));
    bad_case(t, LIT("19446744073709551615"));
    bad_case(t, LIT("28446744073709551615"));   /* 20 digits, leading digit too big */
    bad_case(t, LIT("99999999999999999999"));
    bad_case(t, LIT("100000000000000000000"));
    bad_case(t, LIT("184467440737095516150"));  /* UINT64_MAX * 10 */
    bad_case(t, LIT("184467440737095516160"));
    bad_case(t, LIT("1844674407370955161500000000000000000000000000000"));
    bad_case(t, LIT("999999999999999999999999999999999999999999"));
}

static void t_u64_overflow_with_leading_zeros(ctest_t *t) {
    ok_case(t, LIT("5"), 5);   /* control: valid input is accepted */
    bad_case(t, LIT("0018446744073709551616"));
    bad_case(t, LIT("00000000000000000000099999999999999999999"));
}

static void t_u64_malformed_rejected(ctest_t *t) {
    ok_case(t, LIT("5"), 5);   /* control: valid input is accepted */
    bad_case(t, LIT(""));
    bad_case(t, (core_str_t){NULL, 0});
    bad_case(t, LIT("+1"));
    bad_case(t, LIT("-1"));
    bad_case(t, LIT("-0"));
    bad_case(t, LIT("+"));
    bad_case(t, LIT("-"));
    bad_case(t, LIT(" 1"));
    bad_case(t, LIT("1 "));
    bad_case(t, LIT("1 2"));
    bad_case(t, LIT(" "));
    bad_case(t, LIT("\t1"));
    bad_case(t, LIT("1\n"));
    bad_case(t, LIT("1\r\n"));
    bad_case(t, LIT("1a"));
    bad_case(t, LIT("a1"));
    bad_case(t, LIT("12a45"));
    bad_case(t, LIT("0x10"));
    bad_case(t, LIT("0b1"));
    bad_case(t, LIT("1.5"));
    bad_case(t, LIT("1."));
    bad_case(t, LIT(".5"));
    bad_case(t, LIT("1e3"));
    bad_case(t, LIT("1_000"));
    bad_case(t, LIT("1,000"));
    bad_case(t, LIT("abc"));
    bad_case(t, LIT("\xd9\xa1"));      /* Arabic-indic digit one (UTF-8) */
    bad_case(t, LIT("\xb1"));
    bad_case(t, LIT("12\xb3"));
}

static void t_u64_chars_adjacent_to_digits_rejected(ctest_t *t) {
    ok_case(t, LIT("5"), 5);   /* control: valid input is accepted */
    bad_case(t, LIT("/"));    /* '0' - 1 */
    bad_case(t, LIT(":"));    /* '9' + 1 */
    bad_case(t, LIT("1/"));
    bad_case(t, LIT("1:"));
    bad_case(t, LIT("/1"));
    bad_case(t, LIT(":1"));
    bad_case(t, LIT("5/5"));
    bad_case(t, LIT("5:5"));
    bad_case(t, LIT("@"));
    bad_case(t, LIT("`"));
}

static void t_u64_embedded_nul_rejected(ctest_t *t) {
    ok_case(t, LIT("5"), 5);   /* control: valid input is accepted */
    bad_case(t, LIT("\0"));
    bad_case(t, LIT("1\0"));
    bad_case(t, LIT("\0" "1"));
    bad_case(t, LIT("1\0" "2"));
}

static void t_u64_uses_only_slice_length(ctest_t *t) {
    ok_case(t, (core_str_t){"123456", 3}, 123);
    ok_case(t, (core_str_t){"12x", 2}, 12);
    ok_case(t, (core_str_t){"7 ", 1}, 7);
    bad_case(t, (core_str_t){"12", 0});       /* len 0 is empty even with a non-NULL ptr */
    bad_case(t, (core_str_t){"1x", 2});
}

static void t_u64_failure_does_not_modify_output(ctest_t *t) {
    uint64_t out = 77;
    CHECK(t, !core_parse_u64(LIT("12x"), &out));
    CHECK_EQ_U64(t, out, 77);
    CHECK(t, !core_parse_u64(LIT("18446744073709551616"), &out));
    CHECK_EQ_U64(t, out, 77);
    CHECK(t, core_parse_u64(LIT("78"), &out));
    CHECK_EQ_U64(t, out, 78);
}

static void t_u64_null_output(ctest_t *t) {
    ok_case(t, LIT("5"), 5);   /* control */
    CHECK(t, !core_parse_u64(LIT("1"), NULL));
    CHECK(t, !core_parse_u64(LIT(""), NULL));
    CHECK(t, !core_parse_u64(LIT("x"), NULL));
}

static void t_hex_nibble_digits(ctest_t *t) {
    CHECK_EQ_I64(t, core_parse_hex_nibble('0'), 0);
    CHECK_EQ_I64(t, core_parse_hex_nibble('1'), 1);
    CHECK_EQ_I64(t, core_parse_hex_nibble('5'), 5);
    CHECK_EQ_I64(t, core_parse_hex_nibble('9'), 9);
    CHECK_EQ_I64(t, core_parse_hex_nibble('a'), 10);
    CHECK_EQ_I64(t, core_parse_hex_nibble('b'), 11);
    CHECK_EQ_I64(t, core_parse_hex_nibble('c'), 12);
    CHECK_EQ_I64(t, core_parse_hex_nibble('d'), 13);
    CHECK_EQ_I64(t, core_parse_hex_nibble('e'), 14);
    CHECK_EQ_I64(t, core_parse_hex_nibble('f'), 15);
    CHECK_EQ_I64(t, core_parse_hex_nibble('A'), 10);
    CHECK_EQ_I64(t, core_parse_hex_nibble('B'), 11);
    CHECK_EQ_I64(t, core_parse_hex_nibble('C'), 12);
    CHECK_EQ_I64(t, core_parse_hex_nibble('D'), 13);
    CHECK_EQ_I64(t, core_parse_hex_nibble('E'), 14);
    CHECK_EQ_I64(t, core_parse_hex_nibble('F'), 15);
}

static void t_hex_nibble_rejects_neighbours(ctest_t *t) {
    CHECK_EQ_I64(t, core_parse_hex_nibble('/'), -1);   /* '0' - 1 */
    CHECK_EQ_I64(t, core_parse_hex_nibble(':'), -1);   /* '9' + 1 */
    CHECK_EQ_I64(t, core_parse_hex_nibble('@'), -1);   /* 'A' - 1 */
    CHECK_EQ_I64(t, core_parse_hex_nibble('G'), -1);   /* 'F' + 1 */
    CHECK_EQ_I64(t, core_parse_hex_nibble('`'), -1);   /* 'a' - 1 */
    CHECK_EQ_I64(t, core_parse_hex_nibble('g'), -1);   /* 'f' + 1 */
    CHECK_EQ_I64(t, core_parse_hex_nibble('z'), -1);
    CHECK_EQ_I64(t, core_parse_hex_nibble('Z'), -1);
    CHECK_EQ_I64(t, core_parse_hex_nibble(' '), -1);
    CHECK_EQ_I64(t, core_parse_hex_nibble('-'), -1);
    CHECK_EQ_I64(t, core_parse_hex_nibble('\0'), -1);
    CHECK_EQ_I64(t, core_parse_hex_nibble((char)0x80), -1);
    CHECK_EQ_I64(t, core_parse_hex_nibble((char)0xB0), -1);
    CHECK_EQ_I64(t, core_parse_hex_nibble((char)0xFF), -1);
}

static void t_hex_nibble_all_256_bytes(ctest_t *t) {
    int i;
    for (i = 0; i < 256; i++) {
        int want = -1;
        if (i >= '0' && i <= '9') want = i - '0';
        if (i >= 'a' && i <= 'f') want = i - 'a' + 10;
        if (i >= 'A' && i <= 'F') want = i - 'A' + 10;
        CHECK_EQ_I64(t, core_parse_hex_nibble((char)i), want);
    }
}

static void t_ascii_lower_letters(ctest_t *t) {
    CHECK_EQ_I64(t, core_ascii_lower('A'), 'a');
    CHECK_EQ_I64(t, core_ascii_lower('B'), 'b');
    CHECK_EQ_I64(t, core_ascii_lower('M'), 'm');
    CHECK_EQ_I64(t, core_ascii_lower('Y'), 'y');
    CHECK_EQ_I64(t, core_ascii_lower('Z'), 'z');
    CHECK_EQ_I64(t, core_ascii_lower('a'), 'a');
    CHECK_EQ_I64(t, core_ascii_lower('z'), 'z');
}

static void t_ascii_lower_neighbours_and_others_unchanged(ctest_t *t) {
    CHECK_EQ_I64(t, core_ascii_lower('@'), '@');     /* 'A' - 1 */
    CHECK_EQ_I64(t, core_ascii_lower('['), '[');     /* 'Z' + 1 */
    CHECK_EQ_I64(t, core_ascii_lower('`'), '`');
    CHECK_EQ_I64(t, core_ascii_lower('{'), '{');
    CHECK_EQ_I64(t, core_ascii_lower('0'), '0');
    CHECK_EQ_I64(t, core_ascii_lower('9'), '9');
    CHECK_EQ_I64(t, core_ascii_lower(' '), ' ');
    CHECK_EQ_I64(t, core_ascii_lower('\0'), 0);
    CHECK_EQ_I64(t, core_ascii_lower('\x7f'), 0x7f);
    CHECK_EQ_I64(t, (unsigned char)core_ascii_lower((char)0xC1), 0xC1);   /* Latin-1 'A'-acute */
    CHECK_EQ_I64(t, (unsigned char)core_ascii_lower((char)0xFF), 0xFF);
}

static void t_ascii_lower_all_256_bytes(ctest_t *t) {
    int i;
    for (i = 0; i < 256; i++) {
        int want = i;
        if (i >= 'A' && i <= 'Z') want = i - 'A' + 'a';
        CHECK_EQ_I64(t, (unsigned char)core_ascii_lower((char)i), want);
    }
}

CTEST_SUITE("parse",
    {"u64_small_values", t_u64_small_values},
    {"u64_every_digit", t_u64_every_digit},
    {"u64_32_bit_boundary", t_u64_32_bit_boundary},
    {"u64_leading_zeros", t_u64_leading_zeros},
    {"u64_max_boundary", t_u64_max_boundary},
    {"u64_overflow_rejected", t_u64_overflow_rejected},
    {"u64_overflow_with_leading_zeros", t_u64_overflow_with_leading_zeros},
    {"u64_malformed_rejected", t_u64_malformed_rejected},
    {"u64_chars_adjacent_to_digits_rejected", t_u64_chars_adjacent_to_digits_rejected},
    {"u64_embedded_nul_rejected", t_u64_embedded_nul_rejected},
    {"u64_uses_only_slice_length", t_u64_uses_only_slice_length},
    {"u64_failure_does_not_modify_output", t_u64_failure_does_not_modify_output},
    {"u64_null_output", t_u64_null_output},
    {"hex_nibble_digits", t_hex_nibble_digits},
    {"hex_nibble_rejects_neighbours", t_hex_nibble_rejects_neighbours},
    {"hex_nibble_all_256_bytes", t_hex_nibble_all_256_bytes},
    {"ascii_lower_letters", t_ascii_lower_letters},
    {"ascii_lower_neighbours_and_others_unchanged", t_ascii_lower_neighbours_and_others_unchanged},
    {"ascii_lower_all_256_bytes", t_ascii_lower_all_256_bytes})
