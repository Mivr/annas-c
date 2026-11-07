/* The member-key class and the download-path rule (annas.h, Messages: KEY CLASS and DOWNLOAD PATH). A key is 1..256
 * bytes of [A-Za-z0-9._~-]; a download path that contains the key is refused; both before any port call, in
 * annas_config_load and in the two operations. Fake keys only. */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"
#include "ports/fakes.h"

#define KEYSHORT "ANNAS_SECRET_KEY must be 16 to 256 bytes"
#define KA "zq9-0123456789ab" /* a 16-byte fake key */
#define K7 "7777777777777777"
#define KQ "zqzqzqzqzqzqzqzq"
#define KAB "abababababababab"
#define KEYRED "ANNAS_SECRET_KEY must not contain REDACTED"
#define KEYBAD "ANNAS_SECRET_KEY may only contain letters, digits and the characters . _ ~ -"
#define DIRKEY "ANNAS_DOWNLOAD_PATH contains the key"
#define KEYLONG "ANNAS_SECRET_KEY is longer than 256 bytes"
#define DIRLONG "ANNAS_DOWNLOAD_PATH is longer than 256 bytes"
#define MISSING "ANNAS_SECRET_KEY and ANNAS_DOWNLOAD_PATH environment variables must be set"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static annas_config_t g_cfg, g_sent;
static annas_error_t g_err;
static ports_fake_env_t g_env;
static ports_fake_env_entry_t g_ent[2];
static char g_key[300], g_dir[300], g_all[80];

static bool in_class(unsigned b) {
    return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '.' || b == '_' ||
           b == '~' || b == '-';
}

/* The key and dir are slices (a key may hold a NUL byte); NULL ptr: variable unset. */
static annas_code_t load_s(core_str_t key, core_str_t dir, bool need) {
    size_t n = 0;
    if (key.ptr != NULL) {
        g_ent[n++] = (ports_fake_env_entry_t){core_str_from_cstr("ANNAS_SECRET_KEY"), key};
    }
    if (dir.ptr != NULL) {
        g_ent[n++] = (ports_fake_env_entry_t){core_str_from_cstr("ANNAS_DOWNLOAD_PATH"), dir};
    }
    if (!ports_fake_env_init(&g_env, g_ent, n)) {
        return ANNAS_OK;
    }
    memset(&g_cfg, 0xA5, sizeof g_cfg);
    g_sent = g_cfg;
    memset(&g_err, 0xA5, sizeof g_err);
    return annas_config_load(ports_fake_env_port(&g_env), need, 0, &g_cfg, &g_err);
}

static annas_code_t load(const char *key, const char *dir, bool need) {
    return load_s(key == NULL ? (core_str_t){NULL, 0} : core_str_from_cstr(key),
                  dir == NULL ? (core_str_t){NULL, 0} : core_str_from_cstr(dir), need);
}

static void expect_fail(ctest_t *t, annas_code_t got, const char *msg) {
    CHECK_EQ_I64(t, got, ANNAS_E_CONFIG);
    CHECK_EQ_I64(t, g_err.code, ANNAS_E_CONFIG);
    CHECK_EQ_I64(t, g_err.http_status, 0);
    CHECK_EQ_STR(t, g_err.message, msg);
    CHECK_EQ_U64(t, g_err.message_len, strlen(msg));
    CHECK(t, memcmp(&g_cfg, &g_sent, sizeof g_cfg) == 0); /* the config is left byte for byte unchanged */
}

static void expect_ok(ctest_t *t, annas_code_t got) {
    CHECK_EQ_I64(t, got, ANNAS_OK);
    CHECK_EQ_I64(t, g_err.code, ANNAS_OK);
    CHECK_EQ_U64(t, g_err.message_len, 0);
}

/* ---- annas_config_load: the key class ------------------------------------------------------------------------ */

static void t_config_every_byte_outside_the_key_class_is_refused(ctest_t *t) {
    for (unsigned b = 0; b < 256u; b++) {
        if (in_class(b)) {
            continue;
        }
        char c = (char)b;
        char key[3][16] = {{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', c},
                           {c, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o'},
                           {'a', 'b', 'c', 'd', 'e', 'f', 'g', c, 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o'}};
        for (unsigned i = 0; i < 3u; i++) {
            expect_fail(t, load_s((core_str_t){key[i], 16}, core_str_from_cstr("/dl"), true), KEYBAD);
        }
    }
}

static void t_config_lower_case_percent_forms_are_refused(ctest_t *t) {
    static const char *const bad[] = {"abcdefghijkl%1ecd", "abcdefghijkl%1Ecd", "abcdefghijklmno%", "abcdefghijklmn%zz",
                                      "abcdefghijklm%20", "%00abcdefghijklmn", "%1eabcdefghijklmn"};
    for (size_t i = 0; i < COUNT(bad); i++) {
        expect_fail(t, load(bad[i], "/dl", true), KEYBAD);
    }
}

static void t_config_the_whole_class_is_accepted(ctest_t *t) {
    size_t n = 0;
    for (unsigned b = 1; b < 256u; b++) {
        if (in_class(b)) {
            g_all[n++] = (char)b;
        }
    }
    g_all[n] = '\0';
    CHECK_EQ_U64(t, n, 66);
    expect_ok(t, load(g_all, "/dl", true));
    CHECK_EQ_MEM(t, g_cfg.key.ptr, g_cfg.key.len, g_all, 66);
    static const char *const same[] = {"aaaaaaaaaaaaaaaa", "ZZZZZZZZZZZZZZZZ", "0000000000000000", "................",
                                       "________________", "~~~~~~~~~~~~~~~~", "----------------"};
    for (size_t i = 0; i < COUNT(same); i++) {
        expect_ok(t, load(same[i], "/dl", true));
        CHECK_EQ_U64(t, g_cfg.key.len, 16);
    }
}

static void t_config_the_length_limits_are_16_and_256(ctest_t *t) {
    memset(g_key, 'a', 256);
    g_key[256] = '\0';
    expect_ok(t, load(g_key, "/dl", true));
    CHECK_EQ_U64(t, g_cfg.key.len, 256);
    g_key[256] = 'a';
    g_key[257] = '\0';
    expect_fail(t, load(g_key, "/dl", true), KEYLONG);
    g_key[256] = '\0';
    g_key[255] = '!'; /* 256 bytes, the last outside the class */
    expect_fail(t, load(g_key, "/dl", true), KEYBAD);
    g_key[255] = 'a';
    g_key[10] = '!';
    g_key[256] = 'a';
    g_key[257] = '\0'; /* 257 bytes and a bad byte: the length message comes first */
    expect_fail(t, load(g_key, "/dl", true), KEYLONG);
}

static void t_config_a_key_of_15_bytes_or_less_is_refused(ctest_t *t) {
    for (size_t n = 1; n <= 15u; n++) {
        memset(g_key, 'a', n);
        g_key[n] = '\0';
        expect_fail(t, load(g_key, "/dl", true), KEYSHORT);
    }
    memset(g_key, 'a', 16);
    g_key[16] = '\0';
    expect_ok(t, load(g_key, "/dl", true)); /* 16 is the first accepted length */
    CHECK_EQ_U64(t, g_cfg.key.len, 16);
    static const char *const short_bad[] = {"a b", "ab%1ecd", "!", "q#q#q#q#q#q#q#q", "abcdefghijklmn\x01"};
    for (size_t i = 0; i < COUNT(short_bad); i++) { /* too short wins over a bad byte */
        expect_fail(t, load(short_bad[i], "/dl", true), KEYSHORT);
    }
    expect_fail(t, load("qqqq", "relative", true), KEYSHORT);              /* before the path checks */
    expect_fail(t, load("qqqq", "/qqqq", true), KEYSHORT);
    expect_fail(t, load("qqqq", NULL, true), MISSING);                     /* the missing-variable check is first */
    expect_ok(t, load("qqqq", "/dl", false));                              /* not a download: no key checks at all */
    CHECK_EQ_U64(t, g_cfg.key.len, 4);
}

static const char *const g_red[] = {"REDACTEDREDACTED", "REDACTEDabcdefghi", "abcdefgREDACTEDhijk", "abcdefghijklREDACTED",
                                    "xREDACTEDxxxxxxxxx", "REDACTED-0123456789", "q.REDACTED.REDACTED.q"};
static const char *const g_red_ok[] = {"REDACTEdREDACTEd", "redactedredacted", "REDACTE_DREDACTE_D", "REDACTEabcdefghijkl",
                                       "REDACTE-0123456789"};

static void t_config_a_key_containing_redacted_is_refused(ctest_t *t) {
    for (size_t i = 0; i < COUNT(g_red); i++) {
        expect_fail(t, load(g_red[i], "/dl", true), KEYRED);
        expect_fail(t, load(g_red[i], "relative", true), KEYRED);               /* before the path checks */
        expect_fail(t, load(g_red[i], "/dl", true), KEYRED);
    }
    snprintf(g_dir, sizeof g_dir, "/%s", g_red[0]);
    expect_fail(t, load(g_red[0], g_dir, true), KEYRED);                     /* before key-in-path */
    expect_fail(t, load("REDACTED", "/dl", true), KEYSHORT);                 /* too short first */
    expect_fail(t, load("REDACTED !abcdefghij", "/dl", true), KEYBAD);       /* the class first */
    expect_fail(t, load(g_red[0], NULL, true), MISSING);
    expect_ok(t, load(g_red[0], "/dl", false));                               /* not a download: no key checks */
    for (size_t i = 0; i < COUNT(g_red_ok); i++) {
        expect_ok(t, load(g_red_ok[i], "/dl", true)); /* near misses: another case, or a gap, are not the text */
        CHECK(t, g_cfg.key.len >= 16u);
    }
}

static void t_config_key_class_is_checked_only_when_a_download_needs_it(ctest_t *t) {
    expect_ok(t, load("abcdefghijk mnopq", "/dl", false));
    CHECK_EQ_U64(t, g_cfg.key.len, 17);
    expect_ok(t, load("abcdefghijk mnopq", NULL, false));
    expect_fail(t, load("abcdefghijk mnopq", "/dl", true), KEYBAD);
    expect_fail(t, load("abcdefghijk mnopq", NULL, true), MISSING); /* the missing-variable check is first */
    expect_fail(t, load("", "/dl", true), MISSING);
}

/* ---- annas_config_load: the download path ---------------------------------------------------------------------- */

typedef struct {
    const char *key;
    const char *dir;
} pair_t;

static void t_config_a_path_containing_the_key_is_refused(ctest_t *t) {
    static const pair_t c[] = {
        {K7, "/" K7}, {K7, "/a/" K7 "/b"}, {K7, "/a/b" K7}, {K7, "/" K7 "/"}, {K7, "/7" K7}, {KA, "/" KA}, {KA, "/d/" KA},
        {KA, "/" KA "/d"}, {KA, "/a/" KA "/b/" KA}, {KA, "/z" KA}, {KA, "/" KA KA}, {KQ, "/" KQ "zqzq"},
        {KAB, "/ab" KAB}, {"q-x.7_~-0123456789", "/dl/q-x.7_~-0123456789/"},
        {"q-x.7_~-0123456789", "/dl/a q-x.7_~-0123456789 b"}};
    for (size_t i = 0; i < COUNT(c); i++) {
        expect_fail(t, load(c[i].key, c[i].dir, true), DIRKEY);
    }
    memset(g_key, '5', 63); /* a long key at the start, in the middle and at the end of the path */
    g_key[63] = '\0';
    snprintf(g_dir, sizeof g_dir, "/%s", g_key);
    expect_fail(t, load(g_key, g_dir, true), DIRKEY);
    snprintf(g_dir, sizeof g_dir, "/dl/%s/books", g_key);
    expect_fail(t, load(g_key, g_dir, true), DIRKEY);
    snprintf(g_dir, sizeof g_dir, "/books/%s", g_key);
    expect_fail(t, load(g_key, g_dir, true), DIRKEY);
    memset(g_key, '6', 200); /* 200-byte key, 201-byte path */
    g_key[200] = '\0';
    snprintf(g_dir, sizeof g_dir, "/%s", g_key);
    expect_fail(t, load(g_key, g_dir, true), DIRKEY);
}

static void t_config_near_misses_in_the_path_are_accepted(ctest_t *t) {
    static const pair_t c[] = {{KA, "/zq9-0123456789a"}, {KA, "/zq9-0123456789/ab"}, {KA, "/ZQ9-0123456789AB"},
                               {KA, "/zq9-0123456789a-b"}, {KA, "/zq9-0123456789a%62"}, {KQ, "/zqzqzqzqzqzqzqzx"},
                               {K7, "/777777777777777"}, {K7, "/7777777/77777777"}};
    for (size_t i = 0; i < COUNT(c); i++) {
        expect_ok(t, load(c[i].key, c[i].dir, true));
    }
    expect_ok(t, load(KA, "/" KA, false)); /* not a download: no path check */
}

static void t_config_precedence_of_the_key_and_path_checks(ctest_t *t) {
    expect_fail(t, load("abcdefghijk mnopq", "relative", true), KEYBAD);          /* class before absolute */
    expect_fail(t, load("ab zq9-0123456789", "/ab zq9-0123456789", true), KEYBAD); /* class before key-in-path */
    expect_fail(t, load(KA, "zq9-0123456789ab/a", true), "ANNAS_DOWNLOAD_PATH must be an absolute path, got: REDACTED/a");
    memset(g_dir, 'a', 257); /* a 257-byte path that contains the key: the length message first */
    memcpy(g_dir, "/" KA, 17);
    g_dir[257] = '\0';
    expect_fail(t, load(KA, g_dir, true), DIRLONG);
    expect_fail(t, load(KA, "", true), MISSING);
    memset(g_key, 'a', 257); /* key too long and a path too long: the key first */
    g_key[257] = '\0';
    expect_fail(t, load(g_key, g_dir, true), KEYLONG);
}

/* ---- the operations: the same checks on the key and directory of a context ------------------------------------- */

static annas_code_t both(kit_t *k, bool download) {
    annas_request_t r = kit_req(KIT_MD5);
    return download ? kit_download(k, &r) : kit_fast(k, &r);
}

static void t_ops_refuse_a_key_outside_the_class_before_any_port_call(ctest_t *t) {
    for (unsigned b = 1; b < 256u; b++) {
        if (in_class(b)) {
            continue;
        }
        char key[18] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', (char)b, 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 0};
        for (unsigned d = 0; d < 2u; d++) {
            kit_t *k = kit_begin();
            kit_set_key(k, key);
            kit_check_err(t, k, both(k, d != 0), ANNAS_E_CONFIG, 0, KEYBAD);
            kit_check_fs_untouched(t, k);
        }
    }
    kit_t *k = kit_begin();
    kit_set_key(k, "abcdefghijkl%1ecd");
    kit_check_err(t, k, both(k, false), ANNAS_E_CONFIG, 0, KEYBAD);
    k = kit_begin();
    CHECK(t, kit_prepare(k));
    k->cfg.key = (core_str_t){"abcdefghijklmn\0o", 16}; /* a NUL byte inside the key */
    kit_check_err(t, k, both(k, false), ANNAS_E_CONFIG, 0, KEYBAD);
    k = kit_begin();
    CHECK(t, kit_prepare(k));
    k->cfg.key = (core_str_t){"abcdefghijklmn\0o", 16};
    kit_check_err(t, k, both(k, true), ANNAS_E_CONFIG, 0, KEYBAD);
}

static void t_ops_refuse_a_key_of_15_bytes_or_less_before_any_port_call(ctest_t *t) {
    for (size_t n = 1; n <= 15u; n++) {
        memset(g_key, 'a', n);
        g_key[n] = '\0';
        for (unsigned d = 0; d < 2u; d++) {
            kit_t *k = kit_begin();
            kit_set_key(k, g_key);
            kit_check_err(t, k, both(k, d != 0), ANNAS_E_CONFIG, 0, KEYSHORT);
            kit_check_fs_untouched(t, k);
        }
    }
    kit_t *k = kit_begin(); /* too short wins over a bad byte and over a bad md5 */
    kit_set_key(k, "a b");
    annas_request_t r = kit_req("not-an-md5");
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_CONFIG, 0, KEYSHORT);
    k = kit_begin();
    kit_set_key(k, "aaaaaaaaaaaaaaa"); /* 15 bytes, with the dir holding it */
    kit_set_dir(k, "/aaaaaaaaaaaaaaa");
    kit_check_err(t, k, both(k, true), ANNAS_E_CONFIG, 0, KEYSHORT);
}

static void run_fast_ok(ctest_t *t, const char *key, size_t len);

static void t_ops_refuse_a_key_containing_redacted_before_any_port_call(ctest_t *t) {
    for (size_t i = 0; i < COUNT(g_red); i++) {
        for (unsigned d = 0; d < 2u; d++) {
            kit_t *k = kit_begin();
            kit_set_key(k, g_red[i]);
            kit_check_err(t, k, both(k, d != 0), ANNAS_E_CONFIG, 0, KEYRED);
            kit_check_fs_untouched(t, k);
        }
    }
    kit_t *k = kit_begin(); /* before the dir check */
    kit_set_key(k, g_red[0]);
    kit_set_dir(k, "/REDACTEDREDACTED");
    kit_check_err(t, k, both(k, true), ANNAS_E_CONFIG, 0, KEYRED);
    for (size_t i = 0; i < COUNT(g_red_ok); i++) {
        run_fast_ok(t, g_red_ok[i], strlen(g_red_ok[i]));
    }
}

static void t_ops_config_comes_before_request_validation(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_set_key(k, "abcdefghijk mnopq");
    annas_request_t r = kit_req("not-an-md5");
    kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_CONFIG, 0, KEYBAD);
    k = kit_begin();
    kit_set_key(k, "abcdefghijk mnopq");
    r = kit_req("not-an-md5");
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_CONFIG, 0, KEYBAD);
}

static void t_ops_refuse_a_key_longer_than_the_limit(ctest_t *t) {
    static char big[260];
    memset(big, 'a', 257);
    for (unsigned d = 0; d < 2u; d++) {
        kit_t *k = kit_begin();
        CHECK(t, kit_prepare(k));
        k->cfg.key = (core_str_t){big, 257};
        kit_check_err(t, k, both(k, d != 0), ANNAS_E_CONFIG, 0, KEYLONG);
    }
}

static void t_ops_refuse_a_dir_containing_the_key(ctest_t *t) {
    static const pair_t c[] = {{K7, "/" K7}, {KA, "/dl/" KA}, {KA, "/" KA "/b"}, {KA, "/a/" KA KA}, {KQ, "/" KQ "zq"}};
    for (size_t i = 0; i < COUNT(c); i++) {
        kit_t *k = kit_begin();
        kit_set_key(k, c[i].key);
        kit_set_dir(k, c[i].dir);
        kit_check_err(t, k, both(k, true), ANNAS_E_CONFIG, 0, DIRKEY);
        kit_check_fs_untouched(t, k);
    }
    kit_t *k = kit_begin(); /* the key class comes first; the dir length comes before the key check */
    kit_set_key(k, "zq 9-0123456789ab");
    kit_set_dir(k, "/zq 9-0123456789ab");
    kit_check_err(t, k, both(k, true), ANNAS_E_CONFIG, 0, KEYBAD);
    k = kit_begin();
    memset(g_dir, 'a', 257);
    memcpy(g_dir, "/" KA, 17);
    g_dir[257] = '\0';
    kit_set_key(k, KA);
    kit_set_dir(k, g_dir);
    kit_check_err(t, k, both(k, true), ANNAS_E_CONFIG, 0, DIRLONG);
}

static void run_fast_ok(ctest_t *t, const char *key, size_t len) {
    kit_t *k = kit_begin();
    kit_connect(k, k->cfg.base, 443);
    kit_expect(k, KIT_FMT(k,
                          "GET /dyn/api/fast_download.json?md5=%s&key=%s HTTP/1.1\r\nHost: %s\r\n"
                          "User-Agent: annas-c/0.1\r\nAccept: */*\r\nConnection: close\r\n\r\n",
                          KIT_MD5, key, k->cfg.base));
    kit_respond(k, kit_http(k, 200, "OK", "", kit_ok_json(k, "https://dl.example.org/f", NULL)), 0);
    CHECK(t, kit_prepare(k));
    k->cfg.key = (core_str_t){key, len};
    kit_check_ok(t, k, both(k, false));
}

static void t_ops_accept_keys_at_the_limit_and_the_whole_class(ctest_t *t) {
    size_t n = 0;
    for (unsigned b = 1; b < 256u; b++) {
        if (in_class(b)) {
            g_all[n++] = (char)b;
        }
    }
    g_all[n] = '\0';
    run_fast_ok(t, g_all, n); /* all 66 bytes of the class, sent as they are */
    memset(g_key, 'q', 255);
    g_key[255] = '~'; /* ANNAS_KEY_MAX bytes */
    g_key[256] = '\0';
    run_fast_ok(t, g_key, 256);
    run_fast_ok(t, K7, 16); /* the shortest key */
    kit_t *k = kit_begin(); /* a dir that holds a near miss of the key is fine for a download */
    kit_set_key(k, KA);
    kit_set_dir(k, "/zq9-0123456789a/b");
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, "https://dl.example.org/f", NULL)));
    kit_get(k, "dl.example.org", 443, "/f", kit_http(k, 200, "OK", "", "d"));
    kit_check_ok(t, k, both(k, true));
    k = kit_begin(); /* the fast call never looks at the dir */
    kit_set_key(k, KA);
    kit_set_dir(k, "/" KA);
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, "https://dl.example.org/f", NULL)));
    kit_check_ok(t, k, both(k, false));
}

CTEST_SUITE("key_config",
    {"config_every_byte_outside_the_key_class_is_refused", t_config_every_byte_outside_the_key_class_is_refused},
    {"config_lower_case_percent_forms_are_refused", t_config_lower_case_percent_forms_are_refused},
    {"config_the_whole_class_is_accepted", t_config_the_whole_class_is_accepted},
    {"config_the_length_limits_are_16_and_256", t_config_the_length_limits_are_16_and_256},
    {"config_key_class_is_checked_only_when_a_download_needs_it", t_config_key_class_is_checked_only_when_a_download_needs_it},
    {"config_a_path_containing_the_key_is_refused", t_config_a_path_containing_the_key_is_refused},
    {"config_near_misses_in_the_path_are_accepted", t_config_near_misses_in_the_path_are_accepted},
    {"config_precedence_of_the_key_and_path_checks", t_config_precedence_of_the_key_and_path_checks},
    {"ops_refuse_a_key_outside_the_class_before_any_port_call", t_ops_refuse_a_key_outside_the_class_before_any_port_call},
    {"ops_refuse_a_key_of_15_bytes_or_less_before_any_port_call", t_ops_refuse_a_key_of_15_bytes_or_less_before_any_port_call},
    {"config_a_key_of_15_bytes_or_less_is_refused", t_config_a_key_of_15_bytes_or_less_is_refused},
    {"config_a_key_containing_redacted_is_refused", t_config_a_key_containing_redacted_is_refused},
    {"ops_refuse_a_key_containing_redacted_before_any_port_call", t_ops_refuse_a_key_containing_redacted_before_any_port_call},
    {"ops_config_comes_before_request_validation", t_ops_config_comes_before_request_validation},
    {"ops_refuse_a_key_longer_than_the_limit", t_ops_refuse_a_key_longer_than_the_limit},
    {"ops_refuse_a_dir_containing_the_key", t_ops_refuse_a_dir_containing_the_key},
    {"ops_accept_keys_at_the_limit_and_the_whole_class", t_ops_accept_keys_at_the_limit_and_the_whole_class})
