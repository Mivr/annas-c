#include "core/buf.h"

/* Stubs: the implementation task (A002) replaces these. */
bool core_buf_init(core_buf_t *b, char *storage, size_t cap) {
    (void)b;
    (void)storage;
    (void)cap;
    return false;
}

void core_buf_put(core_buf_t *b, const void *p, size_t n) {
    (void)b;
    (void)p;
    (void)n;
}

void core_buf_put_str(core_buf_t *b, core_str_t s) {
    (void)b;
    (void)s;
}

void core_buf_put_char(core_buf_t *b, char c) {
    (void)b;
    (void)c;
}

void core_buf_put_u64(core_buf_t *b, uint64_t v) {
    (void)b;
    (void)v;
}

size_t core_buf_len(const core_buf_t *b) {
    (void)b;
    return 0;
}

bool core_buf_overflow(const core_buf_t *b) {
    (void)b;
    return false;
}

core_str_t core_buf_str(const core_buf_t *b) {
    core_str_t r = {NULL, 0};
    (void)b;
    return r;
}

const char *core_buf_cstr(const core_buf_t *b) {
    (void)b;
    return NULL;
}
