/* Tests for json/json.h: arena use, independence from the input, argument handling. Public header only. */
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

static bool str_is(core_str_t s, core_str_t want) { return core_str_eq(s, want); }

static void t_dom_independent_of_input(ctest_t *t) {
    static char in[] = "{\"key\":[\"value\",1234567890123,{\"in\":\"ner\"}],\"k2\":\"v\\u00e9\"}";
    core_str_t s, nt;
    const json_value_t *arr;
    const json_value_t *v = ok_parse(t, (core_str_t){in, sizeof in - 1});
    memset(in, 'X', sizeof in - 1);
    arr = json_obj_get(v, LIT("key"));
    CHECK(t, str_is(json_key_at(v, 0), LIT("key")));
    CHECK(t, str_is(json_key_at(v, 1), LIT("k2")));
    CHECK(t, json_get_str(json_at(arr, 0), &s) && str_is(s, LIT("value")));
    CHECK(t, json_number_text(json_at(arr, 1), &nt) && str_is(nt, LIT("1234567890123")));
    CHECK(t, json_get_str(json_obj_get(json_at(arr, 2), LIT("in")), &s) && str_is(s, LIT("ner")));
    CHECK(t, json_get_str(json_obj_get(v, LIT("k2")), &s) && str_is(s, LIT("v\xc3\xa9")));
}

static bool in_arena(const void *p, size_t n) {
    const unsigned char *c = (const unsigned char *)p;
    return n == 0 || (c >= g_mem && c + n <= g_mem + sizeof g_mem);
}

static void t_text_lives_in_the_arena(ctest_t *t) {
    core_str_t s, k, nt;
    const json_value_t *arr;
    const json_value_t *v = ok_parse(t, LIT("{\"kk\":[\"abc\",-12.5e3]}"));
    k = json_key_at(v, 0);
    arr = json_obj_get(v, LIT("kk"));
    CHECK(t, json_get_str(json_at(arr, 0), &s));
    CHECK(t, json_number_text(json_at(arr, 1), &nt));
    CHECK(t, in_arena(k.ptr, k.len) && k.len == 2);
    CHECK(t, in_arena(s.ptr, s.len) && s.len == 3);
    CHECK(t, in_arena(nt.ptr, nt.len) && nt.len == 7);
    CHECK(t, in_arena(v, 1) && in_arena(arr, 1));
    CHECK(t, core_arena_used(&g_a) > 0 && core_arena_used(&g_a) <= core_arena_capacity(&g_a));
}

static void t_arena_use_grows_with_document(ctest_t *t) {
    size_t small, big;
    CHECK(t, ok_parse(t, LIT("[1]")) != NULL);
    small = core_arena_used(&g_a);
    CHECK(t, ok_parse(t, LIT("[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20]")) != NULL);
    big = core_arena_used(&g_a);
    CHECK(t, small > 0);
    CHECK(t, big > small);
}

static void t_two_documents_share_an_arena(ctest_t *t) {
    const json_value_t *r1 = NULL, *r2 = NULL;
    size_t off = 9, used1, pre;
    core_str_t s;
    CHECK(t, core_arena_init(&g_a, g_mem, sizeof g_mem));
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("{\"one\":\"first\"}"), NULL, &r1, &off), JSON_OK);
    used1 = core_arena_used(&g_a);
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("[\"second\"]"), NULL, &r2, &off), JSON_OK);
    CHECK(t, core_arena_used(&g_a) > used1);
    CHECK(t, r1 != r2);
    CHECK(t, json_get_str(json_obj_get(r1, LIT("one")), &s) && str_is(s, LIT("first")));
    CHECK(t, json_get_str(json_at(r2, 0), &s) && str_is(s, LIT("second")));
    pre = core_arena_used(&g_a);
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("[1,"), NULL, &r2, &off), JSON_E_SYNTAX);
    CHECK_EQ_U64(t, core_arena_used(&g_a), pre);
    CHECK(t, json_get_str(json_obj_get(r1, LIT("one")), &s) && str_is(s, LIT("first")));
}

static void t_prior_allocations_untouched(ctest_t *t) {
    const json_value_t *r = NULL;
    size_t off = 1, i;
    unsigned char *blk;
    CHECK(t, core_arena_init(&g_a, g_mem, sizeof g_mem));
    blk = (unsigned char *)core_arena_alloc(&g_a, 13, 1);
    CHECK(t, blk != NULL);
    if (blk == NULL) return;
    memset(blk, 0xA5, 13);
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("{\"a\":[1,2,\"three\"]}"), NULL, &r, &off), JSON_OK);
    CHECK_EQ_U64(t, json_len(json_obj_get(r, LIT("a"))), 3);
    for (i = 0; i < 13; i++) {
        CHECK_EQ_U64(t, blk[i], 0xA5);
    }
    CHECK(t, core_arena_used(&g_a) > 13);
}

static void t_misaligned_arena_base(ctest_t *t) {
    const json_value_t *r = NULL;
    size_t off = 1;
    core_str_t s;
    CHECK(t, core_arena_init(&g_a, g_mem + 1, sizeof g_mem - 1));
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("[\"abc\",{\"k\":7}]"), NULL, &r, &off), JSON_OK);
    CHECK(t, json_get_str(json_at(r, 0), &s) && str_is(s, LIT("abc")));
    CHECK_EQ_U64(t, json_len(json_at(r, 1)), 1);
}

static void t_optional_arguments(ctest_t *t) {
    const json_value_t *r = NULL;
    json_limits_t one = json_limits_default();
    one.max_depth = 1;
    CHECK(t, core_arena_init(&g_a, g_mem, sizeof g_mem));
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("[[1]]"), NULL, &r, NULL), JSON_OK);   /* NULL lim = defaults */
    CHECK(t, r != NULL);
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("[[1]]"), &one, &r, NULL), JSON_E_DEPTH);
    CHECK(t, r == NULL);
}

static void t_arg_errors(ctest_t *t) {
    const json_value_t *r = SENTINEL;
    size_t off = OFF_MARK;
    CHECK(t, core_arena_init(&g_a, g_mem, sizeof g_mem));
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("1"), NULL, NULL, &off), JSON_E_ARG);
    CHECK_EQ_I64(t, json_parse(NULL, LIT("1"), NULL, &r, &off), JSON_E_ARG);
    CHECK(t, r == NULL);
    CHECK_EQ_U64(t, off, 0);
    r = SENTINEL;
    off = OFF_MARK;
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){NULL, 3}, NULL, &r, &off), JSON_E_ARG);
    CHECK(t, r == NULL);
    CHECK_EQ_U64(t, off, 0);
    CHECK_EQ_U64(t, core_arena_used(&g_a), 0);
    /* controls: the same call shapes with valid arguments succeed */
    CHECK_EQ_I64(t, json_parse(&g_a, LIT("1"), NULL, &r, &off), JSON_OK);
    CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){NULL, 0}, NULL, &r, &off), JSON_E_SYNTAX);
    CHECK_EQ_U64(t, off, 0);
    CHECK(t, r == NULL);
}

static void t_success_resets_error_offset(ctest_t *t) {
    const json_value_t *r;
    size_t off;
    CHECK_EQ_I64(t, run(LIT("[1]"), NULL, &r, &off), JSON_OK);
    CHECK_EQ_U64(t, off, 0);
    CHECK(t, r != NULL && r != SENTINEL);
    CHECK_EQ_I64(t, run(LIT("[1"), NULL, &r, &off), JSON_E_SYNTAX);
    CHECK_EQ_U64(t, off, 2);
    CHECK(t, r == NULL);
}

CTEST_SUITE("arena",
    {"dom_independent_of_input", t_dom_independent_of_input},
    {"text_lives_in_the_arena", t_text_lives_in_the_arena},
    {"arena_use_grows_with_document", t_arena_use_grows_with_document},
    {"two_documents_share_an_arena", t_two_documents_share_an_arena},
    {"prior_allocations_untouched", t_prior_allocations_untouched},
    {"misaligned_arena_base", t_misaligned_arena_base},
    {"optional_arguments", t_optional_arguments},
    {"arg_errors", t_arg_errors},
    {"success_resets_error_offset", t_success_resets_error_offset})
