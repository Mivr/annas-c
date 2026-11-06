/* annas/tests/kit.h: shared scaffolding for the annas tests (test-only library, not part of the product).
 * One static kit per test process (every ctest case runs in a fresh fork, so state never leaks between cases).
 * The kit builds scripted-stream exchanges, an in-memory fs, a manual clock and an annas context, and offers
 * the common post-conditions. It talks to annas only through annas/annas.h. */
#ifndef ANNAS_TESTS_KIT_H
#define ANNAS_TESTS_KIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "annas/annas.h"
#include "ctest/ctest.h"
#include "ports/fakes.h"

#define KIT_MAX_STEPS 96u
#define KIT_POOL ((size_t)200000)
#define KIT_FS_DEFAULT ((size_t)(8u * 65536u))
#define KIT_KEY "FAKEKEY-not-a-real-key-0123456789"
#define KIT_MD5 "0123456789abcdef0123456789abcdef"
#define KIT_START_MS ((uint64_t)5000)

typedef struct {
    annas_config_t cfg;
    char key[64];
    char key_pct[192];
    char dir[300];
    ports_fake_step_t steps[KIT_MAX_STEPS];
    size_t n_steps;
    unsigned ok_connects;
    char pool[KIT_POOL];
    size_t pool_len;
    char scratch[24576];
    ports_fake_clock_t clock;
    uint64_t read_advance;
    ports_fake_stream_t stream;
    ports_fake_fs_t fs;
    unsigned char *fs_store;
    size_t fs_cap;
    unsigned char fs_default[KIT_FS_DEFAULT];
    unsigned preloaded;
    bool prepared;
    bool allow_unused;
    _Alignas(64) unsigned char work[ANNAS_WORKSPACE_MIN];
    annas_ctx_t ctx;
    annas_error_t err;
    annas_fast_result_t fast, fast_before;
    annas_download_result_t dl, dl_before;
} kit_t;

kit_t *kit_begin(void);
void kit_set_key(kit_t *k, const char *key);
void kit_set_base(kit_t *k, const char *base);
void kit_set_dir(kit_t *k, const char *dir);
void kit_set_timeout_s(kit_t *k, uint64_t s);
void kit_use_fs(kit_t *k, void *storage, size_t cap);

/* Copies n bytes into the pool, NUL-terminates, returns the stable pointer (NULL if the pool is full). */
const char *kit_put(kit_t *k, int n);
/* Copies n bytes of s into the pool. */
const char *kit_put_str(kit_t *k, const char *s, size_t n);
#define KIT_FMT(k, ...) kit_put((k), snprintf((k)->scratch, sizeof (k)->scratch, __VA_ARGS__))

/* script */
void kit_connect(kit_t *k, const char *host, uint16_t port);
void kit_connect_err(kit_t *k, const char *host, uint16_t port, ports_err_t e);
void kit_expect(kit_t *k, const char *bytes);
void kit_respond(kit_t *k, const char *bytes, size_t chunk);
void kit_respond_n(kit_t *k, const char *bytes, size_t len, size_t chunk);
void kit_eof(kit_t *k);
void kit_fail_write(kit_t *k, ports_err_t e);
void kit_fail_read(kit_t *k, ports_err_t e);
/* exact API request for the configured base and key; extra is "" or "&path_index=1" etc. */
void kit_expect_api(kit_t *k, const char *md5, const char *extra);
/* exact GET for a non-API URL: no key; Host has ":port" when port != 443 */
void kit_expect_get(kit_t *k, const char *host, uint16_t port, const char *target);
/* "HTTP/1.1 <status> <reason>\r\n<hdrs>Content-Length: n\r\nConnection: close\r\n\r\n<body>"; hdrs "" or lines ending CRLF */
const char *kit_http(kit_t *k, int status, const char *reason, const char *hdrs, const char *body);
/* head only, Content-Length announced as len */
const char *kit_head(kit_t *k, int status, const char *reason, const char *hdrs, size_t len);
/* {"download_url":"<url>","account_fast_download_info":<account>} (account NULL: omitted) */
const char *kit_ok_json(kit_t *k, const char *url, const char *account);
/* connect(base,443) + exact API request + one RESPOND with `response` */
void kit_api(kit_t *k, const char *md5, const char *extra, const char *response);
/* connect(host,port) + exact GET + one RESPOND */
void kit_get(kit_t *k, const char *host, uint16_t port, const char *target, const char *response);

/* running */
bool kit_prepare(kit_t *k);
bool kit_preload(kit_t *k, const char *name, const char *content);
annas_code_t kit_fast(kit_t *k, const annas_request_t *req);
annas_code_t kit_download(kit_t *k, const annas_request_t *req);
annas_request_t kit_req(const char *md5);

/* post-conditions (each is several CHECKs) */
void kit_check_ok(ctest_t *t, kit_t *k, annas_code_t got);
void kit_check_err(ctest_t *t, kit_t *k, annas_code_t got, annas_code_t want, int status, const char *msg);
void kit_check_fs_untouched(ctest_t *t, kit_t *k);
void kit_check_file(ctest_t *t, kit_t *k, const char *name, const void *content, size_t len);
/* no committed file other than the preloaded ones, no temp file; discards expected */
void kit_check_no_partial(ctest_t *t, kit_t *k, uint32_t discards);
bool kit_contains(const char *hay, const char *needle);

/* fixtures/live/ captures, byte for byte (1083, 1081 and 283 bytes) */
extern const char kit_fixture_401[];
extern const char kit_fixture_404[];
extern const char kit_fixture_302[];

#endif
