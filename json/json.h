/* json/json.h: strict RFC 8259 parser into an arena DOM, accessors, and a streaming writer.
 *
 * Everything is non-recursive and allocation-free outside the caller's arena / buffer. Input is a
 * `core_str_t` (bytes, not NUL-terminated). The DOM lives entirely in the arena: after a successful
 * parse the input bytes may be overwritten or freed, and every string, key and number text stays valid
 * until the arena is rewound past it. A DOM value is read only; a `const json_value_t *` is the
 * only handle and is valid until the arena is reset below the mark taken before `json_parse`.
 *
 * ---- Grammar ---------------------------------------------------------------------------------
 * RFC 8259 exactly, with these decisions:
 *  - Whitespace is only space, \t, \n, \r. Leading and trailing whitespace around the root is allowed.
 *  - Any JSON value may be the root (a bare `1`, `"x"`, `null`). Empty / whitespace-only input is
 *    an error. A UTF-8 BOM is an error (it is not whitespace).
 *  - The whole input must be consumed: anything after the root value (and trailing whitespace) is an error.
 *  - Strings: input must be valid UTF-8 (no overlong forms, no surrogates U+D800..DFFF, nothing above
 *    U+10FFFF) and must not contain raw bytes < 0x20. Escapes: \" \\ \/ \b \f \n \r \t and \uXXXX (hex
 *    digits of either case). A \uD800..\uDBFF escape must be followed immediately by a \uDC00..\uDFFF
 *    escape and the pair decodes to one code point (4 UTF-8 bytes); a lone high or low surrogate escape is an
 *    error. \u0000 is allowed and yields a NUL byte inside the decoded string. The decoded string is UTF-8.
 *  - max_string (see json_limits_t) applies to strings and keys only; the length of a number's text is
 *    bounded only by max_input.
 *  - Numbers: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?. The text is kept verbatim (a float or an
 *    exponent literal parses fine); see json_get_i64 / json_get_u64 for conversions.
 *  - Objects: member keys are compared by their DECODED bytes; a repeated key is an error, so
 *    {"a":1,"a":2} is rejected. Member order is source order.
 *
 * ---- Errors ----------------------------------------------------------------------------------
 * `json_parse` returns a `json_err_t` and, on failure, the byte offset in the input. The offset rule is the
 * same everywhere: it is the offset of the FIRST byte that cannot continue a valid document (the "first bad
 * byte"), or the input length when the input simply ends too early. Specifics:
 *  - JSON_E_SYNTAX: structure / literal / string-escape errors; offset = first bad byte (or length at EOF).
 *    A bad escape letter (`\x`) is reported at the letter after the backslash; a bad hex digit in \uXXXX at
 *    that digit; a raw control byte in a string at that byte; an unterminated string at the length.
 *    A repeated object key is JSON_E_SYNTAX at the opening quote of the SECOND occurrence.
 *  - JSON_E_NUMBER: a malformed number. After `-` a digit is required; after a leading `0` a following digit is
 *    an error (reported at that digit); after `.` and after `e`/`E`[+-] a digit is required. Offset = the
 *    byte where the required digit is missing (the length at EOF). A byte that merely cannot follow a complete
 *    number (`1x`, `1.5.2`) is not a number error: it is JSON_E_SYNTAX at that byte.
 *  - JSON_E_UTF8: invalid UTF-8 INSIDE a string (or key); offset = the first byte of the malformed
 *    sequence (a truncated sequence at the closing quote or at EOF is reported at its lead byte). Also a lone
 *    surrogate \u escape: offset = the backslash of that escape (for a high surrogate, the high one's
 *    backslash, whatever follows it). Invalid bytes outside strings are JSON_E_SYNTAX.
 *  - JSON_E_DEPTH: the opening bracket that would exceed `max_depth` (offset of that bracket).
 *  - JSON_E_LIMIT: a limit other than depth was exceeded, see json_limits_t for the offsets.
 *  - JSON_E_NOMEM: the arena is too small. The offset is unspecified, but <= the input length.
 *  - JSON_E_ARG: NULL `root`, NULL arena, or `in.ptr == NULL` with `in.len > 0`. Offset 0.
 * On every failure `*root` is NULL and the arena is rewound to exactly its state before the call.
 * On success `*err_offset` is 0. */
#ifndef JSON_JSON_H
#define JSON_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/arena.h"
#include "core/buf.h"
#include "core/str.h"

/* Hard caps. */
#define JSON_MAX_DEPTH ((size_t)64)
#define JSON_MAX_INPUT ((size_t)8 * 1024 * 1024)

typedef enum {
    JSON_NONE = 0, /* json_type(NULL) */
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

typedef enum {
    JSON_OK = 0,
    JSON_E_SYNTAX,
    JSON_E_DEPTH,
    JSON_E_LIMIT,
    JSON_E_NOMEM,
    JSON_E_UTF8,
    JSON_E_NUMBER,
    JSON_E_ARG
} json_err_t;

/* Opaque DOM node. */
typedef struct json_value json_value_t;

/* Caller-chosen limits. Nesting depth = number of containers open at once: `1` has depth 0, `[1]` depth 1,
 * `[[1]]` depth 2.
 *  - max_depth:    0..JSON_MAX_DEPTH. A document of depth <= max_depth is accepted; a deeper one is
 *                  JSON_E_DEPTH. max_depth 0 accepts scalars only.
 *  - max_input:    0..JSON_MAX_INPUT. Input longer than max_input is JSON_E_LIMIT with offset max_input.
 *                  This check comes first: it wins over every syntax error in the input.
 *  - max_string:   maximum DECODED length in bytes of any string value or object key (escapes count as
 *                  the UTF-8 bytes they decode to). Longer: JSON_E_LIMIT at the string's opening quote.
 *  - max_elements: maximum number of values in the whole document, counting the root and every nested
 *                  value (object keys are not values): `[1,2,3]` has 4, `{"a":[]}` has 2, `7` has 1.
 *                  The first value beyond the maximum: JSON_E_LIMIT at that value's first byte.
 * A max_depth > JSON_MAX_DEPTH or max_input > JSON_MAX_INPUT makes json_parse fail with JSON_E_LIMIT at
 * offset 0 whatever the input. All other values (including 0) are taken literally. */
typedef struct {
    size_t max_depth;
    size_t max_input;
    size_t max_string;
    size_t max_elements;
} json_limits_t;

/* {64, 8 MiB, 1 MiB, 1048576}. */
json_limits_t json_limits_default(void);

/* Parse `in` into `*root` (non-NULL), allocating from `a`. `lim == NULL` means json_limits_default().
 * `err_offset` may be NULL. See the error rules above. */
json_err_t json_parse(core_arena_t *a, core_str_t in, const json_limits_t *lim, const json_value_t **root,
                      size_t *err_offset);

/* ---- Access. Every accessor tolerates NULL and the wrong type (returns NONE / NULL / false / 0 / {NULL,0})
 * and then leaves its out-parameter untouched. ---- */

json_type_t json_type(const json_value_t *v);

/* Number of elements of an array / members of an object; 0 for every other type and for NULL. */
size_t json_len(const json_value_t *v);

/* Array: element i. Object: the value of member i in source order. NULL if i >= json_len or other type. */
const json_value_t *json_at(const json_value_t *v, size_t i);

/* Object: the key of member i (decoded). An empty slice (len 0) for other types or out of range. */
core_str_t json_key_at(const json_value_t *v, size_t i);

/* Object: the value whose decoded key equals `key` byte-for-byte (case-sensitive), or NULL. */
const json_value_t *json_obj_get(const json_value_t *v, core_str_t key);

/* true/false of a JSON_BOOL. */
bool json_get_bool(const json_value_t *v, bool *out);

/* Decoded UTF-8 bytes of a JSON_STRING (may contain NUL; not NUL-terminated). */
bool json_get_str(const json_value_t *v, core_str_t *out);

/* Verbatim source text of a JSON_NUMBER, e.g. "-0", "1.50", "1E+2". */
bool json_number_text(const json_value_t *v, core_str_t *out);

/* Checked conversions of a JSON_NUMBER. Only INTEGER literals convert: -?(0|[1-9][0-9]*) with no
 * fraction and no exponent ("1.0", "1e2", "0.0" all fail). json_get_i64 accepts [INT64_MIN, INT64_MAX]
 * (so "-0" is 0). json_get_u64 accepts [0, UINT64_MAX] and rejects every literal starting with '-',
 * including "-0". Out-of-range fails; `*out` is untouched on failure. */
bool json_get_i64(const json_value_t *v, int64_t *out);
bool json_get_u64(const json_value_t *v, uint64_t *out);

/* ---- Compact serialiser of a DOM (no whitespace, members in source order, numbers verbatim, strings
 * escaped as the writer below does). Appends to `b`. Returns false if `v` is NULL, `b` is NULL, or the
 * buffer overflowed (core_buf semantics: all-or-nothing appends, sticky overflow). Non-recursive. ---- */
bool json_write(core_buf_t *b, const json_value_t *v);

/* ---- Streaming writer into a core_buf_t, compact output.
 *   Strings: `"` -> \" , `\` -> \\ , every byte < 0x20 -> \u00xx (lower-case hex, so newline is \u000a),
 *   everything else (including 0x7f, '/', and valid multi-byte UTF-8) verbatim. Invalid UTF-8 in a string or
 *   key is a writer error (same validity rules as the parser). Integers in decimal ("-9223372036854775808"
 *   for INT64_MIN). Commas and colons are emitted automatically.
 *   Misuse puts the writer into a sticky error state in which later calls do nothing: a value where an object
 *   needs a key, a key outside an object or twice in a row, ending the wrong container kind or with a
 *   key pending or with nothing open, a second root value, nesting deeper than JSON_MAX_DEPTH.
 *   json_w_ok is also false once the buffer has overflowed. ---- */
typedef struct {
    core_buf_t *buf;
    size_t depth;
    unsigned char kind[JSON_MAX_DEPTH]; /* implementation use */
    unsigned char flags[JSON_MAX_DEPTH];
    bool root_done;
    bool failed;
} json_writer_t;

/* Starts a writer over `b` (nothing is written yet). Returns false if w or b is NULL; then, if w is
 * non-NULL, it is left in the error state (json_w_ok false), never uninitialised. */
bool json_w_init(json_writer_t *w, core_buf_t *b);
void json_w_begin_object(json_writer_t *w);
void json_w_end_object(json_writer_t *w);
void json_w_begin_array(json_writer_t *w);
void json_w_end_array(json_writer_t *w);
void json_w_key(json_writer_t *w, core_str_t key);
void json_w_string(json_writer_t *w, core_str_t s);
void json_w_i64(json_writer_t *w, int64_t v);
void json_w_u64(json_writer_t *w, uint64_t v);
void json_w_bool(json_writer_t *w, bool v);
void json_w_null(json_writer_t *w);

/* No error so far and the buffer has not overflowed (false for NULL). */
bool json_w_ok(const json_writer_t *w);
/* json_w_ok and exactly one complete root value has been written with every container closed. */
bool json_w_done(const json_writer_t *w);

#endif
