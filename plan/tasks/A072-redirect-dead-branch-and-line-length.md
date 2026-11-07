# A072: delete `emit_path`'s unreachable non-'/' branch; wrap `annas.h:252`

Repo: `Mivr/annas-c`. Branch `task/A072`. **Implementer task.** Read `AGENTS.md`, `gate/AGENTS.md`, `http/http.h`
(`http_redirect_resolve`, `http_url_split`: a target always starts with '/') and `http/src/redirect.c`.

## Why
The 0ba3af8 grade (T091) and A071 left two items that are code, not tests:
1. `http/src/redirect.c:80` `emit_path`: survivor `++`->`--` on the `i++` of the `m_at(m, i) != '/'` branch. That
   branch is unreachable: the merged path always starts with '/' (the base path comes from `http_url_split`, a
   relative reference is joined after the base's last '/'), and every iteration then sets `i` to the end of a segment,
   which is a '/' or the end. Ruling: **delete the branch** (no guard, no rewrite to dodge mutants). If you find an input
   through the public header that reaches it, stop and report that input instead.
2. `structure` `line-length`: `annas/annas.h:252` is 186 characters (limit 160). Rewrap that comment only; no
   wording change beyond the line break.

## Acceptance (the orchestrator re-runs it)
```sh
for m in release san; do
  gate/tools/buck2 build //... --target-platforms toolchains//:linux_$m --show-full-output
done
# every results.jsonl line "pass" (1765 tests); `http` survivors re-measured: the redirect.c:80 one is gone
```
Measure `//http:http` mutants in a scratch clone under `/var/tmp/cache/A072/` with the current gate (as A046/A071 did)
and report before/after survivors for http.

## Rules
Edit only `http/src/redirect.c` and `annas/annas.h` (that one comment). `TMPDIR=/var/tmp/cache/A072/tmp`; never write
to `/tmp`. Build only with `gate/tools/buck2`. No `git stash`, no merge of `main`, no push. Never read
`/var/tmp/.config`, `/var/tmp/.claude*`, `/var/tmp/src/c-rigor-hidden`, other worktrees or other cache dirs. Kill only
processes you started, by pid. No sleep-poll loops (`gate/tools/job`). 2 CPUs, 8 GiB shared. Fake keys only; no
network.
