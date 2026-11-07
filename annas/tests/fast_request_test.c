/* annas_fast_download_url: the exact request bytes and the argument validation (before any port call). */
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

#define OKRESP(k) kit_http((k), 200, "OK", "", "{\"download_url\":\"https://dl.example.org/f/1\"}")
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void expect_success(ctest_t *t, kit_t *k, annas_code_t got) {
    kit_check_ok(t, k, got);
    CHECK_EQ_STR(t, k->fast.url, "https://dl.example.org/f/1");
    CHECK_EQ_U64(t, k->fast.url_len, 26);
    CHECK_EQ_STR(t, k->fast.account, "");
    CHECK_EQ_U64(t, k->fast.account_len, 0);
}

static void t_default_request_is_exact(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_connect(k, "annas-archive.gl", 443);
    kit_expect(k, "GET /dyn/api/fast_download.json?md5=0123456789abcdef0123456789abcdef&key=FAKEKEY-not-a-real-key-0123456789"
                  " HTTP/1.1\r\nHost: annas-archive.gl\r\nUser-Agent: annas-c/0.1\r\nAccept: */*\r\n"
                  "Connection: close\r\n\r\n");
    kit_respond(k, OKRESP(k), 0);
    annas_request_t r = kit_req(KIT_MD5);
    expect_success(t, k, kit_fast(k, &r));
    CHECK_EQ_U64(t, ports_fake_fs_committed_count(&k->fs) + ports_fake_fs_temp_count(&k->fs), 0);
}

static void t_indices(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "&path_index=0", OKRESP(k));
    annas_request_t r = kit_req(KIT_MD5);
    r.has_path_index = true;
    expect_success(t, k, kit_fast(k, &r));

    k = kit_begin();
    kit_api(k, KIT_MD5, "&domain_index=7", OKRESP(k));
    r = kit_req(KIT_MD5);
    r.has_domain_index = true;
    r.domain_index = 7;
    expect_success(t, k, kit_fast(k, &r));

    k = kit_begin();
    kit_api(k, KIT_MD5, "&path_index=12&domain_index=0", OKRESP(k));
    r = kit_req(KIT_MD5);
    r.has_path_index = r.has_domain_index = true;
    r.path_index = 12;
    expect_success(t, k, kit_fast(k, &r));
}

static void t_index_values_without_flag_are_ignored(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", OKRESP(k));
    annas_request_t r = kit_req(KIT_MD5);
    r.path_index = 5;
    r.domain_index = 6;
    expect_success(t, k, kit_fast(k, &r));
}

static void t_largest_index(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "&path_index=18446744073709551615&domain_index=4294967296", OKRESP(k));
    annas_request_t r = kit_req(KIT_MD5);
    r.has_path_index = r.has_domain_index = true;
    r.path_index = UINT64_MAX;
    r.domain_index = 4294967296u;
    expect_success(t, k, kit_fast(k, &r));
}

static void t_md5_is_lower_cased(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, "d6e1dc51a50726f00ec438af21952a45", "", OKRESP(k));
    annas_request_t r = kit_req("D6E1DC51A50726F00EC438AF21952A45");
    expect_success(t, k, kit_fast(k, &r));
    k = kit_begin();
    kit_api(k, "d6e1dc51a50726f00ec438af21952a45", "", OKRESP(k));
    r = kit_req("D6e1dC51a50726F00eC438aF21952a45");
    expect_success(t, k, kit_fast(k, &r));
}

static void t_custom_base(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_set_base(k, "mirror-2.example.org");
    kit_connect(k, "mirror-2.example.org", 443);
    kit_expect(k, "GET /dyn/api/fast_download.json?md5=0123456789abcdef0123456789abcdef&key=FAKEKEY-not-a-real-key-0123456789"
                  " HTTP/1.1\r\nHost: mirror-2.example.org\r\nUser-Agent: annas-c/0.1\r\nAccept: */*\r\n"
                  "Connection: close\r\n\r\n");
    kit_respond(k, OKRESP(k), 0);
    annas_request_t r = kit_req(KIT_MD5);
    expect_success(t, k, kit_fast(k, &r));
}

static void t_key_is_percent_encoded(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_set_key(k, "aZ09.b_c~d-e"); /* every key byte is URL-unreserved: pct_encode(key) == key, sent as it is */
    kit_connect(k, "annas-archive.gl", 443);
    kit_expect(k, "GET /dyn/api/fast_download.json?md5=0123456789abcdef0123456789abcdef"
                  "&key=aZ09.b_c~d-e HTTP/1.1\r\nHost: annas-archive.gl\r\n"
                  "User-Agent: annas-c/0.1\r\nAccept: */*\r\nConnection: close\r\n\r\n");
    kit_respond(k, OKRESP(k), 0);
    annas_request_t r = kit_req(KIT_MD5);
    expect_success(t, k, kit_fast(k, &r));
    k = kit_begin(); /* a byte that would need percent-encoding is not a key at all: no request is made */
    kit_set_key(k, "a b+c/d=e&f%g#h?i\xc3\xa9");
    r = kit_req(KIT_MD5);
    kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_CONFIG, 0,
                  "ANNAS_SECRET_KEY may only contain letters, digits and the characters . _ ~ -");
}

static void t_title_and_format_are_ignored(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", OKRESP(k));
    annas_request_t r = kit_req(KIT_MD5);
    r.title = core_str_from_cstr("../../etc/passwd");
    r.format = core_str_from_cstr("not/a format");
    expect_success(t, k, kit_fast(k, &r));
}

static void t_two_calls_on_one_context(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", "{\"download_url\":\"https://a.example.org/1\"}"));
    kit_api(k, "ffffffffffffffffffffffffffffffff", "", kit_http(k, 200, "OK", "", "{\"download_url\":\"https://b.example.org/22\"}"));
    annas_request_t r = kit_req(KIT_MD5);
    CHECK_EQ_I64(t, kit_fast(k, &r), ANNAS_OK);
    CHECK_EQ_STR(t, k->fast.url, "https://a.example.org/1");
    CHECK(t, !ports_fake_stream_is_open(&k->stream));
    r = kit_req("ffffffffffffffffffffffffffffffff");
    CHECK_EQ_I64(t, annas_fast_download_url(&k->ctx, &r, &k->fast, &k->err), ANNAS_OK);
    CHECK_EQ_STR(t, k->fast.url, "https://b.example.org/22");
    CHECK_EQ_U64(t, k->fast.url_len, 24);
    kit_check_ok(t, k, ANNAS_OK);
}

static void t_bad_md5(ctest_t *t) {
    static const char *const bad[] = {"", "0", "0123456789abcdef0123456789abcde", "0123456789abcdef0123456789abcdef0",
                                      "0123456789abcdef0123456789abcdeg", "g123456789abcdef0123456789abcdef",
                                      "0123456789abcdef 123456789abcdef", " 123456789abcdef0123456789abcdef",
                                      "0123456789abcdef0123456789abcde ", "0123456789abcdef0123456789abcdef\n",
                                      "0x23456789abcdef0123456789abcdef", "0123456789abcdef0123456789abcdé",
                                      "-123456789abcdef0123456789abcdef"};
    for (size_t i = 0; i < COUNT(bad); i++) {
        kit_t *k = kit_begin();
        annas_request_t r = kit_req(bad[i]);
        kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_ARG, 0, "md5 must be 32 hexadecimal characters");
        CHECK_EQ_U64(t, ports_fake_stream_pos(&k->stream), 0);
    }
    /* embedded NUL: 32 bytes long but not hex */
    kit_t *k = kit_begin();
    annas_request_t r = {{"0123456789abcdef" "\0" "123456789abcdef", 32}, {NULL, 0}, {NULL, 0}, false, 0, false, 0};
    kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_ARG, 0, "md5 must be 32 hexadecimal characters");
}

static void t_all_hex_digits_accepted(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, "0123456789abcdefabcdef0123456789", "", OKRESP(k));
    annas_request_t r = kit_req("0123456789ABCDEFabcdef0123456789");
    expect_success(t, k, kit_fast(k, &r));
}

CTEST_SUITE("fast_request",
    {"default_request_is_exact", t_default_request_is_exact},
    {"indices", t_indices},
    {"index_values_without_flag_are_ignored", t_index_values_without_flag_are_ignored},
    {"largest_index", t_largest_index},
    {"md5_is_lower_cased", t_md5_is_lower_cased},
    {"custom_base", t_custom_base},
    {"key_is_percent_encoded", t_key_is_percent_encoded},
    {"title_and_format_are_ignored", t_title_and_format_are_ignored},
    {"two_calls_on_one_context", t_two_calls_on_one_context},
    {"bad_md5", t_bad_md5},
    {"all_hex_digits_accepted", t_all_hex_digits_accepted})
