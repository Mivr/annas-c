/* Tests for annas_config_load (spec section 1) through ports_env_t fakes. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "ctest/ctest.h"
#include "ports/fakes.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static annas_config_t g_cfg, g_sent;
static annas_error_t g_err;
static ports_fake_env_t g_env;
static ports_fake_env_entry_t g_ent[3];
static char g_long[400];

static annas_code_t load(const char *base, const char *key, const char *dir, bool need, int64_t to) {
    size_t n = 0;
    if (base != NULL) {
        g_ent[n++] = (ports_fake_env_entry_t){core_str_from_cstr("ANNAS_BASE_URL"), core_str_from_cstr(base)};
    }
    if (key != NULL) {
        g_ent[n++] = (ports_fake_env_entry_t){core_str_from_cstr("ANNAS_SECRET_KEY"), core_str_from_cstr(key)};
    }
    if (dir != NULL) {
        g_ent[n++] = (ports_fake_env_entry_t){core_str_from_cstr("ANNAS_DOWNLOAD_PATH"), core_str_from_cstr(dir)};
    }
    if (!ports_fake_env_init(&g_env, g_ent, n)) {
        return ANNAS_OK;
    }
    memset(&g_cfg, 0xA5, sizeof g_cfg);
    g_sent = g_cfg;
    memset(&g_err, 0xA5, sizeof g_err);
    return annas_config_load(ports_fake_env_port(&g_env), need, to, &g_cfg, &g_err);
}

static void expect_fail(ctest_t *t, annas_code_t got, annas_code_t want, const char *msg) {
    CHECK_EQ_I64(t, got, want);
    CHECK_EQ_I64(t, g_err.code, want);
    CHECK_EQ_I64(t, g_err.http_status, 0);
    CHECK_EQ_STR(t, g_err.message, msg);
    CHECK_EQ_U64(t, g_err.message_len, strlen(msg));
    CHECK(t, memcmp(&g_cfg, &g_sent, sizeof g_cfg) == 0);
}

static void expect_ok(ctest_t *t, annas_code_t got, const char *base, uint64_t ms) {
    CHECK_EQ_I64(t, got, ANNAS_OK);
    CHECK_EQ_I64(t, g_err.code, ANNAS_OK);
    CHECK_EQ_I64(t, g_err.http_status, 0);
    CHECK_EQ_U64(t, g_err.message_len, 0);
    CHECK_EQ_STR(t, g_err.message, "");
    CHECK_EQ_STR(t, g_cfg.base, base);
    CHECK_EQ_U64(t, g_cfg.base_len, strlen(base));
    CHECK_EQ_U64(t, g_cfg.timeout_ms, ms);
}

#define MISSING "ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set"

static void t_defaults(ctest_t *t) {
    expect_ok(t, load(NULL, "fake-key", "/dl", true, 0), "annas-archive.gl", 3600000u);
    CHECK_EQ_U64(t, g_cfg.key.len, 8);
    CHECK_EQ_MEM(t, g_cfg.key.ptr, g_cfg.key.len, "fake-key", 8);
    CHECK_EQ_U64(t, g_cfg.download_dir.len, 3);
    CHECK_EQ_MEM(t, g_cfg.download_dir.ptr, g_cfg.download_dir.len, "/dl", 3);
}

static void t_empty_base_is_default(ctest_t *t) {
    expect_ok(t, load("", "fake-key", "/dl", true, 0), "annas-archive.gl", 3600000u);
}

static void t_base_normalisation(ctest_t *t) {
    static const char *const in[] = {"example.org", "https://example.org", "http://example.org", "https://example.org/",
                                     "http://example.org/", "example.org/", "a-b.c1.org", "1.2.3.4", "xn--a.org"};
    static const char *const out[] = {"example.org", "example.org", "example.org", "example.org", "example.org",
                                      "example.org", "a-b.c1.org", "1.2.3.4", "xn--a.org"};
    for (size_t i = 0; i < COUNT(in); i++) {
        expect_ok(t, load(in[i], "fake-key", "/dl", true, 0), out[i], 3600000u);
    }
}

static void t_base_invalid(ctest_t *t) {
    static const char *const bad[] = {"Example.org", "x.org//", "https://", "/", "https:///", "https://http://x.org",
                                      " x.org", "x.org ", "x.org:8080", "a..b.org", "-x.org", "x.org-", "1.2.3",
                                      "ftp://x.org", "x_y.org", "HTTPS://x.org", "x.org/path", ".org"};
    for (size_t i = 0; i < COUNT(bad); i++) {
        char want[160];
        snprintf(want, sizeof want, "ANNAS_BASE_URL is not a valid domain, got: %s", bad[i]);
        expect_fail(t, load(bad[i], "fake-key", "/dl", true, 0), ANNAS_E_CONFIG, want);
    }
}

static void t_base_checked_even_without_download(ctest_t *t) {
    expect_fail(t, load("Bad.Org", NULL, NULL, false, 0), ANNAS_E_CONFIG,
                "ANNAS_BASE_URL is not a valid domain, got: Bad.Org");
    expect_ok(t, load("good.org", NULL, NULL, false, 0), "good.org", 3600000u);
}

static void t_missing(ctest_t *t) {
    expect_fail(t, load(NULL, NULL, "/dl", true, 0), ANNAS_E_CONFIG, MISSING);
    expect_fail(t, load(NULL, "", "/dl", true, 0), ANNAS_E_CONFIG, MISSING);
    expect_fail(t, load(NULL, "fake-key", NULL, true, 0), ANNAS_E_CONFIG, MISSING);
    expect_fail(t, load(NULL, "fake-key", "", true, 0), ANNAS_E_CONFIG, MISSING);
    expect_fail(t, load(NULL, NULL, NULL, true, 0), ANNAS_E_CONFIG, MISSING);
    expect_fail(t, load(NULL, "", "", true, 0), ANNAS_E_CONFIG, MISSING);
    expect_ok(t, load(NULL, "k", "/", true, 0), "annas-archive.gl", 3600000u); /* positive control */
}

static void t_not_required_without_download(ctest_t *t) {
    expect_ok(t, load(NULL, NULL, NULL, false, 0), "annas-archive.gl", 3600000u);
    CHECK_EQ_U64(t, g_cfg.key.len, 0);
    CHECK_EQ_U64(t, g_cfg.download_dir.len, 0);
    expect_ok(t, load(NULL, "fake-key", "relative", false, 0), "annas-archive.gl", 3600000u);
    CHECK_EQ_U64(t, g_cfg.key.len, 8);
    CHECK_EQ_U64(t, g_cfg.download_dir.len, 8);
}

static void t_names_are_case_sensitive(ctest_t *t) {
    ports_fake_env_entry_t e[2] = {
        {core_str_from_cstr("annas_secret_key"), core_str_from_cstr("fake-key")},
        {core_str_from_cstr("annas_download_path"), core_str_from_cstr("/dl")}};
    CHECK(t, ports_fake_env_init(&g_env, e, 2));
    memset(&g_cfg, 0xA5, sizeof g_cfg);
    g_sent = g_cfg;
    CHECK_EQ_I64(t, annas_config_load(ports_fake_env_port(&g_env), true, 0, &g_cfg, &g_err), ANNAS_E_CONFIG);
    CHECK_EQ_STR(t, g_err.message, MISSING);
}

static void t_relative(ctest_t *t) {
    static const char *const bad[] = {"dl", "./dl", "../dl", "a/b", " /dl", "C:/dl", "~"};
    for (size_t i = 0; i < COUNT(bad); i++) {
        char want[160];
        snprintf(want, sizeof want, "ANNAS_DOWNLOAD_PATH must be an absolute path, got: %s", bad[i]);
        expect_fail(t, load(NULL, "fake-key", bad[i], true, 0), ANNAS_E_CONFIG, want);
    }
    static const char *const good[] = {"/", "/dl", "/dl/", "/a/b c/d", "//x"};
    for (size_t i = 0; i < COUNT(good); i++) {
        expect_ok(t, load(NULL, "fake-key", good[i], true, 0), "annas-archive.gl", 3600000u);
        CHECK_EQ_U64(t, g_cfg.download_dir.len, strlen(good[i]));
        CHECK_EQ_MEM(t, g_cfg.download_dir.ptr, g_cfg.download_dir.len, good[i], strlen(good[i]));
    }
}

static void t_order_of_checks(ctest_t *t) {
    /* timeout beats base, base beats missing, missing beats relative */
    expect_fail(t, load("Bad", "k", "/d", true, -1), ANNAS_E_ARG, "timeout must be 0 or more seconds, got: -1");
    expect_fail(t, load("Bad", NULL, "rel", true, 0), ANNAS_E_CONFIG, "ANNAS_BASE_URL is not a valid domain, got: Bad");
    expect_fail(t, load(NULL, NULL, "rel", true, 0), ANNAS_E_CONFIG, MISSING);
    expect_fail(t, load(NULL, "k", "rel", true, 0), ANNAS_E_CONFIG,
                "ANNAS_DOWNLOAD_PATH must be an absolute path, got: rel");
}

static void t_timeout(ctest_t *t) {
    expect_ok(t, load(NULL, "k", "/d", true, 1), "annas-archive.gl", 1000u);
    expect_ok(t, load(NULL, "k", "/d", true, 3600), "annas-archive.gl", 3600000u);
    expect_ok(t, load(NULL, "k", "/d", true, 31536000), "annas-archive.gl", 31536000000u);
    expect_ok(t, load(NULL, "k", "/d", false, 7), "annas-archive.gl", 7000u);
    expect_fail(t, load(NULL, "k", "/d", true, 31536001), ANNAS_E_ARG,
                "timeout must be at most 31536000 seconds, got: 31536001");
    expect_fail(t, load(NULL, "k", "/d", true, INT64_MAX), ANNAS_E_ARG,
                "timeout must be at most 31536000 seconds, got: 9223372036854775807");
    expect_fail(t, load(NULL, "k", "/d", true, -1), ANNAS_E_ARG, "timeout must be 0 or more seconds, got: -1");
    expect_fail(t, load(NULL, "k", "/d", false, INT64_MIN), ANNAS_E_ARG,
                "timeout must be 0 or more seconds, got: -9223372036854775808");
}

static void t_size_limits(ctest_t *t) {
    memset(g_long, 'k', 256);
    g_long[256] = '\0';
    expect_ok(t, load(NULL, g_long, "/d", true, 0), "annas-archive.gl", 3600000u);
    g_long[256] = 'k';
    g_long[257] = '\0';
    expect_fail(t, load(NULL, g_long, "/d", true, 0), ANNAS_E_CONFIG, "ANNAS_SECRET_KEY is longer than 256 bytes");
    memset(g_long, 'd', 256);
    g_long[0] = '/';
    g_long[256] = '\0';
    expect_ok(t, load(NULL, "k", g_long, true, 0), "annas-archive.gl", 3600000u);
    g_long[256] = 'd';
    g_long[257] = '\0';
    expect_fail(t, load(NULL, "k", g_long, true, 0), ANNAS_E_CONFIG, "ANNAS_DOWNLOAD_PATH is longer than 256 bytes");
    expect_ok(t, load(NULL, g_long, "/d", false, 0), "annas-archive.gl", 3600000u); /* limits only when needed */
}

static void t_echo_is_one_line_and_cut(ctest_t *t) {
    expect_fail(t, load(NULL, "k", "rel\nline\x01x\x7f", true, 0), ANNAS_E_CONFIG,
                "ANNAS_DOWNLOAD_PATH must be an absolute path, got: rel?line?x?");
    memset(g_long, 'a', 250);
    g_long[250] = '\0';
    char want[300];
    memset(want, 0, sizeof want);
    snprintf(want, sizeof want, "ANNAS_DOWNLOAD_PATH must be an absolute path, got: ");
    memset(want + strlen(want), 'a', 200);
    expect_fail(t, load(NULL, "k", g_long, true, 0), ANNAS_E_CONFIG, want);
}

static void t_key_is_redacted_from_echoes(ctest_t *t) {
    expect_fail(t, load(NULL, "zzfakekeyzz", "zzfakekeyzz", true, 0), ANNAS_E_CONFIG,
                "ANNAS_DOWNLOAD_PATH must be an absolute path, got: REDACTED");
    expect_fail(t, load(NULL, "zzfakekeyzz", "a/zzfakekeyzz/b/zzfakekeyzz", true, 0), ANNAS_E_CONFIG,
                "ANNAS_DOWNLOAD_PATH must be an absolute path, got: a/REDACTED/b/REDACTED");
    expect_fail(t, load("ZZ.zzfakekeyzz!", "zzfakekeyzz", "/d", true, 0), ANNAS_E_CONFIG,
                "ANNAS_BASE_URL is not a valid domain, got: ZZ.REDACTED!");
    expect_fail(t, load("ZZ.zzfakekeyzz!", "zzfakekeyzz", NULL, false, 0), ANNAS_E_CONFIG,
                "ANNAS_BASE_URL is not a valid domain, got: ZZ.REDACTED!");
    /* percent-encoded form of a key with specials */
    expect_fail(t, load(NULL, "fake key/1", "fake%20key%2F1", true, 0), ANNAS_E_CONFIG,
                "ANNAS_DOWNLOAD_PATH must be an absolute path, got: REDACTED");
    expect_fail(t, load(NULL, "fake key/1", "fake key/1", true, 0), ANNAS_E_CONFIG,
                "ANNAS_DOWNLOAD_PATH must be an absolute path, got: REDACTED");
    /* control: a different value is not touched */
    expect_fail(t, load(NULL, "zzfakekeyzz", "zzfakekey", true, 0), ANNAS_E_CONFIG,
                "ANNAS_DOWNLOAD_PATH must be an absolute path, got: zzfakekey");
}

static void t_null_arguments(ctest_t *t) {
    ports_fake_env_t e;
    CHECK(t, ports_fake_env_init(&e, NULL, 0));
    memset(&g_err, 0xA5, sizeof g_err);
    CHECK_EQ_I64(t, annas_config_load(ports_fake_env_port(&e), false, 0, NULL, &g_err), ANNAS_E_ARG);
    CHECK_EQ_I64(t, g_err.code, ANNAS_E_ARG);
    CHECK_EQ_STR(t, g_err.message, "invalid argument");
    CHECK_EQ_I64(t, annas_config_load(ports_fake_env_port(&e), false, 0, &g_cfg, NULL), ANNAS_E_ARG);
    CHECK_EQ_I64(t, annas_config_load(ports_fake_env_port(&e), false, 0, &g_cfg, &g_err), ANNAS_OK); /* control */
}

CTEST_SUITE("config",
    {"defaults", t_defaults},
    {"empty_base_is_default", t_empty_base_is_default},
    {"base_normalisation", t_base_normalisation},
    {"base_invalid", t_base_invalid},
    {"base_checked_even_without_download", t_base_checked_even_without_download},
    {"missing", t_missing},
    {"not_required_without_download", t_not_required_without_download},
    {"names_are_case_sensitive", t_names_are_case_sensitive},
    {"relative", t_relative},
    {"order_of_checks", t_order_of_checks},
    {"timeout", t_timeout},
    {"size_limits", t_size_limits},
    {"echo_is_one_line_and_cut", t_echo_is_one_line_and_cut},
    {"key_is_redacted_from_echoes", t_key_is_redacted_from_echoes},
    {"null_arguments", t_null_arguments})
