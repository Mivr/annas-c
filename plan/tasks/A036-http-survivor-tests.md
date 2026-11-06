# A036: http tests kill the surviving mutants (R004 T6/T7 classes; A029 survivor list)

Repo: `Mivr/annas-c`. Branch `task/A036`. **Test-author task**: `http/tests/**` only, no `src/`, no public header, no
BUCK change unless a new test file needs registering (then only `http/BUCK`, test entries only). Read `AGENTS.md` and
`gate/AGENTS.md` ("Working efficiently": use `gate/tools/job` if present, else run long builds in the foreground
with a long timeout). Do not edit `plan/`. In flight in parallel: the other survivor task (A037 on `app/tests/**`). 

## Why
A029 ran the mutants of `http` (in a scratch copy) and found pre-existing survivors; L3 requires zero. The weak spots it
named: `header.c` `status_code` (digit positions 9 and 10 of the status line) and `is_chunked_ci`; `redirect.c` `resolve_args_ok` (NULL pointer with a non-zero length), `pick_path`, `ref_parse`; `url.c` scheme handling, `next_label`, `octet_ok`; `chunk.c` (size parsing, extensions, CRLF handling). R004 (`plan/audits/R004-test-adequacy.md`, T6/T7) lists the expected classes (product-macro limits at
limit and limit + 1, NULL conditions, exact error codes per site).

## Method
1. In a **scratch clone under `/var/tmp/cache/A036/`** with `gate/` copied from your worktree, add a `c_mutants` target
   for `//http:http` covering all of its tests (see `gate/docs/mutate.md`; if header-only test kits break the rule, drop
   them from `deps` and add the transitive libraries, as A029 did). Build it on `linux_release` and list the
   survivors (`gate/rigor mutants` or the report JSON). Save the list as `/var/tmp/cache/A036/survivors-before.txt`.
2. Write tests in `http/tests/` through the public header only, each pinning exact outputs, that kill as many survivors
   as possible, starting with the named weak spots. A survivor that is an equivalent mutant: explain why in your
   report (file:line, mutation, why no input can tell it apart); do not add an exclusion.
3. Rebuild the scratch `c_mutants` target with your tests and save `survivors-after.txt`.

## Acceptance (the orchestrator re-runs it)
1. `gate/tools/buck2 build //http/... --target-platforms toolchains//:linux_release --show-full-output` and `linux_san`:
   every test passes on today's `http/src`; list the new test ids.
2. Before/after survivor counts and the list of survivors left, each with a one-line reason (missing test I could not
   write / equivalent / needs a header decision). Target: the named weak spots all killed, total survivors at most
   half of before.
Report every deviation.
