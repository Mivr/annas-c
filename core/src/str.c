#include "core/str.h"

#include <string.h>

core_str_t core_str_from_cstr(const char *c) {
    core_str_t r = {NULL, 0};
    if (c != NULL) {
        r.ptr = c;
        r.len = strlen(c);
    }
    return r;
}

bool core_str_eq(core_str_t a, core_str_t b) {
    if (a.len != b.len) {
        return false;
    }
    return a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0;
}

bool core_str_starts_with(core_str_t s, core_str_t prefix) {
    if (prefix.len == 0) {
        return true;
    }
    if (prefix.len > s.len) {
        return false;
    }
    return memcmp(s.ptr, prefix.ptr, prefix.len) == 0;
}

size_t core_str_find(core_str_t hay, core_str_t needle) {
    size_t i;
    if (needle.len == 0) {
        return 0;
    }
    if (needle.len > hay.len) {
        return CORE_NPOS;
    }
    for (i = 0; i <= hay.len - needle.len; i++) {
        if (memcmp(hay.ptr + i, needle.ptr, needle.len) == 0) {
            return i;
        }
    }
    return CORE_NPOS;
}

static bool is_ws(char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

core_str_t core_str_trim(core_str_t s) {
    core_str_t r = {NULL, 0}; /* from the first to the last byte that is not white space; empty if none */

    for (size_t i = 0; i < s.len; i++) {
        if (!is_ws(s.ptr[i])) {
            if (r.ptr == NULL) {
                r.ptr = s.ptr + i;
            }
            r.len = (size_t)(s.ptr + i + 1 - r.ptr);
        }
    }
    return r;
}

bool core_str_split_once(core_str_t s, core_str_t sep, core_str_t *head, core_str_t *tail) {
    core_str_t h = s;
    core_str_t t = {NULL, 0};
    bool found = false;
    size_t pos = sep.len == 0 ? CORE_NPOS : core_str_find(s, sep);

    if (pos != CORE_NPOS) {
        h.len = pos;
        t.ptr = s.ptr + pos + sep.len;
        t.len = s.len - pos - sep.len;
        found = true;
    }
    if (head != NULL) {
        *head = h;
    }
    if (tail != NULL) {
        *tail = t;
    }
    return found;
}
