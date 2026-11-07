# A058: assertion kills for `slice_arg_bad` in http/src/request.c (crash-kill-ratio)

Repo: `Mivr/annas-c`. Branch `task/A058`. **Test-author task**: add cases to the existing `http/tests/request_test.c`
only. No new test target, no BUCK change, no `src/`, no header. Read `AGENTS.md`, `gate/AGENTS.md`, `http/http.h`, and in
the current gate checkout `/var/tmp/src/c-rigor-gate` (read-only) `docs/mutate.md` (section on `crash-kill-ratio`). Do not
edit `plan/`. Do not move the `gate` submodule.

## Why
The gate's `crash-kill-ratio` rule (T078; L3 error) flags `slice_arg_bad` in `http/src/request.c`. Its mutants are killed,
but mostly by crashes (a NULL slice reaching a read), not by an assertion on the result. A test suite that only crashes
does not prove the contract. `http.h` promises, for `http_request_write`:
- `HTTP_E_ARG` for a slice with `ptr == NULL` and `len > 0` (in `url->host`, `url->target` or `version`), with precedence
  `HTTP_E_ARG` > `HTTP_E_INVALID` > `HTTP_E_OVERFLOW`;
- a `{NULL, 0}` slice is not an argument error: it is an empty component, judged by the validity rules (`HTTP_E_INVALID`).

## Requirements
- For each of the three slices: `{NULL, 1}` and `{NULL, large}` give `HTTP_E_ARG`; `{NULL, 0}` gives `HTTP_E_INVALID`
  (not `HTTP_E_ARG`); a valid non-NULL slice with the other two valid gives `HTTP_OK` with the exact request bytes.
- Precedence: a NULL-with-length slice together with another component that is invalid still gives `HTTP_E_ARG`, and so does
  one with `out` already overflowed. Cover each slice in that position.
- Check every expected result against `http.h` before you pin it, including what `out` holds after an error. If the header
  does not promise it, say so and do not pin it.
- Every new assertion checks the return value (and bytes where promised), so the mutant dies on an assertion, not a crash.
  Tests are graded against a witness implementation: pin only what the header promises.
- Measure in a scratch clone under `/var/tmp/cache/A058/` with the current gate copied over `gate/`: the `c_mutants` target
  for `//http:http` with its tests, on `linux_release`. Report each `slice_arg_bad` mutant's verdict before and after
  (`killed-assert` vs `killed-crash`), and run the gate's `rigor mutants` on the report to show `slice_arg_bad` no longer
  draws `crash-kill-ratio`. Report any other http function that still does.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //http/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //http/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"; the passing count rises only by your new cases
```
Report the new test ids (they need a gate config follow-up), the verdicts before and after, and every deviation.

## Rules
Use `TMPDIR=/var/tmp/cache/A058/tmp`; never write to `/tmp`. Do not use `git stash`. Do not merge `main`, do not push.
Never read `/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs.
Never kill processes you did not start. No sleep-poll loops (`gate/tools/job`). 2 CPUs and 8 GiB are shared.
