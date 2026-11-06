#include "app/src/internal.h"

#include "core/parse.h"

#define OPT_BIT(o) (1u << (unsigned)(o))

typedef enum { O_NAME = 0, O_FORMAT, O_PATH, O_DOMAIN, O_TIMEOUT } opt_id_t;

typedef struct {
    const char *name;
    opt_id_t id;
} optdef_t;

static const optdef_t k_opts[] = {
    {"--name", O_NAME}, {"--format", O_FORMAT}, {"--path-index", O_PATH}, {"--domain-index", O_DOMAIN},
    {"--timeout", O_TIMEOUT}};

typedef struct {
    const char *name;
    app_cmd_kind_t kind;
    size_t npos;
    unsigned opts;
} spec_t;

#define DL_OPTS (OPT_BIT(O_PATH) | OPT_BIT(O_DOMAIN) | OPT_BIT(O_TIMEOUT))

static const spec_t k_specs[] = {
    {"download", APP_CMD_DOWNLOAD, 1, DL_OPTS | OPT_BIT(O_NAME) | OPT_BIT(O_FORMAT)},
    {"book-download", APP_CMD_DOWNLOAD, 2, 0},
    {"download-url", APP_CMD_DOWNLOAD_URL, 1, DL_OPTS},
    {"mcp", APP_CMD_MCP, 0, 0},
    {"--help", APP_CMD_HELP, 0, 0},
    {"-h", APP_CMD_HELP, 0, 0},
    {"--version", APP_CMD_VERSION, 0, 0}};

/* A usage problem: "<msg>" [+ "<opt>: "] + the argument, cut and made printable. */
typedef struct {
    const char *msg;
    const char *opt;
    core_str_t arg;
} problem_t;

typedef struct {
    const spec_t *spec;
    int argc;
    const char *const *argv;
    app_cmd_t cmd;
    core_str_t pos[2];
    size_t np;
    unsigned seen;
} parse_t;

static core_str_t cs(const char *c) { return core_str_from_cstr(c); }

static bool problem(problem_t *p, const char *msg, const char *opt, core_str_t arg) {
    p->msg = msg;
    p->opt = opt;
    p->arg = arg;
    return false;
}

static void usage_text(app_usage_error_t *err, const problem_t *p) {
    core_buf_t b;
    char cut[APP_ECHO_MAX];
    (void)core_buf_init(&b, err->text, sizeof err->text);
    core_buf_put_str(&b, cs("annas-c: E_ARG: "));
    core_buf_put_str(&b, cs(p->msg));
    if (p->opt != NULL) {
        core_buf_put_str(&b, cs(p->opt));
        core_buf_put_str(&b, cs(": "));
    }
    core_buf_put(&b, cut, app_echo_cut(p->arg, cut));
    core_buf_put_str(&b, cs("\nTry 'annas-c --help' for usage.\n"));
    err->len = core_buf_len(&b);
}

static bool is_option(core_str_t a) { return a.len > 1u && a.ptr[0] == '-'; }

static bool parse_i64(core_str_t s, int64_t *out) {
    bool neg = s.len > 0 && s.ptr[0] == '-';
    core_str_t d = s;
    uint64_t m = 0;
    if (neg) {
        d.ptr++;
        d.len--;
    }
    if (!core_parse_u64(d, &m)) {
        return false;
    }
    if (!neg) {
        *out = (int64_t)m;
        return m <= (uint64_t)INT64_MAX;
    }
    if (m > (uint64_t)INT64_MAX + 1u) {
        return false;
    }
    *out = m == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)m;
    return true;
}

static bool store_opt(parse_t *ps, opt_id_t id, core_str_t v) {
    app_cmd_t *c = &ps->cmd;
    switch (id) {
    case O_NAME:
        c->title = v;
        return true;
    case O_FORMAT:
        c->format = v;
        return true;
    case O_PATH:
        c->has_path_index = core_parse_u64(v, &c->path_index);
        return c->has_path_index;
    case O_DOMAIN:
        c->has_domain_index = core_parse_u64(v, &c->domain_index);
        return c->has_domain_index;
    default:
        return parse_i64(v, &c->timeout_seconds);
    }
}

static const optdef_t *find_opt(const spec_t *sp, core_str_t arg) {
    for (size_t i = 0; i < sizeof k_opts / sizeof k_opts[0]; i++) {
        if ((sp->opts & OPT_BIT(k_opts[i].id)) != 0u && core_str_eq(arg, cs(k_opts[i].name))) {
            return &k_opts[i];
        }
    }
    return NULL;
}

/* Handles the option at argv[*i] and its value; advances *i to the value. */
static bool parse_option(parse_t *ps, int *i, problem_t *p) {
    core_str_t arg = cs(ps->argv[*i]);
    const optdef_t *o = find_opt(ps->spec, arg);
    core_str_t v;
    if (o == NULL) {
        return problem(p, "unknown option: ", NULL, arg);
    }
    if (*i + 1 >= ps->argc) {
        return problem(p, "option needs a value: ", NULL, arg);
    }
    if ((ps->seen & OPT_BIT(o->id)) != 0u) {
        return problem(p, "option given twice: ", NULL, arg);
    }
    ps->seen |= OPT_BIT(o->id);
    (*i)++;
    v = cs(ps->argv[*i]);
    if (!store_opt(ps, o->id, v)) {
        return problem(p, "invalid value for ", o->name, v);
    }
    return true;
}

/* Index of the last '.' of s when it has a byte before and after it, else SIZE_MAX. */
static size_t split_dot(core_str_t s) {
    for (size_t i = s.len; i > 0; i--) {
        if (s.ptr[i - 1u] == '.') {
            return (i - 1u > 0 && i < s.len) ? i - 1u : SIZE_MAX;
        }
    }
    return SIZE_MAX;
}

static bool parse_positional(parse_t *ps, core_str_t arg, problem_t *p) {
    if (ps->np >= ps->spec->npos) {
        return problem(p, "unexpected argument: ", NULL, arg);
    }
    ps->pos[ps->np] = arg;
    ps->np++;
    if (ps->spec->npos == 2u && ps->np == 2u && split_dot(arg) == SIZE_MAX) {
        return problem(p, "filename must be <title>.<ext>, got: ", NULL, arg);
    }
    return true;
}

static bool finish(parse_t *ps, problem_t *p) {
    app_cmd_t *c = &ps->cmd;
    core_str_t none = {NULL, 0};
    if (ps->np < ps->spec->npos) {
        return problem(p, ps->np == 0 ? "missing <md5>" : "missing <filename.ext>", NULL, none);
    }
    c->kind = ps->spec->kind;
    c->md5 = ps->pos[0]; /* empty for the commands without positionals */
    if (ps->spec->npos == 2u) {
        size_t d = split_dot(ps->pos[1]);
        c->title.ptr = ps->pos[1].ptr;
        c->title.len = d;
        c->format.ptr = ps->pos[1].ptr + d + 1u;
        c->format.len = ps->pos[1].len - d - 1u;
    }
    return true;
}

static const spec_t *find_spec(core_str_t a) {
    for (size_t i = 0; i < sizeof k_specs / sizeof k_specs[0]; i++) {
        if (core_str_eq(a, cs(k_specs[i].name))) {
            return &k_specs[i];
        }
    }
    return NULL;
}

static bool parse_args(parse_t *ps, problem_t *p) {
    core_str_t none = {NULL, 0};
    core_str_t a1;
    if (ps->argc < 2) {
        return problem(p, "missing command", NULL, none);
    }
    a1 = cs(ps->argv[1]);
    ps->spec = find_spec(a1);
    if (ps->spec == NULL) {
        return problem(p, is_option(a1) ? "unknown option: " : "unknown command: ", NULL, a1);
    }
    for (int i = 2; i < ps->argc; i++) {
        core_str_t a = cs(ps->argv[i]);
        if (is_option(a) ? !parse_option(ps, &i, p) : !parse_positional(ps, a, p)) {
            return false;
        }
    }
    return finish(ps, p);
}

bool app_cli_parse(int argc, const char *const *argv, app_cmd_t *cmd, app_usage_error_t *err) {
    parse_t ps = {0};
    problem_t p = {"missing command", NULL, {NULL, 0}};
    bool ok = false;
    ps.argc = argc;
    ps.argv = argv;
    ps.cmd.timeout_seconds = ANNAS_DEFAULT_TIMEOUT_SECONDS;
    if (cmd != NULL && argv != NULL && argc >= 1) {
        ok = parse_args(&ps, &p);
    }
    if (ok) {
        *cmd = ps.cmd;
    } else if (err != NULL) {
        usage_text(err, &p);
    }
    return ok;
}

/* ---- running ---------------------------------------------------------------------------------------------- */

#define HELP_TEXT                                                                                                  \
    "annas-c " ANNAS_VERSION ": download files from Anna's Archive with a member account key\n"                    \
    "\n"                                                                                                           \
    "Usage:\n"                                                                                                     \
    "  annas-c download <md5> [--name <title>] [--format <ext>] [--path-index <n>] [--domain-index <n>] [--timeout <seconds>]\n" \
    "  annas-c book-download <md5> <filename.ext>\n"                                                               \
    "  annas-c download-url <md5> [--path-index <n>] [--domain-index <n>] [--timeout <seconds>]\n"                 \
    "  annas-c mcp\n"                                                                                              \
    "  annas-c --help\n"                                                                                           \
    "  annas-c --version\n"                                                                                        \
    "\n"                                                                                                           \
    "Environment (a .env file in the current directory is read first, then the process environment wins):\n"       \
    "  ANNAS_SECRET_KEY     member account key (required)\n"                                                       \
    "  ANNAS_DOWNLOAD_PATH  absolute download directory (required by download)\n"                                  \
    "  ANNAS_BASE_URL       domain of the site (default annas-archive.gl)\n"                                       \
    "\n"                                                                                                           \
    "Exit codes: 0 ok, 2 bad usage or configuration, 3 browser check, 4 HTTP or API error,\n"                      \
    "5 network error or timeout, 6 file error, 7 parse error\n"

static int cli_internal(core_buf_t *errout) {
    core_buf_put_str(errout, cs(APP_INTERNAL_LINE "\n"));
    return 1;
}

static app_op_t op_of(const app_cmd_t *cmd) {
    app_op_t op;
    op.req.md5 = cmd->md5;
    op.req.title = cmd->title;
    op.req.format = cmd->format;
    op.req.has_path_index = cmd->has_path_index;
    op.req.path_index = cmd->path_index;
    op.req.has_domain_index = cmd->has_domain_index;
    op.req.domain_index = cmd->domain_index;
    op.timeout_seconds = cmd->timeout_seconds;
    op.download = cmd->kind == APP_CMD_DOWNLOAD;
    return op;
}

/* Success text goes to out, built in the free space of errout; failure text goes to errout, built behind the job
 * in the free space of out. */
static int cli_finish(const app_op_t *op, const app_deps_t *deps, app_op_status_t st, const app_job_t *job,
                      app_span_t behind_job, core_buf_t *out, core_buf_t *errout) {
    bool ok = st == APP_OP_OK;
    app_span_t sp = ok ? app_span_of(errout) : behind_job;
    char *mem = app_span_take(&sp, APP_TEXT_CAP);
    core_buf_t text = {NULL, 0, 0, false};
    if (mem == NULL) {
        return cli_internal(errout);
    }
    (void)core_buf_init(&text, mem, APP_TEXT_CAP);
    app_op_text(op, job, st, &text);
    core_buf_put_char(&text, '\n');
    app_emit(ok ? out : errout, &text, app_key(deps));
    return ok ? 0 : annas_exit_code(job->err.code);
}

static int cli_op(const app_cmd_t *cmd, const app_deps_t *deps, core_buf_t *out, core_buf_t *errout) {
    app_op_t op = op_of(cmd);
    app_span_t sp = app_span_of(out);
    app_job_t *job = app_span_take(&sp, sizeof *job);
    app_op_status_t st;
    if (deps == NULL || job == NULL) {
        return cli_internal(errout);
    }
    st = app_op_run(deps, &op, job);
    if (st == APP_OP_INTERNAL) {
        return cli_internal(errout);
    }
    return cli_finish(&op, deps, st, job, sp, out, errout);
}

int app_cli_run(const app_cmd_t *cmd, const app_deps_t *deps, core_buf_t *out, core_buf_t *errout) {
    if (out == NULL || errout == NULL) {
        return 1;
    }
    if (cmd == NULL) {
        return cli_internal(errout);
    }
    switch (cmd->kind) {
    case APP_CMD_HELP:
        core_buf_put_str(out, cs(HELP_TEXT));
        return 0;
    case APP_CMD_VERSION:
        core_buf_put_str(out, cs("annas-c " APP_VERSION "\n"));
        return 0;
    case APP_CMD_DOWNLOAD:
    case APP_CMD_DOWNLOAD_URL:
        return cli_op(cmd, deps, out, errout);
    default:
        return cli_internal(errout);
    }
}
