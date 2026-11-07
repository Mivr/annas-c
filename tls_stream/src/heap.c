/* tls_stream/src/heap.c: the arena allocator handed to mbedTLS (no malloc), and the slot that says which object
 * is connected. mbedtls_platform_set_calloc_free() takes plain functions, so the active object is a global. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mbedtls/platform.h"

#include "tls_stream/src/internal.h"

#define ALIGN 16u

typedef struct {
    size_t size; /* the whole block, header included; a multiple of ALIGN */
    size_t is_free;
} block_t;

_Static_assert(sizeof(block_t) == ALIGN, "the block header keeps payloads aligned");

static tls_obj_t *g_active;

static block_t *block_next(const tls_heap_t *h, block_t *b)
{
    unsigned char *p = (unsigned char *) b + b->size;
    return p < h->base + h->size ? (block_t *) p : NULL;
}

void tls_heap_setup(tls_heap_t *h, void *mem, size_t size)
{
    size_t skew = (size_t) ((ALIGN - ((uintptr_t) mem % ALIGN)) % ALIGN);
    block_t *b;
    h->base = (unsigned char *) mem + skew;
    h->size = (size - skew) / ALIGN * ALIGN;
    h->used = 0;
    h->peak = 0;
    h->failed = false;
    b = (block_t *) h->base;
    b->size = h->size;
    b->is_free = 1;
}

static block_t *find_fit(const tls_heap_t *h, size_t need)
{
    block_t *b = (block_t *) h->base;
    while (b != NULL && !(b->is_free != 0U && b->size >= need)) {
        b = block_next(h, b);
    }
    return b;
}

static void split(block_t *b, size_t need)
{
    if (b->size - need >= 2U * ALIGN) {
        block_t *rest = (block_t *) ((unsigned char *) b + need);
        rest->size = b->size - need;
        rest->is_free = 1;
        b->size = need;
    }
}

static void *heap_calloc(size_t count, size_t size)
{
    tls_heap_t *h;
    block_t *b;
    size_t need;
    if (g_active == NULL) {
        return NULL;
    }
    h = &g_active->heap;
    /* need = the payload rounded up to ALIGN, plus the header; both steps refuse a size_t overflow */
    if (__builtin_mul_overflow(count, size, &need) || __builtin_add_overflow(need, 2U * ALIGN - 1U, &need)) {
        h->failed = true;
        return NULL;
    }
    need = need / ALIGN * ALIGN;
    b = find_fit(h, need);
    if (b == NULL) {
        h->failed = true;
        return NULL;
    }
    split(b, need);
    b->is_free = 0;
    h->used += b->size;
    h->peak = h->used > h->peak ? h->used : h->peak;
    memset((unsigned char *) b + ALIGN, 0, b->size - ALIGN);
    return (unsigned char *) b + ALIGN;
}

static void merge_free(const tls_heap_t *h)
{
    block_t *b = (block_t *) h->base;
    while (b != NULL) {
        block_t *n = block_next(h, b);
        if (b->is_free != 0U && n != NULL && n->is_free != 0U) {
            b->size += n->size;
        } else {
            b = n;
        }
    }
}

static void heap_free(void *p)
{
    block_t *b;
    if (p == NULL || g_active == NULL) {
        return;
    }
    b = (block_t *) ((unsigned char *) p - ALIGN);
    g_active->heap.used -= b->size;
    b->is_free = 1;
    merge_free(&g_active->heap);
}

bool tls_claim(tls_obj_t *o)
{
    if (g_active != NULL) {
        return false;
    }
    /* always 0 in mbedtls 3.6.7 (platform.c): the hook cannot fail to install */
    (void) mbedtls_platform_set_calloc_free(heap_calloc, heap_free);
    g_active = o;
    return true;
}

void tls_release(tls_obj_t *o)
{
    if (g_active == o) {
        g_active = NULL;
    }
}

const tls_obj_t *tls_active(void)
{
    return g_active;
}
