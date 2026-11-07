# A070: make the json `survivor_keys` tree tests cheap enough for san under load (same kill power)

Repo: `Mivr/annas-c`. Branch `task/A070`. **Test task.** Read `AGENTS.md`, `gate/docs/mutate.md`, `json/json.h`,
`json/tests/survivor_keys_test.c` (A064 added the tree tests).

## Why
`survivor_keys/tree_ascending_and_descending`, `tree_alternating_and_zigzag` and `tree_seeded_random` each take about
2.7 s of CPU under `linux_san` (`tree_sizes` builds and probes a document for **every** n in 1..`TREE_MAX` = 200, for two
insertion patterns each). The gate's per-test wall limit is 10 s, so on a loaded RE farm all three time out (measured on
`6e31d86`: a fresh san run fails 3 of 9 in this file; earlier passes were cached results). Tests must be fast (ms each).

## Build
1. Keep the three test ids and what each one checks (the same insertion patterns, `check_tree_object` and
   `check_tree_probes` at each size). Reduce the sizes walked, so each test is <= 0.3 s CPU under san: for example every
   n in 1..40, then every power of two and its neighbours up to `TREE_MAX` (63, 64, 65, 127, 128, 129, 199, 200), plus
   whatever sizes the index's rebalancing/growth boundaries need (read `json/src/` to find them; name them in a
   comment). Do not change `json/src/**`, `json/json.h` or any other test.
2. **Kill power must not drop.** Measure the json module's mutants with the gate's `c_mutants`/`rigor mutants` on
   `linux_release` (a scratch clone under `/var/tmp/cache/A070/`) **before and after**: the set of mutants that the
   suite kills must be identical (list the counts; if any mutant changes from killed to survived, add the size that
   kills it back).
3. Report the CPU time per test before and after (host-timed run of the built san test binary is fine; never compile on
   the host).

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //json/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //json/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass", on a fresh (uncached) run of survivor_keys_test
```

## Rules
Edit only `json/tests/survivor_keys_test.c`. Use `TMPDIR=/var/tmp/cache/A070/tmp`; never write to `/tmp` (the harness
scratchpad under `/tmp` included). Build only with `gate/tools/buck2`, never host compilers. No `git stash`, no merge
of `main`, no push. Never read `/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees
or other cache dirs. Fake keys only. Never kill processes you did not start; never `buck2 kill` outside your scratch.
No sleep-poll loops (`gate/tools/job`). 2 CPUs and 8 GiB shared.
