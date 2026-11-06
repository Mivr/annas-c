/* http/http.h: HTTP/1.1 client message layer (spec sections 2 and 3.3). Pure: no sockets, no IO, no
 * allocation, no clock. It works on bytes the caller moves to and from a stream port.
 *
 *   - URL split          `https://host[:port]/path?query` -> host, port, target.
 *   - request writer     one GET request into a core_buf_t.
 *   - response parser    incremental pull parser; the caller feeds arbitrary chunks (one byte at a time
 *                        must work) and receives events.
 *   - redirect helper    next absolute https URL from a 3xx + Location, capped at 5 by a counter.
 *   - challenge check    spec section 2 (ddos-guard / check=1) as a pure function.
 *
 * All code is non-recursive. Memory is bounded: the parser is a fixed-size struct that never grows with
 * the body (body bytes are handed out as slices of the caller's input, never copied or stored).
 *
 * Status codes. One enum for the whole module. Precedence when several apply (functions that take
 * pointers): HTTP_E_ARG first. `HTTP_E_ARG` means a NULL pointer where one is required, or `ptr == NULL`
 * with `len > 0`, or an enum value out of range.
 */
#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/buf.h"
#include "core/str.h"

typedef enum {
    HTTP_OK = 0,
    HTTP_E_ARG,                /* see above */
    /* URL / request / redirect */
    HTTP_E_SCHEME,             /* not https (http://, ftp:, javascript:, scheme-less absolute URL, ...) */
    HTTP_E_URL,                /* malformed URL, host, port, userinfo, or forbidden byte */
    HTTP_E_INVALID,            /* request writer: a component holds a forbidden byte or is empty/ill-formed */
    HTTP_E_OVERFLOW,           /* output buffer too small (its overflow flag is set; content unspecified) */
    HTTP_E_REDIRECT,           /* not a redirect status, or empty Location */
    HTTP_E_TOO_MANY_REDIRECTS, /* the counter already holds HTTP_MAX_REDIRECTS */
    HTTP_E_BROWSER_CHECK,      /* challenge detected (spec section 2) */
    /* response parser (all sticky: once returned, every later call returns the same code) */
    HTTP_E_STATUS_LINE,        /* malformed status line, or status outside 200..599 */
    HTTP_E_HEADER,             /* malformed header/trailer line: name, colon, forbidden byte in value */
    HTTP_E_FOLDING,            /* obsolete line folding: a header line starting with SP or HTAB */
    HTTP_E_LINE_ENDING,        /* LF without CR, or CR not followed by LF */
    HTTP_E_LINE_TOO_LONG,      /* a line longer than HTTP_MAX_LINE */
    HTTP_E_TOO_MANY_HEADERS,   /* more than HTTP_MAX_HEADERS header + trailer lines */
    HTTP_E_CONTENT_LENGTH,     /* bad value, overflow, or any second Content-Length */
    HTTP_E_FRAMING,            /* Transfer-Encoding other than exactly "chunked" or repeated, CL together with
                                * TE, TE in an HTTP/1.0 response, framing on 204/304, framing in a trailer */
    HTTP_E_CHUNK,              /* bad chunk-size line, size overflow, missing CRLF after chunk data */
    HTTP_E_EXTRA,              /* bytes after the end of the message */
    HTTP_E_TRUNCATED           /* end of stream before the message was complete */
} http_status_t;

#define HTTP_MAX_LINE ((size_t)8192)  /* longest line, excluding its CRLF (status, header, chunk-size, trailer) */
#define HTTP_MAX_HEADERS 100u         /* header + trailer lines per message */
#define HTTP_MAX_REDIRECTS 5u
#define HTTP_MAX_URL ((size_t)8192)   /* longest URL accepted by http_url_split */
#define HTTP_CHALLENGE_PREFIX ((size_t)1024) /* bytes of body examined by http_detect_challenge */

/* ---- URL ------------------------------------------------------------------------------------------ */

typedef struct {
    core_str_t host;   /* slice of the input URL, case preserved, no port, no brackets */
    uint16_t port;     /* 1..65535; 443 when the URL has no port */
    core_str_t target; /* slice of the input: from the first '/' after the authority up to (excluding)
                        * a '#' or the end: path plus optional "?query". Always starts with '/'. */
} http_url_t;

/* True iff `s` is a valid host: a DNS name (labels of 1..63 bytes from [A-Za-z0-9-], not starting or
 * ending with '-', separated by single dots, no leading/trailing dot, total length <= 253) or an IPv4
 * literal. If the LAST label is all digits, or is "0x"/"0X" followed by one or more hex digits (the
 * spellings of a number that C resolvers accept), the whole host must be a strict dotted quad: four
 * decimal octets 0..255 without leading zeros ("1.2.3", "1.2.3.256", "01.2.3.4", "1.2.3.4.5", "123",
 * "0x7f.0.0.1", "0x7f.0.0.0x1" and "a.0x1f" are all invalid, "1.2.3.4" and "0.0.0.0" valid; "a.0x", "a.0xg"
 * and "1.2.3.4a" are ordinary DNS names and valid). No IPv6, no userinfo, no port. */
bool http_host_valid(core_str_t s);

/* Splits `url`. Checks, in this order:
 *   1. scheme: must start with "https://" (scheme letters in either case); anything else, including
 *      "http://", is HTTP_E_SCHEME.
 *   2. the whole URL: longer than HTTP_MAX_URL, or any byte <= 0x20, 0x7F, >= 0x80 or '\\' is HTTP_E_URL.
 *   3. authority (up to the first '/', '?' or '#'): '@' (userinfo) is HTTP_E_URL; the host (up to the
 *      first ':') must satisfy http_host_valid; an optional ":port" is 1 to 5 digits, no leading zero,
 *      value 1..65535 (":", ":0", ":0443", ":65536", ":80a" are HTTP_E_URL); "[" is HTTP_E_URL.
 *   4. a '/' must follow the authority: "https://host" and "https://host?x" are HTTP_E_URL.
 * On any error *out is untouched. */
http_status_t http_url_split(core_str_t url, http_url_t *out);

/* ---- request writer ----------------------------------------------------------------------------------- */

/* Appends exactly
 *   "GET <target> HTTP/1.1\r\nHost: <host>[:<port>]\r\nUser-Agent: annas-c/<version>\r\n"
 *   "Accept: <star>/<star>\r\nConnection: close\r\n\r\n"   (<star> is the asterisk; i.e. any media type)
 * (":<port>" only when port != 443). Validated BEFORE anything is appended; any failure other than
 * HTTP_E_OVERFLOW appends nothing:
 *   - `url->host` must satisfy http_host_valid, `url->port` must be non-zero, `url->target` must start
 *     with '/' and consist only of bytes 0x21..0x7E, `version` must be 1..64 bytes from [A-Za-z0-9._+-]
 *     (so CR, LF, NUL, space and any control byte in any component are refused) -> HTTP_E_INVALID.
 * Precedence: HTTP_E_ARG, HTTP_E_INVALID, HTTP_E_OVERFLOW. If `out` already has its overflow flag set, a
 * valid call returns HTTP_E_OVERFLOW. */
http_status_t http_request_write(core_buf_t *out, const http_url_t *url, core_str_t version);

/* ---- response parser ---------------------------------------------------------------------------------- */

typedef enum {
    HTTP_EV_NONE = 0,    /* no event: more input is needed (the input is fully consumed) */
    HTTP_EV_STATUS,      /* status line: status, minor, reason */
    HTTP_EV_HEADER,      /* one header line: name (ASCII lower-cased), value (trimmed of SP/HTAB) */
    HTTP_EV_HEADERS_END, /* the blank line: framing, content_length */
    HTTP_EV_BODY,        /* data: a non-empty slice */
    HTTP_EV_TRAILER,     /* chunked trailer line: name, value (same rules as HTTP_EV_HEADER) */
    HTTP_EV_END          /* the message is complete */
} http_ev_kind_t;

typedef enum {
    HTTP_FRAMING_NONE = 0, /* 204 and 304: no body */
    HTTP_FRAMING_LENGTH,   /* Content-Length (possibly 0) */
    HTTP_FRAMING_CHUNKED,
    HTTP_FRAMING_CLOSE     /* read until the stream ends */
} http_framing_t;

typedef struct {
    http_ev_kind_t kind;
    int status;                /* STATUS: 200..599 */
    int minor;                 /* STATUS: 0 for "HTTP/1.0", 1 for "HTTP/1.1" */
    core_str_t reason;         /* STATUS: bytes after the single SP following the code; may be empty */
    core_str_t name, value;    /* HEADER, TRAILER */
    http_framing_t framing;    /* HEADERS_END */
    uint64_t content_length;   /* HEADERS_END, meaningful only for HTTP_FRAMING_LENGTH (else 0) */
    core_str_t data;           /* BODY */
} http_event_t;

/* Parser state. Fixed size (about 8.3 KiB: too big for a stack frame, so declare it static or in caller-owned
 * memory). Treat every member as private; use only the functions below. */
typedef struct {
    unsigned char line[HTTP_MAX_LINE]; /* the line being collected */
    uint64_t remaining;                /* body or chunk bytes still expected */
    uint64_t content_length;
    size_t line_len;
    unsigned header_count;
    int state;
    int status;
    int err;                           /* sticky http_status_t */
    unsigned flags;
    unsigned aux;
} http_parser_t;

/* Prepares `p` for one response. Returns false (touching nothing) for NULL, true otherwise. May be called
 * again to reuse the struct. */
bool http_parser_init(http_parser_t *p);

/* Consumes bytes from the front of `*input` (advancing input->ptr and input->len) until ONE event is
 * ready, and returns it in *ev. *ev is always overwritten first (all fields zero, so kind is NONE).
 *   - HTTP_OK with kind NONE: more input is needed; *input has been consumed completely. (Some events need
 *     no input: after the last body byte, or after HEADERS_END of a bodiless message, call again, even
 *     with an empty input, to receive END.)
 *   - HTTP_OK with another kind: exactly the bytes belonging to that event were consumed and no more
 *     (STATUS/HEADER/TRAILER/HEADERS_END: through their CRLF; BODY: exactly ev->data; END: the blank line
 *     that ends a chunked message, otherwise nothing).
 *   - an error code: sticky; every later call returns it again, consumes nothing and yields no event.
 *     How much was consumed by the failing call is unspecified. HTTP_E_ARG (NULL p, input or ev, or
 *     input->ptr == NULL with len > 0) is NOT sticky and changes nothing.
 * Slices in *ev (reason, name, value) stay valid until the next call; `data` points into the caller's input.
 *
 * Event order: STATUS, (HEADER)*, HEADERS_END, (BODY)*, [for chunked: (TRAILER)*], END.
 * BODY slices are never empty and never span a chunk boundary; each is as long as the input and the
 * framing allow (one BODY per call at most). A zero-length body gives no BODY event.
 * Bytes offered after END: the first call with a non-empty input returns HTTP_E_EXTRA (sticky); with an
 * empty input it returns HTTP_OK with kind NONE.
 *
 * Message grammar (strict CRLF everywhere; every violation is the stated sticky error):
 *   status line  "HTTP/1.0" or "HTTP/1.1", one SP, three digits 200..599, then end of line, or one SP and
 *                a reason made of HTAB, SP, 0x21..0x7E and bytes >= 0x80 (so a reason may be empty)
 *                -> HTTP_E_STATUS_LINE.
 *   header line  token name (!#$%&'*+-.^_`|~ digits letters; non-empty; no space before the colon), ':',
 *                value of HTAB, SP, 0x21..0x7E and >= 0x80 bytes -> HTTP_E_HEADER; a line starting with SP or
 *                HTAB -> HTTP_E_FOLDING; LF without CR / CR without LF -> HTTP_E_LINE_ENDING; more than
 *                HTTP_MAX_LINE bytes before the CRLF -> HTTP_E_LINE_TOO_LONG (raised as soon as byte
 *                HTTP_MAX_LINE+1 of the line arrives); the (HTTP_MAX_HEADERS+1)th header or trailer line
 *                -> HTTP_E_TOO_MANY_HEADERS. A line is checked when its CRLF arrives and, if bad, no event
 *                is emitted for it; the events before it have been delivered.
 *   framing      Content-Length: ASCII digits only (leading zeros allowed), value <= UINT64_MAX; empty,
 *                signed, listed ("5, 5"), overflowing -> HTTP_E_CONTENT_LENGTH, and so is ANY second
 *                Content-Length line, even an identical one. Transfer-Encoding: the value, compared
 *                case-insensitively, must be exactly "chunked"; anything else, a second TE line, TE in an
 *                HTTP/1.0 response, or TE and Content-Length in the same message (either order; raised on
 *                whichever line comes second) -> HTTP_E_FRAMING. For 204 and 304: TE, or Content-Length
 *                other than 0 -> HTTP_E_FRAMING, and the message has no body. Otherwise: chunked, else
 *                Content-Length, else read-until-close.
 *   chunked      "<hex>[;<ext>]CRLF" then <hex> bytes then CRLF; the size is 1+ hex digits (either case,
 *                leading zeros fine) with value <= UINT64_MAX; the extension (after ';') holds no control
 *                byte except HTAB and no 0x7F; anything else after the digits (space, sign, "0x", ...),
 *                an empty size, an overflowing size, a size line with bad bytes, or anything but CRLF after
 *                the data -> HTTP_E_CHUNK. After the "0" chunk: trailer lines (HTTP_EV_TRAILER) and a blank line.
 *                A trailer named content-length or transfer-encoding -> HTTP_E_FRAMING.
 *   body end     Content-Length: after exactly that many bytes. Chunked: after the blank line ending the
 *                trailers. Read-until-close: only via http_parser_eof. */
http_status_t http_parser_next(http_parser_t *p, core_str_t *input, http_event_t *ev);

/* The stream ended. Overwrites *ev (zero) first. Returns:
 *   HTTP_OK with kind END: the message is complete (including a read-until-close body, and a message
 *       whose END was not pulled yet); the parser moves to its finished state.
 *   HTTP_OK with kind NONE: END was already delivered.
 *   HTTP_E_TRUNCATED (sticky): anything else (no or partial head, short Content-Length body, unfinished
 *       chunked body).
 *   the sticky error, if the parser already failed.
 *   HTTP_E_ARG for NULL p or ev. */
http_status_t http_parser_eof(http_parser_t *p, http_event_t *ev);

/* ---- redirects ------------------------------------------------------------------------------------------ */

typedef struct {
    unsigned count; /* redirects followed so far; zero-initialisation is the initial state */
} http_redirects_t;

/* True for 301, 302, 303, 307, 308 (and no other value). */
bool http_is_redirect_status(int status);

/* Resolves `location` against `cur` (RFC 3986 section 5, https only) and APPENDS the absolute URL to
 * `out` as "https://<host>[:<port>]<path>[?<query>]": the scheme lower-case, host as written, ":443"
 * dropped, an empty path written as "/", the fragment dropped, the query kept verbatim, and dot
 * segments ("." and "..") removed from the path (never above the root; percent-encoded dots such as
 * "%2e%2e" are NOT decoded). Reference forms: absolute "https://...", scheme-relative "//host/...",
 * absolute-path "/p", relative "p" and "../p" (merged with the directory of cur->target), query-only
 * "?q" (current path, new query), fragment-only "#f" (current path and query).
 * Errors, in this order: HTTP_E_ARG; HTTP_E_REDIRECT when !http_is_redirect_status(status) or `location`
 * is empty; HTTP_E_TOO_MANY_REDIRECTS when r->count >= HTTP_MAX_REDIRECTS; HTTP_E_SCHEME for any other
 * scheme than https (a leading "<letter>[<letter|digit|+|-|.>]*:" before any '/', '?' or '#') ;
 * HTTP_E_URL for "https:" not followed by "//", for a bad byte (<= 0x20, 0x7F, >= 0x80, '\\'), userinfo, bad host or port; HTTP_E_OVERFLOW.
 * Buffer requirement: `out` is also the working space of the dot-segment removal, and an overflow is sticky
 * (core_buf_truncate does not clear it). So HTTP_E_OVERFLOW is reported when the merged path, as it is BEFORE
 * dot-segment removal, does not fit in `out` (after the scheme, host and port already written), even if the
 * path left after removing "." and ".." would have fitted. `out` must therefore have room for the whole
 * absolute URL as if no segment were removed.
 * r->count is incremented only when HTTP_OK is returned. `cur` must come from http_url_split. */
http_status_t http_redirect_resolve(http_redirects_t *r, const http_url_t *cur, int status, core_str_t location,
                                    core_buf_t *out);

/* ---- challenge detection ---------------------------------------------------------------------------------- */

/* Spec section 2. Returns HTTP_E_BROWSER_CHECK when
 *   - `status` is 403 or 503 and the first min(body_prefix.len, HTTP_CHALLENGE_PREFIX) bytes of
 *     `body_prefix` contain "ddos-guard" in any letter case; or
 *   - http_is_redirect_status(status) and `location` contains the exact (case-sensitive) text "check=1"
 *     anywhere (so "check=10" matches too).
 * Otherwise HTTP_OK. HTTP_E_ARG for `ptr == NULL` with `len > 0`. */
http_status_t http_detect_challenge(int status, core_str_t location, core_str_t body_prefix);

#endif
