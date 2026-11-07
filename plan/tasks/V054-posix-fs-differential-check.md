# V054: differential check of A054 (posix_fs refactor): old and new give identical results

Repo: `Mivr/annas-c` (read-only for you). **Verification task**: no change to any repo. Everything you write goes in
`/var/tmp/cache/V054/`. Read `AGENTS.md`, `gate/AGENTS.md`, `ports/ports.h` (the fs port contract), `posix_fs/posix_fs.h`
(including the `posix_fs_sys_t` seam) and `posix_fs/src/internal.h`.

## Context
A054 (branch `task/A054`, commits `037ae0b` to `f9c0535`) rewrote parts of `posix_fs/src/{fs,io,sys}.c` to remove redundant
code. It claims to be behaviour-preserving. The riskiest change: `write_all` now advances `slot->size` per chunk and
`posix_fs_write` rolls back by comparing sizes (truncate to the old size, mark the slot broken if that truncate fails,
restore `size`). Also: one shared slot scan, a branchless id wrap (`next_id % UINT32_MAX + 1`), no `dir_fd = -1` resets,
`memcpy` of the committed name, and no `snprintf` check in `sys_link_tmp`. The unit tests pass, but they are the tests that
left those mutants alive, so they cannot be the proof. You give an independent one.

## Method
1. In a scratch clone under `/var/tmp/cache/V054/`, build two static libraries of `//posix_fs:posix_fs` and its deps: OLD from
   `8e81008` (the merge base, so the deps are identical) and NEW from `task/A054`. Use the gate's hermetic toolchain through
   Buck2. Make the symbols distinct (`llvm-objcopy --prefix-symbols=old_`/`new_`, or `-D` renames in the scratch only), so
   one harness binary links both.
2. Write a scratch harness (C, through `posix_fs/posix_fs.h` and the port only) that drives both versions through
   `posix_fs_open_sys` with **your own scripted fake `posix_fs_sys_t`** (no real files needed for this part), replays the
   same script to both, and compares everything observable: every return code, every handle, every call the library makes
   to the seam (function, fd, offset, length, bytes, name, in order), and the state seen by later calls.
   Seam scripts must cover:
   - `pwrite` returning: the full length, every short count 1..len-1, 0, more than asked, `-EINTR`, `-EAGAIN`, `-ENOSPC`,
     `-EFBIG`, `-EIO`, at every chunk of a multi-chunk write;
   - `ftruncate` after a failed write succeeding, failing, and `-EINTR`; then every later call on that file (write, commit,
     discard) and on other files;
   - sizes near `INT64_MAX` (writes that reach it exactly, one past it, and an empty write there);
   - `open_dir`, `open_tmp`, `fsync`, `link_tmp`, `close` returning each of success, 0, `-EINTR`, and errors;
   - every slot count from empty to full and one past, create/discard/commit in random orders, handle reuse, and the id wrap
     (start `next_id` near `UINT32_MAX` by creating and discarding; if that is too slow, say what bound you used);
   - double close, use after close, reopen after close, and every NULL argument;
   - committed names of every length 0 to the limit and one past, `.`, `..`, `.a`, names with `/` and NUL.
   Then at least 50,000 seeded random call sequences with random seam results, reported with the seed.
3. Also run a short real-file pass for both versions in your cache dir (`posix_fs_open` on a scratch directory under
   `/var/tmp/cache/V054/`): write, partial failure is not reachable here, commit and discard, and compare the files.
4. Run the whole harness under the gate's `linux_san` flags (ASan, UBSan) for both versions.
5. Any difference is a finding: give the call sequence, seam script, the old and new outputs, and the A054 site responsible.

## Rules
Read-only on every repo and worktree except that you may `git clone` `/var/tmp/src/annas-c` into your cache dir and
`git fetch` `task/A054` from `/var/tmp/src/annas-c` into it. Never read `/var/tmp/.config`, `/var/tmp/.claude*`,
`/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs. No network except the RE farm. Never kill processes you
did not start. No sleep-poll loops (`gate/tools/job`). Never write to `/tmp`; use `TMPDIR=/var/tmp/cache/V054/tmp`. Do not
use `git stash`. 2 CPUs and 8 GiB are shared: keep runs bounded.

## Deliverable (final message)
The harness's coverage (functions, seam scripts, counts, seed), the sanitizer result, every difference found (or "none"),
and the paths of the harness and its logs.
