#include "json/src/internal.h"

static bool is_container(const json_value_t *v) { return v->type == JSON_OBJECT || v->type == JSON_ARRAY; }

/* Emits v (its key first when it is a member) and, for a container, its opening bracket.
 * Returns true if v is a non-empty container to descend into. */
static bool emit_open(json_writer_t *w, const json_value_t *v, bool member) {
    if (member) {
        json_w_key(w, v->key);
    }
    switch (v->type) {
    case JSON_OBJECT: json_w_begin_object(w); break;
    case JSON_ARRAY: json_w_begin_array(w); break;
    case JSON_STRING: json_w_string(w, v->u.s); break;
    case JSON_NUMBER: json_w_number_text(w, v->u.s); break;
    case JSON_BOOL: json_w_bool(w, v->b); break;
    default: json_w_null(w); break;
    }
    return is_container(v) && v->u.c.first != NULL;
}

static void emit_close(json_writer_t *w, const json_value_t *v) {
    if (v->type == JSON_OBJECT) {
        json_w_end_object(w);
    } else {
        json_w_end_array(w);
    }
}

static bool is_member(const json_value_t *v, const json_value_t *top) {
    return v != top && v->parent != NULL && v->parent->type == JSON_OBJECT;
}

bool json_write(core_buf_t *b, const json_value_t *top) {
    json_writer_t w;
    const json_value_t *cur = top;
    bool down = true;
    if (top == NULL || !json_w_init(&w, b)) {
        return false;
    }
    while (cur != NULL) {
        if (down && emit_open(&w, cur, is_member(cur, top))) {
            cur = cur->u.c.first;
            continue;
        }
        if (is_container(cur)) {
            emit_close(&w, cur);
        }
        if (cur == top) {
            break;
        }
        down = cur->next != NULL;
        cur = down ? cur->next : cur->parent;
    }
    return json_w_done(&w);
}
