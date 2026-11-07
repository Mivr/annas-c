#include "ports/fakes.h"

static bool slice_ok(core_str_t s) { return s.ptr != NULL || s.len == 0; }

static bool entries_valid(const ports_fake_env_entry_t *entries, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (entries[i].name.len == 0 || !slice_ok(entries[i].name) || !slice_ok(entries[i].value)) {
            return false;
        }
    }
    return true;
}

static bool env_get(void *ctx, core_str_t name, core_str_t *value) {
    const ports_fake_env_t *e = (const ports_fake_env_t *)ctx;
    if (value != NULL) {
        value->ptr = NULL;
        value->len = 0;
    }
    if (e == NULL || value == NULL || name.len == 0 || name.ptr == NULL) {
        return false;
    }
    for (size_t i = 0; i < e->count; i++) {
        if (core_str_eq(e->entries[i].name, name)) {
            *value = e->entries[i].value;
            return true;
        }
    }
    return false;
}

static const ports_env_ops_t k_ops = {env_get};

bool ports_fake_env_init(ports_fake_env_t *e, const ports_fake_env_entry_t *entries, size_t count) {
    if (e == NULL) {
        return false;
    }
    *e = (ports_fake_env_t){.entries = NULL}; /* count is zero: an unusable fake on every false return below */
    if (entries == NULL && count > 0) {
        return false;
    }
    if (!entries_valid(entries, count)) { /* count == 0 is vacuously valid */
        return false;
    }
    e->entries = entries;
    e->count = count;
    return true;
}

ports_env_t ports_fake_env_port(ports_fake_env_t *e) {
    ports_env_t p = {&k_ops, e};
    return p;
}
