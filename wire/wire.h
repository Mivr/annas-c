/* wire/wire.h: the composition root of annas-c as a TESTED module. The two binaries (bin/) are a few lines each: they
 * hold the big buffers as static storage and call wire_run(), which does everything else. wire builds the REAL
 * dependencies of app (posix env/clock/fs, the TLS stream over a socket transport), reads the `.env` file and the CA
 * bundle, runs the command line or the MCP loop, and writes the texts. Every effect that is not already behind a port
 * goes through the seams below (wire_sys_t, plus the optional ones of wire_io_t), so the tests drive it with fakes;
 * `wire_io_libc` is the production set. Spec sections 1, 2, 5, 6; app/app.h is the behaviour of the operations.
 *
 *   wire_run   argv + envp + seams -> exit code. Mode WIRE_CLI is the `annas-c` binary, WIRE_MCP is `annas-c-mcp`.
 *
 * ---- Memory ---------------------------------------------------------------------------------------------------------
 * wire has no globals and no malloc. wire_mem_t is the whole working memory (about 8 MiB: far too big for a stack, the
 * binary declares ONE static object). wire_run initialises everything it uses on every call, so the object may hold
 * anything on entry (zero-initialised static storage is typical) and may be reused for a second call. The fields
 * after `state` are implementation state; treat the whole object as opaque.
 *
 * ---- Order of work in wire_run (the first failure ends the run; its text goes to stderr, nothing else is printed) ---
 *   1. Arguments. m, io, io->sys or one of the four wire_sys_t functions NULL, argv NULL, argc < 1, an invalid mode:
 *      returns WIRE_EXIT_INTERNAL (1), touches no seam and writes nothing. envp NULL = an empty environment.
 *   2. The command line. WIRE_CLI: app_cli_parse. A usage error echoes a cut of an argument the user typed, so wire
 *      first reads the `.env` file the way step 3 does but BEST EFFORT (any failure of it is ignored: no error text, no
 *      change of the usage error), layers it under the process environment as in step 4, and writes app's text to
 *      stderr with every occurrence of the layered ANNAS_SECRET_KEY replaced by "REDACTED" (one pass, left to right,
 *      non-overlapping; nothing is replaced when the key is unset or empty); exit 2. A usage error reads no other file
 *      (no CA bundle) and touches no other seam. --help and --version are run by app_cli_run and written to stdout,
 *      exit 0; they need no file, no environment and no network: no seam function other than write_out is called.
 *      WIRE_MCP: argc == 1 runs the loop (step 7); argc == 2 with "--help", "-h" or "--version" is the same as in
 *      WIRE_CLI; anything else writes WIRE_ERR_MCP_ARGS (it does not echo the argument, so nothing is read for it) and
 *      returns 2.
 *   3. The `.env` file (download, download-url, mcp): wire_sys_t.read_file(WIRE_DOTENV_PATH, ..., WIRE_DOTENV_MAX + 1).
 *      -ENOENT is fine (no file; any other negative result is WIRE_ERR_DOTENV_READ, exit 2). A result of
 *      WIRE_DOTENV_MAX + 1 bytes is WIRE_ERR_DOTENV_BIG; exactly WIRE_DOTENV_MAX bytes is accepted. app_dotenv_parse
 *      refusing it (more than APP_DOTENV_MAX_ENTRIES) is WIRE_ERR_DOTENV_MANY. All exit 2.
 *   4. The environment: posix_env over `envp`, with the `.env` entries layered UNDER it (app_env_layer_*: the process
 *      environment wins, even over a value set to the empty string). This layered port is app_deps_t.env, so the key
 *      from either source is the one app scrubs from every output.
 *   5. The CA bundle (downloads, download-url and mcp; so no network operation can start without one). Path rule: the
 *      layered variable SSL_CERT_FILE, when set and not empty, names the ONLY candidate (no fallback). Otherwise the
 *      candidates are tried in the order of WIRE_CA_PATHS and the first for which read_file does not fail is used.
 *      Each is read with cap WIRE_CA_MAX + 1. A path longer than WIRE_PATH_MAX - 1 bytes or holding a NUL byte is not
 *      passed to the seam and counts as unreadable. Outcomes: SSL_CERT_FILE unreadable -> WIRE_ERR_CA_READ; no
 *      candidate readable -> WIRE_ERR_CA_NONE; the file that was read has 0 bytes -> WIRE_ERR_CA_EMPTY, more than
 *      WIRE_CA_MAX bytes -> WIRE_ERR_CA_BIG (the choice is final at the first file that read successfully: later
 *      candidates are not tried). All exit 2. The bytes are handed to the TLS stream as they are, plus one NUL byte;
 *      whether they are good certificates is the TLS stream's business (a bad bundle makes the connect fail, which
 *      app reports as E_NET "TLS error", exit 5).
 *   6. The dependencies: posix_clock over io->clock_sys (NULL = the libc table), the TLS stream over io->transport
 *      (NULL = tls_stream_sock over that clock) with the wall clock wire_sys_t.unix_seconds, the CA bundle and the
 *      heap of wire_mem_t; a posix_fs LAZILY opened on ANNAS_DOWNLOAD_PATH (see below); the layered env. The
 *      workspace and the scratch of wire_mem_t are app's.
 *   7. The command. WIRE_CLI download / download-url: app_cli_run, then the texts are written (stdout first, then
 *      stderr; an empty text is not written at all, no write_out call) and app's exit code is returned. WIRE_CLI `mcp`
 *      and WIRE_MCP: the MCP loop. The TLS stream is deinitialised and the download directory closed before return.
 *
 * Lazy download directory. posix_fs_open (through io->fs_sys, NULL = posix_fs_sys_libc) happens at the FIRST
 * create_temp of the run, on the layered ANNAS_DOWNLOAD_PATH (the first create_temp happens only after annas got a
 * 200 for the file); download-url, every failure before that, and every MCP line that is not a successful download
 * never open it. The directory stays open for the rest of the run. A path that is not set, longer than ANNAS_DIR_MAX,
 * holds a NUL byte, or that posix_fs_open refuses makes create_temp return PORTS_FS_E_IO (annas then reports E_FS,
 * exit 6). The other fs calls on a directory that is not open return PORTS_FS_E_ARG.
 *
 * ---- Writing and reading -------------------------------------------------------------------------------------------
 * write_out: wire writes ALL bytes of a text: a short count continues with the rest; -EINTR is retried (at most
 * WIRE_EINTR_MAX times in a row for one call; the next one is a failure); 0, a negative errno other than -EINTR, or
 * a count above len is a failure of that stream. A failure of stdout writes WIRE_ERR_STDOUT to stderr (best effort,
 * unless stderr itself failed) and the run returns WIRE_EXIT_IO (6) whatever else was decided, in the loop at once.
 * A failure of stderr returns 6 as well. read_in follows the same EINTR rule; a failure (negative errno other than
 * -EINTR, or a count above cap) writes WIRE_ERR_STDIN and returns 6.
 *
 * ---- The MCP loop (WIRE_MCP, and `annas-c mcp`) -----------------------------------------------------------------------
 * Newline-delimited JSON-RPC (spec section 5). wire reads stdin with read_in in blocks of at most WIRE_READ_CHUNK
 * bytes and cuts lines at '\n' (nothing else: a '\r' stays at the end of its line, where app's JSON parser treats it as
 * whitespace; a line of only '\r' gets no reply). Each line goes to app_mcp_handle (WITHOUT its '\n'); a REPLY is
 * written to stdout as the reply text followed by one '\n' and nothing else is ever written to stdout; NO_REPLY writes
 * nothing; any other result writes nothing (it cannot happen with the buffers of wire_mem_t) and the loop goes on. The
 * reply of a line is on its way (write_out returned) BEFORE the next read_in call: a client that waits for it never
 * deadlocks. stderr stays empty in a normal session.
 *   - A last line without '\n' before the end of input is handled like any other.
 *   - A line is held in wire_mem_t.line (APP_MCP_LINE_MAX + 1 bytes). One that has more than APP_MCP_LINE_MAX bytes is
 *     handed to app_mcp_handle as its first APP_MCP_LINE_MAX + 1 bytes (app answers -32600 "line longer than ..."),
 *     the rest of the line up to its '\n' is read and discarded by wire (the loop resyncs there), and the line after
 *     it is handled normally. Exactly APP_MCP_LINE_MAX bytes is handled as a normal line.
 *   - End of input (read_in returns 0) ends the loop with exit 0. An empty input is exit 0 with nothing written.
 *   - Bad input never ends the loop, hangs it or crashes it: the loop's only exits are end of input and the I/O
 *     failures above.
 *   - Every line is independent; the same TLS stream, directory and workspace serve all tool calls in turn.
 */
#ifndef WIRE_WIRE_H
#define WIRE_WIRE_H

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "annas/annas.h"
#include "app/app.h"
#include "core/buf.h"
#include "core/str.h"
#include "ports/ports.h"
#include "posix_clock/posix_clock.h"
#include "posix_env/posix_env.h"
#include "posix_fs/posix_fs.h"
#include "tls_stream/tls_stream.h"

/* ---- constants ------------------------------------------------------------------------------------------------- */

#define WIRE_DOTENV_PATH ".env"
#define WIRE_DOTENV_MAX ((size_t)16384)    /* longest accepted `.env`, in bytes */
#define WIRE_CA_MAX ((size_t)524288)       /* longest accepted CA bundle, in bytes */
#define WIRE_PATH_MAX ((size_t)4096)       /* a path (SSL_CERT_FILE) must be shorter than this, with its NUL */
#define WIRE_READ_CHUNK ((size_t)65536)    /* largest read_in request */
#define WIRE_OUT_CAP ((size_t)65552)       /* stdout / stderr text buffers: above app's 64 KiB reply limit + '\n' */
#define WIRE_PARSE_CAP ((size_t)3145728)   /* MCP parse memory (APP_MCP_LINE_MAX plus slack for the DOM and the reply) */
#define WIRE_TLS_HEAP ((size_t)TLS_STREAM_HEAP_BYTES(WIRE_CA_MAX + 1u))
#define WIRE_EINTR_MAX 100                 /* consecutive -EINTR results tolerated for one read or write call */

/* The system CA bundle candidates when SSL_CERT_FILE is not set, in this order. */
#define WIRE_CA_PATH_1 "/etc/ssl/certs/ca-certificates.crt"
#define WIRE_CA_PATH_2 "/etc/pki/tls/certs/ca-bundle.crt"
#define WIRE_CA_PATH_3 "/etc/ssl/cert.pem"
#define WIRE_CA_PATH_COUNT 3u

#define WIRE_STDOUT 1
#define WIRE_STDERR 2

/* ---- exit codes and texts (stderr, each ends with a newline, spec section 6) -------------------------------------- */

#define WIRE_EXIT_OK 0
#define WIRE_EXIT_INTERNAL 1  /* invalid arguments of wire_run */
#define WIRE_EXIT_CONFIG 2    /* E_ARG / E_CONFIG of wire itself, as in the spec */
#define WIRE_EXIT_IO 6        /* E_FS: stdin / stdout / stderr failed */

#define WIRE_ERR_DOTENV_READ "annas-c: E_CONFIG: cannot read .env in the current directory\n"
#define WIRE_ERR_DOTENV_BIG "annas-c: E_CONFIG: .env is larger than 16384 bytes\n"
#define WIRE_ERR_DOTENV_MANY "annas-c: E_CONFIG: .env has more than 64 entries\n"
#define WIRE_ERR_CA_NONE "annas-c: E_CONFIG: no CA bundle found; set SSL_CERT_FILE to a PEM file\n"
#define WIRE_ERR_CA_READ "annas-c: E_CONFIG: cannot read the CA bundle named by SSL_CERT_FILE\n"
#define WIRE_ERR_CA_EMPTY "annas-c: E_CONFIG: the CA bundle is empty\n"
#define WIRE_ERR_CA_BIG "annas-c: E_CONFIG: the CA bundle is larger than 524288 bytes\n"
#define WIRE_ERR_MCP_ARGS "annas-c: E_ARG: annas-c-mcp takes no arguments except --help and --version\n"
#define WIRE_ERR_STDIN "annas-c: E_FS: cannot read standard input\n"
#define WIRE_ERR_STDOUT "annas-c: E_FS: cannot write standard output\n"

/* ---- seams ------------------------------------------------------------------------------------------------------- */

/* All results are errno-style: a value >= 0 is the result, a negative value is -errno. The real functions
 * (`wire_sys_libc`) never return -EINTR for read_file (they retry); read_in and write_out may, and wire retries.
 *   read_file(path, buf, cap)   open `path` (NUL-terminated, relative to the current directory when not absolute),
 *                               read from its start until `cap` bytes or end of file, close; returns the byte count
 *                               (0..cap). cap >= 1. A directory or a missing file is an error (-EISDIR, -ENOENT).
 *   read_in(buf, cap)           read(0, buf, cap): 1..cap bytes, 0 = end of input. cap >= 1.
 *   write_out(fd, buf, len)     write(fd, buf, len) for fd WIRE_STDOUT or WIRE_STDERR: 1..len bytes. len >= 1. Any
 *                               other fd (0, 3, negative) writes nothing and returns -EBADF.
 *   unix_seconds()              CLOCK_REALTIME in whole seconds since 1970; 0 when unknown (then every certificate is
 *                               "not yet valid": fail closed).
 * `ctx` is passed through unchanged. */
typedef struct {
    int64_t (*read_file)(void *ctx, const char *path, void *buf, size_t cap);
    int64_t (*read_in)(void *ctx, void *buf, size_t cap);
    int64_t (*write_out)(void *ctx, int fd, const void *buf, size_t len);
    uint64_t (*unix_seconds)(void *ctx);
} wire_sys_t;

/* The real table (open/read/close, read(2) on fd 0, write(2), clock_gettime(CLOCK_REALTIME)). */
extern const wire_sys_t wire_sys_libc;

/* Everything wire_run needs from outside. Only `sys` is required; the others replace the production default when
 * non-NULL (tests, mostly):
 *   fs_sys      the posix_fs syscall table             default &posix_fs_sys_libc
 *   clock_sys   the monotonic clock table              default &posix_clock_sys_libc
 *   transport   the byte transport under the TLS       default tls_stream_sock_transport() */
typedef struct {
    const wire_sys_t *sys;
    void *sys_ctx;
    const posix_fs_sys_t *fs_sys;
    void *fs_sys_ctx;
    const posix_clock_sys_t *clock_sys;
    void *clock_sys_ctx;
    const tls_stream_transport_t *transport;
} wire_io_t;

/* The production io: sys = &wire_sys_libc, everything else NULL. */
extern const wire_io_t wire_io_libc;

/* ---- memory ------------------------------------------------------------------------------------------------------ */

/* The lazily opened download directory (implementation use). */
typedef struct {
    posix_fs_t fs;
    bool open;
    const posix_fs_sys_t *sys;
    void *sys_ctx;
    ports_env_t env;
    char path[ANNAS_DIR_MAX + 1u];
} wire_lazyfs_t;

typedef struct {
    alignas(64) unsigned char workspace[ANNAS_WORKSPACE_MIN]; /* app_deps_t.workspace */
    alignas(64) unsigned char scratch[APP_SCRATCH_MIN];       /* app_deps_t.scratch */
    alignas(64) unsigned char parse[WIRE_PARSE_CAP];          /* app_mcp_init parse memory */
    alignas(64) unsigned char heap[WIRE_TLS_HEAP];            /* tls_stream heap */
    char ca[WIRE_CA_MAX + 1u];                                /* the CA bundle plus its NUL */
    char dotenv[WIRE_DOTENV_MAX + 1u];                        /* the `.env` file (+1 byte to detect an oversize file) */
    char ca_path[WIRE_PATH_MAX];                              /* a candidate CA path as a C string */
    char line[APP_MCP_LINE_MAX + 1u];                         /* the line being assembled */
    unsigned char chunk[WIRE_READ_CHUNK];                     /* one read_in block */
    char out[WIRE_OUT_CAP];
    char err[WIRE_OUT_CAP];
    /* implementation state */
    core_buf_t outb;
    core_buf_t errb;
    app_dotenv_t denv;
    app_env_layer_t layer;
    posix_env_t penv;
    posix_clock_t clock;
    tls_stream_sock_t sock;
    tls_stream_wall_t wall;
    tls_stream_t tls;
    wire_lazyfs_t lazyfs;
    app_deps_t deps;
    app_mcp_t mcp;
    app_cmd_t cmd;
    app_usage_error_t usage;
} wire_mem_t;

/* ---- the entry point -------------------------------------------------------------------------------------------- */

typedef enum {
    WIRE_CLI = 1, /* the `annas-c` binary: parse argv, download / download-url / mcp / --help / --version */
    WIRE_MCP = 2  /* the `annas-c-mcp` binary: the MCP loop (no arguments) */
} wire_mode_t;

/* Runs as described at the top; returns the process exit code (the binaries return it from main). `argv[0]` is the
 * program name and is ignored. Never aborts, never blocks except in the seams. */
int wire_run(wire_mem_t *m, wire_mode_t mode, const wire_io_t *io, int argc, const char *const *argv,
             char *const *envp);

#endif
