/* Tests for json/json.h: json_write and writer/parser round trips. Public header only. */
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

static char g_o1[1u << 15];
static char g_o2[1u << 15];
static const char k_doc[] = "{\"a\":[1,\"x\\n\",null],\"\xc3\xa9\":true}";
static const char k_canon[] = "{\"a\":[1,\"x\\u000a\",null],\"\xc3\xa9\":true}";

static void t_write_exact_output(ctest_t *t) {
    core_buf_t b;
    const json_value_t *v = ok_parse(t, core_str_from_cstr(k_doc));
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, json_write(&b, v));
    CHECK_EQ_STR(t, core_buf_cstr(&b), k_canon);
    CHECK_EQ_U64(t, core_buf_len(&b), sizeof k_canon - 1);
    CHECK(t, !core_buf_overflow(&b));
}

static void t_write_overflow_boundary(ctest_t *t) {
    const json_value_t *v = ok_parse(t, core_str_from_cstr(k_doc));
    size_t len = sizeof k_canon - 1, cap;
    for (cap = 0; cap <= len + 1; cap++) {
        core_buf_t b;
        bool ok;
        CHECK(t, core_buf_init(&b, g_o1, cap));
        ok = json_write(&b, v);
        CHECK_EQ_I64(t, ok, cap == len + 1);
        CHECK_EQ_I64(t, core_buf_overflow(&b), cap != len + 1);
        if (ok) {
            CHECK_EQ_STR(t, core_buf_cstr(&b), k_canon);
        }
    }
}

static void t_write_arguments(ctest_t *t) {
    core_buf_t b;
    const json_value_t *v = ok_parse(t, LIT("[1]"));
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, !json_write(&b, NULL));
    CHECK(t, !json_write(NULL, v));
    CHECK_EQ_U64(t, core_buf_len(&b), 0);
    CHECK(t, json_write(&b, v));   /* control */
    CHECK_EQ_STR(t, core_buf_cstr(&b), "[1]");
    core_buf_put(&b, NULL, 1);     /* force the sticky overflow flag */
    CHECK(t, core_buf_overflow(&b));
    CHECK(t, !json_write(&b, v));
    CHECK_EQ_STR(t, core_buf_cstr(&b), "[1]");
}

static void t_write_appends_after_existing_content(ctest_t *t) {
    core_buf_t b;
    const json_value_t *v = ok_parse(t, LIT("{\"a\":1}"));
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    core_buf_put(&b, "pre:", 4);
    CHECK(t, json_write(&b, v));
    CHECK(t, json_write(&b, v));
    CHECK_EQ_STR(t, core_buf_cstr(&b), "pre:{\"a\":1}{\"a\":1}");
}

static void t_write_subtrees(ctest_t *t) {
    core_buf_t b;
    const json_value_t *v = ok_parse(t, LIT("{\"x\":[1,{\"y\":\"z\"},[]],\"w\":7}"));
    const json_value_t *arr = json_obj_get(v, LIT("x"));
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, json_write(&b, arr));
    CHECK_EQ_STR(t, core_buf_cstr(&b), "[1,{\"y\":\"z\"},[]]");
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, json_write(&b, json_at(arr, 1)));
    CHECK_EQ_STR(t, core_buf_cstr(&b), "{\"y\":\"z\"}");
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, json_write(&b, json_at(arr, 0)));   /* the first of several siblings: no separator */
    CHECK_EQ_STR(t, core_buf_cstr(&b), "1");
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, json_write(&b, json_at(arr, 2)));   /* the last sibling */
    CHECK_EQ_STR(t, core_buf_cstr(&b), "[]");
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, json_write(&b, json_obj_get(v, LIT("w"))));
    CHECK_EQ_STR(t, core_buf_cstr(&b), "7");
}

static void t_write_deep_nesting(ctest_t *t) {
    static char in[400];
    core_buf_t b;
    size_t i;
    const json_value_t *v;
    for (i = 0; i < 64; i++) {
        in[i] = '[';
        in[64 + i] = ']';
    }
    v = ok_parse(t, (core_str_t){in, 128});
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, json_write(&b, v));
    CHECK_EQ_MEM(t, core_buf_str(&b).ptr, core_buf_len(&b), in, 128);
    for (i = 0; i < 64; i++) {
        memcpy(in + 5 * i, "{\"a\":", 5);
    }
    in[320] = '1';
    memset(in + 321, '}', 64);
    v = ok_parse(t, (core_str_t){in, 385});
    CHECK(t, core_buf_init(&b, g_o2, sizeof g_o2));
    CHECK(t, json_write(&b, v));
    CHECK_EQ_MEM(t, core_buf_str(&b).ptr, core_buf_len(&b), in, 385);
}

static void t_write_wide_documents(ctest_t *t) {
    static char in[8192];
    size_t i, n = 0;
    core_buf_t b;
    const json_value_t *v;
    in[n++] = '[';
    for (i = 0; i < 700; i++) {
        n += (size_t)snprintf(in + n, sizeof in - n, "%s%zu", i ? "," : "", i * 3);
    }
    in[n++] = ']';
    v = ok_parse(t, (core_str_t){in, n});
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, json_write(&b, v));
    CHECK_EQ_MEM(t, core_buf_str(&b).ptr, core_buf_len(&b), in, n);
}

/* ---- writer output parses back to the same data ---- */

static void t_writer_strings_survive_a_parse(ctest_t *t) {
    char all[128];
    core_buf_t b;
    json_writer_t w;
    const json_value_t *v;
    core_str_t s = {NULL, 0};
    size_t i;
    for (i = 0; i < 128; i++) {
        all[i] = (char)i;
    }
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, json_w_init(&w, &b));
    json_w_begin_object(&w);
    json_w_key(&w, (core_str_t){all, 128});
    json_w_string(&w, (core_str_t){all, 128});
    json_w_key(&w, LIT("u"));
    json_w_string(&w, LIT("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\xef\xbf\xbf\xf4\x8f\xbf\xbf"));
    json_w_end_object(&w);
    CHECK(t, json_w_done(&w));
    v = ok_parse(t, core_buf_str(&b));
    CHECK_EQ_U64(t, json_len(v), 2);
    CHECK(t, str_is(json_key_at(v, 0), (core_str_t){all, 128}));
    CHECK(t, json_get_str(json_at(v, 0), &s) && str_is(s, (core_str_t){all, 128}));
    CHECK(t, json_get_str(json_obj_get(v, LIT("u")), &s));
    CHECK(t, str_is(s, LIT("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\xef\xbf\xbf\xf4\x8f\xbf\xbf")));
}

static void t_writer_numbers_survive_a_parse(ctest_t *t) {
    static const int64_t iv[] = {0, 1, -1, 9, 10, INT32_MAX, INT32_MIN, INT64_MAX, INT64_MIN, INT64_MIN + 1, INT64_MAX - 1};
    static const uint64_t uv[] = {0, 1, 10, UINT64_C(9223372036854775808), UINT64_MAX, UINT64_MAX - 1};
    core_buf_t b;
    json_writer_t w;
    const json_value_t *v;
    size_t i;
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, json_w_init(&w, &b));
    json_w_begin_array(&w);
    for (i = 0; i < sizeof iv / sizeof iv[0]; i++) {
        json_w_i64(&w, iv[i]);
    }
    for (i = 0; i < sizeof uv / sizeof uv[0]; i++) {
        json_w_u64(&w, uv[i]);
    }
    json_w_end_array(&w);
    CHECK(t, json_w_done(&w));
    v = ok_parse(t, core_buf_str(&b));
    CHECK_EQ_U64(t, json_len(v), sizeof iv / sizeof iv[0] + sizeof uv / sizeof uv[0]);
    for (i = 0; i < sizeof iv / sizeof iv[0]; i++) {
        int64_t x = 12345;
        CHECK(t, json_get_i64(json_at(v, i), &x));
        CHECK_EQ_I64(t, x, iv[i]);
    }
    for (i = 0; i < sizeof uv / sizeof uv[0]; i++) {
        uint64_t x = 12345;
        CHECK(t, json_get_u64(json_at(v, sizeof iv / sizeof iv[0] + i), &x));
        CHECK_EQ_U64(t, x, uv[i]);
    }
}

static void t_writer_document_equals_dom_write(ctest_t *t) {
    core_buf_t b1, b2;
    json_writer_t w;
    const json_value_t *v;
    size_t i;
    CHECK(t, core_buf_init(&b1, g_o1, sizeof g_o1));
    CHECK(t, json_w_init(&w, &b1));
    json_w_begin_object(&w);
    for (i = 0; i < 20; i++) {
        char key[8];
        int kl = snprintf(key, sizeof key, "k%zu", i);
        json_w_key(&w, (core_str_t){key, (size_t)kl});
        if (i % 4 == 0) {
            json_w_begin_array(&w);
            json_w_i64(&w, -(int64_t)i);
            json_w_null(&w);
            json_w_bool(&w, i % 8 == 0);
            json_w_string(&w, LIT("a\tb"));
            json_w_end_array(&w);
        } else if (i % 4 == 1) {
            json_w_begin_object(&w);
            json_w_end_object(&w);
        } else if (i % 4 == 2) {
            json_w_string(&w, LIT("q\"\\"));
        } else {
            json_w_u64(&w, UINT64_MAX - i);
        }
    }
    json_w_end_object(&w);
    CHECK(t, json_w_done(&w));
    v = ok_parse(t, core_buf_str(&b1));
    CHECK_EQ_U64(t, json_len(v), 20);
    CHECK(t, core_buf_init(&b2, g_o2, sizeof g_o2));
    CHECK(t, json_write(&b2, v));
    CHECK_EQ_U64(t, core_buf_len(&b2), core_buf_len(&b1));
    CHECK_EQ_MEM(t, core_buf_str(&b1).ptr, core_buf_len(&b1), core_buf_str(&b2).ptr, core_buf_len(&b2));
}

static void t_writer_deep_document_parses(ctest_t *t) {
    core_buf_t b;
    json_writer_t w;
    const json_value_t *v;
    size_t i, d = 0;
    CHECK(t, core_buf_init(&b, g_o1, sizeof g_o1));
    CHECK(t, json_w_init(&w, &b));
    for (i = 0; i < 64; i++) {
        json_w_begin_array(&w);
    }
    json_w_null(&w);
    for (i = 0; i < 64; i++) {
        json_w_end_array(&w);
    }
    CHECK(t, json_w_done(&w));
    v = ok_parse(t, core_buf_str(&b));
    while (json_type(v) == JSON_ARRAY) {
        v = json_at(v, 0);
        d++;
    }
    CHECK_EQ_U64(t, d, 64);
    CHECK_EQ_I64(t, json_type(v), JSON_NULL);
}

CTEST_SUITE("roundtrip",
    {"write_exact_output", t_write_exact_output},
    {"write_overflow_boundary", t_write_overflow_boundary},
    {"write_arguments", t_write_arguments},
    {"write_appends_after_existing_content", t_write_appends_after_existing_content},
    {"write_subtrees", t_write_subtrees},
    {"write_deep_nesting", t_write_deep_nesting},
    {"write_wide_documents", t_write_wide_documents},
    {"writer_strings_survive_a_parse", t_writer_strings_survive_a_parse},
    {"writer_numbers_survive_a_parse", t_writer_numbers_survive_a_parse},
    {"writer_document_equals_dom_write", t_writer_document_equals_dom_write},
    {"writer_deep_document_parses", t_writer_deep_document_parses})
