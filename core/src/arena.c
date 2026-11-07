#include "core/arena.h"

#include <stdint.h>

bool core_arena_init(core_arena_t *a, void *buf, size_t cap) {
    if (a == NULL) {
        return false;
    }
    if (buf == NULL && cap > 0) {
        return false;
    }
    a->base = (unsigned char *)buf;
    a->cap = cap;
    a->used = 0;
    return true;
}

static bool align_valid(size_t align) {
    if (align == 0 || align > CORE_ARENA_MAX_ALIGN) {
        return false;
    }
    return (align & (align - 1)) == 0;
}

void *core_arena_alloc(core_arena_t *a, size_t size, size_t align) {
    size_t remaining;
    size_t pad;
    uintptr_t addr;
    unsigned char *block;

    if (a == NULL || a->cap == 0 || a->base == NULL || size == 0 || !align_valid(align)) {
        return NULL;
    }
    remaining = a->cap - a->used;
    addr = (uintptr_t)(a->base + a->used);
    pad = (size_t)((align - (addr & (align - 1))) & (align - 1));
    if (size > remaining || pad > remaining - size) {
        return NULL;
    }
    block = a->base + a->used + pad;
    a->used += pad + size;
    return block;
}

size_t core_arena_used(const core_arena_t *a) {
    return a == NULL ? 0 : a->used;
}

size_t core_arena_capacity(const core_arena_t *a) {
    return a == NULL ? 0 : a->cap;
}

size_t core_arena_mark(const core_arena_t *a) {
    return core_arena_used(a);
}

bool core_arena_reset_to(core_arena_t *a, size_t mark) {
    if (a == NULL || mark > a->used) {
        return false;
    }
    a->used = mark;
    return true;
}
