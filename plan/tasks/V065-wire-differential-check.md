# V065: differential check of A065 (wire survivor triage): old and new give identical results

Repo: `Mivr/annas-c` (read-only for you). **Verification task**: no change to any repo. Everything you write goes in
`/var/tmp/cache/V065/`. Read `AGENTS.md`, `gate/AGENTS.md`, `wire/wire.h`, `app/app.h`, `posix_fs/posix_fs.h`,
`ports/ports.h`, `tls_stream/tls_stream.h`.

## Context
A065 (branch `task/A065`, merge base `25953c1`) changed only `wire/src/*.c`:
- `config.c`: `wire_env_get` no longer zeroes the value; the `denv.count = 0` resets, the `ca_read` count > cap+1 check
  and the `tls_stream_sock_init` result check are gone.
- `io.c`: the EINTR counter reset after progress is replaced by a per-call `write_retry` (still "at most 100 in a row").
- `sys.c`: the `clock_gettime` failure and `tv_sec < 0` branch are gone; `got < cap` became `got != cap`.
- `lazyfs.c`: E_ARG / E_IO guards and `return false` paths removed; `wire_lazyfs_init` calls `posix_fs_open_sys` with a
  NULL path to leave the object closed; `lazy_open` is one expression.
- `loop.c`: the line is held in a `core_buf` (no `LINE_CAP` bound); an over-long line reaches app as
  `APP_MCP_LINE_MAX + 1` bytes; the post-reply overflow check and two `l > 0` guards are gone.
- `run.c`: `emit` guards, the `fail_text` strlen loop, the `have_env` flag, `ca_len` initialiser, `argv[1]` check and
  the `denv.count` / `cmd.kind` resets are gone.

## Method
OLD = the merge base, NEW = `task/A065`, each `wire` copy renamed by sed in your scratch clone only (verify the rename is
pure: reverse-sed reproduces `git show`). One harness links both through the gate toolchain (`gate/tools/buck2`
`c_binary` targets in your scratch clone; host compilers are not allowed) and drives `wire_run` (and the internal entry
points both copies share, through the renamed internal headers) with scripted fake seams (fs, env, clock, socket/tls as
the wire tests do). Compare every observable: exit code, every stdout/stderr byte, every seam call and its arguments, fs
effects, and the final state the header promises (for example `m->tls` all zero after deinit). Cover at least:
- CLI and MCP modes over a corpus of requests (empty line, whitespace-only, lines of `APP_MCP_LINE_MAX - 1`, `MAX`,
  `MAX + 1`, `2 * MAX`, no trailing newline, CRLF, many lines), seeded random lines (report the seed);
- write paths: short writes, EINTR runs of 1, 99, 100, 101 interleaved with progress, write errors, an over-reporting seam;
- env: missing / empty / present key, bad `.env`, `.env` with a NUL byte, download paths of 255, 256 and 257 bytes;
- CA file: 0 bytes, cap - 1, cap, cap + 1 and an over-reporting `read_file`;
- clock: normal, 0 and large values; reads short of cap from a FIFO-like seam;
- lazy fs: create_temp / write / commit / discard in every order including after a failed open, and with a closed object.
A difference is acceptable only for an input outside the header contract (say which clause); report every one with the
exact input, both outputs, and the A065 site. Run everything in release and under `linux_san` (no sanitizer report).

## Rules
Read-only on every repo and worktree; you may `git clone /var/tmp/src/annas-c` into your cache dir and fetch `task/A065`
(init the `gate` submodule from `/var/tmp/src/c-rigor-gate`). Never read `/var/tmp/.config`, `/var/tmp/.claude*`,
`/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs. Use only fake keys. Never write to `/tmp`
(`TMPDIR=/var/tmp/cache/V065/tmp`). No git stash. Never kill processes you did not start; never `buck2 kill` outside your
scratch. No sleep-poll loops (`gate/tools/job`). 2 CPUs and 8 GiB are shared.

## Deliverable (final message)
Coverage (classes, counts, seed), the sanitizer result, every difference (or "none") with input and A065 site, and the paths
of the harness and logs.
