#include "core/parse.h"
#include "json/src/internal.h"

static bool digit_at(const json_parser_t *p, size_t pos) {
    return pos < p->n && p->s[pos] >= '0' && p->s[pos] <= '9';
}

static size_t skip_digits(const json_parser_t *p, size_t pos) {
    while (digit_at(p, pos)) {
        pos++;
    }
    return pos;
}

json_err_t json_num_scan(json_parser_t *p) {
    size_t pos = p->i;
    if (p->s[pos] == '-') {
        pos++;
    }
    if (!digit_at(p, pos)) {
        p->err = pos;
        return JSON_E_NUMBER;
    }
    if (p->s[pos] == '0') {
        pos++;
        if (digit_at(p, pos)) {
            p->err = pos;
            return JSON_E_NUMBER;
        }
    } else {
        pos = skip_digits(p, pos);
    }
    if (pos < p->n && p->s[pos] == '.') {
        pos++;
        if (!digit_at(p, pos)) {
            p->err = pos;
            return JSON_E_NUMBER;
        }
        pos = skip_digits(p, pos);
    }
    if (pos < p->n && (p->s[pos] == 'e' || p->s[pos] == 'E')) {
        pos++;
        if (pos < p->n && (p->s[pos] == '+' || p->s[pos] == '-')) {
            pos++;
        }
        if (!digit_at(p, pos)) {
            p->err = pos;
            return JSON_E_NUMBER;
        }
        pos = skip_digits(p, pos);
    }
    p->i = pos;
    return JSON_OK;
}

bool json_number_text(const json_value_t *v, core_str_t *out) {
    if (v == NULL || v->type != JSON_NUMBER || out == NULL) {
        return false;
    }
    *out = v->u.s;
    return true;
}

/* Splits a number's text into its sign and the rest. The text comes from json_num_scan (the only way a
 * JSON_NUMBER node is made), so it is -?digits... and never empty. core_parse_u64 then accepts only a pure
 * digit string, which rejects a fraction or an exponent. */
static void split_sign(core_str_t t, bool *neg, core_str_t *digits) {
    *neg = t.ptr[0] == '-';
    digits->ptr = t.ptr + *neg;
    digits->len = t.len - *neg;
}

bool json_get_u64(const json_value_t *v, uint64_t *out) {
    core_str_t d;
    bool neg;
    uint64_t mag;
    if (v == NULL || v->type != JSON_NUMBER || out == NULL) {
        return false;
    }
    split_sign(v->u.s, &neg, &d);
    if (neg || !core_parse_u64(d, &mag)) {
        return false;
    }
    *out = mag;
    return true;
}

bool json_get_i64(const json_value_t *v, int64_t *out) {
    core_str_t d;
    bool neg;
    uint64_t mag;
    if (v == NULL || v->type != JSON_NUMBER || out == NULL) {
        return false;
    }
    split_sign(v->u.s, &neg, &d);
    if (!core_parse_u64(d, &mag)) {
        return false;
    }
    if (neg) {
        if (mag > (uint64_t)INT64_MAX + 1u) {
            return false;
        }
        *out = (int64_t)(0u - mag);
        return true;
    }
    if (mag > (uint64_t)INT64_MAX) {
        return false;
    }
    *out = (int64_t)mag;
    return true;
}
