# A026: implement `core_buf_truncate`; rewrite `http_redirect_resolve` dot-segment removal as a write cursor (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A026` (based on `task/A025` + main; both merge together after this passes).
Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`, `core/buf.h` (the new `core_buf_truncate`
contract), `http/http.h` (the `http_redirect_resolve` contract), RFC 3986 §5.2.4, and `http/tests/**`
(especially `complexity_test.c`).

## Allowed paths
`core/src/buf.c`, `http/src/redirect.c` (and `http/src/internal.h` only if a declaration changes).

## Protected
Headers, BUCK files, all tests, everything else.

## Goal
1. Implement `core_buf_truncate` exactly as the header says. All `core` tests pass.
2. Replace the block-tree dot-segment algorithm in `http/src/redirect.c` with the RFC 3986 §5.2.4 write cursor over a
   `core_buf_t`: append segments, and on `..` truncate back to the previous `/` with `core_buf_truncate`. It must be
   linear time and space within the output buffer, and the code must be markedly smaller. Report the before and after
   line counts of `redirect.c`. Behaviour must stay byte-identical: every `http` test, including the 19 complexity
   tests and the overflow cases, passes in `release` and `san`. C rules apply (no recursion, no malloc, no mutable
   statics, functions of 50 lines or fewer, frames of 512 bytes or fewer). Never read `core_buf_t` fields directly.

## Acceptance
```sh
gate/tools/buck2 build //core/... //http/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //core/... //http/... --target-platforms toolchains//:linux_san --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san
```
