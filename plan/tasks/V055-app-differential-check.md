# V055: differential check of A055 (app refactor, including the key scrubbing): old and new give identical output

Repo: `Mivr/annas-c` (read-only for you). **Verification task**: no change to any repo. Everything you write goes in
`/var/tmp/cache/V055/`. Read `AGENTS.md`, `gate/AGENTS.md`, `app/app.h` (the public contract, in particular "the product has one
scrubbing rule" and "Memory and output"), and `ports/fakes.h` (the fakes you drive the app with).

## Context
A055 (branch `task/A055`, commits `f550cd7` and `3b3c21c`) rewrote parts of `app/src/{cli,dotenv,mcp,ops}.c` to remove
redundant code. `ops.c` holds the **API-key scrubbing**, and a behaviour change there can leak a key into output. A055 claims
to be behaviour-preserving. Prove or refute it independently. Use only fake keys you make up, such as `kKk-test-0123`; there is
no real key anywhere in this task, and you must not look for one.

## Method
1. In a scratch clone under `/var/tmp/cache/V055/`, build `//app:app` and its deps twice: OLD from `main` (`daed4a2` or later,
   without A055) and NEW from `task/A055`. Rename the symbols of the app archive (for example `llvm-objcopy --prefix-symbols`
   on `app` only, linking one shared copy of core, ports and the fakes), so one harness binary drives both through `app/app.h`.
   If renaming is impractical, build two harness binaries and compare their outputs byte for byte instead.
2. Drive every public entry point of `app.h` with the ports fakes, for both versions, and compare **everything observable**:
   return code and exit code, every byte of stdout and stderr text, the MCP reply bytes, fs-fake contents, and the clock and
   env calls made. Inputs:
   - every argv and MCP request literal from `app/tests/*.c` and `wire/tests/*.c`;
   - targeted cases for each A055 site:
     - CLI option parsing: every option, a missing value, a duplicate, an unknown option, a negative or overflowing number,
       domains with dots at every position, and argc 0 to 3;
     - dotenv: names and values wrapped in every whitespace byte including `\v` and `\f`, empty values, lines of `#`, `\r`,
       `\r\n` and lone `\r`, and the file slices `{NULL,0}` and `{NULL,1}`;
     - MCP: every protocol version offered (known, unknown, missing), every tool and error path, two requests on one session,
       and ids from 0 bytes to 20,000 bytes.
   - **Scrubbing**, with keys of length 0, 1, 2, 255, 256 and 257 and with special characters (`&`, `%`, `+`, `=`, `/`, space,
     UTF-8). Cover:
     - the key, its percent-encoded form and its JSON-escaped form at the start, middle and end of the text, adjacent, and
       overlapping;
     - texts just under, at and over 64 KiB before and after each pass;
     - control bytes 0x00 to 0x1f in tool results.
   - and at least 100,000 random argv and MCP-request cases from a seeded generator, reported with the seed.

   Also run the harness under the gate's `linux_san` flags (ASan, UBSan) for both versions.
3. **Any difference is a finding**: give the entry point, the input (hex), the old and new outputs, and the A055 site.
   Separately, **for every case of either version, check that no output byte sequence equals the key or its encoded forms**
   when the key is non-empty. That is the scrubbing invariant. Any hit is a finding even if OLD and NEW agree.

## Rules
Read-only on every repo and worktree, except that you may `git clone` `/var/tmp/src/annas-c` into your cache dir and fetch
`task/A055` into it. Never read `/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or
other cache dirs. No network at all. Never kill processes you did not start. No sleep-poll loops (`gate/tools/job`). 2 CPUs and
8 GiB are shared, so keep runs bounded. Never write to `/tmp`.

## Deliverable (final message)
The coverage (entry points, input classes, counts, seed), the sanitizer result, every difference found (or "none"), the
scrubbing-invariant result, and the paths of the harness and its logs.
