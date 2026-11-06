# A021: worst-case complexity tests for `json` and `http` (test-author task; implementations are on main)

Repo: `Mivr/annas-c`. Branch `task/A021`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`json/json.h`, `http/http.h` (the input caps they document), `json/tests/**`, `http/tests/**`, and
`gate/docs/ctest.md` (the per-test timeout).

## Why
Two known defects: duplicate-key detection in `json` is O(n^2) in the number of keys of one object, and
dot-segment removal in `http` (URL/redirect path normalisation) is O(n^2) in the path length. Today only the
input caps bound them. A hostile server or redirect target can make the client burn seconds of CPU per document,
and under `san` that can exceed the test timeout. Every public operation must be (near-)linear in its input up
to the documented caps.

## Allowed paths
`json/tests/**`, `http/tests/**` (new test files are fine; add them to `json/BUCK` / `http/BUCK` test targets
only, and touch nothing else in BUCK). Witness and scratch under `/var/tmp/cache/A021/` only.

## Tests
For each public entry point of `json` and `http` that takes untrusted input, find the worst-case input shape up
to the documented cap. That includes:
- for `json`: maximal key counts in one object with distinct keys, keys sharing long prefixes, colliding-shape
  keys, deep nesting at the depth cap, and long strings with many escapes;
- for `http`: paths of `/./`, `/../` and `//` at the cap, long header blocks, many headers, chunked bodies of
  minimal chunks, and long redirect `Location` values.

Each test must run the input to completion and assert the correct result, not only "doesn't hang". Choose
input sizes so that a linear or n·log n implementation finishes in well under 1 s under `san`, and an O(n^2) one
exceeds the 10 s ctest timeout by a wide margin. State the arithmetic for each in a comment. Don't put
wall-clock assertions inside tests: the ctest timeout is the only clock, so the tests stay deterministic.

## Acceptance
- The current main implementations fail exactly the tests that target the two known defects, by timeout, in
  `release` or `san` (report which), and pass all the other new tests.
- A witness (in `/var/tmp/cache/A021/`: copies of `json/src` and `http/src` with linear algorithms, never
  committed) passes everything in `release` and `san`.
- Report: the tests per file with their arithmetic, the results of main's implementation (with timings from the
  results files), and the witness summaries. The implementer task A022 then fixes `json/src` and `http/src`.
