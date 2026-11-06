# A041: structure blockers: `indirect-cycle-possible` in wire/lazyfs, and `app/app.h` line length

Repo: `Mivr/annas-c`. Branch `task/A041`. Read `AGENTS.md`, plus `gate/docs/structure.md` sections on
`indirect-cycle-possible` and "Known limits".
Source: the T059 audit grade of main `f990233` (2026-11-06). Its `structure` gate fails with:
- 6 × `indirect-cycle-possible` (error at L3), all in `wire/src/lazyfs.c`. The functions `lz_create_temp`,
  `lz_write`, `lz_commit` and `lz_discard` have their address taken in `k_lazy_ops`, and each makes a call through
  a value (`p.ops->write(...)` on the posix_fs port). The rule cannot tell which functions a pointer may hold, so a
  table function that itself calls through a pointer is a possible cycle. The grade also cites `posix_fs_open_sys`
  in `posix_fs/src/fs.c:47`.
- 2 × `line-length` in `app/app.h`: line 19 has 166 characters and line 163 has 163, against a limit of 160.

## Part 1: diagnose, then fix the cycle in the product (role: implementer, behaviour-preserving refactor)
1. First, write down the call graph the rule sees: F (address taken), the call through a value G, and the direct
   path from F to G. Do this for each of the 6 findings. Get the facts from a local build of `//wire:wire_facts`
   (or the structure facts target), not from guessing.
2. Fix it so no table function reaches a call through a value, with behaviour unchanged. Preferred: lazyfs
   delegates to posix_fs by **direct calls**. If posix_fs has no direct entry points for the four operations,
   you may **add** them to `posix_fs/posix_fs.h`: `posix_fs_create_temp`, `posix_fs_write`, `posix_fs_commit`
   and `posix_fs_discard`, each taking `posix_fs_t *`, with the same semantics as the port ops. The port's ops then
   call these. Adding functions is allowed; changing or removing any existing declaration or documented
   behaviour is not.
3. Check that posix_fs's own calls through its `posix_fs_sys_t` pointers do not reintroduce the finding. If they
   do, stop and report the exact chain. The rule may then need a gate-side refinement (orchestrator), and you
   must not invent a workaround that hides the call (no casts through `void *`, no tables that the scan does
   not see; those are bypasses).
4. Tests: all existing tests must pass unchanged. If you add posix_fs functions, the existing posix_fs and wire
   tests must still exercise them through the port. Add direct-call tests only under `posix_fs/tests/` if a new
   function would otherwise be untested, and keep them minimal.

## Part 2: `app/app.h` line length (whitespace and line breaks only)
Reflow lines 19 and 163 so no line exceeds 160. Check the whole header.

## Allowed paths
`wire/src/**`, `posix_fs/src/**`, `posix_fs/posix_fs.h` (additions only), `posix_fs/tests/**`, `posix_fs/BUCK`
(test entries only), `app/app.h` (whitespace only). Do not edit any other test.

## Acceptance
```sh
gate/tools/buck2 build //... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"
awk 'length > 160' app/app.h | wc -l    # 0
diff <(git show main:app/app.h | tr -d ' \t\n*') <(tr -d ' \t\n*' < app/app.h) && echo same-text
```
Then show that the structure facts no longer give the 6 findings: run `gate/rigor structure` over the built
wire and posix_fs structure facts (see `gate/docs/structure.md`), and report its findings for both modules.
Report the call-graph diagnosis, the change, the acceptance tails and every deviation.
