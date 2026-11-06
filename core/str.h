/* core/str.h: non-owning byte slices ("string views"). Not NUL-terminated; may contain NUL bytes.
 *
 * A slice with len == 0 is the empty string whatever its ptr is (NULL is allowed). Functions never
 * read beyond ptr[len-1] and never write. "Whitespace" is exactly the ASCII bytes
 * space, \t, \n, \v, \f, \r (0x20, 0x09..0x0d); bytes >= 0x80 are never whitespace. */
#ifndef CORE_STR_H
#define CORE_STR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *ptr;
    size_t len;
} core_str_t;

/* Returned by core_str_find when there is no match. */
#define CORE_NPOS SIZE_MAX

/* View of the NUL-terminated string `c` (the NUL is not included). NULL gives {NULL, 0}. */
core_str_t core_str_from_cstr(const char *c);

/* Byte-for-byte equality: same length and same bytes (case-sensitive; embedded NULs compared).
 * Any two empty slices are equal. */
bool core_str_eq(core_str_t a, core_str_t b);

/* True iff the first prefix.len bytes of s equal prefix. An empty prefix is a prefix of everything
 * (including the empty string). A prefix longer than s is false. */
bool core_str_starts_with(core_str_t s, core_str_t prefix);

/* Index of the first occurrence of needle in hay, or CORE_NPOS. An empty needle matches at 0 (even in
 * an empty hay). A needle longer than hay gives CORE_NPOS. Overlapping candidates are handled
 * ("aab" in "aaab" is at 1). */
size_t core_str_find(core_str_t hay, core_str_t needle);

/* Sub-slice of s without leading and trailing ASCII whitespace. The result points into s. If s is
 * empty or all whitespace the result has len 0. Interior whitespace is kept. */
core_str_t core_str_trim(core_str_t s);

/* Split s at the FIRST occurrence of sep (a multi-byte separator is allowed).
 * Found: returns true, *head = bytes before sep, *tail = bytes after sep (both may be empty;
 * both point into s).
 * Not found, or sep is empty: returns false, *head = s unchanged, *tail = empty (len 0).
 * `head` and `tail` may each be NULL, in which case that output is skipped. */
bool core_str_split_once(core_str_t s, core_str_t sep, core_str_t *head, core_str_t *tail);

#endif
