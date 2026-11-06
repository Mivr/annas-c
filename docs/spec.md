# annas-c functional spec (v1)

Written by the orchestrator from a behavioural study of `annas-mcp`; implementers work from this file only.
Where this spec deliberately differs from upstream it says **Deviation**.

## 0. Scope of v1 (owner decision 2026-11-06)
v1 exposes **only what the member JSON API supports**. Anna's Archive documents exactly one stable API,
`/dyn/api/fast_download.json` (see `fixtures/live/`); its HTML pages answer non-browser clients with a
browser-verification redirect, and its FAQ points custom search at the published ElasticSearch/MariaDB dumps.
So v1 has:
- `download <md5>` (§3.3) and its MCP tool `book_download`;
- `download-url <md5>` and MCP tool `fast_download_url`: the same API call, returning the `download_url`
  and the account info without fetching the file;
- the API's optional `path_index` and `domain_index` (integers >= 0) on both;
- the account's fast-download info (`account_fast_download_info`, printed as returned: its JSON object
  re-serialised compactly) after each successful call.
**Deferred to v2** (local index built from the published metadata dumps): §3.1, §3.2, §3.4, §3.5 and §4.
They stay in this document as the v2 design and must not be implemented in v1.

## 1. Configuration
- Sources, later wins: a `.env` file in the current directory (`KEY=VALUE` lines, `#` comments, optional
  double quotes), then the process environment.
- `ANNAS_BASE_URL`: a domain. Normalise by stripping a leading `http://` or `https://` and trailing `/`.
  Default `annas-archive.gl`. Must match `[a-z0-9.-]+` after normalising, else `E_CONFIG`.
- `ANNAS_SECRET_KEY`, `ANNAS_DOWNLOAD_PATH`: required only by download operations. Missing or empty:
  `E_CONFIG: ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set`.
  Relative path: `E_CONFIG: ANNAS_DOWNLOAD_PATH must be an absolute path, got: <path>`.
- Timeout: `--timeout <seconds>` (CLI, default 3600) and `timeout_seconds` (MCP, integer, 0 = server default,
  negative = `E_ARG`). The whole operation must finish within it, else `E_TIMEOUT`.
- The key is never printed, logged, or included in any error text (including URLs echoed in errors).

## 2. Transport
All requests are HTTPS GET to `https://<base>/...` with a fixed, honest `User-Agent: annas-c/<version>`.
**Deviation:** upstream sends a browser User-Agent; we do not impersonate a browser.

Challenge detection, applied to every HTML response before parsing, returns `E_BROWSER_CHECK`
("the site requires browser verification; use download-by-md5 with a hash found in a browser"):
- HTTP 403 or 503 whose body mentions `ddos-guard` (case-insensitive), or
- any redirect whose `Location` contains `check=1`.
Other non-2xx: `E_HTTP <status>`. Network failure: `E_NET`.

## 3. Operations
### 3.1 `book-search <query>` / MCP `book_search{query}`  *(v2, not in v1)*
`GET /search?q=<urlencoded query>&content=book_any`. Parse result rows (§4.1). Output for zero rows:
`No books found.` Otherwise for each row `Book <n>:` then, one per line, `Title:`, `Authors:`,
`Publisher:`, `Language:`, `Format:`, `Size:`, `URL:` (`https://<base>/md5/<hash>`), `Hash:`; blank line
between books.

### 3.2 `article-search <query>` / MCP `article_search{query}`  *(v2, not in v1)*
If the trimmed query starts with `10.` it is a DOI: do §3.4 lookup and print one block with `DOI:`, `Title:`,
`Authors:`, `Journal:`, `Size:`, `Hash:`, `Download URL:`, `Page:`; on failure `No paper found for DOI: <q>`.
Otherwise `GET /search?q=<q>&content=journal`, parse as §4.1 (publisher field = journal), print `Article <n>:`
blocks with the same fields as books; zero rows: `No articles found.`

### 3.3 `download <md5> [--name <title>] [--format <ext>]` / MCP `book_download{hash,title,format}`
**Deviation:** download by md5 is the primary, first-class operation (it works without HTML pages).
Upstream's CLI form `book-download <md5> <filename-with-ext>` is accepted as an alias (title = basename minus
extension, format = extension; no extension = `E_ARG`).
1. `md5` must be 32 hex characters (case-insensitive, normalised to lower case), else `E_ARG`.
2. `GET /dyn/api/fast_download.json?md5=<md5>&key=<key>`. The body is JSON (`fixtures/live/`): success is
   status **200 or 204** with a non-empty `download_url` (and an `account_fast_download_info` object); failures
   use other statuses with `download_url: null` and an `error` string. Unknown keys (such as the
   `///download_url` documentation array) are ignored. Map: status 200/204 with a non-empty string
   `download_url` -> step 3; any other status, or `download_url` null/empty, -> `E_API <status>: <error>`
   using the `error` string if present (at most 200 bytes), else `E_API <status>: API returned empty download URL`;
   a body that is not JSON -> `E_API <status>` with at most the first 512 bytes of the body. The key is
   redacted from every message.
3. `GET <download_url>` (any https host), stream to `<dir>/<sanitised title>.<format>` via a temporary file in
   the same directory, then rename. On any failure the temporary file is removed and no partial file remains.
   Format is lower-cased; empty format means `bin`. Default title is the md5.
4. Success: `Book downloaded successfully to: <path>`.

### 3.4 DOI lookup (used by article-search and article-download)  *(v2, not in v1)*
1. `GET /scidb/<doi>`; follow redirects (max 5). From the final HTML take the first `a[href^="/md5/"]`; its
   hash is the href without `/md5/`. None: not found.
2. `GET /md5/<hash>`. Title: text of `<title>` before ` - Anna`. Journal: `meta[name=description]` content
   split on a blank line; take part 3, else part 2, else the whole. Authors: first `a[href^="/search"]` that
   contains a `span` whose class includes `icon-[mdi--user-edit]`, text trimmed. Size: first
   `div.text-gray-500` whose text contains `MB` or `KB`, matched by `\d+(\.\d+)?\s*(KB|MB|GB|TB)`.
3. Download URL is `https://<base>/scidb?doi=<doi>`; Page is `https://<base>/md5/<hash>`.

### 3.5 `article-download <doi>` / MCP `article_download{doi}`  *(v2, not in v1)*
Lookup (§3.4), then §3.3 with the hash, title from lookup and format `pdf`. If that fails and the failure is not
`E_BROWSER_CHECK`, fall back to `GET /scidb?doi=<doi>` and save the body; extension from `Content-Disposition`
filename, else from `Content-Type` (`application/pdf` -> `.pdf`), else `.pdf`.
**Deviation:** `--md5 <hash>` skips the lookup entirely.

## 4. HTML extraction  *(v2, not in v1)*
All selectors live in one table in the `annas` module so a site change is a one-place edit plus new fixtures.
The HTML tokenizer must be tolerant (unclosed tags, attributes without quotes, entities `&amp; &lt; &gt; &quot;
&#NN; &#xHH;`) and bounded (input size cap 8 MiB, nesting cap 256; beyond: `E_PARSE`).

### 4.1 Search rows
- Row anchor: `a[href^="/md5/"]` whose `class` attribute is exactly
  `custom-a block mr-2 sm:mr-4 hover:opacity-80`. The row container is its parent element.
- In the container, the first `div.max-w-full`: Title = text of its first `a[href^="/md5/"]`;
  Authors = text of the parent of the `a[href^="/search"]` containing a span with class
  `icon-[mdi--user-edit]`; Publisher = same with `icon-[mdi--company]`.
- Meta = text of the first `div.text-gray-800`, split on ` · `; needs at least 3 parts or the meta fields stay
  empty. Language = part 1 text before ` [`, with a leading `✅ ` removed. Format = first match of
  `EPUB|PDF|MOBI|AZW3|AZW|DJVU|CBZ|CBR|FB2|DOCX|DOC|TXT` (case-insensitive, output upper-case).
  Size = first match of `\d+(\.\d+)?\s*(KB|MB|GB|TB)`.
- Hash = anchor href without `/md5/`. Rows with an empty title or hash are skipped. Text is whitespace-collapsed
  and trimmed.

## 5. MCP server (`annas-c mcp`)
Newline-delimited JSON-RPC 2.0 on stdin/stdout; logs to stderr only. Implement `initialize` (server name
`annas-c`, protocol version echoed from a supported list), `notifications/initialized`, `tools/list`,
`tools/call`, `ping`. v1 tools:
- `book_download{hash, title?, format?, path_index?, domain_index?, timeout_seconds?}` -> §3.3;
- `fast_download_url{hash, path_index?, domain_index?, timeout_seconds?}` -> the API call only; result text is
  `Download URL: <url>` then `Account: <compact JSON of account_fast_download_info>` (omitted if absent).
`hash` and `title` are strings, the rest integers >= 0. Results are one `text` content block holding the CLI
output; operation errors are returned as `isError: true` results with the error text (not JSON-RPC errors).
Malformed JSON: JSON-RPC `-32700`; unknown method `-32601`; bad params `-32602`. Line length cap 1 MiB.

## 6. Exit codes (CLI)
0 ok; 2 `E_ARG`/`E_CONFIG`; 3 `E_BROWSER_CHECK`; 4 `E_HTTP`/`E_API`; 5 `E_NET`/`E_TIMEOUT`; 6 `E_FS`;
7 `E_PARSE`. Errors print as one line `annas-c: <CODE>: <message>` on stderr.

## 7. Filename sanitiser
Input: title bytes. Replace each of `< > : " / \ | ? *` and every byte < 0x20 or == 0x7f with `_`; replace
every `..` with `_`; strip leading `.` and spaces and trailing spaces and dots; truncate to 200 bytes without
splitting a UTF-8 sequence; empty result becomes `untitled`. The result never contains `/`, never equals
`.` or `..`, and joined with the download dir never escapes it (this is a proof obligation).
