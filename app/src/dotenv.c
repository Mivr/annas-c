#include "app/app.h"

/* STUB (test-author task A014). */

static bool stub_get(void *ctx, core_str_t name, core_str_t *value) {
    (void)ctx;
    (void)name;
    if (value != NULL) {
        value->ptr = NULL;
        value->len = 0;
    }
    return false;
}

static const ports_env_ops_t k_stub_ops = {stub_get};

bool app_dotenv_parse(core_str_t file, app_dotenv_t *out) {
    (void)file;
    (void)out;
    return false;
}

bool app_env_layer_init(app_env_layer_t *l, const app_dotenv_t *file, ports_env_t process) {
    (void)l;
    (void)file;
    (void)process;
    return false;
}

ports_env_t app_env_layer_port(app_env_layer_t *l) {
    ports_env_t e;
    e.ops = &k_stub_ops;
    e.ctx = l;
    return e;
}
