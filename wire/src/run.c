/* wire/src/run.c: wire_run: the arguments, the order of work and the wiring. */
#include "wire/src/internal.h"

/* Writes the captured stdout text, then the stderr text; returns `code` or WIRE_EXIT_IO when a write failed. */
static int emit(wire_mem_t *m, const wire_io_t *io, int code)
{
    if (!wire_write_all(io, WIRE_STDOUT, m->out, core_buf_len(&m->outb))) {
        (void)wire_write_all(io, WIRE_STDERR, WIRE_ERR_STDOUT, sizeof WIRE_ERR_STDOUT - 1u); /* best effort */
        return WIRE_EXIT_IO;
    }
    if (!wire_write_all(io, WIRE_STDERR, m->err, core_buf_len(&m->errb))) {
        return WIRE_EXIT_IO;
    }
    return code;
}

/* A text for stderr and the exit code that goes with it. */
static int fail_text(const wire_io_t *io, const char *text, int code)
{
    return wire_write_all(io, WIRE_STDERR, text, core_str_from_cstr(text).len) ? code : WIRE_EXIT_IO;
}

/* The usage error text with every occurrence of the layered key replaced by "REDACTED". */
static void put_scrubbed(wire_mem_t *m)
{
    core_str_t key;
    core_str_t rest = {m->usage.text, m->usage.len};
    ports_env_t env = app_env_layer_port(&m->layer);
    (void)wire_env_get(&env, "ANNAS_SECRET_KEY", &key); /* ports_env_t: not found sets {NULL, 0} */
    while (key.len > 0u) {
        size_t at = core_str_find(rest, key);
        if (at == CORE_NPOS) {
            break;
        }
        core_buf_put(&m->errb, rest.ptr, at);
        core_buf_put(&m->errb, "REDACTED", 8u);
        rest.ptr += at + key.len;
        rest.len -= at + key.len;
    }
    core_buf_put(&m->errb, rest.ptr, rest.len);
}

static int usage_failure(wire_mem_t *m, const wire_io_t *io, char *const *envp)
{
    const char *text = NULL;
    /* Not strict: a bad `.env` is ignored, and posix_env_init / app_env_layer_init only fail for NULL arguments, which
     * envp (NULL = empty) and wire's own objects are not. */
    (void)wire_env_setup(m, io, envp, false, &text);
    put_scrubbed(m);
    return emit(m, io, WIRE_EXIT_CONFIG);
}

static int info(wire_mem_t *m, const wire_io_t *io)
{
    int rc = app_cli_run(&m->cmd, NULL, &m->outb, &m->errb);
    return emit(m, io, rc);
}

/* download, download-url and mcp: configuration, CA bundle, dependencies, then the command. */
static int run_net(wire_mem_t *m, const wire_io_t *io, char *const *envp)
{
    const char *text = NULL;
    size_t ca_len;
    int rc = wire_env_setup(m, io, envp, true, &text);
    if (rc == 0) {
        rc = wire_ca_load(m, io, &ca_len, &text);
    }
    if (rc != 0) {
        return fail_text(io, text, rc);
    }
    if (wire_deps_build(m, io, ca_len) != 0) {
        return fail_text(io, WIRE_ERR_INTERNAL, WIRE_EXIT_INTERNAL);
    }
    if (m->cmd.kind == APP_CMD_MCP) {
        rc = wire_mcp_loop(m, io);
    } else {
        rc = app_cli_run(&m->cmd, &m->deps, &m->outb, &m->errb);
    }
    tls_stream_deinit(&m->tls);
    wire_lazyfs_close(&m->lazyfs);
    return m->cmd.kind == APP_CMD_MCP ? rc : emit(m, io, rc);
}

static int run_cli(wire_mem_t *m, const wire_io_t *io, int argc, const char *const *argv, char *const *envp)
{
    if (!app_cli_parse(argc, argv, &m->cmd, &m->usage)) {
        return usage_failure(m, io, envp);
    }
    if (m->cmd.kind == APP_CMD_HELP || m->cmd.kind == APP_CMD_VERSION) {
        return info(m, io);
    }
    return run_net(m, io, envp);
}

static bool is_info_flag(const char *arg)
{
    core_str_t a = core_str_from_cstr(arg);
    return core_str_eq(a, core_str_from_cstr("--help")) || core_str_eq(a, core_str_from_cstr("-h")) ||
           core_str_eq(a, core_str_from_cstr("--version"));
}

static int run_mcp(wire_mem_t *m, const wire_io_t *io, int argc, const char *const *argv, char *const *envp)
{
    if (argc == 1) {
        m->cmd.kind = APP_CMD_MCP;
        return run_net(m, io, envp);
    }
    if (argc == 2 && is_info_flag(argv[1])) {
        return run_cli(m, io, argc, argv, envp);
    }
    return fail_text(io, WIRE_ERR_MCP_ARGS, WIRE_EXIT_CONFIG);
}

int wire_run(wire_mem_t *m, wire_mode_t mode, const wire_io_t *io, int argc, const char *const *argv,
             char *const *envp)
{
    if (m == NULL || io == NULL || io->sys == NULL || io->sys->read_file == NULL || io->sys->read_in == NULL ||
        io->sys->write_out == NULL || io->sys->unix_seconds == NULL || argv == NULL || argc < 1 ||
        (mode != WIRE_CLI && mode != WIRE_MCP)) {
        return WIRE_EXIT_INTERNAL;
    }
    if (!core_buf_init(&m->outb, m->out, sizeof m->out) || !core_buf_init(&m->errb, m->err, sizeof m->err)) {
        return WIRE_EXIT_INTERNAL;
    }
    return mode == WIRE_CLI ? run_cli(m, io, argc, argv, envp) : run_mcp(m, io, argc, argv, envp);
}
