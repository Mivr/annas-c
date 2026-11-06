# A040: two product-side grade blockers: tls_stream san timeouts and an over-long annas.h line

Repo: `Mivr/annas-c`. Branch `task/A040`. Read `AGENTS.md` first.
Source: the orchestrator's audit grade of main `81012ea` (gate `02050f8`, 2026-11-06).

1. **`tests-san`: three tests time out** at ctest's 10 s per-test limit under the sanitizer build, and so fail with
   0 assertions:
   - `tls_stream_eof/truncated_handshake_is_net_tls13`
   - `tls_stream_split/handshake_split_at_every_early_byte_tls12`
   - `tls_stream_split/handshake_split_at_every_early_byte_tls13`

   They pass in release. Make every case finish well under the limit in san (target: < 3 s each in san on a loaded
   machine), **without dropping coverage**. For example, shard "every early byte" into several test cases that
   together cover exactly the same byte positions, or reuse one handshake transcript instead of redoing the
   handshake per position. Keep each original test id, or replace it with shards named
   `<old>_part<N>`, and list the old-to-new id mapping in your report. Each shard must assert as much as the
   original did.
2. **`structure`: `annas/annas.h` has a 201-character line**, over the L3 limit of 160. Reflow that comment, or
   wrap that declaration, so no line exceeds 160. Change only whitespace and line breaks; the text and the API
   stay byte-identical apart from those. Check the whole header for other lines over 160.

## Allowed paths
`tls_stream/tests/**`, `tls_stream/BUCK` (test entries only), `annas/annas.h` (whitespace and line breaks only).
No `src/` change.

## Acceptance
```sh
gate/tools/buck2 build //tls_stream/... //annas/... --target-platforms toolchains//:linux_release --show-full-output
gate/tools/buck2 build //tls_stream/... //annas/... --target-platforms toolchains//:linux_san --show-full-output
# every results.jsonl line "pass"; report the slowest tls_stream case's time in san (the results carry it, or time the binary)
awk 'length > 160' annas/annas.h | wc -l      # 0
diff <(git show main:annas/annas.h | tr -d ' \t\n*') <(tr -d ' \t\n*' < annas/annas.h) && echo same-text   # only whitespace and comment '*' prefixes changed
```
Report the id mapping, the per-case san times before and after, and every deviation.
