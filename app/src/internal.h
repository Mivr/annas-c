#ifndef APP_INTERNAL_H
#define APP_INTERNAL_H
/* app/src/internal.h: private declarations shared by the app sources. Not part of the public interface.
 *
 * Memory plan (no malloc, no statics): the CLI has no scratch parameter, so app_cli_run borrows the unused
 * capacity behind the content of its `out` / `errout` buffers (app_span_of); the MCP server borrows the rest of
 * its parse arena after the request DOM. See the report of task A015. */

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
#define APP_ALIGN16(n) (((n) + (size_t)15u) & ~(size_t)15u)

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

typedef enum { APP_OP_OK = 0, APP_OP_FAIL, APP_OP_INTERNAL } app_op_status_t;

/* config -> annas_ctx_init -> operation. FAIL: job->err is set. INTERNAL: deps unusable. */
app_op_status_t app_op_run(const app_deps_t *d, const app_op_t *op, app_job_t *job);
/* Appends the text of the outcome without a newline: the success text, the error line, or the internal error. */
void app_op_text(const app_op_t *op, const app_job_t *job, app_op_status_t st, core_buf_t *b);

/* ANNAS_SECRET_KEY from deps->env; empty when unset or when the env port is unusable. */
core_str_t app_key(const app_deps_t *d);
/* Appends src with every occurrence of the key (raw, else percent-encoded) replaced by "REDACTED". */
void app_scrub_put(core_buf_t *dst, core_str_t src, core_str_t key);
/* Appends the content of `text` scrubbed; a text that overflowed puts dst into its overflow state. */
void app_emit(core_buf_t *dst, const core_buf_t *text, core_str_t key);

/* The first APP_ECHO_MAX bytes of `in`, bytes < 0x20 and 0x7f as '?', into out[0..APP_ECHO_MAX); returns the length. */
size_t app_echo_cut(core_str_t in, char *out);
/* Replaces every byte that is not part of a well-formed UTF-8 sequence by '?'. */
void app_utf8_clean(char *s, size_t n);
/* Appends s as the inside of a JSON string (the writer's escaping of json/json.h). */
void app_put_escaped(core_buf_t *b, core_str_t s);

/* Unused capacity behind the content of a buffer (after its NUL), and aligned allocation from such a span. */
typedef struct {
    unsigned char *p;
    size_t left;
} app_span_t;
app_span_t app_span_of(const core_buf_t *b);
void *app_span_take(app_span_t *sp, size_t n);

#endif
