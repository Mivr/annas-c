#include "annas/tests/kit.h"

#include <stdio.h>
#include <string.h>

static void pct(const char *in, char *out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (; *in != '\0' && n + 4 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                     c == '.' || c == '_' || c == '~';
        if (plain) {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15u];
        }
    }
    out[n] = '\0';
}

static void sync_cfg(kit_t *k) {
    k->cfg.key = core_str_from_cstr(k->key);
    k->cfg.download_dir = core_str_from_cstr(k->dir);
    pct(k->key, k->key_pct, sizeof k->key_pct);
}

kit_t *kit_begin(void) {
    static kit_t g;
    memset(&g, 0, sizeof g);
    snprintf(g.key, sizeof g.key, "%s", KIT_KEY);
    snprintf(g.dir, sizeof g.dir, "%s", "/dl");
    snprintf(g.cfg.base, sizeof g.cfg.base, "%s", "annas-archive.gl");
    g.cfg.base_len = strlen(g.cfg.base);
    g.cfg.timeout_ms = 3600000u;
    sync_cfg(&g);
    ports_fake_clock_init(&g.clock, KIT_START_MS);
    g.fs_store = g.fs_default;
    g.fs_cap = sizeof g.fs_default;
    return &g;
}

void kit_set_key(kit_t *k, const char *key) {
    snprintf(k->key, sizeof k->key, "%s", key);
    sync_cfg(k);
}
void kit_set_base(kit_t *k, const char *base) {
    snprintf(k->cfg.base, sizeof k->cfg.base, "%s", base);
    k->cfg.base_len = strlen(k->cfg.base);
}
void kit_set_dir(kit_t *k, const char *dir) {
    snprintf(k->dir, sizeof k->dir, "%s", dir);
    sync_cfg(k);
}
void kit_set_timeout_s(kit_t *k, uint64_t s) { k->cfg.timeout_ms = s * 1000u; }
void kit_use_fs(kit_t *k, void *storage, size_t cap) {
    k->fs_store = (unsigned char *)storage;
    k->fs_cap = cap;
}

const char *kit_put(kit_t *k, int n) {
    if (n < 0 || (size_t)n >= sizeof k->scratch || k->pool_len + (size_t)n + 1u > KIT_POOL) {
        return NULL;
    }
    char *p = k->pool + k->pool_len;
    memcpy(p, k->scratch, (size_t)n);
    p[n] = '\0';
    k->pool_len += (size_t)n + 1u;
    return p;
}

const char *kit_put_str(kit_t *k, const char *s, size_t n) {
    if (n >= sizeof k->scratch) {
        return NULL;
    }
    memcpy(k->scratch, s, n);
    return kit_put(k, (int)n);
}

static ports_fake_step_t *push(kit_t *k, ports_fake_step_kind_t kind) {
    if (k->n_steps >= KIT_MAX_STEPS) {
        return NULL;
    }
    ports_fake_step_t *s = &k->steps[k->n_steps++];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    return s;
}

void kit_connect_err(kit_t *k, const char *host, uint16_t port, ports_err_t e) {
    ports_fake_step_t *s = push(k, PORTS_FAKE_STEP_CONNECT);
    if (s != NULL) {
        s->data = core_str_from_cstr(host);
        s->port = port;
        s->err = e;
        if (e == PORTS_OK) {
            k->ok_connects++;
        }
    }
}
void kit_connect(kit_t *k, const char *host, uint16_t port) { kit_connect_err(k, host, port, PORTS_OK); }
void kit_expect(kit_t *k, const char *bytes) {
    ports_fake_step_t *s = push(k, PORTS_FAKE_STEP_EXPECT);
    if (s != NULL) {
        s->data = core_str_from_cstr(bytes);
    }
}
void kit_respond_n(kit_t *k, const char *bytes, size_t len, size_t chunk) {
    ports_fake_step_t *s = push(k, PORTS_FAKE_STEP_RESPOND);
    if (s != NULL) {
        s->data.ptr = bytes;
        s->data.len = len;
        s->chunk = chunk;
    }
}
void kit_respond(kit_t *k, const char *bytes, size_t chunk) { kit_respond_n(k, bytes, strlen(bytes), chunk); }
void kit_eof(kit_t *k) { (void)push(k, PORTS_FAKE_STEP_EOF); }
void kit_fail_write(kit_t *k, ports_err_t e) {
    ports_fake_step_t *s = push(k, PORTS_FAKE_STEP_FAIL_WRITE);
    if (s != NULL) {
        s->err = e;
    }
}
void kit_fail_read(kit_t *k, ports_err_t e) {
    ports_fake_step_t *s = push(k, PORTS_FAKE_STEP_FAIL_READ);
    if (s != NULL) {
        s->err = e;
    }
}

void kit_expect_api(kit_t *k, const char *md5, const char *extra) {
    kit_expect(k, KIT_FMT(k,
                          "GET /dyn/api/fast_download.json?md5=%s&key=%s%s HTTP/1.1\r\nHost: %s\r\n"
                          "User-Agent: annas-c/0.1\r\nAccept: */*\r\nConnection: close\r\n\r\n",
                          md5, k->key_pct, extra, k->cfg.base));
}

void kit_expect_get(kit_t *k, const char *host, uint16_t port, const char *target) {
    if (port == 443u) {
        kit_expect(k, KIT_FMT(k,
                              "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: annas-c/0.1\r\nAccept: */*\r\n"
                              "Connection: close\r\n\r\n",
                              target, host));
    } else {
        kit_expect(k, KIT_FMT(k,
                              "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: annas-c/0.1\r\nAccept: */*\r\n"
                              "Connection: close\r\n\r\n",
                              target, host, (unsigned)port));
    }
}

const char *kit_head(kit_t *k, int status, const char *reason, const char *hdrs, size_t len) {
    return KIT_FMT(k, "HTTP/1.1 %d %s\r\n%sContent-Length: %zu\r\nConnection: close\r\n\r\n", status, reason, hdrs,
                   len);
}

const char *kit_http(kit_t *k, int status, const char *reason, const char *hdrs, const char *body) {
    return KIT_FMT(k, "HTTP/1.1 %d %s\r\n%sContent-Length: %zu\r\nConnection: close\r\n\r\n%s", status, reason,
                   hdrs, strlen(body), body);
}

const char *kit_ok_json(kit_t *k, const char *url, const char *account) {
    if (account == NULL) {
        return KIT_FMT(k, "{\"download_url\":\"%s\"}", url);
    }
    return KIT_FMT(k, "{\"download_url\":\"%s\",\"account_fast_download_info\":%s}", url, account);
}

void kit_api(kit_t *k, const char *md5, const char *extra, const char *response) {
    kit_connect(k, k->cfg.base, 443);
    kit_expect_api(k, md5, extra);
    kit_respond(k, response, 0);
}

void kit_get(kit_t *k, const char *host, uint16_t port, const char *target, const char *response) {
    kit_connect(k, host, port);
    kit_expect_get(k, host, port, target);
    kit_respond(k, response, 0);
}

bool kit_prepare(kit_t *k) {
    if (k->prepared) {
        return true;
    }
    k->prepared = true;
    sync_cfg(k);
    bool ok = ports_fake_stream_init(&k->stream, k->steps, k->n_steps, &k->clock, k->read_advance);
    ok = ports_fake_fs_init(&k->fs, k->fs_store, k->fs_cap) && ok;
    ok = annas_ctx_init(&k->ctx, &k->cfg, (annas_ports_t){ports_fake_stream_port(&k->stream), ports_fake_fs_port(&k->fs),
                                        ports_fake_clock_port(&k->clock)},
                        k->work, sizeof k->work) &&
         ok;
    return ok;
}

bool kit_preload(kit_t *k, const char *name, const char *content) {
    if (!kit_prepare(k)) {
        return false;
    }
    if (!ports_fake_fs_preload(&k->fs, core_str_from_cstr(name), core_str_from_cstr(content))) {
        return false;
    }
    k->preloaded++;
    return true;
}

annas_request_t kit_req(const char *md5) {
    annas_request_t r;
    memset(&r, 0, sizeof r);
    r.md5 = core_str_from_cstr(md5);
    return r;
}

annas_code_t kit_fast(kit_t *k, const annas_request_t *req) {
    (void)kit_prepare(k);
    memset(&k->fast, 0xA5, sizeof k->fast);
    memset(&k->err, 0xA5, sizeof k->err);
    k->fast_before = k->fast;
    return annas_fast_download_url(&k->ctx, req, &k->fast, &k->err);
}

annas_code_t kit_download(kit_t *k, const annas_request_t *req) {
    (void)kit_prepare(k);
    memset(&k->dl, 0xA5, sizeof k->dl);
    memset(&k->err, 0xA5, sizeof k->err);
    k->dl_before = k->dl;
    return annas_download(&k->ctx, req, &k->dl, &k->err);
}

bool kit_contains(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static void check_stream(ctest_t *t, kit_t *k) {
    ports_fake_mismatch_t mm;
    CHECK(t, !ports_fake_stream_mismatch(&k->stream, &mm));
    CHECK(t, !ports_fake_stream_is_open(&k->stream));
    CHECK_EQ_U64(t, ports_fake_stream_closes(&k->stream), k->ok_connects);
    if (!k->allow_unused) {
        CHECK(t, ports_fake_stream_done(&k->stream));
    }
}

void kit_check_ok(ctest_t *t, kit_t *k, annas_code_t got) {
    CHECK_EQ_I64(t, got, ANNAS_OK);
    CHECK_EQ_I64(t, k->err.code, ANNAS_OK);
    CHECK_EQ_I64(t, k->err.http_status, 0);
    CHECK_EQ_U64(t, k->err.message_len, 0);
    CHECK_EQ_STR(t, k->err.message, "");
    check_stream(t, k);
}

void kit_check_err(ctest_t *t, kit_t *k, annas_code_t got, annas_code_t want, int status, const char *msg) {
    CHECK_EQ_I64(t, got, want);
    CHECK_EQ_I64(t, k->err.code, want);
    CHECK_EQ_I64(t, k->err.http_status, status);
    CHECK_EQ_STR(t, k->err.message, msg);
    CHECK_EQ_U64(t, k->err.message_len, strlen(k->err.message));
    CHECK(t, k->err.message_len < ANNAS_MSG_MAX);
    CHECK(t, !kit_contains(k->err.message, k->key));
    CHECK(t, !kit_contains(k->err.message, k->key_pct));
    for (size_t i = 0; k->err.message[i] != '\0'; i++) {
        unsigned char c = (unsigned char)k->err.message[i];
        CHECK(t, c >= 0x20u && c != 0x7fu);
    }
    CHECK(t, memcmp(&k->fast, &k->fast_before, sizeof k->fast) == 0);
    CHECK(t, memcmp(&k->dl, &k->dl_before, sizeof k->dl) == 0);
    check_stream(t, k);
}

void kit_check_fs_untouched(ctest_t *t, kit_t *k) {
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&k->fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_commits(&k->fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_discards(&k->fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&k->fs), k->preloaded);
}

void kit_check_no_partial(ctest_t *t, kit_t *k, uint32_t discards) {
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&k->fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_commits(&k->fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_discards(&k->fs), discards);
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&k->fs), k->preloaded);
}

void kit_check_file(ctest_t *t, kit_t *k, const char *name, const void *content, size_t len) {
    core_str_t got = {NULL, 0};
    CHECK(t, ports_fake_fs_find(&k->fs, core_str_from_cstr(name), &got));
    CHECK_EQ_MEM(t, got.ptr, got.len, content, len);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&k->fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_commits(&k->fs), 1);
    CHECK_EQ_U64(t, ports_fake_fs_discards(&k->fs), 0);
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&k->fs), k->preloaded + 1u);
}
