# A023: `tls_stream` implementation (implementer task)

Repo: `Mivr/annas-c`. Branch `task/A023`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`ports/ports.h`, the contract `tls_stream/tls_stream.h` (trust, the two seams, the check order, sticky errors,
deadlines, the one-connected-object rule, the heap arena and its sizing), `gate/docs/third-party.md`, and the
mbedTLS 3.6 public headers.

## Allowed paths
`tls_stream/src/*.c`, `tls_stream/src/internal.h` (the file list is fixed by `tls_stream/BUCK`: `tls_stream.c`, `bio.c`,
`heap.c`, `sock.c`, `internal.h`).

## Protected
`tls_stream/tls_stream.h`, `tls_stream/BUCK`, `tls_stream/tests/**` (including the kit), everything else.

## Goal
All 215 tests pass in `release` and `san` with no leaks, within the C rules (no recursion, no malloc of your
own, since mbedTLS gets the caller's arena via `mbedtls_platform_set_calloc_free`; no mutable globals except
where the header documents the process-wide mbedTLS hooks, and then exactly as documented; functions of 50 lines
or fewer, frames of 512 bytes or fewer, every return checked). This module decides whether a download can be
trusted: verification is `VERIFY_REQUIRED`, the host check is on, time comes only from the injected wall clock,
and a transport EOF without close_notify is never EOF. Implement the header, not the tests. If a test
contradicts the header, report it.

## Acceptance
```sh
gate/tools/buck2 build //tls_stream/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //tls_stream/... --target-platforms toolchains//:linux_san --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san
```
Report: the results per test file in both modes, the measured heap peak, and every deviation.
