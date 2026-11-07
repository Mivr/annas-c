# A068: app + posix_fs tests for the A063 killable survivors

Repo: `Mivr/annas-c`. Branch `task/A068`. **Test-author task**: add tests for `//app:app` and `//posix_fs:posix_fs`.
Put them in **new** test files (`app/tests/<name>.c`, `posix_fs/tests/<name>.c`) registered in the module's `BUCK` with the
smallest possible edit; another lane is editing existing `app/tests` files, so do not edit existing test files. No `src/`,
no header. Read `AGENTS.md`, `gate/AGENTS.md`, `app/app.h`, `app/app_mcp.h` (and any other public `app/*.h`),
`posix_fs/posix_fs.h`, existing tests in both modules for style and fakes, and in `/var/tmp/src/c-rigor-gate` (read-only)
`docs/mutate.md`. Do not edit `plan/`. Do not move the `gate` submodule.

## Why
After A063 (main), these survivors are killable through the public headers. Each row: file:line, mutant id(s) at A063's
measurement (lines may have moved: re-measure), the mutation, and a killing input. Use only fake keys (the key below is a
fake).

app:
- dotenv.c:3 `5410d3d07b157767`, `fefa0775edae6b43`: `'\v'`, `'\f'` in is_ws. A `.env` line `"\vA=b\f"` must give A=b
  (header: "space, \t, \v, \f").
- dotenv.c:66 `67d54f0937f11dcc`: `file.len > 0` -> `> 1`. File {NULL, 1} must return false (header).
- mcp.c:252 `8d76a85da053e3b7`: `nomem(m)` deleted in call_run. A tools/call whose DOM leaves fewer than APP_MCP_CALL_MEM +
  6 * id_len bytes must reply -32603 and touch no port (header, app_mcp_init).
- mcp.c:383 `5164b7c8e8304ec9`: `core_arena_reset_to(&s->arena, 0)` -> `1`. Send a line, then a line whose DOM exactly fills
  the parse memory (APP_MCP_ARENA_MIN): the second must succeed (header: the arena is "rewound for every line").
- ops.c:31 `174a7d6215bd9e43`: match_pct `t.len < w` -> `<=`. The percent-encoded key as the LAST bytes of the text.
- ops.c:123 (id `1916c4d7...`): `src.len <= APP_FIN_MAX` -> `<`. No key, an MCP reply of exactly 65536 bytes: delivered.
- Scrub shortcut (ops.c pass_run / has_match / pass_write / app_finalise): `79167078` (`cap - n` -> `+`), `d8516020`,
  `28cf9981`, `c85ed009`, `a8e6e0a6`, `dbbd0857` (`!force_copy`), `50407ddd` (`return false` -> true). Input, through
  app_mcp_handle: key `ab+cd/ef=gh` (env ANNAS_SECRET_KEY), parse memory >= 1 MiB, request
  `{"jsonrpc":"2.0","id":"<ID>","method":"ping"}` with <ID> = `ab%2Bcd%2Fef%3Dgh` repeated and cut to N bytes. The reply is
  `{"jsonrpc":"2.0","id":"<ID>","result":{}}` (N + 37 bytes before scrubbing); pass 2 turns each 17-byte encoded key into
  REDACTED. Expected reply lengths: N = 65499 -> 30868; N = 65500 -> 30869 (NOT the -32603 error); N = 100000 -> 47099;
  N = 200000 -> the -32603 error. Without a key: N = 65499 delivered (65536 bytes), N = 65500 -> -32603. Assert the
  exact bytes, not only lengths, where it is cheap.
- ops.c:199 `cee36376e6927c65`: `hex[c >> 4]` -> `>> 5`. Every control byte 0x00..0x1f in a JSON string reply
  (`\u0000`..`\u001f`, exact escape text).

posix_fs:
- io.c:112 `c5922a802687844e`: name_valid literal 1 -> 0. Names "." and ".." give E_ARG, ".x" is accepted.
- sys.c:17 (`4f6b8941853c2435`, `364db2a75ffb4189`) and :23 (`6e136f375ec36925`, `c8c9e5c966bc37e7`): `fd < 0` -> `<= 0`.
  Call `posix_fs_sys_libc.open_dir` / `open_tmp` while descriptor 0 is free (close stdin first, as wire's test
  `libc_seam_reads_file_opened_as_fd_zero` does): the result must be 0, not -errno. Restore fd 0 after.
- sys.c:29 (`e4fd8331bab60d1e`, `09c8990d436afa66`): `n < 0` -> `<= 0`. `posix_fs_sys_libc.pwrite` of 0 bytes returns 0.
- sys.c:34 (`191e59020ecaf7f6`, `dfc9e68e599b16c4`), :39 (`7f7372907475c17d`), :48 (`e0c575a9dbe602dc`): the `0` of `!= 0`
  and of the success value. Call `posix_fs_sys_libc.ftruncate / fsync / link_tmp` on a real descriptor (under TMPDIR the
  test gets from the test runner; use what existing posix_fs real-file tests use) and check the result is exactly 0.

The two remaining survivors, dotenv.c:6 and dotenv.c:43, are equivalent (approved by the orchestrator): do not try them.

## Measurement
In a scratch clone under `/var/tmp/cache/A068/` with the current gate over `gate/`: `c_mutants` for `//app:app` and
`//posix_fs:posix_fs` with all their tests on `linux_release`, judged by `rigor mutants`. Report survivors before and
after; the target is exactly the two dotenv survivors left in app and none in posix_fs. Report any crash-kill-ratio finding.

## Acceptance (the orchestrator re-runs it)
```sh
gate/tools/buck2 build //app/... //posix_fs/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //app/... //posix_fs/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"
```
Report the new test ids (they need a gate config follow-up), the measurements, and every deviation. Tests are graded
against a witness implementation: pin only what the headers promise.

## Rules
Use `TMPDIR=/var/tmp/cache/A068/tmp`; never write to `/tmp`. Build only with `gate/tools/buck2`, never host compilers. No
`git stash`, no merge of `main`, no push. Never read `/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`,
other worktrees or other cache dirs. Never kill processes you did not start; never `buck2 kill` outside your scratch. No
sleep-poll loops (`gate/tools/job`). 2 CPUs, 8 GiB shared.
