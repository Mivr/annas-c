# A001: `core` module: dev harness, public header, BUCK, tests (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A001`. Read `AGENTS.md` (and the gate rules it links, available in the
`gate/` submodule after step 1), `plan/phase1.md`, `docs/spec.md`, and in the gate: `docs/toolchain.md`,
`docs/ctest.md`.

## Allowed paths
`.gitmodules`, `gate` (the submodule pointer only), `.buckconfig`, `.buckroot`, `.gitignore`, `core/**`,
`docs/dev.md`.

## 1. Dev harness
- Add c-rigor-gate as a submodule at `gate/`: the orchestrator has prepared a local clone at
  `/var/tmp/src/c-rigor-gate`; use `git submodule add https://github.com/Mivr/c-rigor-gate gate` but
  **check out commit `e9645c3`** (do not fetch over the network if the local clone can serve it: you may use
  `git -c protocol.file.allow=always submodule add /var/tmp/src/c-rigor-gate gate`, then set the URL in
  `.gitmodules` to the GitHub URL).
- `.buckconfig` with cells `root=.`, `gate=gate`, `toolchains=gate/toolchains`, bundled prelude, and the gate's
  RE settings and platforms (mirror `gate/.buckconfig`). Build with `gate/tools/buck2`.
- `docs/dev.md`: how to build and test locally in `release` and `san`.

## 2. The `core` module (C17, gate macros only, no malloc)
Design the public API in `core/include/core/*.h`, small and explicit, at least:
- **arena**: `core_arena_t` over a caller-provided buffer; `core_arena_init(a, buf, cap)`,
  `core_arena_alloc(a, size, align)` returning NULL on exhaustion or bad align (power of two, <= 64),
  overflow-safe; `core_arena_mark` / `core_arena_reset_to(a, mark)`; `core_arena_used`.
- **slice**: `core_str_t { const char *ptr; size_t len; }` (not NUL-terminated); `core_str_from_cstr`,
  `core_str_eq`, `core_str_starts_with`, `core_str_find` (index or `CORE_NPOS`), `core_str_trim` (ASCII
  whitespace), `core_str_split_once(s, sep, &head, &tail)`.
- **builder**: `core_buf_t` bounded byte builder over a caller buffer; `core_buf_put`, `core_buf_put_str`,
  `core_buf_put_u64`, `core_buf_put_char`; once full it sets a sticky `overflow` flag and stops writing; always
  NUL-terminates within capacity (capacity 0 allowed and handled).
- **parse**: `core_parse_u64(s, &out)` decimal, no sign, no whitespace, overflow detected;
  `core_parse_hex_nibble`; `core_ascii_lower`.
Every function: documented contract in the header (inputs, outputs, error cases, NULL handling).

## 3. Tests (`core/tests/*_test.c`, ctest)
- Through the public header only. One `c_test` per area (arena, str, buf, parse).
- Cover every documented contract clause, every error path, every boundary (0, 1, cap-1, cap, cap+1,
  `SIZE_MAX`, `UINT64_MAX` and `UINT64_MAX+1` as text, alignment 1/2/64/65/3).
- Strong assertions: check exact values and exact output bytes, not "non-NULL" or "> 0".
- Tests must be deterministic and independent of order.

## 4. Stubs
`core/src/*.c` contains only stubs (return 0 / NULL / do nothing) so everything compiles under the sealed flags,
and the tests **fail**. The implementer task (A002) replaces them.

## Acceptance
```sh
gate/tools/buck2 build //core/...
gate/tools/buck2 build //core/... --target-platforms toolchains//:linux_release  # results artifacts exist
# every *_results file shows the stubs failing (most tests fail, none crash the framework)
```
Report: the API (header text), the test list per file with what each covers, and the failing stub results
summary (`total` / `passed` per results file).
