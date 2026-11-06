#include <string.h>

#include "json/src/internal.h"

static const char g_empty[1] = {0};

json_limits_t json_limits_default(void) {
    json_limits_t l = {JSON_MAX_DEPTH, JSON_MAX_INPUT, (size_t)1 << 20, 1048576};
    return l;
}

static json_err_t fail(json_parser_t *p, json_err_t e, size_t off) {
    p->err = off;
    return e;
}

static json_err_t nomem(json_parser_t *p) { return fail(p, JSON_E_NOMEM, p->i); }

static void skip_ws(json_parser_t *p) {
    while (p->i < p->n && (p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n' || p->s[p->i] == '\r')) {
        p->i++;
    }
}

/* Decodes the string at p->i into the arena: a counting pass first, so errors win over NOMEM and the
 * allocation is exact. */
static json_err_t read_str(json_parser_t *p, core_str_t *out) {
    size_t start = p->i, len = 0, len2 = 0, end;
    char *buf = NULL;
    json_err_t e = json_str_decode(p, NULL, &len);
    if (e != JSON_OK) {
        return e;
    }
    end = p->i;
    if (len == 0) {
        out->ptr = g_empty;
        out->len = 0;
        return JSON_OK;
    }
    buf = core_arena_alloc(p->a, len, 1);
    if (buf == NULL) {
        return nomem(p);
    }
    p->i = start;
    e = json_str_decode(p, buf, &len2);
    p->i = end;
    out->ptr = buf;
    out->len = len;
    return e;
}

static json_err_t literal(json_parser_t *p, const char *word, size_t len) {
    size_t k;
    for (k = 0; k < len; k++) {
        if (p->i + k >= p->n) {
            return fail(p, JSON_E_SYNTAX, p->n);
        }
        if (p->s[p->i + k] != (unsigned char)word[k]) {
            return fail(p, JSON_E_SYNTAX, p->i + k);
        }
    }
    p->i += len;
    return JSON_OK;
}

static json_err_t read_number(json_parser_t *p, core_str_t *out) {
    size_t start = p->i;
    char *buf;
    json_err_t e = json_num_scan(p);
    if (e != JSON_OK) {
        return e;
    }
    buf = core_arena_alloc(p->a, p->i - start, 1);
    if (buf == NULL) {
        return nomem(p);
    }
    memcpy(buf, p->s + start, p->i - start);
    out->ptr = buf;
    out->len = p->i - start;
    return JSON_OK;
}

static json_err_t scalar(json_parser_t *p, json_value_t *v) {
    switch (v->type) {
    case JSON_STRING: return read_str(p, &v->u.s);
    case JSON_NUMBER: return read_number(p, &v->u.s);
    case JSON_NULL: return literal(p, "null", 4);
    default:
        v->b = p->s[p->i] == 't';
        return v->b ? literal(p, "true", 4) : literal(p, "false", 5);
    }
}

static json_type_t start_type(unsigned char c) {
    switch (c) {
    case '{': return JSON_OBJECT;
    case '[': return JSON_ARRAY;
    case '"': return JSON_STRING;
    case 't':
    case 'f': return JSON_BOOL;
    case 'n': return JSON_NULL;
    case '-': return JSON_NUMBER;
    default: return (c >= '0' && c <= '9') ? JSON_NUMBER : JSON_NONE;
    }
}

/* Allocates a node of type ty under the current container, consuming the pending key. NULL: out of memory. */
static json_value_t *new_node(json_parser_t *p, json_type_t ty) {
    json_value_t *v = core_arena_alloc(p->a, sizeof *v, _Alignof(json_value_t));
    if (v == NULL) {
        return NULL;
    }
    memset(v, 0, sizeof *v);
    v->type = ty;
    v->parent = p->cur;
    if (p->has_key) {
        v->key = p->key;
        p->has_key = false;
    }
    if (p->cur == NULL) {
        p->root = v;
    } else if (p->cur->u.c.first == NULL) {
        p->cur->u.c.first = v;
        p->cur->u.c.last = v;
        p->cur->u.c.n = 1;
    } else {
        p->cur->u.c.last->next = v;
        p->cur->u.c.last = v;
        p->cur->u.c.n++;
    }
    return v;
}

static json_err_t value_start(json_parser_t *p) {
    json_type_t ty;
    json_value_t *v;
    bool box;
    skip_ws(p);
    if (p->i >= p->n) {
        return fail(p, JSON_E_SYNTAX, p->n);
    }
    ty = start_type(p->s[p->i]);
    box = ty == JSON_ARRAY || ty == JSON_OBJECT;
    if (ty == JSON_NONE) {
        return fail(p, JSON_E_SYNTAX, p->i);
    }
    if (box && p->depth >= p->lim.max_depth) {
        return fail(p, JSON_E_DEPTH, p->i);
    }
    if (p->elems >= p->lim.max_elements) {
        return fail(p, JSON_E_LIMIT, p->i);
    }
    v = new_node(p, ty);
    if (v == NULL) {
        return nomem(p);
    }
    p->elems++;
    p->just_opened = box;
    if (!box) {
        return scalar(p, v);
    }
    p->cur = v;
    p->depth++;
    p->i++;
    return JSON_OK;
}

static bool key_taken(const json_parser_t *p, core_str_t key) {
    const json_value_t *c;
    for (c = p->cur->u.c.first; c != NULL; c = c->next) {
        if (core_str_eq(c->key, key)) {
            return true;
        }
    }
    return false;
}

static json_err_t member(json_parser_t *p) {
    core_str_t key;
    size_t start;
    json_err_t e;
    skip_ws(p);
    if (p->i >= p->n || p->s[p->i] != '"') {
        return fail(p, JSON_E_SYNTAX, p->i);
    }
    start = p->i;
    e = read_str(p, &key);
    if (e != JSON_OK) {
        return e;
    }
    if (key_taken(p, key)) {
        return fail(p, JSON_E_SYNTAX, start);
    }
    skip_ws(p);
    if (p->i >= p->n || p->s[p->i] != ':') {
        return fail(p, JSON_E_SYNTAX, p->i);
    }
    p->i++;
    p->key = key;
    p->has_key = true;
    return value_start(p);
}

static json_err_t close_container(json_parser_t *p) {
    json_value_t *c = p->cur;
    json_value_t *const *items = NULL;
    json_value_t **w;
    json_value_t *ch;
    if (c->u.c.n > 0) {
        w = core_arena_alloc(p->a, c->u.c.n * sizeof *w, _Alignof(json_value_t *));
        if (w == NULL) {
            return nomem(p);
        }
        items = w;
        for (ch = c->u.c.first; ch != NULL; ch = ch->next) {
            *w++ = ch;
        }
    }
    c->u.c.items = items;
    p->cur = c->parent;
    p->depth--;
    p->i++;
    p->just_opened = false;
    return JSON_OK;
}

static json_err_t next_step(json_parser_t *p) {
    unsigned char closer = p->cur->type == JSON_ARRAY ? ']' : '}';
    bool first = p->just_opened;
    skip_ws(p);
    if (p->i >= p->n) {
        return fail(p, JSON_E_SYNTAX, p->n);
    }
    p->just_opened = false;
    if (p->s[p->i] == closer) {
        return close_container(p);
    }
    if (!first) {
        if (p->s[p->i] != ',') {
            return fail(p, JSON_E_SYNTAX, p->i);
        }
        p->i++;
    }
    return p->cur->type == JSON_ARRAY ? value_start(p) : member(p);
}

static json_err_t run(core_arena_t *a, core_str_t in, const json_limits_t *lim, const json_value_t **root,
                      size_t *off) {
    json_parser_t p;
    json_err_t e;
    memset(&p, 0, sizeof p);
    p.s = (const unsigned char *)in.ptr;
    p.n = in.len;
    p.a = a;
    p.lim = *lim;
    e = value_start(&p);
    while (e == JSON_OK && p.cur != NULL) {
        e = next_step(&p);
    }
    if (e == JSON_OK) {
        skip_ws(&p);
        e = p.i < p.n ? fail(&p, JSON_E_SYNTAX, p.i) : JSON_OK;
    }
    *off = e == JSON_OK ? 0 : p.err;
    *root = e == JSON_OK ? p.root : NULL;
    return e;
}

json_err_t json_parse(core_arena_t *a, core_str_t in, const json_limits_t *lim, const json_value_t **root,
                      size_t *err_offset) {
    json_limits_t l = lim != NULL ? *lim : json_limits_default();
    size_t off = 0, mark;
    json_err_t e;
    if (root == NULL) {
        if (err_offset != NULL) {
            *err_offset = 0;
        }
        return JSON_E_ARG;
    }
    *root = NULL;
    if (a == NULL || (in.ptr == NULL && in.len > 0)) {
        e = JSON_E_ARG;
    } else if (l.max_depth > JSON_MAX_DEPTH || l.max_input > JSON_MAX_INPUT) {
        e = JSON_E_LIMIT;
    } else if (in.len > l.max_input) {
        e = JSON_E_LIMIT;
        off = l.max_input;
    } else {
        mark = core_arena_mark(a);
        e = run(a, in, &l, root, &off);
        if (e != JSON_OK && !core_arena_reset_to(a, mark)) {
            e = JSON_E_ARG;
        }
    }
    if (err_offset != NULL) {
        *err_offset = off;
    }
    return e;
}
