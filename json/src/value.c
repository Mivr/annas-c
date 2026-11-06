#include "json/json.h"

/* STUB (A003): every accessor reports "nothing". */
json_type_t json_type(const json_value_t *v) {
    (void)v;
    return JSON_NONE;
}

size_t json_len(const json_value_t *v) {
    (void)v;
    return 0;
}

const json_value_t *json_at(const json_value_t *v, size_t i) {
    (void)v;
    (void)i;
    return NULL;
}

core_str_t json_key_at(const json_value_t *v, size_t i) {
    core_str_t e = {NULL, 0};
    (void)v;
    (void)i;
    return e;
}

const json_value_t *json_obj_get(const json_value_t *v, core_str_t key) {
    (void)v;
    (void)key;
    return NULL;
}

bool json_get_bool(const json_value_t *v, bool *out) {
    (void)v;
    (void)out;
    return false;
}

bool json_get_str(const json_value_t *v, core_str_t *out) {
    (void)v;
    (void)out;
    return false;
}
