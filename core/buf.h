/* core/buf.h: bounded byte builder over a caller-provided buffer. No allocation.
 *
 * The buffer of `cap` bytes holds at most cap-1 content bytes plus a NUL terminator, which is
 * maintained after every call (when cap >= 1). Appends are ALL-OR-NOTHING: a call whose bytes do
 * not all fit leaves the content untouched and sets the sticky `overflow` flag. Once overflow is
 * set, every later append is ignored (even one that would fit) until `core_buf_init` is called
 * again. A call that appends zero bytes never sets overflow.
 *
 * cap == 0 is allowed: nothing is ever written (not even a NUL); any append of >= 1 byte sets
 * overflow. cap == 1 holds only the empty string. */
#ifndef CORE_BUF_H
#define CORE_BUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/str.h"

/* Treat as opaque; use only through the functions below. */
typedef struct {
    char *data;
    size_t cap;
    size_t len;
    bool overflow;
} core_buf_t;

/* Start an empty builder over storage[0..cap). Clears the overflow flag, sets length 0 and, if
 * cap >= 1, stores a NUL in storage[0]. Returns true on success. Returns false and changes nothing
 * when `b` is NULL or (`storage` is NULL and cap > 0). `storage == NULL` with cap == 0 is valid. */
bool core_buf_init(core_buf_t *b, char *storage, size_t cap);

/* Append n bytes from p. n == 0 is a successful no-op (p may then be NULL). p == NULL with n > 0
 * counts as a failed append (overflow set, nothing written). NULL `b`: no effect. */
void core_buf_put(core_buf_t *b, const void *p, size_t n);

/* Append the bytes of s. */
void core_buf_put_str(core_buf_t *b, core_str_t s);

/* Append one byte c (the byte 0 is a legal content byte). */
void core_buf_put_char(core_buf_t *b, char c);

/* Append v in decimal: no sign, no leading zeros, "0" for zero, up to "18446744073709551615". */
void core_buf_put_u64(core_buf_t *b, uint64_t v);

/* Shrink the content to its first `len` bytes. If len < core_buf_len(b): the length becomes len and,
 * when cap >= 1, a NUL is stored at data[len] (bytes after it are unspecified, nothing outside
 * data[0..cap) is touched). If len >= core_buf_len(b), or `b` is NULL, or cap == 0: no effect at all
 * (truncate never grows). len == 0 empties the builder (content "", NUL at data[0]). The overflow flag
 * is NEVER changed: truncating does not clear a sticky overflow, since the dropped tail of an
 * overflowed append cannot be recovered, and later appends stay ignored until `core_buf_init`.
 * Truncating twice to the same length equals truncating once. Appends after a truncation continue
 * from the new length (subject to the usual capacity rules and stickiness). */
void core_buf_truncate(core_buf_t *b, size_t len);

/* Number of content bytes (0 for NULL). */
size_t core_buf_len(const core_buf_t *b);

/* True if an append has failed since the last init (false for NULL). */
bool core_buf_overflow(const core_buf_t *b);

/* The content as a slice into the buffer ({NULL, 0} for NULL or cap == 0). */
core_str_t core_buf_str(const core_buf_t *b);

/* The content as a NUL-terminated C string: the buffer itself when cap >= 1. When cap == 0 (or `b`
 * is NULL) returns a pointer to a static empty string "". */
const char *core_buf_cstr(const core_buf_t *b);

#endif
