# A027: `wire` implementation (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A027`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§1, §2, §5, §6), the headers of `core`, `ports`, `app`, `tls_stream`, `posix_fs`, `posix_env` and
`posix_clock`, and the contract `wire/wire.h` (the seams, the order of work, `.env` and CA rules, the MCP loop framing,
the error texts and exit codes, lazy download-dir opening, and memory).

## Allowed paths
`wire/src/*.c`, `wire/src/internal.h` (the file list is fixed by `wire/BUCK`: `config.c`, `io.c`, `lazyfs.c`, `loop.c`,
`run.c`, `sys.c`, `internal.h`).

## Protected
`wire/wire.h`, `wire/BUCK`, `wire/tests/**`, `bin/**`, everything else.

## Goal
All 135 `wire` tests pass in `release` and `san`, and `//bin/...` builds, within the C rules (no recursion, no
malloc, no mutable globals or statics, functions of 50 lines or fewer, frames of 512 bytes or fewer, every return
checked). `wire` is the composition root's logic: it decides which CA file is trusted, which key is used, and
which directory is written. The key must never appear in output. Implement the header, not the tests. If a test
contradicts the header, report it.

## Acceptance
```sh
gate/tools/buck2 build //wire/... //bin/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //wire/... //bin/... --target-platforms toolchains//:linux_san --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san
```
Also run the release binaries by hand, offline only: `annas-c --version`, `annas-c --help`, a usage error,
`printf '<initialize>\n<tools/list>\n' | annas-c-mcp`. Report the outputs and exit codes. Never connect to any
network host, and never use a real key.
