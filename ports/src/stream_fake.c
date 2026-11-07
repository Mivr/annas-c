#include <string.h>

#include "ports/fakes.h"

_Static_assert(PORTS_FAKE_MM_NONE == 0, "a zeroed stream has no mismatch");

static ports_fake_stream_t *as_stream(void *ctx) { return (ports_fake_stream_t *)ctx; }

static const ports_fake_step_t *cur_step(const ports_fake_stream_t *s, ports_fake_step_kind_t kind) {
    if (s->pos < s->count && s->steps[s->pos].kind == kind) {
        return &s->steps[s->pos];
    }
    return NULL;
}

static void record(ports_fake_stream_t *s, ports_fake_mismatch_kind_t kind, size_t offset) {
    if (!s->failed) {
        s->failed = true;
        s->mm.kind = kind;
        s->mm.step = s->pos;
        s->mm.offset = offset;
    }
}

static ports_err_t unexpected(ports_fake_stream_t *s, ports_err_t err) {
    record(s, PORTS_FAKE_MM_UNEXPECTED_CALL, 0u);
    return err;
}

static void consume(ports_fake_stream_t *s) {
    s->pos++;
    s->progress = 0u;
}

static bool timed_out(const ports_fake_stream_t *s, uint64_t deadline) {
    return s->clock != NULL && deadline != 0u && s->clock->now >= deadline;
}

/* Index of the first differing byte; the shorter length when one is a prefix of the other. */
static size_t first_diff(core_str_t a, core_str_t b) {
    size_t i = 0u;
    while (i != a.len && i != b.len && a.ptr[i] == b.ptr[i]) {
        i++;
    }
    return i;
}

static ports_err_t stream_connect(void *ctx, core_str_t host, uint16_t port, uint64_t deadline_ms) {
    ports_fake_stream_t *s = as_stream(ctx);
    if (s == NULL) {
        return PORTS_E_ARG;
    }
    if (s->failed) {
        return PORTS_E_NET;
    }
    if (s->open) {
        return unexpected(s, PORTS_E_NET);
    }
    if (host.len == 0u || host.ptr == NULL || port == 0u) {
        return PORTS_E_ARG;
    }
    const ports_fake_step_t *st = cur_step(s, PORTS_FAKE_STEP_CONNECT);
    if (st == NULL) {
        return unexpected(s, PORTS_E_NET);
    }
    if (!core_str_eq(host, st->data) || port != st->port) {
        record(s, PORTS_FAKE_MM_CONNECT_TARGET, core_str_eq(host, st->data) ? host.len : first_diff(host, st->data));
        return PORTS_E_NET;
    }
    if (timed_out(s, deadline_ms)) {
        return PORTS_E_TIMEOUT;
    }
    consume(s);
    if (st->err == PORTS_OK) {
        s->open = true;
        s->eof = false;
        s->deadline_ms = deadline_ms;
    }
    return st->err;
}

static ports_err_t write_expect(ports_fake_stream_t *s, const ports_fake_step_t *st, const char *buf, size_t len) {
    size_t remaining = st->data.len - s->progress;
    for (size_t i = 0; i < len; i++) {
        if (i >= remaining || buf[i] != st->data.ptr[s->progress + i]) {
            record(s, PORTS_FAKE_MM_REQUEST_BYTES, s->progress + i);
            return PORTS_E_NET;
        }
    }
    s->progress += len;
    if (s->progress == st->data.len) {
        consume(s);
    }
    return PORTS_OK;
}

static ports_err_t stream_write(void *ctx, const void *buf, size_t len) {
    ports_fake_stream_t *s = as_stream(ctx);
    if (s == NULL) {
        return PORTS_E_ARG;
    }
    if (s->failed) {
        return PORTS_E_NET;
    }
    if (!s->open) {
        return unexpected(s, PORTS_E_CLOSED);
    }
    if (buf == NULL && len > 0u) {
        return PORTS_E_ARG;
    }
    if (len == 0u) {
        return PORTS_OK;
    }
    if (timed_out(s, s->deadline_ms)) {
        return PORTS_E_TIMEOUT;
    }
    const ports_fake_step_t *st = cur_step(s, PORTS_FAKE_STEP_EXPECT);
    if (st != NULL) {
        return write_expect(s, st, (const char *)buf, len);
    }
    st = cur_step(s, PORTS_FAKE_STEP_FAIL_WRITE);
    if (st == NULL) {
        return unexpected(s, PORTS_E_NET);
    }
    consume(s);
    return st->err;
}

static size_t min_size(size_t a, size_t b) { return a < b ? a : b; }

static void read_respond(ports_fake_stream_t *s, const ports_fake_step_t *st, void *buf, size_t cap, size_t *n) {
    size_t limit = st->chunk != 0u ? st->chunk : SIZE_MAX; /* chunk 0 = no limit */
    size_t k = min_size(min_size(st->data.len - s->progress, cap), limit);
    memcpy(buf, st->data.ptr + s->progress, k);
    *n = k;
    s->progress += k;
    if (s->progress == st->data.len) {
        consume(s);
    }
    if (s->clock != NULL) {
        ports_fake_clock_advance(s->clock, s->read_advance_ms);
    }
}

static ports_err_t read_step(ports_fake_stream_t *s, void *buf, size_t cap, size_t *n) {
    const ports_fake_step_t *st = cur_step(s, PORTS_FAKE_STEP_RESPOND);
    if (st != NULL) {
        read_respond(s, st, buf, cap, n);
        return PORTS_OK;
    }
    st = cur_step(s, PORTS_FAKE_STEP_EOF);
    if (st != NULL) {
        consume(s);
        s->eof = true;
        *n = 0u;
        return PORTS_OK;
    }
    st = cur_step(s, PORTS_FAKE_STEP_FAIL_READ);
    if (st == NULL) {
        return unexpected(s, PORTS_E_NET);
    }
    consume(s);
    return st->err;
}

static ports_err_t stream_read(void *ctx, void *buf, size_t cap, size_t *n) {
    ports_fake_stream_t *s = as_stream(ctx);
    if (s == NULL) {
        return PORTS_E_ARG;
    }
    if (s->failed) {
        return PORTS_E_NET;
    }
    if (!s->open) {
        return unexpected(s, PORTS_E_CLOSED);
    }
    if (buf == NULL || n == NULL || cap == 0u) {
        return PORTS_E_ARG;
    }
    if (timed_out(s, s->deadline_ms)) {
        return PORTS_E_TIMEOUT;
    }
    if (s->eof) {
        *n = 0u;
        return PORTS_OK;
    }
    return read_step(s, buf, cap, n);
}

static ports_err_t stream_close(void *ctx) {
    ports_fake_stream_t *s = as_stream(ctx);
    if (s == NULL) {
        return PORTS_E_ARG;
    }
    if (!s->open) {
        return PORTS_E_CLOSED;
    }
    s->open = false;
    s->eof = false;
    s->closes++;
    return PORTS_OK;
}

static const ports_stream_ops_t k_ops = {stream_connect, stream_write, stream_read, stream_close};

static bool err_valid(ports_err_t e) { return (unsigned)e <= (unsigned)PORTS_E_ARG; }

static bool step_valid(const ports_fake_step_t *st) {
    bool data_ok = st->data.len > 0u && st->data.ptr != NULL;
    switch (st->kind) {
    case PORTS_FAKE_STEP_CONNECT:
        return data_ok && st->port >= 1u && err_valid(st->err);
    case PORTS_FAKE_STEP_EXPECT:
    case PORTS_FAKE_STEP_RESPOND:
        return data_ok;
    case PORTS_FAKE_STEP_EOF:
        return true;
    case PORTS_FAKE_STEP_FAIL_WRITE:
    case PORTS_FAKE_STEP_FAIL_READ:
        return err_valid(st->err) && st->err != PORTS_OK;
    default:
        return false;
    }
}

static bool script_valid(const ports_fake_step_t *steps, size_t count) {
    if (steps == NULL && count > 0u) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (!step_valid(&steps[i])) {
            return false;
        }
    }
    return true;
}

bool ports_fake_stream_init(ports_fake_stream_t *s, const ports_fake_step_t *steps, size_t count,
                            ports_fake_clock_t *clock, uint64_t read_advance_ms) {
    if (s == NULL) {
        return false;
    }
    /* everything else starts at zero: no script, position 0, closed, not failed, mismatch NONE, no deadline */
    *s = (ports_fake_stream_t){.clock = clock, .read_advance_ms = read_advance_ms};
    if (!script_valid(steps, count)) {
        record(s, PORTS_FAKE_MM_UNEXPECTED_CALL, 0u); /* unusable: every call fails with E_NET */
        return false;
    }
    s->steps = steps;
    s->count = count;
    return true;
}

ports_stream_t ports_fake_stream_port(ports_fake_stream_t *s) {
    ports_stream_t p = {&k_ops, s};
    return p;
}

bool ports_fake_stream_done(const ports_fake_stream_t *s) { return s != NULL && !s->failed && s->pos == s->count; }

bool ports_fake_stream_mismatch(const ports_fake_stream_t *s, ports_fake_mismatch_t *out) {
    if (s == NULL || !s->failed || out == NULL) {
        return s != NULL && s->failed;
    }
    *out = s->mm;
    return true;
}

bool ports_fake_stream_is_open(const ports_fake_stream_t *s) { return s != NULL && s->open; }

uint32_t ports_fake_stream_closes(const ports_fake_stream_t *s) { return s == NULL ? 0u : s->closes; }

size_t ports_fake_stream_pos(const ports_fake_stream_t *s) { return s == NULL ? 0u : s->pos; }
