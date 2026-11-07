# V059: differential check of A059 (core, http, posix_env, posix_clock, wire): old and new give identical results

Repo: `Mivr/annas-c` (read-only for you). **Verification task**: no change to any repo. Everything you write goes in
`/var/tmp/cache/V059/`. Read `AGENTS.md`, `gate/AGENTS.md`, `core/*.h`, `http/http.h`, `posix_env/posix_env.h`,
`posix_clock/posix_clock.h`.

## Context
A059 (branch `task/A059`) changed, claiming identical behaviour through the headers:
- `core/src/arena.c` `core_arena_alloc`: the fit test is now `size > remaining || pad > remaining - size`;
- `core/src/buf.c` `core_buf_truncate`: no `cap == 0` test;
- `core/src/str.c` `core_str_trim`: one forward scan;
- `http/src/header.c` `is_tchar`: one `strchr` literal (reached through the header parser and the challenge/redirect paths);
- `http/src/url.c` `http_url_split`: the authority start found with `core_str_split_once(url, "://")`;
- `posix_env/src/env.c`: no `found` temporary; `posix_clock/src/sys.c`, `wire/src/sys.c`: the errno fallback removed
  (POSIX guarantees errno on failure; only reachable through the real libc, so check the fake-seam paths only).

## Method
Build OLD (the merge base `d0dd534`) and NEW (`task/A059`) copies of the changed modules with renamed symbols (sed in your
scratch only; verify the rename is pure), linked into one harness through the gate toolchain and Buck2
(`gate/tools/buck2`, never host compilers). Compare every return value, output slice (as offset into the input),
buffer bytes and state:
- `core_str_trim`: every string of length 0..8 over {space, \t, \n, \r, \v, \f, 'a', 0x00, 0xff}, plus long random ones;
- `core_arena_alloc`: every (cap 0..96, used 0..cap, size 0..100 and SIZE_MAX-k, align 1,2,4,8,16,32 and invalid) on
  base addresses with every low-bit alignment 0..31, plus sequences;
- `core_buf_truncate`: every (cap, len, new len) up to 16, NULL, and buffers from `core_buf_init` with NULL storage;
- `http_url_split`: every URL from `http/tests` literals, plus generated `https://` URLs with every authority shape
  (userinfo, IPv6, ports 0..65536, empty host), missing '/', '?'/'#' before '/', other schemes and case variants, and
  every truncation of each;
- header parsing: every byte value as each position of a header name and value through the public parser API;
- `posix_env` lookups over generated env arrays (names with '=' and NUL, empty values, duplicates).
At least 1,000,000 cases in total, seed reported. Run under `linux_san` too.

## Rules
Read-only on every repo and worktree; you may `git clone /var/tmp/src/annas-c` into your cache dir and fetch
`task/A059`. Never read `/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or other
cache dirs. Never write to `/tmp` (`TMPDIR=/var/tmp/cache/V059/tmp`). No git stash. Never kill processes you did not
start. No sleep-poll loops (`gate/tools/job`). 2 CPUs and 8 GiB are shared.

## Deliverable (final message)
Coverage per function (classes, counts, seed), the sanitizer result, every difference (or "none") with input and site,
and the paths of the harness and logs.
