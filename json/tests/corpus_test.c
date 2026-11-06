/* Tests for json/json.h: a table-driven corpus of valid and invalid documents. Public header only. */
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


static unsigned char g_mem2[1u << 16];
static core_arena_t g_a2;
static char g_out1[1u << 15];
static char g_out2[1u << 15];

typedef struct {
    const char *in;
    const char *canon;   /* compact serialisation of the parsed DOM */
} valid_t;

static bool write_to(const json_value_t *v, char *dst, size_t cap, core_buf_t *b) {
    return core_buf_init(b, dst, cap) && json_write(b, v);
}

/* Compares two DOMs structurally without recursion (static explicit stack). */
typedef struct {
    const json_value_t *a;
    const json_value_t *b;
    size_t i;
    size_t n;
} pair_t;

static pair_t g_stack[JSON_MAX_DEPTH + 1];

static bool shallow_same(const json_value_t *a, const json_value_t *b) {
    core_str_t x = {NULL, 0}, y = {NULL, 0};
    bool p = false, q = false;
    if (json_type(a) != json_type(b) || json_type(a) == JSON_NONE) {
        return false;
    }
    switch (json_type(a)) {
    case JSON_BOOL: return json_get_bool(a, &p) && json_get_bool(b, &q) && p == q;
    case JSON_STRING: return json_get_str(a, &x) && json_get_str(b, &y) && core_str_eq(x, y);
    case JSON_NUMBER: return json_number_text(a, &x) && json_number_text(b, &y) && core_str_eq(x, y);
    case JSON_ARRAY:
    case JSON_OBJECT: return json_len(a) == json_len(b);
    default: return true;
    }
}

static bool same_dom(const json_value_t *a, const json_value_t *b) {
    size_t sp = 0;
    if (!shallow_same(a, b)) {
        return false;
    }
    g_stack[sp++] = (pair_t){a, b, 0, json_len(a)};
    while (sp > 0) {
        pair_t *f = &g_stack[sp - 1];
        const json_value_t *ca, *cb;
        if (f->i == f->n) {
            sp--;
            continue;
        }
        ca = json_at(f->a, f->i);
        cb = json_at(f->b, f->i);
        if (json_type(f->a) == JSON_OBJECT && !core_str_eq(json_key_at(f->a, f->i), json_key_at(f->b, f->i))) {
            return false;
        }
        f->i++;
        if (!shallow_same(ca, cb)) {
            return false;
        }
        if (json_len(ca) > 0) {
            if (sp >= sizeof g_stack / sizeof g_stack[0]) {
                return false;
            }
            g_stack[sp++] = (pair_t){ca, cb, 0, json_len(ca)};
        }
    }
    return true;
}

static void check_valid(ctest_t *t, const valid_t *c) {
    core_buf_t b1, b2;
    const json_value_t *v1 = ok_parse(t, core_str_from_cstr(c->in));
    const json_value_t *v2 = NULL;
    size_t off;
    core_str_t w1;
    CHECK(t, write_to(v1, g_out1, sizeof g_out1, &b1));
    CHECK_EQ_STR(t, core_buf_cstr(&b1), c->canon);
    /* the canonical form parses to the same DOM and writes to itself */
    w1 = core_buf_str(&b1);
    CHECK(t, core_arena_init(&g_a2, g_mem2, sizeof g_mem2));
    CHECK_EQ_I64(t, json_parse(&g_a2, w1, NULL, &v2, &off), JSON_OK);
    CHECK(t, write_to(v2, g_out2, sizeof g_out2, &b2));
    CHECK_EQ_STR(t, core_buf_cstr(&b2), c->canon);
    CHECK(t, same_dom(v1, v2));
}

static void run_valid(ctest_t *t, const valid_t *c, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        check_valid(t, &c[i]);
    }
}

static void t_rfc8259_examples(ctest_t *t) {
    static const valid_t c[] = {
        {"{\n  \"Image\": {\n      \"Width\":  800,\n      \"Height\": 600,\n      \"Title\":  \"View from 15th Floor\",\n"
         "      \"Thumbnail\": {\n          \"Url\":    \"http://www.example.com/image/481989943\",\n"
         "          \"Height\": 125,\n          \"Width\":  100\n      },\n      \"Animated\" : false,\n"
         "      \"IDs\": [116, 943, 234, 38793]\n    }\n}\n",
         "{\"Image\":{\"Width\":800,\"Height\":600,\"Title\":\"View from 15th Floor\",\"Thumbnail\":{\"Url\":"
         "\"http://www.example.com/image/481989943\",\"Height\":125,\"Width\":100},\"Animated\":false,"
         "\"IDs\":[116,943,234,38793]}}"},
        {"[\n  {\n     \"precision\": \"zip\",\n     \"Latitude\":  37.7668,\n     \"Longitude\": -122.3959,\n"
         "     \"Address\":   \"\",\n     \"City\":      \"SAN FRANCISCO\",\n     \"State\":     \"CA\",\n"
         "     \"Zip\":       \"94107\",\n     \"Country\":   \"US\"\n  },\n  {\n     \"precision\": \"zip\",\n"
         "     \"Latitude\":  37.371991,\n     \"Longitude\": -122.026020,\n     \"Address\":   \"\",\n"
         "     \"City\":      \"SUNNYVALE\",\n     \"State\":     \"CA\",\n     \"Zip\":       \"94085\",\n"
         "     \"Country\":   \"US\"\n  }\n]",
         "[{\"precision\":\"zip\",\"Latitude\":37.7668,\"Longitude\":-122.3959,\"Address\":\"\",\"City\":"
         "\"SAN FRANCISCO\",\"State\":\"CA\",\"Zip\":\"94107\",\"Country\":\"US\"},{\"precision\":\"zip\","
         "\"Latitude\":37.371991,\"Longitude\":-122.026020,\"Address\":\"\",\"City\":\"SUNNYVALE\",\"State\":"
         "\"CA\",\"Zip\":\"94085\",\"Country\":\"US\"}]"},
        {"\"Hello world!\"", "\"Hello world!\""},
        {"42", "42"},
        {"true", "true"},
        {"\"\\uD834\\uDD1E\"", "\"\xf0\x9d\x84\x9e\""},   /* the RFC's G clef */
        {"\"\\u005C\"", "\"\\\\\""},
        {"\"\\u0022\"", "\"\\\"\""},
        {"[]", "[]"},
        {"{}", "{}"},
    };
    run_valid(t, c, sizeof c / sizeof c[0]);
}

static void t_valid_corpus_canonical_forms(ctest_t *t) {
    static const valid_t c[] = {
        {"[[]]", "[[]]"}, {"[{}]", "[{}]"}, {"{\"a\":{}}", "{\"a\":{}}"}, {"{\"a\":[]}", "{\"a\":[]}"},
        {"\"\\u0041\"", "\"A\""}, {"\"\\/\"", "\"/\""}, {"\"\\n\"", "\"\\u000a\""}, {"\"\\t\\r\\b\\f\"", "\"\\u0009\\u000d\\u0008\\u000c\""},
        {"\"\\u00e9\"", "\"\xc3\xa9\""}, {"\"\xc3\xa9\"", "\"\xc3\xa9\""}, {"\"\\ud83d\\ude00\"", "\"\xf0\x9f\x98\x80\""},
        {"\"\\u0000\"", "\"\\u0000\""}, {"\"\\u007f\"", "\"\x7f\""}, {"\"\\u2028\"", "\"\xe2\x80\xa8\""},
        {"\"\\\\\\\"\"", "\"\\\\\\\"\""}, {"\"\\u001f\"", "\"\\u001f\""}, {"\"\\u001F\"", "\"\\u001f\""},
        {"1E+2", "1E+2"}, {"-0", "-0"}, {"0.10", "0.10"}, {"[1e0,1E0,1e-0,0.0,-0.0,0e0]", "[1e0,1E0,1e-0,0.0,-0.0,0e0]"},
        {"123456789012345678901234567890", "123456789012345678901234567890"}, {"-1.5e-10", "-1.5e-10"},
        {" [ 1 , 2 , 3 ] ", "[1,2,3]"}, {" { \"a\" : [ 1 , 2 ] } ", "{\"a\":[1,2]}"},
        {"{\"b\":1,\"a\":2}", "{\"b\":1,\"a\":2}"}, {"{\"a\":null,\"b\":true,\"c\":false}", "{\"a\":null,\"b\":true,\"c\":false}"},
        {"{\"\xc3\xa9\":1}", "{\"\xc3\xa9\":1}"}, {"{\"\\u00e9\":1}", "{\"\xc3\xa9\":1}"}, {"{\"a\\nb\":1}", "{\"a\\u000ab\":1}"},
        {"{\"\":1}", "{\"\":1}"}, {"[\"\\ud800\\udc00\"]", "[\"\xf0\x90\x80\x80\"]"}, {"[[[[1]]]]", "[[[[1]]]]"},
        {"{\"a\":[{\"b\":[{\"c\":1}]}]}", "{\"a\":[{\"b\":[{\"c\":1}]}]}"}, {"[null,null]", "[null,null]"},
        {"[true,false,true]", "[true,false,true]"}, {"[\"\",\"\"]", "[\"\",\"\"]"}, {"{\"a\":\"\"}", "{\"a\":\"\"}"},
        {"[0,-1,10,-10,100]", "[0,-1,10,-10,100]"}, {"\n\t[\r\n1\r\n]\n", "[1]"},
        {"[[1,2],[3,4],[[5],[6]]]", "[[1,2],[3,4],[[5],[6]]]"}, {"{\"a\":{\"b\":{\"c\":{\"d\":{}}}}}", "{\"a\":{\"b\":{\"c\":{\"d\":{}}}}}"},
        {"[\"a\\\"b\",\"c\\\\d\"]", "[\"a\\\"b\",\"c\\\\d\"]"}, {"{\"k\":\"v\",\"k2\":[\"v2\"]}", "{\"k\":\"v\",\"k2\":[\"v2\"]}"},
        {"\"\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82\"", "\"\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82\""},
    };
    run_valid(t, c, sizeof c / sizeof c[0]);
}

static void err_case(ctest_t *t, const char *tmpl, json_err_t code) {
    char buf[256];
    size_t at;
    size_t n = unmark(tmpl, buf, &at);
    const json_value_t *root;
    size_t off;
    json_err_t e = run((core_str_t){buf, n}, NULL, &root, &off);
    CHECK_EQ_I64(t, e, code);
    CHECK_EQ_U64(t, off, at);
    CHECK(t, root == NULL);
}

static void err_all(ctest_t *t, const char *const *tmpls, size_t n, json_err_t code) {
    size_t i;
    for (i = 0; i < n; i++) {
        err_case(t, tmpls[i], code);
    }
}

static void t_invalid_corpus_syntax(ctest_t *t) {
    static const char *const bad[] = {
        "[\"a\"^", "[1,^]", "{\"a\":1,^}", "[^.5]", "[^+1]", "[^NaN]", "[^Infinity]", "[^'a']", "[\"\\^x\"]",
        "[\"\\u12^\"]", "[\"^\t\"]", "[\"a^\nb\"]", "{\"a\" ^1}", "{^a:1}", "{\"a\":1 ^\"b\":2}", "[1 ^2]", "[^,1]",
        "[1,^,2]", "^]", "^}", "[^}", "{^]", "[[]^", "{\"a\":[^}", "nul^", "tru^", "fals^y", "[tru^]", "\"abc^", "\"\\^",
        "^'", "^", " ^", "[\"a\"]^x", "[1] ^[2]", "{} ^{}", "1 ^2", "{\"a\":1,^\"a\":2}", "[^undefined]", "{\"a\"^}",
        "[1,2,3^", "{\"a\":{\"b\":1}^", "{\"a\":[1,2^}", "[\"a\",^]", "[]^]", "{\"a\":^}", "[1^;2]", "^\xef\xbb\xbf[]",
        "[1,2 ^", "{\"a\":1^:2}", "[\"\\u^zzzz\"]",
    };
    err_all(t, bad, sizeof bad / sizeof bad[0], JSON_E_SYNTAX);
}

static void t_invalid_corpus_numbers(ctest_t *t) {
    static const char *const bad[] = {
        "[0^1]", "[1.^]", "[-^]", "[1e^]", "-^-1", "-^+1", "1e+^", "1.^e1", "0e^", "0^1.5", "[0^0]", "[-^x]", "[1e+^]",
        "-^", "[0.^]", "1.^", "[-^.5]", "[1E^]", "{\"a\":-^}",
    };
    err_all(t, bad, sizeof bad / sizeof bad[0], JSON_E_NUMBER);
}

static void t_invalid_corpus_utf8(ctest_t *t) {
    static const char *const bad[] = {
        "[\"^\xff\"]", "[\"^\xc0\xaf\"]", "[\"^\\ud800\"]", "[\"^\\udc00\"]", "[\"^\\ud800\\u0041\"]",
        "[\"^\xed\xa0\x80\"]", "[\"^\xe2\x82\"]", "[\"^\xf4\x90\x80\x80\"]", "{\"^\xff\":1}", "[\"a^\xc3\"]",
        "[\"^\\udbff\"]", "[\"^\\udfff\"]", "[\"\\ud83d\\ude00^\\udc00\"]",
    };
    err_all(t, bad, sizeof bad / sizeof bad[0], JSON_E_UTF8);
}

static void t_valid_and_invalid_are_distinguished(ctest_t *t) {
    /* minimal pairs: one byte turns a valid document into an invalid one */
    static const struct { const char *good; const char *bad; json_err_t code; } pairs[] = {
        {"[1,2]", "[1,^]", JSON_E_SYNTAX},
        {"{\"a\":1}", "{\"a\":1^", JSON_E_SYNTAX},
        {"\"a\"", "\"a^", JSON_E_SYNTAX},
        {"[0]", "[0^0]", JSON_E_NUMBER},
        {"[0.5]", "[0.^]", JSON_E_NUMBER},
        {"[1e5]", "[1e^]", JSON_E_NUMBER},
        {"[true]", "[tru^]", JSON_E_SYNTAX},
        {"\"\\u0041\"", "\"\\u004^\"", JSON_E_SYNTAX},
        {"\"\\ud83d\\ude00\"", "\"^\\ud83d\"", JSON_E_UTF8},
        {"\"\xc3\xa9\"", "\"^\xc3\"", JSON_E_UTF8},
        {"{\"a\":1,\"b\":2}", "{\"a\":1,^\"a\":2}", JSON_E_SYNTAX},
        {"[[]]", "[[]^", JSON_E_SYNTAX},
    };
    size_t i;
    for (i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        CHECK(t, ok_parse(t, core_str_from_cstr(pairs[i].good)) != NULL);
        err_case(t, pairs[i].bad, pairs[i].code);
    }
}

CTEST_SUITE("corpus",
    {"rfc8259_examples", t_rfc8259_examples},
    {"valid_corpus_canonical_forms", t_valid_corpus_canonical_forms},
    {"invalid_corpus_syntax", t_invalid_corpus_syntax},
    {"invalid_corpus_numbers", t_invalid_corpus_numbers},
    {"invalid_corpus_utf8", t_invalid_corpus_utf8},
    {"valid_and_invalid_are_distinguished", t_valid_and_invalid_are_distinguished})
