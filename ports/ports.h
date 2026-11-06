/* ports/ports.h: the only ways IO reaches the pure modules. Four small interfaces, each a CONST table of
 * function pointers plus an opaque context pointer, so `annas` can be tested with the fakes in
 * ports/fakes.h and wired to real adapters (TLS stream, POSIX fs/env/clock) by `cli` and `mcp`.
 *
 * Calling convention (all ports): call `p.ops->fn(p.ctx, ...)`. The module never retains a pointer
 * passed in beyond the call, never allocates, and every call returns a status that MUST be checked.
 * On any non-OK status every out-parameter is left as the caller initialised it, except where stated.
 * A NULL pointer argument that the function needs (other than a zero-length buffer, see below) is an
 * argument error: PORTS_E_ARG / PORTS_FS_E_ARG.
 */
#ifndef PORTS_PORTS_H
#define PORTS_PORTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/str.h"

/* ---- stream: one byte-stream connection (TLS client in production) ---------------------------------- */

typedef enum {
    PORTS_OK = 0,
    PORTS_E_NET,     /* resolve/connect/transport failure, or a broken protocol sequence in a fake */
    PORTS_E_TIMEOUT, /* the deadline passed */
    PORTS_E_TLS,     /* handshake or certificate failure */
    PORTS_E_CLOSED,  /* the stream is not open (never connected, failed to connect, or closed) */
    PORTS_E_ARG      /* NULL/zero-capacity argument that the call needs */
} ports_err_t;

/* One stream = at most one connection at a time. States: not open -> (connect OK) -> open -> (close) ->
 * not open. Zero-length write is an OK no-op (buf may then be NULL).
 *
 * connect(host, port, deadline_ms): `host` is a non-empty name, `port` >= 1. `deadline_ms` is ABSOLUTE
 *   time on the ports_clock_t the program uses (0 = no deadline); it bounds the connect and every later
 *   read and write on this connection. A deadline already passed (now >= deadline_ms) -> PORTS_E_TIMEOUT.
 *   Failure leaves the stream not open.
 * write(buf, len): sends all len bytes or fails (no partial success is reported).
 * read(buf, cap, &n): cap >= 1. On OK, *n = number of bytes stored (1..cap), or 0 at end of stream (EOF).
 *   After EOF, further reads return OK with n == 0. Not open -> PORTS_E_CLOSED; cap == 0 -> PORTS_E_ARG.
 * close(): open -> PORTS_OK and not open afterwards; not open -> PORTS_E_CLOSED (harmless). */
typedef struct {
    ports_err_t (*connect)(void *ctx, core_str_t host, uint16_t port, uint64_t deadline_ms);
    ports_err_t (*write)(void *ctx, const void *buf, size_t len);
    ports_err_t (*read)(void *ctx, void *buf, size_t cap, size_t *n);
    ports_err_t (*close)(void *ctx);
} ports_stream_ops_t;

typedef struct {
    const ports_stream_ops_t *ops;
    void *ctx;
} ports_stream_t;

/* ---- fs: temp file + atomic commit inside ONE download directory ------------------------------------ */

typedef enum {
    PORTS_FS_OK = 0,
    PORTS_FS_E_IO,      /* any other failure */
    PORTS_FS_E_EXISTS,  /* commit target name already exists */
    PORTS_FS_E_NOSPACE, /* no room: no free temp slot, or the data does not fit */
    PORTS_FS_E_ARG      /* NULL argument, unknown/stale file handle, or an invalid final name */
} ports_fs_err_t;

/* Opaque handle of an open temp file. 0 (PORTS_FS_NO_FILE) is never a valid handle. A handle stops being
 * valid after a successful commit or discard; using it again is PORTS_FS_E_ARG.
 *
 * create_temp(&file): a new empty temp file, invisible by name. On OK *file is a valid handle.
 * write(file, buf, len): appends len bytes; all-or-nothing (on any failure the content is unchanged).
 *   len == 0 is an OK no-op (buf may be NULL).
 * commit(file, final_name): atomically makes the content visible as `final_name` (a plain file name:
 *   1..255 bytes, no '/', no NUL, not "." and not ".."; else PORTS_FS_E_ARG). If a file of that name
 *   already exists -> PORTS_FS_E_EXISTS and nothing is overwritten. On any failure the temp file stays open
 *   and valid (the caller must still discard it). On OK the handle is consumed.
 * discard(file): removes the temp file and consumes the handle; nothing becomes visible. */
typedef uint32_t ports_fs_file_t;
#define PORTS_FS_NO_FILE ((ports_fs_file_t)0)

typedef struct {
    ports_fs_err_t (*create_temp)(void *ctx, ports_fs_file_t *file);
    ports_fs_err_t (*write)(void *ctx, ports_fs_file_t file, const void *buf, size_t len);
    ports_fs_err_t (*commit)(void *ctx, ports_fs_file_t file, core_str_t final_name);
    ports_fs_err_t (*discard)(void *ctx, ports_fs_file_t file);
} ports_fs_ops_t;

typedef struct {
    const ports_fs_ops_t *ops;
    void *ctx;
} ports_fs_t;

/* ---- env: read-only configuration lookup ------------------------------------------------------------ */

/* get(name, &value): true iff `name` is set; then *value is its value (possibly empty: set-but-empty is
 * still found). Names are compared byte for byte, case-sensitively; an empty name is never found.
 * Not found (or NULL `value`): returns false; if `value` is non-NULL it is set to {NULL, 0}. The returned
 * slice stays valid for the life of the environment object. */
typedef struct {
    bool (*get)(void *ctx, core_str_t name, core_str_t *value);
} ports_env_ops_t;

typedef struct {
    const ports_env_ops_t *ops;
    void *ctx;
} ports_env_t;

/* ---- clock: monotonic milliseconds ------------------------------------------------------------------ */

/* now_ms(): a never-decreasing millisecond count with an arbitrary origin. */
typedef struct {
    uint64_t (*now_ms)(void *ctx);
} ports_clock_ops_t;

typedef struct {
    const ports_clock_ops_t *ops;
    void *ctx;
} ports_clock_t;

#endif
