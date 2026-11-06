# annas-c phase 1 plan

Graded by [c-rigor-gate](https://github.com/Mivr/c-rigor-gate). Spec: [docs/spec.md](../docs/spec.md).

## Two-context rule (every module)
1. **Test author** (task `A<NNN>-<module>-tests`): writes the module's public header `<module>/include/<module>/*.h`,
   its `BUCK`, and its tests `<module>/tests/*_test.c` from the spec. Implementation files contain only stubs
   that make the tests compile and **fail**. The tests are the specification in executable form.
2. **Implementer** (task `A<NNN>-<module>-impl`): writes `<module>/src/*.c` only. Headers, BUCK and tests are
   protected paths in that task's contract, so the implementer can't weaken them. If a test is wrong, the
   implementer reports it and the orchestrator issues a test-author fix task.
3. Hidden tests (private repo, phase 1 later) and mutation testing then check that the visible tests were
   neither too weak nor overfitted.

## Modules, in order (v1 = API only, see spec §0)
| Module | Depends on | Level | Status / notes |
|---|---|---|---|
| `core` | - | L3 | merged (provisional) |
| `json` | core | L3 | merged (provisional) |
| `url` | core | L3 | merged (provisional); only base, fast_download builder, redaction and md5 are used in v1 |
| `fsname` | core | L3 | merged (provisional) |
| `http` | core | L3 | HTTP/1.1 response parser + request writer (GET only; status line, headers, Content-Length, chunked, redirects <= 5), over a byte-stream port |
| `ports` | core | L2 | `stream` (connect/read/write/close), `fs` (temp file, write, rename, remove in the download dir), `env`, `clock` as const function tables + fakes |
| `annas` | core json url fsname http ports | L3 | pure v1 operations: fast_download call, response mapping, streamed download to temp + rename |
| `tls_stream` | ports | L2 | adapter: TLS client stream on mbedTLS 3.6 LTS (vendored, pinned by sha256, built hermetically; system CA bundle path configurable) |
| `posix_fs`, `posix_env`, `posix_clock` | ports | L2 | adapters over a syscall seam (fault-injecting fake + real libc); tests A016 |
| `app` | core json annas ports | L2 | pure CLI + MCP core: tests merged (A014), impl A015 |
| `cli`, `mcp` binaries | app + adapters | L1 | thin `main`s: wiring only |
| ~~`html`~~ | | | deferred to v2 (search via a local index of the published dumps) |

## Dev builds
`gate/` is a git submodule of c-rigor-gate (pinned) only for local development; the grader ignores it and the
product's `.buckconfig`, and assembles its own tree.
