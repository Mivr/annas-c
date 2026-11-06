# Rules for coding agents in annas-c

The rules in [c-rigor-gate/AGENTS.md](https://github.com/Mivr/c-rigor-gate/blob/main/AGENTS.md) apply in full.
Product-specific additions:

1. **Clean room.** Implement from [docs/spec.md](docs/spec.md) only. Do not read, copy or paraphrase the source of
   `iosifache/annas-mcp` or any fork (including any local clone such as `/var/tmp/src/annas-mcp`).
2. **No secrets anywhere.** The member key is never in the repo, tests, fixtures, logs, error messages or reports.
   Tests use the fake transport and recorded fixtures only. Agents never make live requests to Anna's Archive;
   live checks are run by the orchestrator only.
3. **No bot-protection evasion.** No code that solves, spoofs or works around DDoS-Guard, captchas, JS challenges,
   cookies from challenge pages or TLS fingerprinting. A challenge is detected and reported as
   `E_BROWSER_CHECK`.
4. **Architecture.** `core` (arena, strings, buffers) <- `json`, `html`, `url` <- `annas` (pure: builds requests,
   parses responses) <- `cli`, `mcp`. IO only through ports: `transport` (HTTP GET with status, headers, body
   stream), `fs` (create/write/rename/remove in the download dir), `env`, `clock`. Adapters (libcurl, POSIX fs)
   are separate modules. Nothing below `cli`/`mcp` touches IO directly.
5. **Parsers are the strictest tier.** `json`, `html`, `url` and the filename sanitizer each get a fuzz target,
   full branch coverage and, where the gate asks, CBMC proofs.
