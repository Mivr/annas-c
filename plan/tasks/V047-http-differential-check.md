# V047: differential check of A047 (http refactor): old and new give identical results

Repo: `Mivr/annas-c` (read-only for you). **Verification task**: no change to any repo. Everything you write goes in
`/var/tmp/cache/V047/`. Read `AGENTS.md`, `gate/AGENTS.md`, and `http/http.h` (the public contract).

## Context
A047 (branch `task/A047`, commit `eae4fc0`) rewrote parts of `http/src/*.c` to remove redundant code. It claims to be
behaviour-preserving, citing invariants. The unit tests pass, but they are exactly the tests that left those mutants
alive, so they cannot be the proof. You give an independent one.

## Method
1. In a scratch clone under `/var/tmp/cache/V047/`, build two static libraries of `//http:http` (plus `//core:core`):
   OLD from `main` (`0e5d9a5` or later, without A047) and NEW from `task/A047`. Use the gate's hermetic toolchain through
   Buck2. Then make the symbols distinct (for example `llvm-objcopy --prefix-symbols=old_` and `new_` on the archives, or
   build each with `-D` renames in the scratch only), so one harness binary links both.
2. Write a scratch harness (C, through `http/http.h` only) that, for every public function, feeds both versions the same
   inputs and compares **everything observable**: return code, every output field and slice (content and length; a
   slice into the caller's input compared by offset into that input), and every byte written to caller buffers.
   Inputs:
   - every literal from `http/tests/*.c`;
   - targeted edge cases for each site A047 changed (see its commit diff; for example status lines with `l[9]`/`l[10]`
     non-digits and codes 0, 199, 200, 599, 600; reason phrases of length 0 to 2; header names with `[ \ ] ^ _ \``
     and upper-case letters; colons at index 0 and at the end; `Content-Length` edge values; chunk sizes and CRLF splits
     at every byte boundary; redirect locations with schemes of length 0 to 6, `//` and `..`/`.` segments, empty and
     `/`-only targets; URLs with `@`, `[`, `?`, `#` before the first `/`, ports of 5 to 7 digits, octets of 3 to 4
     digits);
   - and at least 200,000 random and mutated inputs from a seeded generator, reported with the seed.

   Also run the whole harness under the gate's `linux_san` flags (ASan, UBSan) for both versions.
3. Any difference is a finding: give the function, the input (hex), the old and new outputs, and the A047 site
   responsible. Also check the 2 inputs A047 reported as killable (`redirect.c:80`, `cur.target = {"",0}`, status 302,
   location `"p/q"`; `redirect.c:244`, `cur.target = {NULL,0}`, location `"p"`). Report what OLD and NEW do on each, and
   whether `http.h` allows those inputs.

## Rules
Read-only on every repo and worktree except that you may `git clone` `/var/tmp/src/annas-c` into your cache dir and
`git fetch` `task/A047` from `/var/tmp/src/annas-c` into it. Never read `/var/tmp/.config`, `/var/tmp/.claude*`,
`/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs. No network except the RE farm. Never kill processes
you did not start. No sleep-poll loops (`gate/tools/job`). 2 CPUs and 8 GiB are shared: keep runs bounded.

## Deliverable (final message)
The harness's coverage (functions, input classes, counts, seed), the sanitizer result, every difference found (or
"none"), the two killable-input results, and the paths of the harness and its logs.
