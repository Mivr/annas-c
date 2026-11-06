#include "core/str.h"

/* Stubs: the implementation task (A002) replaces these. */
core_str_t core_str_from_cstr(const char *c) {
    core_str_t r = {NULL, 0};
    (void)c;
    return r;
}

bool core_str_eq(core_str_t a, core_str_t b) {
    (void)a;
    (void)b;
    return false;
}

bool core_str_starts_with(core_str_t s, core_str_t prefix) {
    (void)s;
    (void)prefix;
    return false;
}

size_t core_str_find(core_str_t hay, core_str_t needle) {
    (void)hay;
    (void)needle;
    return 0;
}

core_str_t core_str_trim(core_str_t s) {
    core_str_t r = {NULL, 0};
    (void)s;
    return r;
}

bool core_str_split_once(core_str_t s, core_str_t sep, core_str_t *head, core_str_t *tail) {
    (void)s;
    (void)sep;
    (void)head;
    (void)tail;
    return false;
}
