# A010: `ports` fakes implementation (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A010`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`core/*.h`, and the contracts `ports/ports.h` and `ports/fakes.h` (the comments define call-order precedence and
every error).

## Allowed paths
`ports/src/*.c` only.

## Protected
`ports/*.h`, `ports/BUCK`, `ports/tests/**`, everything else.

## Goal
Implement the fakes so every `ports` test passes in `release` and `san`, within the C rules (no malloc, no IO,
no recursion, functions <= 50 lines, frames <= 512 bytes, every return checked). The fakes are what the pure
`annas` module will be tested against, so they must be exactly as strict as the header says. Implement the header,
not the tests. If a test contradicts the header, report it.

## Acceptance
```sh
gate/tools/buck2 build //ports/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //ports/... --target-platforms toolchains//:linux_san --show-full-output
```
