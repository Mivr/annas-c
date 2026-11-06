/* app/tests/kit.h: shared scaffolding for the app tests (test-only library, not part of the product).
 * One static kit per test process (every ctest case runs in a fresh fork). It sits on the annas test kit (scripted
 * stream, in-memory fs, manual clock) and adds the layered env, deps, output buffers and an MCP server. It talks to
 * app only through app/app.h. */
#ifndef APP_TESTS_KIT_H
#define APP_TESTS_KIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "annas/tests/kit.h"
#include "app/app.h"
#include "ctest/ctest.h"

#define AK_OUT_CAP ((size_t)65536)
#define AK_PARSE_CAP ((size_t)(3u * 1024u * 1024u))
#define AK_LINE_CAP (APP_MCP_LINE_MAX + 64u)
#define AK_ENV_MAX 8u
#define AK_DOTENV_MAX 4096u
#define AK_MD5 KIT_MD5

typedef struct {
    kit_t *k;
    ports_fake_env_entry_t ent[AK_ENV_MAX];
    size_t n_ent;
    ports_fake_env_t env;
    char dotenv[AK_DOTENV_MAX];
    size_t dotenv_len;
    bool has_dotenv;
    app_dotenv_t denv;
    app_env_layer_t layer;
    app_deps_t deps;
    bool deps_ready;
    char out_store[AK_OUT_CAP];
    char err_store[AK_OUT_CAP];
    core_buf_t out;
    core_buf_t err;
    app_mcp_t mcp;
    bool mcp_ready;
    unsigned char parse_mem[AK_PARSE_CAP];
    char line[AK_LINE_CAP];
} ak_t;

/* annas kit_begin plus the default env (key KIT_KEY, download dir /dl) and empty output buffers. */
ak_t *ak_begin(void);
/* Set (or replace) a process env variable; value NULL removes it. Call before the first ak_deps / ak_cli. */
void ak_env(ak_t *a, const char *name, const char *value);
/* Content of a `.env` file layered under the process env. Call before the first ak_deps / ak_cli. */
void ak_dotenv(ak_t *a, const char *text);
/* Wires the layered env, the ports of the annas kit and its workspace; idempotent. */
const app_deps_t *ak_deps(ak_t *a);
/* Empty both output buffers. */
void ak_clear(ak_t *a);

/* Parse (must succeed, else returns -1000) and run; outputs in ak_out / ak_err. */
int ak_cli(ak_t *a, int argc, const char *const *argv);
#define AK_ARGC(...) ((int)(sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *)))
#define AK_CLI(a, ...) ak_cli((a), AK_ARGC("annas-c", __VA_ARGS__), (const char *const[]){"annas-c", __VA_ARGS__})
const char *ak_out(ak_t *a);
const char *ak_err(ak_t *a);

/* MCP: handles one line (clears `out` first) with a server over the kit deps; reply text in ak_out. */
app_mcp_result_t ak_mcp(ak_t *a, const char *line);
app_mcp_result_t ak_mcp_n(ak_t *a, const char *bytes, size_t len);

/* Post-conditions. */
void ak_check_stream(ctest_t *t, ak_t *a);     /* no mismatch, every opened stream closed, script consumed */
void ak_check_no_io(ctest_t *t, ak_t *a);      /* no port call at all (stream untouched, fs untouched) */
/* Result is a REPLY holding exactly `want`, one line, the key absent. */
void ak_check_reply(ctest_t *t, ak_t *a, app_mcp_result_t got, const char *want);
/* The key (raw and percent-encoded) occurs in neither output buffer. */
void ak_check_no_key(ctest_t *t, ak_t *a);
/* Exit code, stdout, stderr exactly; stdout empty or stderr empty is asserted by passing "". */
void ak_check_cli(ctest_t *t, ak_t *a, int got, int want, const char *out, const char *err);

/* argv slices */
bool ak_eq(core_str_t s, const char *want);

/* JSON-RPC text builders (string literals only) */
#define AK_OKR(id, result) "{\"jsonrpc\":\"2.0\",\"id\":" id ",\"result\":" result "}"
#define AK_ERR(id, code, msg) "{\"jsonrpc\":\"2.0\",\"id\":" id ",\"error\":{\"code\":" code ",\"message\":\"" msg "\"}}"
/* tools/call results: the text is already JSON-escaped */
#define AK_TOOL_OK(text) "{\"content\":[{\"type\":\"text\",\"text\":\"" text "\"}],\"isError\":false}"
#define AK_TOOL_ERR(text) "{\"content\":[{\"type\":\"text\",\"text\":\"" text "\"}],\"isError\":true}"

#define AK_PROPS_COMMON                                                                                          \
    "\"path_index\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"Collection index, 0 or larger.\"},"   \
    "\"domain_index\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"Download server index, 0 or larger.\"}," \
    "\"timeout_seconds\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"Time limit in seconds (0 = server default of 3600).\"}"
#define AK_PROP_HASH \
    "\"hash\":{\"type\":\"string\",\"description\":\"MD5 hash of the book (32 hexadecimal characters).\"}"
#define AK_TOOLS_LIST                                                                                            \
    "{\"tools\":[{\"name\":\"book_download\",\"description\":\"Download a book by MD5 hash with a member account key and save it in the download directory.\"," \
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{" AK_PROP_HASH ","                                      \
    "\"title\":{\"type\":\"string\",\"description\":\"File name without extension (default: the hash).\"},"        \
    "\"format\":{\"type\":\"string\",\"description\":\"File extension, 1 to 16 letters or digits (default: bin).\"}," \
    AK_PROPS_COMMON "},\"required\":[\"hash\"],\"additionalProperties\":false}},"                                  \
    "{\"name\":\"fast_download_url\",\"description\":\"Get the fast download URL and the account's download quota for an MD5 hash without downloading the file.\"," \
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{" AK_PROP_HASH "," AK_PROPS_COMMON                         \
    "},\"required\":[\"hash\"],\"additionalProperties\":false}}]}"

#endif
