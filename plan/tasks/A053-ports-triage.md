# A053: ports, posix_clock and posix_env: survivor triage toward L3 (63 survivors in the T070 audit grade)

Repo: `Mivr/annas-c`. Branch `task/A053`. **Implementer task, behaviour-preserving**: `ports/src/**`, `posix_clock/src/**`, `posix_env/src/**` only. No public
header change, no test change, no BUCK change. Read `AGENTS.md`, `gate/AGENTS.md` ("Working efficiently"), and, in the
current gate checkout `/var/tmp/src/c-rigor-gate` (read-only), `docs/mutate.md`. Do not edit `plan/`. Do not move the
`gate` submodule.

## Why
The orchestrator's T070 audit grade (all 13 modules) reported 63 surviving mutants (`survivor`, info at today's level L2) in `//ports:ports`, `//posix_clock:posix_clock`, `//posix_env:posix_env` (`/var/tmp/cache/A053/survivors.tsv`, read-only: rule,
file, line, function, operator, original, replacement, id). Nobody has triaged them yet: expect a mix of redundant code and missing tests (report the latter as killable, with inputs). These modules are at L2 today; moving them to L3 needs zero survivors, and every equivalent mutant left needs a signed approval. In
this project a surviving mutant is either a missing test or **dead or redundant code**. Typical redundant shapes:
- a check that an earlier check already decides;
- an initialiser that is always overwritten;
- a clamp at a bound that is never reached;
- a branch whose precondition makes it unreachable;
- a case-fold of a range the input can never contain.

Remove that redundancy so the mutants cease to exist, without changing behaviour.

## Requirements
1. For each survivor, do one of:
   - (a) **simplify** the code so the mutated token no longer exists or is now decisive: drop a redundant check, initialiser or clamp; merge two checks; replace a hand-rolled case fold of a fixed needle with an exact compare; turn an unreachable branch into straight-line code;
   - (b) **keep** it with a one-line reason: a defensive check the header contract requires, or a check that guards memory safety on a path the tests cannot reach. Those go to the orchestrator as approval candidates;
   - (c) mark it **killable** if you find an input through the public header that distinguishes it. Report the input; do not add the test, because tests are out of scope.
2. Behaviour unchanged: every existing test passes, with no test edited, in both modes. Simplifications must not
   weaken a memory-safety guard that matters for **any** input the header allows. Where you rely on an invariant, such as
   "the port's caller validated the argument first", name the function that establishes it,
   and keep a guard if the invariant does not hold for every input the public API accepts.
3. Structure stays clean: build the structure facts with the current gate and run `rigor structure` over `ports`, `posix_clock`, `posix_env` (as
   A042 did, scratch driver outside the repo). No new findings.
4. Measure: in a scratch clone under `/var/tmp/cache/A053/` with the current gate copied over `gate/`, rebuild the `c_mutants`
   target for `//ports:ports`, `//posix_clock:posix_clock`, `//posix_env:posix_env` covering all its tests. Report survivors before (should match `survivors.txt`) and after.
   Only one mutants build at a time. Stop if your Buck2 daemon passes 2.5 GiB RSS.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //ports/... //posix_clock/... //posix_env/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //ports/... //posix_clock/... //posix_env/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"; the passing count is unchanged
```
Report the per-survivor decision table (simplified / kept + reason / killable + input), the survivor counts, the
structure result, and every deviation. Target: every survivor decided (simplified, kept with a reason, or killable with an input). Killable ones become a test-author task.
