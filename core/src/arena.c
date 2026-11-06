#include "core/arena.h"

/* Stubs: the implementation task (A002) replaces these. */
bool core_arena_init(core_arena_t *a, void *buf, size_t cap) {
    (void)a;
    (void)buf;
    (void)cap;
    return false;
}

void *core_arena_alloc(core_arena_t *a, size_t size, size_t align) {
    (void)a;
    (void)size;
    (void)align;
    return NULL;
}

size_t core_arena_used(const core_arena_t *a) {
    (void)a;
    return 0;
}

size_t core_arena_capacity(const core_arena_t *a) {
    (void)a;
    return 0;
}

size_t core_arena_mark(const core_arena_t *a) {
    (void)a;
    return 0;
}

bool core_arena_reset_to(core_arena_t *a, size_t mark) {
    (void)a;
    (void)mark;
    return false;
}
