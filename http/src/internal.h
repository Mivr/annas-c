/* http/src/internal.h: private helpers shared by the http sources. Not part of the public interface. */
#ifndef HTTP_INTERNAL_H
#define HTTP_INTERNAL_H

#include "http/http.h"

/* Parser states (http_parser_t.state). */
enum {
    HTTP_ST_STATUS = 0, /* collecting the status line */
    HTTP_ST_HEADERS,    /* collecting header lines */
    HTTP_ST_BODY_LENGTH,
    HTTP_ST_BODY_CLOSE,
    HTTP_ST_CHUNK_SIZE,
    HTTP_ST_CHUNK_DATA,
    HTTP_ST_CHUNK_CRLF,
    HTTP_ST_TRAILERS,
    HTTP_ST_END_PENDING, /* END is ready, needs no input */
    HTTP_ST_DONE         /* END delivered */
};

/* Parser flag bits (http_parser_t.flags). */
#define HTTP_F_CR 1u     /* a CR was seen in the current line */
#define HTTP_F_MINOR1 2u /* HTTP/1.1 */
#define HTTP_F_CL 4u     /* a Content-Length line was seen */
#define HTTP_F_TE 8u     /* a Transfer-Encoding line was seen */

/* True for a byte that no URL may contain: <= 0x20, 0x7F, >= 0x80 or backslash. */
bool http_url_byte_bad(unsigned char c);

/* Validates "host[:port]" (no userinfo, no brackets). Errors are HTTP_E_URL. */
http_status_t http_authority_parse(core_str_t auth, core_str_t *host, uint16_t *port);

/* Handles one completed head/trailer line of `n` bytes in p->line (n > 0). Fills *ev for the line's event and
 * does the framing bookkeeping. `trailer` selects trailer rules. */
http_status_t http_header_line(http_parser_t *p, size_t n, bool trailer, http_event_t *ev);

/* Handles a completed status line of `n` bytes in p->line. */
http_status_t http_status_line(http_parser_t *p, size_t n, http_event_t *ev);

/* Handles the blank line ending the head: fills *ev (HEADERS_END) and picks the next state. */
http_status_t http_headers_end(http_parser_t *p, http_event_t *ev);

/* Handles a completed chunk-size line of `n` bytes in p->line; sets remaining and the next state. */
http_status_t http_chunk_size_line(http_parser_t *p, size_t n);

#endif
