# A007: `html` module: header, BUCK, tests, failing stubs (test-author task)

Repo: `Mivr/annas-c`. Branch `task/A007`. Read `AGENTS.md`, `gate/AGENTS.md`, `plan/phase1.md`, `docs/dev.md`,
`docs/spec.md` (§4 entirely, §3.4 detail-page extraction), `core/*.h`, `fixtures/live/README.md`.

## Allowed paths
`html/**`. Witness under `/var/tmp/cache/A007/witness/` only; never commit it.

## Module
A **generic** HTML layer (no Anna's-specific selectors: those live in the later `annas` module's table):
- **Tokenizer** over `core_str_t`: start tags (name lower-cased, attributes with double-quoted, single-quoted
  and unquoted values, duplicate attributes: first wins), end tags, self-closing, text, comments, `<!doctype>`,
  raw-text elements (`script`, `style`) whose content is not parsed, character references in text and attribute
  values (`&amp; &lt; &gt; &quot; &#39; &apos; &nbsp; &#NN; &#xHH;`, invalid ones kept literally), void elements.
  Tolerant of unclosed tags and stray `<`. Non-recursive. Bounded: input <= 8 MiB, nesting <= 256 (beyond:
  `HTML_E_LIMIT`), arena exhaustion: `HTML_E_NOMEM`.
- **Tree** in a `core_arena_t`: elements with parent/child/sibling links, implied closing per a small fixed table
  (`p`, `li`, `td`, `tr`, `option`), mis-nested end tags closing up to the nearest matching open element and
  ignored if none.
- **Queries** sufficient for spec §4: find first/next descendant matching a compound selector of: tag name,
  `.class` (whitespace-separated class membership), `[attr]`, `[attr="v"]`, `[attr^="v"]`, `[attr*="v"]`, and
  exact class-attribute equality; parent access; `text` of a subtree (descendant text concatenated,
  whitespace-collapsed, trimmed, into a `core_buf_t`); attribute lookup. Selectors are built as plain structs in
  C (no selector-string parser needed).

## Tests
A001 style: exact tree shapes, exact texts, every tokenizer state with malformed input, every entity rule,
limits at N-1/N/N+1, and spec §4-shaped synthetic pages (a search-results page with several rows, a detail
page, the live `fixtures/live/search_302_check.html`) proving every selector the spec needs works, including
class attributes with extra whitespace and the icon-span patterns. Positive controls for every rejection.

## Stubs and acceptance
As A001. Report API, tests per file, stub/witness summaries.
