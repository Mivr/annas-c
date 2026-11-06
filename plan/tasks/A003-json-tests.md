# A003: `json` module: public header, BUCK, tests, failing stubs (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A003`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§3.3 API body, §5 MCP), and `core/*.h` (you build on `core`).

## Allowed paths
`json/**`. Also write a **witness** (a reference implementation proving your tests are satisfiable) under
`/var/tmp/cache/A003/witness/` only; never commit it. The orchestrator moves it out of reach of implementers.

## Module
- **Parser**: strict RFC 8259 into a DOM allocated from a caller's `core_arena_t`; input `core_str_t`;
  limits: depth (caller-supplied, max 64), input size (8 MiB), string length, element count; no recursion
  (explicit stack in the arena or a fixed array). Errors carry a code and byte offset:
  `JSON_E_SYNTAX`, `JSON_E_DEPTH`, `JSON_E_LIMIT`, `JSON_E_NOMEM`, `JSON_E_UTF8`, `JSON_E_NUMBER`.
  Strings: full escape handling including `\uXXXX` and surrogate pairs (lone surrogates = error), output UTF-8;
  invalid UTF-8 in input = error. Numbers: kept as text plus checked conversion helpers to `int64_t`/`uint64_t`
  (no floats needed; a float literal is valid JSON and parses, but `json_get_i64` on it fails). Duplicate
  object keys: error. Trailing garbage after the value: error. Leading/trailing whitespace: allowed.
- **Access**: type query, object lookup by key (`core_str_t`), array index, length, iteration.
- **Writer**: into a `core_buf_t`: objects, arrays, strings (escaping `"` `\` and control chars as `\uXXXX`,
  keeping valid UTF-8 as is), integers, booleans, null; output must re-parse to the same DOM.

## Tests
Strong and exhaustive in the A001 style: every error code with its exact offset, every escape, surrogate edge
cases, every limit at N-1 / N / N+1, deep nesting at the limit and beyond, numbers at `INT64_MIN`/`INT64_MAX`/
`UINT64_MAX` and one past, the RFC 8259 examples, and writer round-trips. Include a small table-driven test
over a corpus of known-valid and known-invalid documents (write them yourself; do not download suites).

## Stubs and acceptance
As A001: stubs compile under the sealed flags and every test fails without crashing the framework; your witness
passes everything in `release` and `san`. Report the API, the tests per file, and the stub/witness summaries.
