# Live captures (2026-11-06, orchestrator)

Captured with `curl` and the honest User-Agent `annas-c/0.1` from `annas-archive.gl`. No secrets.

| File | Request | Status | Notes |
|---|---|---|---|
| `search_302_check.html` | `GET /search?q=lakos&content=book_any` | 302 | `Location: /search?q=lakos&content=book_any&check=1` (browser verification) -> `E_BROWSER_CHECK` |
| `api_401_invalid_key.json` | `GET /dyn/api/fast_download.json?md5=000...0` (no key) | 401 | `error: "Invalid secret key"`, `download_url: null`, plus a `///download_url` documentation array |
| `api_404_record_not_found.json` | same with a valid member key | 404 | `error: "Record not found"` |

Real search/detail result pages cannot be fetched without a browser; synthetic fixtures follow spec §4 until
the owner supplies browser-saved pages.
