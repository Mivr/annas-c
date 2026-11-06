/* wire/src/internal.h: the private prototypes shared by config.c (.env, CA bundle, dependencies), io.c (bounded read /
 * write loops), lazyfs.c (the lazily opened download directory), loop.c (the MCP loop), run.c (wire_run) and sys.c (the
 * libc seam). The public contract is wire/wire.h. */
#ifndef WIRE_SRC_INTERNAL_H
#define WIRE_SRC_INTERNAL_H

#include "wire/wire.h"

#define WIRE_ERR_INTERNAL "annas-c: internal error: invalid dependencies\n"

/* config.c. Each returns 0, or an exit code (WIRE_EXIT_CONFIG / WIRE_EXIT_INTERNAL) with *text the stderr text. */
int wire_env_setup(wire_mem_t *m, const wire_io_t *io, char *const *envp, bool strict, const char **text);
int wire_ca_load(wire_mem_t *m, const wire_io_t *io, size_t *ca_len, const char **text);
int wire_deps_build(wire_mem_t *m, const wire_io_t *io, size_t ca_len);
bool wire_env_get(const ports_env_t *env, const char *name, core_str_t *value);

/* io.c: all bytes written / one read; false / -1 on failure (the caller reports it). */
bool wire_write_all(const wire_io_t *io, int fd, const char *buf, size_t len);
int64_t wire_read_some(const wire_io_t *io, void *buf, size_t cap);

/* lazyfs.c */
void wire_lazyfs_init(wire_lazyfs_t *l, const wire_io_t *io, ports_env_t env);
ports_fs_t wire_lazyfs_port(wire_lazyfs_t *l);
void wire_lazyfs_close(wire_lazyfs_t *l);

/* loop.c: runs until end of input; returns the exit code (0 or WIRE_EXIT_IO). */
int wire_mcp_loop(wire_mem_t *m, const wire_io_t *io);

#endif
