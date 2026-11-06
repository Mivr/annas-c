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
 * (stdout text, stderr text, an MCP response line) is finalised so that every non-overlapping occurrence (left to
 * right) of the key, then of url_pct_encode(key) if that differs, is replaced by "REDACTED". That includes echoes
 * of client-controlled text: a file name made from the key, an MCP id, a method or tool name. (The file written
 * to the fs port keeps its real name; only output is scrubbed.) An empty or unset key redacts nothing.
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
 * >= ANNAS_WORKSPACE_MIN bytes, 16-byte aligned. All fields must be valid; the struct is copied by app_mcp_init
 * and used by pointer in app_cli_run (it must outlive the call). A workspace or port that annas_ctx_init refuses
 * makes the operation fail with the internal error described at app_cli_run / app_mcp_handle. */
typedef struct {
    ports_stream_t stream;
    ports_fs_t fs;
    ports_clock_t clock;
    ports_env_t env;
    void *workspace;
    size_t workspace_cap;
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
 * missing, or a workspace annas_ctx_init refuses; APP_CMD_MCP, APP_CMD_NONE or an unknown kind. The order of work
 * for the download kinds is: config (annas_config_load, its errors are reported normally), then
 * annas_ctx_init (failure = internal error), then the operation. HELP and VERSION need only cmd, out and errout
 * (deps may be NULL). The help text, byte for byte (HELP_TEXT):
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

typedef struct {
    app_deps_t deps;
    core_arena_t arena;   /* implementation use: the JSON DOM of the line being handled */
} app_mcp_t;

/* `parse_mem[0..parse_cap)` is the memory of the JSON DOM of ONE request (rewound for every line); it needs
 * about the decoded size of the request's strings plus a small constant per value, so APP_MCP_LINE_MAX plus some
 * slack always suffices. Returns false when an argument is NULL or parse_cap < APP_MCP_ARENA_MIN. The server has
 * no state between lines: tools/call works before or without `initialize`. */
bool app_mcp_init(app_mcp_t *s, const app_deps_t *deps, void *parse_mem, size_t parse_cap);

typedef enum {
    APP_MCP_NO_REPLY = 0,  /* nothing appended to `out`: a notification, or a blank line */
    APP_MCP_REPLY,         /* exactly one JSON text appended to `out`, without a trailing newline */
    APP_MCP_E_ARG,         /* NULL argument (or line.ptr NULL with len > 0); nothing appended */
    APP_MCP_E_OVERFLOW     /* `out` too small for the reply; `out` is in its overflow state (core_buf rules) */
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
 *                   annas_error_line(). A ports/ workspace annas refuses: isError true with E =
 *                   "annas-c: internal error: invalid dependencies".
 *     any other method: -32601 "Method not found".
 *  The reply is finalised by the key scrubbing described at the top of this file. */
app_mcp_result_t app_mcp_handle(app_mcp_t *s, core_str_t line, core_buf_t *out);

#endif
