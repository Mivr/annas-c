# A019: `app` rework against the A018 contract (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A019` (based on `task/A018` + main; both merge together after this passes).
Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`, `core/buf.h`, `annas/annas.h`, and the
contract `app/app.h`, especially the new "Memory and output" rule, `APP_SCRATCH_MIN`, the two-pass scrubbing,
the MCP shortfall order, `APP_MCP_CALL_MEM`/`APP_MCP_REPLY_MAX` and the UTF-8 rule.

## Allowed paths
`app/src/*.c`, `app/src/internal.h`.

## Protected
`app/app.h`, `app/BUCK`, `app/tests/**`, everything else.

## Goal
Today's `app/src` borrows spare capacity of `out`/`errout` by reading `core_buf_t` internals, does single-pass
scrubbing, and has no all-or-nothing output. 18 of the 33 new tests fail. Rework it so that all 211 `app` tests
pass in `release` and `san`. Use only `deps->scratch`, the parse arena and `core/buf.h` functions; reading any
`core_buf_t` field directly is forbidden. Keep the C rules (no recursion, no malloc, no mutable globals or
statics, functions of 50 lines or fewer, frames of 512 bytes or fewer, every return checked). Implement the
header, not the tests.

## Acceptance
```sh
gate/tools/buck2 build //app/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //app/... --target-platforms toolchains//:linux_san --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san
grep -nE '\b(data|cap|len|overflow)\b' app/src/*.c   # no core_buf_t field access (report the output)
```
