# A017: `tls_stream` adapter: a `ports_stream_t` over TLS on mbedTLS: header, BUCK, tests, failing stubs (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A017`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§0, §1, §3.3), `core/*.h`, `ports/ports.h` (the stream contract this adapter implements exactly),
`gate/docs/third-party.md`, `gate/third_party/tls_peer/tls_peer.h` (the in-memory TLS server for tests and its
PKI), and the mbedTLS public headers under the gate's pinned tarball as the build exposes them.

## Allowed paths
`tls_stream/**` (header in the module directory, `BUCK`, `src/`, `tests/`). Witness and scratch under
`/var/tmp/cache/A017/` only.

## Design (required)
- Two seams, both const function tables plus a ctx, so that tests run with no network:
  1. a **transport** seam: resolve and connect to host:port with a deadline, then send, recv and close bytes,
     with errno-style results. The production table (sockets: `getaddrinfo`, a non-blocking `connect` + `poll`
     with the remaining deadline, `send`/`recv` with `MSG_NOSIGNAL`) is part of this module and is tested only
     through a fault-injecting fake table;
  2. the **clock** as `ports_clock_t`, for deadlines.
- TLS: mbedTLS client from `gate//third_party/mbedtls:mbedtls` (the only allowed dep label). TLS 1.2 + 1.3,
  full certificate verification (`VERIFY_REQUIRED`), SNI and host-name check against the connect host, the CA
  bundle from a caller-given PEM buffer (the binary later loads the system bundle path; this module does no
  file IO), and the time from a caller-supplied `ports_clock_t`-like wall-clock seam passed into
  `mbedtls_platform_set_time` or the X.509 time check (document how; no ambient `time()`). All memory is the
  caller's: the adapter object holds the mbedTLS contexts in caller storage, with no malloc of its own. If mbedTLS
  itself needs heap, say so and use `mbedtls_platform_set_calloc_free` with a caller-provided fixed arena,
  sized and documented.
- Error mapping, exactly per `ports.h`: resolution, connect and transport failures map to `PORTS_E_NET`, the
  deadline to `PORTS_E_TIMEOUT`, any handshake or verify failure to `PORTS_E_TLS`, and not open to
  `PORTS_E_CLOSED`. **A clean close_notify is EOF (read OK, n == 0). A transport EOF without close_notify is
  `PORTS_E_NET` (truncation), never EOF.** T012 found that mbedTLS reports these two differently, and folding
  them together would turn truncated downloads into "complete" files.
- `connect` performs the full handshake before returning OK. `write` sends all bytes or fails. `close` sends
  close_notify (best effort) and releases the connection; the object is reusable for a new connect.

## Tests (with `gate//third_party/tls_peer:tls_peer` as the only server; transport fake = an in-memory pipe to the peer)
For TLS 1.3 and 1.2: the handshake, then a request/response exchange, bulk data both ways, and chunked delivery
at every split point of the first records. Every identity in the PKI table with its expected outcome (expired,
not yet valid, wrong host, unknown CA, other CA trusted, chain with intermediate). SNI and the host check
(asking for a different name fails). Truncation at a record boundary and inside a record is `PORTS_E_NET`, and
a peer close_notify is EOF. A peer fatal alert during and after the handshake. Deadline expiry during connect,
handshake, read and write, driven by the manual clock. Transport errors at every call (EINTR retried, EAGAIN
waits within the deadline, ECONNRESET is NET). Reconnect after close. All of `ports.h`'s stream state rules
(close when not open, read when not open, cap == 0, zero-length write). Positive controls for every rejection.
Every test must run under `san` with no leak.

## Stubs and acceptance
As A001: the stubs compile under the sealed flags and the tests fail. The witness (in `/var/tmp/cache/A017/`)
passes everything in `release` and `san` built with the committed BUCK file lists. Report the API, the tests per
file, the stub and witness summaries, the memory sizing, and every doubt about `ports.h` or the peer.
