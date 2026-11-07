# V056: differential check of A056 (tls_stream triage): old and new give identical results

Repo: `Mivr/annas-c` (read-only for you). **Verification task**: no change to any repo. Everything you write goes in
`/var/tmp/cache/V056/`. Read `AGENTS.md`, `gate/AGENTS.md`, `tls_stream/tls_stream.h`, `ports/ports.h` and
`tls_stream/src/internal.h`, and the existing tests in `tls_stream/tests/` (they show how to build the in-process
TLS test peer `gate//third_party/tls_peer` and the fake transports).

## Context
A056 (branch `task/A056`, commits `bb3ad0e`, `66956ae`, `1243dc4`) changed `tls_stream/src/{bio,heap,sock,tls_stream}.c`:
`wait_ready` folds a passed deadline into the `ETIMEDOUT` path; `heap_calloc` uses `__builtin_mul_overflow`/
`__builtin_add_overflow`; `tls_claim` drops the `mbedtls_platform_set_calloc_free` result check; `wall_now` drops the
`LONG_MAX` clamp; `sock.c` drops dead initialisers, the `snprintf` truncation check for the port and uses designated
initialisers for `hints`; `load_ca` and the `*_init` calls are as before. Claimed behaviour-preserving.

## Method
1. Build OLD (`tls_stream/src` at `e191cba`, the merge base) and NEW (`task/A056`) as two renamed copies (`tls_stream_`->
   `otls_stream_` etc. by sed in your scratch only, verify the rename is pure) linked into one harness with the same
   mbedTLS and deps, through the gate's toolchain and Buck2.
2. Drive both through the public header and the port with the same scripted inputs, and compare every return code,
   every transport call (connect host/port/deadline, every byte sent, read sizes), every clock read, every byte
   delivered to the caller, `tls_stream_heap_used`/`heap_peak`, and the version string. Cover:
   - the existing tests' scenarios (handshake OK, bad cert, wrong host, expired, CA bundle variants), using the in-process
     TLS peer; sessions with reads and writes of every size up to 2 records, and close;
   - transport faults at every call (EINTR, EAGAIN, timeouts, peer close, short writes) and deadlines crossing at every
     clock read;
   - the heap hook directly through `mbedtls_calloc`/`mbedtls_free` while a stream is open: sizes 0..4096, products
     that overflow (`SIZE_MAX/2+1` x 2, `SIZE_MAX` x 1), arena exhaustion and reuse;
   - `unix_seconds()` values 0, 1, 2^31, 2^32, 2^62, 2^63-1, 2^63, UINT64_MAX through a fake clock (report what each
     version does at and above 2^63; a difference there is expected by A056 and must fail closed: name it);
   - the sock layer with an injected `socketpair` fd where the header allows it, no network.
   At least 20,000 seeded random sessions (seed reported). No network except the RE farm; no real hosts.
3. Run the whole harness under `linux_san` for both.

## Rules
Read-only on every repo and worktree; you may `git clone /var/tmp/src/annas-c` into your cache dir and fetch
`task/A056`. Never read `/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or other
cache dirs. Never write to `/tmp` (`TMPDIR=/var/tmp/cache/V056/tmp`). No git stash. Never kill processes you did not
start. No sleep-poll loops (`gate/tools/job`). 2 CPUs and 8 GiB are shared: keep runs bounded.

## Deliverable (final message)
Coverage (scenarios, counts, seed), the sanitizer result, every difference (or "none") with the input and A056 site,
and the paths of the harness and logs.
