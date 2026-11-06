# A035: app tests pin the long-key scrubbing rule (R004 T5, closes P8)

Repo: `Mivr/annas-c`. Branch `task/A035`. **Test-author task**: `app/tests/**` only, no `src/`, no public header, no
BUCK change unless a new test file needs registering (then only `app/BUCK`, test entries only). Read `AGENTS.md`
(and `gate/AGENTS.md`, "Working efficiently") first. Do not edit `plan/`.

## Why
R004 probe P8 applied pass 2 to keys longer than `ANNAS_KEY_MAX` (256) bytes and all 211 app tests still passed.
`app/app.h` (line ~37): "Keys longer than ANNAS_KEY_MAX bytes are not accepted by annas_config_load; for such a key
only pass 1 is applied."

## Requirements
- A made-up key of 300 bytes containing `+` (so `url_pct_encode(key)` differs from the key), reaching the app's
  scrubbing through its public interface (find the path the existing `scrub_utf8_test.c` / `leak_test.c` use; e.g.
  an MCP request id or argument echoed in the reply).
- Text containing the raw key is redacted (pass 1). Text containing only `url_pct_encode(key)` is echoed
  **unchanged** (pass 2 not applied), exactly as app.h says. Pin the full output text.
- Control at the boundary: a 256-byte key with `+` gets both passes (encoded form redacted); a 257-byte key gets
  pass 1 only. Pin the exact boundary app.h implies (`> ANNAS_KEY_MAX`); cite the header line in a comment.
Every new test must pass on today's `app/src`.

## Acceptance (the orchestrator re-runs it)
1. `gate/tools/buck2 build //app/... --target-platforms toolchains//:linux_release --show-full-output`, and `linux_san`:
   every test passes; list the new test ids.
2. Probe P8 in a **scratch clone under `/var/tmp/cache/A035/`** (never edit `src/` in your worktree): make pass 2 apply
   to long keys too (remove the length condition), rebuild `//app/...` release, show which new test fails. Also the
   off-by-one probe (`>` -> `>=` on that length check): show which test fails.
Report every deviation.
