# A028: every product BUCK passes the BUCK guard; `http.h` states the redirect buffer requirement (test-author housekeeping)

Repo: `Mivr/annas-c`. Branch `task/A028`. Read `AGENTS.md`, `gate/AGENTS.md`, `gate/docs/product-checks.md` (the BUCK guard:
literal macro calls only, no assignments, no `+`, deps same-cell or exactly `gate//third_party/...` from the
allowlists, and macros add `gate//ctest:ctest` themselves), every `*/BUCK`, and `http/http.h` (`http_redirect_resolve`).

## Why
`app/`, `http/`, `posix_*` and other modules were never graded (gate B14), so their BUCK files were never checked.
Known violations: `gate//ctest:ctest` named in the testkit deps of `annas/BUCK`, `app/BUCK` and `posix_fs/BUCK`, and a
`COMMON_TEST_DEPS` assignment in `posix_fs/BUCK`. A026 changed `http_redirect_resolve` so it reports `HTTP_E_OVERFLOW`
when the merged path *before* dot-segment removal doesn't fit `out`; the header must say so.

## Allowed paths
Every `*/BUCK` (structure only: same targets, same srcs and headers, same effective deps), test kit sources only if a
kit must move into each test's `srcs` (as `wire/BUCK` does), and the `http_redirect_resolve` comment in `http/http.h`.
No other header or test change, and no `src` change.

## Acceptance
```sh
gate/rigor buckfiles --root .          # or the exact gate command for the BUCK guard: 0 findings over the whole repo
gate/tools/buck2 build //... --target-platforms toolchains//:linux_release --show-full-output   # every test still passes
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san --show-full-output
```
Report: every BUCK diff, the guard output before and after, and the test totals before and after (they must be equal).
