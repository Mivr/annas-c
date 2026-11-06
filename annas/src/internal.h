/* annas/src/internal.h: private helpers shared by the annas sources. Not part of the public interface. */
#ifndef ANNAS_INTERNAL_H
#define ANNAS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "annas/annas.h"
#include "core/parse.h"
#include "fsname/fsname.h"
#include "http/http.h"
#include "json/json.h"
#include "url/url.h"

/* Keeps the stack frames of the call chain separate (each frame must stay within 512 bytes). */
#define NOINLINE __attribute__((noinline))

/* ---- workspace layout (ctx.c) -----------------------------------------------------------------------------------
 * parser | result | io buffer | body buffer | rest. The result holds everything an operation builds, so the caller's
 * result struct is only written once the operation has succeeded. `rest` is scratch: request target, URL buffers,
 * the JSON arena and the redaction output (never in use at the same time). */
#define WS_IO_CAP ((size_t)8192)
#define WS_BODY_CAP ANNAS_API_BODY_MAX
#define WS_URL_BUF (HTTP_MAX_URL + 1u)
#define WS_TARGET_CAP ((size_t)2048)

typedef struct {
    http_parser_t *parser;
    annas_download_result_t *res;
    unsigned char *io;
    unsigned char *body;
    unsigned char *rest;
    size_t rest_cap;
} ws_t;

void ws_split(const annas_ctx_t *ctx, ws_t *w);

static inline core_str_t ctx_key(const annas_ctx_t *ctx) { return ctx->cfg->key; }

/* Begins an operation: takes the start time and sets the absolute deadline (saturating). */
void op_begin(annas_ctx_t *ctx);
/* ANNAS_E_TIMEOUT (message set) when the clock has reached the deadline, else ANNAS_OK. */
annas_code_t op_check(annas_ctx_t *ctx, annas_error_t *err);
/* Key and download directory must be set and within their limits (E_CONFIG). */
annas_code_t check_config(const annas_ctx_t *ctx, annas_error_t *err, bool need_dir);
/* E_ARG "invalid argument". */
annas_code_t arg_invalid(annas_error_t *err, core_str_t key);

/* ---- messages (msg.c) ------------------------------------------------------------------------------------------- */
#define MSG_NO_CUT SIZE_MAX

void msg_ok(annas_error_t *err);
/* Sets code/status and the message "<prefix><echo>", finalised as the header describes (control bytes, key
 * redaction); the echo part is cut to `cut` bytes AFTER that. Returns `code`. */
annas_code_t msg_fail(annas_error_t *err, annas_code_t code, int status, core_str_t key, core_str_t prefix,
                      core_str_t echo, size_t cut);
annas_code_t msg_text(annas_error_t *err, annas_code_t code, int status, core_str_t key, const char *text);
/* lead + decimal(n) + tail, into buf[0..cap) (always fits for the callers); returns the text. */
core_str_t msg_u64(char *buf, size_t cap, const char *lead, uint64_t n, const char *tail);

/* ---- redaction (redact.c) ---------------------------------------------------------------------------------------- */
/* A text made of two consecutive slices; with `san` set, bytes < 0x20 and 0x7f read as '?'. */
typedef struct {
    core_str_t a;
    core_str_t b;
    bool san;
} cat_t;

size_t cat_len(const cat_t *t);
unsigned char cat_at(const cat_t *t, size_t i);
/* Length of the key (raw, else percent-encoded) found at `pos` of `t`, or 0. A zero-length key never matches. */
size_t key_match(const cat_t *t, size_t pos, core_str_t key);
/* True iff `hay` contains the key, raw or percent-encoded. */
bool key_in(core_str_t hay, core_str_t key);
/* url_redact_key(src) into the rest of the arena; false when it does not fit. */
bool redact_into(core_arena_t *ar, core_str_t src, core_str_t *out);

/* ---- names (name.c) ---------------------------------------------------------------------------------------------- */
/* True for an absent format or 1..16 ASCII letters/digits. */
bool name_format_ok(core_str_t format);
/* Fills res->name, name_len, path, path_len. md5 is the normalised hash. */
annas_code_t name_build(const annas_ctx_t *ctx, annas_error_t *err, const annas_request_t *req, const char *md5,
                        annas_download_result_t *res);
/* Argument validation of a request (pointers, md5, and for downloads title/format). md5_out gets the lower-case hash. */
annas_code_t req_check(const annas_ctx_t *ctx, annas_error_t *err, const annas_request_t *req, char *md5_out,
                       bool for_download);

/* ---- one HTTP exchange (exchange.c) ------------------------------------------------------------------------------ */
typedef struct {
    annas_ctx_t *ctx;
    annas_error_t *err;
    http_parser_t *parser;
    unsigned char *io;
    core_str_t pending;
    bool open;
    bool eof;
} exch_t;

typedef struct {
    int status;
    bool challenge;   /* a Location header that is a verification redirect */
    bool has_loc;
    size_t loc_len;
} head_t;

void exch_init(exch_t *x, annas_ctx_t *ctx, annas_error_t *err, const ws_t *w);
annas_code_t exch_fail(const exch_t *x, annas_code_t code, int status, const char *text);
/* Checks the deadline, connects, checks again, writes the request. */
annas_code_t exch_open(exch_t *x, core_str_t host, uint16_t port, core_str_t request);
/* Next parser event; reads from the stream as needed. END is only returned with no bytes left over. */
annas_code_t exch_event(exch_t *x, http_event_t *ev);
void exch_close(exch_t *x);
/* Records the status and the first Location header (copied to `loc` when non-NULL). */
void head_note(head_t *h, const http_event_t *ev, char *loc, size_t loc_cap);

/* ---- the API call (api.c) ---------------------------------------------------------------------------------------- */
annas_code_t api_fetch(annas_ctx_t *ctx, const annas_request_t *req, const char *md5, const ws_t *w,
                       annas_error_t *err);

#define BROWSER_MESSAGE \
    "the site requires browser verification; use download-by-md5 with a hash found in a browser"

#endif
