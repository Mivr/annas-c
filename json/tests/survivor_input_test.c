/* Tests for json/json.h (A044): the input ends where in.len says, argument checks, and the arena contents at
 * every capacity. Public header only. */
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

static json_err_t run(core_str_t in, const json_limits_t *lim, const json_value_t **root, size_t *off) {
    if (!core_arena_init(&g_a, g_mem, sizeof g_mem)) {
        return JSON_E_ARG;
    }
    *root = SENTINEL;
    *off = OFF_MARK;
    return json_parse(&g_a, in, lim, root, off);
}

static void expect_err(ctest_t *t, core_str_t in, const json_limits_t *lim, json_err_t code, size_t at) {
    const json_value_t *root;
    size_t off;
    CHECK_EQ_I64(t, run(in, lim, &root, &off), code);
    CHECK_EQ_U64(t, off, at);
    CHECK(t, root == NULL);
}


/* `full` is copied to a buffer, the parse sees only its first `cut` bytes. */
static char g_tail[64];

static core_str_t cut_of(const char *full, size_t full_len, size_t cut) {
    memcpy(g_tail, full, full_len);
    return (core_str_t){g_tail, cut};
}


static void number_cut_ok(ctest_t *t, const char *full, size_t full_len, size_t cut) {
    const json_value_t *root;
    size_t off;
    core_str_t txt = {NULL, 0};
    CHECK_EQ_I64(t, run(cut_of(full, full_len, cut), NULL, &root, &off), JSON_OK);
    CHECK_EQ_U64(t, off, 0);
    CHECK_EQ_I64(t, json_type(root), JSON_NUMBER);
    CHECK(t, json_number_text(root, &txt));
    CHECK_EQ_MEM(t, txt.ptr, txt.len, full, cut);
}

static void t_number_ends_at_input_end(ctest_t *t) {
    number_cut_ok(t, "1.5", 3, 1);
    number_cut_ok(t, "1e5", 3, 1);
    number_cut_ok(t, "1E5", 3, 1);
    number_cut_ok(t, "0.5", 3, 1);
    number_cut_ok(t, "-1.5", 4, 2);
    number_cut_ok(t, "1.5e5", 5, 3);
    number_cut_ok(t, "1.5E5", 5, 3);
    number_cut_ok(t, "1e+5", 4, 1);
    number_cut_ok(t, "12.50e-1", 8, 5);
}

static void t_exponent_sign_after_input_end(ctest_t *t) {
    expect_err(t, cut_of("1e+5", 4, 2), NULL, JSON_E_NUMBER, 2);
    expect_err(t, cut_of("1E-5", 4, 2), NULL, JSON_E_NUMBER, 2);
    expect_err(t, cut_of("1.5e+7", 6, 4), NULL, JSON_E_NUMBER, 4);
    expect_err(t, cut_of("1.5E-7", 6, 4), NULL, JSON_E_NUMBER, 4);
    expect_err(t, cut_of("1.5", 3, 2), NULL, JSON_E_NUMBER, 2);
    expect_err(t, cut_of("[1.5", 4, 3), NULL, JSON_E_NUMBER, 3);
}

/* ---- an invalid first byte of a value is a syntax error even when the element limit is exhausted ---- */

static void t_invalid_start_byte_beats_element_limit(ctest_t *t) {
    static const char bad[] = "x+.}]:,'NTF/ \\\x01\x7f\x80\xff";
    json_limits_t lim = json_limits_default();
    char doc[8];
    size_t i;
    lim.max_elements = 0;
    for (i = 0; i + 1 < sizeof bad; i++) {
        if (bad[i] == ' ') {
            continue;
        }
        doc[0] = bad[i];
        expect_err(t, (core_str_t){doc, 1}, &lim, JSON_E_SYNTAX, 0);
    }
    lim.max_elements = 2;
    expect_err(t, LIT("[1,x]"), &lim, JSON_E_SYNTAX, 3);
    expect_err(t, LIT("[1,+]"), &lim, JSON_E_SYNTAX, 3);
    expect_err(t, LIT("[1,2]"), &lim, JSON_E_LIMIT, 3);
    expect_err(t, LIT("{\"a\":x}"), &lim, JSON_E_SYNTAX, 5);
    lim.max_elements = 1;
    expect_err(t, LIT("{\"a\":x}"), &lim, JSON_E_SYNTAX, 5);
}

/* ---- json_parse argument checks ---- */

static void t_parse_arguments(ctest_t *t) {
    const json_value_t *root = SENTINEL;
    size_t off = OFF_MARK;
    static const char one[] = "1";
    CHECK(t, core_arena_init(&g_a, g_mem, sizeof g_mem));
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("1"), NULL, NULL, &off), JSON_E_ARG);
    CHECK_EQ_U64(t, off, 0);
    off = OFF_MARK;
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("1"), NULL, NULL, NULL), JSON_E_ARG);
    CHECK_EQ_I64(t, json_parse(NULL, LIT("1"), NULL, NULL, &off), JSON_E_ARG);
    CHECK_EQ_U64(t, off, 0);
    off = OFF_MARK;
    CHECK_EQ_I64(t, json_parse(NULL, LIT("1"), NULL, &root, &off), JSON_E_ARG);
    CHECK(t, root == NULL);
    CHECK_EQ_U64(t, off, 0);
    root = SENTINEL;
    off = OFF_MARK;
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){NULL, 1}, NULL, &root, &off), JSON_E_ARG);
    CHECK(t, root == NULL);
    CHECK_EQ_U64(t, off, 0);
    root = SENTINEL;
    off = OFF_MARK;
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){NULL, 2}, NULL, &root, &off), JSON_E_ARG);
    CHECK_EQ_U64(t, off, 0);
    root = SENTINEL;
    off = OFF_MARK;
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){NULL, 0}, NULL, &root, &off), JSON_E_SYNTAX);
    CHECK(t, root == NULL);
    CHECK_EQ_U64(t, off, 0);
    root = SENTINEL;
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){one, 1}, NULL, &root, NULL), JSON_OK);
    CHECK_EQ_I64(t, json_type(root), JSON_NUMBER);
}

/* ---- arena: exact content at every capacity, nothing written past the capacity ---- */

#define GUARD 64u
static _Alignas(16) unsigned char g_reg[16384];

static bool same_text(const json_value_t *v, core_str_t want) {
    core_str_t got;
    return json_get_str(v, &got) && core_str_eq(got, want);
}

/* Parses `in` over g_reg[0..cap) with a guard band behind it; returns the error; checks the band. */
static json_err_t parse_guarded(ctest_t *t, core_str_t in, size_t cap, const json_value_t **root, size_t *off) {
    json_err_t e;
    size_t i;
    memset(g_reg + cap, 0xA5, GUARD);
    CHECK(t, core_arena_init(&g_a, g_reg, cap));
    *root = SENTINEL;
    *off = OFF_MARK;
    e = json_parse(&g_a, in, NULL, root, off);
    for (i = 0; i < GUARD; i++) {
        if (g_reg[cap + i] != 0xA5) {
            CHECK(t, g_reg[cap + i] == 0xA5);
            break;
        }
    }
    return e;
}

/* `check` verifies the parsed DOM of `in`. */
typedef void (*dom_check_fn)(ctest_t *t, const json_value_t *root);

static void sweep(ctest_t *t, core_str_t in, dom_check_fn check) {
    const json_value_t *root;
    size_t off, cap, ok_from = SIZE_MAX;
    for (cap = 0; cap <= 1024; cap++) {
        json_err_t e = parse_guarded(t, in, cap, &root, &off);
        if (e == JSON_OK) {
            if (ok_from == SIZE_MAX) {
                ok_from = cap;
            }
            CHECK_EQ_U64(t, off, 0);
            CHECK(t, root != NULL && root != SENTINEL);
            CHECK(t, core_arena_used(&g_a) <= cap);
            if (root != NULL && root != SENTINEL) {
                check(t, root);
            }
        } else {
            CHECK_EQ_I64(t, e, JSON_E_NOMEM);
            CHECK(t, root == NULL);
            CHECK(t, off <= in.len);
            CHECK_EQ_U64(t, core_arena_used(&g_a), 0);
            CHECK(t, ok_from == SIZE_MAX);
        }
    }
    CHECK(t, ok_from != SIZE_MAX);
}

static void chk_long_string(ctest_t *t, const json_value_t *r) {
    CHECK(t, same_text(r, LIT("abcdefghijklmnopqrstuvwxyz0123456789")));
}

static void chk_long_number(ctest_t *t, const json_value_t *r) {
    core_str_t n = {NULL, 0};
    CHECK(t, json_number_text(r, &n));
    CHECK(t, core_str_eq(n, LIT("-12345678901234567890.12345678901234567890e+123")));
}

static void chk_two_byte_last(ctest_t *t, const json_value_t *r) { CHECK(t, same_text(r, LIT("hell\xc3\xa9"))); }

static void chk_three_byte_last(ctest_t *t, const json_value_t *r) {
    CHECK(t, same_text(r, LIT("ab\xe2\x82\xac")));
}

static void chk_four_byte_last(ctest_t *t, const json_value_t *r) {
    CHECK(t, same_text(r, LIT("\xf0\x9f\x98\x80\xf0\x9f\x98\x80")));
}

static void chk_escaped(ctest_t *t, const json_value_t *r) {
    CHECK(t, same_text(r, LIT("a\xc3\xa9\xf0\x9f\x98\x80\n" "b")));
}

static void chk_object(ctest_t *t, const json_value_t *r) {
    const json_value_t *a;
    CHECK_EQ_I64(t, json_type(r), JSON_OBJECT);
    CHECK_EQ_U64(t, json_len(r), 2);
    CHECK(t, core_str_eq(json_key_at(r, 0), LIT("abcdefghijklmnopqrstuvwxyz")));
    CHECK(t, core_str_eq(json_key_at(r, 1), LIT("k\xc3\xa9y")));
    CHECK(t, same_text(json_at(r, 0), LIT("0123456789ABCDEFGHIJ")));
    a = json_at(r, 1);
    CHECK_EQ_I64(t, json_type(a), JSON_ARRAY);
    CHECK_EQ_U64(t, json_len(a), 2);
    CHECK(t, same_text(json_at(a, 0), LIT("xyz")));
    CHECK_EQ_I64(t, json_type(json_at(a, 1)), JSON_NUMBER);
}

static void chk_array(ctest_t *t, const json_value_t *r) {
    core_str_t n = {NULL, 0};
    CHECK_EQ_U64(t, json_len(r), 3);
    CHECK(t, same_text(json_at(r, 0), LIT("xyz")));
    CHECK(t, json_number_text(json_at(r, 1), &n));
    CHECK(t, core_str_eq(n, LIT("123456789012")));
    CHECK(t, same_text(json_at(r, 2), LIT("abcdefghijklmnop")));
}

static void t_arena_exact_content_strings(ctest_t *t) {
    sweep(t, LIT("\"abcdefghijklmnopqrstuvwxyz0123456789\""), chk_long_string);
    sweep(t, LIT("\"hell\xc3\xa9\""), chk_two_byte_last);
    sweep(t, LIT("\"ab\xe2\x82\xac\""), chk_three_byte_last);
    sweep(t, LIT("\"\xf0\x9f\x98\x80\xf0\x9f\x98\x80\""), chk_four_byte_last);
    sweep(t, LIT("\"a\\u00e9\\ud83d\\ude00\\n" "b\""), chk_escaped);
}

static void t_arena_exact_content_numbers(ctest_t *t) {
    sweep(t, LIT("-12345678901234567890.12345678901234567890e+123"), chk_long_number);
}

static void t_arena_exact_content_containers(ctest_t *t) {
    sweep(t, LIT("{\"abcdefghijklmnopqrstuvwxyz\":\"0123456789ABCDEFGHIJ\",\"k\xc3\xa9y\":[\"xyz\",1234567]}"),
          chk_object);
    sweep(t, LIT("[\"xyz\",123456789012,\"abcdefghijklmnop\"]"), chk_array);
}


CTEST_SUITE("survivor_input",
    {"number_ends_at_input_end", t_number_ends_at_input_end},
    {"exponent_sign_after_input_end", t_exponent_sign_after_input_end},
    {"invalid_start_byte_beats_element_limit", t_invalid_start_byte_beats_element_limit},
    {"parse_arguments", t_parse_arguments},
    {"arena_exact_content_strings", t_arena_exact_content_strings},
    {"arena_exact_content_numbers", t_arena_exact_content_numbers},
    {"arena_exact_content_containers", t_arena_exact_content_containers})
