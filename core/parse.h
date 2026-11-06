/* core/parse.h: checked integer parsing and ASCII helpers. Locale independent. */
#ifndef CORE_PARSE_H
#define CORE_PARSE_H

#include <stdbool.h>
#include <stdint.h>

#include "core/str.h"

/* Parse s as an unsigned decimal number. Accepted: one or more ASCII digits '0'..'9' and nothing
 * else (leading zeros allowed, any number of them: "000...0042" is 42). Rejected (returns false and
 * leaves *out untouched): empty s, any sign ('+' or '-'), whitespace, any other byte (including an
 * embedded NUL), a value above UINT64_MAX (18446744073709551616 and beyond), `out == NULL`.
 * UINT64_MAX itself ("18446744073709551615") is accepted. */
bool core_parse_u64(core_str_t s, uint64_t *out);

/* Value 0..15 of the hex digit c ('0'-'9', 'a'-'f', 'A'-'F'), or -1 for every other byte value
 * (c is treated as an unsigned byte; 'g', 'G', '/', ':', '@', '`' are all -1). */
int core_parse_hex_nibble(char c);

/* ASCII lower-casing: 'A'..'Z' map to 'a'..'z'; every other byte (including '@', '[' and bytes
 * >= 0x80) is returned unchanged. */
char core_ascii_lower(char c);

#endif
