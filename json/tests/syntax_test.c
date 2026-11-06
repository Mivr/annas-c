/* Tests for json/json.h: syntax errors with exact offsets, duplicate keys. Public header only. */
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
    CHECK_EQ_U64(t, core_arena_used(&g_a), 0);
}

static void err_all(ctest_t *t, const char *const *tmpls, size_t n, json_err_t code) {
    size_t i;
    for (i = 0; i < n; i++) {
        err_case(t, tmpls[i], code);
    }
}

static void ok_all(ctest_t *t, const char *const *docs, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        CHECK(t, ok_parse(t, core_str_from_cstr(docs[i])) != NULL);
    }
}

#define ERR_ALL(t, arr, code) err_all((t), (arr), sizeof(arr) / sizeof((arr)[0]), (code))
#define OK_ALL(t, arr) ok_all((t), (arr), sizeof(arr) / sizeof((arr)[0]))

static void t_empty_and_blank_input(ctest_t *t) {
    static const char *const bad[] = {"^", " ^", "  \n\t\r^", "\n^", "\t^", "\r\n^"};
    ERR_ALL(t, bad, JSON_E_SYNTAX);
    {
        const json_value_t *root = SENTINEL;
        size_t off = OFF_MARK;
        CHECK(t, core_arena_init(&g_a, g_mem, sizeof g_mem));
        CHECK_EQ_I64(t, json_parse(&g_a, (core_str_t){"ignored", 0}, NULL, &root, &off), JSON_E_SYNTAX);
        CHECK_EQ_U64(t, off, 0);
        CHECK(t, root == NULL);
    }
}

static void t_literal_errors(ctest_t *t) {
    static const char *const bad[] = {
        "^True", "^TRUE", "^Null", "^NULL", "^False", "^FALSE", "n^il", "^undefined", "^None", "^yes", "t^", "tr^",
        "tru^", "f^", "fa^", "fal^", "fals^", "n^", "nu^", "nul^", "tr^x", "tru^x", "nul^x", "nu^x", "fals^x", "fal^x",
        "nul^L", "tr^U", "tru^ e", "true^e", "null^x", "false^e", "true^1", "true^_", "false ^x", "[tru^]", "[nul^]",
        "[fals^]", "[tru^,1]", "{\"a\":tru^}", "{\"a\":nu^x}", "[t^ ]", "[nul^1]", "null^l", "[true^e]", "[true^x]",
        "[true^false]", "true^true", "null ^null",
    };
    ERR_ALL(t, bad, JSON_E_SYNTAX);
}

static void t_array_structure_errors(ctest_t *t) {
    static const char *const bad[] = {
        "[^", "[1^", "[1,^", "[1,^]", "[^,1]", "[^,]", "[1,^,2]", "[1 ^2]", "[1^}", "[1^:2]", "^]", "[1]^]", "[1]^[2]",
        "[[1]^", "[\"a\"^\"b\"]", "[1,2^", "[1 , ^]", "[ 1 ^", "[[]^", "[[],^]", "[1,[2,^]]", "[1,[2],^]",
        "[1,2,^,]", "[1,^,]", "[[1,2]^[3]]", "[true ^false]", "[\"a\"^:1]", "[{\"a\":1^]", "[1,2^}",
        "[null^null]", "[\"a\" ^\"b\"]", "[]^]", "[[]]^]",
    };
    ERR_ALL(t, bad, JSON_E_SYNTAX);
}

static void t_object_structure_errors(ctest_t *t) {
    static const char *const bad[] = {
        "{^", "{\"a\"^", "{\"a\":^", "{\"a\":1^", "{\"a\":1,^", "{\"a\":1,^}", "{^,}", "{^1:1}", "{^a:1}", "{^'a':1}",
        "{^[]:1}", "{^null:1}", "{^true:1}", "{^{}:1}", "{\"a\"^1}", "{\"a\"^,1}", "{\"a\"^}", "{\"a\":^}",
        "{\"a\":^,\"b\":1}", "{\"a\":^:1}", "{\"a\"^=1}", "{\"a\":1 ^\"b\":2}", "{\"a\":1^]", "{\"a\":1,\"b\"^}",
        "{\"a\":1^;", "^}", "{}^}", "{\"a\":1}^,", "{\"a\":1}^{", "{\"a\":[1,2^}", "[{\"a\":1^]", "{^1}",
        "{\"a\" ^\"b\"}", "{^,\"a\":1}", "{\"a\":1,^,\"b\":2}", "{\"a\":1^:2}", "{\"a\":1,\"b\":^",
        "{\"a\":{^", "{\"a\":{\"b\":1}^", "{ ^", "{ \"a\" ^", "{\"a\" : ^", "{\"a\" ^1}", "{\"a\":1}^}", "{\"a\":[]^]",
    };
    ERR_ALL(t, bad, JSON_E_SYNTAX);
}

static void t_trailing_garbage(ctest_t *t) {
    static const char *const bad[] = {
        "1 ^2", "\"a\"^\"b\"", "null^null", "true ^false", "[]^x", "{} ^x", "1 ^// comment", "[1] ^/* c */",
        "^// c\n1", "[1 ^// c\n]", "{} ^}", "[]^[]", "{}^{}", "\"a\"^b", "\"a\" ^,", "[1] ^2", "{\"a\":1} ^x",
        "null\n\n^0", "true^,", "false^]", "1\n^x", "[]^\x01", "{}^\xc3\xa9",
    };
    static const char *const good[] = {"1 ", "\"a\"\n", "null\t\r\n ", "[] ", "{}\n", " [ ] "};
    ERR_ALL(t, bad, JSON_E_SYNTAX);
    OK_ALL(t, good);
}

static void t_nul_byte_in_input(ctest_t *t) {
    static const char a[] = {'1', '\0'};
    static const char b[] = {'\0'};
    static const char c[] = {'[', '1', ',', '\0', '2', ']'};
    static const char d[] = {'[', '1', ']', '\0'};
    static const char e[] = {'t', 'r', '\0', 'e'};
    static const struct { const char *p; size_t n; size_t off; } cs[] = {
        {a, 2, 1}, {b, 1, 0}, {c, 6, 3}, {d, 4, 3}, {e, 4, 2},
    };
    size_t i;
    for (i = 0; i < sizeof cs / sizeof cs[0]; i++) {
        const json_value_t *root;
        size_t off;
        CHECK_EQ_I64(t, run((core_str_t){cs[i].p, cs[i].n}, NULL, &root, &off), JSON_E_SYNTAX);
        CHECK_EQ_U64(t, off, cs[i].off);
        CHECK(t, root == NULL);
    }
}

static int is_json_ws(int b) { return b == ' ' || b == '\t' || b == '\n' || b == '\r'; }

static void expect_run(ctest_t *t, core_str_t in, json_err_t code, size_t at) {
    const json_value_t *root;
    size_t off;
    CHECK_EQ_I64(t, run(in, NULL, &root, &off), code);
    CHECK_EQ_U64(t, off, code == JSON_OK ? 0 : at);
    CHECK(t, (root != NULL && root != SENTINEL) == (code == JSON_OK));
}

static void t_every_leading_byte(ctest_t *t) {
    int b;
    for (b = 0; b < 256; b++) {
        char in[2];
        in[0] = (char)b;
        in[1] = '1';
        if (is_json_ws(b) || b == '-' || (b >= '1' && b <= '9')) {
            expect_run(t, (core_str_t){in, 2}, JSON_OK, 0);          /* " 1" "-1" "11" */
        } else if (b == '0') {
            expect_run(t, (core_str_t){in, 2}, JSON_E_NUMBER, 1);    /* "01" */
        } else if (b == '"' || b == '[') {
            expect_run(t, (core_str_t){in, 2}, JSON_E_SYNTAX, 2);    /* unterminated */
        } else if (b == '{' || b == 't' || b == 'f' || b == 'n') {
            expect_run(t, (core_str_t){in, 2}, JSON_E_SYNTAX, 1);
        } else {
            expect_run(t, (core_str_t){in, 2}, JSON_E_SYNTAX, 0);
        }
    }
}

static void t_every_trailing_byte(ctest_t *t) {
    int b;
    for (b = 0; b < 256; b++) {
        char in[4];
        const json_value_t *root;
        size_t off;
        json_err_t e;
        in[0] = '"'; in[1] = 'x'; in[2] = '"'; in[3] = (char)b;
        e = run((core_str_t){in, 4}, NULL, &root, &off);
        if (is_json_ws(b)) {
            CHECK_EQ_I64(t, e, JSON_OK);
        } else {
            CHECK_EQ_I64(t, e, JSON_E_SYNTAX);
            CHECK_EQ_U64(t, off, 3);
            CHECK(t, root == NULL);
        }
    }
}

static void t_non_json_whitespace(ctest_t *t) {
    static const char *const bad[] = {
        "^\v" "1", "^\f" "1", "1^\v", "1^\f", "[1,^\v" "2]", "[^\f]", "{\"a\"^\v:1}", "^\x1c" "1", "^\x1f" "1",
        "^\xc2\xa0" "1", "^\xe2\x80\x83" "1", "[1,2]^\x0b", "^\x85" "1", "^\xef\xbb\xbf[]",
    };
    static const char *const good[] = {"\t\n\r [\t1\n,\r2 ]", " { \"a\" : 1 } "};
    ERR_ALL(t, bad, JSON_E_SYNTAX);
    OK_ALL(t, good);
}

static void t_comments_and_extensions_rejected(ctest_t *t) {
    static const char *const bad[] = {
        "^// c\n1", "[1,^// c\n2]", "^/* c */1", "{\"a\":^/*x*/1}", "[1,2,^]", "{\"a\":1,^}", "^#c\n1", "[^undefined]",
        "^NaN", "[^Infinity]", "{^a:1}", "{^'a':1}", "[^'a']", "0^x10", "[0^x10]", "[1^_0]", "^+1", "^.5",
        "[^`a`]", "^@",
    };
    ERR_ALL(t, bad, JSON_E_SYNTAX);
}

static void t_duplicate_keys(ctest_t *t) {
    static const char *const bad[] = {
        "{\"a\":1,^\"a\":2}", "{\"a\":1,\"b\":2,^\"a\":3}", "{\"a\":1,^\"\\u0061\":2}", "{\"\\u0061\":1,^\"a\":2}",
        "{\"\\u0061\":1,^\"\\u0061\":2}", "{\"\":1,^\"\":2}", "{\"a\\u0000\":1,^\"a\\u0000\":2}",
        "{\"\\u00e9\":1,^\"\xc3\xa9\":2}", "{\"\xc3\xa9\":1,^\"\\u00E9\":2}",
        "{\"\\ud83d\\ude00\":1,^\"\xf0\x9f\x98\x80\":2}", "{\"a\\nb\":1,^\"a\\u000ab\":2}",
        "{\"a\":{\"b\":1,\"c\":2},^\"a\":3}", "{\"a\":1,\"b\":{\"x\":1,^\"x\":2}}", "[{\"a\":1,^\"a\":2}]",
        "{\"k\":1,\"l\":2,\"m\":3,\"n\":4,\"o\":5,^\"k\":6}", "{\"a\":1,^\"a\":2,\"a\":3}",
        "{\"a\\\"\":1,^\"a\\\"\":2}", "{\"\\\\\":1,^\"\\u005c\":2}", "{\"a\":1,\"b\":1,\"c\":1,^\"b\":1}",
        "{\"a\":1,\"b\":2,^\"a\":3,\"b\":4}", "{\"x\":[{\"a\":1,^\"a\":1}]}", "{\"/\":1,^\"\\/\":2}",
    };
    static const char *const good[] = {
        "{\"a\":1,\"A\":2}", "{\"a\":1,\"ab\":2}", "{\"a\":1,\"a \":2}", "{\"a\\u0000\":1,\"a\":2}", "{\"a\":{\"a\":1}}",
        "[{\"a\":1},{\"a\":2}]", "{\"a\":1,\"b\":{\"a\":2}}", "{\"\xc3\xa9\":1,\"e\":2}", "{\"\":1,\"a\":2}",
        "{\"a\":{\"a\":{\"a\":{}}}}", "{\"a\":[{\"a\":1}],\"b\":[{\"a\":1}]}", "{\"\\u00e9\":1,\"\\u00e8\":2}",
        "{\"a\\u0000b\":1,\"a\\u0000c\":2}", "{\"a\":1,\"a\\\\\":2}",
    };
    ERR_ALL(t, bad, JSON_E_SYNTAX);
    OK_ALL(t, good);
}

static void t_duplicate_key_in_a_large_object(ctest_t *t) {
    static char in[4096];
    size_t i, base, n = 0;
    const json_value_t *root;
    size_t off;
    in[n++] = '{';
    for (i = 0; i < 100; i++) {
        n += (size_t)snprintf(in + n, sizeof in - n, "%s\"key%zu\":%zu", i ? "," : "", i, i);
    }
    base = n;
    in[n++] = '}';
    CHECK(t, ok_parse(t, (core_str_t){in, n}) != NULL);   /* control: 100 distinct keys */
    for (i = 0; i < 100; i += 33) {
        size_t m = base;
        m += (size_t)snprintf(in + base, sizeof in - base, ",\"key%zu\":0}", i);
        CHECK_EQ_I64(t, run((core_str_t){in, m}, NULL, &root, &off), JSON_E_SYNTAX);
        CHECK_EQ_U64(t, off, base + 1);
        CHECK(t, root == NULL);
    }
    {
        size_t m = base + (size_t)snprintf(in + base, sizeof in - base, ",\"key99\":0}");
        CHECK_EQ_I64(t, run((core_str_t){in, m}, NULL, &root, &off), JSON_E_SYNTAX);   /* last key repeated */
        CHECK_EQ_U64(t, off, base + 1);
        m = base + (size_t)snprintf(in + base, sizeof in - base, ",\"key100\":0}");
        CHECK_EQ_I64(t, run((core_str_t){in, m}, NULL, &root, &off), JSON_OK);   /* control: a new key */
    }
}

CTEST_SUITE("syntax",
    {"empty_and_blank_input", t_empty_and_blank_input},
    {"literal_errors", t_literal_errors},
    {"array_structure_errors", t_array_structure_errors},
    {"object_structure_errors", t_object_structure_errors},
    {"trailing_garbage", t_trailing_garbage},
    {"nul_byte_in_input", t_nul_byte_in_input},
    {"every_leading_byte", t_every_leading_byte},
    {"every_trailing_byte", t_every_trailing_byte},
    {"non_json_whitespace", t_non_json_whitespace},
    {"comments_and_extensions_rejected", t_comments_and_extensions_rejected},
    {"duplicate_keys", t_duplicate_keys},
    {"duplicate_key_in_a_large_object", t_duplicate_key_in_a_large_object})
