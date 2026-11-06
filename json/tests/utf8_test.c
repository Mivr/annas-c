/* Tests for json/json.h: UTF-8, escape and string errors with exact offsets. Public header only. */
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


/* ---- errors ---- */

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

static void t_invalid_utf8_in_strings(ctest_t *t) {
    static const char *const bad[] = {
        "\"^\xc0\x80\"", "\"^\xc1\xbf\"", "\"^\xe0\x80\x80\"", "\"^\xe0\x9f\xbf\"", "\"^\xf0\x80\x80\x80\"",
        "\"^\xf0\x8f\xbf\xbf\"", "\"^\xed\xa0\x80\"", "\"^\xed\xbf\xbf\"", "\"^\xed\xa0\x80\xed\xb0\x80\"",
        "\"^\xf4\x90\x80\x80\"", "\"^\xf5\x80\x80\x80\"", "\"^\xf8\x88\x80\x80\x80\"", "\"^\xff\"", "\"^\xfe\"",
        "\"^\x80\"", "\"^\xbf\"", "\"^\xc3\"", "\"^\xe2\x82\"", "\"^\xf0\x9f\x98\"",
        "\"^\xc3\x41\"", "\"^\xe2\x82\x41\"", "\"^\xe2\x41\x82\"", "\"^\xf0\x9f\x41\x80\"", "\"^\xf0\x41\x80\x80\"",
        "\"^\xc3\xc3\xa9\"", "\"^\xe2\x82\xe2\x82\xac\"",
        "\"ab^\xc3\"", "\"ab^\xff\"", "\"\xc3\xa9^\xa9\"", "\"\xc3\xa9\xc3\xa9^\x80\"",
        "\"a\\n^\xc0\"", "\"\\u00e9^\xc0\"", "{\"^\xff\":1}", "{\"k\":\"^\xff\"}", "[1,\"^\xc3\"]",
        "{\"ok\":1,\"a^\xe2\x82\":2}",
    };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        err_case(t, bad[i], JSON_E_UTF8);
    }
}

static void t_truncated_utf8_at_eof(ctest_t *t) {
    err_case(t, "\"^\xc3", JSON_E_UTF8);
    err_case(t, "\"x^\xe2\x82", JSON_E_UTF8);
    err_case(t, "\"^\xf0\x9f\x98", JSON_E_UTF8);
    /* controls: the same bytes completed are fine, and a missing quote alone is a plain syntax error */
    CHECK(t, ok_parse(t, LIT("\"\xc3\xa9\"")) != NULL);
    err_case(t, "\"\xc3\xa9^", JSON_E_SYNTAX);
}

static void t_every_continuation_or_invalid_lead_byte(ctest_t *t) {
    int b;
    for (b = 0x80; b <= 0xff; b++) {
        char in[3];
        const json_value_t *root;
        size_t off;
        in[0] = '"';
        in[1] = (char)b;
        in[2] = '"';
        /* A lone 0x80..0xff byte before the quote is never valid UTF-8, whether it is a continuation
         * byte, an invalid lead (c0 c1 f5..ff) or a valid lead that is truncated by the quote. */
        CHECK_EQ_I64(t, run((core_str_t){in, 3}, NULL, &root, &off), JSON_E_UTF8);
        CHECK_EQ_U64(t, off, 1);
        CHECK(t, root == NULL);
    }
}

/* `"` lead b tail... `"`: valid exactly when b is in [lo, hi]; invalid is JSON_E_UTF8 at the lead (offset 1). */
static void second_byte_case(ctest_t *t, unsigned lead, unsigned lo, unsigned hi, size_t cont) {
    unsigned b;
    for (b = 0; b <= 0xff; b++) {
        char in[8];
        const json_value_t *root;
        size_t off, n = 0, k;
        in[n++] = '"';
        in[n++] = (char)lead;
        in[n++] = (char)b;
        for (k = 0; k < cont; k++) {
            in[n++] = (char)0x80;
        }
        in[n++] = '"';
        if (b >= lo && b <= hi) {
            CHECK_EQ_I64(t, run((core_str_t){in, n}, NULL, &root, &off), JSON_OK);
        } else {
            CHECK_EQ_I64(t, run((core_str_t){in, n}, NULL, &root, &off), JSON_E_UTF8);
            CHECK_EQ_U64(t, off, 1);
        }
    }
}

static void t_second_byte_ranges(ctest_t *t) {
    second_byte_case(t, 0xc2, 0x80, 0xbf, 0);
    second_byte_case(t, 0xdf, 0x80, 0xbf, 0);
    second_byte_case(t, 0xe0, 0xa0, 0xbf, 1);
    second_byte_case(t, 0xe1, 0x80, 0xbf, 1);
    second_byte_case(t, 0xec, 0x80, 0xbf, 1);
    second_byte_case(t, 0xed, 0x80, 0x9f, 1);
    second_byte_case(t, 0xee, 0x80, 0xbf, 1);
    second_byte_case(t, 0xef, 0x80, 0xbf, 1);
    second_byte_case(t, 0xf0, 0x90, 0xbf, 2);
    second_byte_case(t, 0xf1, 0x80, 0xbf, 2);
    second_byte_case(t, 0xf3, 0x80, 0xbf, 2);
    second_byte_case(t, 0xf4, 0x80, 0x8f, 2);
}

static void t_invalid_bytes_outside_strings_are_syntax(ctest_t *t) {
    err_case(t, "^\xff", JSON_E_SYNTAX);
    err_case(t, "^\xef\xbb\xbf" "1", JSON_E_SYNTAX);   /* BOM */
    err_case(t, "[^\xc3\xa9]", JSON_E_SYNTAX);
    err_case(t, "1 ^\xff", JSON_E_SYNTAX);
    err_case(t, "{^\xc3\xa9\":1}", JSON_E_SYNTAX);
    err_case(t, "^\xc2\xa0" "1", JSON_E_SYNTAX);   /* NBSP is not whitespace */
    err_case(t, "1^\xc2\xa0", JSON_E_SYNTAX);
}

static void t_lone_surrogate_escapes(ctest_t *t) {
    static const char *const bad[] = {
        "\"^\\ud800\"", "\"^\\udbff\"", "\"^\\udc00\"", "\"^\\udfff\"", "\"^\\uD800\"", "\"^\\uDFFF\"",
        "\"a^\\ud800b\"", "\"a^\\udc00b\"", "\"^\\ud800abc\"", "\"^\\ud800\\ud800\"", "\"^\\ud800\\udbff\"",
        "\"^\\ud800\\u0041\"", "\"^\\ud800\\ue000\"", "\"^\\ud800\\ud7ff\"", "\"^\\udbff\\u0000\"",
        "\"^\\ud800\\n\"", "\"^\\ud800\\\\\"", "\"^\\ud800\\\"\"", "\"^\\udc00\\ud800\"", "\"^\\udc00\\udc00\"",
        "\"^\\udfff\\udfff\"", "\"\\ud83d\\ude00^\\ud83d\"", "\"\\ud83d\\ude00^\\ude00\"",
        "\"\\u0041^\\udc00\"", "{\"^\\ud800\":1}", "[\"x\",\"^\\udc00\"]", "\"^\\ud800\\\\ude00\"",
    };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        err_case(t, bad[i], JSON_E_UTF8);
    }
    /* positive controls for the edges of the surrogate range */
    CHECK(t, ok_parse(t, LIT("\"\\ud7ff\"")) != NULL);
    CHECK(t, ok_parse(t, LIT("\"\\ue000\"")) != NULL);
    CHECK(t, ok_parse(t, LIT("\"\\ud800\\udc00\"")) != NULL);
    CHECK(t, ok_parse(t, LIT("\"\\udbff\\udfff\"")) != NULL);
}

static int is_escape_letter(int b) {
    return b == '"' || b == '\\' || b == '/' || b == 'b' || b == 'f' || b == 'n' || b == 'r' || b == 't';
}

static void t_bad_escape_letters(ctest_t *t) {
    static const char *const bad[] = {
        "\"\\^x\"", "\"\\^a\"", "\"\\^U0041\"", "\"\\^'\"", "\"\\^0\"", "\"\\^ \"", "\"\\^B\"",
        "\"\\^N\"", "\"\\^v\"", "\"\\^e\"", "\"\\^z\"", "\"\\^<\"", "\"a\\^qb\"", "\"\\n\\^q\"",
        "{\"\\^x\":1}", "[\"\\^x\"]", "\"\\^\x7f\"", "\"\\^\xc3\xa9\"",
    };
    size_t i;
    int b;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        err_case(t, bad[i], JSON_E_SYNTAX);
    }
    /* every printable byte after a backslash: the eight escape letters work, `u` needs digits, rest fail */
    for (b = 0x20; b < 0x7f; b++) {
        char in[4];
        const json_value_t *root;
        size_t off;
        json_err_t e;
        in[0] = '"'; in[1] = '\\'; in[2] = (char)b; in[3] = '"';
        e = run((core_str_t){in, 4}, NULL, &root, &off);
        if (is_escape_letter(b)) {
            CHECK_EQ_I64(t, e, JSON_OK);
            CHECK(t, root != NULL && root != SENTINEL);
        } else {
            CHECK_EQ_I64(t, e, JSON_E_SYNTAX);
            CHECK_EQ_U64(t, off, b == 'u' ? 3u : 2u);
        }
    }
}

static void t_bad_unicode_escape_digits(ctest_t *t) {
    static const char bad[] = "gG/:@`xz -+.\"\\";
    size_t i, pos;
    for (pos = 0; pos < 4; pos++) {
        for (i = 0; i < sizeof bad - 1; i++) {
            char in[16];
            const json_value_t *root;
            size_t off, n = 0;
            size_t k;
            in[n++] = '"'; in[n++] = '\\'; in[n++] = 'u';
            for (k = 0; k < 4; k++) {
                in[n++] = (k == pos) ? bad[i] : '1';
            }
            in[n++] = '"'; in[n++] = ' ';
            CHECK_EQ_I64(t, run((core_str_t){in, n}, NULL, &root, &off), JSON_E_SYNTAX);
            CHECK_EQ_U64(t, off, 3 + pos);
            CHECK(t, root == NULL);
        }
    }
}

static void t_truncated_unicode_escapes(ctest_t *t) {
    err_case(t, "\"\\u^\"", JSON_E_SYNTAX);
    err_case(t, "\"\\u1^\"", JSON_E_SYNTAX);
    err_case(t, "\"\\u12^\"", JSON_E_SYNTAX);
    err_case(t, "\"\\u123^\"", JSON_E_SYNTAX);
    err_case(t, "\"\\u^", JSON_E_SYNTAX);
    err_case(t, "\"\\u12^", JSON_E_SYNTAX);
    err_case(t, "\"\\u123^", JSON_E_SYNTAX);
    err_case(t, "\"\\^", JSON_E_SYNTAX);
    err_case(t, "\"\\\"^", JSON_E_SYNTAX);   /* escaped quote, then EOF */
    err_case(t, "\"abc\\u00e^", JSON_E_SYNTAX);
    CHECK(t, ok_parse(t, LIT("\"\\u0123\"")) != NULL);   /* control: four digits */
}

static void t_raw_control_bytes_rejected(ctest_t *t) {
    int b;
    for (b = 0; b < 0x20; b++) {
        char in[5];
        const json_value_t *root;
        size_t off;
        in[0] = '"'; in[1] = 'a'; in[2] = (char)b; in[3] = 'b'; in[4] = '"';
        CHECK_EQ_I64(t, run((core_str_t){in, 5}, NULL, &root, &off), JSON_E_SYNTAX);
        CHECK_EQ_U64(t, off, 2);
        CHECK(t, root == NULL);
    }
    CHECK(t, ok_parse(t, LIT("\"a b\"")) != NULL);   /* controls: 0x20 and 0x7f are fine */
    CHECK(t, ok_parse(t, LIT("\"a\x7f" "b\"")) != NULL);
    err_case(t, "{\"a^\t\":1}", JSON_E_SYNTAX);
    err_case(t, "[\"x\",\"^\n\"]", JSON_E_SYNTAX);
    err_case(t, "\"^\r\"", JSON_E_SYNTAX);
}

static void t_unterminated_strings(ctest_t *t) {
    err_case(t, "\"^", JSON_E_SYNTAX);
    err_case(t, "\"abc^", JSON_E_SYNTAX);
    err_case(t, "[\"abc^", JSON_E_SYNTAX);
    err_case(t, "{\"abc^", JSON_E_SYNTAX);
    err_case(t, "{\"a\":\"b^", JSON_E_SYNTAX);
    err_case(t, "\"abc\\\\^", JSON_E_SYNTAX);   /* escaped backslash, then EOF */
    CHECK(t, ok_parse(t, LIT("\"abc\\\\\"")) != NULL);
}

static void t_single_quotes_are_not_strings(ctest_t *t) {
    err_case(t, "^'a'", JSON_E_SYNTAX);
    err_case(t, "[^'a']", JSON_E_SYNTAX);
    err_case(t, "{^'a':1}", JSON_E_SYNTAX);
    err_case(t, "{\"a\":^'b'}", JSON_E_SYNTAX);
}

CTEST_SUITE("utf8",
    {"invalid_utf8_in_strings", t_invalid_utf8_in_strings},
    {"truncated_utf8_at_eof", t_truncated_utf8_at_eof},
    {"every_continuation_or_invalid_lead_byte", t_every_continuation_or_invalid_lead_byte},
    {"second_byte_ranges", t_second_byte_ranges},
    {"invalid_bytes_outside_strings_are_syntax", t_invalid_bytes_outside_strings_are_syntax},
    {"lone_surrogate_escapes", t_lone_surrogate_escapes},
    {"bad_escape_letters", t_bad_escape_letters},
    {"bad_unicode_escape_digits", t_bad_unicode_escape_digits},
    {"truncated_unicode_escapes", t_truncated_unicode_escapes},
    {"raw_control_bytes_rejected", t_raw_control_bytes_rejected},
    {"unterminated_strings", t_unterminated_strings},
    {"single_quotes_are_not_strings", t_single_quotes_are_not_strings})
