# A014: `app` module (pure CLI + MCP core): header, BUCK, tests, failing stubs; annas.h comment fix

Repo: `Mivr/annas-c`. Branch `task/A014`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§0, §1, §3.3, §5, §6), and the headers of `core`, `json`, `annas`, `ports`.

## Allowed paths
`app/**`; and in `annas/annas.h` **only** the comment of `annas_fast_download_url` step (2): the implementer of
A013 reported that it says `format` is validated while the test `fast_request_test/title_and_format_are_ignored`
(and the second sentence of the same comment) say title/format are ignored by the fast call. Make the comment say
the fast call validates only the md5 and ignores title/format. No other change to `annas/**`.
Witness and scratch under `/var/tmp/cache/A014/` only.

## Module (pure: no IO; the binaries in a later task only wire adapters and call these)
- **CLI**: `app_cli_parse(argc, argv, &cmd, &err)` for: `download <md5> [--name T] [--format F]
  [--path-index N] [--domain-index N] [--timeout S]`, the alias `book-download <md5> <filename.ext>`,
  `download-url <md5> [--path-index N] [--domain-index N] [--timeout S]`, `mcp`, `--help`, `--version`.
  Exact usage/error texts and exit code 2 for bad usage. `.env` parsing as a pure function over file bytes
  (`KEY=VALUE`, `#` comments, optional double quotes, CRLF tolerated) producing an env table that layers under
  the process env (spec §1). `app_cli_run(&cmd, &deps, out, errout)` -> exit code: runs the annas operation via
  `deps` (annas ctx built from ports) and writes the exact stdout/stderr texts (spec §3.3, §6).
- **MCP**: `app_mcp_handle(&server, line, out)` for one newline-delimited JSON-RPC 2.0 message -> zero or one
  response line, per spec §5 (initialize, notifications/initialized, tools/list with the exact two tool schemas,
  tools/call, ping, error codes -32700/-32601/-32602, `isError` results with the error text, 1 MiB line cap,
  id echo of string/number/null, batch arrays rejected with -32600). Tool calls go through the same `deps`.

## Tests
With the ports fakes (scripted stream, in-memory fs, table env, manual clock): every command and flag, every
usage error with exact text, `.env` edge cases, exact stdout/stderr and exit codes for success and each
error class, the key never in any output; MCP: exact response lines for each method, malformed inputs, schema
validation of tool arguments (types, negatives, unknown fields), id echo, notification (no response), large
lines at the cap. Positive controls for every rejection.

## Stubs and acceptance
As A001. Report API, tests per file, stub/witness summaries.
