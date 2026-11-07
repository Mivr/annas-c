#include "app/src/internal.h"

static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\v' || c == '\f'; }

static core_str_t trim_ws(core_str_t s) {
    while (s.len > 0) {
        if (is_ws(s.ptr[0])) {
            s.ptr++;
        } else if (!is_ws(s.ptr[s.len - 1u])) {
            break;
        }
        s.len--;
    }
    return s;
}

static bool name_ok(core_str_t n) {
    if (n.len == 0) {
        return false;
    }
    for (size_t i = 0; i < n.len; i++) {
        char c = n.ptr[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static core_str_t unquote(core_str_t v) {
    if (v.len >= 2u && v.ptr[0] == '"' && v.ptr[v.len - 1u] == '"') {
        v.ptr++;
        v.len -= 2u;
    }
    return v;
}

/* One line (without its '\n') -> an entry; false when the line is skipped. */
static bool parse_line(core_str_t line, app_dotenv_entry_t *e) {
    core_str_t name;
    core_str_t value;
    if (line.len > 0 && line.ptr[line.len - 1u] == '\r') {
        line.len--;
    }
    line = trim_ws(line);
    /* No test for an empty line or a '#' comment: neither has a valid name before an '='. */
    if (!core_str_split_once(line, core_str_from_cstr("="), &name, &value)) {
        return false;
    }
    name = trim_ws(name);
    if (!name_ok(name)) {
        return false;
    }
    e->name = name;
    e->value = unquote(trim_ws(value));
    return true;
}

bool app_dotenv_parse(core_str_t file, app_dotenv_t *out) {
    core_str_t rest = file;
    if (out == NULL) {
        return false;
    }
    out->count = 0;
    if (file.ptr == NULL && file.len > 0) {
        return false;
    }
    while (rest.len > 0) {
        core_str_t line;
        app_dotenv_entry_t e;
        (void)core_str_split_once(rest, core_str_from_cstr("\n"), &line, &rest); /* not found: line = rest, rest empty */
        if (!parse_line(line, &e)) {
            continue;
        }
        if (out->count >= APP_DOTENV_MAX_ENTRIES) {
            out->count = 0;
            return false;
        }
        out->entries[out->count] = e;
        out->count++;
    }
    return true;
}

static bool layer_get(void *ctx, core_str_t name, core_str_t *value) {
    const app_env_layer_t *l = ctx;
    core_str_t none = {NULL, 0};
    core_str_t v = none;
    bool found = false;
    if (l != NULL && l->process.ops != NULL && name.len > 0 && value != NULL) {
        found = l->process.ops->get(l->process.ctx, name, &v);
        for (size_t i = l->file == NULL ? 0 : l->file->count; !found && i > 0; i--) {
            if (core_str_eq(l->file->entries[i - 1u].name, name)) {
                v = l->file->entries[i - 1u].value;
                found = true;
            }
        }
    }
    if (value != NULL) {
        *value = found ? v : none;
    }
    return found;
}

static const ports_env_ops_t k_layer_ops = {layer_get};

bool app_env_layer_init(app_env_layer_t *l, const app_dotenv_t *file, ports_env_t process) {
    if (l == NULL) {
        return false;
    }
    l->file = NULL;
    l->process.ops = NULL;
    l->process.ctx = NULL;
    if (process.ops == NULL || process.ops->get == NULL) {
        return false;
    }
    l->file = file;
    l->process = process;
    return true;
}

ports_env_t app_env_layer_port(app_env_layer_t *l) {
    ports_env_t e;
    e.ops = &k_layer_ops;
    e.ctx = l;
    return e;
}
