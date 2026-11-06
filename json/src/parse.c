#include "json/json.h"

/* STUB (A003): every call fails. */
json_limits_t json_limits_default(void) {
    json_limits_t l = {0, 0, 0, 0};
    return l;
}

json_err_t json_parse(core_arena_t *a, core_str_t in, const json_limits_t *lim, const json_value_t **root,
                      size_t *err_offset) {
    (void)a;
    (void)in;
    (void)lim;
    if (root != NULL) {
        *root = NULL;
    }
    if (err_offset != NULL) {
        *err_offset = 0;
    }
    return JSON_E_ARG;
}
