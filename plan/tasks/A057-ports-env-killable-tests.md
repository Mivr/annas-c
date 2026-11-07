# A057: tests for the 11 killable survivors A053 found (ports fakes, posix_env)

Repo: `Mivr/annas-c`. Branch `task/A057`. **Test-author task**: add cases to the existing files in `ports/tests/` and
`posix_env/tests/` only. No new test target, no BUCK change, no `src/`, no header. Read `AGENTS.md`, `gate/AGENTS.md`,
`ports/fakes.h`, `ports/ports.h`, `posix_env/posix_env.h`, and in the current gate checkout `/var/tmp/src/c-rigor-gate`
(read-only) `docs/mutate.md`. Do not edit `plan/`. Do not move the `gate` submodule.

## Why
A053 (merged as `d75a7ba`) left these survivors and named an input for each through the public headers:

| site | mutants | input and expected result |
|---|---|---|
| `ports/src/fs_fake.c` init loop | 3 | re-init a used fs: init, create, init again, `inject(CREATE, 1, E_IO)`, create gives `E_IO` |
| `fs_fake.c` preload | 3 | `preload(name, {NULL, 5})` and `{NULL, 1}` give false; `{NULL, 0}` gives true with an empty file |
| `fs_fake.c` preload content | 1 | preload 1-byte content `"x"`, then `find` gives `"x"` |
| `fs_fake.c` find | 1 | `ports_fake_fs_find(NULL, name, &c)` gives false |
| `fs_fake.c` temp_content | 1 | `ports_fake_fs_temp_content(NULL, 1, &c)` gives false |
| `ports/src/stream_fake.c` init | 3 | init with an invalid script (an EXPECT step with empty data): init returns false, `mismatch` is true with `{UNEXPECTED_CALL, 0, 0}`, `done` is false |
| `stream_fake.c` mismatch | 2 | make a stream fail, then `mismatch(&s, NULL)` gives true |
| `posix_env/src/env.c` | 1 | env `{"A=1"}`: `get("A")` is found with value `"1"` |
| `env.c` NUL guard | 1 | entry bytes `{'A','\0','B','=','v','\0'}` and lookup name `{'A','\0','B'}` of len 3: not found |

Check every expected result against the header before you pin it. If the header does not promise it, say so and do not pin it.

## Requirements
- Pin exactly what the headers promise: return values, lengths and contents. Tests are graded against a witness implementation.
- Measure in a scratch clone under `/var/tmp/cache/A057/` with the current gate copied over `gate/`: `c_mutants` targets for
  `//ports:ports` and `//posix_env:posix_env` with their tests, on `linux_release`. Report which of the 11 are killed, and by
  assertion (`killed-assert`), not only by a crash.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //ports/... //posix_env/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //ports/... //posix_env/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"; the passing count rises only by your new cases
```
Report the new test ids, the verdict of each of the 11 after, and every deviation.
