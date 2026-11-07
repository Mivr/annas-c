/* Tests for json/json.h (A044): a repeated object key is found whatever the insertion order of the members.
 * Public header only. */
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

static char g_doc[16384];

/* Builds `{"k1":0,"k2":0,...` (no closing brace) from the key texts; returns the length. */
static size_t build_members(char keys[][8], const size_t *order, size_t n) {
    size_t i, len = 0;
    g_doc[len++] = '{';
    for (i = 0; i < n; i++) {
        len += (size_t)snprintf(g_doc + len, sizeof g_doc - len, "%s\"%s\":0", i ? "," : "", keys[order[i]]);
    }
    return len;
}

/* The object must parse, in source order, and every key repeated at the end must be refused at that repeat. */
static void check_object_order(ctest_t *t, char keys[][8], const size_t *order, size_t n) {
    const json_value_t *root;
    size_t off, len, j;
    len = build_members(keys, order, n);
    g_doc[len] = '}';
    CHECK_EQ_I64(t, run((core_str_t){g_doc, len + 1}, NULL, &root, &off), JSON_OK);
    CHECK_EQ_U64(t, off, 0);
    if (root == NULL || root == SENTINEL) {
        return;
    }
    CHECK_EQ_U64(t, json_len(root), n);
    for (j = 0; j < n; j++) {
        CHECK(t, core_str_eq(json_key_at(root, j), core_str_from_cstr(keys[order[j]])));
        CHECK(t, json_obj_get(root, core_str_from_cstr(keys[order[j]])) == json_at(root, j));
    }
    for (j = 0; j < n; j++) {
        size_t extra = (size_t)snprintf(g_doc + len, sizeof g_doc - len, ",\"%s\":1}", keys[j]);
        expect_err(t, (core_str_t){g_doc, len + extra}, NULL, JSON_E_SYNTAX, len + 1);
    }
}

static bool next_perm(size_t *p, size_t n) {
    size_t i = n - 1, j = n - 1, tmp;
    while (i > 0 && p[i - 1] >= p[i]) {
        i--;
    }
    if (i == 0) {
        return false;
    }
    while (p[j] <= p[i - 1]) {
        j--;
    }
    tmp = p[i - 1];
    p[i - 1] = p[j];
    p[j] = tmp;
    for (j = n - 1; i < j; i++, j--) {
        tmp = p[i];
        p[i] = p[j];
        p[j] = tmp;
    }
    return true;
}

static void t_repeated_key_every_order_of_seven(ctest_t *t) {
    char keys[7][8];
    size_t order[7], i;
    for (i = 0; i < 7; i++) {
        keys[i][0] = (char)('a' + i);
        keys[i][1] = '\0';
        order[i] = i;
    }
    do {
        check_object_order(t, keys, order, 7);
    } while (next_perm(order, 7));
}

static void t_repeated_key_every_order_of_mixed_lengths(ctest_t *t) {
    /* the 2 + 4 + 8 keys of length 1..3 over {a, b}: ordered by length first, then by bytes */
    static const char *const k_keys[] = {"a", "b", "aa", "ab", "ba", "bb", "aaa", "aab", "aba", "abb",
                                         "baa", "bab", "bba", "bbb"};
    char keys[14][8];
    size_t order[14], i, round;
    for (i = 0; i < 14; i++) {
        (void)snprintf(keys[i], sizeof keys[i], "%s", k_keys[i]);
        order[i] = i;
    }
    for (round = 0; round < 400; round++) {
        /* a deterministic shuffle (LCG), then the object and the 14 repeats */
        uint32_t x = (uint32_t)round * 2654435761u + 12345u;
        for (i = 13; i > 0; i--) {
            size_t k, tmp;
            x = x * 1664525u + 1013904223u;
            k = (size_t)((x >> 8) % (i + 1));
            tmp = order[i];
            order[i] = order[k];
            order[k] = tmp;
        }
        check_object_order(t, keys, order, 14);
    }
}

#define NKEYS 211u
static void fill_pattern(size_t *order, unsigned pattern) {
    size_t i;
    for (i = 0; i < NKEYS; i++) {
        switch (pattern) {
        case 0: order[i] = i; break;
        case 1: order[i] = NKEYS - 1 - i; break;
        case 2: order[i] = (i % 2 == 0) ? i / 2 : NKEYS - 1 - i / 2; break;
        case 3: order[i] = (i * 37u) % NKEYS; break;
        case 4: order[i] = (i * 101u + 7u) % NKEYS; break;
        default: order[i] = (i % 2 == 0) ? NKEYS / 2 - i / 2 : NKEYS / 2 + 1 + i / 2; break;
        }
    }
}

static void t_repeated_key_in_long_objects(ctest_t *t) {
    static char keys[NKEYS][8];
    static size_t order[NKEYS];
    size_t i;
    unsigned pattern;
    for (i = 0; i < NKEYS; i++) {
        (void)snprintf(keys[i], sizeof keys[i], "%zu", i);
    }
    for (pattern = 0; pattern < 6; pattern++) {
        fill_pattern(order, pattern);
        check_object_order(t, keys, order, NKEYS);
    }
}

static void t_repeated_key_is_per_object(ctest_t *t) {
    const json_value_t *root;
    size_t off;
    CHECK_EQ_I64(t, run(LIT("{\"b\":{\"a\":1,\"b\":2},\"a\":{\"b\":1,\"a\":2},\"c\":[{\"a\":1},{\"a\":2}]}"), NULL,
                        &root, &off),
                 JSON_OK);
    CHECK_EQ_U64(t, json_len(root), 3);
    expect_err(t, LIT("{\"b\":{\"a\":1},\"a\":1,\"b\":2}"), NULL, JSON_E_SYNTAX, 19);
    expect_err(t, LIT("{\"a\":{\"x\":1,\"y\":2,\"x\":3},\"x\":1}"), NULL, JSON_E_SYNTAX, 18);
}


CTEST_SUITE("survivor_keys",
    {"repeated_key_every_order_of_seven", t_repeated_key_every_order_of_seven},
    {"repeated_key_every_order_of_mixed_lengths", t_repeated_key_every_order_of_mixed_lengths},
    {"repeated_key_in_long_objects", t_repeated_key_in_long_objects},
    {"repeated_key_is_per_object", t_repeated_key_is_per_object})
