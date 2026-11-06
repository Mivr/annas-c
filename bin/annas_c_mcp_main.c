/* bin/annas_c_mcp_main.c: the `annas-c-mcp` binary. The composition root: it owns the working memory (static storage) and
 * calls wire_run(); everything else, and all the logic, is in wire/. */
#include "wire/wire.h"

static wire_mem_t g_mem;

int main(int argc, char **argv, char **envp)
{
    return wire_run(&g_mem, WIRE_MCP, &wire_io_libc, argc, (const char *const *) argv, envp);
}
