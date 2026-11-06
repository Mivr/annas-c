/* Tests for ports/fakes.h: the table env, used only through ports_env_t. */
#include <stdint.h>
#include <string.h>

#include "ctest/ctest.h"
#include "ports/fakes.h"
#include "ports/ports.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define S(s) {(s), sizeof(s) - 1}
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const ports_fake_env_entry_t k_table[] = {
    {S("ANNAS_BASE_URL"), S("annas-archive.gl")},
    {S("ANNAS_SECRET_KEY"), S("")},
    {S("HOME"), S("/home/x")},
    {S("HOME"), S("/second")},
    {S("Path"), S("a\0b")},
};

static const char k_nul_value[] = {'a', '\0', 'b'};
static const char k_nul_name[] = {'H', '\0', 'M'};

static bool get(ports_env_t e, core_str_t name, core_str_t *v) { return e.ops->get(e.ctx, name, v); }

static ports_env_t table_env(ports_fake_env_t *fe, ctest_t *t) {
    CHECK(t, ports_fake_env_init(fe, k_table, COUNT(k_table)));
    return ports_fake_env_port(fe);
}

static void t_found_returns_value(ctest_t *t) {
    ports_fake_env_t fe;
    ports_env_t e = table_env(&fe, t);
    core_str_t v = {NULL, 99};
    CHECK(t, get(e, LIT("ANNAS_BASE_URL"), &v));
    CHECK(t, core_str_eq(v, LIT("annas-archive.gl")));
    CHECK(t, get(e, LIT("HOME"), &v));
    CHECK(t, core_str_eq(v, LIT("/home/x")));
}

static void t_not_found_clears_value(ctest_t *t) {
    ports_fake_env_t fe;
    ports_env_t e = table_env(&fe, t);
    core_str_t v = {"junk", 4};
    CHECK(t, !get(e, LIT("NOPE"), &v));
    CHECK(t, v.ptr == NULL && v.len == 0);
    v = LIT("junk");
    CHECK(t, get(e, LIT("HOME"), &v)); /* positive control */
    CHECK(t, v.len == 7);
}

static void t_set_but_empty_is_found(ctest_t *t) {
    ports_fake_env_t fe;
    ports_env_t e = table_env(&fe, t);
    core_str_t v = {"junk", 4};
    CHECK(t, get(e, LIT("ANNAS_SECRET_KEY"), &v));
    CHECK_EQ_U64(t, v.len, 0);
    CHECK(t, !get(e, LIT("ANNAS_SECRET_KEY_"), &v)); /* control: a different name is not found */
}

static void t_empty_name_is_never_found(ctest_t *t) {
    ports_fake_env_t fe;
    ports_env_t e = table_env(&fe, t);
    core_str_t v = {"junk", 4};
    CHECK(t, !get(e, (core_str_t){NULL, 0}, &v));
    CHECK(t, v.ptr == NULL && v.len == 0);
    CHECK(t, !get(e, LIT(""), &v));
    CHECK(t, get(e, LIT("Path"), &v)); /* positive control */
}

static void t_names_are_exact_and_case_sensitive(ctest_t *t) {
    ports_fake_env_t fe;
    ports_env_t e = table_env(&fe, t);
    core_str_t v;
    CHECK(t, !get(e, LIT("home"), &v));
    CHECK(t, !get(e, LIT("PATH"), &v));
    CHECK(t, !get(e, LIT("ANNAS"), &v));              /* prefix of a stored name */
    CHECK(t, !get(e, LIT("ANNAS_BASE_URL2"), &v));    /* stored name is a prefix of the query */
    CHECK(t, !get(e, LIT("ANNAS_BASE_UR"), &v));
    CHECK(t, !get(e, LIT(" HOME"), &v));
    CHECK(t, get(e, LIT("Path"), &v));                /* positive control */
    CHECK(t, get(e, LIT("HOME"), &v));
}

static void t_first_duplicate_wins(ctest_t *t) {
    ports_fake_env_t fe;
    ports_env_t e = table_env(&fe, t);
    core_str_t v;
    CHECK(t, get(e, LIT("HOME"), &v));
    CHECK(t, core_str_eq(v, LIT("/home/x")));
    CHECK(t, !core_str_eq(v, LIT("/second")));
}

static void t_embedded_nul_is_part_of_value_and_name(ctest_t *t) {
    ports_fake_env_t fe;
    ports_env_t e = table_env(&fe, t);
    core_str_t v = {NULL, 0};
    CHECK(t, get(e, LIT("Path"), &v));
    CHECK_EQ_U64(t, v.len, 3);
    CHECK(t, v.len == 3 && memcmp(v.ptr, k_nul_value, 3) == 0);
    /* "H\0M" is not "HOME" nor "H" */
    CHECK(t, !get(e, (core_str_t){k_nul_name, 3}, &v));
    CHECK(t, !get(e, LIT("H"), &v));
}

static void t_null_value_pointer_is_not_found(ctest_t *t) {
    ports_fake_env_t fe;
    ports_env_t e = table_env(&fe, t);
    core_str_t v;
    CHECK(t, !get(e, LIT("HOME"), NULL));
    CHECK(t, get(e, LIT("HOME"), &v)); /* positive control: same query with an out slot */
}

static void t_lookups_are_repeatable(ctest_t *t) {
    ports_fake_env_t fe;
    ports_env_t e = table_env(&fe, t);
    core_str_t a;
    core_str_t b;
    CHECK(t, get(e, LIT("HOME"), &a));
    CHECK(t, get(e, LIT("ANNAS_BASE_URL"), &b));
    CHECK(t, get(e, LIT("HOME"), &b));
    CHECK(t, core_str_eq(a, b));
    CHECK(t, a.ptr == b.ptr); /* the same slice, valid for the life of the environment */
}

static void t_empty_environment(ctest_t *t) {
    ports_fake_env_t fe;
    core_str_t v = {"junk", 4};
    CHECK(t, ports_fake_env_init(&fe, NULL, 0));
    ports_env_t e = ports_fake_env_port(&fe);
    CHECK(t, !get(e, LIT("HOME"), &v));
    CHECK(t, v.ptr == NULL && v.len == 0);
    CHECK(t, ports_fake_env_init(&fe, k_table, COUNT(k_table))); /* control: the same object now serves */
    CHECK(t, get(ports_fake_env_port(&fe), LIT("HOME"), &v));
}

static void t_init_rejects_bad_arguments(ctest_t *t) {
    ports_fake_env_t fe;
    static const ports_fake_env_entry_t empty_name[] = {{S("A"), S("1")}, {S(""), S("2")}};
    static const ports_fake_env_entry_t null_name[] = {{{NULL, 3}, S("2")}};
    static const ports_fake_env_entry_t null_value[] = {{S("A"), {NULL, 2}}};
    static const ports_fake_env_entry_t ok_empty_value[] = {{S("A"), {NULL, 0}}};
    CHECK(t, ports_fake_env_init(&fe, k_table, COUNT(k_table)));      /* positive controls first */
    CHECK(t, ports_fake_env_init(&fe, ok_empty_value, 1));
    CHECK(t, !ports_fake_env_init(NULL, k_table, COUNT(k_table)));
    CHECK(t, !ports_fake_env_init(&fe, NULL, 1));
    CHECK(t, !ports_fake_env_init(&fe, empty_name, 2));
    CHECK(t, !ports_fake_env_init(&fe, null_name, 1));
    CHECK(t, !ports_fake_env_init(&fe, null_value, 1));
}

static void t_port_is_a_const_table_with_this_context(ctest_t *t) {
    ports_fake_env_t fe;
    ports_env_t e = table_env(&fe, t);
    CHECK(t, e.ops != NULL && e.ops->get != NULL);
    CHECK(t, e.ctx == (void *)&fe);
}

CTEST_SUITE("env",
    {"found_returns_value", t_found_returns_value},
    {"not_found_clears_value", t_not_found_clears_value},
    {"set_but_empty_is_found", t_set_but_empty_is_found},
    {"empty_name_is_never_found", t_empty_name_is_never_found},
    {"names_are_exact_and_case_sensitive", t_names_are_exact_and_case_sensitive},
    {"first_duplicate_wins", t_first_duplicate_wins},
    {"embedded_nul_is_part_of_value_and_name", t_embedded_nul_is_part_of_value_and_name},
    {"null_value_pointer_is_not_found", t_null_value_pointer_is_not_found},
    {"lookups_are_repeatable", t_lookups_are_repeatable},
    {"empty_environment", t_empty_environment},
    {"init_rejects_bad_arguments", t_init_rejects_bad_arguments},
    {"port_is_a_const_table_with_this_context", t_port_is_a_const_table_with_this_context})
