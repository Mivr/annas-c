# annas-c

A command-line client and MCP (Model Context Protocol) stdio server for the Anna's Archive member download API,
written in C17.

It is a clean-room reimplementation of part of the behaviour of
[iosifache/annas-mcp](https://github.com/iosifache/annas-mcp), written from a functional spec
([docs/spec.md](docs/spec.md)) without copying its code. It is also the first product graded by
[c-rigor-gate](https://github.com/Mivr/c-rigor-gate), a gate designed to make code written by AI coding agents
trustworthy and hard to game.

Status: v1 builds and its unit and integration tests pass against fakes and an in-memory TLS peer. It has not been
run against the live site by the agents that wrote it; live checks are done by the orchestrator only, and no live
result is claimed here.

## What v1 does
v1 covers only what the member JSON API (`/dyn/api/fast_download.json`) supports (spec section 0). Two binaries:

- `annas-c download <md5>` and `annas-c book-download <md5> <filename.ext>`: call the API with your member key, then
  download the file into your download directory.
- `annas-c download-url <md5>`: the same API call, printing the download URL and the account's fast-download info
  without fetching the file.
- `annas-c mcp` and `annas-c-mcp`: an MCP server over stdio (newline-delimited JSON-RPC 2.0) with two tools,
  `book_download` and `fast_download_url`.

Full usage, flags, error texts, exit codes and tool schemas: [docs/usage.md](docs/usage.md).

## Deferred to v2
Book and article search, DOI lookup and article download (spec sections 3.1, 3.2, 3.4, 3.5 and 4) are not
implemented. Anna's Archive's HTML pages answer non-browser clients with a browser-verification redirect, and its
documented route for custom search is the published metadata dumps. The v2 design is a local index built from those
dumps. This tool does not attempt to bypass bot protection; a challenge on an API or download response is detected
and reported as `E_BROWSER_CHECK`.

## Build
Builds use Buck2 through the gate's pinned wrapper on the remote-execution farm (see [docs/dev.md](docs/dev.md)).

```sh
git submodule update --init gate
gate/tools/buck2 build //bin:annas-c //bin:annas-c-mcp --show-output
```

`--show-output` prints the path of each binary under `buck-out/`. The hash directory in the path changes with the
configuration, so use what the command prints.

## Configuration
Sources, later wins: a `.env` file in the current directory (`KEY=VALUE` lines, `#` comments, optional double
quotes), then the process environment.

| Variable | Meaning |
|---|---|
| `ANNAS_SECRET_KEY` | Member API key. Required by every network command. Never logged or printed: it is replaced by `REDACTED` in every output and error text. |
| `ANNAS_DOWNLOAD_PATH` | Absolute directory for downloads. Required by every network command. |
| `ANNAS_BASE_URL` | Mirror domain, for example `annas-archive.gl` (the default). A leading `http://` or `https://` and a trailing `/` are stripped. |
| `SSL_CERT_FILE` | Optional PEM CA bundle. Without it the binaries try `/etc/ssl/certs/ca-certificates.crt`, `/etc/pki/tls/certs/ca-bundle.crt`, then `/etc/ssl/cert.pem`. |

## c-rigor-gate grading status
Levels and status as recorded in [plan/phase1.md](plan/phase1.md). "Provisional" means merged but not yet confirmed
by the grader's hidden tests and mutation testing. Parsers (`json`, `url`, `fsname`) are the strictest tier.

| Module | Level | Status |
|---|---|---|
| `core`, `json`, `url`, `fsname` | L3 | merged (provisional) |
| `http` | L3 | listed in the plan; its merge status is not stated there |
| `ports` | L2 | listed in the plan; its merge status is not stated there |
| `annas` | L3 | listed in the plan; its merge status is not stated there |
| `tls_stream` | L2 | listed in the plan; its merge status is not stated there |
| `posix_fs`, `posix_env`, `posix_clock` | L2 | listed in the plan; its merge status is not stated there |
| `app` | L2 | listed in the plan; its merge status is not stated there |
| `bin` (the two binaries) | L1 | a `main` that calls `wire_run()`; no logic |
| `wire` | not in the plan table | composition root with its own tests; its level is not stated in the plan |

No module has a final grade: the grader's hidden tests are not yet in place.

License: MIT OR Apache-2.0.
