/* Tests for json/json.h: numbers, conversions and number errors. Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/arena.h"
#include "core/buf.h"
#include "core/str.h"
#include "ctest/ctest.h"
#include "json/json.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define OFF_MARK 12345u

static unsigned char g_mem[1u << 20];
static core_arena_t g_a;
static int g_sentinel;
#define SENTINEL ((const json_value_t *)(const void *)&g_sentinel)

/* Fresh arena, then parse; presets the outputs to garbage so "untouched" and "reset" are visible. */
static json_err_t run(core_str_t in, const json_limits_t *lim, const json_value_t **root, size_t *off) {
    if (!core_arena_init(&g_a, g_mem, sizeof g_mem)) {
        return JSON_E_ARG;
    }
    *root = SENTINEL;
    *off = OFF_MARK;
    return json_parse(&g_a, in, lim, root, off);
}

/* Parses `in` with default limits, expects success and returns the root (NULL after a failed check). */
static const json_value_t *ok_parse(ctest_t *t, core_str_t in) {
    const json_value_t *root;
    size_t off;
    json_err_t e = run(in, NULL, &root, &off);
    CHECK_EQ_I64(t, e, JSON_OK);
    CHECK_EQ_U64(t, off, 0);
    CHECK(t, root != NULL && root != SENTINEL);
    return e == JSON_OK ? root : NULL;
}

/* Copies tmpl to dst without its first '^'; *at = index the '^' stood at (SIZE_MAX if none). */
static size_t unmark(const char *tmpl, char *dst, size_t *at) {
    size_t i = 0, o = 0;
    *at = SIZE_MAX;
    for (; tmpl[i] != '\0'; i++) {
        if (tmpl[i] == '^' && *at == SIZE_MAX) {
            *at = o;
        } else {
            dst[o++] = tmpl[i];
        }
    }
    return o;
}

static bool str_is(core_str_t s, core_str_t want) { return core_str_eq(s, want); }

#define MARK_I (INT64_C(0x1234567890ab))
#define MARK_U (UINT64_C(0xfedcba9876543210))

typedef struct {
    const char *text;
    int i_ok;       /* json_get_i64 succeeds */
    int64_t i_val;
    int u_ok;       /* json_get_u64 succeeds */
    uint64_t u_val;
} num_case_t;

static void check_number(ctest_t *t, const num_case_t *c) {
    core_str_t nt = {NULL, 0};
    int64_t i = MARK_I;
    uint64_t u = MARK_U;
    core_str_t in = core_str_from_cstr(c->text);
    const json_value_t *v = ok_parse(t, in);
    CHECK_EQ_I64(t, json_type(v), JSON_NUMBER);
    CHECK(t, json_number_text(v, &nt) && str_is(nt, in));
    CHECK(t, json_get_i64(v, &i) == (c->i_ok != 0));
    CHECK_EQ_I64(t, i, c->i_ok ? c->i_val : MARK_I);
    CHECK(t, json_get_u64(v, &u) == (c->u_ok != 0));
    CHECK_EQ_U64(t, u, c->u_ok ? c->u_val : MARK_U);
}

static void run_numbers(ctest_t *t, const num_case_t *cases, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        check_number(t, &cases[i]);
    }
}

static void t_small_integers(ctest_t *t) {
    static const num_case_t c[] = {
        {"0", 1, 0, 1, 0},
        {"1", 1, 1, 1, 1},
        {"9", 1, 9, 1, 9},
        {"10", 1, 10, 1, 10},
        {"42", 1, 42, 1, 42},
        {"99", 1, 99, 1, 99},
        {"100", 1, 100, 1, 100},
        {"255", 1, 255, 1, 255},
        {"256", 1, 256, 1, 256},
        {"65535", 1, 65535, 1, 65535},
        {"65536", 1, 65536, 1, 65536},
        {"2147483647", 1, INT32_MAX, 1, INT32_MAX},
        {"2147483648", 1, INT64_C(2147483648), 1, UINT64_C(2147483648)},
        {"4294967295", 1, INT64_C(4294967295), 1, UINT64_C(4294967295)},
        {"4294967296", 1, INT64_C(4294967296), 1, UINT64_C(4294967296)},
        {"1234567890123456789", 1, INT64_C(1234567890123456789), 1, UINT64_C(1234567890123456789)},
        {"-1", 1, -1, 0, 0},
        {"-9", 1, -9, 0, 0},
        {"-10", 1, -10, 0, 0},
        {"-2147483648", 1, INT32_MIN, 0, 0},
        {"-2147483649", 1, INT64_C(-2147483649), 0, 0},
        {"-4294967296", 1, INT64_C(-4294967296), 0, 0},
        {"-0", 1, 0, 0, 0},
    };
    run_numbers(t, c, sizeof c / sizeof c[0]);
}

static void t_integer_range_edges(ctest_t *t) {
    static const num_case_t c[] = {
        {"9223372036854775806", 1, INT64_MAX - 1, 1, UINT64_C(9223372036854775806)},
        {"9223372036854775807", 1, INT64_MAX, 1, UINT64_C(9223372036854775807)},
        {"9223372036854775808", 0, 0, 1, UINT64_C(9223372036854775808)},
        {"9223372036854775809", 0, 0, 1, UINT64_C(9223372036854775809)},
        {"10000000000000000000", 0, 0, 1, UINT64_C(10000000000000000000)},
        {"18446744073709551614", 0, 0, 1, UINT64_MAX - 1},
        {"18446744073709551615", 0, 0, 1, UINT64_MAX},
        {"18446744073709551616", 0, 0, 0, 0},
        {"18446744073709551617", 0, 0, 0, 0},
        {"18446744073709551625", 0, 0, 0, 0},
        {"18446744073709551626", 0, 0, 0, 0},
        {"19999999999999999999", 0, 0, 0, 0},
        {"20000000000000000000", 0, 0, 0, 0},
        {"28446744073709551615", 0, 0, 0, 0},
        {"36893488147419103232", 0, 0, 0, 0},
        {"92233720368547758080", 0, 0, 0, 0},
        {"99999999999999999999", 0, 0, 0, 0},
        {"1000000000000000000000000000000000000000", 0, 0, 0, 0},
        {"-9223372036854775807", 1, -INT64_MAX, 0, 0},
        {"-9223372036854775808", 1, INT64_MIN, 0, 0},
        {"-9223372036854775809", 0, 0, 0, 0},
        {"-9223372036854775810", 0, 0, 0, 0},
        {"-10000000000000000000", 0, 0, 0, 0},
        {"-18446744073709551615", 0, 0, 0, 0},
        {"-18446744073709551616", 0, 0, 0, 0},
        {"-99999999999999999999", 0, 0, 0, 0},
        {"-1000000000000000000000000000000000000000", 0, 0, 0, 0},
    };
    run_numbers(t, c, sizeof c / sizeof c[0]);
}

static void t_non_integer_literals_do_not_convert(ctest_t *t) {
    static const num_case_t c[] = {
        {"0.0", 0, 0, 0, 0},   {"-0.0", 0, 0, 0, 0},   {"1.0", 0, 0, 0, 0},   {"1.5", 0, 0, 0, 0},
        {"0.5", 0, 0, 0, 0},   {"-0.5", 0, 0, 0, 0},   {"10.25", 0, 0, 0, 0}, {"1e0", 0, 0, 0, 0},
        {"1e2", 0, 0, 0, 0},   {"1E2", 0, 0, 0, 0},    {"1e+2", 0, 0, 0, 0},  {"1e-2", 0, 0, 0, 0},
        {"1E+2", 0, 0, 0, 0},  {"1E-2", 0, 0, 0, 0},   {"0e0", 0, 0, 0, 0},   {"0E0", 0, 0, 0, 0},
        {"-0e0", 0, 0, 0, 0},  {"1.5e3", 0, 0, 0, 0},  {"1.5E-3", 0, 0, 0, 0}, {"0.0e0", 0, 0, 0, 0},
        {"-1e-0", 0, 0, 0, 0}, {"123.456e789", 0, 0, 0, 0}, {"1e999999999999", 0, 0, 0, 0},
        {"12345678901234567890.5", 0, 0, 0, 0}, {"3.14159265358979323846264338327950288", 0, 0, 0, 0},
        {"100e-2", 0, 0, 0, 0}, {"5e00", 0, 0, 0, 0},  {"0.000001", 0, 0, 0, 0}, {"-123.0e+0", 0, 0, 0, 0},
    };
    run_numbers(t, c, sizeof c / sizeof c[0]);
}

static void t_number_text_preserved_verbatim(ctest_t *t) {
    static const char *const texts[] = {"-0", "0", "1.50", "1E+2", "1e-07", "-0.0", "0.10", "100", "5e00",
                                        "1.000000000000000000001"};
    size_t i;
    for (i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        core_str_t nt = {NULL, 0};
        const json_value_t *v = ok_parse(t, core_str_from_cstr(texts[i]));
        CHECK(t, json_number_text(v, &nt));
        CHECK(t, str_is(nt, core_str_from_cstr(texts[i])));
    }
}

static void t_long_number_text(ctest_t *t) {
    static char in[400];
    size_t i;
    core_str_t nt = {NULL, 0};
    const json_value_t *v;
    int64_t x = MARK_I;
    in[0] = '-';
    in[1] = '1';
    for (i = 2; i < 300; i++) {
        in[i] = (char)('0' + (i % 10));
    }
    v = ok_parse(t, (core_str_t){in, 300});
    CHECK(t, json_number_text(v, &nt) && nt.len == 300 && memcmp(nt.ptr, in, 300) == 0);
    CHECK(t, !json_get_i64(v, &x));
    CHECK_EQ_I64(t, x, MARK_I);
    /* a long fraction of zeros after "1." is legal and is not an integer */
    memcpy(in, "1.", 2);
    memset(in + 2, '0', 300);
    v = ok_parse(t, (core_str_t){in, 302});
    CHECK(t, !json_get_i64(v, &x));
}

static void t_numbers_inside_containers(ctest_t *t) {
    int64_t a = 0, b = 0;
    uint64_t c = 0;
    const json_value_t *v = ok_parse(t, LIT("[-5,{\"x\":18446744073709551615},[7]]"));
    CHECK(t, json_get_i64(json_at(v, 0), &a) && a == -5);
    CHECK(t, json_get_u64(json_obj_get(json_at(v, 1), LIT("x")), &c) && c == UINT64_MAX);
    CHECK(t, json_get_i64(json_at(json_at(v, 2), 0), &b) && b == 7);
    CHECK(t, !json_get_i64(json_obj_get(json_at(v, 1), LIT("x")), &b));
    CHECK_EQ_I64(t, b, 7);
}

static void t_negative_zero_conversions(ctest_t *t) {
    int64_t i = MARK_I;
    uint64_t u = MARK_U;
    const json_value_t *v = ok_parse(t, LIT("-0"));
    CHECK(t, json_get_i64(v, &i));
    CHECK_EQ_I64(t, i, 0);
    CHECK(t, !json_get_u64(v, &u));
    CHECK_EQ_U64(t, u, MARK_U);
    v = ok_parse(t, LIT("0"));
    CHECK(t, json_get_u64(v, &u));
    CHECK_EQ_U64(t, u, 0);
}

static void num_err(ctest_t *t, const char *tmpl, json_err_t code) {
    char buf[128];
    size_t at;
    size_t n = unmark(tmpl, buf, &at);
    const json_value_t *root;
    size_t off;
    json_err_t e = run((core_str_t){buf, n}, NULL, &root, &off);
    CHECK_EQ_I64(t, e, code);
    CHECK_EQ_U64(t, off, at);
    CHECK(t, root == NULL);
    CHECK_EQ_U64(t, core_arena_used(&g_a), 0);
}

static void t_minus_needs_a_digit(ctest_t *t) {
    static const char *const bad[] = {
        "-^", "-^x", "-^ 1", "-^-1", "-^+1", "-^.5", "-^a", "-^Infinity", "-^NaN", "-^e5", "-^]", "[-^]",
        "[-^,1]", "[1,-^]", "{\"a\":-^}", "{\"a\":-^,\"b\":1}", "[ -^ ]", "[-^\"1\"]", "[-^-]", "-^\n1",
    };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        num_err(t, bad[i], JSON_E_NUMBER);
    }
}

static void t_leading_zeros_rejected(ctest_t *t) {
    static const char *const bad[] = {
        "0^0", "0^1", "0^9", "0^01", "0^0.5", "0^7", "-0^0", "-0^1", "-0^9", "[0^1]", "[-0^7]", "[1,0^2]", "{\"a\":0^1}",
        "0^1.5", "-0^1e5", "[0^0,0]",
    };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        num_err(t, bad[i], JSON_E_NUMBER);
    }
    /* controls: a single zero followed by a non-digit is fine */
    CHECK(t, ok_parse(t, LIT("[0,0.5,-0,0e1,0E-1]")) != NULL);
}

static void t_fraction_needs_a_digit(ctest_t *t) {
    static const char *const bad[] = {
        "1.^", "1.^x", "1.^e5", "1.^E5", "[1.^]", "[1.^,2]", "0.^", "-1.^", "-0.^", "{\"a\":1.^}", "1.^ ",
        "1.^.5", "1.^-", "1.^+1", "1.^\"", "12345.^", "[0.^ ]",
    };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        num_err(t, bad[i], JSON_E_NUMBER);
    }
    CHECK(t, ok_parse(t, LIT("[1.0,0.0,1.5e1]")) != NULL);
}

static void t_exponent_needs_a_digit(ctest_t *t) {
    static const char *const bad[] = {
        "1e^", "1E^", "1e+^", "1e-^", "1E+^", "1E-^", "1e^x", "1e+^x", "1e-^x", "1e^.", "1e+^+",
        "1e+^-", "1e-^+", "1e^ ", "1e-^-", "1.5e^", "1.5E+^", "[1e^]", "[1e+^]", "[1e^,2]", "{\"a\":1e-^}", "0e^", "-0e^",
        "-1e+^", "1e^e5", "1e+^e5",
    };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        num_err(t, bad[i], JSON_E_NUMBER);
    }
}

static void t_junk_after_complete_number_is_syntax(ctest_t *t) {
    static const char *const bad[] = {
        "1^x", "1^a", "0^x10", "0^x1F", "1.5^.2", "1.5^.", "1e5^e", "1e5^.5", "1e5^E", "1^_000", "1^+1", "1^-1",
        "-1^-1", "1^,", "1 ^2", "[1 ^2]", "[1^x]", "[1.5^.5]", "{\"a\":1^x}", "1^]", "1^}", "1^:",
        "1e5^+", "0.5^.", "0.5e1^.", "123^abc", "-5^f",
    };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        num_err(t, bad[i], JSON_E_SYNTAX);
    }
}

static void t_not_a_number_start(ctest_t *t) {
    static const char *const bad[] = {
        "^+1", "^.5", "^Infinity", "^NaN", "n^an", "^inf", "^Inf", "[^+1]", "[^.5]", "[1,^+2]", "[^NaN]",
        "{\"a\":^+1}", "^e5", "^E5", "^#", "^,", "^:", "^]", "^}", "^'1'",
    };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        num_err(t, bad[i], JSON_E_SYNTAX);
    }
}

CTEST_SUITE("number",
    {"small_integers", t_small_integers},
    {"integer_range_edges", t_integer_range_edges},
    {"non_integer_literals_do_not_convert", t_non_integer_literals_do_not_convert},
    {"number_text_preserved_verbatim", t_number_text_preserved_verbatim},
    {"long_number_text", t_long_number_text},
    {"numbers_inside_containers", t_numbers_inside_containers},
    {"negative_zero_conversions", t_negative_zero_conversions},
    {"minus_needs_a_digit", t_minus_needs_a_digit},
    {"leading_zeros_rejected", t_leading_zeros_rejected},
    {"fraction_needs_a_digit", t_fraction_needs_a_digit},
    {"exponent_needs_a_digit", t_exponent_needs_a_digit},
    {"junk_after_complete_number_is_syntax", t_junk_after_complete_number_is_syntax},
    {"not_a_number_start", t_not_a_number_start})
