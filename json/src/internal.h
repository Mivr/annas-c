/* json/src/internal.h: helpers shared by the json implementation files. Not part of the public API. */
#ifndef JSON_SRC_INTERNAL_H
#define JSON_SRC_INTERNAL_H

#include <stddef.h>

/* Length (1..4) of the valid UTF-8 sequence starting at p[0], looking at no more than n bytes; 0 when the
 * sequence is malformed, truncated, overlong, a surrogate, or above U+10FFFF (or n == 0). */
size_t json_utf8_seq_len(const unsigned char *p, size_t n);

#endif
