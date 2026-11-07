# A071: tests kill the 0ba3af8 grade's mutant errors (annas, fsname, http, json)

Repo: `Mivr/annas-c`. Branch `task/A071`. **Test-author task** (role `test-author`): only `annas/tests/**`,
`fsname/tests/**`, `http/tests/**`, `json/tests/**`, plus those modules' `BUCK` files for test entries only. No `src/`,
no public header, no other BUCK change. Read `AGENTS.md`, `gate/AGENTS.md` ("Working efficiently": `gate/tools/job`)
and, in the **current gate** checkout `/var/tmp/src/c-rigor-gate` (read-only, newer than the pinned `gate/`),
`docs/mutate.md` and `docs/witness.md` ("test-author"). Do not edit `plan/`. Do not move the `gate` submodule.
Precedents: `plan/tasks/A044`-`A046` and `A070` (json tree tests).

## Why
The orchestrator's whole-repo grade of annas-c `0ba3af8` (via T091; the graded sources are unchanged on `main`)
reports 13 error-level mutant findings, listed in `/var/tmp/cache/A071/survivors-gate.jsonl` (read-only; one JSON
object per finding with rule, file, line, function, operator, original, replacement, mutant id):
- 7 `survived-mutant`: `annas/src/config.c:13 env_get`, `annas/src/exchange.c:66 fill`, `annas/src/name.c:91
  name_text`, `fsname/src/fsname.c:96 fsname_sanitize`, `http/src/redirect.c:80 emit_path`, `http/src/redirect.c:244
  pick_path` (two).
- 5 `crash-kill-ratio` in `json/src/parse.c` (`rot1`, `rot2`, `index_insert`, `fix_red`, `link_node`): their mutants
  are killed mostly by sanitizer crashes, not by assertions. Ratio must reach at least 1/2 asserts.
- 1 `crash-only-function`: `json/src/parse.c is_red` (killed only by crashes).
L3 needs zero survivors and no crash-ratio errors. Your tests are graded against a gate-held witness implementation,
so test **behaviour through the public header** (exact outputs, exact error codes, limits at N and N + 1), never
incidental details. For the json tree findings, the observable behaviour of the key index is what to pin (duplicate-key
detection, lookup results and order-independent outcomes over many insertion orders), so a broken rotation or colour
fails an assertion before it can crash. Keep every test fast under san (A070 lesson: sample sizes, no huge loops).

## Method
1. In a **scratch clone under `/var/tmp/cache/A071/`**, replace `gate/` with a copy of the current gate checkout and add
   `c_mutants` targets for `//annas:annas`, `//fsname:fsname`, `//http:http`, `//json:json` covering every test that
   depends on each module (`docs/mutate.md`). Build on `linux_release`, one mutants build at a time. Check that your
   findings match `survivors-gate.jsonl` (and the crash/assert counts for the json functions); report any difference;
   save `findings-before.txt`. Stop and report if your Buck2 daemon's RSS passes 2.5 GiB.
2. Write tests through the public headers only that kill the survivors by assertion and raise the json functions'
   assert-kill ratio. For a survivor that is an equivalent mutant, explain in your report why no input can tell it
   apart (file:line, mutation). Do not add an exclusion.
3. Rebuild the scratch targets and save `findings-after.txt`.

## Acceptance (the orchestrator re-runs it, and grades the result with the witness)
```sh
for m in release san; do
  gate/tools/buck2 build //annas/... //fsname/... //http/... //json/... --target-platforms toolchains//:linux_$m --show-full-output
done
# every results.jsonl line "pass"; the passing count rises only by your new tests
```
Report before/after findings, the new test ids, per-json-function asserts/crashes before and after, and each finding
left with a one-line reason (equivalent / needs a header decision / could not write). Report every deviation. Reports
never quote witness or gate-internal code.

## Rules
`TMPDIR=/var/tmp/cache/A071/tmp`; never write to `/tmp` (any harness scratchpad under `/tmp` included). Build only with
`gate/tools/buck2`, never host compilers. No `git stash`, no merge of `main`, no push. Never read `/var/tmp/.config`,
`/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs. Never kill processes you did
not start; never `buck2 kill` outside your scratch. No sleep-poll loops (`gate/tools/job`). 2 CPUs, 8 GiB shared. Use
fake keys only (the tests' existing fixtures); never a real key, never the network.
