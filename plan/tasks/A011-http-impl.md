# A011: `http` implementation (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A011`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§0, §2), `core/*.h`, and the contract `http/http.h`.

## Allowed paths
`http/src/*.c`, `http/src/internal.h` (file list fixed by `http/BUCK`).

## Protected
`http/http.h`, `http/BUCK`, `http/tests/**`, everything else.

## Goal
Implement the documented contract so every `http` test passes in `release` and `san`, within the C rules (no
recursion, no malloc, functions <= 50 lines, frames <= 512 bytes, every return checked). This parses hostile
network input: implement the header's rules exactly (strict CRLF, smuggling rejections, limits enforced as bytes
arrive), not just the visible tests. If a test contradicts the header, report it.

## Acceptance
```sh
gate/tools/buck2 build //http/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //http/... --target-platforms toolchains//:linux_san --show-full-output
```
