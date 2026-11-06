# A005: `url` and `fsname` modules: headers, BUCK, tests, failing stubs (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A005`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§1 base URL, §2, §3 request paths, §7 sanitiser), and `core/*.h`.

## Allowed paths
`url/**`, `fsname/**`. Witness under `/var/tmp/cache/A005/witness/` only; never commit it.

## `url`
- Percent-encoding of a query component (RFC 3986 unreserved kept, everything else `%XX` upper-case; space as
  `%20`), percent-decoding (malformed `%` sequences = error).
- `ANNAS_BASE_URL` normalisation per spec §1 (strip scheme and trailing `/`, validate `[a-z0-9.-]+`, default).
- Request URL builders for every request in spec §3 (`/search?q=..&content=..`, `/dyn/api/fast_download.json`,
  `/md5/<hash>`, `/scidb/<doi>`, `/scidb?doi=`), into a `core_buf_t`. The key must be encoded like any value.
- A redaction helper that replaces the value of a `key=` query parameter with `REDACTED` (spec §1: the key never
  appears in errors).
- md5 validation/normalisation (32 hex, lower-cased) and DOI recognition (trimmed, starts with `10.`).

## `fsname`
The sanitiser of spec §7, exactly. Tests must include adversarial titles: `../../etc/passwd`, `..`, `.`, `...`,
leading dots and spaces, trailing dots and spaces, every forbidden character, all control bytes, DEL, NUL inside
the slice, an all-forbidden title, an empty title, 199/200/201/1000-byte titles, multi-byte UTF-8 straddling
the 200-byte cut (2, 3 and 4-byte sequences), and invalid UTF-8. Also a property test: for 10 000 titles from a
seeded deterministic generator (bytes 0-255, lengths 0-400), the output is non-empty, <= 200 bytes, contains no
`/`, `\`, NUL or control byte, is not `.` or `..`, never starts with `.`, and never contains `..`.

## Stubs and acceptance
As A001. Report APIs, tests per file, stub/witness summaries.
