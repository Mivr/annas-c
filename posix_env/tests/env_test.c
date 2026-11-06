/* posix_env: the ports_env_t adapter over an envp snapshot, only through its port. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ctest/ctest.h"
#include "posix_env/posix_env.h"
#include "ports/ports.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})

extern char **environ;

static posix_env_t g_env;

static bool get(ports_env_t p, core_str_t name, core_str_t *v) { return p.ops->get(p.ctx, name, v); }

static ports_env_t over(ctest_t *t, char *const *envp) {
    CHECK(t, posix_env_init(&g_env, envp));
    return posix_env_port(&g_env);
}

static void t_init_validates_arguments(ctest_t *t) {
    static char a[] = "A=1";
    static char *const envp[] = {a, NULL};
    static char *const empty[] = {NULL};
    posix_env_t e;
    memset(&e, 0, sizeof e);
    CHECK(t, !posix_env_init(NULL, envp));
    CHECK(t, !posix_env_init(&e, NULL));
    CHECK(t, posix_env_init(&e, envp));
    CHECK(t, posix_env_init(&e, empty));
}

static void t_finds_values_and_points_into_the_snapshot(ctest_t *t) {
    static char a[] = "HOME=/home/me";
    static char b[] = "PATH=/bin:/usr/bin";
    static char *const envp[] = {a, b, NULL};
    ports_env_t p = over(t, envp);
    core_str_t v = {NULL, 99};
    CHECK(t, get(p, LIT("HOME"), &v));
    CHECK(t, core_str_eq(v, LIT("/home/me")));
    CHECK(t, v.ptr == a + 5); /* zero copy */
    CHECK(t, get(p, LIT("PATH"), &v));
    CHECK(t, core_str_eq(v, LIT("/bin:/usr/bin")));
    CHECK(t, v.ptr == b + 5);
    CHECK(t, p.ops == posix_env_port(&g_env).ops && p.ctx == (void *)&g_env);
}

static void t_not_found_clears_the_output(ctest_t *t) {
    static char a[] = "HOME=/h";
    static char *const envp[] = {a, NULL};
    ports_env_t p = over(t, envp);
    core_str_t v = {a, 7};
    CHECK(t, !get(p, LIT("USER"), &v));
    CHECK(t, v.ptr == NULL && v.len == 0);
    v = (core_str_t){a, 7};
    CHECK(t, get(p, LIT("HOME"), &v)); /* control */
    CHECK_EQ_U64(t, v.len, 2);
}

static void t_empty_value_is_still_found(ctest_t *t) {
    static char a[] = "EMPTY=";
    static char b[] = "NEXT=x";
    static char *const envp[] = {a, b, NULL};
    ports_env_t p = over(t, envp);
    core_str_t v = {a, 9};
    CHECK(t, get(p, LIT("EMPTY"), &v));
    CHECK_EQ_U64(t, v.len, 0);
    CHECK(t, get(p, LIT("NEXT"), &v));
    CHECK_EQ_U64(t, v.len, 1);
}

static void t_names_are_case_sensitive_and_exact(ctest_t *t) {
    static char a[] = "Path=lower";
    static char b[] = "HOME=/h";
    static char c[] = "HOMEDIR=/hd";
    static char *const envp[] = {a, b, c, NULL};
    ports_env_t p = over(t, envp);
    core_str_t v;
    CHECK(t, get(p, LIT("Path"), &v) && core_str_eq(v, LIT("lower")));
    CHECK(t, !get(p, LIT("PATH"), &v));
    CHECK(t, !get(p, LIT("path"), &v));
    CHECK(t, !get(p, LIT("HOM"), &v));    /* a prefix of an entry name */
    CHECK(t, !get(p, LIT("HOMEX"), &v));  /* longer than the entry name */
    CHECK(t, !get(p, LIT("HOME="), &v));  /* includes the separator */
    CHECK(t, !get(p, LIT("HOME=/"), &v)); /* tries to reach into the value */
    CHECK(t, get(p, LIT("HOME"), &v) && core_str_eq(v, LIT("/h")));
    CHECK(t, get(p, LIT("HOMEDIR"), &v) && core_str_eq(v, LIT("/hd")));
}

static void t_value_may_contain_equals_signs(ctest_t *t) {
    static char a[] = "OPTS=a=b=c";
    static char b[] = "A=B=C";
    static char *const envp[] = {a, b, NULL};
    ports_env_t p = over(t, envp);
    core_str_t v;
    CHECK(t, get(p, LIT("OPTS"), &v) && core_str_eq(v, LIT("a=b=c")));
    CHECK(t, get(p, LIT("A"), &v) && core_str_eq(v, LIT("B=C")));
    CHECK(t, !get(p, LIT("A=B"), &v)); /* a name with '=' can never match */
    CHECK(t, !get(p, LIT("A=B=C"), &v));
    CHECK(t, !get(p, LIT("OPTS=a"), &v));
}

static void t_first_duplicate_wins_and_lookalikes_do_not_stop_the_scan(ctest_t *t) {
    static char a[] = "AB=1";
    static char b[] = "A=first";
    static char c[] = "A=second";
    static char d[] = "A";
    static char *const envp[] = {a, d, b, c, NULL};
    ports_env_t p = over(t, envp);
    core_str_t v;
    CHECK(t, get(p, LIT("A"), &v) && core_str_eq(v, LIT("first")));
    CHECK(t, get(p, LIT("AB"), &v) && core_str_eq(v, LIT("1")));
}

static void t_entries_without_equals_or_with_empty_names_are_ignored(ctest_t *t) {
    static char a[] = "NOEQ";
    static char b[] = "=novalue-name";
    static char c[] = "";
    static char d[] = "NOEQ=real";
    static char e[] = "=";
    static char *const envp[] = {a, b, c, e, d, NULL};
    ports_env_t p = over(t, envp);
    core_str_t v = {a, 1};
    CHECK(t, get(p, LIT("NOEQ"), &v) && core_str_eq(v, LIT("real"))); /* the bare "NOEQ" is not a binding */
    CHECK(t, !get(p, (core_str_t){"", 0}, &v));                       /* an empty name is never found */
    CHECK(t, v.ptr == NULL && v.len == 0);
    CHECK(t, !get(p, (core_str_t){NULL, 0}, &v));
    CHECK(t, !get(p, LIT("=novalue-name"), &v));
    CHECK(t, !get(p, LIT("="), &v));
}

static void t_bare_entry_alone_is_not_set(ctest_t *t) {
    static char a[] = "ONLYNAME";
    static char *const envp[] = {a, NULL};
    ports_env_t p = over(t, envp);
    core_str_t v;
    CHECK(t, !get(p, LIT("ONLYNAME"), &v));
    CHECK(t, !get(p, LIT("ONLYNAM"), &v));
    CHECK(t, !get(p, LIT("ONLYNAME="), &v));
}

static void t_degenerate_names_and_outputs(ctest_t *t) {
    static char a[] = "K=v";
    static char *const envp[] = {a, NULL};
    ports_env_t p = over(t, envp);
    core_str_t v = {a, 5};
    CHECK(t, get(p, LIT("K"), &v)); /* control */
    v = (core_str_t){a, 5};
    CHECK(t, !get(p, (core_str_t){NULL, 1}, &v)); /* NULL ptr with len > 0 */
    CHECK(t, v.ptr == NULL && v.len == 0);
    v = (core_str_t){a, 5};
    CHECK(t, !get(p, (core_str_t){"K\0", 2}, &v)); /* embedded NUL never matches "K=" */
    CHECK(t, v.ptr == NULL && v.len == 0);
    CHECK(t, !get(p, (core_str_t){"K\0x", 3}, &v));
    CHECK(t, !get(p, LIT("K"), NULL)); /* NULL value pointer: false, no crash, even though K is set */
}

static void t_empty_environment_and_null_object(ctest_t *t) {
    static char *const empty[] = {NULL};
    ports_env_t p = over(t, empty);
    core_str_t v = {empty[0], 4};
    CHECK(t, !get(p, LIT("A"), &v));
    CHECK(t, v.ptr == NULL && v.len == 0);
    ports_env_t q = posix_env_port(NULL);
    CHECK(t, q.ops != NULL && q.ops->get != NULL);
    v = (core_str_t){"zz", 2};
    CHECK(t, !get(q, LIT("A"), &v));
    CHECK(t, v.ptr == NULL && v.len == 0);
    CHECK(t, !get(q, LIT("A"), NULL));
}

static void t_long_snapshot_and_long_values(ctest_t *t) {
    static char store[300][24];
    static char big[4100];
    static char *envp[302];
    for (size_t i = 0; i < 300; i++) {
        snprintf(store[i], sizeof store[i], "V%zu=%zu", i, i * 3u);
        envp[i] = store[i];
    }
    memcpy(big, "BIG=", 4);
    memset(big + 4, 'z', sizeof big - 5u);
    envp[300] = big;
    envp[301] = NULL;
    ports_env_t p = over(t, envp);
    core_str_t v;
    CHECK(t, get(p, LIT("V299"), &v) && core_str_eq(v, LIT("897")));
    CHECK(t, get(p, LIT("V0"), &v) && core_str_eq(v, LIT("0")));
    CHECK(t, get(p, LIT("V150"), &v) && core_str_eq(v, LIT("450")));
    CHECK(t, !get(p, LIT("V300"), &v));
    CHECK(t, get(p, LIT("BIG"), &v));
    CHECK_EQ_U64(t, v.len, sizeof big - 5u);
    CHECK(t, v.ptr[0] == 'z' && v.ptr[v.len - 1u] == 'z');
}

static void t_snapshot_changes_are_seen_because_it_is_borrowed(ctest_t *t) {
    static char a[] = "K=one";
    static char *envp[] = {a, NULL};
    ports_env_t p = over(t, envp);
    core_str_t v;
    CHECK(t, get(p, LIT("K"), &v) && core_str_eq(v, LIT("one")));
    a[2] = 't'; /* borrowed, not copied */
    CHECK(t, get(p, LIT("K"), &v) && core_str_eq(v, LIT("tne")));
}

static void t_real_process_environment(ctest_t *t) {
    CHECK_EQ_I64(t, setenv("POSIX_ENV_TEST_A", "alpha=beta", 1), 0);
    CHECK_EQ_I64(t, setenv("POSIX_ENV_TEST_EMPTY", "", 1), 0);
    ports_env_t p = over(t, environ);
    core_str_t v;
    CHECK(t, get(p, LIT("POSIX_ENV_TEST_A"), &v) && core_str_eq(v, LIT("alpha=beta")));
    CHECK(t, get(p, LIT("POSIX_ENV_TEST_EMPTY"), &v) && v.len == 0);
    CHECK(t, !get(p, LIT("POSIX_ENV_TEST_UNSET"), &v));
    CHECK(t, !get(p, LIT("posix_env_test_a"), &v));
    const char *path = getenv("PATH");
    if (path != NULL) { /* agrees with libc for a variable that is normally set */
        CHECK(t, get(p, LIT("PATH"), &v) && v.len == strlen(path) && memcmp(v.ptr, path, v.len) == 0);
    }
}

CTEST_SUITE("posix_env",
    {"init_validates_arguments", t_init_validates_arguments},
    {"finds_values_and_points_into_the_snapshot", t_finds_values_and_points_into_the_snapshot},
    {"not_found_clears_the_output", t_not_found_clears_the_output},
    {"empty_value_is_still_found", t_empty_value_is_still_found},
    {"names_are_case_sensitive_and_exact", t_names_are_case_sensitive_and_exact},
    {"value_may_contain_equals_signs", t_value_may_contain_equals_signs},
    {"first_duplicate_wins_and_lookalikes_do_not_stop_the_scan", t_first_duplicate_wins_and_lookalikes_do_not_stop_the_scan},
    {"entries_without_equals_or_with_empty_names_are_ignored", t_entries_without_equals_or_with_empty_names_are_ignored},
    {"bare_entry_alone_is_not_set", t_bare_entry_alone_is_not_set},
    {"degenerate_names_and_outputs", t_degenerate_names_and_outputs},
    {"empty_environment_and_null_object", t_empty_environment_and_null_object},
    {"long_snapshot_and_long_values", t_long_snapshot_and_long_values},
    {"snapshot_changes_are_seen_because_it_is_borrowed", t_snapshot_changes_are_seen_because_it_is_borrowed},
    {"real_process_environment", t_real_process_environment})
