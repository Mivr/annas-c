# A009: `ports` module: IO interfaces + deterministic fakes, header, BUCK, tests (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A009`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§0, §1, §3.3), `core/*.h`.

## Allowed paths
`ports/**`. Witness and scratch under `/var/tmp/cache/A009/` only.

## Module
Interfaces as `const` function tables plus an opaque context pointer, so the pure `annas` module can be tested
with fakes and wired to real adapters by `cli`/`mcp`:
- `ports_stream_t`: `connect(ctx, host, port, deadline_ms)`, `write(ctx, buf, len)`, `read(ctx, buf, cap,
  &n)` (n == 0 means EOF), `close(ctx)`; every call returns a `ports_err_t` (`OK`, `NET`, `TIMEOUT`, `TLS`,
  `CLOSED`).
- `ports_fs_t` (all paths relative to one download directory handle): `create_temp(ctx, &file)`,
  `write(ctx, file, buf, len)`, `commit(ctx, file, final_name)` (atomic rename, refuses to overwrite an existing
  name), `discard(ctx, file)`; errors `OK`, `IO`, `EXISTS`, `NOSPACE`.
- `ports_env_t`: `get(ctx, name, &value)` returning a `core_str_t` or not-found.
- `ports_clock_t`: `now_ms(ctx)` monotonic.
**Fakes** (part of the module, used by later tests): a scripted stream (a list of expected connect target,
expected request bytes, and response bytes delivered in a scripted chunking, with injectable errors and EOF
points), an in-memory fs (fixed-capacity, records commit/discard, injectable IO/NOSPACE at the Nth write), a
table env, and a manual clock (advanced by the stream fake per read, configurable). Fakes are deterministic and
use no malloc.

## Tests
Exercise each fake through the port interface exactly as a consumer would: scripted exchanges, request
mismatch detection (the fake records the first mismatch offset), error injection at every step, fs atomicity
(no file visible before commit; discard leaves nothing; commit refuses existing names), env lookups, clock
advance. Positive controls for every rejection.

## Stubs and acceptance
As A001. Report API, tests per file, stub/witness summaries.
