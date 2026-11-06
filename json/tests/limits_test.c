/* Tests for json/json.h: depth, size, string and element limits; out of memory. Public header only. */
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


static char g_text[1u << 14];
static char g_big[JSON_MAX_INPUT + 2];
static unsigned char g_arena_big[4u << 20];

static json_limits_t lim_depth(size_t d) {
    json_limits_t l = json_limits_default();
    l.max_depth = d;
    return l;
}

/* d nested arrays around a 0: "[[0]]". */
static size_t nest_arrays(size_t d) {
    size_t i;
    for (i = 0; i < d; i++) {
        g_text[i] = '[';
        g_text[d + 1 + i] = ']';
    }
    g_text[d] = '0';
    return 2 * d + 1;
}

/* d nested single-member objects around a 0: {"a":{"a":0}}. */
static size_t nest_objects(size_t d) {
    size_t i, n = 0;
    for (i = 0; i < d; i++) {
        memcpy(g_text + n, "{\"a\":", 5);
        n += 5;
    }
    g_text[n++] = '0';
    for (i = 0; i < d; i++) {
        g_text[n++] = '}';
    }
    return n;
}

static void expect_err(ctest_t *t, core_str_t in, const json_limits_t *l, json_err_t code, size_t at) {
    const json_value_t *root;
    size_t off;
    CHECK_EQ_I64(t, run(in, l, &root, &off), code);
    CHECK_EQ_U64(t, off, at);
    CHECK(t, root == NULL);
    CHECK_EQ_U64(t, core_arena_used(&g_a), 0);
}

static void expect_ok(ctest_t *t, core_str_t in, const json_limits_t *l) {
    const json_value_t *root;
    size_t off;
    CHECK_EQ_I64(t, run(in, l, &root, &off), JSON_OK);
    CHECK_EQ_U64(t, off, 0);
    CHECK(t, root != NULL && root != SENTINEL);
}

/* Depth of a chain made by nest_*: number of containers before the 0. */
static size_t chain_depth(const json_value_t *v) {
    size_t d = 0;
    while (json_type(v) == JSON_ARRAY || json_type(v) == JSON_OBJECT) {
        v = json_at(v, 0);
        d++;
    }
    return d;
}

static void depth_sweep(ctest_t *t, size_t (*build)(size_t), size_t unit) {
    static const size_t maxes[] = {0, 1, 2, 3, 10, 63, 64};
    size_t m;
    for (m = 0; m < sizeof maxes / sizeof maxes[0]; m++) {
        size_t max = maxes[m], d;
        json_limits_t l = lim_depth(max);
        for (d = (max > 0 ? max - 1 : 0); d <= max + 2 && d <= 66; d++) {
            size_t n = build(d);
            if (d <= max) {
                const json_value_t *root;
                size_t off;
                CHECK_EQ_I64(t, run((core_str_t){g_text, n}, &l, &root, &off), JSON_OK);
                CHECK_EQ_U64(t, chain_depth(root), d);
            } else {
                expect_err(t, (core_str_t){g_text, n}, &l, JSON_E_DEPTH, max * unit);
            }
        }
    }
}

static void t_depth_arrays_boundaries(ctest_t *t) { depth_sweep(t, nest_arrays, 1); }

static void t_depth_objects_boundaries(ctest_t *t) { depth_sweep(t, nest_objects, 5); }

static void t_depth_mixed_containers(ctest_t *t) {
    core_str_t doc = LIT("[{\"a\":[{\"a\":0}]}]");   /* depth 4; opens at 0, 1, 6, 7 */
    json_limits_t l = lim_depth(4);
    expect_ok(t, doc, &l);
    l = lim_depth(3);
    expect_err(t, doc, &l, JSON_E_DEPTH, 7);
    l = lim_depth(2);
    expect_err(t, doc, &l, JSON_E_DEPTH, 6);
    l = lim_depth(1);
    expect_err(t, doc, &l, JSON_E_DEPTH, 1);
    l = lim_depth(0);
    expect_err(t, doc, &l, JSON_E_DEPTH, 0);
}

static void t_depth_is_open_containers_not_total(ctest_t *t) {
    json_limits_t l = lim_depth(2);
    expect_ok(t, LIT("[[1],[2],[3]]"), &l);
    expect_ok(t, LIT("[[],[],[],[],[]]"), &l);
    expect_ok(t, LIT("{\"a\":{},\"b\":{},\"c\":[]}"), &l);
    expect_ok(t, LIT("[[],[]]"), &l);
    l = lim_depth(3);
    expect_ok(t, LIT("[[[1]],[[1]],[[1]]]"), &l);
    l = lim_depth(2);
    expect_err(t, LIT("[[[1]],[[1]]]"), &l, JSON_E_DEPTH, 2);
    expect_err(t, LIT("[[1],[[2]]]"), &l, JSON_E_DEPTH, 6);
    l = lim_depth(1);
    expect_ok(t, LIT("[1,2,3]"), &l);
    expect_ok(t, LIT("{\"a\":1,\"b\":2}"), &l);
    expect_err(t, LIT("[[1],[2]]"), &l, JSON_E_DEPTH, 1);
    expect_err(t, LIT("[1,[2]]"), &l, JSON_E_DEPTH, 3);
    expect_err(t, LIT("{\"a\":[1]}"), &l, JSON_E_DEPTH, 5);
}

static void t_depth_checked_before_the_rest_is_read(ctest_t *t) {
    json_limits_t l = lim_depth(2);
    expect_err(t, LIT("[[["), &l, JSON_E_DEPTH, 2);
    expect_err(t, LIT("[[[[[[[[[["), &l, JSON_E_DEPTH, 2);
    expect_err(t, LIT("[[[ garbage"), &l, JSON_E_DEPTH, 2);
    expect_err(t, LIT("[[ garbage"), &l, JSON_E_SYNTAX, 3);   /* control: within the limit, the syntax error shows */
    expect_err(t, LIT("{\"a\":{\"a\":{"), &l, JSON_E_DEPTH, 10);
}

static void t_depth_default_is_64(ctest_t *t) {
    size_t n = nest_arrays(64);
    expect_ok(t, (core_str_t){g_text, n}, NULL);
    n = nest_arrays(65);
    expect_err(t, (core_str_t){g_text, n}, NULL, JSON_E_DEPTH, 64);
    n = nest_objects(64);
    expect_ok(t, (core_str_t){g_text, n}, NULL);
    n = nest_objects(65);
    expect_err(t, (core_str_t){g_text, n}, NULL, JSON_E_DEPTH, 320);
    memset(g_text, '[', 5000);
    expect_err(t, (core_str_t){g_text, 5000}, NULL, JSON_E_DEPTH, 64);
}

static void t_depth_zero_allows_scalars_only(ctest_t *t) {
    json_limits_t l = lim_depth(0);
    expect_ok(t, LIT("1"), &l);
    expect_ok(t, LIT(" \"s\" "), &l);
    expect_ok(t, LIT("null"), &l);
    expect_ok(t, LIT("true"), &l);
    expect_err(t, LIT("[]"), &l, JSON_E_DEPTH, 0);
    expect_err(t, LIT("{}"), &l, JSON_E_DEPTH, 0);
    expect_err(t, LIT("  []"), &l, JSON_E_DEPTH, 2);
}

static void t_invalid_limits(ctest_t *t) {
    json_limits_t l = json_limits_default();
    l.max_depth = 65;
    expect_err(t, LIT("1"), &l, JSON_E_LIMIT, 0);
    expect_err(t, LIT("["), &l, JSON_E_LIMIT, 0);
    expect_err(t, LIT(""), &l, JSON_E_LIMIT, 0);
    l.max_depth = SIZE_MAX;
    expect_err(t, LIT("1"), &l, JSON_E_LIMIT, 0);
    l.max_depth = 64;
    expect_ok(t, LIT("1"), &l);   /* control */
    l.max_input = JSON_MAX_INPUT + 1;
    expect_err(t, LIT("1"), &l, JSON_E_LIMIT, 0);
    expect_err(t, LIT("@"), &l, JSON_E_LIMIT, 0);
    l.max_input = SIZE_MAX;
    expect_err(t, LIT("1"), &l, JSON_E_LIMIT, 0);
    l.max_input = JSON_MAX_INPUT;
    expect_ok(t, LIT("1"), &l);   /* control */
}

static void t_input_size_boundaries(ctest_t *t) {
    json_limits_t l = json_limits_default();
    l.max_input = 5;
    expect_ok(t, LIT("[1,2]"), &l);
    expect_ok(t, LIT("  [] "), &l);
    expect_err(t, LIT("[1,2] "), &l, JSON_E_LIMIT, 5);
    expect_err(t, LIT("[1, 2]"), &l, JSON_E_LIMIT, 5);
    expect_err(t, LIT("[1,2]  "), &l, JSON_E_LIMIT, 5);
    l.max_input = 4;
    expect_err(t, LIT("[1,2]"), &l, JSON_E_LIMIT, 4);
    expect_ok(t, LIT("[12]"), &l);
    l.max_input = 1;
    expect_ok(t, LIT("7"), &l);
    expect_err(t, LIT("77"), &l, JSON_E_LIMIT, 1);
    l.max_input = 0;
    expect_err(t, LIT("7"), &l, JSON_E_LIMIT, 0);
    expect_err(t, LIT(""), &l, JSON_E_SYNTAX, 0);   /* nothing exceeds the limit; empty input is a syntax error */
}

static void t_input_size_wins_over_other_errors(ctest_t *t) {
    json_limits_t l = json_limits_default();
    l.max_input = 3;
    expect_err(t, LIT("@@@@@"), &l, JSON_E_LIMIT, 3);
    expect_err(t, LIT("[[[[[["), &l, JSON_E_LIMIT, 3);
    expect_err(t, LIT("\"\xff\xff\xff\""), &l, JSON_E_LIMIT, 3);
    expect_err(t, LIT("[1,]"), &l, JSON_E_LIMIT, 3);
    l.max_depth = 1;
    expect_err(t, LIT("[[[["), &l, JSON_E_LIMIT, 3);
    expect_err(t, LIT("[[["), &l, JSON_E_DEPTH, 1);   /* control: exactly at the size limit, the depth error shows */
    expect_err(t, LIT("@@@"), &l, JSON_E_SYNTAX, 0);
}

static void t_input_size_at_the_hard_cap(ctest_t *t) {
    const json_value_t *root;
    size_t off;
    size_t cap = JSON_MAX_INPUT;
    memset(g_big, ' ', sizeof g_big);
    g_big[cap - 1] = '7';
    CHECK(t, core_arena_init(&g_a, g_arena_big, sizeof g_arena_big));
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){g_big, cap}, NULL, &root, &off), JSON_OK);
    CHECK_EQ_I64(t, json_type(root), JSON_NUMBER);
    CHECK(t, core_arena_init(&g_a, g_arena_big, sizeof g_arena_big));
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){g_big, cap + 1}, NULL, &root, &off), JSON_E_LIMIT);
    CHECK_EQ_U64(t, off, cap);
    CHECK(t, root == NULL);
    memset(g_big, ' ', sizeof g_big);
    g_big[0] = '7';   /* the same size but with the value first: still fine */
    CHECK(t, core_arena_init(&g_a, g_arena_big, sizeof g_arena_big));
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){g_big, cap}, NULL, &root, &off), JSON_OK);
}

static void t_string_limit_boundaries(ctest_t *t) {
    json_limits_t l = json_limits_default();
    l.max_string = 3;
    expect_ok(t, LIT("\"\""), &l);
    expect_ok(t, LIT("\"abc\""), &l);
    expect_err(t, LIT("\"abcd\""), &l, JSON_E_LIMIT, 0);
    expect_err(t, LIT("\"abcde\""), &l, JSON_E_LIMIT, 0);
    expect_err(t, LIT("  \"abcd\""), &l, JSON_E_LIMIT, 2);
    l.max_string = 0;
    expect_ok(t, LIT("\"\""), &l);
    expect_ok(t, LIT("[\"\",\"\"]"), &l);
    expect_err(t, LIT("\"a\""), &l, JSON_E_LIMIT, 0);
    expect_err(t, LIT("[\"\",\"a\"]"), &l, JSON_E_LIMIT, 4);
    expect_ok(t, LIT("[1,true,null,[],{}]"), &l);   /* only strings and keys are limited */
}

static void t_string_limit_counts_decoded_bytes(ctest_t *t) {
    json_limits_t l = json_limits_default();
    l.max_string = 2;
    expect_ok(t, LIT("\"\\u00e9\""), &l);            /* 6 input bytes, 2 decoded */
    expect_ok(t, LIT("\"\xc3\xa9\""), &l);
    expect_ok(t, LIT("\"\\n\\n\""), &l);
    expect_ok(t, LIT("\"\\u0000\\u0000\""), &l);
    expect_err(t, LIT("\"\\n\\n\\n\""), &l, JSON_E_LIMIT, 0);
    expect_err(t, LIT("\"\\u20ac\""), &l, JSON_E_LIMIT, 0);   /* 3 bytes */
    expect_err(t, LIT("\"\xe2\x82\xac\""), &l, JSON_E_LIMIT, 0);
    l.max_string = 1;
    expect_ok(t, LIT("\"\\u0000\""), &l);
    expect_err(t, LIT("\"\\u00e9\""), &l, JSON_E_LIMIT, 0);
    l.max_string = 4;
    expect_ok(t, LIT("\"\\ud83d\\ude00\""), &l);   /* a surrogate pair is 4 bytes */
    expect_err(t, LIT("\"\\ud83d\\ude00a\""), &l, JSON_E_LIMIT, 0);
    l.max_string = 3;
    expect_err(t, LIT("\"\\ud83d\\ude00\""), &l, JSON_E_LIMIT, 0);
}

static void t_string_limit_applies_to_keys_and_each_string(ctest_t *t) {
    json_limits_t l = json_limits_default();
    l.max_string = 3;
    expect_ok(t, LIT("[\"abc\",\"abc\",\"abc\"]"), &l);   /* per string, not in total */
    expect_ok(t, LIT("{\"abc\":\"abc\",\"def\":\"def\"}"), &l);
    expect_err(t, LIT("{\"abcd\":1}"), &l, JSON_E_LIMIT, 1);
    expect_err(t, LIT("{\"ab\":\"abcd\"}"), &l, JSON_E_LIMIT, 6);
    expect_err(t, LIT("[\"abc\",\"abcd\"]"), &l, JSON_E_LIMIT, 7);
    expect_err(t, LIT("{\"a\":1,\"abcd\":2}"), &l, JSON_E_LIMIT, 7);
    expect_err(t, LIT("[[\"a\"],{\"k\":[\"abcd\"]}]"), &l, JSON_E_LIMIT, 13);
}

static void t_string_limit_default_is_1_mib(ctest_t *t) {
    const json_value_t *root;
    size_t off, n = (size_t)1 << 20;
    core_str_t s = {NULL, 0};
    g_big[0] = '"';
    memset(g_big + 1, 'a', n + 1);
    g_big[n + 1] = '"';
    g_big[n + 2] = '"';
    CHECK(t, core_arena_init(&g_a, g_arena_big, sizeof g_arena_big));
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){g_big, n + 2}, NULL, &root, &off), JSON_OK);
    CHECK(t, json_get_str(root, &s));
    CHECK_EQ_U64(t, s.len, n);
    g_big[n + 1] = 'a';
    g_big[n + 2] = '"';
    CHECK(t, core_arena_init(&g_a, g_arena_big, sizeof g_arena_big));
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){g_big, n + 3}, NULL, &root, &off), JSON_E_LIMIT);
    CHECK_EQ_U64(t, off, 0);
    CHECK(t, root == NULL);
}

static void t_element_limit_scalars_and_containers(ctest_t *t) {
    json_limits_t l = json_limits_default();
    l.max_elements = 0;
    expect_err(t, LIT("7"), &l, JSON_E_LIMIT, 0);
    expect_err(t, LIT("  []"), &l, JSON_E_LIMIT, 2);
    l.max_elements = 1;
    expect_ok(t, LIT("7"), &l);
    expect_ok(t, LIT("\"s\""), &l);
    expect_ok(t, LIT("[]"), &l);
    expect_ok(t, LIT("{}"), &l);
    expect_err(t, LIT("[1]"), &l, JSON_E_LIMIT, 1);
    expect_err(t, LIT("[ []]"), &l, JSON_E_LIMIT, 2);
    l.max_elements = 2;
    expect_ok(t, LIT("[1]"), &l);
    expect_ok(t, LIT("{\"a\":1}"), &l);   /* keys are not values */
    expect_ok(t, LIT("{\"a\":[]}"), &l);
    expect_err(t, LIT("{\"a\":[]}"), &(json_limits_t){64, JSON_MAX_INPUT, 1048576, 1}, JSON_E_LIMIT, 5);
    expect_err(t, LIT("[1,2]"), &l, JSON_E_LIMIT, 3);
}

static void t_element_limit_counts_every_value(ctest_t *t) {
    json_limits_t l = json_limits_default();
    l.max_elements = 4;
    expect_ok(t, LIT("[1,2,3]"), &l);
    expect_ok(t, LIT("[[],[],[]]"), &l);
    expect_ok(t, LIT("{\"a\":1,\"b\":2,\"c\":3}"), &l);
    expect_err(t, LIT("[1,2,3,4]"), &l, JSON_E_LIMIT, 7);
    expect_err(t, LIT("[[],[],[],[]]"), &l, JSON_E_LIMIT, 10);
    expect_err(t, LIT("{\"a\":1,\"b\":2,\"c\":3,\"d\":4}"), &l, JSON_E_LIMIT, 23);
    expect_err(t, LIT("[[1,2],[3]]"), &l, JSON_E_LIMIT, 7);
    l.max_elements = 3;
    expect_err(t, LIT("[1,2,3]"), &l, JSON_E_LIMIT, 5);
    expect_ok(t, LIT("[1,2]"), &l);
    l.max_elements = 5;
    expect_ok(t, LIT("[1,2,3,4]"), &l);
    expect_err(t, LIT("[1,2,3,4,5]"), &l, JSON_E_LIMIT, 9);
}

static void t_element_limit_with_generated_arrays(ctest_t *t) {
    size_t n;
    for (n = 1; n <= 12; n += 11) {
        json_limits_t l = json_limits_default();
        size_t i, len = 0;
        l.max_elements = 10;   /* the root array plus up to 9 elements */
        g_text[len++] = '[';
        for (i = 0; i < n; i++) {
            if (i) g_text[len++] = ',';
            g_text[len++] = '0';
        }
        g_text[len++] = ']';
        if (n + 1 <= 10) {
            expect_ok(t, (core_str_t){g_text, len}, &l);
        } else {
            expect_err(t, (core_str_t){g_text, len}, &l, JSON_E_LIMIT, 1 + 2 * 9);
        }
    }
    {
        json_limits_t l = json_limits_default();
        size_t len = 0;
        l.max_elements = 10;
        memcpy(g_text, "[0,0,0,0,0,0,0,0,0]", 19);   /* exactly 10 values */
        len = 19;
        expect_ok(t, (core_str_t){g_text, len}, &l);
        memcpy(g_text, "[0,0,0,0,0,0,0,0,0,0]", 21);
        expect_err(t, (core_str_t){g_text, 21}, &l, JSON_E_LIMIT, 19);
    }
}

/* ---- out of memory ---- */

static json_err_t parse_in_cap(core_str_t in, size_t cap, const json_value_t **root, size_t *off) {
    if (!core_arena_init(&g_a, g_mem, cap)) {
        return JSON_E_ARG;
    }
    return json_parse(&g_a, in, NULL, root, off);
}

/* Finds the smallest capacity (<= 16384) that parses `in`; below it every parse is NOMEM, above it every
 * parse works; the minimum is at least `payload` (the bytes of decoded text that must be stored). */
static void nomem_sweep(ctest_t *t, core_str_t in, size_t payload) {
    const json_value_t *root;
    size_t off, cap, min = SIZE_MAX;
    for (cap = 0; cap <= 16384 && min == SIZE_MAX; cap++) {
        root = SENTINEL;
        off = OFF_MARK;
        if (parse_in_cap(in, cap, &root, &off) == JSON_OK) {
            min = cap;
        } else {
            CHECK_EQ_I64(t, parse_in_cap(in, cap, &root, &off), JSON_E_NOMEM);
            CHECK(t, root == NULL);
            CHECK(t, off <= in.len);
            CHECK_EQ_U64(t, core_arena_used(&g_a), 0);
        }
    }
    CHECK(t, min != SIZE_MAX);
    CHECK(t, min >= payload);
    CHECK(t, min < 4096);
    if (min != SIZE_MAX) {
        CHECK_EQ_I64(t, parse_in_cap(in, min, &root, &off), JSON_OK);
        CHECK(t, core_arena_used(&g_a) <= min);
        CHECK_EQ_I64(t, parse_in_cap(in, min + 1, &root, &off), JSON_OK);
        CHECK_EQ_I64(t, parse_in_cap(in, min + 1000, &root, &off), JSON_OK);
        CHECK_EQ_I64(t, parse_in_cap(in, 16384, &root, &off), JSON_OK);
    }
}

static void t_nomem_sweeps(ctest_t *t) {
    nomem_sweep(t, LIT("1"), 1);
    nomem_sweep(t, LIT("null"), 0);
    nomem_sweep(t, LIT("\"hello\""), 5);
    nomem_sweep(t, LIT("[]"), 0);
    nomem_sweep(t, LIT("{\"abc\":[1,\"xy\"]}"), 6);
    nomem_sweep(t, LIT("[[],[[]],{}]"), 0);
    nomem_sweep(t, LIT("\"\\u00e9\\ud83d\\ude00\""), 6);
}

static void t_nomem_degenerate_arenas(ctest_t *t) {
    const json_value_t *r = SENTINEL;
    size_t off = OFF_MARK;
    core_arena_t zero = {0};
    CHECK(t, core_arena_init(&g_a, NULL, 0));
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("1"), NULL, &r, &off), JSON_E_NOMEM);
    CHECK(t, r == NULL);
    r = SENTINEL;
    CHECK_EQ_I64(t, json_parse(&zero, LIT("null"), NULL, &r, &off), JSON_E_NOMEM);
    CHECK(t, r == NULL);
    CHECK(t, off <= 4);
}

static void t_nomem_restores_arena_and_allows_reuse(ctest_t *t) {
    static char text[8192];
    size_t i, n = 0;
    const json_value_t *r = SENTINEL;
    size_t off = 0, mark;
    unsigned char *pre;
    text[n++] = '[';
    for (i = 0; i < 1000; i++) {
        n += (size_t)snprintf(text + n, sizeof text - n, "%s%zu", i ? "," : "", i);
    }
    text[n++] = ']';
    CHECK(t, core_arena_init(&g_a, g_mem, 4096));
    pre = (unsigned char *)core_arena_alloc(&g_a, 7, 1);
    CHECK(t, pre != NULL);
    mark = core_arena_used(&g_a);
    CHECK_EQ_U64(t, mark, 7);
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){text, n}, NULL, &r, &off), JSON_E_NOMEM);
    CHECK(t, r == NULL);
    CHECK_EQ_U64(t, core_arena_used(&g_a), mark);
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("[1]"), NULL, &r, &off), JSON_OK);
    CHECK_EQ_U64(t, json_len(r), 1);
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){text, n}, NULL, &r, &off), JSON_E_NOMEM);
}

CTEST_SUITE("limits",
    {"depth_arrays_boundaries", t_depth_arrays_boundaries},
    {"depth_objects_boundaries", t_depth_objects_boundaries},
    {"depth_mixed_containers", t_depth_mixed_containers},
    {"depth_is_open_containers_not_total", t_depth_is_open_containers_not_total},
    {"depth_checked_before_the_rest_is_read", t_depth_checked_before_the_rest_is_read},
    {"depth_default_is_64", t_depth_default_is_64},
    {"depth_zero_allows_scalars_only", t_depth_zero_allows_scalars_only},
    {"invalid_limits", t_invalid_limits},
    {"input_size_boundaries", t_input_size_boundaries},
    {"input_size_wins_over_other_errors", t_input_size_wins_over_other_errors},
    {"input_size_at_the_hard_cap", t_input_size_at_the_hard_cap},
    {"string_limit_boundaries", t_string_limit_boundaries},
    {"string_limit_counts_decoded_bytes", t_string_limit_counts_decoded_bytes},
    {"string_limit_applies_to_keys_and_each_string", t_string_limit_applies_to_keys_and_each_string},
    {"string_limit_default_is_1_mib", t_string_limit_default_is_1_mib},
    {"element_limit_scalars_and_containers", t_element_limit_scalars_and_containers},
    {"element_limit_counts_every_value", t_element_limit_counts_every_value},
    {"element_limit_with_generated_arrays", t_element_limit_with_generated_arrays},
    {"nomem_sweeps", t_nomem_sweeps},
    {"nomem_degenerate_arenas", t_nomem_degenerate_arenas},
    {"nomem_restores_arena_and_allows_reuse", t_nomem_restores_arena_and_allows_reuse})
