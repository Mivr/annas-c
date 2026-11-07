/* wire/src/config.c: the `.env` file, the layered environment, the CA bundle and the dependencies of app. */
#include <errno.h>

#include "wire/src/internal.h"

static char *const k_empty_env[1] = {NULL};

bool wire_env_get(const ports_env_t *env, const char *name, core_str_t *value)
{
    return env->ops->get(env->ctx, core_str_from_cstr(name), value);
}

static int dotenv_load(wire_mem_t *m, const wire_io_t *io, const char **text)
{
    m->denv.count = 0;
    int64_t n = io->sys->read_file(io->sys_ctx, WIRE_DOTENV_PATH, m->dotenv, WIRE_DOTENV_MAX + 1u);
    if (n == -(int64_t)ENOENT) {
        return 0;
    }
    if (n < 0 || (uint64_t)n > (uint64_t)(WIRE_DOTENV_MAX + 1u)) {
        *text = WIRE_ERR_DOTENV_READ;
        return WIRE_EXIT_CONFIG;
    }
    if ((size_t)n > WIRE_DOTENV_MAX) {
        *text = WIRE_ERR_DOTENV_BIG;
        return WIRE_EXIT_CONFIG;
    }
    core_str_t file = {m->dotenv, (size_t)n};
    if (!app_dotenv_parse(file, &m->denv)) { /* app: count is 0 on failure */
        *text = WIRE_ERR_DOTENV_MANY;
        return WIRE_EXIT_CONFIG;
    }
    return 0;
}

int wire_env_setup(wire_mem_t *m, const wire_io_t *io, char *const *envp, bool strict, const char **text)
{
    int rc = dotenv_load(m, io, text);
    if (rc != 0 && strict) {
        return rc; /* best effort (not strict): the failure is ignored; every failure leaves no entries */
    }
    if (!posix_env_init(&m->penv, envp != NULL ? envp : k_empty_env) ||
        !app_env_layer_init(&m->layer, &m->denv, posix_env_port(&m->penv))) {
        *text = WIRE_ERR_INTERNAL;
        return WIRE_EXIT_INTERNAL;
    }
    return 0;
}

/* Reads one candidate into m->ca (a count above the cap is the seam's bug; it is reported as too big). */
static int64_t ca_read(wire_mem_t *m, const wire_io_t *io, const char *path)
{
    return io->sys->read_file(io->sys_ctx, path, m->ca, WIRE_CA_MAX + 1u);
}

/* The path named by SSL_CERT_FILE: copied to a C string, or refused when it cannot be one. */
static int64_t ca_read_named(wire_mem_t *m, const wire_io_t *io, core_str_t v)
{
    if (v.len >= WIRE_PATH_MAX) {
        return -(int64_t)ENAMETOOLONG;
    }
    for (size_t i = 0; i < v.len; i++) {
        if (v.ptr[i] == '\0') {
            return -(int64_t)EINVAL;
        }
        m->ca_path[i] = v.ptr[i];
    }
    m->ca_path[v.len] = '\0';
    return ca_read(m, io, m->ca_path);
}

static int64_t ca_read_default(wire_mem_t *m, const wire_io_t *io)
{
    static const char *const k_paths[WIRE_CA_PATH_COUNT] = {WIRE_CA_PATH_1, WIRE_CA_PATH_2, WIRE_CA_PATH_3};
    for (size_t i = 0; i < WIRE_CA_PATH_COUNT; i++) {
        int64_t n = ca_read(m, io, k_paths[i]);
        if (n >= 0) {
            return n;
        }
    }
    return -(int64_t)ENOENT;
}

int wire_ca_load(wire_mem_t *m, const wire_io_t *io, size_t *ca_len, const char **text)
{
    ports_env_t env = app_env_layer_port(&m->layer);
    core_str_t v;
    bool named = wire_env_get(&env, "SSL_CERT_FILE", &v) && v.len > 0;
    int64_t n = named ? ca_read_named(m, io, v) : ca_read_default(m, io);
    if (n < 0) {
        *text = named ? WIRE_ERR_CA_READ : WIRE_ERR_CA_NONE;
    } else if (n == 0) {
        *text = WIRE_ERR_CA_EMPTY;
    } else if ((uint64_t)n > (uint64_t)WIRE_CA_MAX) {
        *text = WIRE_ERR_CA_BIG;
    } else {
        m->ca[n] = '\0';
        *ca_len = (size_t)n;
        return 0;
    }
    return WIRE_EXIT_CONFIG;
}

static bool tls_setup(wire_mem_t *m, const wire_io_t *io, ports_clock_t clock, size_t ca_len)
{
    tls_stream_config_t cfg;
    (void)tls_stream_sock_init(&m->sock, clock); /* false only for a NULL object or a clock without now_ms */
    cfg.transport = io->transport != NULL ? *io->transport : tls_stream_sock_transport(&m->sock);
    cfg.clock = clock;
    m->wall.unix_seconds = io->sys->unix_seconds;
    m->wall.ctx = io->sys_ctx;
    cfg.wall = m->wall;
    cfg.ca_pem.ptr = m->ca;
    cfg.ca_pem.len = ca_len + 1u;
    cfg.heap = m->heap;
    cfg.heap_size = sizeof m->heap;
    return tls_stream_init(&m->tls, &cfg);
}

int wire_deps_build(wire_mem_t *m, const wire_io_t *io, size_t ca_len)
{
    const posix_clock_sys_t *cs = io->clock_sys != NULL ? io->clock_sys : &posix_clock_sys_libc;
    if (!posix_clock_init(&m->clock, cs, io->clock_sys_ctx)) {
        return WIRE_EXIT_INTERNAL;
    }
    ports_clock_t clock = posix_clock_port(&m->clock);
    if (!tls_setup(m, io, clock, ca_len)) {
        return WIRE_EXIT_INTERNAL;
    }
    ports_env_t env = app_env_layer_port(&m->layer);
    wire_lazyfs_init(&m->lazyfs, io, env);
    m->deps.stream = tls_stream_port(&m->tls);
    m->deps.fs = wire_lazyfs_port(&m->lazyfs);
    m->deps.clock = clock;
    m->deps.env = env;
    m->deps.workspace = m->workspace;
    m->deps.workspace_cap = sizeof m->workspace;
    m->deps.scratch = m->scratch;
    m->deps.scratch_cap = sizeof m->scratch;
    return 0;
}
