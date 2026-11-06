# A022: linear `json` duplicate-key detection and linear `http` dot-segment removal (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A022` (based on `task/A021` + main; both merge together after this passes).
Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`, `json/json.h`, `http/http.h`, and the
new tests `json/tests/complexity_test.c` and `http/tests/complexity_test.c` with their size arithmetic.

## Allowed paths
`json/src/*.c`, `json/src/internal.h`, `http/src/*.c`, `http/src/internal.h` (the file lists are fixed by BUCK).

## Protected
Headers, BUCK files, all tests, everything else.

## Goal
10 tests time out on main: 5 json duplicate-key tests (wide objects, long shared prefixes, a base-31 hash-collision
family) and 5 `http_redirect_resolve` dot-segment tests. Make every `json` and `http` test pass in `release` and
`san`, each well under 1 s, **within the C rules: no malloc, no globals or mutable statics, no recursion,
functions of 50 lines or fewer, frames of 512 bytes or fewer.**
- json: the only memory is the caller's parse arena. A duplicate-key index (for example a hash table per object, or a
  sort of key references at object close) must live in the arena and must not make a document that fits today
  stop fitting, unless the header's arena-sizing rule allows it. Read that rule exactly and report how much
  extra arena your approach needs in the worst case. A hash must resist the collision family in the tests,
  which plain multiplicative string hashes don't: think about it (seeded, or an O(n log n) sort-based check with
  no hashing at all).
- http: resolve in the output buffer itself (a write cursor plus backtracking to the previous `/`), not with a
  segment stack of unbounded size.
If the contract makes a linear solution impossible without a header change, stop and report it; don't change
the header.

## Acceptance
```sh
gate/tools/buck2 build //json/... //http/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //json/... //http/... --target-platforms toolchains//:linux_san --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san
```
Report: the results per test file in both modes, the algorithms, the worst-case extra arena, and any deviation.
