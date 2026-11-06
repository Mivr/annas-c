# A030: `annas_ctx_init` takes at most 6 parameters (structure gate, L3)

Repo: `Mivr/annas-c`. Branch `task/A030`. Signature-only refactor, no behaviour change.

## Why
The T011 structure gate reports `parameter-count` 7 > 6 for the public `annas_ctx_init` (`annas/annas.h`, L3).

## Allowed paths
`annas/annas.h` (this declaration, its comment, and one new type), `annas/src/**`, and in `**/tests/**` and `app/src/**`
**only the lines that call `annas_ctx_init`** (plus the few lines that build the new argument right before each call).
No other test change: no assertion, test name, expected value or fixture may change. A029 runs in parallel on
`*/src/**` of `http`, `app` (`ops.c`) and `annas/src/msg.c`: don't touch those files.

## Requirements
1. Group the three ports (`stream`, `fs`, `clock`) into one by-value struct (for example `annas_ports_t`), or another
   grouping you justify, so `annas_ctx_init` has <= 6 parameters. Same semantics, same validation (a missing port is
   still the same error), documented in the header comment.
2. Every call site updated. Release and san: build `//...`, sum the `<name>_results` files: the same test ids and
   the same pass count as before (1454/1454), listed before and after.
3. `git diff` of `**/tests/**` must show only call-site lines; say how many hunks and that each is a call site.

## Acceptance
```sh
gate/tools/buck2 build //... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san --show-full-output
```
