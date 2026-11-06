# A016: POSIX adapters `posix_fs`, `posix_env`, `posix_clock`: headers, BUCK, tests, failing stubs (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A016`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§0, §1, §3.3), `core/*.h`, and above all `ports/ports.h`: these adapters implement its fs, env
and clock contracts exactly. Use `ports/fakes.h` only as a model of how consumers call a port.

## Allowed paths
`posix_fs/**`, `posix_env/**`, `posix_clock/**` (three modules, each with its own `BUCK`, header in the module
directory as in the other modules, `src/`, `tests/`). Witness and scratch under `/var/tmp/cache/A016/` only.

## Design (required)
- **Syscall seam.** Each adapter does its OS calls through a small `const` table of function pointers
  (`posix_sys_t`-style: only the calls it needs, such as `openat`, `write`, `linkat`, `renameat2`, `unlinkat`,
  `fsync`, `close`, `clock_gettime`), with a public constant for the real libc table. Tests run every adapter
  twice: against a **fault-injecting fake table** (fail the Nth call with a chosen errno: `EINTR`, `EIO`,
  `ENOSPC`, `EEXIST`, `EAGAIN`, short writes) and against the **real table** in a fresh directory under
  `$TMPDIR` (or the test's cwd if `TMPDIR` is unset). That's how mutation testing at L2 can reach the error
  paths.
- **posix_fs**: `posix_fs_open(&fs, dir_path)` / `posix_fs_close(&fs)` take the caller's storage (no malloc,
  fixed number of temp slots, stated in the header), hold a directory fd, and give a `ports_fs_t`.
  Temp files are invisible by name (`O_TMPFILE` when available, otherwise a dot-prefixed random name created
  `O_EXCL`; specify which, and what happens to leftovers). `commit` is atomic and never overwrites
  (`linkat` from the tmpfile, or `renameat2(RENAME_NOREPLACE)`), fsyncs the file before it becomes visible,
  and maps errno to `ports_fs_err_t` per the port contract. Every write is all-or-nothing as the port requires.
  Retry `EINTR`, and loop on short writes.
- **posix_env**: over a caller-supplied `char *const *envp` snapshot (from `main`), with no `getenv` and no
  global `environ`. Semantics exactly as `ports_env_t` (first match wins for duplicates; say so in the header).
- **posix_clock**: `CLOCK_MONOTONIC` in ms. Overflow-safe conversion, and never decreasing even if the seam
  returns a smaller value (clamp, documented).

## Tests
Port-contract tests first: for each adapter, a shared table of contract cases from `ports.h`, run against the
real table. Then the error paths through the fake table: errno-to-port-error mapping, `EINTR` retry, short
writes, a commit failure leaving the temp file valid, a discard after a failed commit, an exhausted slot pool,
stale handles, and an invalid final name (each rule in `ports.h`). Every test must leave no files behind (check
the directory listing at the end). Positive controls for every rejection.

## Stubs and acceptance
As A001: stubs compile under the sealed flags and the tests fail. Report the API, tests per file, the stub
summaries and the witness summaries in `release` and `san`, plus any place where `ports.h` was ambiguous.
