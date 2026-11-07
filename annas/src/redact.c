#include "annas/src/internal.h"

size_t cat_len(const cat_t *t) { return t->a.len + t->b.len; }

unsigned char cat_at(const cat_t *t, size_t i) {
    unsigned char v = (unsigned char)(i < t->a.len ? t->a.ptr[i] : t->b.ptr[i - t->a.len]);
    if (t->san && (v < 0x20u || v == 0x7fu)) {
        return (unsigned char)'?';
    }
    return v;
}

static bool unreserved(unsigned char c) {
    return (c >= (unsigned char)'A' && c <= (unsigned char)'Z') || (c >= (unsigned char)'a' && c <= (unsigned char)'z') ||
           (c >= (unsigned char)'0' && c <= (unsigned char)'9') || c == (unsigned char)'-' || c == (unsigned char)'.' ||
           c == (unsigned char)'_' || c == (unsigned char)'~';
}

static size_t match_raw(const cat_t *t, size_t pos, core_str_t key) {
    if (key.len > cat_len(t) - pos) {
        return 0;
    }
    for (size_t i = 0; i < key.len; i++) {
        if (cat_at(t, pos + i) != (unsigned char)key.ptr[i]) {
            return 0;
        }
    }
    return key.len;
}

/* One key byte as it appears percent-encoded; returns its encoded length (1 or 3) or 0 on a mismatch. */
static size_t match_pct_byte(const cat_t *t, size_t p, size_t total, unsigned char k) {
    static const char hex[] = "0123456789ABCDEF";
    if (unreserved(k)) {
        return (p < total && cat_at(t, p) == k) ? 1u : 0u;
    }
    if (total - p < 3u || cat_at(t, p) != (unsigned char)'%') {
        return 0;
    }
    if (cat_at(t, p + 1u) != (unsigned char)hex[k >> 4] || cat_at(t, p + 2u) != (unsigned char)hex[k & 15u]) {
        return 0;
    }
    return 3;
}

static size_t match_pct(const cat_t *t, size_t pos, core_str_t key) {
    size_t total = cat_len(t);
    size_t p = pos;
    for (size_t i = 0; i < key.len; i++) {
        size_t n = match_pct_byte(t, p, total, (unsigned char)key.ptr[i]);
        if (n == 0) {
            return 0;
        }
        p += n;
    }
    return p - pos;
}

size_t key_match(const cat_t *t, size_t pos, core_str_t key) {
    if (key.len == 0) {
        return 0;
    }
    size_t n = match_raw(t, pos, key);
    return n != 0 ? n : match_pct(t, pos, key);
}

/* A match needs at least key.len bytes (raw, or 1 or 3 per byte encoded), so later positions are not tried. */
bool key_in(core_str_t hay, core_str_t key) {
    cat_t t = {hay, {NULL, 0}, false};
    for (size_t pos = 0; pos + key.len <= hay.len; pos++) {
        if (key_match(&t, pos, key) != 0) {
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
