# A065: triage the 41 surviving mutants of wire under the owner rule

Repo: `Mivr/annas-c`. Branch `task/A065`. **Implementer task**: edit `wire/src/**` only. No test, header,
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

## Scope: `wire` (`main` `801bce3`)
A059's measurement found 41 survivors in `//wire:wire`, in `src/config.c`, `src/io.c`, `src/lazyfs.c`, `src/loop.c`,
`src/run.c` and `src/sys.c` (`sys_fill`, `sys_unix_seconds`). No task has triaged them yet. Re-measure first (the
lines and ids are whatever your measurement gives). For each survivor decide, in this order:
1. **Remove** the logic (delete it, or refactor so the mutated construct is gone), keeping behaviour through `wire`'s
   public header, except for inputs the header or POSIX says cannot happen.
2. **Killable**: if a test through the public header can tell the mutant apart, do not change the code; write the input
   and the expected result in `/var/tmp/cache/A065/killable.md` for a test-author task.
3. **Kept**: only under the two exceptions, or as a truly equivalent mutant that no refactor can remove; write the entry
   in `/var/tmp/cache/A065/approvals.md`.
Do not touch tests, headers or BUCK.

## Method
1. In a scratch clone under `/var/tmp/cache/A065/` with the current gate copied over `gate/`, measure the survivors of `//wire:wire` (`c_mutants` with all its tests, `linux_release`, judged by the gate's `rigor mutants`). Save the reports.
2. For each survivor, try in order: (a) delete the logic; (b) refactor so the mutated construct no longer exists (for
   example a table instead of a range test, a single expression instead of a guarded branch). The behaviour through the
   public header must not change, except for inputs the header or POSIX says cannot happen.
3. Re-measure. Every change must keep both acceptance builds green.
4. For each survivor that remains, write an entry in `/var/tmp/cache/A065/approvals.md`: mutant id, file:line, the
   mutation, why (a) and (b) are impossible (be concrete: what would break or what UB would open), and which exception
   applies if any. The orchestrator signs approvals from this file, so it must stand on its own.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass", same count as before (no test changes)
```
Report: survivors before and after, every removal (with the header or POSIX clause that makes it safe), the
remaining ones with their approvals.md entries, and every deviation.

## Rules
Use `TMPDIR=/var/tmp/cache/A065/tmp`; never write to `/tmp`. No `git stash`, no merge of `main`, no push. Never read
`/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs. Never kill
processes you did not start; never `buck2 kill` outside your scratch clone. No sleep-poll loops (`gate/tools/job`).
2 CPUs and 8 GiB are shared: one mutants build at a time.
