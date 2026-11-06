# A008: `http` module: header, BUCK, tests, failing stubs (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A008`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§0, §2, §3.3), `core/*.h`, `url/url.h`, `json/` for the house style.

## Allowed paths
`http/**`. Witness and all scratch under `/var/tmp/cache/A008/` only; never commit the witness.

## Module (pure: no IO, no sockets; it works on bytes the caller feeds it)
- **Request writer**: `GET <path-and-query> HTTP/1.1` with `Host`, `User-Agent: annas-c/<version>`,
  `Accept`, `Connection: close`; refuses CR/LF/NUL in any component; into a `core_buf_t`.
- **URL split**: `https://host[:port]/path?query` into host, port (default 443), target; only `https` accepted
  (an `http://` download URL is `HTTP_E_SCHEME`); host must be a valid DNS name or IPv4 literal; no userinfo.
- **Response parser**: an incremental (push) parser: the caller feeds arbitrary chunks (1 byte at a time must
  work) and gets events: status (code, reason), each header (name lower-cased, value trimmed), end of headers,
  body bytes, end of message. Handles `Content-Length`, `Transfer-Encoding: chunked` (with chunk extensions and
  trailers), and read-until-close; rejects conflicting/duplicate `Content-Length`, CL + chunked together,
  obsolete line folding, bare LF line endings (strict CRLF), header lines > 8 KiB, more than 100 headers, bad
  chunk sizes and overflow, a body longer than Content-Length. Bounded memory (no arena growth per body byte).
- **Redirect helper**: given a 301/302/303/307/308 and its `Location`, produce the next absolute https URL
  (relative locations resolved against the current URL); cap of 5 enforced by a counter struct.
- **Challenge detection** per spec §2 (403/503 with `ddos-guard` in a body prefix, or a redirect whose
  `Location` contains `check=1`) as a pure function -> `HTTP_E_BROWSER_CHECK`.

## Tests
A001 style, adversarial (this parses data from the network): exact event sequences, every error with its
condition, chunk boundaries at every byte position for a fixed response (feed-split exhaustive test), limits at
N-1/N/N+1, request-smuggling shapes (CL+TE, duplicate CL, `Transfer-Encoding: chunked, identity`, header names
with spaces), the live redirect from `fixtures/live/search_302_check.html` (status 302 + Location with
`check=1`), and positive controls for every rejection. Non-recursive, frames <= 512 bytes.

## Stubs and acceptance
As A001. Report API, tests per file, stub/witness summaries.
