# A032: annas tests pin timeouts, port tables and request arguments (R004 T1)

Repo: `Mivr/annas-c`. Branch `task/A032`. **Test-author task**: `annas/tests/**` only (the kit headers included), no `src/`,
no public header, no BUCK change unless a new test file needs registering (then only `annas/BUCK`, test entries only).
A029 runs in parallel on `annas/src/msg.c` and other `src/` files; nothing here overlaps.

## Why
R004 (`plan/audits/R004-test-adequacy.md`, "G1 probes" and "T1") deleted spec-required checks in `annas/src/` and the
tests still passed: P2a (`annas_ctx_init` accepts a port table with a NULL function), P3 (the deadline wraps instead of
saturating), P4 (no clock check before the fs commit). Several surviving mutants in the annas L3 run have the same cause.

## Requirements
Exactly the T1 list in the R004 plan: the 11 NULL-function port cases, the saturating-clock success cases, a test clock that
advances by `step` ms on every `now_ms` call and the four deadline placements, the `{NULL, 3}` title/format/md5 argument
cases, and the 403 `ddos-guard` placement cases (in the 2nd read; crossing offset 1024; at 1030). Every error case uses
`kit_check_err` with exact code, message and `http_status`. Every new test must pass on today's `annas/src`.

## Acceptance (the orchestrator re-runs it)
1. `gate/tools/buck2 build //annas/... --target-platforms toolchains//:linux_{release,san} --show-full-output`: every test
   passes; report the new test ids.
2. For each probe P2a, P3, P4 (scripts in `/var/tmp/cache/A032/r004-scripts/`, read them to see how they apply an edit to
   a scratch copy): apply it in a **scratch clone in your cache**, rebuild `//annas/...`, and show that at least one of your
   new tests fails, naming it. Never edit `src/` in your worktree.
