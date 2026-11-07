#include "annas/src/internal.h"

#include <string.h>

typedef struct {
    core_str_t key;
    core_str_t dir;
    core_str_t base_raw;
} env_values_t;

/* The env port sets the value when it returns true (ports.h); a port that does not leaves {NULL, 0}. */
static core_str_t env_get(ports_env_t env, const char *name) {
    core_str_t v = {NULL, 0};
    (void)env.ops->get(env.ctx, core_str_from_cstr(name), &v);
    if (v.ptr == NULL) { /* a slice without bytes is unset, whatever length the port gave it */
        v.len = 0;
    }
    return v;
}

/* 1. timeout_seconds in range */
static annas_code_t check_timeout(int64_t seconds, core_str_t key, annas_error_t *err) {
    char buf[96];
    core_str_t none = {NULL, 0};
    core_str_t text;
    if (seconds < 0) {
        uint64_t mag = (uint64_t)0 - (uint64_t)seconds;
        text = msg_u64(buf, sizeof buf, "timeout must be 0 or more seconds, got: -", mag, "");
    } else if (seconds > (int64_t)ANNAS_MAX_TIMEOUT_SECONDS) {
        text = msg_u64(buf, sizeof buf, "timeout must be at most 31536000 seconds, got: ", (uint64_t)seconds, "");
    } else {
        return ANNAS_OK;
    }
    msg_body_t body = {text, none, MSG_NO_CUT};
    return msg_fail(err, ANNAS_E_ARG, 0, key, &body);
}

/* 2. base: normalised into `base` (NUL-terminated); E_CONFIG echoing the raw value when invalid */
static annas_code_t check_base(const env_values_t *v, char *base, size_t *base_len, annas_error_t *err) {
    core_buf_t b;
    if (!core_buf_init(&b, base, ANNAS_BASE_MAX) || url_base_normalize(v->base_raw, &b) != URL_OK ||
        !http_host_valid(core_buf_str(&b))) {
        msg_body_t body = {core_str_from_cstr("ANNAS_BASE_URL is not a valid domain, got: "), v->base_raw, 200};
        return msg_fail(err, ANNAS_E_CONFIG, 0, v->key, &body);
    }
    *base_len = core_buf_len(&b);
    return ANNAS_OK;
}

annas_code_t annas_config_load(ports_env_t env, bool need_download, int64_t timeout_seconds, annas_config_t *cfg,
                               annas_error_t *err) {
    env_values_t v;
    char base[ANNAS_BASE_MAX];
    size_t base_len; /* set by check_base, read only when it succeeded */
    uint64_t seconds = timeout_seconds == 0 ? (uint64_t)ANNAS_DEFAULT_TIMEOUT_SECONDS : (uint64_t)timeout_seconds;
    if (err == NULL) {
        return ANNAS_E_ARG;
    }
    if (cfg == NULL || env.ops == NULL || env.ops->get == NULL) {
        return arg_invalid(err, (core_str_t){NULL, 0});
    }
    v.key = env_get(env, "ANNAS_SECRET_KEY");
    v.dir = env_get(env, "ANNAS_DOWNLOAD_PATH");
    v.base_raw = env_get(env, "ANNAS_BASE_URL");
    annas_code_t c = check_timeout(timeout_seconds, v.key, err);
    if (c == ANNAS_OK) {
        c = check_base(&v, base, &base_len, err);
    }
    if (c == ANNAS_OK && need_download) {
        c = check_config(v.key, v.dir, CFG_DIR | CFG_ABS, err);
    }
    if (c != ANNAS_OK) {
        return c;
    }
    memcpy(cfg->base, base, base_len + 1u);
    cfg->base_len = base_len;
    cfg->key = v.key;
    cfg->download_dir = v.dir;
    cfg->timeout_ms = seconds * 1000u;
    msg_ok(err);
    return ANNAS_OK;
}
