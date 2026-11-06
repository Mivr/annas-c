#include <stddef.h>
#include <string.h>

#include "posix_env/posix_env.h"

bool posix_env_init(posix_env_t *e, char *const *envp) {
    if (e == NULL || envp == NULL) {
        return false;
    }
    e->envp = envp;
    return true;
}

/* True iff `entry` is "name=..." for exactly `name`. */
static bool entry_matches(const char *entry, core_str_t name, core_str_t *value) {
    size_t i = 0;
    while (i < name.len) {
        if (entry[i] != name.ptr[i]) {
            return false; /* includes the terminating NUL: the entry is shorter than name */
        }
        i++;
    }
    if (entry[i] != '=') {
        return false;
    }
    *value = core_str_from_cstr(entry + i + 1);
    return true;
}

static bool env_get(void *ctx, core_str_t name, core_str_t *value) {
    const posix_env_t *e = (const posix_env_t *)ctx;
    core_str_t found = {NULL, 0};
    bool ok = false;
    if (e != NULL && value != NULL && name.len > 0 && name.ptr != NULL && memchr(name.ptr, '=', name.len) == NULL &&
        memchr(name.ptr, '\0', name.len) == NULL) {
        for (char *const *p = e->envp; *p != NULL && !ok; p++) {
            ok = entry_matches(*p, name, &found);
        }
    }
    if (value != NULL) {
        *value = ok ? found : (core_str_t){NULL, 0};
    }
    return ok;
}

static const ports_env_ops_t k_ops = {env_get};

ports_env_t posix_env_port(posix_env_t *e) {
    ports_env_t p = {&k_ops, e};
    return p;
}
