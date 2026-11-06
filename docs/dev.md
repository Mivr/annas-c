# Local development

Builds use Buck2 through the gate's pinned wrapper, on the shared RE farm (`forge.internal:9092`). Nothing is
built on the host.

```sh
git submodule update --init gate          # c-rigor-gate pinned at e9645c3
gate/tools/buck2 build //core/...         # release (default)
gate/tools/buck2 build //core/... --target-platforms toolchains//:linux_san
gate/tools/buck2 test  //core/...         # runs the ctest binaries
```

Test verdicts are build artifacts: `<name>_results` is a JSON Lines file (see `gate/docs/ctest.md`).

```sh
gate/tools/buck2 build //core/... --show-output | grep _results   # release
gate/tools/buck2 build //core/... --target-platforms toolchains//:linux_san --show-output | grep _results
tail -1 <path>/results.jsonl    # {"summary":{"total":N,"passed":M}}
```

## Cell layout (deviation from the gate's own `.buckconfig`)
The gate's `.buckconfig` is written for the gate as the root cell and says `gate = root`. As a submodule
it is a nested cell, so this repo's `.buckconfig` names the product cell `annas` and maps the cell name
`root` to `gate/`. Product code still loads `@gate//rules:c.bzl` and targets are still `//core:...`.
The grader assembles its own tree and ignores this file.

## Headers
`c_library` exports headers under `<package>/<file>`, so module headers live directly in the module
directory (`core/arena.h`, included as `"core/arena.h"`) rather than `core/include/core/`.

## Daemon caveat
In the sandbox the Buck2 daemon can miss file changes. If a build seems to use stale sources, run
`gate/tools/buck2 kill` and build again.
