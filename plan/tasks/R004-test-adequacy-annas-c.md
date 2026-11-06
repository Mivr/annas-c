# R004: how exposed are annas-c's tests to the R003 gaps? A per-module strengthening plan (Opus, read-only)

Repo: `Mivr/annas-c` (read-only; the plan goes in your report). Scratch in `/var/tmp/cache/R004/`. Read
`plan/phase1.md`, `docs/spec.md`, every module's header and tests, and the R003 audit
(`/var/tmp/cache/R004/R003-test-adequacy.md`, gaps G1-G7, gate backlog B27-B32).

## Question
R003 showed that mutation testing misses tests that don't pin: code the implementer may omit (G1), `NULL` guards (G2),
enum and string values (G3), struct returns (G4), crash-only oracles (G6), extra exported objects (G7). For each annas-c
module (`core json url fsname http ports annas app posix_* tls_stream wire`), measure the exposure on today's code and
tests, and say which spec requirements are not pinned by any visible test.

## Method
- Static counts per module: conditions with `NULL`, error-code/enum returns and how tests check them (exact value vs
  `!=`), string outputs and how tests check them, struct-returning functions and their tests, exported mutable objects.
- Spec trace: for each spec section a module implements, the tests that pin it, and the requirements no test pins (the G1
  risk: an implementer could omit them and still pass).
- Where it settles a question, a few cached runs on the farm: e.g. delete a spec-required check in a scratch clone and
  grade the module, as R003 did. Keep it to a handful; the farm is shared.

## Report (`/var/tmp/cache/R004/plan.md`)
Per module: exposure table, unpinned spec requirements (with spec section), and a ready-to-run **test-author task** list
(each: tests to add, exact expectations, the gap it closes; small enough for one Sonnet run). Order by risk.
