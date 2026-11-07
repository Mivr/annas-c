#include "core/buf.h"

#include <string.h>

bool core_buf_init(core_buf_t *b, char *storage, size_t cap) {
    if (b == NULL || (storage == NULL && cap > 0)) {
        return false;
    }
    b->data = storage;
    b->cap = cap;
    b->len = 0;
    b->overflow = false;
    if (cap >= 1) {
        storage[0] = '\0';
    }
    return true;
}

void core_buf_put(core_buf_t *b, const void *p, size_t n) {
    if (b == NULL || n == 0 || b->overflow) {
        return;
    }
    if (p == NULL || b->cap == 0 || n > b->cap - 1 - b->len) {
        b->overflow = true;
        return;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void core_buf_put_str(core_buf_t *b, core_str_t s) {
    core_buf_put(b, s.ptr, s.len);
}

void core_buf_put_char(core_buf_t *b, char c) {
    core_buf_put(b, &c, 1);
}

void core_buf_put_u64(core_buf_t *b, uint64_t v) {
    char tmp[20];
    size_t pos = sizeof tmp;

    do {
        pos--;
        tmp[pos] = (char)('0' + (int)(v % 10U));
        v /= 10U;
    } while (v != 0);
    core_buf_put(b, tmp + pos, sizeof tmp - pos);
}

size_t core_buf_len(const core_buf_t *b) {
    return b == NULL ? 0 : b->len;
}

bool core_buf_overflow(const core_buf_t *b) {
    return b != NULL && b->overflow;
}

core_str_t core_buf_str(const core_buf_t *b) {
    core_str_t r = {NULL, 0};
    if (b != NULL && b->cap != 0) {
        r.ptr = b->data;
        r.len = b->len;
    }
    return r;
}

const char *core_buf_cstr(const core_buf_t *b) {
    if (b == NULL || b->cap == 0) {
        return "";
    }
    return b->data;
}

void core_buf_truncate(core_buf_t *b, size_t len) {
    if (b == NULL || b->cap == 0 || len >= b->len) {
        return;
    }
    b->len = len;
    b->data[len] = '\0';
}
