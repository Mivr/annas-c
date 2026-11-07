#include "core/str.h"
#include "json/src/internal.h"

enum { K_OBJ = 1, K_ARR = 2 };
enum { F_ITEMS = 1, F_KEY = 2 };

static bool usable(const json_writer_t *w) { return w != NULL && !w->failed; }

static void set_failed(json_writer_t *w) { w->failed = true; }

bool json_w_init(json_writer_t *w, core_buf_t *b) {
    if (w == NULL) {
        return false;
    }
    /* kind[] and flags[] are not cleared: begin() sets both for a level before anything reads them. */
    w->buf = b;
    w->depth = 0;
    w->root_done = false;
    w->failed = b == NULL;
    return b != NULL;
}

/* Checks that a value may start here and emits the separator. */
static bool pre_value(json_writer_t *w) {
    if (w->depth == 0) {
        if (w->root_done) {
            set_failed(w);
            return false;
        }
        return true;
    }
    if (w->kind[w->depth - 1] == K_OBJ) {
        if ((w->flags[w->depth - 1] & F_KEY) == 0) {
            set_failed(w);
            return false;
        }
        return true;
    }
    if ((w->flags[w->depth - 1] & F_ITEMS) != 0) {
        core_buf_put_char(w->buf, ',');
    }
    return true;
}

static void post_value(json_writer_t *w) {
    if (w->depth == 0) {
        w->root_done = true;
        return;
    }
    w->flags[w->depth - 1] = F_ITEMS; /* the flags are only ITEMS and KEY: one value done sets the first, clears the second */
}

static void begin(json_writer_t *w, unsigned char kind, char open) {
    if (!usable(w)) {
        return;
    }
    if (w->depth >= JSON_MAX_DEPTH) {
        set_failed(w);
        return;
    }
    if (!pre_value(w)) {
        return;
    }
    core_buf_put_char(w->buf, open);
    w->kind[w->depth] = kind;
    w->flags[w->depth] = 0;
    w->depth++;
}

static void end(json_writer_t *w, unsigned char kind, char close) {
    if (!usable(w)) {
        return;
    }
    if (w->depth == 0 || w->kind[w->depth - 1] != kind || (w->flags[w->depth - 1] & F_KEY) != 0) {
        set_failed(w);
        return;
    }
    core_buf_put_char(w->buf, close);
    w->depth--;
    post_value(w);
}

void json_w_begin_object(json_writer_t *w) { begin(w, K_OBJ, '{'); }
void json_w_end_object(json_writer_t *w) { end(w, K_OBJ, '}'); }
void json_w_begin_array(json_writer_t *w) { begin(w, K_ARR, '['); }
void json_w_end_array(json_writer_t *w) { end(w, K_ARR, ']'); }

/* json_utf8_seq_len needs a non-NULL pointer and at least one byte, so an empty slice (any pointer, NULL
 * included) is valid and a NULL slice with a length is not. */
static bool valid_utf8(core_str_t s) {
    size_t i = 0, len;
    if (s.ptr == NULL) {
        return s.len == 0;
    }
    while (i < s.len) {
        len = json_utf8_seq_len((const unsigned char *)s.ptr + i, s.len - i);
        if (len == 0) {
            return false;
        }
        i += len;
    }
    return true;
}

static bool plain(unsigned char c) { return c >= 0x20 && c != '"' && c != '\\'; }

static void put_escape(core_buf_t *b, unsigned char c) {
    static const char hex[] = "0123456789abcdef";
    char e[6] = {'\\', 'u', '0', '0'}; /* e[4], e[5] are set below */
    if (c == '"' || c == '\\') {
        e[1] = (char)c;
        core_buf_put(b, e, 2);
        return;
    }
    e[4] = hex[c >> 4];
    e[5] = hex[c & 15];
    core_buf_put(b, e, 6);
}

static void put_quoted(core_buf_t *b, core_str_t s) {
    const unsigned char *p = (const unsigned char *)s.ptr;
    size_t i = 0, run;
    core_buf_put_char(b, '"');
    while (i < s.len) {
        run = 0;
        while (i + run < s.len && plain(p[i + run])) {
            run++;
        }
        core_buf_put(b, p + i, run);
        i += run;
        if (i < s.len) {
            put_escape(b, p[i]);
            i++;
        }
    }
    core_buf_put_char(b, '"');
}

void json_w_key(json_writer_t *w, core_str_t key) {
    size_t d;
    if (!usable(w)) {
        return;
    }
    d = w->depth;
    if (d == 0 || w->kind[d - 1] != K_OBJ || (w->flags[d - 1] & F_KEY) != 0 || !valid_utf8(key)) {
        set_failed(w);
        return;
    }
    if ((w->flags[d - 1] & F_ITEMS) != 0) {
        core_buf_put_char(w->buf, ',');
    }
    put_quoted(w->buf, key);
    core_buf_put_char(w->buf, ':');
    w->flags[d - 1] = F_KEY; /* ITEMS is not read while KEY is set; post_value sets it again */
}

void json_w_string(json_writer_t *w, core_str_t s) {
    if (!usable(w)) {
        return;
    }
    if (!valid_utf8(s) || !pre_value(w)) {
        set_failed(w);
        return;
    }
    put_quoted(w->buf, s);
    post_value(w);
}

void json_w_number_text(json_writer_t *w, core_str_t text) {
    if (!usable(w) || !pre_value(w)) {
        return;
    }
    core_buf_put_str(w->buf, text);
    post_value(w);
}

void json_w_u64(json_writer_t *w, uint64_t v) {
    if (!usable(w) || !pre_value(w)) {
        return;
    }
    core_buf_put_u64(w->buf, v);
    post_value(w);
}

void json_w_i64(json_writer_t *w, int64_t v) {
    if (!usable(w) || !pre_value(w)) {
        return;
    }
    if (v < 0) {
        core_buf_put_char(w->buf, '-');
        core_buf_put_u64(w->buf, 0u - (uint64_t)v);
    } else {
        core_buf_put_u64(w->buf, (uint64_t)v);
    }
    post_value(w);
}

void json_w_bool(json_writer_t *w, bool v) {
    if (!usable(w) || !pre_value(w)) {
        return;
    }
    core_buf_put_str(w->buf, core_str_from_cstr(v ? "true" : "false"));
    post_value(w);
}

void json_w_null(json_writer_t *w) {
    if (!usable(w) || !pre_value(w)) {
        return;
    }
    core_buf_put_str(w->buf, core_str_from_cstr("null"));
    post_value(w);
}

bool json_w_ok(const json_writer_t *w) {
    return w != NULL && !w->failed && !core_buf_overflow(w->buf);
}

bool json_w_done(const json_writer_t *w) { return json_w_ok(w) && w->depth == 0 && w->root_done; }
