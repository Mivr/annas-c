#include "app/app.h"

/* STUB (test-author task A014). */

bool app_mcp_init(app_mcp_t *s, const app_deps_t *deps, void *parse_mem, size_t parse_cap) {
    (void)s;
    (void)deps;
    (void)parse_mem;
    (void)parse_cap;
    return false;
}

app_mcp_result_t app_mcp_handle(app_mcp_t *s, core_str_t line, core_buf_t *out) {
    (void)s;
    (void)line;
    (void)out;
    return APP_MCP_NO_REPLY;
}
