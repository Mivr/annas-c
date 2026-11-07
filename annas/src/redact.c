#include "annas/src/internal.h"

size_t cat_len(const cat_t *t) { return t->a.len + t->b.len; }

unsigned char cat_at(const cat_t *t, size_t i) {
    unsigned char v = (unsigned char)(i < t->a.len ? t->a.ptr[i] : t->b.ptr[i - t->a.len]);
    if (t->san && (v < 0x20u || v == 0x7fu)) {
        return (unsigned char)'?';
    }
    if (t->fold) {
        return (unsigned char)core_ascii_lower((char)v);
    }
    return v;
}

/* The key is 16..256 bytes of the URL-unreserved set (annas.h, KEY CLASS), so its percent-encoding is itself and one
 * raw comparison finds both forms. With `t->fold` the text and the key are compared ASCII case-insensitively. */
bool key_match(const cat_t *t, size_t pos, core_str_t key) {
    if (key.len == 0 || key.len > cat_len(t) - pos) {
        return false;
    }
    for (size_t i = 0; i < key.len; i++) {
        char k = t->fold ? core_ascii_lower(key.ptr[i]) : key.ptr[i];
        if (cat_at(t, pos + i) != (unsigned char)k) {
            return false;
        }
    }
    return true;
}

bool key_in(core_str_t hay, core_str_t key, bool fold) {
    cat_t t = {hay, {NULL, 0}, false, fold};
    for (size_t pos = 0; pos + key.len <= hay.len; pos++) {
        if (key_match(&t, pos, key)) {
            return true;
        }
    }
    return false;
}

void redact_into(core_arena_t *ar, core_str_t src, core_str_t *out) {
    size_t left = core_arena_capacity(ar) - core_arena_used(ar);
    char *p = core_arena_alloc(ar, left, 1);
    core_buf_t b;
    if (p != NULL && core_buf_init(&b, p, left) && url_redact_key(&b, src) == URL_OK) {
        *out = core_buf_str(&b);
    }
}
