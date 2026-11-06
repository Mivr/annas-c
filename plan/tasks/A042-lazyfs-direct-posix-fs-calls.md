# A042: lazyfs calls posix_fs directly (clears the last 4 `indirect-cycle-possible`)

Repo: `Mivr/annas-c`. Branch `task/A042`. Read `AGENTS.md`, plan/tasks/A041-structure-blockers-lazyfs-apph.md (its
part 1 is this task), and in the **current gate** checkout `/var/tmp/src/c-rigor-gate` (read-only; it is newer than
the pinned `gate/` submodule): `docs/structure.md`, the `indirect-cycle-possible` row and "Typed targets" (T063).

## Why it is fixable now
T063 made the rule typed: a call through a value reaches only address-taken functions of a compatible type. The
4 remaining errors are in `wire/src/lazyfs.c`. `lz_create_temp`, `lz_write`, `lz_commit` and `lz_discard` sit in
`k_lazy_ops`, of type `ports_fs_ops_t`, and each calls the same-typed op of the inner posix_fs port
(`posix_fs_port(&l->fs).ops->...`). The type cannot tell the inner port from lazyfs itself. posix_fs's own calls go
through its `posix_fs_sys_t` seam, which has **different** types. So if lazyfs calls posix_fs **directly**, no
table function reaches a same-typed indirect call.

## Requirements (role: implementer; behaviour-preserving)
1. Add to `posix_fs/posix_fs.h` four direct entry points with exactly the port ops' semantics:
   - `posix_fs_create_temp(posix_fs_t *, ports_fs_file_t *)`
   - `posix_fs_write(posix_fs_t *, ports_fs_file_t, const void *, size_t)`
   - `posix_fs_commit(posix_fs_t *, ports_fs_file_t, core_str_t)`
   - `posix_fs_discard(posix_fs_t *, ports_fs_file_t)`

   Document each in the header style used there, saying it is the same as the port op. Change or remove nothing
   that already exists.
2. posix_fs's port ops become thin calls of these functions, or the other way round. Pick whichever keeps
   `posix_fs` free of new findings.
3. lazyfs calls the direct functions. No `void *` casts of function pointers and no tables the scanner does not
   see; those are bypasses.
4. Tests: every existing test passes unchanged. Add minimal direct-call tests in `posix_fs/tests/` for each new
   function: success, and one error path, mirroring the port tests. Register them in `posix_fs/BUCK`.
5. Prove it: build the structure facts with the **current** gate and run `rigor structure` over wire and posix_fs
   (as A041 did, scratch driver outside the repo). Report 0 `indirect-cycle-possible` errors in both, and every
   other finding.

## Allowed paths
`wire/src/lazyfs.c`, `wire/src/internal.h`, `posix_fs/src/**`, `posix_fs/posix_fs.h` (additions only),
`posix_fs/tests/**`, `posix_fs/BUCK` (test entries only).

## Acceptance
```sh
gate/tools/buck2 build //... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"; the count of passing cases rises only by your new tests
```
Report the structure findings before and after, the new test ids, and every deviation. Do not move the `gate`
submodule.
