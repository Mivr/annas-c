#include <string.h>

#include "http/src/internal.h"

bool http_parser_init(http_parser_t *p) {
    if (p == NULL) {
        return false;
    }
    memset(p, 0, sizeof *p);
    p->state = HTTP_ST_STATUS;
    return true;
}

/* Collects line bytes; sets *done when a full CRLF-terminated line is in p->line[0..line_len). */
static http_status_t line_feed(http_parser_t *p, core_str_t *in, bool *done) {
    *done = false;
    while (in->len > 0) {
        unsigned char c = (unsigned char)*in->ptr;
        in->ptr++;
        in->len--;
        if ((p->flags & HTTP_F_CR) != 0u) {
            p->flags &= ~HTTP_F_CR;
            *done = (c == '\n');
            return c == '\n' ? HTTP_OK : HTTP_E_LINE_ENDING;
        }
        if (c == '\r') {
            p->flags |= HTTP_F_CR;
        } else if (c == '\n') {
            return HTTP_E_LINE_ENDING;
        } else if (p->line_len >= HTTP_MAX_LINE) {
            return HTTP_E_LINE_TOO_LONG;
        } else {
            p->line[p->line_len] = c;
            p->line_len++;
        }
    }
    return HTTP_OK;
}

static http_status_t line_done(http_parser_t *p, size_t n, http_event_t *ev) {
    switch (p->state) {
    case HTTP_ST_STATUS:
        return http_status_line(p, n, ev);
    case HTTP_ST_HEADERS:
        return n == 0 ? http_headers_end(p, ev) : http_header_line(p, n, false, ev);
    case HTTP_ST_TRAILERS:
        if (n == 0) {
            ev->kind = HTTP_EV_END;
            p->state = HTTP_ST_DONE;
            return HTTP_OK;
        }
        return http_header_line(p, n, true, ev);
    default:
        return http_chunk_size_line(p, n);
    }
}

static http_status_t step_line(http_parser_t *p, core_str_t *in, http_event_t *ev) {
    bool done;
    size_t n;
    http_status_t st = line_feed(p, in, &done);
    if (st != HTTP_OK || !done) {
        return st;
    }
    n = p->line_len;
    p->line_len = 0;
    return line_done(p, n, ev);
}

/* Hands out up to `limit` bytes of the input as a BODY event. */
static void take_body(core_str_t *in, uint64_t limit, http_event_t *ev) {
    size_t n = in->len;
    if ((uint64_t)n > limit) {
        n = (size_t)limit;
    }
    ev->kind = HTTP_EV_BODY;
    ev->data = (core_str_t){in->ptr, n};
    in->ptr += n;
    in->len -= n;
}

static http_status_t step_counted(http_parser_t *p, core_str_t *in, http_event_t *ev) {
    take_body(in, p->remaining, ev);
    p->remaining -= ev->data.len;
    if (p->remaining == 0) {
        p->state = (p->state == HTTP_ST_BODY_LENGTH) ? HTTP_ST_END_PENDING : HTTP_ST_CHUNK_CRLF;
        p->aux = 0;
    }
    return HTTP_OK;
}

/* One byte of the CRLF that follows chunk data. */
static http_status_t step_chunk_crlf(http_parser_t *p, core_str_t *in) {
    char c = *in->ptr;
    in->ptr++;
    in->len--;
    if (p->aux == 0u && c == '\r') {
        p->aux = 1;
        return HTTP_OK;
    }
    if (p->aux == 1u && c == '\n') {
        p->aux = 0;
        p->state = HTTP_ST_CHUNK_SIZE;
        return HTTP_OK;
    }
    return HTTP_E_CHUNK;
}

static http_status_t step(http_parser_t *p, core_str_t *in, http_event_t *ev) {
    switch (p->state) {
    case HTTP_ST_STATUS:
    case HTTP_ST_HEADERS:
    case HTTP_ST_TRAILERS:
    case HTTP_ST_CHUNK_SIZE:
        return step_line(p, in, ev);
    case HTTP_ST_BODY_LENGTH:
    case HTTP_ST_CHUNK_DATA:
        return step_counted(p, in, ev);
    case HTTP_ST_BODY_CLOSE:
        take_body(in, UINT64_MAX, ev);
        return HTTP_OK;
    case HTTP_ST_CHUNK_CRLF:
        return step_chunk_crlf(p, in);
    case HTTP_ST_END_PENDING:
        ev->kind = HTTP_EV_END;
        p->state = HTTP_ST_DONE;
        return HTTP_OK;
    default:
        return HTTP_E_EXTRA;
    }
}

static http_status_t drive(http_parser_t *p, core_str_t *in, http_event_t *ev) {
    for (;;) {
        http_status_t st;
        if (in->len == 0 && p->state != HTTP_ST_END_PENDING) {
            return HTTP_OK;
        }
        st = step(p, in, ev);
        if (st != HTTP_OK || ev->kind != HTTP_EV_NONE) {
            return st;
        }
    }
}

http_status_t http_parser_next(http_parser_t *p, core_str_t *input, http_event_t *ev) {
    http_status_t st;
    if (ev != NULL) {
        memset(ev, 0, sizeof *ev);
    }
    if (p == NULL || input == NULL || ev == NULL || (input->ptr == NULL && input->len > 0)) {
        return HTTP_E_ARG;
    }
    if (p->err != 0) {
        return (http_status_t)p->err;
    }
    st = drive(p, input, ev);
    if (st != HTTP_OK) {
        memset(ev, 0, sizeof *ev);
        p->err = (int)st;
    }
    return st;
}

http_status_t http_parser_eof(http_parser_t *p, http_event_t *ev) {
    if (ev != NULL) {
        memset(ev, 0, sizeof *ev);
    }
    if (p == NULL || ev == NULL) {
        return HTTP_E_ARG;
    }
    if (p->err != 0) {
        return (http_status_t)p->err;
    }
    if (p->state == HTTP_ST_DONE) {
        return HTTP_OK;
    }
    if (p->state == HTTP_ST_END_PENDING || p->state == HTTP_ST_BODY_CLOSE) {
        ev->kind = HTTP_EV_END;
        p->state = HTTP_ST_DONE;
        return HTTP_OK;
    }
    p->err = (int)HTTP_E_TRUNCATED;
    return HTTP_E_TRUNCATED;
}
