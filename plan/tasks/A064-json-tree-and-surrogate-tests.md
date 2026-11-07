# A064: json tests that kill by assertion (red-black tree functions, the last survivor)

Repo: `Mivr/annas-c`. Branch `task/A064`. **Test-author task**: add cases to existing files in `json/tests/` (or one new
test file registered in `json/BUCK`, if you must; say why). No `src/`, no header. Read `AGENTS.md`, `gate/AGENTS.md`,
`json/json.h`, and in `/var/tmp/src/c-rigor-gate` (read-only) `docs/mutate.md` (`crash-kill-ratio`, `crash-only-function`).
Do not edit `plan/`. Do not move the `gate` submodule.

## Why
After A048 and A060 (`main` `248ba80`), `//json:json` has one survivor and two kinds of weak kills:
- survivor `utf8.c` `low_escape_at` `||` -> `&&` (mutant id `18fde7d22bc329b2` at A060's measurement). Killing inputs:
  `"\uD83D\xDE00"` and `"\uD83DxuDE00"`: the header requires `JSON_E_UTF8` (check the offset the header promises).
- `crash-kill-ratio` (most kills are crashes, not assertions) on `rot1`, `rot2`, `fix_red`, `index_insert`, `link_node`, and
  `crash-only-function` on `is_red`: these are the red-black tree that indexes object members for `json_obj_get` and the
  duplicate-key check. Their mutants corrupt the tree, and today a test only notices when it crashes.

## Requirements
- Pin, through `json.h` only, observable results that a corrupted tree changes before it crashes: for objects with
  1..200 keys inserted in ascending, descending, alternating, zig-zag and seeded random orders, every key is found by
  `json_obj_get` with its value; every absent near-miss key (one byte changed, prefix, extension) is not found;
  every duplicate at every position gives the duplicate-key error at the exact offset the header promises; member
  iteration order (if the header promises one) is pinned. Make the checks dense enough that a single wrong rotation
  or recolouring changes an assertion result.
- If the header promises a complexity bound (lookups, duplicate detection), add a test that would fail under a degenerate
  tree only if it can be made deterministic (an operation count through a public counter, not wall time). Otherwise say
  it is not testable through the header.
- The `low_escape_at` inputs above and their neighbours (high surrogate then `\x`, then `xu`, then a valid low).
- Tests are graded against a witness implementation: pin only what `json.h` promises.

## Measurement
In a scratch clone under `/var/tmp/cache/A064/` with the current gate over `gate/`: `c_mutants` for `//json:json` with
all its tests on `linux_release`, judged by `rigor mutants`. Report the survivor's verdict after, and for each function
above the kill split (assert vs crash) before and after; the target is no `crash-kill-ratio` / `crash-only-function`
finding left.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //json/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //json/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"
```
Report the new test ids (they need a gate config follow-up), the measurements, and every deviation.

## Rules
Use `TMPDIR=/var/tmp/cache/A064/tmp`; never write to `/tmp`. Build only with `gate/tools/buck2`. No `git stash`, no merge of
`main`, no push. Never read `/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or
other cache dirs. Never kill processes you did not start. No sleep-poll loops (`gate/tools/job`). 2 CPUs, 8 GiB shared.
