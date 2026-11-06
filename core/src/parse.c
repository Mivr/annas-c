#include "core/parse.h"

bool core_parse_u64(core_str_t s, uint64_t *out) {
    uint64_t v = 0;
    size_t i;

    if (out == NULL || s.len == 0) {
        return false;
    }
    for (i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.ptr[i];
        uint64_t d;
        if (c < '0' || c > '9') {
            return false;
        }
        d = (uint64_t)(c - '0');
        if (v > (UINT64_MAX - d) / 10U) {
            return false;
        }
        v = v * 10U + d;
    }
    *out = v;
    return true;
}

int core_parse_hex_nibble(char c) {
    unsigned char u = (unsigned char)c;
    if (u >= '0' && u <= '9') {
        return (int)(u - '0');
    }
    if (u >= 'a' && u <= 'f') {
        return (int)(u - 'a') + 10;
    }
    if (u >= 'A' && u <= 'F') {
        return (int)(u - 'A') + 10;
    }
    return -1;
}

char core_ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}
