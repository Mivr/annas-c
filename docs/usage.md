# annas-c usage (v1)

Two binaries: `annas-c` (the CLI) and `annas-c-mcp` (the MCP server). `annas-c mcp` runs the same server.
Every quoted text below was captured from the binaries built from this tree (`gate/tools/buck2 build
//bin:annas-c //bin:annas-c-mcp --show-output`), run with an empty environment and no key, except where a section
says it is taken from the spec or a header instead.

## Commands

```text
annas-c 0.1: download files from Anna's Archive with a member account key

Usage:
  annas-c download <md5> [--name <title>] [--format <ext>] [--path-index <n>] [--domain-index <n>] [--timeout <seconds>]
  annas-c book-download <md5> <filename.ext>
  annas-c download-url <md5> [--path-index <n>] [--domain-index <n>] [--timeout <seconds>]
  annas-c mcp
  annas-c --help
  annas-c --version

Environment (a .env file in the current directory is read first, then the process environment wins):
  ANNAS_SECRET_KEY     member account key (required)
  ANNAS_DOWNLOAD_PATH  absolute download directory (required by download)
  ANNAS_BASE_URL       domain of the site (default annas-archive.gl)

Exit codes: 0 ok, 2 bad usage or configuration, 3 browser check, 4 HTTP or API error,
5 network error or timeout, 6 file error, 7 parse error
```

(That is the exact stdout of `annas-c --help` and `annas-c -h`, exit 0. `annas-c-mcp --help` prints the same text.)

```text
$ annas-c --version
annas-c 0.1
```

`annas-c-mcp --version` prints the same line. Exit 0.

| Command | Meaning |
|---|---|
| `download <md5> [--name <title>] [--format <ext>] [--path-index <n>] [--domain-index <n>] [--timeout <seconds>]` | Ask the API for the fast-download URL, download the file into `ANNAS_DOWNLOAD_PATH` as `<sanitised title>.<format>` (default title: the md5; default format: `bin`). The options and the md5 may come in any order. |
| `book-download <md5> <filename.ext>` | Alias of the upstream form. Title is the part of `<filename.ext>` before the last `.`, format the part after it. No options. |
| `download-url <md5> [--path-index <n>] [--domain-index <n>] [--timeout <seconds>]` | The same API call; prints the URL and does not fetch the file. |
| `mcp` | Run the MCP server on stdio (see below). |
| `--help`, `-h`, `--version` | As above. Must be the only argument. |

Rules of the argument parser:

- Every option takes exactly one value, which is the next argument whatever it looks like. The spelling is exact:
  `--timeout=5` is an unknown option.
- `<n>` is decimal digits only (no sign, no spaces, at most 18446744073709551615). `<seconds>` is an optional `-`
  and decimal digits; the allowed range is 0 to 31536000 and is checked later (0 means the default, 3600).
- `--format` is 1 to 16 ASCII letters or digits (`tar.gz` is refused) and is lower-cased. `<md5>` is 32 hexadecimal
  characters in either case. An empty `--name` or `--format` value means absent.
- The file name is made safe: characters such as `/ \ : * ? " < > |`, control bytes and `..` become `_`, leading dots
  and spaces and trailing dots and spaces are removed, the name is cut to 200 bytes, an empty name becomes `untitled`
  (spec section 7). If the final file already exists, the download fails with `E_FS` and the file is untouched.
  On any failure no partial file remains.
- The API's `path_index` and `domain_index` are sent only when you pass the matching option.

## Output on success
These two texts are taken from `app/app.h` and the spec; they need a live API answer, so they were not captured
from a run.

```text
download:      Book downloaded successfully to: <path>
               Account: <compact JSON of account_fast_download_info>      (this line only if the API sent it)
download-url:  Download URL: <url>
               Account: <compact JSON of account_fast_download_info>      (this line only if the API sent it)
```

Output ends with a newline. Errors go to stderr and nothing is written to stdout.

## Configuration
See the table in the [README](../README.md#configuration). A `.env` file in the current directory (at most 16384
bytes and 64 entries) is read first; the process environment wins, even over a variable set to the empty string.
A network command also needs a PEM CA bundle (`SSL_CERT_FILE`, else the first of the three system paths that
can be read). The key is replaced by `REDACTED` in every output text, also when percent-encoded.

## Errors and exit codes
Errors print as one line on stderr, `annas-c: <CODE>: <message>`. Usage errors add a second line.

| Exit | Codes |
|---|---|
| 0 | success |
| 2 | `E_ARG`, `E_CONFIG` |
| 3 | `E_BROWSER_CHECK` |
| 4 | `E_HTTP`, `E_API` |
| 5 | `E_NET`, `E_TIMEOUT` |
| 6 | `E_FS` |
| 7 | `E_PARSE` |

(Exit 1 is only `annas-c: internal error: invalid dependencies`, a defect of the program's own wiring.)

### Usage errors (captured; stdout empty, exit 2)
Each is the stderr of the command shown. The second line is always `Try 'annas-c --help' for usage.`

```text
$ annas-c
annas-c: E_ARG: missing command
Try 'annas-c --help' for usage.
```

```text
$ annas-c frobnicate
annas-c: E_ARG: unknown command: frobnicate
Try 'annas-c --help' for usage.
```

```text
$ annas-c --bogus
annas-c: E_ARG: unknown option: --bogus
Try 'annas-c --help' for usage.
```

```text
$ annas-c download
annas-c: E_ARG: missing <md5>
Try 'annas-c --help' for usage.
```

```text
$ annas-c download 0123456789abcdef0123456789abcdef --name
annas-c: E_ARG: option needs a value: --name
Try 'annas-c --help' for usage.
```

```text
$ annas-c download 0123456789abcdef0123456789abcdef --name a --name b
annas-c: E_ARG: option given twice: --name
Try 'annas-c --help' for usage.
```

```text
$ annas-c download 0123456789abcdef0123456789abcdef --path-index x
annas-c: E_ARG: invalid value for --path-index: x
Try 'annas-c --help' for usage.
```

```text
$ annas-c download 0123456789abcdef0123456789abcdef --timeout abc
annas-c: E_ARG: invalid value for --timeout: abc
Try 'annas-c --help' for usage.
```

```text
$ annas-c download 0123456789abcdef0123456789abcdef --timeout=5
annas-c: E_ARG: unknown option: --timeout=5
Try 'annas-c --help' for usage.
```

```text
$ annas-c download 0123456789abcdef0123456789abcdef extra
annas-c: E_ARG: unexpected argument: extra
Try 'annas-c --help' for usage.
```

```text
$ annas-c download-url 0123456789abcdef0123456789abcdef --name x
annas-c: E_ARG: unknown option: --name
Try 'annas-c --help' for usage.
```

```text
$ annas-c book-download 0123456789abcdef0123456789abcdef
annas-c: E_ARG: missing <filename.ext>
Try 'annas-c --help' for usage.
```

```text
$ annas-c book-download 0123456789abcdef0123456789abcdef noext
annas-c: E_ARG: filename must be <title>.<ext>, got: noext
Try 'annas-c --help' for usage.
```

```text
$ annas-c mcp extra
annas-c: E_ARG: unexpected argument: extra
Try 'annas-c --help' for usage.
```

```text
$ annas-c --version extra
annas-c: E_ARG: unexpected argument: extra
Try 'annas-c --help' for usage.
```

### Configuration and argument errors (captured with no key set; stdout empty, exit 2)
No network is used for these: the configuration and the timeout are checked before any connection. The one-line
form is the whole stderr.

```text
$ annas-c download 0123456789abcdef0123456789abcdef
annas-c: E_CONFIG: ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set

$ annas-c download-url 0123456789abcdef0123456789abcdef
annas-c: E_CONFIG: ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set

$ annas-c download 0123456789abcdef0123456789abcdef --timeout -1
annas-c: E_ARG: timeout must be 0 or more seconds, got: -1

$ annas-c download 0123456789abcdef0123456789abcdef --timeout 31536001
annas-c: E_ARG: timeout must be at most 31536000 seconds, got: 31536001

$ ANNAS_BASE_URL=bad/host annas-c download-url 0123456789abcdef0123456789abcdef
annas-c: E_CONFIG: ANNAS_BASE_URL is not a valid domain, got: bad/host

$ SSL_CERT_FILE=/nonexistent annas-c download 0123456789abcdef0123456789abcdef
annas-c: E_CONFIG: cannot read the CA bundle named by SSL_CERT_FILE

$ annas-c-mcp extra
annas-c: E_ARG: annas-c-mcp takes no arguments except --help and --version
```

`download-url` needs both variables set, like `download`.

### Errors that need a key or the network (not captured)
These texts are taken from `annas/annas.h` and the spec; the agents did not run them. The exit code follows the
code, as in the table above.

| Text after `annas-c: <CODE>: ` | Meaning |
|---|---|
| `md5 must be 32 hexadecimal characters` (`E_ARG`) | bad md5 |
| `format must consist of 1 to 16 ASCII letters or digits` (`E_ARG`) | bad `--format` |
| `ANNAS_DOWNLOAD_PATH must be an absolute path, got: <path>` (`E_CONFIG`) | relative download path |
| `ANNAS_SECRET_KEY is longer than 256 bytes`, `ANNAS_DOWNLOAD_PATH is longer than 256 bytes` (`E_CONFIG`) | limits |
| `the site requires browser verification; use download-by-md5 with a hash found in a browser` (`E_BROWSER_CHECK`) | a challenge on an API or download response; nothing is done to get past it |
| `<status>: <detail>` (`E_API`) | the API refused: its `error` text (at most 200 bytes), or `API returned empty download URL` |
| `unexpected HTTP status <n>`, `too many redirects`, `redirect without a Location header`, `redirect to a non-https URL`, `invalid redirect location` (`E_HTTP`) | the file download |
| `connection failed`, `TLS error`, `network error`, `connection closed before the response was complete` (`E_NET`) | network |
| `operation timed out` (`E_TIMEOUT`) | the whole operation exceeded `--timeout` (default 3600 s) |
| `could not create a temporary file`, `could not write the file`, `could not save the file`, `file already exists: <name>` (`E_FS`) | download directory |
| `malformed HTTP response`, `API response is larger than 65536 bytes`, `download URL must be an absolute https URL`, `download URL is longer than 2048 bytes`, `account info is larger than 4096 bytes` (`E_PARSE`) | bad responses |

## MCP server
`annas-c-mcp` (or `annas-c mcp`) speaks newline-delimited JSON-RPC 2.0 on stdin and stdout. Logs would go to stderr;
stderr stays empty in a normal session. The server ends with exit 0 at end of input. It reads the same `.env`,
environment and CA bundle as the CLI, before it reads the first line.

Register it with an MCP client using a stdio server entry. The exact file and key names depend on the client; this
is the common shape:

```json
{
  "mcpServers": {
    "annas": {
      "command": "/absolute/path/to/annas-c-mcp",
      "args": [],
      "env": {
        "ANNAS_SECRET_KEY": "your member key",
        "ANNAS_DOWNLOAD_PATH": "/absolute/path/to/downloads"
      }
    }
  }
}
```

`annas-c-mcp` takes no arguments except `--help` and `--version`. The server keeps no state between lines:
`tools/call` works without `initialize`.

### Captured session
This input was piped to `annas-c-mcp` on stdin with an empty environment (no key); the output is its stdout, exit 0,
stderr empty. Each output line answers the input line with the same id.

Input:

```text
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"t","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
{"jsonrpc":"2.0","id":3,"method":"ping"}
{"jsonrpc":"2.0","id":4,"method":"nope"}
not json
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"x"}}
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"fast_download_url","arguments":{}}}
{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"fast_download_url","arguments":{"hash":"0123456789abcdef0123456789abcdef"}}}
{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"book_download","arguments":{"hash":"0123456789abcdef0123456789abcdef","timeout_seconds":-1}}}
[]
```

Output:

```text
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","capabilities":{"tools":{}},"serverInfo":{"name":"annas-c","version":"0.1"}}}
{"jsonrpc":"2.0","id":2,"result":{"tools":[{"name":"book_download","description":"Download a book by MD5 hash with a member account key and save it in the download directory.","inputSchema":{"type":"object","properties":{"hash":{"type":"string","description":"MD5 hash of the book (32 hexadecimal characters)."},"title":{"type":"string","description":"File name without extension (default: the hash)."},"format":{"type":"string","description":"File extension, 1 to 16 letters or digits (default: bin)."},"path_index":{"type":"integer","minimum":0,"description":"Collection index, 0 or larger."},"domain_index":{"type":"integer","minimum":0,"description":"Download server index, 0 or larger."},"timeout_seconds":{"type":"integer","minimum":0,"description":"Time limit in seconds (0 = server default of 3600)."}},"required":["hash"],"additionalProperties":false}},{"name":"fast_download_url","description":"Get the fast download URL and the account's download quota for an MD5 hash without downloading the file.","inputSchema":{"type":"object","properties":{"hash":{"type":"string","description":"MD5 hash of the book (32 hexadecimal characters)."},"path_index":{"type":"integer","minimum":0,"description":"Collection index, 0 or larger."},"domain_index":{"type":"integer","minimum":0,"description":"Download server index, 0 or larger."},"timeout_seconds":{"type":"integer","minimum":0,"description":"Time limit in seconds (0 = server default of 3600)."}},"required":["hash"],"additionalProperties":false}}]}}
{"jsonrpc":"2.0","id":3,"result":{}}
{"jsonrpc":"2.0","id":4,"error":{"code":-32601,"message":"Method not found"}}
{"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}}
{"jsonrpc":"2.0","id":5,"error":{"code":-32602,"message":"Invalid params: unknown tool: x"}}
{"jsonrpc":"2.0","id":6,"error":{"code":-32602,"message":"Invalid params: hash is required"}}
{"jsonrpc":"2.0","id":7,"result":{"content":[{"type":"text","text":"annas-c: E_CONFIG: ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set"}],"isError":true}}
{"jsonrpc":"2.0","id":8,"result":{"content":[{"type":"text","text":"annas-c: E_ARG: timeout must be 0 or more seconds, got: -1"}],"isError":true}}
{"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Invalid Request: batch requests are not supported"}}
```

The input has 11 lines and the output 10: line 2 is a notification (no id) and gets no reply. The other lines are
`initialize`, `tools/list`, `ping`, an unknown method, malformed JSON, an unknown tool, a missing `hash`, two
`tools/call` runs that failed on configuration or on the timeout value, and a batch.

### Methods
| Method | Result |
|---|---|
| `initialize` | `protocolVersion` is the client's when it is `2025-06-18`, `2025-03-26` or `2024-11-05`, else `2025-06-18`; `capabilities` is `{"tools":{}}`; `serverInfo` is `{"name":"annas-c","version":"0.1"}` |
| `notifications/initialized` | no reply when sent without an id (as a notification must be) |
| `ping` | `{}` |
| `tools/list` | the two tools below |
| `tools/call` | see below |
| anything else | error `-32601` `Method not found` |

### Tools
`tools/list` returns this `result` value, copied byte for byte from the captured reply with id 2 (the part after
`"result":`):

```json
{"tools":[{"name":"book_download","description":"Download a book by MD5 hash with a member account key and save it in the download directory.","inputSchema":{"type":"object","properties":{"hash":{"type":"string","description":"MD5 hash of the book (32 hexadecimal characters)."},"title":{"type":"string","description":"File name without extension (default: the hash)."},"format":{"type":"string","description":"File extension, 1 to 16 letters or digits (default: bin)."},"path_index":{"type":"integer","minimum":0,"description":"Collection index, 0 or larger."},"domain_index":{"type":"integer","minimum":0,"description":"Download server index, 0 or larger."},"timeout_seconds":{"type":"integer","minimum":0,"description":"Time limit in seconds (0 = server default of 3600)."}},"required":["hash"],"additionalProperties":false}},{"name":"fast_download_url","description":"Get the fast download URL and the account's download quota for an MD5 hash without downloading the file.","inputSchema":{"type":"object","properties":{"hash":{"type":"string","description":"MD5 hash of the book (32 hexadecimal characters)."},"path_index":{"type":"integer","minimum":0,"description":"Collection index, 0 or larger."},"domain_index":{"type":"integer","minimum":0,"description":"Download server index, 0 or larger."},"timeout_seconds":{"type":"integer","minimum":0,"description":"Time limit in seconds (0 = server default of 3600)."}},"required":["hash"],"additionalProperties":false}}]}
```

`hash` and `title` are strings; the other arguments are integers >= 0. `book_download` accepts `hash`, `title`,
`format`, `path_index`, `domain_index`, `timeout_seconds`; `fast_download_url` accepts `hash`, `path_index`,
`domain_index`, `timeout_seconds` (passing `title` or `format` to it is an unknown argument). `timeout_seconds` is an
integer of either sign; 0 or absent means 3600, and a negative value or one above 31536000 is an operation error.

A successful `tools/call` returns `{"content":[{"type":"text","text":T}],"isError":false}` where `T` is the CLI's
success output without the trailing newline (see "Output on success"). Not captured (it needs a live answer).

### Errors
- **Operation errors** (bad md5 or format, missing configuration, a negative or too large timeout, any network, API
  or file failure) are a normal result with `"isError":true` and the same line the CLI prints on stderr, without the
  newline. Captured examples: ids 7 and 8 above.
- **Protocol errors** are JSON-RPC errors with `"id"` echoed: `-32700` `Parse error` (id `null`), `-32601`
  `Method not found`, `-32602` `Invalid params: ...` (unknown tool, unknown argument, wrong type, `hash is required`),
  `-32600` `Invalid Request` (including `batch requests are not supported`, and a line longer than 1048576 bytes).
  Captured: ids 4, 5, 6, the parse error and the batch above. The other `-32602` and `-32600` message texts are in
  `app/app.h` and were not captured.
- The server never ends because of bad input. A line over 1 MiB is answered with `-32600` and the loop continues
  after its newline.
