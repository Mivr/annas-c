# annas-c

A command-line client and MCP (Model Context Protocol) stdio server for Anna's Archive, written in C17.

It is a clean-room reimplementation of the behaviour of
[iosifache/annas-mcp](https://github.com/iosifache/annas-mcp), written from a functional spec
([docs/spec.md](docs/spec.md)) without copying its code. It is also the first product graded by
[c-rigor-gate](https://github.com/Mivr/c-rigor-gate), a gate designed to make code written by AI coding agents
trustworthy and hard to game.

Status: not usable yet (phase 1 under construction).

## Scope
- Download by md5 through the member fast-download JSON API (needs your own member key).
- Book and article search through the site's HTML pages, with selectors kept in one table. When the site answers
  with a browser-verification challenge, the tool says so clearly and stops. It does **not** attempt to bypass
  bot protection.
- DOI lookup and article download.
- `annas-c mcp`: MCP server over stdio exposing the same operations as tools.

## Configuration
| Variable | Meaning |
|---|---|
| `ANNAS_SECRET_KEY` | Member API key (downloads only). Never logged. |
| `ANNAS_DOWNLOAD_PATH` | Absolute directory for downloads. |
| `ANNAS_BASE_URL` | Mirror domain, for example `annas-archive.gl`. |

License: MIT OR Apache-2.0.
