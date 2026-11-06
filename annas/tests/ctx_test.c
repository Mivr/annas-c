/* Tests for annas_ctx_init and the argument/config guards of the two operations. */
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"
#include "ports/fakes.h"

static _Alignas(64) unsigned char g_ws[ANNAS_WORKSPACE_MIN + 64];
static annas_ctx_t g_ctx;
static annas_config_t g_cfg;
static ports_fake_stream_t g_stream;
static ports_fake_fs_t g_fs;
static ports_fake_clock_t g_clock;
static unsigned char g_store[64];
static annas_fast_result_t g_fast;
static annas_download_result_t g_dl;
static annas_error_t g_err;

static bool init_with(const annas_config_t *cfg, ports_stream_t s, ports_fs_t f, ports_clock_t c, void *ws,
                      size_t cap) {
    return annas_ctx_init(&g_ctx, cfg, s, f, c, ws, cap);
}

static void setup(void) {
    memset(&g_cfg, 0, sizeof g_cfg);
    memcpy(g_cfg.base, "annas-archive.gl", 17);
    g_cfg.base_len = 16;
    g_cfg.timeout_ms = 1000;
    g_cfg.key = core_str_from_cstr("fake-key");
    g_cfg.download_dir = core_str_from_cstr("/dl");
    (void)ports_fake_stream_init(&g_stream, NULL, 0, &g_clock, 0);
    (void)ports_fake_fs_init(&g_fs, g_store, sizeof g_store);
    (void)ports_fake_clock_init(&g_clock, 0);
}

static void t_init_ok_at_minimum(ctest_t *t) {
    setup();
    CHECK(t, init_with(&g_cfg, ports_fake_stream_port(&g_stream), ports_fake_fs_port(&g_fs),
                       ports_fake_clock_port(&g_clock), g_ws, ANNAS_WORKSPACE_MIN));
    CHECK(t, init_with(&g_cfg, ports_fake_stream_port(&g_stream), ports_fake_fs_port(&g_fs),
                       ports_fake_clock_port(&g_clock), g_ws, ANNAS_WORKSPACE_MIN + 64));
}

static void t_init_rejects_small_workspace(ctest_t *t) {
    setup();
    CHECK(t, !init_with(&g_cfg, ports_fake_stream_port(&g_stream), ports_fake_fs_port(&g_fs),
                        ports_fake_clock_port(&g_clock), g_ws, ANNAS_WORKSPACE_MIN - 1));
    CHECK(t, !init_with(&g_cfg, ports_fake_stream_port(&g_stream), ports_fake_fs_port(&g_fs),
                        ports_fake_clock_port(&g_clock), g_ws, 0));
    CHECK(t, init_with(&g_cfg, ports_fake_stream_port(&g_stream), ports_fake_fs_port(&g_fs),
                       ports_fake_clock_port(&g_clock), g_ws, ANNAS_WORKSPACE_MIN)); /* control */
}

static void t_init_alignment(ctest_t *t) {
    setup();
    ports_stream_t s = ports_fake_stream_port(&g_stream);
    ports_fs_t f = ports_fake_fs_port(&g_fs);
    ports_clock_t c = ports_fake_clock_port(&g_clock);
    CHECK(t, !init_with(&g_cfg, s, f, c, g_ws + 8, ANNAS_WORKSPACE_MIN));
    CHECK(t, !init_with(&g_cfg, s, f, c, g_ws + 1, ANNAS_WORKSPACE_MIN));
    CHECK(t, init_with(&g_cfg, s, f, c, g_ws + 16, ANNAS_WORKSPACE_MIN));
}

static void t_init_rejects_null(ctest_t *t) {
    setup();
    ports_stream_t s = ports_fake_stream_port(&g_stream);
    ports_fs_t f = ports_fake_fs_port(&g_fs);
    ports_clock_t c = ports_fake_clock_port(&g_clock);
    ports_stream_t s0 = {NULL, NULL};
    ports_fs_t f0 = {NULL, NULL};
    ports_clock_t c0 = {NULL, NULL};
    CHECK(t, !annas_ctx_init(NULL, &g_cfg, s, f, c, g_ws, ANNAS_WORKSPACE_MIN));
    CHECK(t, !init_with(NULL, s, f, c, g_ws, ANNAS_WORKSPACE_MIN));
    CHECK(t, !init_with(&g_cfg, s, f, c, NULL, ANNAS_WORKSPACE_MIN));
    CHECK(t, !init_with(&g_cfg, s0, f, c, g_ws, ANNAS_WORKSPACE_MIN));
    CHECK(t, !init_with(&g_cfg, s, f0, c, g_ws, ANNAS_WORKSPACE_MIN));
    CHECK(t, !init_with(&g_cfg, s, f, c0, g_ws, ANNAS_WORKSPACE_MIN));
    CHECK(t, init_with(&g_cfg, s, f, c, g_ws, ANNAS_WORKSPACE_MIN)); /* control */
}

static void expect_missing(ctest_t *t, annas_code_t got) {
    CHECK_EQ_I64(t, got, ANNAS_E_CONFIG);
    CHECK_EQ_I64(t, g_err.code, ANNAS_E_CONFIG);
    CHECK_EQ_STR(t, g_err.message, "ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set");
    ports_fake_mismatch_t mm;
    CHECK(t, !ports_fake_stream_mismatch(&g_stream, &mm));
    CHECK_EQ_U64(t, ports_fake_stream_pos(&g_stream), 0);
    CHECK_EQ_U64(t, ports_fake_fs_temp_count(&g_fs), 0);
}

static void t_missing_key_or_dir_is_config_error_before_any_port_call(ctest_t *t) {
    setup();
    annas_request_t req = kit_req("0123456789abcdef0123456789abcdef");
    CHECK(t, init_with(&g_cfg, ports_fake_stream_port(&g_stream), ports_fake_fs_port(&g_fs),
                       ports_fake_clock_port(&g_clock), g_ws, ANNAS_WORKSPACE_MIN));
    g_cfg.key = core_str_from_cstr("");
    expect_missing(t, annas_fast_download_url(&g_ctx, &req, &g_fast, &g_err));
    expect_missing(t, annas_download(&g_ctx, &req, &g_dl, &g_err));
    g_cfg.key = core_str_from_cstr("fake-key");
    g_cfg.download_dir = core_str_from_cstr("");
    expect_missing(t, annas_download(&g_ctx, &req, &g_dl, &g_err));
    /* the fast call does not need the directory: it goes on and hits the (empty) script */
    CHECK(t, annas_fast_download_url(&g_ctx, &req, &g_fast, &g_err) != ANNAS_E_CONFIG);
}

static void t_config_is_checked_before_arguments(ctest_t *t) {
    setup();
    annas_request_t req = kit_req("not-an-md5");
    CHECK(t, init_with(&g_cfg, ports_fake_stream_port(&g_stream), ports_fake_fs_port(&g_fs),
                       ports_fake_clock_port(&g_clock), g_ws, ANNAS_WORKSPACE_MIN));
    g_cfg.key = core_str_from_cstr("");
    expect_missing(t, annas_fast_download_url(&g_ctx, &req, &g_fast, &g_err));
    g_cfg.key = core_str_from_cstr("fake-key");
    CHECK_EQ_I64(t, annas_fast_download_url(&g_ctx, &req, &g_fast, &g_err), ANNAS_E_ARG);
    CHECK_EQ_STR(t, g_err.message, "md5 must be 32 hexadecimal characters");
}

static void t_null_arguments(ctest_t *t) {
    setup();
    annas_request_t req = kit_req("0123456789abcdef0123456789abcdef");
    CHECK(t, init_with(&g_cfg, ports_fake_stream_port(&g_stream), ports_fake_fs_port(&g_fs),
                       ports_fake_clock_port(&g_clock), g_ws, ANNAS_WORKSPACE_MIN));
    memset(&g_err, 0xA5, sizeof g_err);
    CHECK_EQ_I64(t, annas_fast_download_url(NULL, &req, &g_fast, &g_err), ANNAS_E_ARG);
    CHECK_EQ_STR(t, g_err.message, "invalid argument");
    CHECK_EQ_I64(t, g_err.code, ANNAS_E_ARG);
    CHECK_EQ_I64(t, annas_fast_download_url(&g_ctx, NULL, &g_fast, &g_err), ANNAS_E_ARG);
    CHECK_EQ_I64(t, annas_fast_download_url(&g_ctx, &req, NULL, &g_err), ANNAS_E_ARG);
    CHECK_EQ_I64(t, annas_fast_download_url(&g_ctx, &req, &g_fast, NULL), ANNAS_E_ARG);
    CHECK_EQ_I64(t, annas_download(NULL, &req, &g_dl, &g_err), ANNAS_E_ARG);
    CHECK_EQ_I64(t, annas_download(&g_ctx, NULL, &g_dl, &g_err), ANNAS_E_ARG);
    CHECK_EQ_I64(t, annas_download(&g_ctx, &req, NULL, &g_err), ANNAS_E_ARG);
    CHECK_EQ_I64(t, annas_download(&g_ctx, &req, &g_dl, NULL), ANNAS_E_ARG);
    CHECK_EQ_U64(t, ports_fake_stream_pos(&g_stream), 0);
    req.md5.ptr = NULL; /* ptr NULL with len > 0 */
    CHECK_EQ_I64(t, annas_fast_download_url(&g_ctx, &req, &g_fast, &g_err), ANNAS_E_ARG);
    CHECK_EQ_STR(t, g_err.message, "invalid argument");
}

CTEST_SUITE("ctx",
    {"init_ok_at_minimum", t_init_ok_at_minimum},
    {"init_rejects_small_workspace", t_init_rejects_small_workspace},
    {"init_alignment", t_init_alignment},
    {"init_rejects_null", t_init_rejects_null},
    {"missing_key_or_dir_is_config_error_before_any_port_call", t_missing_key_or_dir_is_config_error_before_any_port_call},
    {"config_is_checked_before_arguments", t_config_is_checked_before_arguments},
    {"null_arguments", t_null_arguments})
