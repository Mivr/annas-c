# A066: rework the app/wire tests that encode the old key contract; tests for annas's killable survivors

Repo: `Mivr/annas-c`. Branch `task/A066`, **based on `task/A062`** (A061's header and tests plus A062's implementation;
the three merge to `main` together). **Test-author task**: edit `app/tests/**`, `wire/tests/**` and `annas/tests/**` only.
No `src/`, no header, no BUCK change unless a new test file must be registered (say so). Read `AGENTS.md`,
`gate/AGENTS.md`, `annas/annas.h` (the KEY CLASS, NAME, DOWNLOAD PATH and REDIRECT rules), `app/app.h`, `wire`'s header.
Use **fake keys only**, all inside the new class (16..256 bytes of `[A-Za-z0-9._~-]`, no `REDACTED`).

## 1. Fourteen tests that encode the old contract
They fail on this branch in `linux_release` and `linux_san` because they use short, out-of-class or percent-encoded keys,
or expect a file named after the key instead of `REDACTED.*`:
- `app/`: `leak/*` (4 ids), `scratch/cli_fit_is_judged_after_scrubbing`, `survivor/*` (2 ids)
- `wire/`: `wire_e2e_mcp/the_title_is_the_key`, `wire_e2e_secret/*` (4 ids), `wire_e2e/*` (2 ids)
Run the build first to get the exact ids. Rework each to the new contract with the same intent; keep every id (a removed
expected id needs a signed test-removal approval, so change, don't remove). Where an old case is now impossible (e.g. a
percent-encoded key), replace it with the new promise it protects (e.g. an out-of-class key is E_CONFIG with no port
call). List each id with what changed.

## 2. Tests for annas's killable survivors
`/var/tmp/cache/A066/a062-findings.md` section B lists the inputs A062 found (`api_send` with a hand-built bad `base`,
`op_begin` with `timeout_ms = 0`, `dl_reject` with a non-200 body of exactly the challenge-prefix size and a truncated
500 body, `dl_transfer` discard after success). Pin each through `annas.h` only, checking the result against the
header before you pin it. Section A is for the orchestrator; ignore it.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"
```
Also measure `//annas:annas` mutants in a scratch clone under `/var/tmp/cache/A066/` with the current gate over `gate/`
(`c_mutants` with all annas tests, `linux_release`, judged by `rigor mutants`): the section B survivors must be killed by
assertion. Report the reworked ids, the new ids (they need a gate config follow-up), the mutant result, and deviations.

## Rules
Use `TMPDIR=/var/tmp/cache/A066/tmp`; never write to `/tmp`. Build only with `gate/tools/buck2`. No `git stash`, no merge of
`main`, no push. Never read `/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or
other cache dirs; never look for a real key. Never kill processes you did not start. No sleep-poll loops
(`gate/tools/job`). 2 CPUs and 8 GiB are shared.
