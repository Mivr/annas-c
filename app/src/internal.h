#ifndef APP_INTERNAL_H
#define APP_INTERNAL_H
/* app/src/internal.h: private declarations shared by the app sources. Not part of the public interface.
 *
 * Memory plan (no malloc, no statics): app_scratch_t is the layout of deps->scratch; the CLI and the MCP tools
 * build their outcome text there, and the key scrubbing runs there. The MCP reply under construction lives in the
 * parse arena behind the request DOM. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "annas/annas.h"
#include "app/app.h"
#include "core/arena.h"
#include "core/buf.h"
#include "core/str.h"

#define APP_INTERNAL_LINE "annas-c: internal error: invalid dependencies"
#define APP_ECHO_MAX ((size_t)100)
#define APP_TEXT_CAP ((size_t)8192)

/* One operation (download or download-url) as the CLI and the MCP tools describe it. */
typedef struct {
    annas_request_t req;
    int64_t timeout_seconds;
    bool download;
} app_op_t;

/* Everything an operation builds; too big for a stack frame, so it lives in borrowed memory. */
typedef struct {
    annas_config_t cfg;
    annas_ctx_t ctx;
    annas_error_t err;
    annas_download_result_t dl; /* dl.fast is the result of a download-url operation */
} app_job_t;

#define APP_FIN_MAX ((size_t)65536)

/* deps->scratch: the first pass of the scrubbing, then the work area (job and outcome text), which the second
 * pass reuses once they are consumed. */
typedef struct {
    char p1[APP_FIN_MAX];
    union {
        struct {
            app_job_t job;
            char text[APP_TEXT_CAP];
        } w;
        char p2[APP_FIN_MAX];
    } u;
} app_scratch_t;

_Static_assert(sizeof(app_scratch_t) <= APP_SCRATCH_MIN, "scratch layout");

typedef enum { APP_OP_OK = 0, APP_OP_FAIL, APP_OP_INTERNAL } app_op_status_t;

/* config -> annas_ctx_init -> operation. FAIL: job->err is set. INTERNAL: deps unusable. */
app_op_status_t app_op_run(const app_deps_t *d, const app_op_t *op, app_job_t *job);
/* Appends the text of the outcome without a newline: the success text, the error line, or the internal error. */
void app_op_text(const app_op_t *op, const app_job_t *job, app_op_status_t st, core_buf_t *b);

/* ANNAS_SECRET_KEY from deps->env; empty when unset or when the env port is unusable. */
core_str_t app_key(const app_deps_t *d);
/* The view of deps->scratch (valid only after app_scratch_ok). */
bool app_scratch_ok(const app_deps_t *d);
app_scratch_t *app_scratch(const app_deps_t *d);
/* The two scrubbing passes over `src` (see app.h): `res` is the final text, at most APP_FIN_MAX bytes, in `src`
 * (no key) or in the scratch (src may live in the work area of the scratch); false when it does not fit. */
bool app_finalise(const app_deps_t *d, core_str_t src, core_str_t *res);
/* Finalises a CLI outcome text (always fits, see ops.c) and appends it with ONE put. */
void app_emit(const app_deps_t *d, core_buf_t *dst, core_str_t text);

/* The first APP_ECHO_MAX bytes of `in`, bytes < 0x20 and 0x7f as '?', into out[0..APP_ECHO_MAX); returns the length. */
size_t app_echo_cut(core_str_t in, char *out);
/* Replaces every byte that is not part of a well-formed UTF-8 sequence by '?'. s[n] must be readable and 0. */
void app_utf8_clean(char *s, size_t n);
/* Appends s as the inside of a JSON string (the writer's escaping of json/json.h). */
void app_put_escaped(core_buf_t *b, core_str_t s);

#endif
