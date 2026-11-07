# A051: annas module: `ctx_key` out of the private header, and triage of 118 survivors (T070 audit grade)

Repo: `Mivr/annas-c`. Branch `task/A051`. **Implementer task, behaviour-preserving**: `annas/src/**` only. No public
header change, no test change, no BUCK change. Read `AGENTS.md`, `gate/AGENTS.md` ("Working efficiently"), and, in the
current gate checkout `/var/tmp/src/c-rigor-gate` (read-only), `docs/mutate.md`. Do not edit `plan/`. Do not move the
`gate` submodule.

## Why
The orchestrator's T070 audit grade (all 13 modules) left 118 surviving mutants in `//annas:annas` (`/var/tmp/cache/A051/survivors.tsv`, read-only: rule,
file, line, function, operator, original, replacement, id). Nobody has triaged them yet: expect a mix of redundant code and missing tests (report the latter as killable, with inputs). L3 needs zero survivors, and every equivalent mutant left needs a signed approval. In
this project a surviving mutant is either a missing test or **dead or redundant code**. Typical redundant shapes:
- a check that an earlier check already decides;
- an initialiser that is always overwritten;
- a clamp at a bound that is never reached;
- a branch whose precondition makes it unreachable;
- a case-fold of a range the input can never contain.

Remove that redundancy so the mutants cease to exist, without changing behaviour.

## Requirements
0. **`ctx_key` (9 `unmutated-function` errors, reason `header`).** `ctx_key` is a function defined in the private header
   `annas/src/internal.h`, which the mutator does not mutate. Move its definition into the `.c` file that owns the context
   (keep a declaration in `internal.h`), so it is mutated like any other function. No behaviour change.
1. For each survivor, do one of:
   - (a) **simplify** the code so the mutated token no longer exists or is now decisive: drop a redundant check, initialiser or clamp; merge two checks; replace a hand-rolled case fold of a fixed needle with an exact compare; turn an unreachable branch into straight-line code;
   - (b) **keep** it with a one-line reason: a defensive check the header contract requires, or a check that guards memory safety on a path the tests cannot reach. Those go to the orchestrator as approval candidates;
   - (c) mark it **killable** if you find an input through the public header that distinguishes it. Report the input; do not add the test, because tests are out of scope.
2. Behaviour unchanged: every existing test passes, with no test edited, in both modes. Simplifications must not
   weaken a memory-safety guard that matters for **any** input the header allows. Where you rely on an invariant, such as
   "`ctx->cfg` is non-NULL because `annas_open` checked it", name the function that establishes it,
   and keep a guard if the invariant does not hold for every input the public API accepts.
3. Structure stays clean: build the structure facts with the current gate and run `rigor structure` over `annas` (as
   A042 did, scratch driver outside the repo). No new findings.
4. Measure: in a scratch clone under `/var/tmp/cache/A051/` with the current gate copied over `gate/`, rebuild the `c_mutants`
   target for `//annas:annas` covering all its tests. Report survivors before (should match `survivors.txt`) and after.
   Only one mutants build at a time. Stop if your Buck2 daemon passes 2.5 GiB RSS.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //annas/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //annas/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"; the passing count is unchanged
```
Report the per-survivor decision table (simplified / kept + reason / killable + input), the survivor counts, the
structure result, and every deviation. Target: 0 `unmutated-function`; every survivor decided (simplified, kept with a reason, or killable with an input). Killable ones become a test-author task.
