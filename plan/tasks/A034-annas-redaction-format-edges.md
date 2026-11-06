# A034: annas tests pin redaction character classes and format boundaries (R004 T2, closes S3, S4)

Repo: `Mivr/annas-c`. Branch `task/A034`. **Test-author task**: `annas/tests/**` only (kit headers included), no
`src/`, no public header, no BUCK change unless a new test file needs registering (then only `annas/BUCK`, test
entries only). In flight in parallel: A029 (`annas/src/**`, structure refactor, behaviour unchanged), A033
(`url/tests/**`). Read `AGENTS.md` (and `gate/AGENTS.md`) first. Do not edit `plan/`.

## Why
R004 (`plan/audits/R004-test-adequacy.md`, survivor classes S3 and S4, and "T2"): 17 surviving mutants in
`redact.c` / `msg.c` (unreserved-class boundaries, a pct match with one hex digit right, a 1-byte key, a key at
position 0) and 3 in `name.c:12` (format character class edges) are not killed by any test.

## Requirements
Exactly the T2 list in the R004 plan:
- 1-byte key `"k"`: server error text `"k"` -> `"REDACTED"`; `"kk k"` -> `"REDACTEDREDACTED REDACTED"`.
- A key equal to the first bytes of the server `error` text: the message starts `"401: REDACTED"`.
- Keys containing each byte at and just outside each unreserved-class edge (`@ A Z [ \` a z { / 0 9 :` and `- . _ ~`):
  the server echoes the pct-encoded form and the expected text has it as `REDACTED`; the same text with one hex digit
  changed (e.g. `%2B` -> `%2C`) is left alone.
- Format `"a" "z" "A" "Z" "0" "9"` accepted (file `<md5>.<lower>`); `` "`" "{" "@" "[" "/" ":" `` give
  `E_ARG "format must consist of 1 to 16 ASCII letters or digits"` before any port call.
Every error case uses `kit_check_err` (exact code, message, `http_status`). Every new test must pass on today's
`annas/src`. Use only the public header and the existing kit/fakes; no real keys (the test keys are made up).

## Acceptance (the orchestrator re-runs it)
1. `gate/tools/buck2 build //annas/... --target-platforms toolchains//:linux_release --show-full-output` and the same
   with `linux_san`: every test passes; list the new test ids.
2. In a **scratch clone under `/var/tmp/cache/A034/`** (never edit `src/` in your worktree), apply at least 6 of the
   S3/S4 survivor mutations by hand, one at a time (e.g. a class edge `<=` -> `<` in `redact.c`, the hex-digit
   comparison, the position-0 loop start, each `name.c:12` bound), rebuild `//annas/...` release, and show which new
   test fails for each. Name the file:line of each edit.
Report every deviation.
