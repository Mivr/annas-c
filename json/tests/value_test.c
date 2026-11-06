/* Tests for json/json.h: DOM construction and access. Public header only. */
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

static void t_limits_default_values(ctest_t *t) {
    json_limits_t l = json_limits_default();
    CHECK_EQ_U64(t, l.max_depth, 64);
    CHECK_EQ_U64(t, l.max_input, 8u * 1024u * 1024u);
    CHECK_EQ_U64(t, l.max_string, 1048576u);
    CHECK_EQ_U64(t, l.max_elements, 1048576u);
    CHECK_EQ_U64(t, JSON_MAX_DEPTH, 64);
    CHECK_EQ_U64(t, JSON_MAX_INPUT, 8388608u);
}

static void t_error_codes_distinct(ctest_t *t) {
    CHECK_EQ_I64(t, JSON_OK, 0);
    CHECK_EQ_I64(t, JSON_NONE, 0);
    CHECK(t, JSON_E_SYNTAX != JSON_OK && JSON_E_DEPTH != JSON_OK && JSON_E_LIMIT != JSON_OK);
    CHECK(t, JSON_E_NOMEM != JSON_OK && JSON_E_UTF8 != JSON_OK && JSON_E_NUMBER != JSON_OK);
    CHECK(t, JSON_E_ARG != JSON_OK);
    CHECK(t, JSON_E_SYNTAX != JSON_E_DEPTH && JSON_E_SYNTAX != JSON_E_LIMIT && JSON_E_SYNTAX != JSON_E_NOMEM);
    CHECK(t, JSON_E_SYNTAX != JSON_E_UTF8 && JSON_E_SYNTAX != JSON_E_NUMBER && JSON_E_SYNTAX != JSON_E_ARG);
    CHECK(t, JSON_E_DEPTH != JSON_E_LIMIT && JSON_E_DEPTH != JSON_E_NOMEM && JSON_E_DEPTH != JSON_E_UTF8);
    CHECK(t, JSON_E_DEPTH != JSON_E_NUMBER && JSON_E_DEPTH != JSON_E_ARG);
    CHECK(t, JSON_E_LIMIT != JSON_E_NOMEM && JSON_E_LIMIT != JSON_E_UTF8 && JSON_E_LIMIT != JSON_E_NUMBER);
    CHECK(t, JSON_E_LIMIT != JSON_E_ARG && JSON_E_NOMEM != JSON_E_UTF8 && JSON_E_NOMEM != JSON_E_NUMBER);
    CHECK(t, JSON_E_NOMEM != JSON_E_ARG && JSON_E_UTF8 != JSON_E_NUMBER && JSON_E_UTF8 != JSON_E_ARG);
    CHECK(t, JSON_E_NUMBER != JSON_E_ARG);
    /* and the parser really reports each of them (inputs chosen to be unambiguous) */
    {
        const json_value_t *r;
        size_t off;
        json_limits_t l = json_limits_default();
        CHECK_EQ_I64(t, run(LIT("[1"), NULL, &r, &off), JSON_E_SYNTAX);
        CHECK_EQ_I64(t, run(LIT("-"), NULL, &r, &off), JSON_E_NUMBER);
        CHECK_EQ_I64(t, run(LIT("\"\xff\""), NULL, &r, &off), JSON_E_UTF8);
        l.max_depth = 0;
        CHECK_EQ_I64(t, run(LIT("[]"), &l, &r, &off), JSON_E_DEPTH);
        l = json_limits_default();
        l.max_input = 1;
        CHECK_EQ_I64(t, run(LIT("[]"), &l, &r, &off), JSON_E_LIMIT);
        CHECK_EQ_I64(t, json_parse(&g_a, LIT("[]"), NULL, NULL, &off), JSON_E_ARG);
        CHECK(t, core_arena_init(&g_a, g_mem, 0));
        CHECK_EQ_I64(t, json_parse(&g_a, LIT("[]"), NULL, &r, &off), JSON_E_NOMEM);
        CHECK_EQ_I64(t, run(LIT("[]"), NULL, &r, &off), JSON_OK);
    }
}

static void t_literals(ctest_t *t) {
    bool b = false;
    const json_value_t *v = ok_parse(t, LIT("null"));
    CHECK_EQ_I64(t, json_type(v), JSON_NULL);
    CHECK_EQ_U64(t, json_len(v), 0);
    v = ok_parse(t, LIT("true"));
    CHECK_EQ_I64(t, json_type(v), JSON_BOOL);
    CHECK(t, json_get_bool(v, &b));
    CHECK(t, b);
    v = ok_parse(t, LIT("false"));
    CHECK_EQ_I64(t, json_type(v), JSON_BOOL);
    b = true;
    CHECK(t, json_get_bool(v, &b));
    CHECK(t, !b);
}

static void t_root_scalars_of_every_type(ctest_t *t) {
    core_str_t s = {NULL, 0};
    const json_value_t *v = ok_parse(t, LIT("123"));
    CHECK_EQ_I64(t, json_type(v), JSON_NUMBER);
    CHECK(t, json_number_text(v, &s) && str_is(s, LIT("123")));
    v = ok_parse(t, LIT("\"hi\""));
    CHECK_EQ_I64(t, json_type(v), JSON_STRING);
    CHECK(t, json_get_str(v, &s) && str_is(s, LIT("hi")));
    v = ok_parse(t, LIT("\"\""));
    CHECK_EQ_I64(t, json_type(v), JSON_STRING);
    s.len = 99;
    CHECK(t, json_get_str(v, &s));
    CHECK_EQ_U64(t, s.len, 0);
    v = ok_parse(t, LIT("[]"));
    CHECK_EQ_I64(t, json_type(v), JSON_ARRAY);
    v = ok_parse(t, LIT("{}"));
    CHECK_EQ_I64(t, json_type(v), JSON_OBJECT);
}

static void t_whitespace_around_root(ctest_t *t) {
    core_str_t s;
    const json_value_t *v = ok_parse(t, LIT(" \t\n\r1\r\n\t "));
    CHECK(t, json_number_text(v, &s) && str_is(s, LIT("1")));
    v = ok_parse(t, LIT("  [ ]  "));
    CHECK_EQ_I64(t, json_type(v), JSON_ARRAY);
    v = ok_parse(t, LIT("\n{\n}\n"));
    CHECK_EQ_I64(t, json_type(v), JSON_OBJECT);
    v = ok_parse(t, LIT("\ttrue"));
    CHECK_EQ_I64(t, json_type(v), JSON_BOOL);
    v = ok_parse(t, LIT("null\r"));
    CHECK_EQ_I64(t, json_type(v), JSON_NULL);
}

static void t_whitespace_inside_containers(ctest_t *t) {
    int64_t n = 0;
    const json_value_t *v = ok_parse(t, LIT("[ 1 ,\t2\n,\r3 ]"));
    CHECK_EQ_U64(t, json_len(v), 3);
    CHECK(t, json_get_i64(json_at(v, 2), &n));
    CHECK_EQ_I64(t, n, 3);
    v = ok_parse(t, LIT("{ \"a\" \n:\t 7 , \"b\"\r:\r8 }"));
    CHECK_EQ_U64(t, json_len(v), 2);
    CHECK(t, json_get_i64(json_obj_get(v, LIT("b")), &n));
    CHECK_EQ_I64(t, n, 8);
    v = ok_parse(t, LIT("[\n]"));
    CHECK_EQ_U64(t, json_len(v), 0);
}

static void t_empty_containers(ctest_t *t) {
    const json_value_t *v = ok_parse(t, LIT("[]"));
    CHECK_EQ_U64(t, json_len(v), 0);
    CHECK(t, json_at(v, 0) == NULL);
    v = ok_parse(t, LIT("{}"));
    CHECK_EQ_U64(t, json_len(v), 0);
    CHECK(t, json_at(v, 0) == NULL);
    CHECK_EQ_U64(t, json_key_at(v, 0).len, 0);
    CHECK(t, json_obj_get(v, LIT("a")) == NULL);
    CHECK(t, json_obj_get(v, LIT("")) == NULL);
}

static void t_array_elements(ctest_t *t) {
    static const json_type_t want[6] = {JSON_NUMBER, JSON_STRING, JSON_BOOL, JSON_NULL, JSON_ARRAY, JSON_OBJECT};
    size_t i;
    const json_value_t *v = ok_parse(t, LIT("[1,\"a\",true,null,[],{}]"));
    CHECK_EQ_I64(t, json_type(v), JSON_ARRAY);
    CHECK_EQ_U64(t, json_len(v), 6);
    for (i = 0; i < 6; i++) {
        CHECK_EQ_I64(t, json_type(json_at(v, i)), want[i]);
    }
    CHECK(t, json_at(v, 6) == NULL);
    CHECK(t, json_at(v, 7) == NULL);
    CHECK(t, json_at(v, SIZE_MAX) == NULL);
    CHECK(t, json_at(v, 0) != json_at(v, 1));
}

static void t_single_element_array(ctest_t *t) {
    int64_t n = 0;
    const json_value_t *v = ok_parse(t, LIT("[5]"));
    CHECK_EQ_U64(t, json_len(v), 1);
    CHECK(t, json_get_i64(json_at(v, 0), &n));
    CHECK_EQ_I64(t, n, 5);
    CHECK(t, json_at(v, 1) == NULL);
}

static void t_object_members_in_source_order(ctest_t *t) {
    int64_t n = 0;
    const json_value_t *v = ok_parse(t, LIT("{\"z\":1,\"a\":2,\"m\":3}"));
    CHECK_EQ_U64(t, json_len(v), 3);
    CHECK(t, str_is(json_key_at(v, 0), LIT("z")));
    CHECK(t, str_is(json_key_at(v, 1), LIT("a")));
    CHECK(t, str_is(json_key_at(v, 2), LIT("m")));
    CHECK_EQ_U64(t, json_key_at(v, 3).len, 0);
    CHECK(t, json_get_i64(json_at(v, 0), &n) && n == 1);
    CHECK(t, json_get_i64(json_at(v, 1), &n) && n == 2);
    CHECK(t, json_get_i64(json_at(v, 2), &n) && n == 3);
    CHECK(t, json_at(v, 3) == NULL);
    CHECK(t, json_at(v, 1) == json_obj_get(v, LIT("a")));
}

static void t_object_lookup(ctest_t *t) {
    int64_t n = 0;
    const json_value_t *v = ok_parse(t, LIT("{\"a\":1,\"ab\":2,\"abc\":3,\"A\":4,\"\":5,\" a\":6}"));
    CHECK(t, json_get_i64(json_obj_get(v, LIT("a")), &n) && n == 1);
    CHECK(t, json_get_i64(json_obj_get(v, LIT("ab")), &n) && n == 2);
    CHECK(t, json_get_i64(json_obj_get(v, LIT("abc")), &n) && n == 3);
    CHECK(t, json_get_i64(json_obj_get(v, LIT("A")), &n) && n == 4);
    CHECK(t, json_get_i64(json_obj_get(v, LIT("")), &n) && n == 5);
    CHECK(t, json_get_i64(json_obj_get(v, (core_str_t){NULL, 0}), &n) && n == 5);
    CHECK(t, json_get_i64(json_obj_get(v, LIT(" a")), &n) && n == 6);
    CHECK(t, json_obj_get(v, LIT("abcd")) == NULL);
    CHECK(t, json_obj_get(v, LIT("b")) == NULL);
    CHECK(t, json_obj_get(v, LIT("a ")) == NULL);
    CHECK(t, json_obj_get(v, LIT("aB")) == NULL);
    CHECK(t, json_obj_get(v, (core_str_t){"abcdef", 3}) == json_obj_get(v, LIT("abc")));
}

static void t_object_keys_with_nul(ctest_t *t) {
    int64_t n = 0;
    const json_value_t *v = ok_parse(t, LIT("{\"a\\u0000b\":1,\"a\":2,\"a\\u0000\":3}"));
    CHECK(t, json_get_i64(json_obj_get(v, (core_str_t){"a\0b", 3}), &n) && n == 1);
    CHECK(t, json_get_i64(json_obj_get(v, (core_str_t){"a", 1}), &n) && n == 2);
    CHECK(t, json_get_i64(json_obj_get(v, (core_str_t){"a\0", 2}), &n) && n == 3);
    CHECK(t, json_obj_get(v, (core_str_t){"a\0c", 3}) == NULL);
    CHECK_EQ_U64(t, json_key_at(v, 0).len, 3);
    CHECK_EQ_U64(t, json_key_at(v, 2).len, 2);
}

static void t_nested_navigation(ctest_t *t) {
    core_str_t s;
    bool b = false;
    const json_value_t *v = ok_parse(t, LIT("{\"a\":{\"b\":[1,{\"c\":null},\"x\"],\"d\":true},\"e\":[]}"));
    const json_value_t *a = json_obj_get(v, LIT("a"));
    const json_value_t *arr = json_obj_get(a, LIT("b"));
    CHECK_EQ_I64(t, json_type(a), JSON_OBJECT);
    CHECK_EQ_U64(t, json_len(a), 2);
    CHECK_EQ_U64(t, json_len(arr), 3);
    CHECK_EQ_I64(t, json_type(json_obj_get(json_at(arr, 1), LIT("c"))), JSON_NULL);
    CHECK(t, json_get_str(json_at(arr, 2), &s) && str_is(s, LIT("x")));
    CHECK(t, json_get_bool(json_obj_get(a, LIT("d")), &b) && b);
    CHECK_EQ_U64(t, json_len(json_obj_get(v, LIT("e"))), 0);
    CHECK(t, json_obj_get(v, LIT("b")) == NULL);
    CHECK(t, json_obj_get(v, LIT("c")) == NULL);
}

static void t_siblings_are_independent(ctest_t *t) {
    const json_value_t *v = ok_parse(t, LIT("[[1],[2,3],[],[4,5,6]]"));
    CHECK_EQ_U64(t, json_len(v), 4);
    CHECK_EQ_U64(t, json_len(json_at(v, 0)), 1);
    CHECK_EQ_U64(t, json_len(json_at(v, 1)), 2);
    CHECK_EQ_U64(t, json_len(json_at(v, 2)), 0);
    CHECK_EQ_U64(t, json_len(json_at(v, 3)), 3);
}

static void t_wrong_type_accessors(ctest_t *t) {
    static const char *const docs[6] = {"null", "true", "1", "\"s\"", "[1]", "{\"k\":1}"};
    size_t i;
    for (i = 0; i < 6; i++) {
        const json_value_t *v = ok_parse(t, core_str_from_cstr(docs[i]));
        bool b = true, want_b = (i == 1);
        core_str_t s = {"zz", 2}, nt = {"zz", 2};
        int64_t n = 77;
        uint64_t u = 88;
        bool container = (i >= 4);
        CHECK_EQ_U64(t, json_len(v), container ? 1u : 0u);
        CHECK(t, (json_at(v, 0) != NULL) == container);
        CHECK_EQ_U64(t, json_key_at(v, 0).len, i == 5 ? 1u : 0u);
        CHECK(t, json_obj_get(v, LIT("k")) == (i == 5 ? json_at(v, 0) : NULL));
        CHECK(t, json_get_bool(v, &b) == want_b);
        CHECK(t, i == 1 || b);
        CHECK(t, json_get_str(v, &s) == (i == 3));
        CHECK_EQ_U64(t, s.len, i == 3 ? 1u : 2u);
        CHECK(t, json_number_text(v, &nt) == (i == 2));
        CHECK_EQ_U64(t, nt.len, i == 2 ? 1u : 2u);
        CHECK(t, json_get_i64(v, &n) == (i == 2));
        CHECK_EQ_I64(t, n, i == 2 ? 1 : 77);
        CHECK(t, json_get_u64(v, &u) == (i == 2));
        CHECK_EQ_U64(t, u, i == 2 ? 1u : 88u);
    }
}

static void t_null_handles(ctest_t *t) {
    bool b = true;
    core_str_t s = {"zz", 2};
    int64_t n = 5;
    uint64_t u = 6;
    CHECK(t, ok_parse(t, LIT("[1]")) != NULL);   /* control: the parser works at all */
    CHECK_EQ_I64(t, json_type(NULL), JSON_NONE);
    CHECK_EQ_U64(t, json_len(NULL), 0);
    CHECK(t, json_at(NULL, 0) == NULL);
    CHECK_EQ_U64(t, json_key_at(NULL, 0).len, 0);
    CHECK(t, json_obj_get(NULL, LIT("a")) == NULL);
    CHECK(t, !json_get_bool(NULL, &b));
    CHECK(t, b);
    CHECK(t, !json_get_str(NULL, &s));
    CHECK_EQ_U64(t, s.len, 2);
    CHECK(t, !json_number_text(NULL, &s));
    CHECK(t, !json_get_i64(NULL, &n));
    CHECK_EQ_I64(t, n, 5);
    CHECK(t, !json_get_u64(NULL, &u));
    CHECK_EQ_U64(t, u, 6);
}

static void t_null_out_parameters(ctest_t *t) {
    const json_value_t *v = ok_parse(t, LIT("[true,\"s\",12]"));
    CHECK(t, !json_get_bool(json_at(v, 0), NULL));
    CHECK(t, !json_get_str(json_at(v, 1), NULL));
    CHECK(t, !json_number_text(json_at(v, 2), NULL));
    CHECK(t, !json_get_i64(json_at(v, 2), NULL));
    CHECK(t, !json_get_u64(json_at(v, 2), NULL));
}

static void t_many_elements_and_members(ctest_t *t) {
    static char text[8192];
    size_t i, n = 0;
    int64_t x;
    const json_value_t *v;
    text[n++] = '[';
    for (i = 0; i < 1000; i++) {
        n += (size_t)snprintf(text + n, sizeof text - n, "%s%zu", i ? "," : "", i * 7);
    }
    text[n++] = ']';
    v = ok_parse(t, (core_str_t){text, n});
    CHECK_EQ_U64(t, json_len(v), 1000);
    for (i = 0; i < 1000; i++) {
        CHECK(t, json_get_i64(json_at(v, i), &x) && x == (int64_t)(i * 7));
    }
    n = 0;
    text[n++] = '{';
    for (i = 0; i < 200; i++) {
        n += (size_t)snprintf(text + n, sizeof text - n, "%s\"key%zu\":%zu", i ? "," : "", i, i);
    }
    text[n++] = '}';
    v = ok_parse(t, (core_str_t){text, n});
    CHECK_EQ_U64(t, json_len(v), 200);
    for (i = 0; i < 200; i++) {
        char k[16];
        int kl = snprintf(k, sizeof k, "key%zu", i);
        CHECK(t, json_get_i64(json_obj_get(v, (core_str_t){k, (size_t)kl}), &x) && x == (int64_t)i);
        CHECK(t, str_is(json_key_at(v, i), (core_str_t){k, (size_t)kl}));
    }
}

CTEST_SUITE("value",
    {"limits_default_values", t_limits_default_values},
    {"error_codes_distinct", t_error_codes_distinct},
    {"literals", t_literals},
    {"root_scalars_of_every_type", t_root_scalars_of_every_type},
    {"whitespace_around_root", t_whitespace_around_root},
    {"whitespace_inside_containers", t_whitespace_inside_containers},
    {"empty_containers", t_empty_containers},
    {"array_elements", t_array_elements},
    {"single_element_array", t_single_element_array},
    {"object_members_in_source_order", t_object_members_in_source_order},
    {"object_lookup", t_object_lookup},
    {"object_keys_with_nul", t_object_keys_with_nul},
    {"nested_navigation", t_nested_navigation},
    {"siblings_are_independent", t_siblings_are_independent},
    {"wrong_type_accessors", t_wrong_type_accessors},
    {"null_handles", t_null_handles},
    {"null_out_parameters", t_null_out_parameters},
    {"many_elements_and_members", t_many_elements_and_members})
