#include "app/src/internal.h"

#include "json/json.h"

#define PROP_HASH \
    "\"hash\":{\"type\":\"string\",\"description\":\"MD5 hash of the book (32 hexadecimal characters).\"}"
#define PROPS_COMMON                                                                                           \
    "\"path_index\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"Collection index, 0 or larger.\"},"    \
    "\"domain_index\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"Download server index, 0 or larger.\"}," \
    "\"timeout_seconds\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"Time limit in seconds (0 = server default of 3600).\"}"
#define TOOLS_LIST                                                                                             \
    "{\"tools\":[{\"name\":\"book_download\",\"description\":\""                                       \
    "Download a book by MD5 hash with a member account key and save it in the download directory.\"," \
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{" PROP_HASH ","                                       \
    "\"title\":{\"type\":\"string\",\"description\":\"File name without extension (default: the hash).\"},"      \
    "\"format\":{\"type\":\"string\",\"description\":\"File extension, 1 to 16 letters or digits (default: bin).\"}," \
    PROPS_COMMON "},\"required\":[\"hash\"],\"additionalProperties\":false}},"                                   \
    "{\"name\":\"fast_download_url\",\"description\":\""                                                \
    "Get the fast download URL and the account's download quota for an MD5 hash without downloading the file.\"," \
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{" PROP_HASH "," PROPS_COMMON                           \
    "},\"required\":[\"hash\"],\"additionalProperties\":false}}]}"

#define INIT_RESULT_HEAD "{\"protocolVersion\":\""
#define INIT_RESULT_TAIL \
    "\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"annas-c\",\"version\":\"" APP_VERSION "\"}}"

#define MSG_INTERNAL_NOMEM "Internal error: request does not fit in memory"

/* The reply under construction: `rb` is the whole rest of the arena (after the request DOM). */
typedef struct {
    app_mcp_t *s;
    const json_value_t *id; /* NULL: echo null */
    core_buf_t rb;
    unsigned char *mem;
    size_t cap;
    size_t left; /* arena memory the request DOM left */
} mctx_t;

static core_str_t cs(const char *c) { return core_str_from_cstr(c); }

static void put_cs(core_buf_t *b, const char *c) { core_buf_put_str(b, cs(c)); }

static void head(mctx_t *m) {
    put_cs(&m->rb, "{\"jsonrpc\":\"2.0\",\"id\":");
    if (m->id == NULL || !json_write(&m->rb, m->id)) {
        put_cs(&m->rb, "null"); /* ignored once the buffer has overflowed */
    }
}

static void err_reply(mctx_t *m, const char *code, const char *msg, core_str_t echo) {
    head(m);
    put_cs(&m->rb, ",\"error\":{\"code\":");
    put_cs(&m->rb, code);
    put_cs(&m->rb, ",\"message\":\"");
    app_put_escaped(&m->rb, cs(msg));
    app_put_escaped(&m->rb, echo);
    put_cs(&m->rb, "\"}}");
}

static void err_plain(mctx_t *m, const char *code, const char *msg) {
    core_str_t none = {NULL, 0};
    err_reply(m, code, msg, none);
}

static void result_raw(mctx_t *m, const char *json) {
    head(m);
    put_cs(&m->rb, ",\"result\":");
    put_cs(&m->rb, json);
    put_cs(&m->rb, "}");
}

/* ---- methods ----------------------------------------------------------------------------------------------- */

static void rt_empty(mctx_t *m, const json_value_t *root) {
    (void)root;
    result_raw(m, "{}");
}

static void rt_list(mctx_t *m, const json_value_t *root) {
    (void)root;
    result_raw(m, TOOLS_LIST);
}

static void rt_initialize(mctx_t *m, const json_value_t *root) {
    static const char *const supported[] = {"2025-06-18", "2025-03-26", "2024-11-05"};
    const json_value_t *params = json_obj_get(root, cs("params"));
    core_str_t ver = {NULL, 0};
    const char *use = supported[0];
    if (json_type(params) != JSON_OBJECT) {
        err_plain(m, "-32602", "Invalid params: params must be an object");
        return;
    }
    if (!json_get_str(json_obj_get(params, cs("protocolVersion")), &ver)) {
        err_plain(m, "-32602", "Invalid params: protocolVersion must be a string");
        return;
    }
    for (size_t i = 0; i < sizeof supported / sizeof supported[0]; i++) {
        use = core_str_eq(ver, cs(supported[i])) ? supported[i] : use;
    }
    head(m);
    put_cs(&m->rb, ",\"result\":" INIT_RESULT_HEAD);
    put_cs(&m->rb, use);
    put_cs(&m->rb, INIT_RESULT_TAIL "}");
}

/* ---- tools/call -------------------------------------------------------------------------------------------- */

typedef enum { A_HASH = 0, A_TITLE, A_FORMAT, A_PATH, A_DOMAIN, A_TIMEOUT } arg_id_t;

typedef struct {
    const char *name;
    arg_id_t id;
    bool book_only;
    const char *bad; /* the -32602 message when the value has the wrong type */
} argdef_t;

static const argdef_t k_args[] = {
    {"hash", A_HASH, false, "Invalid params: hash must be a string"},
    {"title", A_TITLE, true, "Invalid params: title must be a string"},
    {"format", A_FORMAT, true, "Invalid params: format must be a string"},
    {"path_index", A_PATH, false, "Invalid params: path_index must be an integer >= 0"},
    {"domain_index", A_DOMAIN, false, "Invalid params: domain_index must be an integer >= 0"},
    {"timeout_seconds", A_TIMEOUT, false, "Invalid params: timeout_seconds must be an integer"}};

typedef struct {
    app_op_t op;
    bool have_hash;
    const char *msg;      /* set on failure */
    core_str_t echo;      /* points into `buf` */
    char buf[APP_ECHO_MAX];
} call_t;

static bool call_fail(call_t *c, const char *msg) {
    c->msg = msg;
    return false;
}

static bool call_fail_echo(call_t *c, const char *msg, core_str_t name) {
    c->echo.len = app_echo_cut(name, c->buf);
    app_utf8_clean(c->buf, c->echo.len);
    c->echo.ptr = c->buf;
    return call_fail(c, msg);
}

static bool arg_apply(call_t *c, arg_id_t id, const json_value_t *v) {
    annas_request_t *r = &c->op.req;
    switch (id) {
    case A_HASH:
        c->have_hash = json_get_str(v, &r->md5);
        return c->have_hash;
    case A_TITLE:
        return json_get_str(v, &r->title);
    case A_FORMAT:
        return json_get_str(v, &r->format);
    case A_PATH:
        r->has_path_index = json_get_u64(v, &r->path_index);
        return r->has_path_index;
    case A_DOMAIN:
        r->has_domain_index = json_get_u64(v, &r->domain_index);
        return r->has_domain_index;
    default:
        return json_get_i64(v, &c->op.timeout_seconds);
    }
}

static const argdef_t *find_arg(core_str_t key, bool book) {
    for (size_t i = 0; i < sizeof k_args / sizeof k_args[0]; i++) {
        if ((book || !k_args[i].book_only) && core_str_eq(key, cs(k_args[i].name))) {
            return &k_args[i];
        }
    }
    return NULL;
}

static bool args_visit(call_t *c, const json_value_t *args, bool book) {
    for (size_t i = 0; i < json_len(args); i++) {
        core_str_t key = json_key_at(args, i);
        const argdef_t *d = find_arg(key, book);
        if (d == NULL) {
            return call_fail_echo(c, "Invalid params: unknown argument: ", key);
        }
        if (!arg_apply(c, d->id, json_at(args, i))) {
            return call_fail(c, d->bad);
        }
    }
    return true;
}

static bool call_check(call_t *c, const json_value_t *params) {
    core_str_t name = {NULL, 0};
    const json_value_t *args;
    bool book;
    if (json_type(params) != JSON_OBJECT) {
        return call_fail(c, "Invalid params: params must be an object");
    }
    if (!json_get_str(json_obj_get(params, cs("name")), &name)) {
        return call_fail(c, "Invalid params: name must be a string");
    }
    book = core_str_eq(name, cs("book_download"));
    if (!book && !core_str_eq(name, cs("fast_download_url"))) {
        return call_fail_echo(c, "Invalid params: unknown tool: ", name);
    }
    c->op.download = book;
    args = json_obj_get(params, cs("arguments"));
    if (args != NULL && json_type(args) != JSON_OBJECT) {
        return call_fail(c, "Invalid params: arguments must be an object");
    }
    if (args != NULL && !args_visit(c, args, book)) {
        return false;
    }
    return c->have_hash || call_fail(c, "Invalid params: hash is required");
}

/* The -32603 reply that replaces whatever was being built (id null). */
static void nomem(mctx_t *m) {
    (void)core_buf_init(&m->rb, (char *)m->mem, m->cap);
    m->id = NULL;
    err_plain(m, "-32603", MSG_INTERNAL_NOMEM);
}

/* Length of the decoded id (a string), of the text of a number, 4 for null. */
static size_t id_len(const json_value_t *id) {
    core_str_t v = {NULL, 0};
    if (json_get_str(id, &v) || json_number_text(id, &v)) {
        return v.len;
    }
    return 4u;
}

/* Runs the validated operation. The job and the outcome text live in the scratch; the reply is written to the
 * arena only once the text is complete. */
static void call_run(mctx_t *m, const app_op_t *op) {
    app_scratch_t *sc = app_scratch(&m->s->deps);
    core_buf_t tb;
    app_op_status_t st;
    if (m->left < APP_MCP_CALL_MEM + 6u * id_len(m->id)) {
        nomem(m);
        return;
    }
    st = app_op_run(&m->s->deps, op, &sc->u.w.job);
    (void)core_buf_init(&tb, sc->u.w.text, sizeof sc->u.w.text);
    app_op_text(op, &sc->u.w.job, st, &tb);
    if (core_buf_overflow(&tb)) {
        nomem(m);
        return;
    }
    app_utf8_clean(sc->u.w.text, core_buf_len(&tb));
    head(m);
    put_cs(&m->rb, ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"");
    app_put_escaped(&m->rb, core_buf_str(&tb));
    put_cs(&m->rb, st == APP_OP_OK ? "\"}],\"isError\":false}}" : "\"}],\"isError\":true}}");
}

static void rt_call(mctx_t *m, const json_value_t *root) {
    call_t c = {0};
    if (!call_check(&c, json_obj_get(root, cs("params")))) {
        err_reply(m, "-32602", c.msg, c.echo);
        return;
    }
    call_run(m, &c.op);
}

/* ---- dispatch ---------------------------------------------------------------------------------------------- */

typedef struct {
    const char *name;
    void (*fn)(mctx_t *, const json_value_t *);
} route_t;

static const route_t k_routes[] = {{"initialize", rt_initialize},
                                   {"notifications/initialized", rt_empty},
                                   {"ping", rt_empty},
                                   {"tools/list", rt_list},
                                   {"tools/call", rt_call}};

static void route(mctx_t *m, const json_value_t *root, core_str_t method) {
    for (size_t i = 0; i < sizeof k_routes / sizeof k_routes[0]; i++) {
        if (core_str_eq(method, cs(k_routes[i].name))) {
            k_routes[i].fn(m, root);
            return;
        }
    }
    err_plain(m, "-32601", "Method not found");
}

/* Validates the envelope; true when a reply was written (false: a notification). */
static bool dispatch(mctx_t *m, const json_value_t *root) {
    const json_value_t *idv = json_obj_get(root, cs("id"));
    json_type_t it = json_type(idv);
    core_str_t v = {NULL, 0};
    if (json_type(root) == JSON_ARRAY) {
        err_plain(m, "-32600", "Invalid Request: batch requests are not supported");
    } else if (json_type(root) != JSON_OBJECT) {
        err_plain(m, "-32600", "Invalid Request");
    } else if (idv != NULL && it != JSON_STRING && it != JSON_NUMBER && it != JSON_NULL) {
        err_plain(m, "-32600", "Invalid Request: id must be a string, a number or null");
    } else {
        m->id = idv;
        if (!json_get_str(json_obj_get(root, cs("jsonrpc")), &v) || !core_str_eq(v, cs("2.0"))) {
            err_plain(m, "-32600", "Invalid Request: jsonrpc must be \"2.0\"");
        } else if (!json_get_str(json_obj_get(root, cs("method")), &v)) {
            err_plain(m, "-32600", "Invalid Request: method must be a string");
        } else if (idv == NULL) {
            return false;
        } else {
            route(m, root, v);
        }
    }
    return true;
}

static bool blank_line(core_str_t line) {
    for (size_t i = 0; i < line.len; i++) {
        char c = line.ptr[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            return false;
        }
    }
    return true;
}

/* All the arena that is left becomes the reply buffer (cap 0 when there is nothing left). */
static void reply_open(mctx_t *m) {
    core_arena_t *a = &m->s->arena;
    m->left = core_arena_capacity(a) - core_arena_used(a);
    m->cap = m->left;
    m->mem = m->cap > 0 ? core_arena_alloc(a, m->cap, 1) : NULL;
    m->cap = m->mem != NULL ? m->cap : 0;
    (void)core_buf_init(&m->rb, (char *)m->mem, m->cap);
}

/* Scrubs the reply and appends it in one put; a reply that is too long or does not fit the arena becomes the
 * -32603 reply, and when even that does not fit nothing is appended. */
static app_mcp_result_t deliver(mctx_t *m, core_buf_t *out) {
    const app_deps_t *d = &m->s->deps;
    core_str_t fin = {NULL, 0};
    bool ok = !core_buf_overflow(&m->rb) && app_finalise(d, core_buf_str(&m->rb), false, &fin);
    if (!ok) {
        nomem(m);
        ok = !core_buf_overflow(&m->rb) && app_finalise(d, core_buf_str(&m->rb), false, &fin);
    }
    if (!ok) {
        core_buf_put(out, NULL, 1);
        return APP_MCP_E_OVERFLOW;
    }
    core_buf_put_str(out, fin);
    return core_buf_overflow(out) ? APP_MCP_E_OVERFLOW : APP_MCP_REPLY;
}

bool app_mcp_init(app_mcp_t *s, const app_deps_t *deps, void *parse_mem, size_t parse_cap) {
    if (s == NULL || deps == NULL || parse_mem == NULL || parse_cap < APP_MCP_ARENA_MIN || !app_scratch_ok(deps)) {
        return false;
    }
    s->deps = *deps;
    return core_arena_init(&s->arena, parse_mem, parse_cap);
}

static void compose(mctx_t *m, size_t line_len, json_err_t je, const json_value_t *root, bool *replied) {
    *replied = true;
    if (line_len > APP_MCP_LINE_MAX) {
        err_plain(m, "-32600", "Invalid Request: line longer than 1048576 bytes");
    } else if (je == JSON_E_NOMEM) {
        err_plain(m, "-32603", MSG_INTERNAL_NOMEM);
    } else if (je != JSON_OK) {
        err_plain(m, "-32700", "Parse error");
    } else {
        *replied = dispatch(m, root);
    }
}

app_mcp_result_t app_mcp_handle(app_mcp_t *s, core_str_t line, core_buf_t *out) {
    mctx_t m = {0};
    json_limits_t lim = json_limits_default();
    const json_value_t *root = NULL;
    json_err_t je = JSON_OK;
    bool replied = true;
    if (s == NULL || out == NULL || (line.ptr == NULL && line.len > 0)) {
        return APP_MCP_E_ARG;
    }
    m.s = s;
    if (!core_arena_reset_to(&s->arena, 0)) {
        return APP_MCP_E_ARG;
    }
    if (line.len <= APP_MCP_LINE_MAX) {
        if (blank_line(line)) {
            return APP_MCP_NO_REPLY;
        }
        lim.max_input = APP_MCP_LINE_MAX;
        je = json_parse(&s->arena, line, &lim, &root, NULL);
    }
    reply_open(&m);
    compose(&m, line.len, je, root, &replied);
    return replied ? deliver(&m, out) : APP_MCP_NO_REPLY;
}
