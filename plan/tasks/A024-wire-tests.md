# A024: `wire` module and the two binaries `annas-c` (CLI) and `annas-c-mcp`: header, BUCK, tests, failing stubs; `app/BUCK` made literal (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A024`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§0, §1, §2, §5, §6), and the headers of `core`, `ports`, `app`, `tls_stream`, `posix_fs`, `posix_env`,
`posix_clock`, and `gate/docs/product-checks.md` (the BUCK guard: no assignments, no `+`).

## Allowed paths
`wire/**` (new module), `bin/**` (new: the two `c_binary` targets and their `main` files), `app/BUCK` (only to
make it literal, with the same targets, deps and srcs and no assignment or list concatenation; the A017 test author
reported that it violates the BUCK guard). Witness and scratch under `/var/tmp/cache/A024/` only.

## Design
Everything with logic lives in **`wire`** (L2, tested). The binaries are **L1**: each `main` is a handful of lines
that call `wire` and return its exit code, with no branches beyond that.
- `wire` builds the real dependencies for `app`: posix env from `envp` layered under the `.env` file
  (`app_env_layer_*`), the posix clock, posix fs on `ANNAS_DOWNLOAD_PATH` (opened only when an operation needs it),
  and the `tls_stream` over the socket transport with a wall clock (`CLOCK_REALTIME` through a seam), the CA
  bundle and the heap arena.
- **CA bundle**: `SSL_CERT_FILE` if set, otherwise the first readable of a fixed list of system paths
  (`/etc/ssl/certs/ca-certificates.crt`, `/etc/pki/tls/certs/ca-bundle.crt`, `/etc/ssl/cert.pem`). Read through a
  file seam into caller memory with a documented cap. None readable gives the documented E_CONFIG-class error text
  and exit code (define it in the header, consistent with spec §6).
- **`.env`**: read from the current directory through the same file seam, with a cap. A missing file is fine;
  an unreadable or oversize one is an error text.
- **MCP loop**: read newline-delimited lines from stdin through a byte-reader seam, call `app_mcp_handle`, write
  each response line to stdout, flush, and exit 0 at EOF. A line over the 1 MiB cap is handled as `app_mcp_handle`
  defines it, and the loop resyncs at the next newline.
- **CLI**: parse argv, run, then write stdout and stderr through a writer seam, handling short writes and EINTR,
  and use the exit code from `app`.
- All memory is static storage owned by `main` (the binaries may hold the big buffers as `static` arrays in
  `main.c`, because the binary is the composition root) or passed in. `wire` has no globals.

## Tests (for `wire`, with fake seams: files, stdin reader, stdout writer, transport via the `tls_stream` test kit if useful)
Env layering order (process env wins over `.env`); the CA path search order, `SSL_CERT_FILE` precedence, every
failure text; `.env` caps; MCP loop framing (partial reads, many lines per read, a CRLF tolerated or not as
`app` defines, a trailing line without a newline at EOF, an oversize line then resync, stdout short writes);
the CLI path end to end with a scripted transport (the `ports` stream fake or the `tls_stream` kit) for one
`download-url` success and one error; the key never in any output. Positive controls for every rejection.
For `bin/`: one smoke `c_test` per binary only if it can run hermetically (`--version`, `--help`); otherwise
explain why not.

## Stubs and acceptance
As A001. The witness passes everything in `release` and `san` with the committed BUCK file lists. `app`'s tests
still pass unchanged with the literal `app/BUCK`. Report: the API, the tests per file, the stub and witness
summaries, and the error texts and exit codes you defined.
