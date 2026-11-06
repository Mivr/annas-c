/* url/url.h: percent-encoding, base-URL normalisation, request-URL builders, secret redaction and
 * identifier recognition (spec sections 1, 3.3, 3.4). Pure, no allocation, no IO.
 *
 * All builders APPEND to a caller-owned core_buf_t (so pieces can be composed; the buffer may already
 * hold text). Inputs are arbitrary bytes unless stated; NUL bytes are ordinary bytes.
 *
 * Status codes (the same for every function that returns url_status_t):
 *   URL_OK         done.
 *   URL_E_INVALID  an input violates the rule of the function. NOTHING is appended.
 *   URL_E_OVERFLOW the output buffer is too small. The buffer's overflow flag is set (so
 *                  core_buf_overflow() is true); the content appended by this call is unspecified.
 *   URL_E_ARG      a NULL pointer where one is required (`out`, or `ptr == NULL` with `len > 0`),
 *                  or an enum value out of range. NOTHING is appended.
 * Precedence when several apply: URL_E_ARG, then URL_E_INVALID, then URL_E_OVERFLOW. In particular
 * an input is validated completely before the first byte is appended. If `out` already has its
 * overflow flag set, a call whose input is valid returns URL_E_OVERFLOW.
 */
#ifndef URL_H
#define URL_H

#include <stdbool.h>
#include <stddef.h>

#include "core/buf.h"
#include "core/str.h"

typedef enum {
    URL_OK = 0,
    URL_E_INVALID,
    URL_E_OVERFLOW,
    URL_E_ARG
} url_status_t;

/* ---- percent-encoding (RFC 3986) -------------------------------------------------------------- */

/* Appends the encoding of `in`: the unreserved bytes A-Z a-z 0-9 - . _ ~ are copied; EVERY other byte
 * (space, '+', '/', NUL, bytes >= 0x80, ...) becomes '%' followed by two UPPER-case hex digits.
 * Space is "%20", never '+'. Empty input appends nothing and is URL_OK. Only URL_E_ARG/OVERFLOW possible. */
url_status_t url_pct_encode(core_buf_t *out, core_str_t in);

/* Appends the decoding of `in`: each "%XY" (X, Y hex digits of either case) becomes that byte
 * (including 0x00); every other byte, including '+' and bytes >= 0x80, is copied unchanged ('+' does
 * NOT become a space). A '%' not followed by two hex digits (end of input, one digit, a non-hex digit)
 * is URL_E_INVALID. The decoded bytes are not interpreted further (a decoded "%2F" is just '/'). */
url_status_t url_pct_decode(core_buf_t *out, core_str_t in);

/* ---- base URL (ANNAS_BASE_URL, spec section 1) -------------------------------------------------- */

#define URL_DEFAULT_BASE "annas-archive.gl"

/* True iff `s` is non-empty and every byte is in [a-z0-9.-] (lower-case only). No other check. */
bool url_base_is_valid(core_str_t s);

/* Appends the normalised base domain for the configured value `raw`:
 *   - empty `raw` (len 0): URL_DEFAULT_BASE ("annas-archive.gl");
 *   - otherwise one leading "http://" or "https://" is removed (exact lower-case; only one scheme), then
 *     ONE trailing '/' is removed, and the rest must satisfy url_base_is_valid, else URL_E_INVALID.
 *   Nothing is trimmed: " x.org", "x.org " and "X.org" are URL_E_INVALID. "https://", "/" and
 *   "https:///" give URL_E_INVALID (nothing left), "x.org//" and "https://http://x.org" too. */
url_status_t url_base_normalize(core_str_t raw, core_buf_t *out);

/* ---- request URL builders -----------------------------------------------------------------------
 * `base` must be a normalised domain (url_base_is_valid), `md5` a normalised hash (url_md5_is_valid),
 * else URL_E_INVALID. Every produced URL starts with "https://<base>". */

typedef enum {
    URL_CONTENT_BOOK = 0,      /* content=book_any */
    URL_CONTENT_JOURNAL = 1    /* content=journal  */
} url_content_t;

/* "https://<base>/search?q=<pct_encode(query)>&content=<book_any|journal>". `query` may be empty
 * ("q="). Any other `content` value is URL_E_ARG. */
url_status_t url_build_search(core_buf_t *out, core_str_t base, core_str_t query, url_content_t content);

/* "https://<base>/dyn/api/fast_download.json?md5=<md5>&key=<pct_encode(key)>".
 * `key` must be non-empty (else URL_E_INVALID); it is encoded like any other value. */
url_status_t url_build_fast_download(core_buf_t *out, core_str_t base, core_str_t md5, core_str_t key);

/* "https://<base>/md5/<md5>". */
url_status_t url_build_md5_page(core_buf_t *out, core_str_t base, core_str_t md5);

/* "https://<base>/scidb/<doi>": the DOI is encoded as in url_pct_encode EXCEPT that '/' is kept
 * literally. `doi` must start with "10." exactly, with NO trimming
 * (else URL_E_INVALID; " 10.1/x" is invalid; callers trim with url_doi_recognize first). */
url_status_t url_build_scidb_path(core_buf_t *out, core_str_t base, core_str_t doi);

/* "https://<base>/scidb?doi=<pct_encode(doi)>" (here '/' IS encoded, as "%2F"). Same doi rule. */
url_status_t url_build_scidb_download(core_buf_t *out, core_str_t base, core_str_t doi);

/* ---- redaction (spec section 1: the key never appears in errors) --------------------------------- */

#define URL_REDACTED "REDACTED"

/* Appends `in` with the value of every `key=` parameter replaced by "REDACTED". A parameter is the
 * exact, lower-case name "key" followed by '=', where "key" is preceded by the start of `in` or one
 * of ? & ; # space \t \n \r \v \f " ' . Its value runs up to (not including) the next of & # space \t \n \r
 * \v \f " ' or the end of `in`; an EMPTY value is replaced too ("key=" gives "key=REDACTED").
 * Everything else is copied byte for byte (so "monkey=1", "keys=1", "KEY=1", "key" and "key&x=1"
 * are untouched). The scan resumes after the replaced value, so all occurrences are redacted. */
url_status_t url_redact_key(core_buf_t *out, core_str_t in);

/* ---- identifiers ----------------------------------------------------------------------------------- */

/* Size of the buffer for url_md5_normalize: 32 digits plus NUL. */
#define URL_MD5_BUF ((size_t)33)

/* True iff `s` is exactly 32 lower-case hex digits [0-9a-f]. */
bool url_md5_is_valid(core_str_t s);

/* `in` must be exactly 32 hex digits of either case (no trimming, no embedded NUL). On success writes
 * the lower-cased digits and a NUL to out[0..33) and returns true. Otherwise (or `out` NULL, or
 * `in.ptr` NULL with len > 0) returns false and leaves `out` untouched. */
bool url_md5_normalize(core_str_t in, char *out);

/* DOI recognition: trims ASCII whitespace (space \t \n \v \f \r) from both ends; true iff the trimmed
 * text starts with "10." (case-sensitive; nothing is required after the dot). Then *trimmed (if not
 * NULL) is the trimmed slice, pointing into `in`. Otherwise returns false and *trimmed is untouched. */
bool url_doi_recognize(core_str_t in, core_str_t *trimmed);

#endif
