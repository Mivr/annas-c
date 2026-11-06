/* app/app.h: the pure user-facing surface of annas-c: the command line and the MCP (JSON-RPC 2.0) server.
 * No IO happens here: every effect arrives through app_deps_t (the ports), so the binaries of a later task only
 * wire adapters (TLS stream, POSIX fs/env/clock, stdin/stdout, the `.env` file) and call these functions.
 * No globals, no malloc; memory is the caller's. Spec sections 1, 3.3, 5, 6.
 *
 *   app_cli_parse       argv -> app_cmd_t, or a usage error (exact text, exit code 2)
 *   app_dotenv_parse    `.env` file bytes -> entries; app_env_layer_*: the `.env` entries UNDER the process env
 *   app_cli_run         runs a parsed command through annas, writes the exact stdout / stderr texts, returns the
 *                       exit code
 *   app_mcp_handle      one newline-delimited JSON-RPC message -> zero or one response line
 *
 * ---- Common rules ---------------------------------------------------------------------------------------------
 * Key secrecy.  The member key is ANNAS_SECRET_KEY as read from deps->env. Every byte this module outputs
 * (stdout text, stderr text, an MCP response line) is finalised by TWO SEQUENTIAL PASSES over the complete text:
 *   pass 1: every non-overlapping occurrence (scanning left to right) of the key is replaced by "REDACTED";
 *   pass 2: only if url_pct_encode(key) differs from the key, every non-overlapping occurrence (left to right) of
 *           url_pct_encode(key) in the OUTPUT OF PASS 1 is replaced by "REDACTED".
 * (Reason: it is the rule annas already applies to its messages, annas.h "finalised by the same three steps", so
 * the product has one scrubbing rule. Consequences, pinned by tests: pass 2 does not see what pass 1 replaced; an
 * occurrence of the encoded key that overlaps a raw occurrence is destroyed by pass 1 and survives only as the
 * bytes pass 1 left of it; and a "REDACTED" written by pass 1 may combine with its neighbours into the encoded
 * key, which pass 2 then replaces.)
 * That includes echoes of client-controlled text: a file name made from the key, an MCP id, a method or tool
 * name. (The file written to the fs port keeps its real name; only output is scrubbed.) An empty or unset key
 * redacts nothing. The scrubbing is applied to the whole text BEFORE it is appended, see "Memory and output".
 * Memory and output.  This module owns no memory. It uses ONLY: deps->scratch (below), the MCP parse arena
 * (the DOM of the request, then the reply under construction), and the `core_buf_t` objects it appends to. It
 * reads and writes a `core_buf_t` only through the functions of core/buf.h: the unused capacity behind the content
 * (after its NUL) is never read, written or borrowed, so every result is independent of how large the caller's
 * `out` / `errout` happen to be (an `out` of exactly content length + 1 bytes is enough).
 * Output is ALL-OR-NOTHING. Each text (the whole stdout text, the whole stderr text, the whole MCP reply) is
 * finalised (scrubbed) completely in memory first and then appended with ONE core_buf_put: when the finalised
 * text does not fit, that buffer's content is unchanged (byte for byte, including what was there before the call)
 * and its overflow flag is set; the exit code and the port interactions are not changed by it. A buffer that is
 * already in its overflow state stays unchanged. The scrubbing itself is always applied to the complete text; it
 * can lengthen it (a key shorter than "REDACTED"), and the fit is judged on the final bytes.
 * Keys longer than ANNAS_KEY_MAX bytes are not accepted by annas_config_load; for such a key only pass 1 is applied.
 * Operations.  Both the CLI and the MCP tools call annas (annas_config_load with the env of the deps, then
 * annas_ctx_init over deps->workspace, then annas_download / annas_fast_download_url). Error texts, exit codes
 * and every port interaction are exactly annas's; this module adds only the framing described below.
 * Error line.  annas_error_line(): "annas-c: <CODE>: <message>"; the exit code is annas_exit_code(code).
 */
#ifndef APP_APP_H
#define APP_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "annas/annas.h"
#include "core/arena.h"
#include "core/buf.h"
#include "core/str.h"
#include "ports/ports.h"

#define APP_VERSION ANNAS_VERSION

/* ---- dependencies ---------------------------------------------------------------------------------------------- */

/* `env` is the LAYERED environment (see app_env_layer_*); `workspace` is annas's scratch memory:
 * >= ANNAS_WORKSPACE_MIN bytes, 16-byte aligned. `scratch` is THIS module's own scratch memory (the job's config,
 * context, error and result structs, and the texts and the reply under construction): >= APP_SCRATCH_MIN bytes,
 * 16-byte aligned, and it must not overlap `workspace`, the output buffers or the MCP parse arena. Its contents on
 * return are unspecified. All fields must be valid; the struct is copied by app_mcp_init and used by pointer in
 * app_cli_run (it must outlive the call). A workspace or port that annas_ctx_init refuses makes the operation fail
 * with the internal error described at app_cli_run / app_mcp_handle. A scratch region that is NULL, shorter than
 * APP_SCRATCH_MIN or not 16-byte aligned is refused by app_cli_run (internal error) and by app_mcp_init (false). */
#define APP_SCRATCH_MIN ((size_t)131072)  /* the job (about 9 KB), one outcome text (8 KB) and one finalised text (64 KB) */

typedef struct {
    ports_stream_t stream;
    ports_fs_t fs;
    ports_clock_t clock;
    ports_env_t env;
    void *workspace;
    size_t workspace_cap;
    void *scratch;
    size_t scratch_cap;
} app_deps_t;

/* ---- command line ---------------------------------------------------------------------------------------------- */

typedef enum {
    APP_CMD_NONE = 0,
    APP_CMD_DOWNLOAD,      /* download <md5> ...  and  book-download <md5> <filename.ext> */
    APP_CMD_DOWNLOAD_URL,  /* download-url <md5> ... */
    APP_CMD_MCP,           /* mcp (the loop around app_mcp_handle belongs to the binary) */
    APP_CMD_HELP,          /* --help | -h */
    APP_CMD_VERSION        /* --version */
} app_cmd_kind_t;

/* Slices point into the caller's argv strings (argv must outlive the command); absent = len 0. */
typedef struct {
    app_cmd_kind_t kind;
    core_str_t md5;         /* as typed (not validated, not lower-cased: annas does that) */
    core_str_t title;       /* --name; for book-download the part of <filename.ext> before the LAST '.' */
    core_str_t format;      /* --format; for book-download the part after the last '.' */
    bool has_path_index;
    uint64_t path_index;
    bool has_domain_index;
    uint64_t domain_index;
    int64_t timeout_seconds; /* --timeout; ANNAS_DEFAULT_TIMEOUT_SECONDS (3600) when absent */
} app_cmd_t;

#define APP_USAGE_MAX ((size_t)512)

/* The complete stderr text of a usage error, NUL-terminated, exactly
 *   "annas-c: E_ARG: <message>\nTry 'annas-c --help' for usage.\n"
 * with len = strlen(text). The exit code of a usage error is 2. */
typedef struct {
    size_t len;
    char text[APP_USAGE_MAX];
} app_usage_error_t;

/* Grammar (argv[0] is the program name and is ignored; argc >= 1; an argument is an OPTION when it starts with
 * '-' and has more than one byte, otherwise a positional; the lone "-" is a positional):
 *   annas-c download <md5> [--name <title>] [--format <ext>] [--path-index <n>] [--domain-index <n>]
 *                          [--timeout <seconds>]            options and the md5 in any order
 *   annas-c book-download <md5> <filename.ext>              no options
 *   annas-c download-url <md5> [--path-index <n>] [--domain-index <n>] [--timeout <seconds>]
 *   annas-c mcp                                             no arguments
 *   annas-c --help | -h        annas-c --version            argv[1] only, no further arguments
 * Every option takes exactly one value, which is the NEXT argument whatever it looks like ("--name --x" is
 * title "--x"). Spelling is exact: "--name=x" is an unknown option. An empty --name / --format value means
 * absent. md5, title and format are NOT validated here (annas does, at run time: exit 2 as well).
 *   <n>: decimal digits only, 1 or more, value <= UINT64_MAX (no sign, no spaces).
 *   <seconds>: optional '-' then decimal digits, value within int64; the range (0..31536000) is annas's check.
 *   book-download <filename.ext>: split at the LAST '.', which must have at least one byte before and after it.
 * On success returns true, *cmd is fully written. On failure returns false, *err is written, *cmd is unchanged.
 * Arguments are examined left to right and the FIRST problem is reported, except that a missing positional is
 * reported after the last argument has been examined. <arg> below is the argument cut to its first 100 bytes with
 * every byte < 0x20 and 0x7f replaced by '?'. The <message>s:
 *   argc < 2                                   missing command
 *   argv[1] positional, not a command          unknown command: <arg>
 *   an option the command does not have        unknown option: <arg>      (also for argv[1] starting with '-'
 *                                                                          that is not --help, -h, --version)
 *   a positional beyond those the command has  unexpected argument: <arg>
 *   option as the last argument                option needs a value: <option>
 *   same option twice                          option given twice: <option>
 *   bad <n> / <seconds>                        invalid value for <option>: <arg>
 *   book-download name without title or ext    filename must be <title>.<ext>, got: <arg>
 *   no md5                                     missing <md5>
 *   book-download with one positional          missing <filename.ext>
 * NULL argument or argc < 1: returns false with err (if non-NULL) set to the "missing command" error. */
bool app_cli_parse(int argc, const char *const *argv, app_cmd_t *cmd, app_usage_error_t *err);

/* ---- `.env` ---------------------------------------------------------------------------------------------------- */

#define APP_DOTENV_MAX_ENTRIES 64

typedef struct {
    core_str_t name;
    core_str_t value;
} app_dotenv_entry_t;

/* Treat as private; slices point into the parsed file bytes, which must outlive this object. */
typedef struct {
    app_dotenv_entry_t entries[APP_DOTENV_MAX_ENTRIES];
    size_t count;
} app_dotenv_t;

/* Parses the bytes of a `.env` file. Lines end at '\n'; a '\r' at the end of a line (before the '\n', or at the very end of
 * the file) is dropped (CRLF); the last line needs no terminator. For each line: strip leading and trailing ASCII whitespace (space, \t, \v, \f); an empty line or
 * one whose first byte is '#' is skipped. Otherwise split at the FIRST '='; a line without '=' is skipped.
 * The name (trimmed of whitespace) must be 1 or more bytes of [A-Za-z0-9_], else the line is skipped. The value
 * is the rest, trimmed; if it has at least 2 bytes and starts and ends with '"', the value is what is between
 * those quotes, verbatim (no escapes, '#' and spaces kept, trimmed no further). Anything else is verbatim: no
 * inline comments, no single-quote handling, a lone leading '"' stays. `KEY=` is a set, empty value. There is no
 * `export` prefix ("export K=v" has the invalid name "export K" and is skipped). A later line with the same
 * name replaces an earlier one (see app_env_layer_port).
 * Returns true and fills *out (count = accepted lines). Returns false when `out` is NULL, `file.ptr` is NULL with
 * len > 0, or more than APP_DOTENV_MAX_ENTRIES lines would be accepted (then out->count is 0). */
bool app_dotenv_parse(core_str_t file, app_dotenv_t *out);

/* The `.env` entries layered UNDER the process environment (spec section 1: later wins): get(name) asks the
 * process env first and returns its answer whenever the name is set there (even to an empty value); only if it is
 * not set there, the LAST `.env` entry of that name answers; else not found. get follows the ports_env_t rules
 * (exact case-sensitive names, an empty name is never found, not found sets *value to {NULL,0}). `file` may be
 * NULL (no `.env`). Both objects must outlive the port. */
typedef struct {
    const app_dotenv_t *file;
    ports_env_t process;
} app_env_layer_t;

/* Returns false (object unusable, its port finds nothing) when `l` is NULL or process.ops/process.ops->get is NULL. */
bool app_env_layer_init(app_env_layer_t *l, const app_dotenv_t *file, ports_env_t process);
ports_env_t app_env_layer_port(app_env_layer_t *l);

/* ---- running a command ----------------------------------------------------------------------------------------- */

/* Runs `cmd` and appends the texts to `out` (stdout) and `errout` (stderr); returns the process exit code. The
 * texts end with a newline. The buffers are not cleared first.
 *   APP_CMD_HELP        stdout = the help text below, exit 0
 *   APP_CMD_VERSION     stdout = "annas-c 0.1\n", exit 0
 *   APP_CMD_DOWNLOAD    annas_config_load(need_download = true), annas_download; success: stdout =
 *                       annas_format_downloaded() + "\n", exit 0
 *   APP_CMD_DOWNLOAD_URL  annas_config_load(need_download = false), annas_fast_download_url; success: stdout =
 *                       annas_format_download_url() + "\n", exit 0 (title and format are ignored)
 *   failure of either    stdout empty, stderr = annas_error_line() + "\n", exit annas_exit_code(code). The
 *                       config is loaded from deps->env with cmd->timeout_seconds; request fields come from cmd.
 * On success stderr is empty. No port is touched before annas has validated config and request.
 * Internal errors (return 1, stdout empty, stderr exactly "annas-c: internal error: invalid dependencies\n"):
 * out or errout NULL (then nothing is written at all); for DOWNLOAD / DOWNLOAD_URL: cmd or deps NULL, a port table
 * missing (stream, fs or clock ops NULL, or env.ops NULL or env.ops->get NULL), a scratch region that is NULL,
 * shorter than APP_SCRATCH_MIN or not 16-byte aligned, or a workspace annas_ctx_init refuses; APP_CMD_MCP,
 * APP_CMD_NONE or an unknown kind. The order of work for the download kinds is: the env port and the scratch
 * region (a defect of either is the internal error, even when the configuration is also wrong: without them no
 * error text can be built), then config (annas_config_load, its errors are reported normally, also when the
 * workspace is too small), then annas_ctx_init (failure = internal error), then the operation. HELP and VERSION
 * need only cmd, out and errout (deps may be NULL). Every text is appended as described at "Memory and output":
 * stdout text and stderr text each in ONE append, scrubbed first. The help text, byte for byte (HELP_TEXT):
 *
 *   annas-c 0.1: download files from Anna's Archive with a member account key\n
 *   \n
 *   Usage:\n
 *     annas-c download <md5> [--name <title>] [--format <ext>] [--path-index <n>] [--domain-index <n>] [--timeout <seconds>]\n
 *     annas-c book-download <md5> <filename.ext>\n
 *     annas-c download-url <md5> [--path-index <n>] [--domain-index <n>] [--timeout <seconds>]\n
 *     annas-c mcp\n
 *     annas-c --help\n
 *     annas-c --version\n
 *   \n
 *   Environment (a .env file in the current directory is read first, then the process environment wins):\n
 *     ANNAS_SECRET_KEY     member account key (required)\n
 *     ANNAS_DOWNLOAD_PATH  absolute download directory (required by download)\n
 *     ANNAS_BASE_URL       domain of the site (default annas-archive.gl)\n
 *   \n
 *   Exit codes: 0 ok, 2 bad usage or configuration, 3 browser check, 4 HTTP or API error,\n
 *   5 network error or timeout, 6 file error, 7 parse error\n
 */
int app_cli_run(const app_cmd_t *cmd, const app_deps_t *deps, core_buf_t *out, core_buf_t *errout);

/* ---- MCP server (spec section 5) -------------------------------------------------------------------------------- */

#define APP_MCP_LINE_MAX ((size_t)1048576)  /* longest accepted line, in bytes, without the newline */
#define APP_MCP_ARENA_MIN ((size_t)65536)   /* smallest parse memory accepted by app_mcp_init */
#define APP_MCP_CALL_MEM ((size_t)16384)    /* see app_mcp_init: arena memory a tools/call needs after the DOM */
#define APP_MCP_REPLY_MAX ((size_t)65536)   /* longest scrubbed reply */

typedef struct {
    app_deps_t deps;
    core_arena_t arena;   /* implementation use: the DOM of the line being handled, then its reply */
} app_mcp_t;

/* `parse_mem[0..parse_cap)` is the memory of ONE request (rewound for every line): first its JSON DOM, which needs
 * about the decoded size of the request's strings plus a small constant per value (so APP_MCP_LINE_MAX plus some
 * slack always suffices for it), then, in ALL of what the DOM left (capacity - core_arena_used after the parse),
 * the reply under construction before its scrubbing. Memory shortfalls, in the order they are met:
 *   - the DOM does not fit: the -32603 reply described at rule 3 (this is how it always was);
 *   - a tools/call arrives and the DOM left less than APP_MCP_CALL_MEM bytes plus 6 times the decoded length of the
 *     id (a number: the length of its text; null: 4): NO tool runs and no port is touched; the reply is -32603
 *     "Internal error: request does not fit in memory", id null. With that much left, the reply of a started call
 *     fits (for keys of 8 bytes or more);
 *   - the unscrubbed reply does not fit what the DOM left, or the scrubbed reply is longer than APP_MCP_REPLY_MAX
 *     bytes (scrubbing happens in deps->scratch): the same -32603 reply replaces it (a tool that has run stays run);
 *   - that -32603 reply itself (107 bytes) does not fit what the DOM left: nothing is appended and the result is
 *     APP_MCP_E_OVERFLOW with `out` in its overflow state, exactly as for an `out` that is too small.
 * Returns false when an argument is NULL, parse_cap < APP_MCP_ARENA_MIN, or deps->scratch is NULL, shorter than
 * APP_SCRATCH_MIN or not 16-byte aligned. The ports and the workspace are not validated here (see tools/call). The
 * server has no state between lines: tools/call works before or without `initialize`. */
bool app_mcp_init(app_mcp_t *s, const app_deps_t *deps, void *parse_mem, size_t parse_cap);

typedef enum {
    APP_MCP_NO_REPLY = 0,  /* nothing appended to `out`: a notification, or a blank line */
    APP_MCP_REPLY,         /* exactly one JSON text appended to `out`, without a trailing newline */
    APP_MCP_E_ARG,         /* NULL argument (or line.ptr NULL with len > 0); nothing appended */
    APP_MCP_E_OVERFLOW     /* `out` too small for the reply: its content is unchanged (all-or-nothing, see
                              "Memory and output"), and it is in its overflow state (core_buf rules) */
} app_mcp_result_t;

/* Handles ONE message: `line` is one line WITHOUT its newline. Never crashes, never blocks, never reads outside
 * `line`, never touches a port except through a successful tools/call, whatever the bytes are. All JSON below is
 * compact (json_write / json_w_* escaping: a newline inside a string is \u000a), members in the order shown.
 *
 *  1. line.len > APP_MCP_LINE_MAX: REPLY the error (-32600, id null, message
 *     "Invalid Request: line longer than 1048576 bytes"). Exactly APP_MCP_LINE_MAX bytes is handled normally.
 *  2. A line of only JSON whitespace (space, \t, \r, \n) or no bytes: NO_REPLY.
 *  3. json_parse with json_limits_default() except max_input = APP_MCP_LINE_MAX (so nesting deeper than 64 is
 *     a parse error). A parse failure: -32700 "Parse error", id null; JSON_E_NOMEM: -32603 "Internal error: request
 *     does not fit in memory", id null. (Embedded NULs, invalid UTF-8, repeated object keys, trailing bytes after
 *     the value are parse errors.)
 *  4. Root is an array (a batch, empty or not): -32600 "Invalid Request: batch requests are not supported", id
 *     null. Any other non-object root: -32600 "Invalid Request", id null.
 *  5. "id": if present it must be a string, a number or null, else -32600 "Invalid Request: id must be a string,
 *     a number or null" with id null. The id is echoed in every reply as its compact JSON (number text verbatim,
 *     strings re-escaped). Absent id = a notification.
 *  6. "jsonrpc" must be the string "2.0" and "method" a string, else -32600 "Invalid Request: jsonrpc must be
 *     \"2.0\"" / "Invalid Request: method must be a string" (checked in that order), echoing the id when it
 *     is valid and absent ids as null: such a reply is sent even for a message without id.
 *  7. A well-formed message without id is a notification: it is NOT executed (no tool runs, no port is touched)
 *     and gets NO_REPLY, whatever its method or params.
 *  8. With an id, by method (error reply shape:
 *       {"jsonrpc":"2.0","id":<id>,"error":{"code":<n>,"message":"<text>"}}
 *     success reply shape: {"jsonrpc":"2.0","id":<id>,"result":<result>}):
 *     "initialize"  params must be an object ("Invalid params: params must be an object") whose protocolVersion is
 *                   a string ("Invalid params: protocolVersion must be a string"), else -32602. Result
 *                   {"protocolVersion":V,"capabilities":{"tools":{}},"serverInfo":{"name":"annas-c","version":"0.1"}}
 *                   with V = the client's version when it is one of "2025-06-18", "2025-03-26", "2024-11-05",
 *                   otherwise "2025-06-18".
 *     "notifications/initialized"  result {}.   "ping"  result {} (params ignored).
 *     "tools/list"  result {"tools":[BOOK_DOWNLOAD,FAST_DOWNLOAD_URL]} (params ignored), where, byte for byte:
 *       BOOK_DOWNLOAD = {"name":"book_download","description":"Download a book by MD5 hash with a member account
 *         key and save it in the download directory.","inputSchema":{"type":"object","properties":{
 *         "hash":{"type":"string","description":"MD5 hash of the book (32 hexadecimal characters)."},
 *         "title":{"type":"string","description":"File name without extension (default: the hash)."},
 *         "format":{"type":"string","description":"File extension, 1 to 16 letters or digits (default: bin)."},
 *         PROPS},"required":["hash"],"additionalProperties":false}}
 *       FAST_DOWNLOAD_URL = {"name":"fast_download_url","description":"Get the fast download URL and the account's
 *         download quota for an MD5 hash without downloading the file.","inputSchema":{"type":"object",
 *         "properties":{"hash":{"type":"string","description":"MD5 hash of the book (32 hexadecimal characters)."},
 *         PROPS},"required":["hash"],"additionalProperties":false}}
 *       PROPS = "path_index":{"type":"integer","minimum":0,"description":"Collection index, 0 or larger."},
 *         "domain_index":{"type":"integer","minimum":0,"description":"Download server index, 0 or larger."},
 *         "timeout_seconds":{"type":"integer","minimum":0,"description":"Time limit in seconds (0 = server default
 *         of 3600)."}
 *       (no whitespace outside the strings; a description is one string on one line.)
 *     "tools/call"  params must be an object ("Invalid params: params must be an object"); "name" a string
 *                   ("Invalid params: name must be a string"); the name one of the two tools ("Invalid params:
 *                   unknown tool: <name>", <name> cut to 100 bytes, bytes < 0x20 and 0x7f as '?'); "arguments"
 *                   absent (= {}) or an object ("Invalid params: arguments must be an object"). Then the
 *                   arguments members are visited in source order, the FIRST problem is reported, all -32602:
 *                     unknown member (for fast_download_url that includes title and format)
 *                                                       "Invalid params: unknown argument: <name>" (cut as above)
 *                     hash, title, format not strings    "Invalid params: <name> must be a string"
 *                     path_index, domain_index not an integer literal >= 0 (floats such as 1.0 or 1e2, negatives
 *                       and values above UINT64_MAX are refused; so are null, strings, booleans)
 *                                                       "Invalid params: <name> must be an integer >= 0"
 *                     timeout_seconds not an integer literal within int64 (any sign)
 *                                                       "Invalid params: timeout_seconds must be an integer"
 *                   after the last member, a missing hash: "Invalid params: hash is required".
 *                   Everything else is the operation's business (annas): a bad md5 or format, a negative or too
 *                   large timeout_seconds, missing configuration, and every network / API / fs failure give a
 *                   NORMAL result with isError true. The config is loaded from deps.env with timeout_seconds
 *                   (0 or absent = default 3600), need_download = true for book_download, false for
 *                   fast_download_url; empty title / format mean absent, as on the command line.
 *                   Result: {"content":[{"type":"text","text":T}],"isError":false}  with T = annas_format_downloaded
 *                   (book_download) or annas_format_download_url (fast_download_url) and no trailing newline; on
 *                   an operation error {"content":[{"type":"text","text":E}],"isError":true} with E =
 *                   annas_error_line(). A ports/ workspace annas refuses, or an env port that is missing
 *                   (env.ops NULL or env.ops->get NULL): isError true with E =
 *                   "annas-c: internal error: invalid dependencies" (no port is touched; with no usable env there
 *                   is no key, so nothing is scrubbed). The other methods do not need the ports.
 *                   UTF-8: T and E, and the <name> echoed in "unknown tool" / "unknown argument" (after its cut to
 *                   100 bytes), are made valid UTF-8 BEFORE the JSON escaping: scanning left to right, at each
 *                   byte, if a well-formed UTF-8 sequence (RFC 3629: no overlong forms, no surrogates U+D800..
 *                   U+DFFF, nothing above U+10FFFF) starts there it is kept whole, otherwise that ONE byte becomes
 *                   '?'. So a sequence cut in the middle by a length limit, a stray continuation byte, 0xC0, 0xC1,
 *                   0xF5..0xFF, and every byte of an overlong or surrogate encoding each give one '?'. (The CLI
 *                   writes such bytes verbatim: it is bytes, not JSON.)
 *     any other method: -32601 "Method not found".
 *  The reply is finalised by the key scrubbing described at the top of this file. */
app_mcp_result_t app_mcp_handle(app_mcp_t *s, core_str_t line, core_buf_t *out);

#endif
