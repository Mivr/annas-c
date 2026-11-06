# A031: README and `docs/usage.md` describe v1 as it is (API-only CLI and MCP server)

Repo: `Mivr/annas-c`. Branch `task/A031`. Docs only: no code, BUCK, test or header change.

## Why
`README.md` still says "not usable yet" and lists HTML search and DOI lookup, which v1 deferred (`docs/spec.md` §0,
`plan/phase1.md`). v1 has two binaries (`bin/annas_c_main.c`, `bin/annas_c_mcp_main.c`): download by md5,
download-url, and the MCP server over stdio with two tools.

## Allowed paths
`README.md`, a new `docs/usage.md`.

## Requirements
1. **README**: what it is (v1 scope, exactly), what's deferred to v2 and why, how to build (`gate/tools/buck2 build
   //bin:...`, the binary paths `--show-output` prints), configuration (env vars and `.env` per spec §1; the key is never
   logged), the c-rigor-gate grading status of each module (level from `plan/phase1.md` and the product policy), license.
   No marketing claims. No claim of a live test the docs can't show.
2. **docs/usage.md**: every CLI command and flag with exact usage and error texts and exit codes, copied from the real
   binaries (build them with Buck2 and run `--help`, `--version` and each usage error locally; no network, no key) and
   from spec §3.3/§6. The MCP server: how to register it with an MCP client (a generic JSON config snippet), the two tool
   schemas exactly as `tools/list` returns them (run the binary with a scripted `initialize` + `tools/list` on stdin),
   and the error behaviour (spec §5).
3. Every command, flag, text and schema in the docs must match the binary's real output; list in your report which
   outputs you captured and how.

## Acceptance
The captured outputs (in your report) match the docs byte for byte for every quoted text.
