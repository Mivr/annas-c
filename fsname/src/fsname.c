#include "fsname/fsname.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SCRATCH_LEN (FSNAME_MAX_LEN + 1)

typedef struct {
    unsigned char kept[SCRATCH_LEN]; /* first 201 bytes of the stripped stream */
    size_t idx;                      /* bytes emitted so far (after the leading strip) */
    size_t keep;                     /* length up to the last byte that is neither '.' nor ' ' */
    bool started;
} fsname_state_t;

static bool is_edge(unsigned char b) {
    return b == (unsigned char)'.' || b == (unsigned char)' ';
}

static unsigned char map_byte(unsigned char c) {
    switch (c) {
    case '<': case '>': case ':': case '"': case '/': case '\\': case '|': case '?': case '*':
        return (unsigned char)'_';
    default:
        break;
    }
    if (c < 0x20U || c == 0x7fU) {
        return (unsigned char)'_';
    }
    return c;
}

static void emit(fsname_state_t *st, unsigned char b) {
    if (!st->started && is_edge(b)) {
        return;
    }
    st->started = true;
    if (st->idx <= FSNAME_MAX_LEN) {
        st->kept[st->idx] = b;
    }
    st->idx++;
    if (!is_edge(b)) {
        st->keep = st->idx;
    }
}

static void flush_dots(fsname_state_t *st, size_t *run) {
    size_t pairs = *run / 2U;
    for (size_t i = 0; i < pairs; i++) {
        emit(st, (unsigned char)'_');
    }
    if ((*run % 2U) != 0U) {
        emit(st, (unsigned char)'.');
    }
    *run = 0;
}

static size_t cut_point(const unsigned char *s) {
    /* k == 0: s[200] itself; a lead byte or ASCII there means the cut is at 200 already */
    for (size_t k = 0; k <= 3U; k++) {
        unsigned char b = s[FSNAME_MAX_LEN - k];
        if (b >= 0xC0U) {
            return FSNAME_MAX_LEN - k;
        }
        if (b < 0x80U) {
            break;
        }
    }
    return FSNAME_MAX_LEN;
}

size_t fsname_sanitize(core_str_t title, char *out, size_t cap) {
    if (out == NULL || cap < FSNAME_BUF_SIZE || (title.ptr == NULL && title.len > 0)) {
        return 0;
    }
    fsname_state_t st;
    memset(&st, 0, sizeof st);
    size_t run = 0;
    const unsigned char *in = (const unsigned char *)title.ptr;
    for (size_t i = 0; i < title.len; i++) {
        unsigned char m = map_byte(in[i]);
        if (m == (unsigned char)'.') {
            run++;
        } else {
            flush_dots(&st, &run);
            emit(&st, m);
        }
    }
    flush_dots(&st, &run);

    size_t n = st.keep;
    if (n == 0) {
        memcpy(out, "untitled", 9);
        return 8;
    }
    if (n > FSNAME_MAX_LEN) {
        n = cut_point(st.kept);
    }
    memcpy(out, st.kept, n);
    out[n] = '\0';
    return n;
}
