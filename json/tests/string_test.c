/* Tests for json/json.h: string decoding. Public header only. */
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

typedef struct {
    const char *in;
    size_t in_len;
    const char *want;
    size_t want_len;
} str_case_t;

#define E(in, want) {(in), sizeof(in) - 1, (want), sizeof(want) - 1}

/* Parses the whole JSON text and expects one string with exactly the wanted bytes. */
static void expect_string(ctest_t *t, const str_case_t *c) {
    core_str_t s = {NULL, 0};
    const json_value_t *v = ok_parse(t, (core_str_t){c->in, c->in_len});
    CHECK_EQ_I64(t, json_type(v), JSON_STRING);
    CHECK(t, json_get_str(v, &s));
    CHECK_EQ_U64(t, s.len, c->want_len);
    if (s.len == c->want_len) {
        CHECK_EQ_MEM(t, s.ptr, s.len, c->want, c->want_len);
    }
}

static void run_cases(ctest_t *t, const str_case_t *cases, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        expect_string(t, &cases[i]);
    }
}

static void t_plain_strings(ctest_t *t) {
    static const str_case_t c[] = {
        E("\"\"", ""),
        E("\"a\"", "a"),
        E("\"abc\"", "abc"),
        E("\"hello world\"", "hello world"),
        E("\" \"", " "),
        E("\"  x  \"", "  x  "),
        E("\"/\"", "/"),
        E("\"{}[]:,\"", "{}[]:,"),
        E("\"\x7f\"", "\x7f"),
    };
    run_cases(t, c, sizeof c / sizeof c[0]);
}

static void t_simple_escapes(ctest_t *t) {
    static const str_case_t c[] = {
        E("\"\\\"\"", "\""),
        E("\"\\\\\"", "\\"),
        E("\"\\/\"", "/"),
        E("\"\\b\"", "\b"),
        E("\"\\f\"", "\f"),
        E("\"\\n\"", "\n"),
        E("\"\\r\"", "\r"),
        E("\"\\t\"", "\t"),
        E("\"a\\nb\"", "a\nb"),
        E("\"\\n\\n\"", "\n\n"),
        E("\"\\\\n\"", "\\n"),
        E("\"\\\\\\\"\"", "\\\""),
        E("\"x\\\"y\\\\z\\/w\"", "x\"y\\z/w"),
        E("\"\\\"\\\"\"", "\"\""),
    };
    run_cases(t, c, sizeof c / sizeof c[0]);
}

static void t_unicode_escapes_by_encoded_length(ctest_t *t) {
    static const str_case_t c[] = {
        E("\"\\u0041\"", "A"),
        E("\"\\u0000\"", "\0"),
        E("\"\\u001f\"", "\x1f"),
        E("\"\\u0020\"", " "),
        E("\"\\u0022\"", "\""),
        E("\"\\u005c\"", "\\"),
        E("\"\\u005C\"", "\\"),
        E("\"\\u007e\"", "~"),
        E("\"\\u007f\"", "\x7f"),
        E("\"\\u0080\"", "\xc2\x80"),
        E("\"\\u00e9\"", "\xc3\xa9"),
        E("\"\\u00E9\"", "\xc3\xa9"),
        E("\"\\u00ff\"", "\xc3\xbf"),
        E("\"\\u0100\"", "\xc4\x80"),
        E("\"\\u07ff\"", "\xdf\xbf"),
        E("\"\\u0800\"", "\xe0\xa0\x80"),
        E("\"\\u12aB\"", "\xe1\x8a\xab"),
        E("\"\\u20ac\"", "\xe2\x82\xac"),
        E("\"\\ud000\"", "\xed\x80\x80"),
        E("\"\\ud7ff\"", "\xed\x9f\xbf"),
        E("\"\\uD7FF\"", "\xed\x9f\xbf"),
        E("\"\\ue000\"", "\xee\x80\x80"),
        E("\"\\uE000\"", "\xee\x80\x80"),
        E("\"\\ufffd\"", "\xef\xbf\xbd"),
        E("\"\\ufffe\"", "\xef\xbf\xbe"),
        E("\"\\uffff\"", "\xef\xbf\xbf"),
        E("\"\\uFFFF\"", "\xef\xbf\xbf"),
        E("\"a\\u00e9b\"", "a\xc3\xa9" "b"),
        E("\"\\u0041\\u0042\"", "AB"),
        E("\"\\u0000\\u0000\"", "\0\0"),
        E("\"a\\u0000b\"", "a\0b"),
    };
    run_cases(t, c, sizeof c / sizeof c[0]);
}

static void t_surrogate_pairs(ctest_t *t) {
    static const str_case_t c[] = {
        E("\"\\ud800\\udc00\"", "\xf0\x90\x80\x80"),
        E("\"\\ud800\\udfff\"", "\xf0\x90\x8f\xbf"),
        E("\"\\udbff\\udc00\"", "\xf4\x8f\xb0\x80"),
        E("\"\\udbff\\udfff\"", "\xf4\x8f\xbf\xbf"),
        E("\"\\ud83d\\ude00\"", "\xf0\x9f\x98\x80"),
        E("\"\\uD83D\\uDE00\"", "\xf0\x9f\x98\x80"),
        E("\"\\uD834\\uDD1E\"", "\xf0\x9d\x84\x9e"),
        E("\"a\\ud83d\\ude00b\"", "a\xf0\x9f\x98\x80" "b"),
        E("\"\\ud83d\\ude00\\ud83d\\ude00\"", "\xf0\x9f\x98\x80\xf0\x9f\x98\x80"),
        E("\"\\ud800\\udc00\\u0041\"", "\xf0\x90\x80\x80" "A"),
    };
    run_cases(t, c, sizeof c / sizeof c[0]);
}

static void t_hex_digit_positions(ctest_t *t) {
    static const char hexd[] = "0123456789abcdefABCDEF";
    size_t i;
    for (i = 0; i < sizeof hexd - 1; i++) {
        unsigned nib = (unsigned)(i < 16 ? i : i - 6);
        char in[16];
        core_str_t s;
        const json_value_t *v;
        /* low digit: value 0x00..0x0f is a single byte */
        int n = snprintf(in, sizeof in, "\"\\u000%c\"", hexd[i]);
        v = ok_parse(t, (core_str_t){in, (size_t)n});
        CHECK(t, json_get_str(v, &s) && s.len == 1 && (unsigned char)s.ptr[0] == nib);
        /* third digit: 0x10 * nib, 0x00..0xf0; two bytes from 0x80 up */
        n = snprintf(in, sizeof in, "\"\\u00%c0\"", hexd[i]);
        v = ok_parse(t, (core_str_t){in, (size_t)n});
        CHECK(t, json_get_str(v, &s));
        if (nib < 8) {
            CHECK(t, s.len == 1 && (unsigned char)s.ptr[0] == nib * 16u);
        } else {
            CHECK(t, s.len == 2 && (unsigned char)s.ptr[0] == (0xC0u | ((nib * 16u) >> 6)));
            CHECK(t, s.len == 2 && (unsigned char)s.ptr[1] == (0x80u | ((nib * 16u) & 0x3fu)));
        }
        /* second digit: 0x0N00 is two bytes (N >= 8 is still < 0x800 only when N < 8) */
        n = snprintf(in, sizeof in, "\"\\u0%c00\"", hexd[i]);
        v = ok_parse(t, (core_str_t){in, (size_t)n});
        CHECK(t, json_get_str(v, &s));
        if (nib == 0) {
            CHECK(t, s.len == 1 && s.ptr[0] == 0);
        } else if (nib < 8) {
            CHECK(t, s.len == 2 && (unsigned char)s.ptr[0] == (0xC0u | (nib << 2)) && s.ptr[1] == (char)0x80);
        } else {
            CHECK(t, s.len == 3);
        }
        /* first digit: 0xN000, three bytes unless it is a surrogate range lead (d is exercised elsewhere) */
        if (hexd[i] != 'd' && hexd[i] != 'D') {
            n = snprintf(in, sizeof in, "\"\\u%c000\"", hexd[i]);
            v = ok_parse(t, (core_str_t){in, (size_t)n});
            CHECK(t, json_get_str(v, &s));
            CHECK(t, nib == 0 ? (s.len == 1 && s.ptr[0] == 0) : (s.len == 3 && (unsigned char)s.ptr[0] == (0xE0u | nib)));
        }
    }
}

static void t_raw_utf8_passthrough(ctest_t *t) {
    static const str_case_t c[] = {
        E("\"\xc2\x80\"", "\xc2\x80"),
        E("\"\xc3\xa9\"", "\xc3\xa9"),
        E("\"\xdf\xbf\"", "\xdf\xbf"),
        E("\"\xe0\xa0\x80\"", "\xe0\xa0\x80"),
        E("\"\xe2\x82\xac\"", "\xe2\x82\xac"),
        E("\"\xed\x9f\xbf\"", "\xed\x9f\xbf"),
        E("\"\xee\x80\x80\"", "\xee\x80\x80"),
        E("\"\xef\xbf\xbf\"", "\xef\xbf\xbf"),
        E("\"\xf0\x90\x80\x80\"", "\xf0\x90\x80\x80"),
        E("\"\xf0\x9f\x98\x80\"", "\xf0\x9f\x98\x80"),
        E("\"\xf4\x8f\xbf\xbf\"", "\xf4\x8f\xbf\xbf"),
        E("\"a\xc3\xa9" "b\xf0\x9f\x98\x80" "c\"", "a\xc3\xa9" "b\xf0\x9f\x98\x80" "c"),
        E("\"\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82\"", "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82"),
    };
    run_cases(t, c, sizeof c / sizeof c[0]);
}

static void t_every_printable_ascii_raw(ctest_t *t) {
    int b;
    for (b = 0x20; b <= 0x7f; b++) {
        char in[5];
        core_str_t s = {NULL, 0};
        const json_value_t *v;
        if (b == '"' || b == '\\') {
            continue;
        }
        in[0] = '[';
        in[1] = '"';
        in[2] = (char)b;
        in[3] = '"';
        in[4] = ']';
        v = ok_parse(t, (core_str_t){in, 5});
        CHECK(t, json_get_str(json_at(v, 0), &s));
        CHECK(t, s.len == 1 && (unsigned char)s.ptr[0] == (unsigned)b);
    }
}

static void t_escapes_in_keys(ctest_t *t) {
    const json_value_t *v = ok_parse(t, LIT("{\"a\\nb\":1,\"\\u00e9\":2,\"\\ud83d\\ude00\":3}"));
    CHECK(t, str_is(json_key_at(v, 0), LIT("a\nb")));
    CHECK(t, str_is(json_key_at(v, 1), LIT("\xc3\xa9")));
    CHECK(t, str_is(json_key_at(v, 2), LIT("\xf0\x9f\x98\x80")));
    CHECK(t, json_obj_get(v, LIT("a\nb")) != NULL);
    CHECK(t, json_obj_get(v, LIT("\xc3\xa9")) == json_at(v, 1));
    CHECK(t, json_obj_get(v, LIT("a\\nb")) == NULL);
}

static void t_long_string(ctest_t *t) {
    static char in[20002];
    static char want[20000];
    size_t i;
    core_str_t s = {NULL, 0};
    const json_value_t *v;
    for (i = 0; i < sizeof want; i++) {
        want[i] = (char)('a' + (i % 26));
    }
    in[0] = '"';
    memcpy(in + 1, want, sizeof want);
    in[sizeof in - 1] = '"';
    v = ok_parse(t, (core_str_t){in, sizeof in});
    CHECK(t, json_get_str(v, &s));
    CHECK_EQ_U64(t, s.len, sizeof want);
    CHECK(t, s.len == sizeof want && memcmp(s.ptr, want, s.len) == 0);
}

CTEST_SUITE("string",
    {"plain_strings", t_plain_strings},
    {"simple_escapes", t_simple_escapes},
    {"unicode_escapes_by_encoded_length", t_unicode_escapes_by_encoded_length},
    {"surrogate_pairs", t_surrogate_pairs},
    {"hex_digit_positions", t_hex_digit_positions},
    {"raw_utf8_passthrough", t_raw_utf8_passthrough},
    {"every_printable_ascii_raw", t_every_printable_ascii_raw},
    {"escapes_in_keys", t_escapes_in_keys},
    {"long_string", t_long_string})
