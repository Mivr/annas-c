/* core/arena.h: bump allocator over a caller-provided buffer. No malloc, no globals.
 *
 * An arena hands out consecutive, non-overlapping blocks of the buffer. Memory is never freed
 * individually; `core_arena_reset_to` rewinds to an earlier mark. Contents of returned blocks are
 * unspecified (not zeroed). Not thread-safe.
 *
 * All offsets are measured from the buffer start passed to `core_arena_init`.
 */
#ifndef CORE_ARENA_H
#define CORE_ARENA_H

#include <stdbool.h>
#include <stddef.h>

/* Treat as opaque; use only through the functions below. Zero-initialisation (`= {0}`) gives an arena
 * with capacity 0 that rejects every allocation. */
typedef struct {
    unsigned char *base;
    size_t cap;
    size_t used;
} core_arena_t;

/* Largest supported alignment, in bytes. */
#define CORE_ARENA_MAX_ALIGN ((size_t)64)

/* Initialise `a` over `buf[0..cap)`. Afterwards `core_arena_used(a) == 0` and
 * `core_arena_capacity(a) == cap`.
 * Returns true on success. Returns false (and leaves `*a` unchanged) when `a` is NULL, or when
 * `buf` is NULL and `cap > 0`. `buf == NULL` with `cap == 0` is valid (an arena that always fails). */
bool core_arena_init(core_arena_t *a, void *buf, size_t cap);

/* Allocate `size` bytes whose ADDRESS is a multiple of `align`.
 * `align` must be a power of two in [1, CORE_ARENA_MAX_ALIGN]. The block starts at the lowest
 * correctly aligned address at or after the current end of the used region; the skipped padding
 * bytes count as used. After success `core_arena_used(a)` is (block offset + size).
 * Returns NULL, and leaves the arena completely unchanged, when:
 *   - `a` is NULL or was never successfully initialised (capacity 0),
 *   - `size == 0`,
 *   - `align` is 0, not a power of two, or greater than CORE_ARENA_MAX_ALIGN,
 *   - padding plus `size` does not fit in the remaining capacity (including any size_t overflow
 *     of that sum, e.g. `size == SIZE_MAX`).
 * An allocation that exactly fills the remaining capacity succeeds. */
void *core_arena_alloc(core_arena_t *a, size_t size, size_t align);

/* Number of bytes consumed so far, padding included (0 for a NULL arena). */
size_t core_arena_used(const core_arena_t *a);

/* Capacity passed to `core_arena_init` (0 for a NULL arena). */
size_t core_arena_capacity(const core_arena_t *a);

/* A mark is the current `core_arena_used` value (0 for a NULL arena). */
size_t core_arena_mark(const core_arena_t *a);

/* Rewind so that `core_arena_used(a) == mark`; blocks handed out after the mark are invalid and
 * their space is reused by later allocations. Returns true on success.
 * Returns false and changes nothing when `a` is NULL or `mark > core_arena_used(a)`.
 * Resetting to the current mark succeeds and is a no-op. */
bool core_arena_reset_to(core_arena_t *a, size_t mark);

#endif
