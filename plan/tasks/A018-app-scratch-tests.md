# A018: `app` contract fix: an explicit scratch workspace, and tests for the paths A015 found untested (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A018`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§1, §3.3, §5, §6), `core/buf.h`, `annas/annas.h`, `app/app.h`, `app/tests/**`.

## Why
The A015 implementer reported a gap in the header. `annas_error_t` (about 0.8 KB) and `annas_download_result_t`
(about 7 KB) don't fit in a 512-byte frame, and `annas_ctx_init` consumes the whole `ANNAS_WORKSPACE_MIN`. That
leaves `app_cli_run` without memory for its job and texts. To work around this, the implementation borrows the
spare capacity of the caller's `out`/`errout` buffers by reading `core_buf_t` fields that `core/buf.h` documents
as opaque. That is a layering violation, and the contract has to remove the need for it.

The implementer also listed behaviour no test pins:
- key scrubbing when the percent-encoded key overlaps an already-replaced region (the header says two
  sequential passes);
- invalid UTF-8 from server text or a cut name in MCP replies (replaced with `?` before JSON escaping);
- partial `out` content after an overflow;
- the defensive paths for an arena too small for the reply or the job;
- a missing env port.

## Allowed paths
`app/app.h`, `app/tests/**`, `app/BUCK` (only if a test file is added), and the stubs only where the header
change forces a signature change. Witness and scratch under `/var/tmp/cache/A018/` only.

## Changes
1. `app_deps_t` gains an explicit scratch region for the app itself (pointer + size, 16-byte aligned), separate
   from annas's `workspace`, with a public `APP_SCRATCH_MIN` sized so a download and an MCP call fit with margin.
   Too small: the documented internal error. Nothing in `app` may read `core_buf_t` internals: output only through
   `core/buf.h` functions. Add a test that fails if output depends on the spare capacity of `out`/`errout`. For
   example, give exactly-sized output buffers (content length + 1) and expect success when scratch is enough.
2. Decide and document the scrubbing semantics (keep "two sequential passes" or change to "one pass", with a
   reason) and pin it with a test where the two differ.
3. Pin the remaining behaviours listed above with exact-bytes tests. Pin the overflow contract precisely: what
   `out` holds after an overflow (all-or-nothing is preferred, matching `core_buf`).
4. Keep all existing 178 tests unless the header change forces an edit. List every edited test and why.

## Acceptance
The witness (in `/var/tmp/cache/A018/`, built with the committed BUCK) passes everything in `release` and `san`.
The current main implementation must fail the new tests, which proves they pin something. Report: the header diff,
the new tests, the results of the current-main implementation against the new tests, and the witness summaries.
Then an implementer task (A019) reworks `app/src` against it.
