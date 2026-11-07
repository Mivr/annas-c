# V051: differential check of A051 (annas refactor): old and new give identical results

Repo: `Mivr/annas-c` (read-only for you). **Verification task**: no change to any repo. Everything you write goes in
`/var/tmp/cache/V051/`. Read `AGENTS.md`, `gate/AGENTS.md`, `annas/annas.h` (the public contract) and `annas/src/internal.h`.

## Context
A051 (branch `task/A051`, commits `6f7a832` and `85d77cc`) rewrote parts of `annas/src/{api,config,ctx,download,exchange,
format,msg,name,redact}.c` to remove redundant code. It claims to be behaviour-preserving, citing invariants. Two internal
contracts changed on purpose: `redact_into` now leaves `*out` untouched when the redacted text does not fit, and
`exch_event` can now return kind NONE. Confirm that neither change is observable through `annas/annas.h`. The unit tests
pass, but they are exactly the tests that left those mutants alive, so they cannot be the proof. You give an independent one.

## Method
1. In a scratch clone under `/var/tmp/cache/V051/`, build two static libraries of `//annas:annas` and its deps: OLD from
   `202e20d` (the merge base, so the deps are identical) and NEW from `task/A051`. Use the gate's hermetic toolchain through Buck2.
   Then make the symbols distinct (for example `llvm-objcopy --prefix-symbols=old_`/`new_` on the archives, or `-D` renames
   in the scratch only), so one harness binary links both. Drive both through the same fake ports (stream, clock, env, fs)
   from the repo's test fakes, replaying identical scripted server responses to both.
2. Write a scratch harness (C, through `annas/annas.h` only) that, for every public function, feeds both versions the same
   inputs and compares **everything observable**: return code, every output field and slice, every byte written to caller
   buffers, every byte the library sends to the fake stream (request line, headers, body), every file it writes through the
   fake fs, and every log or message string. Inputs:
   - every literal from `annas/tests/*.c`;
   - targeted edge cases for each site A051 changed (see its diff): config with every field missing, empty, at its maximum
     length and one past it, keys of every length 0 to 128 including non-printable and `%`-escaped bytes; md5/ids of every
     length and with one invalid hex digit at every position; file names needing every sanitisation rule and of every length
     to the limit; output buffers of every capacity from 0 to the needed size plus 2;
   - redaction: **only fake keys you generate** (never a real one). For every message and log path, the key must not appear
     in any output byte, in either version, including when the redacted text does not fit the buffer. A redaction leak in
     NEW that OLD did not have is a blocking finding; one in both versions is a separate finding.
   - scripted server responses: every status class, redirects, truncated and oversized headers and bodies, JSON with every
     field missing or of the wrong type, the connection closing at every byte of a recorded response;
   - and at least 100,000 random and mutated configs and responses from a seeded generator, reported with the seed.

   Also run the whole harness under the gate's `linux_san` flags (ASan, UBSan) for both versions.
3. Any difference is a finding: give the function, the input (hex), the old and new outputs (return code, buffers, bytes sent,
   files, messages), and the A051 site responsible.

## Rules
Read-only on every repo and worktree except that you may `git clone` `/var/tmp/src/annas-c` into your cache dir and
`git fetch` `task/A051` from `/var/tmp/src/annas-c` into it. Never read `/var/tmp/.config`, `/var/tmp/.claude*`,
`/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs, and never look for or use a real Anna's key. No network
except the RE farm. Never kill processes you did not start. No sleep-poll loops (`gate/tools/job`). Never write to `/tmp`;
use `TMPDIR=/var/tmp/cache/V051/tmp`. Do not use `git stash`. 2 CPUs and 8 GiB are shared: keep runs bounded.

## Deliverable (final message)
The harness's coverage (functions, input classes, counts, seed), the sanitizer result, the redaction-invariant result,
every difference found (or "none"), and the paths of the harness and its logs.
