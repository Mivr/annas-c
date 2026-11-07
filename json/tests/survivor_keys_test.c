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


/* ---- 1..200 keys in six insertion orders: the key index must accept every distinct key, refuse every repeat
 * at its quote, and leave the parsed members in source order ---- */

#define TREE_MAX 200u
#define KEY_BYTES 80u
#define TREE_DOC 32768u
#define VAL_MARK 9

static char g_keys[TREE_MAX][KEY_BYTES];
static size_t g_klen[TREE_MAX];
static size_t g_order[TREE_MAX];
static size_t g_end[TREE_MAX + 1u];
static char g_tdoc[TREE_DOC];
static char g_probe[TREE_DOC];
static char g_cand[KEY_BYTES];

/* family 0: decimal "12"; 1: fixed width "k012"; 2: runs of one letter "a", "b", "c", "aa", ... (length up to 67) */
static void make_keys(size_t n, unsigned family) {
    size_t i, r;
    for (i = 0; i < n; i++) {
        switch (family) {
        case 0: g_klen[i] = (size_t)snprintf(g_keys[i], KEY_BYTES, "%zu", i); break;
        case 1: g_klen[i] = (size_t)snprintf(g_keys[i], KEY_BYTES, "k%03zu", i); break;
        default:
            g_klen[i] = i / 3 + 1;
            for (r = 0; r < g_klen[i]; r++) {
                g_keys[i][r] = (char)('a' + i % 3);
            }
            g_keys[i][g_klen[i]] = '\0';
            break;
        }
    }
}

static void make_order(size_t n, unsigned pattern) {
    size_t i;
    long lo = (long)(n - 1u) / 2, hi = lo + 1;
    uint32_t x = pattern == 4 ? 12345u : 987654321u;
    for (i = 0; i < n; i++) {
        switch (pattern) {
        case 0: g_order[i] = i; break;
        case 1: g_order[i] = n - 1u - i; break;
        case 2: g_order[i] = (i % 2 == 0) ? i / 2 : n - 1u - i / 2; break;
        case 3:
            if ((i % 2 == 0 && lo >= 0) || hi >= (long)n) {
                g_order[i] = (size_t)lo--;
            } else {
                g_order[i] = (size_t)hi++;
            }
            break;
        default: g_order[i] = i; break;
        }
    }
    if (pattern >= 4) {
        for (i = n - 1u; i > 0; i--) {
            size_t k, tmp;
            x = x * 1664525u + 1013904223u;
            k = (size_t)((x >> 8) % (i + 1u));
            tmp = g_order[i];
            g_order[i] = g_order[k];
            g_order[k] = tmp;
        }
    }
}

/* g_tdoc = `{"<key>":<index>,...` (no closing brace); g_end[k] = length of the first k members. */
static size_t build_tree_doc(size_t n) {
    size_t i, len = 0;
    g_tdoc[len++] = '{';
    g_end[0] = len;
    for (i = 0; i < n; i++) {
        len += (size_t)snprintf(g_tdoc + len, TREE_DOC - len, "%s\"%s\":%zu", i ? "," : "", g_keys[g_order[i]],
                                g_order[i]);
        g_end[i + 1u] = len;
    }
    return len;
}

static bool in_prefix(size_t k, const char *s, size_t len) {
    size_t i;
    for (i = 0; i < k; i++) {
        size_t id = g_order[i];
        if (g_klen[id] == len && memcmp(g_keys[id], s, len) == 0) {
            return true;
        }
    }
    return false;
}

/* The first k members, then one more member named cand (cand has no quote or backslash). A key already among
 * the k is refused at its quote; any other must be accepted and appended as member k. */
static void probe(ctest_t *t, size_t k, const char *cand, size_t clen) {
    const json_value_t *root;
    size_t off, len = g_end[k];
    memcpy(g_probe, g_tdoc, len);
    g_probe[len++] = ',';
    g_probe[len++] = '"';
    memcpy(g_probe + len, cand, clen);
    len += clen;
    len += (size_t)snprintf(g_probe + len, TREE_DOC - len, "\":%d}", VAL_MARK);
    if (in_prefix(k, cand, clen)) {
        expect_err(t, (core_str_t){g_probe, len}, NULL, JSON_E_SYNTAX, g_end[k] + 1u);
        return;
    }
    CHECK_EQ_I64(t, run((core_str_t){g_probe, len}, NULL, &root, &off), JSON_OK);
    CHECK_EQ_U64(t, off, 0);
    if (root == NULL || root == SENTINEL) {
        return;
    }
    CHECK_EQ_U64(t, json_len(root), k + 1u);
    CHECK(t, core_str_eq(json_key_at(root, k), (core_str_t){cand, clen}));
}

/* A key that is not a member, or that is one, by a rotating edit of member id's key. */
static size_t near_miss(size_t id, size_t kind) {
    size_t len = g_klen[id];
    memcpy(g_cand, g_keys[id], len);
    switch (kind % 5u) {
    case 0: g_cand[len - 1u] = (char)(g_cand[len - 1u] + 1); break; /* last byte + 1: may be another member */
    case 1: g_cand[0] = '!'; break;                                 /* first byte changed */
    case 2: len -= 1u; break;                                       /* proper prefix (may be empty) */
    case 3: g_cand[len++] = '0'; break;                             /* extension (may be a member) */
    default: g_cand[len++] = '!'; break;                            /* extension, never a member */
    }
    return len;
}

/* The whole object parses with its members in source order and each key reaches its own value. */
static void check_tree_object(ctest_t *t, size_t n) {
    const json_value_t *root;
    size_t off, len, j;
    len = build_tree_doc(n);
    g_tdoc[len] = '}';
    CHECK_EQ_I64(t, run((core_str_t){g_tdoc, len + 1u}, NULL, &root, &off), JSON_OK);
    CHECK_EQ_U64(t, off, 0);
    if (root == NULL || root == SENTINEL) {
        return;
    }
    CHECK_EQ_U64(t, json_len(root), n);
    for (j = 0; j < n; j++) {
        size_t id = g_order[j];
        int64_t v = -1;
        CHECK(t, core_str_eq(json_key_at(root, j), (core_str_t){g_keys[id], g_klen[id]}));
        CHECK(t, json_obj_get(root, (core_str_t){g_keys[id], g_klen[id]}) == json_at(root, j));
        CHECK(t, json_get_i64(json_at(root, j), &v));
        CHECK_EQ_I64(t, v, (int64_t)id);
    }
}

/* Every repeat against the full tree, a near-miss per member, and a few shorter prefixes. */
static void check_tree_probes(ctest_t *t, size_t n) {
    size_t j, ks[3], q;
    (void)build_tree_doc(n);
    for (j = 0; j < n; j++) {
        size_t id = g_order[j], len;
        probe(t, n, g_keys[id], g_klen[id]);
        len = near_miss(id, j);
        probe(t, n, g_cand, len);
    }
    ks[0] = 1u;
    ks[1] = n / 2u > 0 ? n / 2u : 1u;
    ks[2] = n > 1u ? n - 1u : 1u;
    for (q = 0; q < 3u; q++) {
        size_t k = ks[q];
        probe(t, k, g_keys[g_order[0]], g_klen[g_order[0]]);
        probe(t, k, g_keys[g_order[k - 1u]], g_klen[g_order[k - 1u]]);
        probe(t, k, g_keys[g_order[k / 2u]], g_klen[g_order[k / 2u]]);
        j = near_miss(g_order[k - 1u], k);
        probe(t, k, g_cand, j);
        j = near_miss(g_order[k / 2u], k + 1u);
        probe(t, k, g_cand, j);
    }
}

static void tree_sizes(ctest_t *t, unsigned first_pattern, unsigned last_pattern) {
    size_t n;
    unsigned pattern;
    for (n = 1; n <= TREE_MAX; n++) {
        make_keys(n, (unsigned)(n % 3u));
        for (pattern = first_pattern; pattern <= last_pattern; pattern++) {
            make_order(n, pattern);
            check_tree_object(t, n);
            check_tree_probes(t, n);
        }
    }
}

static void t_tree_ascending_and_descending(ctest_t *t) { tree_sizes(t, 0, 1); }
static void t_tree_alternating_and_zigzag(ctest_t *t) { tree_sizes(t, 2, 3); }
static void t_tree_seeded_random(ctest_t *t) { tree_sizes(t, 4, 5); }

/* Every prefix of the insertions is probed (a repeat of a member chosen by the prefix length, and a near-miss),
 * so a tree repaired wrongly at any single insertion is looked at before the next one hides it. */
static void t_tree_every_prefix(ctest_t *t) {
    static const size_t sizes[] = {5, 8, 13, 31, 32, 33, 50, 63, 64, 65, 100, 127, 128, 129, 200};
    size_t s, k, j;
    unsigned pattern;
    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        size_t n = sizes[s];
        make_keys(n, (unsigned)(s % 3u));
        for (pattern = 0; pattern < 6u; pattern++) {
            make_order(n, pattern);
            (void)build_tree_doc(n);
            for (k = 1; k <= n; k++) {
                size_t id = g_order[(k * 7u + 3u) % k];
                probe(t, k, g_keys[id], g_klen[id]);
                id = g_order[k - 1u];
                probe(t, k, g_keys[id], g_klen[id]);
                j = near_miss(id, k);
                probe(t, k, g_cand, j);
            }
        }
    }
}

/* Every repeat of every prefix, for objects small enough to afford it. */
static void t_tree_every_repeat_of_every_prefix(ctest_t *t) {
    size_t n, k, j;
    unsigned pattern;
    for (n = 1; n <= 24u; n++) {
        make_keys(n, (unsigned)(n % 3u));
        for (pattern = 0; pattern < 6u; pattern++) {
            make_order(n, pattern);
            (void)build_tree_doc(n);
            for (k = 1; k <= n; k++) {
                for (j = 0; j < k; j++) {
                    probe(t, k, g_keys[g_order[j]], g_klen[g_order[j]]);
                }
                for (j = 0; j < 5u; j++) {
                    size_t len = near_miss(g_order[k - 1u], j);
                    probe(t, k, g_cand, len);
                }
            }
        }
    }
}

CTEST_SUITE("survivor_keys",
    {"repeated_key_every_order_of_seven", t_repeated_key_every_order_of_seven},
    {"repeated_key_every_order_of_mixed_lengths", t_repeated_key_every_order_of_mixed_lengths},
    {"repeated_key_in_long_objects", t_repeated_key_in_long_objects},
    {"repeated_key_is_per_object", t_repeated_key_is_per_object},
    {"tree_ascending_and_descending", t_tree_ascending_and_descending},
    {"tree_alternating_and_zigzag", t_tree_alternating_and_zigzag},
    {"tree_seeded_random", t_tree_seeded_random},
    {"tree_every_prefix", t_tree_every_prefix},
    {"tree_every_repeat_of_every_prefix", t_tree_every_repeat_of_every_prefix})
