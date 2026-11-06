# A043: bump the `gate` submodule to c-rigor-gate d58247a

Repo: `Mivr/annas-c`. Branch `task/A043`. Read `AGENTS.md` and `docs/dev.md`.
The `gate` submodule is pinned at `67c2230` (T012). That gate has no `rigor structure`, no typed indirect-call
targets (T063), and no `tools/job`, so agents in this repo cannot run the current checks in-tree (A041 and A042 had
to borrow `/var/tmp/src/c-rigor-gate`).

## Steps
1. Point `gate` at `d58247a`. Fetch from the local clone, with no network:
   `git -C gate fetch /var/tmp/src/c-rigor-gate main && git -C gate checkout d58247a`. Then commit the new
   gitlink.
2. Fix anything in this repo that the newer gate needs, and nothing else: `.buckconfig` cells, toolchain names,
   `docs/dev.md` commands. List each change in the report with the gate commit that requires it.
3. No product source or test changes. If a build or test breaks because the newer gate is stricter, stop and report it
   (file, rule, gate commit). Do not fix it here.

## Allowed paths
`gate` (the gitlink only), `.buckconfig`, `.buckroot`, `toolchains/**` if present at the repo root, `docs/dev.md`,
`AGENTS.md` (command examples only).

## Acceptance
```sh
gate/tools/buck2 build //... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass": 1616 per platform, as on main ec9d3ca
gate/tools/job --help >/dev/null && gate/rigor structure --help >/dev/null && echo tools-ok
```
Report the gitlink change, every other file change with its reason, the test counts, and every deviation.
