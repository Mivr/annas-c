# A069: tls_stream wall time: fail closed above year 9999 (fix on task/A056)

Repo: `Mivr/annas-c`. **Work on branch `task/A056`** (worktree `/var/tmp/wt/annas-c/A056`), on top of `1243dc4`.
**Implementer + test task** for `tls_stream` only: `tls_stream/src/**`, one new test file in `tls_stream/tests/` (or
cases in an existing one; say which) and its BUCK registration, and the one header comment named below. Read
`AGENTS.md`, `gate/AGENTS.md`, `tls_stream/tls_stream.h`.

## Why (verified by the differential check V056)
A056 replaced `v = secs > LONG_MAX ? LONG_MAX : (mbedtls_time_t)secs` in `wall_now` with a plain cast. A wall seam
(caller-supplied: exception 2 of the owner rule) returning a value >= 2^63 now reaches mbedTLS as a negative `time_t`;
mbedTLS 3.6.7 `x509.c` then shifts a negative year (`year << 9`): undefined behaviour (UBSan aborts), and in release
builds some values (for example `unix_seconds` = 18446479356592516588) make an expired or not-yet-valid certificate
**validate**. Both the old and the new version also accept certificates for absurd positive times whose year aliases
into the certificate window modulo 2^23 (for example 264720682970604): a pre-existing mbedTLS year-arithmetic hole.

## Change
- In `wall_now`: a `unix_seconds` result above **253402300799** (9999-12-31T23:59:59Z) is treated exactly like 0
  ("unknown"): the trampoline returns 0, so every certificate is "not yet valid" and the handshake fails closed with
  `PORTS_E_TLS`. One comparison with a named constant; no other change.
- Header: extend the seam-2 comment: "A value after 9999-12-31T23:59:59Z (253402300799) is treated as unknown (0)."
  and the matching bullet at the top of the header. No other header change.
- Tests through the header (existing tls_stream test fixtures: in-process peer, valid / expired / not-yet-valid
  certificates): `unix_seconds` = 253402300799 with a certificate valid then (if a fixture allows; otherwise show it is
  accepted when the certificate window contains it, or say why not testable), 253402300800, 264720682970604,
  2^63 - 1, 2^63, 18446479356592516588, 2^64 - 1: every value above the bound gives `PORTS_E_TLS` on every fixture,
  and no value reaches undefined behaviour under `linux_san`.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //tls_stream/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //tls_stream/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"
```
Measure mutants for `//tls_stream:tls_stream` as A056 did (scratch clone under `/var/tmp/cache/A069/`, current gate):
the new comparison must have no surviving mutant. Commit on `task/A056`. Report the change, the tests, the mutant
result, and every deviation.

## Rules
Use `TMPDIR=/var/tmp/cache/A069/tmp`; never write to `/tmp`. Build only with `gate/tools/buck2`, never host compilers.
No `git stash`, no merge of `main`, no push. Never read `/var/tmp/.config`, `/var/tmp/.claude*`,
`/var/tmp/src/c-rigor-hidden`, other worktrees (except your `A056` worktree) or other cache dirs. Fake keys only. Never
kill processes you did not start; never `buck2 kill` outside your scratch. No sleep-poll loops (`gate/tools/job`).
2 CPUs, 8 GiB shared.
