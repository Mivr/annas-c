#include "posix_env/posix_env.h"

/* STUB (test-author task A016): nothing is ever found. */
bool posix_env_init(posix_env_t *e, char *const *envp) {
    (void)e;
    (void)envp;
    return false;
}

static bool stub_get(void *ctx, core_str_t name, core_str_t *value) {
    (void)ctx;
    (void)name;
    if (value != NULL) {
        value->ptr = NULL;
        value->len = 0;
    }
    return false;
}

static const ports_env_ops_t k_ops = {stub_get};

ports_env_t posix_env_port(posix_env_t *e) {
    ports_env_t p = {&k_ops, e};
    return p;
}
