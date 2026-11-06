#include "json/json.h"

/* STUB (A003): conversions always fail. */
bool json_number_text(const json_value_t *v, core_str_t *out) {
    (void)v;
    (void)out;
    return false;
}

bool json_get_i64(const json_value_t *v, int64_t *out) {
    (void)v;
    (void)out;
    return false;
}

bool json_get_u64(const json_value_t *v, uint64_t *out) {
    (void)v;
    (void)out;
    return false;
}
