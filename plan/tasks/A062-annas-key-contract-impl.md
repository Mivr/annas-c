# A062: implement the A061 key contract in annas/src (and restore port-output initialisers)

Repo: `Mivr/annas-c`. Branch `task/A062`, **based on `val/A061`** (A061's validated header and tests; they merge to
`main` together with this task). **Implementer task**: edit `annas/src/**` only. Headers, BUCK and tests are protected.
Read `AGENTS.md`, `gate/AGENTS.md`, `annas/annas.h` (the new contract: KEY CLASS, NAME, DOWNLOAD PATH, REDIRECT rules and
the a-h order of `annas_config_load` step 3), `url/url.h`, `fsname/fsname.h`, `http/http.h`. Use **fake keys only**.

## Goal
1. Make every test in `annas/tests/` pass in `linux_release` and `linux_san`. Today 25 fail on `main` code (A061's list:
   `key_config/*`, `key_flow/*`, `fast_request/key_is_percent_encoded`, `leak/title_equal_to_the_key_is_redacted_in_fs_errors`).
2. Restore initialisers that A051 removed on values produced by **caller-supplied ports** (owner rule exception 2:
   a misbehaving port must not make annas read uninitialised memory). V051 named: the env value slice when `env.get`
   returns true without setting it, and the stream read count `n` when `read` returns OK without setting it. Look for
   every other local that is only initialised by a port call and initialise it at declaration.
3. Owner rule on annas's two kept survivors (`src/ctx.c:54`, `src/redact.c:69`; re-measure, lines may have moved):
   delete or refactor the logic away if possible; otherwise write an entry in `/var/tmp/cache/A062/approvals.md`
   (mutant id, file:line, mutation, why removal and refactoring are impossible, exception). Defensive code against
   things our own code or the OS never do is deleted.

## Method
Measure survivors of `//annas:annas` before and after in a scratch clone under `/var/tmp/cache/A062/` with the current
gate copied over `gate/` (`c_mutants` with all annas tests, `linux_release`, judged by the gate's `rigor mutants`).
New code must not leave new survivors: if it does, either simplify it or report the input that kills it.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass" (1669 + A061's new tests)
```
Report: the fixes per rule, the restored initialisers, survivors before and after, approvals.md entries, deviations.

## Rules
Use `TMPDIR=/var/tmp/cache/A062/tmp`; never write to `/tmp`. Build only with `gate/tools/buck2` (the gate toolchain), never
host compilers. No `git stash`, no merge of `main`, no push. Never read `/var/tmp/.config`, `/var/tmp/.claude*`,
`/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs (A061's throwaway implementation included); never look
for a real key. Never kill processes you did not start. No sleep-poll loops (`gate/tools/job`). 2 CPUs and 8 GiB shared.
