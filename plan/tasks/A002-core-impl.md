# A002: `core` module implementation (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A002`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md`, and the four headers in `core/*.h` (they are the contract).

## Allowed paths
`core/src/*.c` only.

## Protected (any change rejects the whole diff)
`core/*.h`, `core/BUCK`, `core/tests/**`, everything else in the repo.

## Goal
Replace the stubs with a real implementation of the documented API so that all `core` tests pass in `release`
and `san`, within the C rules (C17, no malloc, no recursion, no VLAs, functions <= 50 lines, every return value
checked, sealed warnings). Implement the **documented contract**, not the tests: hidden tests and mutation testing
will check behaviour the visible tests don't. If a test contradicts its header, do not work around it: report it.

## Acceptance (the orchestrator re-runs)
```sh
gate/tools/buck2 build //core/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //core/... --target-platforms toolchains//:linux_san --show-full-output
# every *_results summary: passed == total
```
Report the results summaries for both modes and anything in the headers you found ambiguous.
