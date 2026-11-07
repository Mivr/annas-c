/* Tests for json/json.h (A071): the key index is observed through its results, one insertion order per test.
 *
 * Each test parses an object whose members arrive in one fixed order (3-byte keys "k00", "k01", ...), then checks
 * that every member is found by json_obj_get, that every key repeated at the end is refused at its quote, and that
 * keys that are not members are accepted. An index repaired wrongly (a rotation that loses a subtree, a double
 * rotation in the wrong direction) answers one of these wrongly. Each order is its own test, so a crash in one order
 * cannot hide the failed checks of another. Orders of 3 to 9 members are enough for every rotation shape: the
 * first single and double rotations at the root, and at a node below the root. Public header only. */
#include <stdio.h>
#include <string.h>

#include "core/arena.h"
#include "core/str.h"
#include "ctest/ctest.h"
#include "json/json.h"

#define OFF_MARK 12345u
#define MAXN 9u

static unsigned char g_mem[1u << 16];
static core_arena_t g_a;
static int g_sentinel;
#define SENTINEL ((const json_value_t *)(const void *)&g_sentinel)

static json_err_t run(const char *s, size_t n, const json_value_t **root, size_t *off) {
    if (!core_arena_init(&g_a, g_mem, sizeof g_mem)) {
        return JSON_E_ARG;
    }
    *root = SENTINEL;
    *off = OFF_MARK;
    return json_parse(&g_a, (core_str_t){s, n}, NULL, root, off);
}

static void key_text(char *dst, unsigned id) { (void)snprintf(dst, 4, "k%02u", id); }

/* `{"k<a>":0,"k<b>":1,...` without the closing brace; returns the length. */
static size_t build(char *doc, size_t cap, const unsigned *order, size_t n) {
    size_t i, len = 0;
    char key[4];
    doc[len++] = '{';
    for (i = 0; i < n; i++) {
        key_text(key, order[i]);
        len += (size_t)snprintf(doc + len, cap - len, "%s\"%s\":%u", i ? "," : "", key, order[i]);
    }
    return len;
}

static void check_order(ctest_t *t, const unsigned *order, size_t n) {
    char doc[256], key[4];
    const json_value_t *root;
    size_t off, len, j, extra;
    static const char *const k_absent[] = {"z"}; /* shorter than every member key */
    len = build(doc, sizeof doc, order, n);
    doc[len] = '}';
    CHECK_EQ_I64(t, run(doc, len + 1u, &root, &off), JSON_OK);
    CHECK_EQ_U64(t, off, 0);
    if (root == NULL || root == SENTINEL) {
        return;
    }
    CHECK_EQ_U64(t, json_len(root), n);
    for (j = 0; j < n; j++) {
        key_text(key, order[j]);
        CHECK(t, json_obj_get(root, core_str_from_cstr(key)) == json_at(root, j));
    }
    for (j = 0; j < n; j++) {
        key_text(key, order[j]);
        extra = (size_t)snprintf(doc + len, sizeof doc - len, ",\"%s\":9}", key);
        CHECK_EQ_I64(t, run(doc, len + extra, &root, &off), JSON_E_SYNTAX);
        CHECK_EQ_U64(t, off, len + 1u);
        CHECK(t, root == NULL);
    }
    for (j = 0; j < sizeof k_absent / sizeof k_absent[0]; j++) {
        extra = (size_t)snprintf(doc + len, sizeof doc - len, ",\"%s\":9}", k_absent[j]);
        CHECK_EQ_I64(t, run(doc, len + extra, &root, &off), JSON_OK);
        if (root != NULL && root != SENTINEL) {
            CHECK_EQ_U64(t, json_len(root), n + 1u);
        }
    }
}

#define ORDER_TEST(name, ...)                                         \
    static void name(ctest_t *t) {                                    \
        static const unsigned k_order[] = {__VA_ARGS__};              \
        check_order(t, k_order, sizeof k_order / sizeof k_order[0]);  \
    }

ORDER_TEST(t_order_10234, 1, 0, 2, 3, 4)
ORDER_TEST(t_order_10243, 1, 0, 2, 4, 3)
ORDER_TEST(t_order_10423, 1, 0, 4, 2, 3)
ORDER_TEST(t_order_10432, 1, 0, 4, 3, 2)
ORDER_TEST(t_order_12034, 1, 2, 0, 3, 4)
ORDER_TEST(t_order_01243, 0, 1, 2, 4, 3)
ORDER_TEST(t_order_01423, 0, 1, 4, 2, 3)
ORDER_TEST(t_order_03421, 0, 3, 4, 2, 1)
ORDER_TEST(t_order_021, 0, 2, 1)
ORDER_TEST(t_order_201, 2, 0, 1)
ORDER_TEST(t_order_0213, 0, 2, 1, 3)
ORDER_TEST(t_order_0312, 0, 3, 1, 2)
ORDER_TEST(t_order_0321, 0, 3, 2, 1)
ORDER_TEST(t_order_3012, 3, 0, 1, 2)
ORDER_TEST(t_order_10647532, 1, 0, 6, 4, 7, 5, 3, 2)
ORDER_TEST(t_order_13062574, 1, 3, 0, 6, 2, 5, 7, 4)
ORDER_TEST(t_order_13052467, 1, 3, 0, 5, 2, 4, 6, 7)
ORDER_TEST(t_order_764810532, 7, 6, 4, 8, 1, 0, 5, 3, 2)
ORDER_TEST(t_order_124780635, 1, 2, 4, 7, 8, 0, 6, 3, 5)
ORDER_TEST(t_order_067184532, 0, 6, 7, 1, 8, 4, 5, 3, 2)

CTEST_SUITE("tree_order",
    {"order_10234", t_order_10234},
    {"order_10243", t_order_10243},
    {"order_10423", t_order_10423},
    {"order_10432", t_order_10432},
    {"order_12034", t_order_12034},
    {"order_01243", t_order_01243},
    {"order_01423", t_order_01423},
    {"order_03421", t_order_03421},
    {"order_021", t_order_021},
    {"order_201", t_order_201},
    {"order_0213", t_order_0213},
    {"order_0312", t_order_0312},
    {"order_0321", t_order_0321},
    {"order_3012", t_order_3012},
    {"order_10647532", t_order_10647532},
    {"order_13062574", t_order_13062574},
    {"order_13052467", t_order_13052467},
    {"order_764810532", t_order_764810532},
    {"order_124780635", t_order_124780635},
    {"order_067184532", t_order_067184532})
