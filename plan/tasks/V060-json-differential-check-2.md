# V060: differential check of A060 (json remove-or-justify): old and new give identical results

Repo: `Mivr/annas-c` (read-only for you). **Verification task**: no change to any repo. Everything you write goes in
`/var/tmp/cache/V060/`. Read `AGENTS.md`, `gate/AGENTS.md` and `json/json.h`.

## Context
A060 (branch `task/A060`, commits `9daf696`, `0295d09`) changed `json/src/{parse,utf8,writer}.c` and `internal.h`:
- `read_str` ignores the second decode pass's result (claim: it re-reads what the first pass accepted, so it cannot fail);
- `descend` picks the direction with an explicit three-way branch;
- `member` uses a new `json_byte_is`, and `utf8.c`'s byte access returns NULL past the end instead of a sentinel;
- `json_parse` no longer maps a failed arena rewind to `JSON_E_ARG`;
- `writer.c`'s internal `json_w_number_text` lost its unusable-writer early return.

## Method
A previous check (V048) built a harness that you may reuse: a copy of its sources is in
`/var/tmp/cache/V060/harness-from-V048/` (BUCK, `tests/diff.c`, `snap_*.c`, `ojson-renamed-old/`) and its corpus
generator in `/var/tmp/cache/V060/scripts-from-V048/`. Rebuild it with OLD = `json/src` at `7084988` (the merge base;
renamed with the same `json_`->`ojson_` / `JSON_`->`OJSON_` sed, verify the rename is pure) and NEW = `task/A060`'s
`json/src`, both on the same core. Run it at the same scale (seed reported, at least 250,000 random documents, the
exhaustive classes) in release and under `linux_san`. Add targeted cases for each A060 site above, in particular
strings whose decode succeeds on pass 1 (every escape, every UTF-8 length, at every arena capacity around the exact
need) and inputs that end exactly at a member's `"` or `:`. Check that the killable input `"\uD83D\xDE00"` gives
`JSON_E_UTF8` on both.

## Rules
Read-only on every repo and worktree; you may `git clone /var/tmp/src/annas-c` into your cache dir and fetch
`task/A060` into it. Never read `/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees
or other cache dirs. Never write to `/tmp` (`TMPDIR=/var/tmp/cache/V060/tmp`). No git stash. Never kill processes you did
not start. No sleep-poll loops (`gate/tools/job`). 2 CPUs and 8 GiB are shared.

## Deliverable (final message)
Coverage (classes, counts, seed), the sanitizer result, every difference (or "none") with input hex and the A060 site,
and the paths of the harness and logs.
