# A004: `json` implementation (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A004`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md`, `core/*.h`, and the contract `json/json.h` (its top comment defines every error offset).

## Allowed paths
`json/src/*.c`, `json/src/internal.h` (the file list is fixed by `json/BUCK`).

## Protected (any change rejects the whole diff)
`json/json.h`, `json/BUCK`, `json/tests/**`, everything else.

## Goal
Implement the documented contract so all `json` tests pass in `release` and `san`, within the C rules:
**no recursion** (iterative parser and writer), no malloc (arena only), functions <= 50 lines, every frame
<= 512 bytes (sealed `-Wframe-larger-than=512`), every return value checked. Implement the header, not the
tests; hidden tests and mutation testing follow. If a test contradicts the header, report it.

## Acceptance
```sh
gate/tools/buck2 build //json/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //json/... --target-platforms toolchains//:linux_san --show-full-output
# every *_results summary: passed == total
```
