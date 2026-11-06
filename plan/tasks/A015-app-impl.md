# A015: `app` implementation (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A015`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§0, §1, §3.3, §5, §6), the headers of `core`, `json`, `annas`, `ports`, and the contract
`app/app.h` (CLI grammar, usage and error texts, exit codes, `.env` layering, MCP JSON-RPC behaviour).

## Allowed paths
`app/src/*.c`, `app/src/internal.h` (file list fixed by `app/BUCK`: `src/cli.c`, `src/dotenv.c`, `src/mcp.c`,
`src/ops.c`, `src/internal.h`; how you split the code across them is up to you).

## Protected
`app/app.h`, `app/BUCK`, `app/tests/**`, everything else.

## Goal
Implement the documented contract so all 178 `app` tests pass in `release` and `san`, within the C rules (no
recursion, no malloc, no globals or static mutable state, functions <= 50 lines, frames <= 512 bytes, every
return checked). This is the user-facing surface. Output bytes are exact, the key must never appear in any
output, and malformed JSON-RPC must never crash, hang or read out of bounds. Implement the header, not the tests.
If a test contradicts the header, report it and don't work around it.

## Acceptance
```sh
gate/tools/buck2 build //app/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //app/... --target-platforms toolchains//:linux_san --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san   # nothing else broke
```
Report: results per test file in both modes, the file split you chose, and any deviation or contract doubt.
