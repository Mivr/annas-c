#include "json/src/internal.h"

static bool is_container(const json_value_t *v) {
    return v != NULL && (v->type == JSON_ARRAY || v->type == JSON_OBJECT);
}

json_type_t json_type(const json_value_t *v) { return v == NULL ? JSON_NONE : v->type; }

size_t json_len(const json_value_t *v) { return is_container(v) ? v->u.c.n : 0; }

const json_value_t *json_at(const json_value_t *v, size_t i) {
    if (!is_container(v) || i >= v->u.c.n) {
        return NULL;
    }
    return v->u.c.items[i];
}

core_str_t json_key_at(const json_value_t *v, size_t i) {
    core_str_t k = {NULL, 0};
    if (v != NULL && v->type == JSON_OBJECT && i < v->u.c.n) {
        k = v->u.c.items[i]->key;
    }
    return k;
}

const json_value_t *json_obj_get(const json_value_t *v, core_str_t key) {
    size_t i;
    if (v == NULL || v->type != JSON_OBJECT) {
        return NULL;
    }
    for (i = 0; i < v->u.c.n; i++) {
        if (core_str_eq(v->u.c.items[i]->key, key)) {
            return v->u.c.items[i];
        }
    }
    return NULL;
}

bool json_get_bool(const json_value_t *v, bool *out) {
    if (v == NULL || v->type != JSON_BOOL || out == NULL) {
        return false;
    }
    *out = v->b;
    return true;
}

bool json_get_str(const json_value_t *v, core_str_t *out) {
    if (v == NULL || v->type != JSON_STRING || out == NULL) {
        return false;
    }
    *out = v->u.s;
    return true;
}
