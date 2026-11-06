/* fsname/fsname.h: filename sanitiser (spec section 7). A SECURITY BOUNDARY: a downloaded file name
 * built from the result can never leave its directory. Pure function, no allocation, no IO.
 *
 * Algorithm, applied in exactly this order to the input bytes (the input is arbitrary bytes: it may be
 * invalid UTF-8 and may contain NUL):
 *   1. Every one of  < > : " / \ | ? *  and every byte < 0x20 and the byte 0x7f becomes '_'.
 *      All other bytes, including every byte >= 0x80, are kept as they are.
 *   2. Scanning left to right, every non-overlapping ".." becomes the single byte '_'.
 *      So "..." becomes "_." , "...." becomes "__" and "....." becomes "__.".
 *   3. Leading '.' and ' ' bytes are removed (any mix, any number), then trailing ' ' and '.' bytes are
 *      removed (any mix, any number). Other whitespace (tab, newline) is already '_' after step 1.
 *   4. The result is cut to FSNAME_MAX_LEN (200) bytes without splitting a UTF-8 sequence. Only a
 *      result longer than 200 bytes is cut. If the byte at index 200 (the first one dropped) is a
 *      continuation byte (0x80..0xBF) and a lead byte (>= 0xC0) sits at index 199, 198 or 197 with
 *      only continuation bytes between it and index 200, the cut moves back to that lead byte (the
 *      whole partial sequence is dropped). Otherwise (stray continuation bytes, invalid input) the cut
 *      stays at exactly 200. Step 3 is NOT repeated after the cut: a cut result may end in '.' or ' '.
 *   5. An empty result becomes the 8 bytes "untitled".
 *
 * Consequences (proof obligations): the output is never empty, is at most 200 bytes, never contains
 * '/', '\\', NUL, a byte < 0x20 or 0x7f, never equals "." or "..", never starts with '.' or ' ' and
 * never contains "..". Joined with a directory by "<dir>/<name>" it stays inside that directory.
 */
#ifndef FSNAME_H
#define FSNAME_H

#include <stddef.h>

#include "core/str.h"

/* Longest output, in bytes (without the terminating NUL). */
#define FSNAME_MAX_LEN ((size_t)200)

/* Smallest output buffer accepted by fsname_sanitize: FSNAME_MAX_LEN + 1 (room for the NUL). */
#define FSNAME_BUF_SIZE ((size_t)201)

/* Sanitise `title` into out[0..cap). On success returns the output length n (1 <= n <= 200),
 * out[0..n) is the name and out[n] is a NUL terminator (the name itself never contains NUL).
 * Nothing beyond out[n] is written. Input of any length is accepted (1 MiB is fine).
 * Returns 0 and writes nothing when: `out` is NULL, `cap < FSNAME_BUF_SIZE`, or `title.ptr` is NULL
 * with `title.len > 0`. A slice with len 0 (ptr may be NULL) is an empty title and gives "untitled".
 * The input bytes are only read. `out` must not overlap the input. */
size_t fsname_sanitize(core_str_t title, char *out, size_t cap);

#endif
