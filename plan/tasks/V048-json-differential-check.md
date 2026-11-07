# V048: differential check of A048 (json refactor): old and new give identical results

Repo: `Mivr/annas-c` (read-only for you). **Verification task**: no change to any repo. Everything you write goes in
`/var/tmp/cache/V048/`. Read `AGENTS.md`, `gate/AGENTS.md`, and `json/json.h` (the public contract).

## Context
A048 (branch `task/A048`, commits `bdac6a1` and `cc3f7ab`) rewrote parts of `json/src/*.c` to remove redundant code. It claims to be
behaviour-preserving, citing invariants. The unit tests pass, but they are exactly the tests that left those mutants
alive, so they cannot be the proof. You give an independent one.

## Method
1. In a scratch clone under `/var/tmp/cache/V048/`, build two static libraries of `//json:json` (plus `//core:core`):
   OLD from `main` (`daed4a2` or later, without A048) and NEW from `task/A048`. Use the gate's hermetic toolchain through
   Buck2. Then make the symbols distinct (for example `llvm-objcopy --prefix-symbols=old_` and `new_` on the archives, or
   build each with `-D` renames in the scratch only), so one harness binary links both.
2. Write a scratch harness (C, through `json/json.h` only) that, for every public function, feeds both versions the same
   inputs and compares **everything observable**: return code, every output field and slice (content and length; a
   slice into the caller's input compared by offset into that input), and every byte written to caller buffers.
   Inputs:
   - every literal from `json/tests/*.c`;
   - targeted edge cases for each site A048 changed (see its diff): numbers (empty, `-`, `0`, leading zeros, every length up to
     20 digits and u64 overflow at 18446744073709551615/6, fractions and exponents with missing digits, input ending inside
     a number); literals `true`/`false`/`null` cut at every byte and with one byte changed; objects with 0 to 64 keys,
     duplicates at every position, keys sharing prefixes and differing only in length; nesting at the limit and one past it;
     strings with every escape, `\u` with 0 to 4 hex digits, surrogate pairs (valid, lone high, lone low, high then
     `\x`/non-backslash then `uDC00`), every UTF-8 sequence length and every truncation, overlong forms, and input ending
     at every byte;
   - the writer API: every call sequence of length up to 6 over begin/end object/array, key, string, number and literals,
     including misuse (value without key, a second root), with buffers of every capacity from 0 to 64;
   - and at least 200,000 random and mutated documents from a seeded generator, reported with the seed.

   Also run the whole harness under the gate's `linux_san` flags (ASan, UBSan) for both versions.
3. Any difference is a finding: give the function, the input (hex), the old and new outputs (return code, error offset,
   DOM through the public accessors, writer bytes and state), and the A048 site responsible. Also check A048's killable
   input `"\uD800\xDC00"` (expect `JSON_E_UTF8` at offset 1) on OLD and NEW.

## Rules
Read-only on every repo and worktree except that you may `git clone` `/var/tmp/src/annas-c` into your cache dir and
`git fetch` `task/A048` from `/var/tmp/src/annas-c` into it. Never read `/var/tmp/.config`, `/var/tmp/.claude*`,
`/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs. No network except the RE farm. Never kill processes
you did not start. No sleep-poll loops (`gate/tools/job`). 2 CPUs and 8 GiB are shared: keep runs bounded.

## Deliverable (final message)
The harness's coverage (functions, input classes, counts, seed), the sanitizer result, every difference found (or
"none"), the killable-input result, and the paths of the harness and its logs.
