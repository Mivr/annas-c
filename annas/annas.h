/* annas/annas.h: the pure v1 operations of annas-c (spec sections 0, 1, 2, 3.3, 5, 6). Everything that touches the
 * outside world arrives through the ports (stream, fs, clock; env only for the configuration). No globals, no
 * malloc: all memory is the caller's workspace and result structs.
 *
 *   annas_config_load        ports_env_t -> annas_config_t   (spec section 1; the `.env` FILE belongs to `cli`)
 *   annas_ctx_init           binds config + ports + a workspace
 *   annas_fast_download_url  the member JSON API call (spec 3.3 step 2): URL + account info
 *   annas_download           the API call, then GET download_url streamed into the fs port (spec 3.3 steps 2-4)
 *   annas_format_*           the CLI/MCP texts; annas_error_line, annas_exit_code (spec 5, 6)
 *
 * ---- Common rules -------------------------------------------------------------------------------------------
 * Codes.  Every operation returns an annas_code_t (ANNAS_OK or an error) and fills `err` (always: on success
 *   err->code == ANNAS_OK, message "" and message_len 0, http_status 0). `err` may NOT be NULL. On failure the
 *   result struct is left byte-for-byte unchanged; on success it is fully (re)written.
 * Messages.  err->message is one line, NUL-terminated, never longer than ANNAS_MSG_MAX-1 bytes, and holds the text
 *   AFTER the code name; the code name is not part of it (see annas_error_line). "Exact" below means byte-exact.
 *   Every message is finalised by the same three steps, in this order, on the complete text:
 *     1. every byte < 0x20 and 0x7f becomes '?' (so a message never contains a newline);
 *     2. every non-overlapping occurrence (left to right) of the configured KEY becomes "REDACTED", then every one
 *        of pct_encode(KEY) (url_pct_encode) if that differs from the key itself;
 *     3. nothing else. (Server-supplied texts, see E_API, are additionally passed through url_redact_key BEFORE
 *        step 2 and BEFORE they are cut to their length limit.)
 *   So the member key (raw or percent-encoded) never appears in any message or result string, whatever the server,
 *   the title or the environment echoes. The key is sent only in the API request line to the configured base
 *   host; it is never part of any other request (download hosts, redirects).
 * Timeout.  An operation takes `start = clock.now_ms()` when it begins and has the absolute deadline
 *   start + cfg->timeout_ms (saturating). The deadline is passed to every stream connect, and annas ALSO compares
 *   the clock with it before every port call (connect, stream write, stream read, fs create_temp, fs write, fs
 *   commit): now >= deadline -> ANNAS_E_TIMEOUT. A PORTS_E_TIMEOUT from a port gives the same result. It is not
 *   checked once the last port call is done.
 * Streams.  One connection per exchange: connect(host, port), write the request, read until the HTTP message is
 *   complete (for a read-until-close body: until EOF), close. annas does NOT read again after the message is
 *   complete (a Content-Length or chunked message needs no EOF step), and ALWAYS closes a stream it opened, on
 *   success and on every failure. Request bytes are exactly http_request_write's output with the version
 *   ANNAS_VERSION ("GET <target> HTTP/1.1", Host, User-Agent: annas-c/0.1, Accept, Connection: close).
 *
 * ---- Error table (code: message, http_status) ------------------------------------------------------------------
 *  E_ARG      "md5 must be 32 hexadecimal characters"
 *             "format must consist of 1 to 16 ASCII letters or digits"
 *             "timeout must be 0 or more seconds, got: <n>"             (annas_config_load, n < 0)
 *             "timeout must be at most 31536000 seconds, got: <n>"
 *             "invalid argument"                                          (NULL pointer argument)
 *  E_CONFIG   "ANNAS_BASE_URL is not a valid domain, got: <raw value>"
 *             "ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set"
 *             "ANNAS_DOWNLOAD_PATH must be an absolute path, got: <path>"
 *             "ANNAS_SECRET_KEY is longer than 256 bytes"   "ANNAS_DOWNLOAD_PATH is longer than 256 bytes"
 *             (echoed values are cut to their first 200 bytes after the common finalisation)
 *  E_BROWSER_CHECK  "the site requires browser verification; use download-by-md5 with a hash found in a browser"
 *             (http_detect_challenge on the API response and on every download response, head + body prefix)
 *  E_API      "<status>: <detail>", http_status = status, from the API response (see annas_fast_download_url)
 *  E_HTTP     "unexpected HTTP status <n>"            download response that is not 200 and not a redirect;
 *             "too many redirects"                    the 6th redirect response;
 *             "redirect without a Location header"    (missing or empty);
 *             "redirect to a non-https URL";          "invalid redirect location"   (any other bad Location)
 *             http_status = the status of the response that caused it
 *  E_NET      "connection failed"                     connect failed (PORTS_E_NET, CLOSED, ARG)
 *             "TLS error"                             PORTS_E_TLS from any port call
 *             "network error"                         PORTS_E_NET/CLOSED/ARG from a write or read
 *             "connection closed before the response was complete"      EOF/HTTP_E_TRUNCATED
 *  E_TIMEOUT  "operation timed out"
 *  E_FS       "could not create a temporary file"   "could not write the file"   "could not save the file"
 *             "file already exists: <final name>"       (commit returned PORTS_FS_E_EXISTS)
 *  E_PARSE    "malformed HTTP response"               any other http parser error, bytes after the message
 *             "API response is larger than 65536 bytes"
 *             "download URL must be an absolute https URL"   (annas_download only, also for redirect targets)
 *             "download URL is longer than 2048 bytes"
 *             "account info is larger than 4096 bytes"
 *             "download URL contains the secret key"    (the decoded download_url contains the key, raw or
 *             "account info contains the secret key"     percent-encoded; the compact account JSON does)
 */
#ifndef ANNAS_ANNAS_H
#define ANNAS_ANNAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/buf.h"
#include "core/str.h"
#include "ports/ports.h"

#define ANNAS_VERSION "0.1"

#define ANNAS_MSG_MAX ((size_t)768)
#define ANNAS_BASE_MAX ((size_t)254)           /* char array size: 253 + NUL */
#define ANNAS_KEY_MAX ((size_t)256)            /* longest key accepted by annas_config_load */
#define ANNAS_DIR_MAX ((size_t)256)            /* longest download dir accepted */
#define ANNAS_URL_MAX ((size_t)2048)           /* longest download_url returned by the API */
#define ANNAS_ACCOUNT_MAX ((size_t)4096)       /* longest compact account JSON */
#define ANNAS_NAME_MAX ((size_t)256)           /* sanitised title (<=200) + '.' + format (<=16), + NUL */
#define ANNAS_PATH_MAX ((size_t)512)           /* dir + '/' + name, + NUL */
#define ANNAS_API_BODY_MAX ((size_t)65536)     /* largest API response body */
#define ANNAS_WORKSPACE_MIN ((size_t)262144)   /* smallest workspace accepted by annas_ctx_init */
#define ANNAS_DEFAULT_TIMEOUT_SECONDS 3600
#define ANNAS_MAX_TIMEOUT_SECONDS 31536000

typedef enum {
    ANNAS_OK = 0,
    ANNAS_E_ARG,
    ANNAS_E_CONFIG,
    ANNAS_E_BROWSER_CHECK,
    ANNAS_E_HTTP,
    ANNAS_E_API,
    ANNAS_E_NET,
    ANNAS_E_TIMEOUT,
    ANNAS_E_FS,
    ANNAS_E_PARSE
} annas_code_t;

typedef struct {
    annas_code_t code;
    int http_status;               /* 0 unless the error is tied to an HTTP response (E_API, E_HTTP) */
    size_t message_len;            /* strlen(message) */
    char message[ANNAS_MSG_MAX];
} annas_error_t;

/* "E_ARG", "E_CONFIG", "E_BROWSER_CHECK", "E_HTTP", "E_API", "E_NET", "E_TIMEOUT", "E_FS", "E_PARSE"; "OK" for
 * ANNAS_OK; "E_UNKNOWN" for any other value. Never NULL. */
const char *annas_code_name(annas_code_t code);

/* Spec section 6: OK 0; E_ARG, E_CONFIG 2; E_BROWSER_CHECK 3; E_HTTP, E_API 4; E_NET, E_TIMEOUT 5; E_FS 6;
 * E_PARSE 7. Any other value: 1. */
int annas_exit_code(annas_code_t code);

/* Appends "annas-c: <CODE>: <message>" (no trailing newline) to `out`, e.g.
 * "annas-c: E_API: 401: Invalid secret key". Returns false (appending nothing is not promised) when `err` or `out`
 * is NULL, err->code is ANNAS_OK, or `out` overflowed. */
bool annas_error_line(const annas_error_t *err, core_buf_t *out);

/* ---- configuration (spec section 1) -------------------------------------------------------------------------- */

typedef struct {
    char base[ANNAS_BASE_MAX]; /* normalised domain, NUL-terminated */
    size_t base_len;
    core_str_t key;            /* slice into the env's storage; len 0 when not required and unset */
    core_str_t download_dir;   /* slice into the env's storage; as written (not slash-stripped) */
    uint64_t timeout_ms;       /* timeout_seconds * 1000; 3600000 for 0 */
} annas_config_t;

/* Reads ANNAS_BASE_URL, ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH (exact, case-sensitive names) from `env`.
 * Checks, in this order, returning the first failure (with `*cfg` unchanged):
 *   1. timeout_seconds < 0 or > ANNAS_MAX_TIMEOUT_SECONDS -> E_ARG; 0 means ANNAS_DEFAULT_TIMEOUT_SECONDS.
 *   2. base: ANNAS_BASE_URL unset or empty -> URL_DEFAULT_BASE; otherwise url_base_normalize, and the result must
 *      also satisfy http_host_valid -> else E_CONFIG (message echoes the RAW value).
 *   3. only when `need_download`: key or download path unset or empty -> E_CONFIG (the combined message);
 *      key longer than ANNAS_KEY_MAX or path longer than ANNAS_DIR_MAX -> E_CONFIG; path not starting with '/'
 *      -> E_CONFIG (absolute-path message).
 * When !need_download the key and path are copied if set (no checks at all) and never required.
 * The key is known from the env even when an earlier check fails, so it is redacted from that message.
 * `err` and `cfg` must not be NULL (E_ARG "invalid argument" if err is non-NULL and cfg is). */
annas_code_t annas_config_load(ports_env_t env, bool need_download, int64_t timeout_seconds, annas_config_t *cfg,
                               annas_error_t *err);

/* ---- context --------------------------------------------------------------------------------------------------- */

/* The three ports an operation needs, grouped so annas_ctx_init stays within 6 parameters. Passed by value. */
typedef struct {
    ports_stream_t stream;
    ports_fs_t fs;
    ports_clock_t clock;
} annas_ports_t;

/* Binds `cfg` (kept by pointer: must outlive the context, and its key/dir slices too), the three ports (`ports`,
 * copied by value; a port whose ops table or any of its functions is NULL makes it fail, as before) and `workspace[0..cap)` (all scratch memory: parser, IO buffer, API body, JSON arena; it need not be
 * zeroed). Returns false (touching nothing that matters) when an argument is NULL, a port table pointer is NULL,
 * `cap < ANNAS_WORKSPACE_MIN`, or the workspace is not aligned to 16 bytes. A context is used by one operation at
 * a time. The ctx is a plain struct the caller allocates; treat its members as private. */
typedef struct {
    const annas_config_t *cfg;
    ports_stream_t stream;
    ports_fs_t fs;
    ports_clock_t clock;
    unsigned char *work;
    size_t work_cap;
    uint64_t deadline_ms;       /* implementation use */
    unsigned priv[8];           /* implementation use */
} annas_ctx_t;

bool annas_ctx_init(annas_ctx_t *ctx, const annas_config_t *cfg, annas_ports_t ports, void *workspace, size_t cap);

/* ---- operations ------------------------------------------------------------------------------------------------ */

/* `md5`: 32 hex digits of either case, normalised to lower case. `title`/`format`: len 0 (ptr may be NULL) means
 * absent; they are only used by annas_download. `format`, when present, must be 1..16 ASCII letters/digits
 * (E_ARG otherwise; "tar.gz" is refused) and is lower-cased. path_index/domain_index are sent only when has_*. */
typedef struct {
    core_str_t md5;
    core_str_t title;
    core_str_t format;
    bool has_path_index;
    uint64_t path_index;
    bool has_domain_index;
    uint64_t domain_index;
} annas_request_t;

typedef struct {
    char url[ANNAS_URL_MAX + 1u];            /* download_url from the API, as given (NUL-terminated) */
    size_t url_len;
    char account[ANNAS_ACCOUNT_MAX + 1u];    /* account_fast_download_info as compact JSON (json_write); "" if absent */
    size_t account_len;
} annas_fast_result_t;

typedef struct {
    annas_fast_result_t fast;                /* what the API returned */
    char name[ANNAS_NAME_MAX];               /* final file name "<fsname(title or md5)>.<format or bin>" */
    size_t name_len;
    char path[ANNAS_PATH_MAX];               /* dir (trailing '/'s removed) + "/" + name */
    size_t path_len;
    uint64_t bytes;                          /* body bytes written */
} annas_download_result_t;

/* The API call. Order of work: (1) config: key (and for annas_download the download dir) must be non-empty, else
 * E_CONFIG with the combined message; (2) request validation -> E_ARG: the fast call validates only the md5 and
 * ignores title and format (annas_download also validates format, md5 first); (1) and (2) happen before ANY port
 * call. (3) connect(base, 443, deadline); write
 *   GET /dyn/api/fast_download.json?md5=<md5>&key=<pct_encode(key)>[&path_index=<n>][&domain_index=<n>] HTTP/1.1
 * with Host: <base>; (4) read the whole response (redirects are NOT followed); more than ANNAS_API_BODY_MAX body
 * bytes -> E_PARSE; any http parser error -> E_PARSE, EOF too early -> E_NET; (5) http_detect_challenge(status,
 * Location, first 1024 body bytes) -> E_BROWSER_CHECK; (6) mapping, with `detail` as follows:
 *   - status 200 or 204 and the body is a JSON OBJECT with a non-empty string "download_url" -> success. (A real
 *     204 carries no body: the http parser refuses a 204 body as framing, so a 204 can only fail here.)
 *     The url longer than ANNAS_URL_MAX -> E_PARSE. account_fast_download_info, when it is a JSON object, is
 *     stored compactly (members in source order, numbers verbatim) in `account`; any other type or absence gives
 *     "" ; a compact form longer than ANNAS_ACCOUNT_MAX -> E_PARSE. Unknown keys are ignored (such as
 *     "///download_url"). No validation of the URL's scheme here.
 *   - otherwise E_API with message "<status>: <detail>" and http_status = status, where detail is
 *       * the body, when it is non-empty and NOT valid JSON (json_parse with depth <= 16, strings <= 8192 bytes,
 *         <= 1024 values; any failure counts as "not JSON"): the first 512 bytes of the body after redaction;
 *       * else the "error" member when it is a non-empty string: its first 200 bytes after redaction;
 *       * else (empty body, no/empty/non-string error, root not an object) "API returned empty download URL".
 *     (This also covers 200/204 with download_url null, "", missing or not a string.) The cut is applied AFTER
 *     redaction, so no part of a key straddling the cut survives.
 * `out`, `err`, `ctx`, `req` NULL -> E_ARG "invalid argument". */
annas_code_t annas_fast_download_url(annas_ctx_t *ctx, const annas_request_t *req, annas_fast_result_t *out,
                                     annas_error_t *err);

/* The API call above (same E_* rules, including the E_CONFIG check for key AND dir), then:
 *   - the URL must pass http_url_split (https only, any host/port) else E_PARSE;
 *   - GET it: connect(host, port, deadline), request line target = path+query, NO key anywhere;
 *   - a redirect (http_is_redirect_status): first http_detect_challenge, then http_redirect_resolve; follow it
 *     on a NEW connection to the new host. At most 5 redirects are followed; the 6th redirect response is
 *     E_HTTP "too many redirects". The key is never sent. Redirect bodies are not read (CL 0 in tests);
 *   - any other status than 200: first http_detect_challenge with the body (annas reads the body up to
 *     HTTP_CHALLENGE_PREFIX bytes or the end of the message, whichever is first), then E_HTTP "unexpected HTTP
 *     status <n>". Nothing is created in the fs for these;
 *   - 200: ONLY NOW create_temp. Each stream read that carries body bytes causes exactly ONE fs write with exactly
 *     those bytes, in order, for Content-Length and read-until-close bodies (a chunked body: one write per body
 *     slice the parser yields). Body bytes are never buffered whole. A zero-length 200 body commits an empty file.
 *     At the end of the message: commit(temp, name) with name = fsname_sanitize(title, or the md5 when the title is
 *     absent) + "." + (format lower-cased, or "bin");
 *   - EVERY failure after create_temp calls discard(temp) once; nothing is committed, no partial file remains
 *     (if discard itself fails the original error is still the one reported). commit == PORTS_FS_E_EXISTS ->
 *     E_FS "file already exists: <name>", the existing file untouched;
 *   - the stream is closed before the next connect and at the end.
 * On success: out->fast as in annas_fast_download_url, name, path, bytes (the body bytes written). */
annas_code_t annas_download(annas_ctx_t *ctx, const annas_request_t *req, annas_download_result_t *out,
                            annas_error_t *err);

/* ---- output (spec 3.3, 5) ---------------------------------------------------------------------------------------- */

/* Appends "Download URL: <url>" and, when account_len > 0, "\nAccount: <account>". No trailing newline.
 * Returns false when an argument is NULL or `out` overflowed. */
bool annas_format_download_url(core_buf_t *out, const annas_fast_result_t *r);

/* Appends "Book downloaded successfully to: <path>" and, when fast.account_len > 0, "\nAccount: <account>".
 * No trailing newline. Returns false when an argument is NULL or `out` overflowed. */
bool annas_format_downloaded(core_buf_t *out, const annas_download_result_t *r);

#endif
