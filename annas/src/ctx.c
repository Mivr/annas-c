#include "annas/src/internal.h"

#define ALIGN16(n) (((n) + (size_t)15u) & ~(size_t)15u)
#define WS_RES_OFF ALIGN16(sizeof(http_parser_t))
#define WS_IO_OFF (WS_RES_OFF + ALIGN16(sizeof(annas_download_result_t)))
#define WS_BODY_OFF (WS_IO_OFF + WS_IO_CAP)
#define WS_REST_OFF (WS_BODY_OFF + WS_BODY_CAP)
/* url_redact_key can turn every 5 bytes ("key=&") into 13: the redaction of a whole API body must fit in `rest`. */
#define WS_REDACT_NEED ((ANNAS_API_BODY_MAX / 5u + 1u) * 13u)

_Static_assert(WS_REST_OFF + WS_REDACT_NEED <= ANNAS_WORKSPACE_MIN, "workspace layout does not fit");
_Static_assert(WS_REST_OFF + 2u * WS_URL_BUF + HTTP_MAX_LINE <= ANNAS_WORKSPACE_MIN, "URL buffers do not fit");

static bool stream_ok(ports_stream_t s) {
    return s.ops != NULL && s.ops->connect != NULL && s.ops->write != NULL && s.ops->read != NULL &&
           s.ops->close != NULL;
}

static bool fs_ok(ports_fs_t f) {
    return f.ops != NULL && f.ops->create_temp != NULL && f.ops->write != NULL && f.ops->commit != NULL &&
           f.ops->discard != NULL;
}

bool annas_ctx_init(annas_ctx_t *ctx, const annas_config_t *cfg, annas_ports_t ports, void *workspace, size_t cap) {
    if (ctx == NULL || cfg == NULL || workspace == NULL || cap < ANNAS_WORKSPACE_MIN) {
        return false;
    }
    if (!stream_ok(ports.stream) || !fs_ok(ports.fs) || ports.clock.ops == NULL ||
        ports.clock.ops->now_ms == NULL) {
        return false;
    }
    if (((uintptr_t)workspace & (uintptr_t)15u) != 0u) {
        return false;
    }
    ctx->cfg = cfg;
    ctx->stream = ports.stream;
    ctx->fs = ports.fs;
    ctx->clock = ports.clock;
    ctx->work = workspace;
    ctx->work_cap = cap;
    ctx->deadline_ms = 0;
    for (size_t i = 0; i < sizeof ctx->priv / sizeof ctx->priv[0]; i++) {
        ctx->priv[i] = 0;
    }
    return true;
}

void ws_split(const annas_ctx_t *ctx, ws_t *w) {
    unsigned char *base = ctx->work;
    w->parser = (http_parser_t *)(void *)base;
    w->res = (annas_download_result_t *)(void *)(base + WS_RES_OFF);
    w->io = base + WS_IO_OFF;
    w->body = base + WS_BODY_OFF;
    w->rest = base + WS_REST_OFF;
    w->rest_cap = ctx->work_cap - WS_REST_OFF;
}

void op_begin(annas_ctx_t *ctx) {
    uint64_t now = ctx->clock.ops->now_ms(ctx->clock.ctx);
    uint64_t t = ctx->cfg->timeout_ms;
    ctx->deadline_ms = (t > UINT64_MAX - now) ? UINT64_MAX : now + t;
}

annas_code_t op_check(annas_ctx_t *ctx, annas_error_t *err) {
    if (ctx->clock.ops->now_ms(ctx->clock.ctx) >= ctx->deadline_ms) {
        return msg_text(err, ANNAS_E_TIMEOUT, 0, ctx_key(ctx), "operation timed out");
    }
    return ANNAS_OK;
}

annas_code_t arg_invalid(annas_error_t *err, core_str_t key) {
    return msg_text(err, ANNAS_E_ARG, 0, key, "invalid argument");
}

annas_code_t check_config(const annas_ctx_t *ctx, annas_error_t *err, bool need_dir) {
    core_str_t key = ctx_key(ctx);
    const core_str_t dir = ctx->cfg->download_dir;
    if (key.len == 0 || (need_dir && dir.len == 0)) {
        return msg_text(err, ANNAS_E_CONFIG, 0, key,
                        "ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set");
    }
    if (key.len > ANNAS_KEY_MAX) {
        return msg_text(err, ANNAS_E_CONFIG, 0, key, "ANNAS_SECRET_KEY is longer than 256 bytes");
    }
    if (need_dir && dir.len > ANNAS_DIR_MAX) {
        return msg_text(err, ANNAS_E_CONFIG, 0, key, "ANNAS_DOWNLOAD_PATH is longer than 256 bytes");
    }
    return ANNAS_OK;
}
