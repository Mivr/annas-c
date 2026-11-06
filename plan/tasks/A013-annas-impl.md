# A013: `annas` implementation (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A013`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§0-§3.3, §5, §6, §7), the headers of `core`, `json`, `url`, `fsname`, `http`, `ports`, and the
contract `annas/annas.h` (error table, exact messages, redaction, timeout and stream rules).

## Allowed paths
`annas/src/*.c`, `annas/src/internal.h` (file list fixed by `annas/BUCK`).

## Protected
`annas/annas.h`, `annas/BUCK`, `annas/tests/**`, everything else.

## Goal
Implement the documented contract so every `annas` test passes in `release` and `san`, within the C rules (no
recursion, no malloc, no globals or static mutable state: everything lives in the caller's workspace;
functions <= 50 lines; frames <= 512 bytes; every return checked). This module handles the secret key and
writes files: the key must never reach any message, and a failed download must never leave a file. Implement the
header, not the tests. If a test contradicts the header, report it.

## Acceptance
```sh
gate/tools/buck2 build //annas/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //annas/... --target-platforms toolchains//:linux_san --show-full-output
```
