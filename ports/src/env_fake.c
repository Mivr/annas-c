/* STUB (test-author task A009): compiles, always fails. Replaced by the implementation task. */
#include "ports/fakes.h"

static bool stub_get(void *ctx, core_str_t name, core_str_t *value) {
    (void)ctx;
    (void)name;
    (void)value;
    return false;
}

static const ports_env_ops_t k_ops = {stub_get};

bool ports_fake_env_init(ports_fake_env_t *e, const ports_fake_env_entry_t *entries, size_t count) {
    (void)e;
    (void)entries;
    (void)count;
    return false;
}

ports_env_t ports_fake_env_port(ports_fake_env_t *e) {
    ports_env_t p = {&k_ops, e};
    return p;
}
