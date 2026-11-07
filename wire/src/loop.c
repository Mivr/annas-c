/* wire/src/loop.c: the MCP line loop: newline-delimited JSON-RPC from stdin to stdout. */
#include "wire/src/internal.h"

#define LINE_CAP (APP_MCP_LINE_MAX + 1u)

/* Handles the assembled line and writes its reply, if any. false: stdout failed. A line that did not fit (the buffer
 * is in overflow) is handed over as APP_MCP_LINE_MAX + 1 bytes: app answers "line longer" without reading them. A line
 * with no bytes gets no reply (app). */
static bool handle_line(wire_mem_t *m, const wire_io_t *io, const core_buf_t *line)
{
    size_t n = core_buf_overflow(line) ? APP_MCP_LINE_MAX + 1u : core_buf_len(line);
    core_str_t text = {m->line, n};
    (void)core_buf_init(&m->outb, m->out, WIRE_OUT_CAP); /* false only for NULL storage */
    if (app_mcp_handle(&m->mcp, text, &m->outb) != APP_MCP_REPLY) {
        return true;
    }
    core_buf_put_char(&m->outb, '\n'); /* WIRE_OUT_CAP is above app's reply limit plus this byte */
    return wire_write_all(io, WIRE_STDOUT, m->out, core_buf_len(&m->outb));
}

static int stdout_failed(const wire_io_t *io)
{
    (void)wire_write_all(io, WIRE_STDERR, WIRE_ERR_STDOUT, sizeof WIRE_ERR_STDOUT - 1u); /* best effort */
    return WIRE_EXIT_IO;
}

/* Feeds n bytes of m->chunk into `line`, which holds at most APP_MCP_LINE_MAX bytes of the line being assembled (the
 * rest of a longer line is dropped until its newline). Returns 0 or an exit code. */
static int feed(wire_mem_t *m, const wire_io_t *io, size_t n, core_buf_t *line)
{
    for (size_t i = 0; i < n; i++) {
        char c = (char)m->chunk[i];
        if (c != '\n') {
            core_buf_put_char(line, c);
            continue;
        }
        bool written = handle_line(m, io, line);
        (void)core_buf_init(line, m->line, LINE_CAP); /* false only for NULL storage */
        if (!written) {
            return stdout_failed(io);
        }
    }
    return 0;
}

int wire_mcp_loop(wire_mem_t *m, const wire_io_t *io)
{
    core_buf_t line;
    (void)core_buf_init(&line, m->line, LINE_CAP); /* false only for NULL storage */
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
        int rc = feed(m, io, (size_t)n, &line);
        if (rc != 0) {
            return rc;
        }
    }
    if (!handle_line(m, io, &line)) {
        return stdout_failed(io);
    }
    return 0;
}
