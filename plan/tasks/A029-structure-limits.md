# A029: annas-c src meets the L3 structure limits (first real findings of the T011 structure gate)

Repo: `Mivr/annas-c`. Branch `task/A029`. Implementer task: **`src/` only**, no header, BUCK or test change.

## Why
c-rigor-gate T011 added `rigor structure` (recursion and size/complexity limits from the clang AST). Run on annas-c
main (`3152577`), information only, it found these in product code (L3 limits: body 50 lines, params 6, nesting 4,
cyclomatic 15, line 160 columns):

| Function / file | Finding |
|---|---|
| `is_tchar` (`http/src/header.c`) | cyclomatic 21 |
| `http_status_line` (`http/src/header.c`) | cyclomatic 20 |
| `http_redirect_resolve` (`http/src/redirect.c`) | cyclomatic 19 |
| `http_header_line` (`http/src/header.c`) | cyclomatic 17 |
| `seq_len` (`app/src/ops.c`) | cyclomatic 17 |
| `http_url_split` (`http/src/url.c`) | cyclomatic 16 |
| `msg_fail` (`annas/src/internal.h`, `msg.c`) | 7 parameters |
| `app/src/mcp.c` (if under `src/`; else report it) | a line of 165 columns |

Out of scope, report only: `annas_ctx_init` (7 parameters, public `annas.h`), long lines in test kits and `app.h`.
Those need a test-author task.

## Allowed paths
`*/src/**` only. Headers under `*/<module>.h` or `include/`, `*/BUCK`, `*/tests/**` and `gate` are protected.

## Requirements
1. Every finding above in `src/` is gone, with **no behaviour change**: same public functions, same results on every
   input. Split decisions into small `static` helpers with clear names (a lookup table is fine for `is_tchar`);
   no macros that hide control flow, no recursion, no function pointers to dodge the count.
2. The whole repo builds and every test passes in release and san (`gate/tools/buck2 build //... --target-platforms
   toolchains//:linux_{release,san}`; sum the `<name>_results` files), with the same test count as before (1454).
3. Measure with the structure gate before and after: copy c-rigor-gate main into your cache
   (`git clone /var/tmp/src/c-rigor-gate`, at `e3500c6` or later), generate `c_structure` targets for annas-c's
   library and binary modules in a scratch copy of the repo (as `docs/structure.md` describes), and run
   `rigor structure`. Report the findings table before and after. Don't commit those generated targets.
4. Mutation: run the mutants of the modules you changed (`docs/mutate.md` in the gate) and report survivors before
   and after; no new survivor.

## Acceptance
Release and san: 1454/1454 pass. `rigor structure` on the scratch copy: 0 findings in `src/` among the ones listed.
