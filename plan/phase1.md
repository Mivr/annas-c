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

## Modules, in order
| Module | Depends on | Level | Notes |
|---|---|---|---|
| `core` | - | L3 | arena (bump allocator over a caller buffer), byte slices, bounded byte builder, checked integer parse |
| `json` | core | L3 | strict RFC 8259 parser (bounded depth, size) into an arena DOM; writer |
| `url` | core | L3 | percent-encoding, query building, base-URL normalisation (spec §1) |
| `fsname` | core | L3 | filename sanitiser (spec §7) |
| `html` | core | L3 | tolerant tokenizer + minimal selector evaluation for the spec §4 table |
| `ports` | core | L2 | `transport`, `fs`, `env`, `clock` interfaces (const function tables) + fakes for tests |
| `annas` | core json url fsname html ports | L3 | pure operations of spec §3 over the ports |
| `curl_transport`, `posix_fs`, `posix_env` | ports | L2 | adapters (libcurl vendored later) |
| `cli`, `mcp` | annas + adapters | L2 | wiring, output formats, JSON-RPC |

## Dev builds
`gate/` is a git submodule of c-rigor-gate (pinned) only for local development; the grader ignores it and the
product's `.buckconfig`, and assembles its own tree.
