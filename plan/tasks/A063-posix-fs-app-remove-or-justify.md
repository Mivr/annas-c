# A063: remove the logic behind "kept" survivors in posix_fs and app, or prove it cannot go

Repo: `Mivr/annas-c`. Branch `task/A063`. **Implementer task**: edit `*/src/**` of the modules below only. No test, header,
BUCK or `plan/` change. Do not move the `gate` submodule. Read `AGENTS.md`, `gate/AGENTS.md`, each module's public header,
and in the current gate checkout `/var/tmp/src/c-rigor-gate` (read-only) `docs/mutate.md`.

## Owner rule (2026-11-07)
A surviving mutant is approved only when its logic **cannot be removed**: not by deleting the code, and not by refactoring
so that the construct is no longer present at all. Defensive code against things libc, the OS or our own callers never do
is over-engineering: **delete it**. The owner's example: `errno > 0 ? -errno : -EINVAL` after a failed libc call. POSIX
guarantees errno is set on those failures, so return `-(int64_t)errno` and drop the fallback.

Two exceptions stay (keep them, and say which rows they cover):
1. A documented requirement of a third-party API (calls it requires, results of security-relevant calls).
2. An initialiser or check whose removal could cause undefined behaviour when a **caller-supplied** seam or input misbehaves
   (for example the `posix_clock/src/clock.c` sec/nsec initialisers: the seam is caller-supplied).

## Scope: `posix_fs` (A054 merged as `353227c`) and `app` (A055 merged as `696252d`)
| module | kept survivors from the triage (re-measure, lines may have moved) |
|---|---|
| `posix_fs` | `src/sys.c:11` `fail_errno` errno==0 fallback (2 mutants): **the owner's own example, delete it**; `src/sys.c:37` `len > INT64_MAX` in `ftruncate` (differs only on filesystems accepting INT64_MAX lengths; the adapter never passes such a length) |
| `app` | the 10 kept rows of A055's report (dotenv, ops scrub, mcp; read `app/src` and re-measure to find them) |

Not in scope: tests (the killable survivors of both modules get a separate test-author task), and every other module.

## Method
1. In a scratch clone under `/var/tmp/cache/A063/` with the current gate copied over `gate/`, measure the survivors of `//posix_fs:posix_fs` and `//app:app` (`c_mutants` with all its tests, `linux_release`, judged by the gate's `rigor mutants`). Save the reports.
2. For each survivor, try in order: (a) delete the logic; (b) refactor so the mutated construct no longer exists (for
   example a table instead of a range test, a single expression instead of a guarded branch). The behaviour through the
   public header must not change, except for inputs the header or POSIX says cannot happen.
3. Re-measure. Every change must keep both acceptance builds green.
4. For each survivor that remains, write an entry in `/var/tmp/cache/A063/approvals.md`: mutant id, file:line, the
   mutation, why (a) and (b) are impossible (be concrete: what would break or what UB would open), and which exception
   applies if any. The orchestrator signs approvals from this file, so it must stand on its own.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass", same count as before (no test changes)
```
Report per module: survivors before and after, every removal (with the header or POSIX clause that makes it safe), the
remaining ones with their approvals.md entries, and every deviation.

## Rules
Use `TMPDIR=/var/tmp/cache/A063/tmp`; never write to `/tmp`. No `git stash`, no merge of `main`, no push. Never read
`/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs. Never kill
processes you did not start; never `buck2 kill` outside your scratch clone. No sleep-poll loops (`gate/tools/job`).
2 CPUs and 8 GiB are shared: one mutants build at a time.
