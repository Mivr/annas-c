#include "annas/src/internal.h"

#include <string.h>

void exch_init(exch_t *x, annas_ctx_t *ctx, annas_error_t *err, const ws_t *w) {
    x->ctx = ctx;
    x->err = err;
    x->parser = w->parser;
    x->io = w->io;
    x->pending.ptr = NULL;
    x->pending.len = 0;
    x->open = false;
    x->eof = false;
}

annas_code_t exch_fail(const exch_t *x, annas_code_t code, int status, const char *text) {
    return msg_text(x->err, code, status, ctx_key(x->ctx), text);
}

/* Port error of a connect (connect_failed) or of a write/read to the annas error. */
static annas_code_t port_fail(const exch_t *x, ports_err_t e, bool connect_failed) {
    if (e == PORTS_E_TIMEOUT) {
        return exch_fail(x, ANNAS_E_TIMEOUT, 0, "operation timed out");
    }
    if (e == PORTS_E_TLS) {
        return exch_fail(x, ANNAS_E_NET, 0, "TLS error");
    }
    return exch_fail(x, ANNAS_E_NET, 0, connect_failed ? "connection failed" : "network error");
}

annas_code_t exch_open(exch_t *x, core_str_t host, uint16_t port, core_str_t request) {
    annas_ctx_t *c = x->ctx;
    (void)http_parser_init(x->parser); /* false only for NULL; the parser is the workspace's (ws_split) */
    annas_code_t r = op_check(c, x->err);
    if (r != ANNAS_OK) {
        return r;
    }
    ports_err_t e = c->stream.ops->connect(c->stream.ctx, host, port, c->deadline_ms);
    if (e != PORTS_OK) {
        return port_fail(x, e, true);
    }
    x->open = true;
    r = op_check(c, x->err);
    if (r != ANNAS_OK) {
        return r;
    }
    e = c->stream.ops->write(c->stream.ctx, request.ptr, request.len);
    return e == PORTS_OK ? ANNAS_OK : port_fail(x, e, false);
}

void exch_close(exch_t *x) {
    if (x->open) {
        /* A failing close cannot change the outcome that is already decided. */
        (void)x->ctx->stream.ops->close(x->ctx->stream.ctx);
        x->open = false;
    }
}

/* Reads once into the io buffer; a zero-byte read is the end of the stream. */
static annas_code_t fill(exch_t *x) {
    annas_ctx_t *c = x->ctx;
    annas_code_t r = op_check(c, x->err);
    if (r != ANNAS_OK) {
        return r;
    }
    size_t n = 0; /* read sets it on PORTS_OK (ports.h); a port that does not reads as end of stream */
    ports_err_t e = c->stream.ops->read(c->stream.ctx, x->io, WS_IO_CAP, &n);
    if (e != PORTS_OK) {
        return port_fail(x, e, false);
    }
    if (n == 0) {
        x->eof = true;
    }
    x->pending.ptr = (const char *)x->io;
    x->pending.len = n;
    return ANNAS_OK;
}

/* The stream ended. exch_event is never called again after END or an error (every caller stops there), so
 * http_parser_eof gives END or HTTP_E_TRUNCATED here. */
static annas_code_t at_eof(exch_t *x, http_event_t *ev) {
    if (http_parser_eof(x->parser, ev) != HTTP_OK) {
        return exch_fail(x, ANNAS_E_NET, 0, "connection closed before the response was complete");
    }
    return ANNAS_OK;
}

annas_code_t exch_event(exch_t *x, http_event_t *ev) {
    http_status_t st = http_parser_next(x->parser, &x->pending, ev);
    if (st != HTTP_OK || (ev->kind == HTTP_EV_END && x->pending.len > 0)) {
        return exch_fail(x, ANNAS_E_PARSE, 0, "malformed HTTP response");
    }
    if (ev->kind != HTTP_EV_NONE) {
        return ANNAS_OK;
    }
    if (x->eof) {
        return at_eof(x, ev);
    }
    return fill(x); /* no event yet: the caller sees kind NONE and calls again */
}

void head_note(head_t *h, const http_event_t *ev, char *loc) {
    core_str_t none = {NULL, 0};
    if (ev->kind == HTTP_EV_STATUS) {
        h->status = ev->status;
        return;
    }
    if (ev->kind != HTTP_EV_HEADER || h->has_loc || !core_str_eq(ev->name, core_str_from_cstr("location"))) {
        return;
    }
    h->has_loc = true;
    if (http_detect_challenge(h->status, ev->value, none) == HTTP_E_BROWSER_CHECK) {
        h->challenge = true;
    }
    if (loc != NULL) {
        /* A header line is at most HTTP_MAX_LINE bytes (the parser refuses longer ones), so is its value, and the
         * value always points into the parser's line buffer, even when empty. */
        memcpy(loc, ev->value.ptr, ev->value.len);
        h->loc_len = ev->value.len;
    }
}
