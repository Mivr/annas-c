#include "app/tests/kit.h"

#include <string.h>

static ak_t g_ak;

ak_t *ak_begin(void) {
    kit_t *k = kit_begin();
    ak_t *a = &g_ak;
    a->k = k;
    a->n_ent = 0;
    a->dotenv_len = 0;
    a->has_dotenv = false;
    a->deps_ready = false;
    a->mcp_ready = false;
    ak_env(a, "ANNAS_SECRET_KEY", KIT_KEY);
    ak_env(a, "ANNAS_DOWNLOAD_PATH", "/dl");
    (void)core_buf_init(&a->out, a->out_store, sizeof a->out_store);
    (void)core_buf_init(&a->err, a->err_store, sizeof a->err_store);
    return a;
}

void ak_env(ak_t *a, const char *name, const char *value) {
    size_t i;
    for (i = 0; i < a->n_ent; i++) {
        if (ak_eq(a->ent[i].name, name)) {
            break;
        }
    }
    if (value == NULL) {
        if (i < a->n_ent) {
            a->ent[i] = a->ent[a->n_ent - 1u];
            a->n_ent--;
        }
        return;
    }
    if (i == a->n_ent) {
        if (a->n_ent >= AK_ENV_MAX) {
            return;
        }
        a->n_ent++;
    }
    a->ent[i].name = core_str_from_cstr(name);
    a->ent[i].value = core_str_from_cstr(value);
}

void ak_dotenv(ak_t *a, const char *text) {
    size_t n = strlen(text);
    if (n < AK_DOTENV_MAX) {
        memcpy(a->dotenv, text, n);
        a->dotenv_len = n;
        a->has_dotenv = true;
    }
}

const app_deps_t *ak_deps(ak_t *a) {
    if (a->deps_ready) {
        return &a->deps;
    }
    a->deps_ready = true;
    (void)kit_prepare(a->k);
    (void)ports_fake_env_init(&a->env, a->ent, a->n_ent);
    const app_dotenv_t *file = NULL;
    if (a->has_dotenv) {
        core_str_t text = {a->dotenv, a->dotenv_len};
        if (app_dotenv_parse(text, &a->denv)) {
            file = &a->denv;
        }
    }
    (void)app_env_layer_init(&a->layer, file, ports_fake_env_port(&a->env));
    a->deps.stream = ports_fake_stream_port(&a->k->stream);
    a->deps.fs = ports_fake_fs_port(&a->k->fs);
    a->deps.clock = ports_fake_clock_port(&a->k->clock);
    a->deps.env = app_env_layer_port(&a->layer);
    a->deps.workspace = a->k->work;
    a->deps.workspace_cap = sizeof a->k->work;
    return &a->deps;
}

void ak_clear(ak_t *a) {
    (void)core_buf_init(&a->out, a->out_store, sizeof a->out_store);
    (void)core_buf_init(&a->err, a->err_store, sizeof a->err_store);
}

int ak_cli(ak_t *a, int argc, const char *const *argv) {
    app_cmd_t cmd;
    static app_usage_error_t ue;
    if (!app_cli_parse(argc, argv, &cmd, &ue)) {
        return -1000;
    }
    return app_cli_run(&cmd, ak_deps(a), &a->out, &a->err);
}

const char *ak_out(ak_t *a) { return core_buf_cstr(&a->out); }
const char *ak_err(ak_t *a) { return core_buf_cstr(&a->err); }

app_mcp_result_t ak_mcp_n(ak_t *a, const char *bytes, size_t len) {
    const app_deps_t *d = ak_deps(a);
    if (!a->mcp_ready) {
        a->mcp_ready = app_mcp_init(&a->mcp, d, a->parse_mem, sizeof a->parse_mem);
    }
    ak_clear(a);
    core_str_t line = {bytes, len};
    return app_mcp_handle(&a->mcp, line, &a->out);
}

app_mcp_result_t ak_mcp(ak_t *a, const char *line) { return ak_mcp_n(a, line, strlen(line)); }

void ak_check_stream(ctest_t *t, ak_t *a) {
    kit_t *k = a->k;
    ports_fake_mismatch_t mm;
    CHECK(t, !ports_fake_stream_mismatch(&k->stream, &mm));
    CHECK(t, !ports_fake_stream_is_open(&k->stream));
    CHECK_EQ_U64(t, ports_fake_stream_closes(&k->stream), k->ok_connects);
    if (!k->allow_unused) {
        CHECK(t, ports_fake_stream_done(&k->stream));
    }
}

void ak_check_no_io(ctest_t *t, ak_t *a) {
    kit_t *k = a->k;
    ports_fake_mismatch_t mm;
    (void)ak_deps(a);
    CHECK(t, !ports_fake_stream_mismatch(&k->stream, &mm));
    CHECK_EQ_U64(t, ports_fake_stream_pos(&k->stream), 0);
    CHECK_EQ_U64(t, ports_fake_stream_closes(&k->stream), 0);
    CHECK(t, !ports_fake_stream_is_open(&k->stream));
    kit_check_fs_untouched(t, k);
}

bool ak_eq(core_str_t s, const char *want) { return core_str_eq(s, core_str_from_cstr(want)); }

void ak_check_no_key(ctest_t *t, ak_t *a) {
    CHECK(t, !kit_contains(ak_out(a), a->k->key));
    CHECK(t, !kit_contains(ak_out(a), a->k->key_pct));
    CHECK(t, !kit_contains(ak_err(a), a->k->key));
    CHECK(t, !kit_contains(ak_err(a), a->k->key_pct));
}

void ak_check_reply(ctest_t *t, ak_t *a, app_mcp_result_t got, const char *want) {
    CHECK_EQ_I64(t, got, APP_MCP_REPLY);
    CHECK_EQ_STR(t, ak_out(a), want);
    CHECK(t, strchr(ak_out(a), '\n') == NULL);
    CHECK_EQ_U64(t, core_buf_len(&a->out), strlen(ak_out(a)));
    CHECK(t, !core_buf_overflow(&a->out));
    ak_check_no_key(t, a);
}

void ak_check_cli(ctest_t *t, ak_t *a, int got, int want, const char *out, const char *err) {
    CHECK_EQ_I64(t, got, want);
    CHECK_EQ_STR(t, ak_out(a), out);
    CHECK_EQ_STR(t, ak_err(a), err);
    CHECK(t, !core_buf_overflow(&a->out));
    CHECK(t, !core_buf_overflow(&a->err));
    ak_check_no_key(t, a);
}
