/* Worst-case complexity tests for json/json.h. Public header only.
 *
 * There are no wall-clock assertions: the ctest 10 s per-test timeout is the only clock. Each test runs its
 * input to completion and asserts the exact result. A linear (or n log n) implementation finishes every test
 * here in well under 1 s, even under `san`. The tests marked QUADRATIC target the duplicate-key check: a
 * parser that compares each new key with every earlier key of the same object does K*(K-1)/2 key
 * comparisons for K keys, and a comparison costs >= 2 ns even in release. Each QUADRATIC test is sized so
 * that this takes >= 20 s (the arithmetic is stated per test). A hash set or sorted index does O(K) or
 * O(K log K) work. The other tests pass on a quadratic key check too (they are linear-by-design shapes
 * that guard against other regressions).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/arena.h"
#include "core/buf.h"
#include "core/str.h"
#include "ctest/ctest.h"
#include "json/json.h"

static unsigned char g_arena_mem[128u << 20];
static core_arena_t g_a;
static char g_in[JSON_MAX_INPUT];
static char g_out[10u << 20];
static core_buf_t g_b;
static int g_sentinel;
#define SENTINEL ((const json_value_t *)(const void *)&g_sentinel)

static core_str_t S(const char *p, size_t n) { return (core_str_t){p, n}; }

static json_err_t parse_lim(size_t n, const json_limits_t *lim, const json_value_t **root, size_t *off) {
    *root = SENTINEL;
    *off = 99999u;
    if (!core_arena_init(&g_a, g_arena_mem, sizeof g_arena_mem)) {
        return JSON_E_ARG;
    }
    return json_parse(&g_a, S(g_in, n), lim, root, off);
}

static json_err_t parse(size_t n, const json_value_t **root, size_t *off) { return parse_lim(n, NULL, root, off); }

/* ---- builders ---- */

/* `{` then K members `"<pfx 'k' * pfx_len><%06zx i>":0` separated by commas. No closing brace. Member text is
 * pfx_len + 6 key bytes + 2 quotes + `:0` = pfx_len + 10, plus a comma between members. */
static size_t build_hex_members(size_t k, size_t pfx_len) {
    size_t n = 0, i;
    g_in[n++] = '{';
    for (i = 0; i < k; i++) {
        g_in[n++] = '"';
        memset(g_in + n, 'k', pfx_len);
        n += pfx_len;
        n += (size_t)snprintf(g_in + n, 8, "%06zx", i);
        g_in[n++] = '"';
        g_in[n++] = ':';
        g_in[n++] = '0';
        if (i + 1 < k) {
            g_in[n++] = ',';
        }
    }
    return n;
}

static void hex_key(char *dst, size_t pfx_len, size_t i) {
    memset(dst, 'k', pfx_len);
    (void)snprintf(dst + pfx_len, 8, "%06zx", i);
}

static size_t put_lit(size_t n, const char *s) {
    size_t l = strlen(s);
    memcpy(g_in + n, s, l);
    return n + l;
}

/* ---- QUADRATIC: wide objects ---- */

/* K = 200000 distinct 6-byte keys: 2.0e10 comparisons (>= 40 s). Input 200000*11 + 1 = 2.2 MB. */
#define K_WIDE 200000u

static void t_wide_distinct(ctest_t *t) {
    const json_value_t *root = NULL;
    size_t off, n = build_hex_members(K_WIDE, 0), i;
    char key[16];
    g_in[n++] = '}';
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_OK);
    CHECK_EQ_U64(t, off, 0);
    CHECK_EQ_I64(t, json_type(root), JSON_OBJECT);
    CHECK_EQ_U64(t, json_len(root), K_WIDE);
    for (i = 0; i < K_WIDE; i += 49999u) {
        core_str_t k = json_key_at(root, i);
        hex_key(key, 0, i);
        CHECK_EQ_MEM(t, k.ptr, k.len, key, 6);
    }
    hex_key(key, 0, K_WIDE - 1);
    CHECK(t, json_obj_get(root, S(key, 6)) == json_at(root, K_WIDE - 1));
    hex_key(key, 0, K_WIDE);
    CHECK(t, json_obj_get(root, S(key, 6)) == NULL);
    /* The compact serialiser reproduces the (compact) input exactly. */
    CHECK(t, core_buf_init(&g_b, g_out, sizeof g_out));
    CHECK(t, json_write(&g_b, root));
    CHECK_EQ_U64(t, core_buf_len(&g_b), n);
    CHECK_EQ_MEM(t, g_out, core_buf_len(&g_b), g_in, n);
}

/* Same object plus a repeat of the FIRST key as the last member: the whole object has been seen before the
 * error. 2.0e10 comparisons, as above. The repeat's opening quote is the byte after the comma that follows
 * the K_WIDE members, i.e. at offset n + 1 where n is the length before the comma. */
static void t_wide_repeat_last(ctest_t *t) {
    const json_value_t *root = SENTINEL;
    size_t off, n = build_hex_members(K_WIDE, 0), at;
    g_in[n++] = ',';
    at = n;
    n = put_lit(n, "\"000000\":1}");
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_E_SYNTAX);
    CHECK_EQ_U64(t, off, at);
    CHECK(t, root == NULL);
    CHECK_EQ_U64(t, core_arena_used(&g_a), 0);
}

/* Keys sharing a 58-byte prefix (key length 64): memcmp must look at 58 equal bytes before it can tell two
 * keys apart. K = 100000: 5.0e9 comparisons of >= 5 ns = >= 25 s. Input 100000*69 = 6.9 MB <= 8 MiB. */
#define K_PREFIX 100000u

static void t_prefix_keys(ctest_t *t) {
    const json_value_t *root = NULL;
    size_t off, n = build_hex_members(K_PREFIX, 58), i;
    char key[80];
    g_in[n++] = '}';
    CHECK(t, n < JSON_MAX_INPUT);
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_OK);
    CHECK_EQ_U64(t, json_len(root), K_PREFIX);
    for (i = 0; i < K_PREFIX; i += 33333u) {
        core_str_t k = json_key_at(root, i);
        hex_key(key, 58, i);
        CHECK_EQ_MEM(t, k.ptr, k.len, key, 64);
    }
    hex_key(key, 58, K_PREFIX - 1);
    CHECK(t, json_obj_get(root, S(key, 64)) == json_at(root, K_PREFIX - 1));
}

/* Colliding-shape keys: 17 blocks, each "Aa" or "BB" (same Java-style string hash, same length 34), so
 * 2^17 = 131072 distinct keys that collide under any polynomial hash of base 31. A hash-based
 * implementation must therefore use a hash that survives this family. Quadratic: 8.6e9 comparisons of
 * 34-byte keys (>= 35 s). Input 131072 * 39 = 5.1 MB. */
#define K_COLLIDE 131072u
#define COLLIDE_BLOCKS 17u

static void collide_key(char *dst, size_t i) {
    size_t b;
    for (b = 0; b < COLLIDE_BLOCKS; b++) {
        memcpy(dst + 2 * b, ((i >> b) & 1u) != 0u ? "BB" : "Aa", 2);
    }
}

static size_t build_collide(void) {
    size_t n = 0, i;
    g_in[n++] = '{';
    for (i = 0; i < K_COLLIDE; i++) {
        g_in[n++] = '"';
        collide_key(g_in + n, i);
        n += 2 * COLLIDE_BLOCKS;
        g_in[n++] = '"';
        g_in[n++] = ':';
        g_in[n++] = '0';
        if (i + 1 < K_COLLIDE) {
            g_in[n++] = ',';
        }
    }
    return n;
}

static void t_collide_distinct(ctest_t *t) {
    const json_value_t *root = NULL;
    size_t off, n = build_collide(), i;
    char key[2 * COLLIDE_BLOCKS];
    g_in[n++] = '}';
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_OK);
    CHECK_EQ_U64(t, json_len(root), K_COLLIDE);
    for (i = 0; i < K_COLLIDE; i += 26213u) {
        core_str_t k = json_key_at(root, i);
        collide_key(key, i);
        CHECK_EQ_MEM(t, k.ptr, k.len, key, sizeof key);
    }
    collide_key(key, K_COLLIDE - 1);
    CHECK(t, json_obj_get(root, S(key, sizeof key)) == json_at(root, K_COLLIDE - 1));
}

/* The collision family with a repeat of key 77777 as the last member (error at the repeat's quote). */
static void t_collide_repeat_last(ctest_t *t) {
    const json_value_t *root = SENTINEL;
    size_t off, n = build_collide(), at;
    g_in[n++] = ',';
    at = n;
    g_in[n++] = '"';
    collide_key(g_in + n, 77777u);
    n += 2 * COLLIDE_BLOCKS;
    n = put_lit(n, "\":1}");
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_E_SYNTAX);
    CHECK_EQ_U64(t, off, at);
    CHECK(t, root == NULL);
}

/* ---- deep nesting at the depth cap ---- */

/* 64 nested objects (the cap), each with 999 scalar members `"kNNNN":0,` (10 bytes) followed by the member
 * `"n":` holding the next level: a level's prefix is 1 + 999*10 + 4 = 9995 bytes. The innermost level has
 * its 999 members and `"n":0`. Per-object quadratic cost is 1000^2/2 * 64 = 3.2e7 comparisons (< 1 s even
 * under san), so this passes on a quadratic check too; it guards against per-level state blow-ups. */
#define LEVEL_PREFIX 9995u

static size_t build_levels(size_t depth) {
    size_t n = 0, d, i;
    for (d = 0; d < depth; d++) {
        g_in[n++] = '{';
        for (i = 0; i < 999u; i++) {
            n += (size_t)snprintf(g_in + n, 12, "\"k%04zu\":0,", i);
        }
        if (d + 1 < depth) {
            n = put_lit(n, "\"n\":");
        } else {
            n = put_lit(n, "\"n\":0");
        }
    }
    for (d = 0; d < depth; d++) {
        g_in[n++] = '}';
    }
    return n;
}

static void t_nested_cap(ctest_t *t) {
    const json_value_t *root = NULL, *v;
    size_t off, n = build_levels(64), d;
    core_str_t num;
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_OK);
    v = root;
    for (d = 0; d < 63; d++) {
        CHECK_EQ_U64(t, json_len(v), 1000);
        v = json_obj_get(v, S("n", 1));
        CHECK_EQ_I64(t, json_type(v), JSON_OBJECT);
    }
    CHECK_EQ_U64(t, json_len(v), 1000);
    v = json_obj_get(v, S("n", 1));
    CHECK(t, json_number_text(v, &num));
    CHECK_EQ_MEM(t, num.ptr, num.len, "0", 1);
    CHECK(t, core_buf_init(&g_b, g_out, sizeof g_out));
    CHECK(t, json_write(&g_b, root));
    CHECK_EQ_U64(t, core_buf_len(&g_b), n);
    CHECK_EQ_MEM(t, g_out, core_buf_len(&g_b), g_in, n);
}

/* One level more than the cap: JSON_E_DEPTH at the 65th '{', offset 64 * 9995. */
static void t_nested_over_cap(ctest_t *t) {
    const json_value_t *root = SENTINEL;
    size_t off, n = build_levels(65);
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_E_DEPTH);
    CHECK_EQ_U64(t, off, 64u * LEVEL_PREFIX);
    CHECK(t, root == NULL);
}

/* 64 nested arrays, each holding 5000 `0,` elements and then the next level: 320000 elements, ~640 KB. */
static void t_nested_arrays(ctest_t *t) {
    const json_value_t *root = NULL, *v;
    size_t off, n = 0, d, i;
    for (d = 0; d < 64; d++) {
        g_in[n++] = '[';
        for (i = 0; i < 5000u; i++) {
            g_in[n++] = '0';
            g_in[n++] = ',';
        }
    }
    g_in[n++] = '1';
    for (d = 0; d < 64; d++) {
        g_in[n++] = ']';
    }
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_OK);
    v = root;
    for (d = 0; d < 63; d++) {
        CHECK_EQ_U64(t, json_len(v), 5001);
        v = json_at(v, 5000);
        CHECK_EQ_I64(t, json_type(v), JSON_ARRAY);
    }
    CHECK_EQ_U64(t, json_len(v), 5001);
    CHECK(t, core_buf_init(&g_b, g_out, sizeof g_out));
    CHECK(t, json_write(&g_b, root));
    CHECK_EQ_MEM(t, g_out, core_buf_len(&g_b), g_in, n);
}

/* ---- long strings with many escapes ---- */

/* Unit: `\n` `\u00e9` `\ud83d\ude00` `\\` `\"` = 2+6+12+2+2 = 24 input bytes, decoded 1+2+4+1+1 = 9 bytes.
 * 100000 units: 2.4 MB of input, 900000 decoded bytes (<= the 1 MiB max_string). Each byte is touched a
 * constant number of times by a linear decoder. */
#define UNITS 100000u
static const char k_unit_in[] = "\\n\\u00e9\\ud83d\\ude00\\\\\\\"";
static const char k_unit_dec[] = "\n\xc3\xa9\xf0\x9f\x98\x80\\\"";
/* The writer's output for one decoded unit: \n -> \u000a, the rest verbatim or backslash-escaped. */
static const char k_unit_wr[] = "\\u000a\xc3\xa9\xf0\x9f\x98\x80\\\\\\\"";

static size_t build_escape_string(size_t n) {
    size_t u;
    g_in[n++] = '"';
    for (u = 0; u < UNITS; u++) {
        memcpy(g_in + n, k_unit_in, sizeof k_unit_in - 1);
        n += sizeof k_unit_in - 1;
    }
    g_in[n++] = '"';
    return n;
}

static void check_escape_decoded(ctest_t *t, core_str_t s) {
    size_t u;
    CHECK_EQ_U64(t, s.len, UNITS * (sizeof k_unit_dec - 1));
    for (u = 0; u < UNITS; u += 9999u) {
        CHECK_EQ_MEM(t, s.ptr + u * (sizeof k_unit_dec - 1), sizeof k_unit_dec - 1, k_unit_dec,
                     sizeof k_unit_dec - 1);
    }
    CHECK_EQ_MEM(t, s.ptr + (UNITS - 1) * (sizeof k_unit_dec - 1), sizeof k_unit_dec - 1, k_unit_dec,
                 sizeof k_unit_dec - 1);
}

static void t_escape_string(ctest_t *t) {
    const json_value_t *root = NULL;
    size_t off, n = build_escape_string(0), u, w = 0;
    core_str_t s = {NULL, 0};
    CHECK_EQ_U64(t, n, UNITS * (sizeof k_unit_in - 1) + 2);
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_OK);
    CHECK(t, json_get_str(root, &s));
    check_escape_decoded(t, s);
    /* Round trip through the writer: expected output built independently, unit by unit. */
    CHECK(t, core_buf_init(&g_b, g_out, sizeof g_out));
    CHECK(t, json_write(&g_b, root));
    g_in[w++] = '"';
    for (u = 0; u < UNITS; u++) {
        memcpy(g_in + w, k_unit_wr, sizeof k_unit_wr - 1);
        w += sizeof k_unit_wr - 1;
    }
    g_in[w++] = '"';
    CHECK_EQ_U64(t, core_buf_len(&g_b), w);
    CHECK_EQ_MEM(t, g_out, core_buf_len(&g_b), g_in, w);
}

/* The same string as an object key and as its value: {"<esc>":"<esc>"}. */
static void t_escape_key(ctest_t *t) {
    const json_value_t *root = NULL, *v;
    size_t off, n = 1;
    core_str_t k, s = {NULL, 0};
    g_in[0] = '{';
    n = build_escape_string(n);
    g_in[n++] = ':';
    n = build_escape_string(n);
    g_in[n++] = '}';
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_OK);
    CHECK_EQ_U64(t, json_len(root), 1);
    k = json_key_at(root, 0);
    check_escape_decoded(t, k);
    v = json_obj_get(root, k);
    CHECK(t, v == json_at(root, 0));
    CHECK(t, json_get_str(v, &s));
    check_escape_decoded(t, s);
}

/* One byte over max_string after decoding: 116509 units * 9 = 1048581 > 1048576. The error is at the
 * opening quote. Linear: the decoder stops as soon as the limit is crossed. */
static void t_escape_over_limit(ctest_t *t) {
    const json_value_t *root = SENTINEL;
    size_t off, n = 0, u;
    g_in[n++] = '"';
    for (u = 0; u < 116509u; u++) {
        memcpy(g_in + n, k_unit_in, sizeof k_unit_in - 1);
        n += sizeof k_unit_in - 1;
    }
    g_in[n++] = '"';
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_E_LIMIT);
    CHECK_EQ_U64(t, off, 0);
    CHECK(t, root == NULL);
}

/* ---- arrays at the element cap, long numbers, the streaming writer ---- */

/* An array of 1048575 `0` values: with the root that is exactly max_elements (1048576). Input 2 MB. */
static void t_array_at_element_cap(ctest_t *t) {
    const json_value_t *root = NULL;
    size_t off, n = 0, i;
    g_in[n++] = '[';
    for (i = 0; i < 1048575u; i++) {
        g_in[n++] = '0';
        g_in[n++] = i + 1 < 1048575u ? ',' : ']';
    }
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_OK);
    CHECK_EQ_U64(t, json_len(root), 1048575u);
    CHECK_EQ_I64(t, json_type(json_at(root, 1048574u)), JSON_NUMBER);
    CHECK(t, json_at(root, 1048575u) == NULL);
    CHECK(t, core_buf_init(&g_b, g_out, sizeof g_out));
    CHECK(t, json_write(&g_b, root));
    CHECK_EQ_U64(t, core_buf_len(&g_b), n);
    CHECK_EQ_MEM(t, g_out, core_buf_len(&g_b), g_in, n);
}

/* One more value: JSON_E_LIMIT at that value's first byte, 1 + 2 * 1048575. */
static void t_array_over_element_cap(ctest_t *t) {
    const json_value_t *root = SENTINEL;
    size_t off, n = 0, i;
    g_in[n++] = '[';
    for (i = 0; i < 1048576u; i++) {
        g_in[n++] = '0';
        g_in[n++] = i + 1 < 1048576u ? ',' : ']';
    }
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_E_LIMIT);
    CHECK_EQ_U64(t, off, 1u + 2u * 1048575u);
    CHECK(t, root == NULL);
    CHECK_EQ_U64(t, core_arena_used(&g_a), 0);
}

/* Number text is bounded only by max_input: 3 runs of 2,000,000 digits (integer, fraction, exponent) =
 * 6000003 bytes plus ".", "e+". It is copied once, kept verbatim, and is not an integer. */
static void t_long_number(ctest_t *t) {
    const json_value_t *root = NULL;
    size_t off, n = 0, i;
    core_str_t txt = {NULL, 0};
    uint64_t u;
    int64_t s;
    g_in[n++] = '9';
    for (i = 1; i < 2000000u; i++) {
        g_in[n++] = (char)('0' + (int)(i % 10u));
    }
    g_in[n++] = '.';
    for (i = 0; i < 2000000u; i++) {
        g_in[n++] = (char)('0' + (int)(i % 10u));
    }
    n = put_lit(n, "e+");
    for (i = 0; i < 2000000u; i++) {
        g_in[n++] = (char)('0' + (int)(i % 10u));
    }
    CHECK_EQ_I64(t, parse(n, &root, &off), JSON_OK);
    CHECK(t, json_number_text(root, &txt));
    CHECK_EQ_U64(t, txt.len, n);
    CHECK_EQ_MEM(t, txt.ptr, txt.len, g_in, n);
    CHECK(t, !json_get_u64(root, &u));
    CHECK(t, !json_get_i64(root, &s));
    /* A pure 6,000,000-digit integer: still not convertible (out of range), still verbatim. */
    memset(g_in, '7', 6000000u);
    CHECK_EQ_I64(t, parse(6000000u, &root, &off), JSON_OK);
    CHECK(t, json_number_text(root, &txt));
    CHECK_EQ_U64(t, txt.len, 6000000u);
    CHECK(t, !json_get_u64(root, &u));
    CHECK(t, !json_get_i64(root, &s));
}

/* The streaming writer: an object of 200000 members (hex key, i64 value), plus a 900000-byte string value
 * that needs an escape for every 9th byte. The expected text is built with snprintf, independently. */
static void t_writer_wide(ctest_t *t) {
    json_writer_t w;
    size_t i, n = 0, u;
    char key[16];
    CHECK(t, core_buf_init(&g_b, g_out, sizeof g_out));
    CHECK(t, json_w_init(&w, &g_b));
    json_w_begin_object(&w);
    g_in[n++] = '{';
    for (i = 0; i < K_WIDE; i++) {
        int64_t v = (int64_t)i - 100000;
        hex_key(key, 0, i);
        json_w_key(&w, S(key, 6));
        json_w_i64(&w, v);
        n += (size_t)snprintf(g_in + n, 40, "%s\"%s\":%lld", i == 0 ? "" : ",", key, (long long)v);
    }
    json_w_key(&w, S("s", 1));
    n = put_lit(n, ",\"s\":\"");
    json_w_string(&w, S("", 0));
    json_w_end_object(&w);
    n = put_lit(n, "\"}");
    CHECK(t, json_w_done(&w));
    CHECK_EQ_U64(t, core_buf_len(&g_b), n);
    CHECK_EQ_MEM(t, g_out, core_buf_len(&g_b), g_in, n);
    /* Long escaped string through the writer: 100000 x ("\n" + 8 plain bytes). */
    CHECK(t, core_buf_init(&g_b, g_out, sizeof g_out));
    CHECK(t, json_w_init(&w, &g_b));
    n = 0;
    for (u = 0; u < UNITS; u++) {
        memcpy(g_in + n, "\nabcdefgh", 9);
        n += 9;
    }
    json_w_string(&w, S(g_in, n));
    CHECK(t, json_w_done(&w));
    CHECK_EQ_U64(t, core_buf_len(&g_b), UNITS * 14u + 2u);
    CHECK_EQ_MEM(t, g_out + 1, 14, "\\u000aabcdefgh", 14);
    CHECK_EQ_MEM(t, g_out + 1 + (UNITS - 1) * 14u, 14, "\\u000aabcdefgh", 14);
}

CTEST_SUITE("json_complexity", { "wide_distinct", t_wide_distinct }, { "wide_repeat_last", t_wide_repeat_last },
            { "prefix_keys", t_prefix_keys }, { "collide_distinct", t_collide_distinct },
            { "collide_repeat_last", t_collide_repeat_last }, { "nested_cap", t_nested_cap },
            { "nested_over_cap", t_nested_over_cap }, { "nested_arrays", t_nested_arrays },
            { "escape_string", t_escape_string }, { "escape_key", t_escape_key },
            { "escape_over_limit", t_escape_over_limit }, { "array_at_element_cap", t_array_at_element_cap },
            { "array_over_element_cap", t_array_over_element_cap }, { "long_number", t_long_number },
            { "writer_wide", t_writer_wide })
