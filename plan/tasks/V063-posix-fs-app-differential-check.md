# V063: differential check of A063 (posix_fs sys seam, app scrubbing and emit): old and new give identical results

Repo: `Mivr/annas-c` (read-only for you). **Verification task**: no change to any repo. Everything you write goes in
`/var/tmp/cache/V063/`. Read `AGENTS.md`, `gate/AGENTS.md`, `posix_fs/posix_fs.h`, `app/app.h`, `annas/annas.h`.

## Context
A063 (branch `task/A063`, commits `0243a40`, `fa16b9d`) changed:
- `posix_fs/src/sys.c`: `fail_errno` returns `-(int64_t)errno` (no `errno == 0` fallback); the `off > INT64_MAX` check in
  the libc `pwrite` and the `len > INT64_MAX` check in `ftruncate` are gone (the header gives the seam `off <= INT64_MAX`);
- `app/src/ops.c`: the scrub passes always write their output (no `has_match` shortcut, no `force_copy`/`in_scratch`);
  `app_emit` and `cli_finish` lost their failure branches (bounds now `_Static_assert`ed: outcome text < 8192 bytes, at most
  8x growth from scrubbing, result fits 64 KiB); `app_utf8_clean` now requires `s[n] == 0` (callers: `call_run` via
  `core_buf`'s NUL, `call_t.buf` now `[APP_ECHO_MAX + 1]` zero-filled);
- `app/src/dotenv.c`: `app_dotenv_parse` is a do-while on `core_str_split_once`.

## Method
Two earlier checks built harnesses you may reuse; copies are in `/var/tmp/cache/V063/posix_fs-harness-from-V054/` (scripted
fake `posix_fs_sys_t`, write trees, seam matrix, id wrap, real-file pass) and `/var/tmp/cache/V063/app-harness-from-V055/`
(CLI, .env, MCP, scrubbing phases, an independent reference model of the two scrub passes). **Build both through the gate
toolchain with `gate/tools/buck2`** (the V055 copy was built with host gcc; port its build to Buck2 `c_binary` targets in
your scratch clone; host compilers are not allowed). OLD = the merge base `721703b`, NEW = `task/A063`, renamed by sed in your
scratch only (verify the rename is pure).
- posix_fs: rerun the V054 classes (seed reported), and add the libc seam directly (`posix_fs_sys_libc`) on real fds in your
  cache dir for the guards A063 removed: `pwrite`/`ftruncate` with offsets and lengths 0, 1, INT64_MAX-1, INT64_MAX,
  INT64_MAX+1, UINT64_MAX. A difference here is expected only for inputs above INT64_MAX (old `-EFBIG`, new whatever the
  kernel returns): report the exact values and confirm no crash and no sanitizer report.
- app: rerun every V055 phase (at least the same counts; seed reported), with the reference model, plus outputs at the new
  bounds: outcome texts of 8191 bytes with maximal scrub growth, and MCP echoes at `APP_ECHO_MAX` and one above.
Run everything in release and under `linux_san`.

## Rules
Read-only on every repo and worktree; you may `git clone /var/tmp/src/annas-c` into your cache dir and fetch `task/A063`.
Never read `/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs. Use
only fake keys. Never write to `/tmp` (`TMPDIR=/var/tmp/cache/V063/tmp`). No git stash. Never kill processes you did not
start. No sleep-poll loops (`gate/tools/job`). 2 CPUs and 8 GiB are shared.

## Deliverable (final message)
Coverage per module (classes, counts, seed), the sanitizer result, every difference (or "none") with input and A063 site,
and the paths of the harness and logs.
