# A046: url and fsname tests kill the surviving mutants (T064 annas-c grade: 14 findings)

Repo: `Mivr/annas-c`. Branch `task/A046`. **Test-author task** (role `test-author`): only `url/tests/**`, `fsname/tests/**`, plus `url/BUCK`, `fsname/BUCK` for
test entries only. No `src/`, no public header, no other BUCK change. Read `AGENTS.md`, `gate/AGENTS.md`
("Working efficiently": `gate/tools/job` for long builds) and, in the **current gate** checkout
`/var/tmp/src/c-rigor-gate` (read-only, newer than the pinned `gate/`), `docs/mutate.md` (T064 batching) and
`docs/witness.md` ("test-author"). Do not edit `plan/`. Do not move the `gate` submodule.
In flight in parallel: the other survivor tasks (A044 json, A045 http, A046 url and fsname), each on its own module's
tests only.

## Why
The orchestrator's annas-c audit grade (via T064, annas-c `02090de`; url and fsname sources are unchanged on main since)
reports for `//url:url`, `//fsname:fsname` the findings in `/var/tmp/cache/A046/survivors-gate.tsv` (read-only;
rule, file, line, function, operator, original, replacement, mutant id). L3 needs zero survivors and no
`crash-only-function`. Your tests are later graded against a gate-held witness implementation, so they must test
**behaviour through the public header** (exact outputs, exact error codes, limits at N and N + 1), never the current
code's incidental details.
Small set: the target is **zero** survivors left, or an equivalence argument for each one left.

## Method
1. In a **scratch clone under `/var/tmp/cache/A046/`**, replace `gate/` with a copy of the current gate checkout
   (`/var/tmp/src/c-rigor-gate`, which batches mutants), and add `c_mutants` targets for `//url:url`, `//fsname:fsname`
   covering every test that depends on the module (see `docs/mutate.md`; follow the deps the gate's grade generates).
   Build on `linux_release`. Check that your survivor list matches `survivors-gate.tsv` and report any difference.
   Save it as `survivors-before.txt`. Only one mutants build at a time in your lane. Other lanes share the
   8 GiB container, so stop and report if your Buck2 daemon's RSS passes 2.5 GiB.
2. Write tests through the public header only, each pinning exact outputs, that kill the survivors. For a
   survivor that is an equivalent mutant, explain in your report why no input can tell it apart (file:line, mutation).
   Do not add an exclusion.
3. Rebuild the scratch `c_mutants` targets with your tests and save `survivors-after.txt`.

## Acceptance (the orchestrator re-runs it, and grades the result with the witness)
```sh
gate/tools/buck2 build //url/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //fsname/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //url/... --target-platforms toolchains//:linux_san --show-full-output
gate/tools/buck2 build //fsname/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"; the passing count rises only by your new tests
```
Report the before and after survivor counts, the new test ids, and each survivor left with a one-line reason
(equivalent / needs a header decision / could not write). Target: at most a quarter of the findings left, and no
`crash-only-function`. Report every deviation. Reports never quote witness or gate-internal code.
