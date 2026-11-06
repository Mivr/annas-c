# A006: `url` and `fsname` implementation (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A006`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§1, §2, §3, §7), `core/*.h`, and the contracts `url/url.h`, `fsname/fsname.h`.

## Allowed paths
`url/src/*.c`, `fsname/src/*.c` only.

## Protected (any change rejects the whole diff)
`url/*.h`, `fsname/*.h`, `url/BUCK`, `fsname/BUCK`, `url/tests/**`, `fsname/tests/**`, everything else.

## Goal
Implement the documented contracts so all `url` and `fsname` tests pass in `release` and `san`, within the C
rules. Implement the header contract, not the tests: hidden tests and mutation testing follow. The sanitiser is a
security boundary. If a test contradicts its header, report it; do not work around it.

## Acceptance
```sh
gate/tools/buck2 build //url/... //fsname/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //url/... //fsname/... --target-platforms toolchains//:linux_san --show-full-output
# every *_results summary: passed == total
```
