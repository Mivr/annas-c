/* Tests for json/json.h (A044): truncated UTF-8 and surrogate escapes in the parser, and what the writer does
 * on misuse and on truncated UTF-8. Public header only. */
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

/* ---- UTF-8 ---- */

static void t_truncated_sequence_after_the_input(ctest_t *t) {
    /* the bytes behind the cut would complete the sequence; they must not count */
    expect_err(t, cut_of("\"\xc3\xa9\"", 4, 2), NULL, JSON_E_UTF8, 1);
    expect_err(t, cut_of("\"ab\xc3\xa9\"", 6, 4), NULL, JSON_E_UTF8, 3);
    expect_err(t, cut_of("\"\xe2\x82\xac\"", 5, 3), NULL, JSON_E_UTF8, 1);
    expect_err(t, cut_of("\"\xe2\x82\xac\"", 5, 2), NULL, JSON_E_UTF8, 1);
    expect_err(t, cut_of("\"ab\xe2\x82\xac\"", 7, 5), NULL, JSON_E_UTF8, 3);
    expect_err(t, cut_of("\"\xf0\x9f\x98\x80\"", 6, 4), NULL, JSON_E_UTF8, 1);
    expect_err(t, cut_of("\"\xf0\x9f\x98\x80\"", 6, 3), NULL, JSON_E_UTF8, 1);
    expect_err(t, cut_of("\"\xf0\x9f\x98\x80\"", 6, 2), NULL, JSON_E_UTF8, 1);
    expect_err(t, cut_of("[\"abc\xf0\x9f\x98\x80\"", 10, 8), NULL, JSON_E_UTF8, 5);
    expect_err(t, cut_of("{\"\xc3\xa9\":1}", 8, 3), NULL, JSON_E_UTF8, 2);
    expect_err(t, cut_of("{\"k\":\"\xc3\xa9\"}", 10, 7), NULL, JSON_E_UTF8, 6);
}

static void str_case(ctest_t *t, core_str_t in, bool ok) {
    const json_value_t *root;
    size_t off;
    json_err_t e = run(in, NULL, &root, &off);
    CHECK_EQ_I64(t, e, ok ? JSON_OK : JSON_E_UTF8);
    CHECK_EQ_U64(t, off, ok ? 0 : 1);
    CHECK_EQ_I64(t, json_type(root), ok ? JSON_STRING : JSON_NONE);
}

static void t_later_continuation_bytes(ctest_t *t) {
    /* third and fourth bytes: 0x80..0xBF only */
    str_case(t, LIT("\"\xe2\x82\x80\""), true);
    str_case(t, LIT("\"\xe2\x82\xbf\""), true);
    str_case(t, LIT("\"\xe2\x82\xc0\""), false);
    str_case(t, LIT("\"\xe2\x82\xc1\""), false);
    str_case(t, LIT("\"\xe2\x82\xff\""), false);
    str_case(t, LIT("\"\xe2\x82\x7f\""), false);
    str_case(t, LIT("\"\xe2\x82\"\""), false);
    str_case(t, LIT("\"\xf0\x90\x80\x80\""), true);
    str_case(t, LIT("\"\xf0\x90\xbf\xbf\""), true);
    str_case(t, LIT("\"\xf0\x90\x80\xc0\""), false);
    str_case(t, LIT("\"\xf0\x90\xc0\x80\""), false);
    str_case(t, LIT("\"\xf0\x90\x80\x7f\""), false);
    str_case(t, LIT("\"\xf0\x90\x7f\x80\""), false);
    str_case(t, LIT("\"\xf4\x8f\xbf\xc0\""), false);
    str_case(t, LIT("\"\xf1\x80\x80\xff\""), false);
}

static void t_high_surrogate_then_bad_escape(ctest_t *t) {
    /* a high surrogate whose following \u escape is not a valid low surrogate escape is an error at the
     * high one's backslash, even when the following escape is malformed or cut short */
    expect_err(t, LIT("\"\\ud83d\\uZZZZ\""), NULL, JSON_E_UTF8, 1);
    expect_err(t, LIT("\"\\ud83d\\u12G4\""), NULL, JSON_E_UTF8, 1);
    expect_err(t, LIT("\"\\ud83d\\ude0g\""), NULL, JSON_E_UTF8, 1);
    expect_err(t, LIT("\"\\ud83d\\uDE0\""), NULL, JSON_E_UTF8, 1);
    expect_err(t, LIT("\"\\ud83d\\u12\""), NULL, JSON_E_UTF8, 1);
    expect_err(t, LIT("\"\\ud83d\\u\""), NULL, JSON_E_UTF8, 1);
    expect_err(t, LIT("\"ab\\udbff\\uzzzz\""), NULL, JSON_E_UTF8, 3);
    expect_err(t, LIT("\"\\ud83d\\ude00\\ud83d\\uzzzz\""), NULL, JSON_E_UTF8, 13);
    expect_err(t, LIT("{\"\\ud83d\\uZZZZ\":1}"), NULL, JSON_E_UTF8, 2);
}

/* ---- writer ---- */

static char g_out[1u << 12];
static core_buf_t g_b;
static json_writer_t g_w;

static void wfresh(ctest_t *t) {
    CHECK(t, core_buf_init(&g_b, g_out, sizeof g_out));
    CHECK(t, json_w_init(&g_w, &g_b));
}

static bool out_is(const char *want) { return core_str_eq(core_buf_str(&g_b), core_str_from_cstr(want)); }

/* One value call, by letter: i i64 u u64 s string t true f false n null o object a array. */
static void put_value(json_writer_t *w, char kind) {
    switch (kind) {
    case 'i': json_w_i64(w, 7); break;
    case 'u': json_w_u64(w, 8); break;
    case 's': json_w_string(w, LIT("x")); break;
    case 't': json_w_bool(w, true); break;
    case 'f': json_w_bool(w, false); break;
    case 'n': json_w_null(w); break;
    case 'o': json_w_begin_object(w); break;
    default: json_w_begin_array(w); break;
    }
}

static void t_second_root_value_writes_nothing(ctest_t *t) {
    static const char kinds[] = "iustfnoa";
    size_t i;
    for (i = 0; kinds[i] != '\0'; i++) {
        wfresh(t);
        json_w_i64(&g_w, 1);
        CHECK(t, json_w_done(&g_w));
        put_value(&g_w, kinds[i]);
        CHECK(t, !json_w_ok(&g_w));
        CHECK(t, !json_w_done(&g_w));
        CHECK(t, out_is("1"));
        json_w_null(&g_w);
        CHECK(t, out_is("1"));
    }
    wfresh(t);
    json_w_begin_array(&g_w);
    json_w_end_array(&g_w);
    put_value(&g_w, 's');
    CHECK(t, !json_w_ok(&g_w));
    CHECK(t, out_is("[]"));
}

static void t_value_without_key_writes_nothing(ctest_t *t) {
    static const char kinds[] = "iustfnoa";
    size_t i;
    for (i = 0; kinds[i] != '\0'; i++) {
        wfresh(t);
        json_w_begin_object(&g_w);
        put_value(&g_w, kinds[i]);
        CHECK(t, !json_w_ok(&g_w));
        CHECK(t, out_is("{"));
        json_w_end_object(&g_w);
        CHECK(t, out_is("{"));
    }
    wfresh(t);
    json_w_begin_object(&g_w);
    json_w_key(&g_w, LIT("a"));
    json_w_i64(&g_w, 1);
    put_value(&g_w, 'i');
    CHECK(t, !json_w_ok(&g_w));
    CHECK(t, out_is("{\"a\":1"));
}

static void tail_string(ctest_t *t, const char *full, size_t full_len, size_t cut) {
    memcpy(g_tail, full, full_len);
    wfresh(t);
    json_w_string(&g_w, (core_str_t){g_tail, cut});
    CHECK(t, !json_w_ok(&g_w));
    CHECK(t, !json_w_done(&g_w));
    wfresh(t);
    json_w_begin_object(&g_w);
    json_w_key(&g_w, (core_str_t){g_tail, cut});
    CHECK(t, !json_w_ok(&g_w));
}

static void t_writer_truncated_utf8_after_the_string(ctest_t *t) {
    tail_string(t, "A\xc3\xa9", 3, 2);
    tail_string(t, "AB\xc3\xa9", 4, 3);
    tail_string(t, "A\xe2\x82\xac", 4, 3);
    tail_string(t, "A\xe2\x82\xac", 4, 2);
    tail_string(t, "A\xf0\x9f\x98\x80", 5, 4);
    tail_string(t, "A\xf0\x9f\x98\x80", 5, 3);
    tail_string(t, "A\xf0\x9f\x98\x80", 5, 2);
    wfresh(t);
    memcpy(g_tail, "A\xc3\xa9", 3);
    json_w_string(&g_w, (core_str_t){g_tail, 3});
    CHECK(t, json_w_done(&g_w));
    CHECK(t, out_is("\"A\xc3\xa9\""));
}


CTEST_SUITE("survivor_text",
    {"truncated_sequence_after_the_input", t_truncated_sequence_after_the_input},
    {"later_continuation_bytes", t_later_continuation_bytes},
    {"high_surrogate_then_bad_escape", t_high_surrogate_then_bad_escape},
    {"second_root_value_writes_nothing", t_second_root_value_writes_nothing},
    {"value_without_key_writes_nothing", t_value_without_key_writes_nothing},
    {"writer_truncated_utf8_after_the_string", t_writer_truncated_utf8_after_the_string})
