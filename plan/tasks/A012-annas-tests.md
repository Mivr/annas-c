# A012: `annas` module (pure v1 operations): header, BUCK, tests, failing stubs (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A012`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§0, §1, §2, §3.3, §5 tools, §6 exit codes, §7), `fixtures/live/`, and the headers it builds on:
`core/*.h`, `json/json.h`, `url/url.h`, `fsname/fsname.h`, `http/http.h`, `ports/ports.h`, `ports/fakes.h`.

## Allowed paths
`annas/**`. Witness and scratch under `/var/tmp/cache/A012/` only.

## Module (pure: all IO through the ports; no globals)
- **Config**: from a `ports_env_t` (the `.env` file reading belongs to `cli`, not here): base URL, key, download
  dir presence/absoluteness checks, timeout; exact `E_CONFIG` messages from spec §1.
- **`annas_fast_download_url(ctx, md5, path_index?, domain_index?, &result)`**: builds the request (url module),
  connects via `ports_stream_t` to the base host on 443, writes the request (http module), parses the response
  (http module) into an arena, maps it (json module) per spec §3.3 step 2 into either `download_url` +
  `account_fast_download_info` (kept as compact JSON text) or an error. Challenge detection -> `E_BROWSER_CHECK`.
  Redirects are **not** followed for the API call (a redirect is `E_API <status>`).
- **`annas_download(ctx, md5, title?, format?, path_index?, domain_index?, &result)`**: the call above, then
  `GET download_url` (https only; follows up to 5 redirects; any host), streaming the body through
  `ports_fs_t` into a temp file and committing it as `<fsname(title or md5)>.<format or bin, lower-cased>`;
  on any failure the temp file is discarded and nothing remains. Body bytes are never buffered whole.
- **Timeouts**: one deadline (ms) for the whole operation from `ports_clock_t`; exceeded -> `E_TIMEOUT`.
- **Errors**: a result struct with the spec §6 code and the exact one-line message (key always redacted).
- **Output formatting** (pure functions into a `core_buf_t`): the CLI/MCP texts of spec §3.3 and §5
  (`Book downloaded successfully to: <path>`, `Download URL: <url>`, `Account: <json>`).

## Tests (with the `ports` fakes; never real IO)
Scripted-stream exchanges with exact expected request bytes (method line, Host, User-Agent, `Connection: close`,
key placement), the live fixtures (401 invalid key, 404 record not found, 302 `check=1`), 200 and 204 success
bodies (write realistic ones per the API doc text in the fixtures), every error mapping and message, the key never
appearing in any message or recorded output, malformed/oversized JSON, download streaming in 1-byte and large
chunks, redirect chains of 0..5 and 6, http:// download URLs rejected, fs failures at every write (temp discarded,
nothing committed), name collisions (`EXISTS`), title sanitisation applied, timeouts at each phase via the manual
clock. Positive controls for every rejection. Non-recursive, frames <= 512 bytes.

## Stubs and acceptance
As A001. Report API, tests per file, stub/witness summaries.
