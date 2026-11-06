/* wire/src/loop.c: the MCP line loop: newline-delimited JSON-RPC from stdin to stdout. */
#include "wire/src/internal.h"

#define LINE_CAP (APP_MCP_LINE_MAX + 1u)

/* Handles the assembled line m->line[0..len) and writes its reply, if any. false: stdout failed. */
static bool handle_line(wire_mem_t *m, const wire_io_t *io, size_t len)
{
    core_str_t line = {m->line, len};
    if (!core_buf_init(&m->outb, m->out, WIRE_OUT_CAP)) {
        return true;
    }
    if (app_mcp_handle(&m->mcp, line, &m->outb) != APP_MCP_REPLY) {
        return true;
    }
    core_buf_put_char(&m->outb, '\n');
    if (core_buf_overflow(&m->outb)) {
        return true;
    }
    return wire_write_all(io, WIRE_STDOUT, m->out, core_buf_len(&m->outb));
}

static int stdout_failed(const wire_io_t *io)
{
    (void)wire_write_all(io, WIRE_STDERR, WIRE_ERR_STDOUT, sizeof WIRE_ERR_STDOUT - 1u); /* best effort */
    return WIRE_EXIT_IO;
}

/* Feeds n bytes of m->chunk; *len is the length of the line being assembled (at most LINE_CAP: the rest of a
 * longer line is dropped until its newline). Returns 0 or an exit code. */
static int feed(wire_mem_t *m, const wire_io_t *io, size_t n, size_t *len)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = m->chunk[i];
        if (c != (unsigned char)'\n') {
            if (*len < LINE_CAP) {
                m->line[*len] = (char)c;
                *len += 1u;
            }
            continue;
        }
        size_t l = *len;
        *len = 0;
        if (l > 0u && !handle_line(m, io, l)) {
            return stdout_failed(io);
        }
    }
    return 0;
}

int wire_mcp_loop(wire_mem_t *m, const wire_io_t *io)
{
    size_t len = 0;
    if (!app_mcp_init(&m->mcp, &m->deps, m->parse, sizeof m->parse)) {
        return WIRE_EXIT_INTERNAL;
    }
    for (;;) {
        int64_t n = wire_read_some(io, m->chunk, sizeof m->chunk);
        if (n < 0) {
            (void)wire_write_all(io, WIRE_STDERR, WIRE_ERR_STDIN, sizeof WIRE_ERR_STDIN - 1u); /* best effort */
            return WIRE_EXIT_IO;
        }
        if (n == 0) {
            break;
        }
        int rc = feed(m, io, (size_t)n, &len);
        if (rc != 0) {
            return rc;
        }
    }
    if (len > 0u && !handle_line(m, io, len)) {
        return stdout_failed(io);
    }
    return 0;
}
