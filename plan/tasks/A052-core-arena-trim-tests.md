# A052: core tests for the two killable survivors A049 found (arena exact fit, trim of an empty slice)

Repo: `Mivr/annas-c`. Branch `task/A052`. **Test-author task**: add cases to the existing `core/tests/*.c` files only. No
new test target, no BUCK change, no `src/`, no header. Read `AGENTS.md`, `gate/AGENTS.md`, `core/core.h` (or the core
headers that declare `core_arena_alloc` and `core_str_trim`), and in the current gate checkout `/var/tmp/src/c-rigor-gate`
(read-only) `docs/mutate.md`. Do not edit `plan/`. Do not move the `gate` submodule.

## Why
A049 left two core survivors that a test can kill (see `plan/tasks/A049-core-url-fsname-redundant-code.md` for context):
1. `core/src/arena.c:37` `core_arena_alloc`, `size > remaining - pad` mutated to `>=`. Input: an arena of capacity N, then
   an allocation of exactly the remaining bytes (`N - pad` with the alignment used). It must succeed and return a
   pointer inside the buffer. The mutant returns NULL.
2. `core/src/str.c:62` `core_str_trim`, `end > start` mutated to `>=`. It differs only for an empty input, where the
   mutant reads `s.ptr[-1]`. Input: `core_str_trim((core_str_t){NULL, 0})` and an empty slice of a real buffer must give
   an empty result, as the header promises.

## Requirements
- Pin exactly what the header promises, never incidental details, because the tests are graded against a witness
  implementation: return values, the returned pointer's range, and lengths.
- Cover nearby cases at the same bound: allocation of remaining + 1 fails without changing state; exact fit after a
  previous allocation with padding; trim of all-whitespace, of a single space, and of one non-space byte.
- Measure in a scratch clone under `/var/tmp/cache/A052/` with the current gate copied over `gate/`: a `c_mutants` target for
  `//core:core` with core's own tests, on `linux_release`. Report whether both survivors are killed, and by assertion
  (`killed-assert`), not only by a crash.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //core/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //core/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"; the passing count rises only by your new cases
```
Report the new test ids, the two survivors' verdicts after, and every deviation.
