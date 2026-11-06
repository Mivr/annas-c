#include "json/json.h"

/* STUB (A003): the writer is permanently in the error state and writes nothing. */
bool json_w_init(json_writer_t *w, core_buf_t *b) {
    (void)b;
    if (w != NULL) {
        w->failed = true;
    }
    return false;
}

void json_w_begin_object(json_writer_t *w) { (void)w; }
void json_w_end_object(json_writer_t *w) { (void)w; }
void json_w_begin_array(json_writer_t *w) { (void)w; }
void json_w_end_array(json_writer_t *w) { (void)w; }

void json_w_key(json_writer_t *w, core_str_t key) {
    (void)w;
    (void)key;
}

void json_w_string(json_writer_t *w, core_str_t s) {
    (void)w;
    (void)s;
}

void json_w_i64(json_writer_t *w, int64_t v) {
    (void)w;
    (void)v;
}

void json_w_u64(json_writer_t *w, uint64_t v) {
    (void)w;
    (void)v;
}

void json_w_bool(json_writer_t *w, bool v) {
    (void)w;
    (void)v;
}

void json_w_null(json_writer_t *w) { (void)w; }

bool json_w_ok(const json_writer_t *w) {
    (void)w;
    return false;
}

bool json_w_done(const json_writer_t *w) {
    (void)w;
    return false;
}
