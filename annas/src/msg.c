#include "annas/src/internal.h"

typedef struct {
    char *dst;
    size_t o;
    size_t boundary;   /* output length where the echo starts; SIZE_MAX until reached */
    size_t cut;
} sink_t;

/* True once `cut` bytes of the echo part are in. Every message fits ANNAS_MSG_MAX on its own: the longest text is a
 * constant, a status number or a name (at most ANNAS_NAME_MAX), plus an echo cut to 512 bytes at most. */
static bool sink_full(const sink_t *s) { return s->boundary != SIZE_MAX && s->o - s->boundary >= s->cut; }

/* Appends c unless the echo is cut. */
static void sink_put(sink_t *s, char c) {
    if (!sink_full(s)) {
        s->dst[s->o] = c;
        s->o++;
    }
}

static void put_redacted(sink_t *s) {
    static const char word[] = URL_REDACTED;
    for (size_t i = 0; i < sizeof word - 1u; i++) {
        sink_put(s, word[i]);
    }
}

/* Steps 1 and 2 of the finalisation in one left-to-right pass over the complete text, until the message is full. */
static void finalise(sink_t *s, const cat_t *t, core_str_t key) {
    size_t total = cat_len(t);
    size_t pos = 0;
    while (pos < total && !sink_full(s)) {
        if (pos >= t->a.len && s->boundary == SIZE_MAX) {
            s->boundary = s->o;
        }
        if (key_match(t, pos, key)) {
            put_redacted(s);
            pos += key.len;
        } else {
            sink_put(s, (char)cat_at(t, pos));
            pos++;
        }
    }
}

void msg_ok(annas_error_t *err) {
    err->code = ANNAS_OK;
    err->http_status = 0;
    err->message_len = 0;
    err->message[0] = '\0';
}

annas_code_t msg_fail(annas_error_t *err, annas_code_t code, int status, core_str_t key, const msg_body_t *body) {
    cat_t t = {body->prefix, body->echo, true, false};
    sink_t s = {err->message, 0, SIZE_MAX, body->cut};
    finalise(&s, &t, key);
    err->message[s.o] = '\0';
    err->message_len = s.o;
    err->code = code;
    err->http_status = status;
    return code;
}

annas_code_t msg_text(annas_error_t *err, annas_code_t code, int status, core_str_t key, const char *text) {
    core_str_t none = {NULL, 0};
    msg_body_t body = {core_str_from_cstr(text), none, MSG_NO_CUT};
    return msg_fail(err, code, status, key, &body);
}

core_str_t msg_u64(char *buf, size_t cap, const char *lead, uint64_t n, const char *tail) {
    core_buf_t b;
    (void)core_buf_init(&b, buf, cap); /* every caller passes its own array: cannot fail */
    core_buf_put_str(&b, core_str_from_cstr(lead));
    core_buf_put_u64(&b, n);
    core_buf_put_str(&b, core_str_from_cstr(tail));
    return core_buf_str(&b);
}
