# A050: http request writer: kill `slice_arg_bad` mutants by assertion, not by crash (T078 `crash-kill-ratio`)

Repo: `Mivr/annas-c`. Branch `task/A050`. **Test-author task**: `http/tests/request_test.c` only (add cases to the
existing file; no new test target, no BUCK change, no `src/`, no header). Read `AGENTS.md`, `gate/AGENTS.md`, `http/http.h`
(`http_request_write`), and in the current gate checkout `/var/tmp/src/c-rigor-gate` (read-only) `docs/mutate.md`
("crash-kill-ratio", T078). Do not edit `plan/`. Do not move the `gate` submodule.

## Why
The gate's new `crash-kill-ratio` rule (at least 4 kills, more than half of them crashes) flags
`http/src/request.c` `slice_arg_bad` (3 kills by a failed check, 4 by a crash). Its mutants let a `{NULL, len > 0}` slice
through, and today only a crash catches that. L3 needs the behaviour pinned by assertions: the header says what
`http_request_write` returns for each bad argument.

## Requirements
1. For each slice argument (`url->host`, `url->target`, `version`), add cases that pin the exact return code through
   the public header:
   - `{NULL, 1}` and `{NULL, large}` give `HTTP_E_ARG`;
   - `{NULL, 0}` gives whatever the header specifies (probably `HTTP_E_INVALID` for host and target, because empty fails
     validation; check `http.h` and pin exactly what it promises);
   - a valid slice next to a bad one still gives `HTTP_E_ARG`;
   - and `out` is untouched on every error (length and overflow flag unchanged).
2. Measure in a scratch clone under `/var/tmp/cache/A050/` with the current gate copied over `gate/`: a `c_mutants` target for
   `//http:http` with all http tests, on `linux_release`. Report the `slice_arg_bad` kill counts (assert/crash) before
   and after. The goal is that its crash share is at most 1/2. Report any other function whose counts changed.
3. Behaviour only: test what the header promises, never incidental details, because the tests are graded against a
   witness implementation.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //http/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //http/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"; the passing count rises only by your new cases
```
Report the new test ids, the before/after counts, and every deviation.
