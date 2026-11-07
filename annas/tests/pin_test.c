/* A038: pins found by the first mutation run of annas (R004 T6/T7 classes): an empty account string, a one-byte error
 * body, one byte after a complete message, a one-byte Location, one-byte NULL slices, a download directory of "/",
 * a percent escape cut off by the end of the text, the 200-byte base echo, the status of the missing-config error and
 * one-byte account texts in the output formatters. Fake keys only. */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

#define DLURL "https://dl.example.org/f/1"
#define MISSING "ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set"


/* ---- result strings ------------------------------------------------------------------------------------------ */

static void t_account_is_the_empty_string_when_absent_or_not_an_object(ctest_t *t) {
    static const char *const accounts[] = {NULL, "null", "5", "\"s\"", "[1]"};
    for (size_t i = 0; i < sizeof accounts / sizeof accounts[0]; i++) {
        kit_t *k = kit_begin();
        kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, accounts[i])));
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_ok(t, k, kit_fast(k, &r));
        CHECK_EQ_U64(t, k->fast.account_len, 0);
        CHECK_EQ_I64(t, k->fast.account[0], 0);
        CHECK_EQ_STR(t, k->fast.url, DLURL);
    }
}

/* A second call on the same context: the previous account text is still in the workspace and must not show through. */
static void t_account_of_an_earlier_call_does_not_show_through(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, "{\"downloads_left\":2}")));
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, NULL)));
    annas_request_t r = kit_req(KIT_MD5);
    CHECK_EQ_I64(t, kit_fast(k, &r), ANNAS_OK);
    CHECK_EQ_STR(t, k->fast.account, "{\"downloads_left\":2}");
    kit_check_ok(t, k, kit_fast(k, &r));
    CHECK_EQ_U64(t, k->fast.account_len, 0);
    CHECK_EQ_STR(t, k->fast.account, "");
}

static void t_one_byte_non_json_error_body_is_the_detail(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", kit_http(k, 500, "X", "", "x"));
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_API, 500, "500: x");
}

static void t_one_byte_after_a_complete_message_is_malformed(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", KIT_FMT(k, "%s!", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, NULL))));
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_PARSE, 0, "malformed HTTP response");
}

static void t_one_byte_location_is_followed(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, NULL)));
    kit_get(k, "dl.example.org", 443, "/f/1", kit_http(k, 302, "Found", "Location: /\r\n", ""));
    kit_get(k, "dl.example.org", 443, "/", kit_http(k, 200, "OK", "", "data"));
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, KIT_MD5 ".bin", "data", 4);
}

/* ---- one-byte NULL slices ------------------------------------------------------------------------------------ */

static void t_null_slices_of_one_byte_are_invalid_arguments(ctest_t *t) {
    kit_t *k = kit_begin();
    annas_request_t r = kit_req(KIT_MD5);
    r.md5 = (core_str_t){NULL, 1};
    kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_ARG, 0, "invalid argument");
    k = kit_begin();
    r.md5 = (core_str_t){NULL, 1};
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_ARG, 0, "invalid argument");
    k = kit_begin();
    r = kit_req(KIT_MD5);
    r.title = (core_str_t){NULL, 1};
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_ARG, 0, "invalid argument");
    k = kit_begin();
    r = kit_req(KIT_MD5);
    r.format = (core_str_t){NULL, 1};
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_ARG, 0, "invalid argument");
    kit_check_fs_untouched(t, k);
}

/* ---- the download directory "/" ----------------------------------------------------------------------------- */

static char g_slashes[24] = "///////////////////////";

static void t_root_directory_gives_a_path_below_the_root(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, NULL)));
    kit_get(k, "dl.example.org", 443, "/f/1", kit_http(k, 200, "OK", "", "data"));
    CHECK(t, kit_prepare(k));
    k->cfg.download_dir = (core_str_t){g_slashes + 12, 1}; /* "/" with more slashes in front of it in memory */
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, KIT_MD5 ".bin", "data", 4);
    CHECK_EQ_STR(t, k->dl.path, "/" KIT_MD5 ".bin");
    CHECK_EQ_U64(t, k->dl.path_len, 1u + strlen(KIT_MD5 ".bin"));
}

/* ---- a percent escape cut off by the end of the text -------------------------------------------------------- */

static void t_percent_escape_cut_off_by_the_end_of_the_text(ctest_t *t) {
    static const char dir_bytes[] = "apq-0123456789xyz"; /* the byte after the slice below would complete the key */
    ports_fake_env_entry_t ent[2] = {
        {core_str_from_cstr("ANNAS_SECRET_KEY"), core_str_from_cstr("pq-0123456789xyz")},
        {core_str_from_cstr("ANNAS_DOWNLOAD_PATH"), {dir_bytes, 16}},
    };
    static ports_fake_env_t env;
    static annas_config_t cfg;
    static annas_error_t err;
    CHECK(t, ports_fake_env_init(&env, ent, 2));
    CHECK_EQ_I64(t, annas_config_load(ports_fake_env_port(&env), true, 0, &cfg, &err), ANNAS_E_CONFIG);
    CHECK_EQ_STR(t, err.message, "ANNAS_DOWNLOAD_PATH must be an absolute path, got: apq-0123456789xy");
    CHECK_EQ_I64(t, err.http_status, 0);
    ent[1].value.len = 17; /* control: the whole path holds the key */
    CHECK(t, ports_fake_env_init(&env, ent, 2));
    CHECK_EQ_I64(t, annas_config_load(ports_fake_env_port(&env), true, 0, &cfg, &err), ANNAS_E_CONFIG);
    CHECK_EQ_STR(t, err.message, "ANNAS_DOWNLOAD_PATH must be an absolute path, got: aREDACTED");
}

/* ---- echoes and statuses ------------------------------------------------------------------------------------ */

static void t_invalid_base_echo_is_cut_at_200_bytes(ctest_t *t) {
    static char base[251];
    memset(base, 'a', 250);
    base[250] = '\0';
    ports_fake_env_entry_t ent[1] = {{core_str_from_cstr("ANNAS_BASE_URL"), core_str_from_cstr(base)}};
    static ports_fake_env_t env;
    static annas_config_t cfg;
    static annas_error_t err;
    static char want[300];
    size_t n = (size_t)snprintf(want, sizeof want, "ANNAS_BASE_URL is not a valid domain, got: ");
    memset(want + n, 'a', 200);
    want[n + 200] = '\0';
    CHECK(t, ports_fake_env_init(&env, ent, 1));
    CHECK_EQ_I64(t, annas_config_load(ports_fake_env_port(&env), false, 0, &cfg, &err), ANNAS_E_CONFIG);
    CHECK_EQ_STR(t, err.message, want);
    CHECK_EQ_U64(t, err.message_len, strlen(want));
    CHECK_EQ_I64(t, err.http_status, 0);
}

/* kit_check_err is not used for an empty key: every text "contains" it. */
static void missing_config(ctest_t *t, kit_t *k, annas_code_t got) {
    CHECK_EQ_I64(t, got, ANNAS_E_CONFIG);
    CHECK_EQ_I64(t, k->err.code, ANNAS_E_CONFIG);
    CHECK_EQ_I64(t, k->err.http_status, 0);
    CHECK_EQ_STR(t, k->err.message, MISSING);
    CHECK_EQ_U64(t, k->err.message_len, strlen(MISSING));
    CHECK_EQ_U64(t, ports_fake_stream_pos(&k->stream), 0);
}

static void t_missing_key_or_dir_has_http_status_zero(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_set_key(k, "");
    annas_request_t r = kit_req(KIT_MD5);
    missing_config(t, k, kit_fast(k, &r));
    k = kit_begin();
    kit_set_key(k, "");
    missing_config(t, k, kit_download(k, &r));
    k = kit_begin();
    kit_set_dir(k, "");
    missing_config(t, k, kit_download(k, &r));
    kit_check_fs_untouched(t, k);
}

/* ---- formatters: an account text of one byte ---------------------------------------------------------------- */

static void t_one_byte_account_is_printed(ctest_t *t) {
    static annas_fast_result_t f;
    static annas_download_result_t d;
    char out[200];
    core_buf_t b;
    memset(&f, 0, sizeof f);
    memset(&d, 0, sizeof d);
    memcpy(f.url, "u", 2);
    f.url_len = 1;
    memcpy(f.account, "x", 2);
    f.account_len = 1;
    CHECK(t, core_buf_init(&b, out, sizeof out));
    CHECK(t, annas_format_download_url(&b, &f));
    CHECK_EQ_MEM(t, core_buf_str(&b).ptr, core_buf_str(&b).len, "Download URL: u\nAccount: x",
                 strlen("Download URL: u\nAccount: x"));
    d.fast = f;
    memcpy(d.path, "/p", 3);
    d.path_len = 2;
    CHECK(t, core_buf_init(&b, out, sizeof out));
    CHECK(t, annas_format_downloaded(&b, &d));
    CHECK_EQ_MEM(t, core_buf_str(&b).ptr, core_buf_str(&b).len, "Book downloaded successfully to: /p\nAccount: x",
                 strlen("Book downloaded successfully to: /p\nAccount: x"));
}

CTEST_SUITE("pin",
    {"account_is_the_empty_string_when_absent_or_not_an_object", t_account_is_the_empty_string_when_absent_or_not_an_object},
    {"account_of_an_earlier_call_does_not_show_through", t_account_of_an_earlier_call_does_not_show_through},
    {"one_byte_non_json_error_body_is_the_detail", t_one_byte_non_json_error_body_is_the_detail},
    {"one_byte_after_a_complete_message_is_malformed", t_one_byte_after_a_complete_message_is_malformed},
    {"one_byte_location_is_followed", t_one_byte_location_is_followed},
    {"null_slices_of_one_byte_are_invalid_arguments", t_null_slices_of_one_byte_are_invalid_arguments},
    {"root_directory_gives_a_path_below_the_root", t_root_directory_gives_a_path_below_the_root},
    {"percent_escape_cut_off_by_the_end_of_the_text", t_percent_escape_cut_off_by_the_end_of_the_text},
    {"invalid_base_echo_is_cut_at_200_bytes", t_invalid_base_echo_is_cut_at_200_bytes},
    {"missing_key_or_dir_has_http_status_zero", t_missing_key_or_dir_has_http_status_zero},
    {"one_byte_account_is_printed", t_one_byte_account_is_printed})
