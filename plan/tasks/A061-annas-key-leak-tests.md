# A061: pin the key-redaction contract where both old and new annas leak (header + tests)

Repo: `Mivr/annas-c`. Branch `task/A061`. **Test-author task**: edit `annas/annas.h` (contract text only, see below) and
`annas/tests/**` only. No `src/`, no BUCK change unless a new test file needs registering (say so). Read `AGENTS.md`,
`gate/AGENTS.md`, `annas/annas.h`, `url/url.h`, `fsname/fsname.h`, and in `/var/tmp/src/c-rigor-gate` (read-only)
`docs/mutate.md`. Do not edit `plan/`. Do not move the `gate` submodule. Use **fake keys only**.

## Why
V051 (a differential check of A051, which changed nothing here) found three ways the configured key leaks, in both the
old and the new implementation. `annas.h` lines 12-25 promise: "the member key (raw or percent-encoded) never appears in
any message or result string, whatever the server, the title or the environment echoes. The key is sent only in the API
request line to the configured base host; it is never part of any other request (download hosts, redirects)."

| # | Leak today | Required behaviour (orchestrator ruling) |
|---|---|---|
| a1 | A title containing the key (raw or `pct_encode(key)`) puts it into the result `name`/`path`, the fs commit name and `annas_format_downloaded` | Before `fsname_sanitize`, replace every non-overlapping occurrence of the key, then of `pct_encode(key)`, in the title with `REDACTED` (the same rule as messages). The commit name and every result string then never contain the key. |
| a2 | An `ANNAS_DOWNLOAD_PATH` containing the key (raw or percent-encoded) appears in the result `path` | `ANNAS_E_CONFIG` with an exact message (you write it, e.g. `download path contains the key`), checked with the other config checks, before any port call. |
| b | A redirect `Location` containing the key is sent in the request line to the redirect host | Before following a redirect, if the resolved target (host, path or query) contains the key or `pct_encode(key)`: `ANNAS_E_HTTP` with an exact message (e.g. `redirect contains the key`), no connection to the new host, nothing created in the fs. |
| c | Keys with control bytes are not redacted (step 1 turns them into `?` before step 2 matches), and lower-case percent-encoding (`%1e`) escapes redaction and the key checks | Remove the class: a key must be 1..`<existing max>` bytes of `[A-Za-z0-9._~-]` (URL-unreserved, so `pct_encode(key) == key` and no control byte is possible); anything else is `ANNAS_E_CONFIG` with an exact message. Check what the header allows today and keep its length limit. |

## Header change
Amend the `Messages` paragraph and the config, download and redirect sections of `annas.h` to state the four rules above
exactly (codes, messages, precedence relative to the existing checks, and that no port call happens). Keep every other
promise unchanged. Your header diff is the new contract; make it self-contained.

## Tests
For each row: the leak input (several key lengths, key at the start/middle/end, twice, overlapping candidates, raw and
percent-encoded where it applies) and assertions through `annas.h` only: return code, exact message, result struct
unchanged on failure, exact commit name on success (a1), and through the strict stream fake that no request to a
redirect host was made (b). Also pin that valid keys at the length limit still work, and every key byte class outside
the allowed set gives `ANNAS_E_CONFIG` (c). New tests fail on the current `main` code (they are red until A062).
List which ones fail on main and why.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //annas/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //annas/... --target-platforms toolchains//:linux_san --show-full-output
# existing tests pass; your new tests fail only for the four leak rules (report each failing id and its cause)
```
Report the header diff, the new test ids (they need a gate config follow-up), which fail on main, and every deviation.

## Rules
Use `TMPDIR=/var/tmp/cache/A061/tmp`; never write to `/tmp`. No `git stash`, no merge of `main`, no push. Never read
`/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs; never look for
a real key. Never kill processes you did not start. No sleep-poll loops (`gate/tools/job`). 2 CPUs and 8 GiB are shared.
