# A033: url tests pin the NULL-base rule for every builder (R004 T3, closes P1)

Repo: `Mivr/annas-c`. Branch `task/A033`. **Test-author task**: `url/tests/**` only, no `src/`, no public header, no
BUCK change (`url_build_test.c` is already registered). In flight in parallel: A029 (`annas/src/**`), A034
(`annas/tests/**`). Nothing here overlaps. Read `AGENTS.md` (and `gate/AGENTS.md`) first. Do not edit `plan/`.

## Why
R004 (`plan/audits/R004-test-adequacy.md`, probe P1 and "T3") deleted the `bad_ptr(base) ||` check in
`url_build_fast_download` and all 75 url tests still passed: the call returned `URL_E_INVALID` instead of
`URL_E_ARG`. `url.h` says `ptr == NULL` with `len > 0` is `URL_E_ARG` and that ARG takes precedence over INVALID.
Only `url_build_search` has a NULL-base test.

## Requirements
For each of `url_build_fast_download`, `url_build_md5_page`, `url_build_scidb_path`, `url_build_scidb_download`:
1. `base = {NULL, 5}` with otherwise valid arguments returns `URL_E_ARG`, and the output buffer is unchanged
   (fill it with a known pattern first and compare every byte, including past the would-be end).
2. `base = {NULL, 5}` together with an invalid md5 (or doi, for the scidb builders) still returns `URL_E_ARG`
   (precedence).
3. `base = {NULL, 0}` follows whatever `url.h` says for an empty base; read the header and pin it exactly (cite the
   header line in a comment). If the header is silent, do not add this case; say so in your report.
Tests go through `url.h` only. Every new test must pass on today's `url/src`.

## Acceptance (the orchestrator re-runs it)
1. `gate/tools/buck2 build //url/... --target-platforms toolchains//:linux_release --show-full-output` and the same
   with `linux_san`: every test passes; list the new test ids.
2. Probe P1, in a **scratch clone under `/var/tmp/cache/A033/`** (never edit `src/` in your worktree): for each of the
   four builders separately, remove its `bad_ptr(base) ||` (or equivalent NULL check of `base`), rebuild
   `//url/...` release, and show which of your new tests fail. Each of the four removals must fail at least one test.
Report every deviation.
