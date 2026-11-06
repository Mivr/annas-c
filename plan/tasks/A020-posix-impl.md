# A020: POSIX adapters `posix_fs`, `posix_env`, `posix_clock`: implementation (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A020`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`ports/ports.h` (the contracts), and the contracts `posix_fs/posix_fs.h`, `posix_env/posix_env.h` and
`posix_clock/posix_clock.h` (seams, errno mapping, check order, temp-file and commit rules, clamp and saturation).

## Allowed paths
`posix_fs/src/*.c`, `posix_fs/src/internal.h`, `posix_env/src/*.c`, `posix_clock/src/*.c` (the file list is fixed
by each BUCK).

## Protected
The headers, the BUCK files, `*/tests/**` (including the `posix_fs` testkit), and everything else.

## Goal
Implement the documented contracts so all 110 tests pass in `release` and `san`, within the C rules (no
recursion, no malloc, no mutable globals or statics, functions of 50 lines or fewer, frames of 512 bytes or
fewer, every return checked). Every OS call goes through the seam table. The real libc tables are the only
place that touches libc IO. These adapters write the user's files: a failed download must never leave a
visible file, and nothing may ever be overwritten. Implement the headers, not the tests. If a test contradicts a
header, report it.

## Acceptance
```sh
gate/tools/buck2 build //posix_fs/... //posix_env/... //posix_clock/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //posix_fs/... //posix_env/... //posix_clock/... --target-platforms toolchains//:linux_san --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san
```
Report: the results per test file in both modes, and every deviation or doubt about the contract.
