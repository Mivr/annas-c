# A025: `core_buf_truncate` and a write-cursor contract for `http_redirect_resolve` (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A025`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`core/buf.h`, `core/tests/buf_test.c`, `http/http.h`, `http/tests/**` (especially `complexity_test.c`).

## Why
A022 had to implement linear dot-segment removal without a way to shrink a `core_buf_t`, which produced an
intricate O(n log n) block-tree algorithm (`http/src/redirect.c`, about 400 lines). With a truncate operation, RFC 3986
§5.2.4 "remove_dot_segments" is a simple write cursor: append each segment, and on `..` truncate back to the
previous `/`. That is less code and fewer mutants, and easier to trust.

## Allowed paths
`core/buf.h` (add the function only; no other change), `core/tests/**`, `core/src/*.c` **stubs only** (the new
function as a stub that does nothing; leave the other functions unchanged), `core/BUCK` (only if a test file is added).
Witness and scratch under `/var/tmp/cache/A025/` only.

## Contract to specify
`void core_buf_truncate(core_buf_t *b, size_t len);` (or `bool`, with a documented reason). It shrinks the content
to `len` bytes if `len <= core_buf_len(b)`, otherwise it is a no-op. Specify exactly what happens to the
overflow flag (recommendation: unchanged; truncating never clears a sticky overflow, because the lost tail
can't be recovered), the NUL terminator for `core_buf_cstr`, NULL `b`, and `len == 0`.

## Tests
Exact content after truncation, the cstr terminator, overflow stickiness, idempotence, no-op cases, then appends
after truncation, interleaved put and truncate sequences checked against a model, and the boundary at capacity.
Positive controls. The existing `core` tests stay unchanged.

## Acceptance
The stub fails the new tests, and all other tests still pass. The witness (in `/var/tmp/cache/A025/`, core only)
passes all of them in `release` and `san`. Then A026 (implementer) implements it in `core` and rewrites
`http/src/redirect.c` as a write cursor. A026's acceptance is all http tests, including complexity, passing.
