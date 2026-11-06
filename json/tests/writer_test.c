/* Tests for json/json.h: the streaming writer. Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/arena.h"
#include "core/buf.h"
#include "core/str.h"
#include "ctest/ctest.h"
#include "json/json.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})


static char g_out[1u << 16];
static core_buf_t g_b;
static json_writer_t g_w;

static void fresh(ctest_t *t) {
    CHECK(t, core_buf_init(&g_b, g_out, sizeof g_out));
    CHECK(t, json_w_init(&g_w, &g_b));
}

static bool out_is(const char *want) { return core_str_eq(core_buf_str(&g_b), core_str_from_cstr(want)); }

/* Mini interpreter: one op per character.
 *  O/o begin/end object   A/a begin/end array   k/K/q key "k" / "K" / "\n"
 *  s string "s"   e string "\n"   m string "e-acute"   z string `"\`   i int 1   j int -1   u u64 2
 *  t true   f false   n null */
static void play(json_writer_t *w, const char *ops) {
    for (; *ops != '\0'; ops++) {
        switch (*ops) {
        case 'O': json_w_begin_object(w); break;
        case 'o': json_w_end_object(w); break;
        case 'A': json_w_begin_array(w); break;
        case 'a': json_w_end_array(w); break;
        case 'k': json_w_key(w, LIT("k")); break;
        case 'K': json_w_key(w, LIT("K")); break;
        case 'q': json_w_key(w, LIT("\n")); break;
        case 's': json_w_string(w, LIT("s")); break;
        case 'e': json_w_string(w, LIT("\n")); break;
        case 'm': json_w_string(w, LIT("\xc3\xa9")); break;
        case 'z': json_w_string(w, LIT("\"\\")); break;
        case 'i': json_w_i64(w, 1); break;
        case 'j': json_w_i64(w, -1); break;
        case 'u': json_w_u64(w, 2); break;
        case 't': json_w_bool(w, true); break;
        case 'f': json_w_bool(w, false); break;
        default: json_w_null(w); break;
        }
    }
}

typedef struct {
    const char *ops;
    int ok;
    int done;
    const char *out;   /* checked when ok */
} seq_t;

static void run_seqs(ctest_t *t, const seq_t *cases, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        fresh(t);
        play(&g_w, cases[i].ops);
        CHECK_EQ_I64(t, json_w_ok(&g_w), cases[i].ok);
        CHECK_EQ_I64(t, json_w_done(&g_w), cases[i].done);
        if (cases[i].ok) {
            CHECK_EQ_STR(t, core_buf_cstr(&g_b), cases[i].out);
        }
    }
}

static void t_scalars(ctest_t *t) {
    static const seq_t c[] = {
        {"n", 1, 1, "null"}, {"t", 1, 1, "true"}, {"f", 1, 1, "false"}, {"i", 1, 1, "1"}, {"j", 1, 1, "-1"},
        {"u", 1, 1, "2"}, {"s", 1, 1, "\"s\""}, {"e", 1, 1, "\"\\u000a\""}, {"m", 1, 1, "\"\xc3\xa9\""},
        {"z", 1, 1, "\"\\\"\\\\\""},
    };
    run_seqs(t, c, sizeof c / sizeof c[0]);
}

static void t_containers_and_separators(ctest_t *t) {
    static const seq_t c[] = {
        {"Oo", 1, 1, "{}"}, {"Aa", 1, 1, "[]"}, {"Okio", 1, 1, "{\"k\":1}"}, {"OkiKuo", 1, 1, "{\"k\":1,\"K\":2}"},
        {"Oks", 1, 0, "{\"k\":\"s\""},
        {"OkAiutfnao", 1, 1, "{\"k\":[1,2,true,false,null]}"}, {"Aiua", 1, 1, "[1,2]"}, {"Ai", 1, 0, "[1"},
        {"AAaAaa", 1, 1, "[[],[]]"}, {"AAAaaa", 1, 1, "[[[]]]"}, {"AOoOoa", 1, 1, "[{},{}]"},
        {"AOkioOKuoa", 1, 1, "[{\"k\":1},{\"K\":2}]"}, {"OkOKioo", 1, 1, "{\"k\":{\"K\":1}}"},
        {"OkAaKOoo", 1, 1, "{\"k\":[],\"K\":{}}"},
        {"Asemza", 1, 1, "[\"s\",\"\\u000a\",\"\xc3\xa9\",\"\\\"\\\\\"]"}, {"Oqio", 1, 1, "{\"\\u000a\":1}"},
        {"AtfnAaOoa", 1, 1, "[true,false,null,[],{}]"}, {"OkAOkioaKso", 1, 1, "{\"k\":[{\"k\":1}],\"K\":\"s\"}"},
        {"Aijija", 1, 1, "[1,-1,1,-1]"}, {"OkjKjo", 1, 1, "{\"k\":-1,\"K\":-1}"},
    };
    run_seqs(t, c, sizeof c / sizeof c[0]);
}

static void t_incomplete_documents_are_ok_but_not_done(ctest_t *t) {
    static const seq_t c[] = {
        {"", 1, 0, ""}, {"O", 1, 0, "{"}, {"A", 1, 0, "["}, {"Ok", 1, 0, "{\"k\":"}, {"Oki", 1, 0, "{\"k\":1"},
        {"OkiK", 1, 0, "{\"k\":1,\"K\":"}, {"AA", 1, 0, "[["}, {"Aa", 1, 1, "[]"}, {"Ai", 1, 0, "[1"},
        {"OkA", 1, 0, "{\"k\":["}, {"OkAa", 1, 0, "{\"k\":[]"},
    };
    run_seqs(t, c, sizeof c / sizeof c[0]);
}

static void t_misuse_is_an_error(ctest_t *t) {
    static const seq_t c[] = {
        {"ii", 0, 0, NULL}, {"nn", 0, 0, NULL}, {"ss", 0, 0, NULL}, {"Oon", 0, 0, NULL}, {"AaA", 0, 0, NULL},
        {"Oai", 0, 0, NULL}, {"Oi", 0, 0, NULL}, {"Os", 0, 0, NULL}, {"On", 0, 0, NULL}, {"OA", 0, 0, NULL},
        {"OO", 0, 0, NULL}, {"Oko", 0, 0, NULL}, {"Okk", 0, 0, NULL}, {"OkK", 0, 0, NULL}, {"k", 0, 0, NULL},
        {"Ak", 0, 0, NULL}, {"o", 0, 0, NULL}, {"a", 0, 0, NULL}, {"Ao", 0, 0, NULL}, {"Oa", 0, 0, NULL},
        {"AOa", 0, 0, NULL}, {"OkAo", 0, 0, NULL}, {"OkiKo", 0, 0, NULL}, {"OkAk", 0, 0, NULL},
        {"Aoa", 0, 0, NULL}, {"Oaa", 0, 0, NULL}, {"Aaa", 0, 0, NULL}, {"Ooo", 0, 0, NULL}, {"AOoa", 1, 1, "[{}]"},
        {"Okiqo", 0, 0, NULL}, {"AAaaa", 0, 0, NULL}, {"OkOoo", 1, 1, "{\"k\":{}}"}, {"Okmo", 1, 1, "{\"k\":\"\xc3\xa9\"}"},
    };
    run_seqs(t, c, sizeof c / sizeof c[0]);
}

static void t_error_is_sticky_and_silent(ctest_t *t) {
    static const char *const bad[] = {"ii", "k", "Ao", "Oi", "OkAo", "Okk"};
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        size_t len;
        fresh(t);
        play(&g_w, bad[i]);
        CHECK(t, !json_w_ok(&g_w));
        len = core_buf_len(&g_b);
        play(&g_w, "Okio");
        play(&g_w, "n");
        play(&g_w, "Aa");
        CHECK(t, !json_w_ok(&g_w));
        CHECK(t, !json_w_done(&g_w));
        CHECK_EQ_U64(t, core_buf_len(&g_b), len);
    }
    fresh(t);   /* control: a fresh writer over a fresh buffer works again */
    play(&g_w, "Okio");
    CHECK(t, json_w_done(&g_w));
}

static void t_reinit_clears_the_error(ctest_t *t) {
    fresh(t);
    play(&g_w, "ii");
    CHECK(t, !json_w_ok(&g_w));
    CHECK(t, core_buf_init(&g_b, g_out, sizeof g_out));
    CHECK(t, json_w_init(&g_w, &g_b));
    CHECK(t, json_w_ok(&g_w));
    CHECK(t, !json_w_done(&g_w));
    play(&g_w, "Aua");
    CHECK(t, json_w_done(&g_w));
    CHECK(t, out_is("[2]"));
    play(&g_w, "n");   /* second root */
    CHECK(t, !json_w_ok(&g_w));
}

static void t_appends_to_existing_buffer_content(ctest_t *t) {
    fresh(t);
    core_buf_put(&g_b, "xx", 2);
    play(&g_w, "Aia");
    CHECK(t, json_w_done(&g_w));
    CHECK(t, out_is("xx[1]"));
}

typedef struct {
    int64_t v;
    const char *want;
} i64_case_t;

static void t_integers(ctest_t *t) {
    static const i64_case_t c[] = {
        {0, "0"}, {1, "1"}, {-1, "-1"}, {9, "9"}, {10, "10"}, {-10, "-10"}, {99, "99"}, {100, "100"},
        {12345, "12345"}, {-12345, "-12345"}, {INT32_MAX, "2147483647"}, {INT32_MIN, "-2147483648"},
        {INT64_C(4294967296), "4294967296"}, {INT64_MAX, "9223372036854775807"}, {INT64_MAX - 1, "9223372036854775806"},
        {INT64_MIN, "-9223372036854775808"}, {INT64_MIN + 1, "-9223372036854775807"},
        {INT64_C(-4294967296), "-4294967296"}, {INT64_C(1000000000000000000), "1000000000000000000"},
    };
    size_t i;
    for (i = 0; i < sizeof c / sizeof c[0]; i++) {
        fresh(t);
        json_w_i64(&g_w, c[i].v);
        CHECK(t, json_w_done(&g_w));
        CHECK_EQ_STR(t, core_buf_cstr(&g_b), c[i].want);
    }
}

static void t_unsigned_integers(ctest_t *t) {
    static const struct { uint64_t v; const char *want; } c[] = {
        {0, "0"}, {1, "1"}, {9, "9"}, {10, "10"}, {UINT64_C(4294967295), "4294967295"}, {UINT64_C(4294967296), "4294967296"},
        {UINT64_C(9223372036854775807), "9223372036854775807"}, {UINT64_C(9223372036854775808), "9223372036854775808"},
        {UINT64_C(10000000000000000000), "10000000000000000000"}, {UINT64_MAX - 1, "18446744073709551614"},
        {UINT64_MAX, "18446744073709551615"},
    };
    size_t i;
    for (i = 0; i < sizeof c / sizeof c[0]; i++) {
        fresh(t);
        json_w_u64(&g_w, c[i].v);
        CHECK(t, json_w_done(&g_w));
        CHECK_EQ_STR(t, core_buf_cstr(&g_b), c[i].want);
    }
}

static void t_string_escaping_every_byte(ctest_t *t) {
    static const char hexd[] = "0123456789abcdef";
    int b;
    for (b = 0; b < 0x80; b++) {
        char in[3];
        char want[16];
        size_t wl = 0;
        in[0] = 'x';
        in[1] = (char)b;
        in[2] = 'y';
        fresh(t);
        json_w_string(&g_w, (core_str_t){in, 3});
        CHECK(t, json_w_done(&g_w));
        want[wl++] = '"';
        want[wl++] = 'x';
        if (b < 0x20) {
            want[wl++] = '\\'; want[wl++] = 'u'; want[wl++] = '0'; want[wl++] = '0';
            want[wl++] = hexd[b >> 4]; want[wl++] = hexd[b & 15];
        } else if (b == '"' || b == '\\') {
            want[wl++] = '\\'; want[wl++] = (char)b;
        } else {
            want[wl++] = (char)b;
        }
        want[wl++] = 'y';
        want[wl++] = '"';
        CHECK_EQ_MEM(t, core_buf_str(&g_b).ptr, core_buf_len(&g_b), want, wl);
    }
}

static void t_string_passthrough_of_valid_utf8(ctest_t *t) {
    static const char *const same[] = {
        "", "a", "hello world", "/", "\x7f", "\xc2\x80", "\xc3\xa9", "\xdf\xbf", "\xe0\xa0\x80", "\xe2\x80\xa8",
        "\xed\x9f\xbf", "\xee\x80\x80", "\xef\xbf\xbf", "\xf0\x90\x80\x80", "\xf0\x9f\x98\x80", "\xf4\x8f\xbf\xbf",
        "a\xc3\xa9\x62", "{}[]:,", "'single'", "<tag>&amp;", "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82",
    };
    size_t i;
    for (i = 0; i < sizeof same / sizeof same[0]; i++) {
        char want[64];
        size_t n = strlen(same[i]);
        fresh(t);
        json_w_string(&g_w, core_str_from_cstr(same[i]));
        CHECK(t, json_w_done(&g_w));
        want[0] = '"';
        memcpy(want + 1, same[i], n);
        want[n + 1] = '"';
        CHECK_EQ_MEM(t, core_buf_str(&g_b).ptr, core_buf_len(&g_b), want, n + 2);
    }
}

static void t_string_mixed_and_nul(ctest_t *t) {
    fresh(t);
    json_w_string(&g_w, LIT("a\"b\\c\nd\te/f\xc3\xa9"));
    CHECK(t, out_is("\"a\\\"b\\\\c\\u000ad\\u0009e/f\xc3\xa9\""));
    fresh(t);
    json_w_string(&g_w, (core_str_t){"a\0b", 3});
    CHECK(t, json_w_done(&g_w));
    CHECK(t, out_is("\"a\\u0000b\""));
    fresh(t);
    json_w_string(&g_w, (core_str_t){NULL, 0});
    CHECK(t, json_w_done(&g_w));
    CHECK(t, out_is("\"\""));
}

static void t_keys_are_escaped_like_strings(ctest_t *t) {
    fresh(t);
    json_w_begin_object(&g_w);
    json_w_key(&g_w, LIT("a\"b"));
    json_w_i64(&g_w, 1);
    json_w_key(&g_w, LIT("\\\x01\xc3\xa9"));
    json_w_i64(&g_w, 2);
    json_w_key(&g_w, LIT(""));
    json_w_i64(&g_w, 3);
    json_w_end_object(&g_w);
    CHECK(t, json_w_done(&g_w));
    CHECK(t, out_is("{\"a\\\"b\":1,\"\\\\\\u0001\xc3\xa9\":2,\"\":3}"));
}

static void t_invalid_utf8_is_an_error(ctest_t *t) {
    static const struct { const char *p; size_t n; } bad[] = {
        {"\xc0\x80", 2}, {"\xc1\xbf", 2}, {"\xe0\x80\x80", 3}, {"\xe0\x9f\xbf", 3}, {"\xf0\x80\x80\x80", 4},
        {"\xf0\x8f\xbf\xbf", 4}, {"\xed\xa0\x80", 3}, {"\xed\xbf\xbf", 3}, {"\xf4\x90\x80\x80", 4},
        {"\xf5\x80\x80\x80", 4}, {"\xff", 1}, {"\xfe", 1}, {"\x80", 1}, {"\xbf", 1}, {"\xc3", 1}, {"\xe2\x82", 2},
        {"\xf0\x9f\x98", 3}, {"\xc3\x41", 2}, {"\xe2\x82\x41", 3}, {"a\xc3", 2}, {"\xc3\xa9\xa9", 3},
        {"ab\xff" "cd", 5}, {"\xf8\x88\x80\x80\x80", 5},
    };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        fresh(t);
        json_w_string(&g_w, (core_str_t){bad[i].p, bad[i].n});
        CHECK(t, !json_w_ok(&g_w));
        CHECK(t, !json_w_done(&g_w));
        fresh(t);
        json_w_begin_object(&g_w);
        json_w_key(&g_w, (core_str_t){bad[i].p, bad[i].n});
        CHECK(t, !json_w_ok(&g_w));
    }
    fresh(t);   /* controls: the same shapes with valid text */
    json_w_string(&g_w, LIT("a\xc3\xa9"));
    CHECK(t, json_w_done(&g_w));
    fresh(t);
    json_w_begin_object(&g_w);
    json_w_key(&g_w, LIT("\xc3\xa9"));
    CHECK(t, json_w_ok(&g_w));
}

static void t_depth_limit_of_64(ctest_t *t) {
    size_t i, d;
    for (d = 63; d <= 65; d++) {
        char want[200];
        size_t wl = 0;
        fresh(t);
        for (i = 0; i < d; i++) {
            json_w_begin_array(&g_w);
        }
        CHECK_EQ_I64(t, json_w_ok(&g_w), d <= 64);
        if (d <= 64) {
            for (i = 0; i < d; i++) {
                json_w_end_array(&g_w);
            }
            for (i = 0; i < d; i++) want[wl++] = '[';
            for (i = 0; i < d; i++) want[wl++] = ']';
            CHECK(t, json_w_done(&g_w));
            CHECK_EQ_MEM(t, core_buf_str(&g_b).ptr, core_buf_len(&g_b), want, wl);
        }
    }
    for (d = 64; d <= 65; d++) {
        fresh(t);
        for (i = 0; i < d; i++) {
            json_w_begin_object(&g_w);
            json_w_key(&g_w, LIT("k"));
        }
        CHECK_EQ_I64(t, json_w_ok(&g_w), d == 64);
    }
}

static void t_buffer_overflow_boundaries(ctest_t *t) {
    static const char *const progs[] = {"n", "Okio", "OkAiutfnao", "Asemza", "Oqio", "e", "AOkioOKuoa"};
    size_t p;
    for (p = 0; p < sizeof progs / sizeof progs[0]; p++) {
        size_t len, cap;
        fresh(t);
        play(&g_w, progs[p]);
        CHECK(t, json_w_done(&g_w));
        len = core_buf_len(&g_b);
        for (cap = 0; cap <= len + 1; cap++) {
            char small[64];
            core_buf_t b;
            json_writer_t w;
            CHECK(t, core_buf_init(&b, small, cap));
            CHECK(t, json_w_init(&w, &b));
            play(&w, progs[p]);
            if (cap == len + 1) {
                CHECK(t, json_w_ok(&w) && json_w_done(&w) && !core_buf_overflow(&b));
                CHECK_EQ_U64(t, core_buf_len(&b), len);
            } else {
                CHECK(t, !json_w_ok(&w));
                CHECK(t, !json_w_done(&w));
                CHECK(t, core_buf_overflow(&b));
            }
        }
    }
}

static void t_init_rejects_null(ctest_t *t) {
    json_writer_t w;
    core_buf_t b;
    char s[8];
    CHECK(t, core_buf_init(&b, s, sizeof s));
    CHECK(t, !json_w_init(NULL, &b));
    CHECK(t, !json_w_init(&w, NULL));
    CHECK(t, !json_w_ok(&w));
    CHECK(t, !json_w_done(&w));
    json_w_null(&w);
    json_w_begin_array(&w);
    CHECK(t, !json_w_ok(&w));
    CHECK(t, json_w_init(&w, &b));   /* control */
    CHECK(t, json_w_ok(&w));
    CHECK(t, !json_w_done(&w));
}

static void t_null_writer_is_harmless(ctest_t *t) {
    json_w_begin_object(NULL);
    json_w_end_object(NULL);
    json_w_begin_array(NULL);
    json_w_end_array(NULL);
    json_w_key(NULL, LIT("k"));
    json_w_string(NULL, LIT("s"));
    json_w_i64(NULL, 1);
    json_w_u64(NULL, 1);
    json_w_bool(NULL, true);
    json_w_null(NULL);
    CHECK(t, !json_w_ok(NULL));
    CHECK(t, !json_w_done(NULL));
    fresh(t);
    play(&g_w, "n");   /* control */
    CHECK(t, json_w_done(&g_w));
}

CTEST_SUITE("writer",
    {"scalars", t_scalars},
    {"containers_and_separators", t_containers_and_separators},
    {"incomplete_documents_are_ok_but_not_done", t_incomplete_documents_are_ok_but_not_done},
    {"misuse_is_an_error", t_misuse_is_an_error},
    {"error_is_sticky_and_silent", t_error_is_sticky_and_silent},
    {"reinit_clears_the_error", t_reinit_clears_the_error},
    {"appends_to_existing_buffer_content", t_appends_to_existing_buffer_content},
    {"integers", t_integers},
    {"unsigned_integers", t_unsigned_integers},
    {"string_escaping_every_byte", t_string_escaping_every_byte},
    {"string_passthrough_of_valid_utf8", t_string_passthrough_of_valid_utf8},
    {"string_mixed_and_nul", t_string_mixed_and_nul},
    {"keys_are_escaped_like_strings", t_keys_are_escaped_like_strings},
    {"invalid_utf8_is_an_error", t_invalid_utf8_is_an_error},
    {"depth_limit_of_64", t_depth_limit_of_64},
    {"buffer_overflow_boundaries", t_buffer_overflow_boundaries},
    {"init_rejects_null", t_init_rejects_null},
    {"null_writer_is_harmless", t_null_writer_is_harmless})
