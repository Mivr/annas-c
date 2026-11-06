# R004: annas-c exposure to the R003 test-adequacy gaps, and a per-module strengthening plan

## What I measured
- **Product:** annas-c `main` at `6172fb1`, cloned in `/var/tmp/cache/R004/annas-c`. The `gate` submodule is
  filled from a local clone of c-rigor-gate (`/var/tmp/cache/R004/gate-src`, main `b1298b7`). It is checked out at
  `b1298b7` in my scratch clone, not at the pinned `67c2230`, so the mutants come from the gen.py the grader uses
  today. Nothing was committed or pushed.
- **Static counts:** scripts in `/var/tmp/cache/R004/scripts/`. `count.py` gives the first counts, `nullargs.py`
  finds NULL guards per parameter and the tests that pass NULL in that position, and `macroconds.py` finds
  conditions that touch a macro.
  - The scripts are approximate. A call made through a test helper (`init_with`, `run_with`) is not seen. So
    every "unpinned" row below was checked by hand or by a probe.
- **G1 probes:** `probe.py`. It deletes one spec-required check in the scratch clone, builds and runs the module's
  tests (release, on RE), then reverts. If every test still passes, nothing pins that requirement. Each probe took
  8 to 33 s and all were cached.
- **One mutation run:** a local `c_mutants` target for `//annas:annas` on the gate-main mutation pipeline. The
  results are in [Mutation run](#mutation-run-annas-at-l3-today). This target is local only, in the scratch
  `annas/BUCK` and `http/BUCK`.

## Headline findings
1. **Most of the product is not mutation-graded at all.** The gate's `products/annas-c` (gate main `b1298b7`) has
   levels only for `core`, `json`, `url` and `fsname` (L3) and for `ports` (L2).
   - Its `[layout] include` leaves out `http`, `app`, `posix_*`, `tls_stream`, `wire` and `bin`. It still lists the
     dead names `html`, `adapters`, `cli` and `mcp`. Its `whole-repo` task `allowed` list has the same paths.
   - So `annas`, which is in the layout, cannot even build in a grading tree, because `//http:http` is not copied.
   - `http`, `annas`, `app`, `wire`, `posix_*` and `tls_stream` have **never had a mutant run**. That is about
     4,600 of the 6,700 source lines, including the key handling, the file writing and the hostile-input parser.
   - For these modules G1 is total: their tests were judged only by passing. phase1.md says http and annas are
     L3.
2. **A new gate blocker (B-new, fails closed).** Five product modules (`annas`, `app`, `wire`, `posix_fs`,
   `tls_stream`) have a header-only test-kit `c_library` with `srcs = []`.
   - `grade_mutants._link_deps` puts it in the `c_mutants` deps, and `rules/mutate.bzl:203` reads
     `d[DefaultInfo].default_outputs[0]`. The analysis fails with `Index 0 is out of bound` (reproduced with
     `//annas:testkit`).
   - Once these modules get a level, the grade will be `mutants-build-failed`.
   - Fix in the gate: skip deps that have no default output in `mutate.bzl` (or have `_link_deps` drop libraries
     without sources), plus a canary with a header-only kit.
3. **The tests themselves are strong.** All 1,346 visible test cases pin byte-exact outputs, exact error codes (no
   `!= OK` oracles: every `!= X_OK` in the tests is helper control flow), exact messages, and the port
   interactions (strict scripted fakes).
   - There are no crash-only helpers (0 hits for `*(volatile`, `abort(` or `__builtin_trap` in any test).
   - G3 and G6 exposure is therefore low across the board.
   - The real exposure is (a) the never-graded modules, (b) conditions that mutation cannot see (NULL and
     product macros, G2 and T028), and (c) a short list of header clauses no test reaches, found by the probes.
4. **G7: none today.** No writable object has external linkage. The four exported objects are const tables:
   `posix_fs_sys_libc`, `posix_clock_sys_libc`, `wire_sys_libc` and `wire_io_libc`. The only mutable file-scope
   object is `static tls_obj_t *g_active` (`tls_stream/src/heap.c:20`), which `tls_stream.h` documents (the mbedTLS
   global hooks).
   - B32's `exports` list must include the four const tables.
5. **G4/G5: about 15 struct-returning functions, all typedef'd anonymous structs.** Every one returns a struct
   whose type is a typedef, so each gets the unviable `return 0` mutants of G5.
   - The ones with no operator at all are the port constructors: `ports_fake_{stream,fs,env,clock}_port`,
     `posix_{env,clock,fs}_port`, `tls_stream_port`, `tls_stream_sock_transport`, `app_env_layer_port`,
     `wire_lazyfs_port`, plus the static `op_of` (app/src/cli.c).
   - Their behaviour is pinned indirectly: every test goes through the returned port.
   - The risk is G5 (a false `no-assert-kill-function` and extra `unviable-ratio`) once `ports` is graded at L3,
     or once `app` and the posix modules get levels.

## Per-module exposure

Legend:
- **graded**: the module has a level in the gate's policy today.
- **NULL conds**: source lines with `NULL` in a condition, invisible to relational, logical and condition mutants
  (G2).
- **macro conds**: conditions with a product object-like macro, also invisible (the T028 class; gen.py drops any
  node whose range touches a macro).
- **unpinned NULL rules**: rules the header states, with no test passing NULL at that position (verified).
- **err returns**: `return <X_E_*>;` sites; how the tests check codes.
- **struct ret**: struct-returning functions (G4/G5).
- **G6**: crash-only helpers in the tests.

| module | graded (gate) / plan | src LOC | tests | NULL conds | macro conds | unpinned NULL rules | err returns / test style | struct ret (no-op body) | G7 | G6 | G1 probes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| core | L3 / L3 | 244 | 126 | 18 | 3 | 0 | none (bool/size APIs), exact values | 3 (0) | 0 | 0 | - |
| json | L3 / L3 | 940 | 129 | 45 | 3 | 0 public | 5 + via `fail()`; exact `CHECK_EQ_I64` + offset | 4 (0) | 0 | 0 | - |
| url | L3 / L3 | 246 | 75 | 14 | 0 (+9 via `bad_ptr`) | **4** (`base` of fast_download, md5_page, scidb_path, scidb_download) | 16; exact | 0 | 0 | 0 | P1 **not pinned** |
| fsname | L3 / L3 | 94 | 34 | 1 | 4 (all limits, boundaries tested at 200/201) | 0 | none | 0 | 0 | 0 | - |
| ports | L2 / L2 | 592 | 129 | 53 | 16 | 1 stated (`preload content NULL`) | 27; exact | 4 (4) | 0 | 0 | - |
| http | **none** / L3 | 971 | 120 | 16 | 15 | 0 | 44; exact (86 `CHECK_EQ_I64(...HTTP_E_*)`) | 0 | 0 | 0 | - |
| annas | **none** / L3 | 1022 | 148 | 25 | 25 | **per-function port NULLs** | 3 + `msg_text` codes; exact code, status, message, result untouched | 0 public | 0 | 0 | P2a, P3, P4 **not pinned** |
| app | **none** / L2 | 973 | 211 | 37 | 9 | 0 | exact text everywhere (57 `CHECK_EQ_STR`) | 2 (1) + `op_of` | 0 | 0 | P5 pinned, P8 **not pinned** |
| posix_clock | **none** / L2 | 65 | 13 | 3 | 1 | 0 | n/a | 1 (1) | const table | 0 | - |
| posix_env | **none** / L2 | 44 | 14 | 6 | 0 | 0 | n/a | 1 (1) | 0 | 0 | - |
| posix_fs | **none** / L2 | 299 | 51 | 12 | 7 | 0 | 17; exact | 1 (1) | const table | 0 | - |
| tls_stream | **none** / L2 | 688 | 189 | 37 | 7 | 0 | 17; exact | 2 (2) | static `g_active` (documented) | 0 | - |
| wire | **none** / (L2) | 509 | 107 | 10 | 17 | 0 | exact stderr texts (macros from wire.h) | 1 (1) | 2 const tables | 0 | - |

Totals:
- 277 source lines carry a `NULL` condition (R003 counted 181 `if`/`while` ones), and 107 conditions touch a
  product macro.
- Of 167 NULL-guarded parameters, the scan flags 30 without a NULL call. After removing helper-wrapped calls,
  internal functions and accessors whose NULL behaviour the header never states, the header-stated gaps are the
  four `url` builders, the annas per-function port checks and `ports_fake_fs_preload(content = NULL)`.

### G1 probes (delete one spec-required check; do the module's tests still pass?)
| # | module | edit | requirement (where) | result |
|---|---|---|---|---|
| P1 | url (L3, graded) | drop `bad_ptr(base) ||` in `url_build_fast_download` | url.h status rules: `ptr == NULL` with `len > 0` is `URL_E_ARG`, precedence ARG > INVALID | 75/75 pass: **not pinned** (the call now returns `URL_E_INVALID`). The same holds statically for md5_page, scidb_path and scidb_download; only `url_build_search` has a NULL-base test |
| P2a | annas | `stream_ok` checks only `s.ops != NULL` | annas.h `annas_ctx_init`: "a port whose ops table **or any of its functions** is NULL makes it fail" | 148/148 pass: **not pinned** (tests use only `{NULL, NULL}` ports). A later call jumps through NULL. G2 also hides the `!= NULL` and `&&` operators from mutation |
| P3 | annas | `deadline = now + t` (wrapping) | annas.h Timeout: "start + cfg->timeout_ms (saturating)" | 148/148 pass: **not pinned** (no test starts the clock near `UINT64_MAX`; a wrapped deadline gives an instant `E_TIMEOUT`) |
| P4 | annas | no `op_check` before `fs commit` | annas.h Timeout: "compares the clock with it before every port call (... fs commit)" | 148/148 pass: **not pinned**. It cannot be pinned with the current kit: the fake clock moves only on stream reads, so nothing advances between the last write and the commit. It needs a test clock that advances on every `now_ms` call |
| P5 | app | drop `line.ptr == NULL && line.len > 0` | app.h `APP_MCP_E_ARG` | caught (`mcp_bad/handle_argument_errors`) |
| P8 | app | apply pass 2 to keys longer than 256 bytes | app.h "for such a key only pass 1 is applied" | 211/211 pass: **not pinned** (low impact: annas refuses such keys anyway) |

### Requirements traced but not probed (static reading: pinned)
- spec §1 `.env` and layering, and `ANNAS_BASE_URL` normalisation: app dotenv tests, annas config, wire.
- spec §2 User-Agent: exact request bytes. Challenge rules, `check=10` and the first 1024 bytes: http
  challenge_test and annas fast_err.
- spec §3.3: every message in the annas.h error table (34 texts) appears in a test with an exact expectation.
  Temp-then-commit, discard on every failure, one fs write per read, and the 5-redirect cap are covered.
- spec §5: every MCP error text and code (-32600/-32601/-32602/-32603/-32700), version negotiation, id echo, line
  cap, notifications and UTF-8 repair are covered.
- spec §6: exit codes in format_test and app. spec §7: fsname, including property tests.
- tls_stream: every TRUST, ERRORS, DEADLINE and STICKY clause has a named test. posix_fs: every order-of-checks,
  rollback and errno-mapping clause has a named test.
- Unpinnable by design (note only):
  - posix_fs "ids skip 0 when the 32-bit counter wraps" needs 2^32 creates.
  - tls_stream_version "also when broken": no test asks for the version after a sticky failure (low).

## Mutation run: annas at L3 today
**What I ran:**
- `c_mutants(module = ":annas", tests = <all 12 annas tests>, deps = [http, json, url, fsname, ports, core])`, built
  on RE: 17 min wall, about 900 compile and about 10,000 run actions. This was the one large farm use.
- I judged it with `gate/rigor mutants`, policy L3. The raw report and verdict are in `/var/tmp/cache/R004/mutres/`.
- I had to leave `:testkit` out of `deps` (finding 2).

**Result:** FAIL. Of 897 mutants:
- 573 killed by a failed check, 26 killed by a crash, **92 survived**;
- 27 equivalent, 118 duplicate, 61 unviable (6.8 %, under the 25 % limit).

Plus **9 x `unmutated-function`** for `static inline ctx_key` in `annas/src/internal.h`, an implementer's
header-defined helper. That one needs an implementer fix (move it to a `.c` file); a test cannot fix it.

**G6 calibration for B31:** the highest per-function crash-kill share is 50 % (`fill` in exchange.c, 4 of 8 live), and no
function is above 50 %. Overall it is 26 of 599 kills. The proposed `> 50 %` threshold would not fire on annas.

**The 92 survivors, by cause** (file:line and operator in `mutres/annas-verdict.json`):

| group | survivors | what is unpinned | who fixes |
|---|---|---|---|
| S1 operation-time config checks | 13: `api.c:7-11, 40, 43, 47`, `ctx.c:83, 86` | `annas_fast_download_url` / `annas_download` called with a hand-built `annas_config_t` whose base is invalid, whose key is longer than 256 bytes, or whose dir is longer than 256 bytes. The whole `base_invalid()` can be deleted, and the 200-byte echo cut can become 201 | **orchestrator decision**: annas.h says ops check only "key (and dir) non-empty". Either the header adds these checks (then a test-author task) or the implementer deletes the code |
| S2 deadline checks have no observable effect | 5: `download.c:26, 38, 52`, `exchange.c:38, 47` (`return r` -> OK) | After `op_check` reports a timeout, carrying on still ends in `E_TIMEOUT` (the fake stream times out too). No test asserts that the port call after the deadline was **not made** (temp-file count, fs-write count, connect count) | test author (T1) |
| S3 redaction char classes and edges | 17: `redact.c:14-15, 37-41, 60, 69`, `msg.c:42` | `pct_differs`/unreserved class boundaries ('A' 'Z' 'a' 'z' '0' '9' and the next byte past each), a pct match with only one hex digit right, a **1-byte key**, a key occurrence at **position 0** of a message | test author (T2) |
| S4 format validation boundaries | 3: `name.c:12` | format letters at the class edges: `a`, `z`, `A`, `Z`, `0`, `9` accepted; `` ` ``, `{`, `@`, `[`, `/`, `:` refused | test author (T2) |
| S5 NULL slices in a request | 5: `name.c:23-25` | `annas_download` with `title` or `format` = `{NULL, n>0}` (and md5 `{NULL, 0}` vs `{NULL, n}`) must give `E_ARG "invalid argument"` before any port call (G2: the NULL side is invisible) | test author (T1) |
| S6 challenge prefix over several reads | 1: `download.c:120` (`n += take` -> `-=`) | a non-200 download response whose `ddos-guard` text arrives in the 2nd or later BODY event, and the 1024-byte cap across events | test author (T1) |
| S7 `http_status` on non-HTTP errors | about 12 integer-literal `0 -> 1` on the status argument of `msg_text`/`dl_fail`/`exch_fail` (`ctx.c:72, 79`, `exchange.c:34, 87`, `download.c:163, 217`, `name.c:77`, ...) | those paths are checked without `http_status == 0` (no `kit_check_err` there), or are unreachable | test author for the reachable ones (T1); implementer removes dead branches (`exch_open` parser-init failure, `dl_hop` request-write failure, `core_buf_init` failure on fixed storage) |
| S8 equivalent or defensive | about 36: initialisers `{NULL, 0}` / `= 0` that are overwritten, the `priv[]` zeroing loop (`ctx.c:41-43`), clamps `MAX - 1u -> 0u` that an invariant already ensures, `head_note(..., NULL, 0)`, `n > 0` before a 0-byte `memcpy`, `name.c:61` (`dir.len >= 0` reads `dir.ptr[-1]` for dir "/": a real OOB read, invisible because **mutants run in release only**) | implementer clean-up or `equivalent-mutant` approvals; gate note: OOB-read mutants need a san run |

**For http, app, wire and the adapters, no run was made** (http alone would be about 13,000 actions). Once the
testkit blocker is fixed and the policy gives them levels, the first grade will produce the equivalent list. Per
line, annas's tests are as strong as http's and app's, so expect a survivor rate of the same order (about 10 %
of the generated mutants).

## Top unpinned spec requirements (by risk)
1. **Nothing at all pins** `http`, `annas`, `app`, `wire`, `posix_*` and `tls_stream` against mutation: there is no
   level in the gate policy and no layout entry (finding 1). spec §2, §3.3, §5 and §6 code is judged only by
   whether tests pass.
2. annas.h **Timeout**: "before every port call (connect, stream write, stream read, fs create_temp, fs write, fs
   commit): now >= deadline -> E_TIMEOUT" and the deadline is **saturating**.
   - Unpinned by probes P3 and P4 and by survivors S2.
   - An implementer can drop the saturation (an instant timeout near `UINT64_MAX`) and the pre-call checks (a
     write or commit after the deadline) and pass.
3. annas.h **`annas_ctx_init`**: "a port whose ops table **or any of its functions** is NULL makes it fail".
   - Unpinned (P2a).
   - Dropping the check means a NULL function pointer is called later.
4. annas.h **message finalisation** (key secrecy) edge cases. A 1-byte key, a key at offset 0, pct-encoding class
   edges and one-digit near-misses are not pinned (S3).
   - An off-by-one can leak a key that starts a server message, or a key whose encoding uses a boundary
     character.
5. url.h **`URL_E_ARG` precedence** for `base = {NULL, n>0}` in `url_build_fast_download`, `url_build_md5_page`,
   `url_build_scidb_path` and `url_build_scidb_download`.
   - Unpinned (P1) in a module the gate already passes at L3. It is a pure G1 example: the check was simply
     omittable.
6. annas.h request validation: `title` / `format` `{NULL, n}` -> `E_ARG` (S5). The format alphabet edges (S4)
   are also unpinned.
7. annas.h download, non-200 handling: the challenge prefix is "up to `HTTP_CHALLENGE_PREFIX` bytes or the end
   of the message", across body events (S6).
8. annas.h errors: `http_status` is 0 for errors not tied to an HTTP response (S7, partly).
9. app.h key scrubbing: only pass 1 for keys longer than `ANNAS_KEY_MAX` (P8; low).
10. Contract gap: operation-time base, key-length and dir-length checks exist in the code but not in annas.h (S1).
    The orchestrator must decide which side moves.

## Test-author task list (ordered by risk; each sized for one Sonnet run)
Every task:
- touches only `<module>/tests/**` (and the module's test-kit headers), never `src/` or the public header;
- runs `gate/tools/buck2 build //<module>/... --target-platforms toolchains//:linux_{release,san}`;
- reports the new tests, each with the probe or survivor it kills. The orchestrator re-runs the probe
  (`scripts/probe.py`) or the annas mutants as acceptance.

**T0 (gate, before any test task; not a test-author task).** These make the work above measurable.
- Fix `rules/mutate.bzl:203` so that header-only `c_library` deps (no default output) are skipped. Add a canary
  `header-only-kit`.
- Add `http`, `annas` (L3) and `app`, `posix_clock`, `posix_env`, `posix_fs`, `tls_stream`, `wire` (L2) to
  `products/annas-c/policy.toml`. Fix `product.toml` `[layout] include` and `tasks/whole-repo.toml` `allowed`
  (add those dirs plus `bin/**`, drop `html`, `adapters`, `cli` and `mcp`), and put `//bin:*` at L1.
- Then grade main once. That gives the http/app/wire survivor lists that feed T6 and T7.

**T1: annas timeouts, ports and request arguments** (`annas/tests/`; closes P2a, P3, P4, S2, S5, S6, S7).
- `ctx_test`: `annas_ctx_init` returns false for a stream table with each of `connect`/`write`/`read`/`close`
  NULL, for an fs table with each of `create_temp`/`write`/`commit`/`discard` NULL, and for a clock with
  `now_ms` NULL (11 cases, each against a valid control).
- Saturation: with a fake clock started at `UINT64_MAX - 5`, timeout 3600 s, a full `annas_fast_download_url`
  succeeds (`kit_check_ok`), and a download succeeds as well.
- A test clock port (in the kit) that advances by `step` ms on **every** `now_ms` call. Use it to place the
  deadline exactly:
  - between the last body write and the commit: `E_TIMEOUT "operation timed out"`, `discards == 1`,
    `commits == 0`;
  - before `create_temp`: temp count 0, discards 0;
  - before an fs write: fs-write count unchanged;
  - after connect, before the request write: no EXPECT consumed (the stream position equals the connect step).
- `annas_download` with `title = {NULL, 3}`, then `format = {NULL, 3}`: `E_ARG "invalid argument"`,
  `http_status 0`, no port touched, result untouched. The same with `md5 = {NULL, 32}`.
- Download response 403 with Content-Length 2000, delivered in 3 reads: "ddos-guard" in the 2nd read gives
  `E_BROWSER_CHECK`. With "ddos-guard" starting at body offset 1020 (crossing 1024), and with it at 1030, the
  result is `E_HTTP "unexpected HTTP status 403"`.
- Every new error case uses `kit_check_err` (status 0 for non-HTTP errors).

**T2: annas redaction and format edges** (`annas/tests/leak_test.c`, `format_test.c` or a new
`edge_test.c`; closes S3, S4).
- A 1-byte key `"k"`: a server error text `"k"`, then `"kk k"`. Expect `"REDACTED"` / `"REDACTEDREDACTED REDACTED"`.
- A key that is the first bytes of the server `error` text: the message starts `"401: REDACTED..."`.
- Keys containing each byte just outside and at each unreserved-class edge (`@ A Z [ ` a z { / 0 9 : - . _ ~`).
  For each, the server echoes the pct-encoded form and the expected text is `REDACTED`. The same text with one
  hex digit changed (`%2B` -> `%2C`) is left alone.
- Format `"a"`, `"z"`, `"A"`, `"Z"`, `"0"`, `"9"` are accepted (file `<md5>.<lower>`). Format `` "`" ``, `"{"`,
  `"@"`, `"["`, `"/"`, `":"` give `E_ARG "format must consist of 1 to 16 ASCII letters or digits"`, before any
  port call.

**T3: url NULL base** (`url/tests/url_build_test.c`; closes P1). For each of `url_build_fast_download`,
`url_build_md5_page`, `url_build_scidb_path` and `url_build_scidb_download`: `base = {NULL, 5}` returns
`URL_E_ARG`, the buffer is unchanged (pre-content kept, not overflowed). It must beat `URL_E_INVALID` from an also
invalid md5 or doi (precedence). It is small, and it is the only gap found in an already graded module, so it can
run first.

**T4: S1 decision, then tests** (`annas/tests/ctx_test.c`; after the orchestrator amends annas.h, or skip if the
implementer removes the code).
- A hand-built config with base `"Bad_Host"`: `E_CONFIG "ANNAS_BASE_URL is not a valid domain, got: Bad_Host"`.
  A base of 253 bytes: exact echo cut at 200 bytes.
- A key of 257 bytes: `"ANNAS_SECRET_KEY is longer than 256 bytes"`. A dir of 257 bytes:
  `"ANNAS_DOWNLOAD_PATH is longer than 256 bytes"`.
- In every case no port is touched and `http_status` is 0.
- `config_test`: a raw base of exactly 201 bytes is echoed cut to 200 (config.c:43 `200 -> 201`).

**T5: app long-key scrubbing** (`app/tests/scrub_utf8_test.c` or `leak_test.c`; closes P8, low).
- A 300-byte key containing `+`, with an MCP id string equal to `pct_encode(key)`.
- The reply echoes the id with only pass 1 applied, i.e. the encoded form is **not** replaced, exactly as
  app.h says. (If the orchestrator prefers to scrub it too, change app.h first; the test then pins the other
  behaviour.)

**T6: http survivors** (after T0's first grade; `http/tests/`). These are the expected classes, from the static
counts:
- 15 product-macro conditions: limits `HTTP_MAX_LINE`, `HTTP_MAX_HEADERS`, `HTTP_CHALLENGE_PREFIX`,
  `HTTP_MAX_URL`, each to be pinned at limit and limit + 1;
- 16 NULL conditions;
- 44 error-return sites. Today all are checked by exact code, but whether each **site** is reached by an exact
  check is unknown until B29's `enum-constant` operator exists. Run one enum-swap probe per site with
  `scripts/probe.py` if B29 is not done first.

**T7: app, wire and adapter survivors** (after T0; one task per module, L2, so survivors are info first). Expect
mostly S8-type equivalents, plus port-constructor G5 noise (`*_port()` struct returns).

## Gate-side notes for the orchestrator (not test-author work)
- **B-new (blocker, fails closed):** header-only test-kit libraries break `c_mutants` analysis (finding 2).
- **B30/G5 will hit:** 12 port constructors, plus `op_of`, return typedef'd anonymous structs with no other
  mutable token. Once their modules are at L3 that means `no-assert-kill-function` and unviable noise, before
  B30 lands.
- **B32:** the export list must contain the four const tables (`posix_fs_sys_libc`, `posix_clock_sys_libc`,
  `wire_sys_libc`, `wire_io_libc`). There is no mutable export today.
- **B31:** annas peaks at 50 % crash kills per function (8 live mutants), so 50 % is a safe threshold here.
- **T028 (product macros):** 107 conditions touch a product macro, 25 of them in annas and 17 in wire. These are
  limit checks that no mutant reaches. The visible tests do test the boundaries I sampled (fsname 200/201, annas
  URL 2048/2049, wire `.env` 16384/16385). Once T028 or B28 lands, re-grade to confirm.
- **Release-only mutants** cannot see OOB reads (`name.c:61`). A san pass over the survivors would turn such
  survivors into kills or real findings.
- **Implementer task for annas** (with T1/T2):
  - move `ctx_key` out of `src/internal.h`;
  - delete the dead branches in S7/S8 (parser-init failure, request-write failure after a successful split,
    `core_buf_init` on fixed storage, the `priv[]` loop);
  - resolve S1.
