/* Redaction character-class edges and download-format edges (R004 T2): percent-encoded echoes of keys made of bytes at
 * and next to every unreserved-class edge, one-hex-digit near misses, 1-byte keys, a key at the start of the server
 * text, and the accepted/refused format characters. Fake keys only. */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define K16 "kkkkkkkkkkkkkkkk"
#define FORMAT_MSG "format must consist of 1 to 16 ASCII letters or digits"

static char g_key[24];
static char g_body[200];
static char g_want[200];

/* Server error text `text` answered with `status`; returns the annas result. */
static annas_code_t api_error(kit_t *k, int status, const char *text) {
    snprintf(g_body, sizeof g_body, "{\"error\":\"%s\"}", text);
    kit_api(k, KIT_MD5, "", kit_http(k, status, "X", "", g_body));
    annas_request_t r = kit_req(KIT_MD5);
    return kit_fast(k, &r);
}

static void expect_message(ctest_t *t, const char *key, const char *echo, const char *want_text) {
    kit_t *k = kit_begin();
    kit_set_key(k, key);
    static char full[220];
    snprintf(full, sizeof full, "400: %s", want_text);
    kit_check_err(t, k, api_error(k, 400, echo), ANNAS_E_API, 400, full);
}

static void t_one_byte_key(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_set_key(k, K16); /* the shortest key, 16 bytes */
    kit_check_err(t, k, api_error(k, 401, K16), ANNAS_E_API, 401, "401: REDACTED");
    k = kit_begin();
    kit_set_key(k, K16);
    kit_check_err(t, k, api_error(k, 401, K16 K16 " " K16 "k"), ANNAS_E_API, 401, "401: REDACTEDREDACTED REDACTEDk");
}

static void t_key_at_the_start_of_the_server_text(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_check_err(t, k, api_error(k, 401, KIT_KEY " rest"), ANNAS_E_API, 401, "401: REDACTED rest");
    k = kit_begin();
    kit_check_err(t, k, api_error(k, 401, KIT_KEY), ANNAS_E_API, 401, "401: REDACTED");
}

/* The key is the byte c, a space and "k"; the server echoes the key byte-wise as the library encodes it. */
static const struct {
    char c;
    const char *pct;      /* c percent-encoded */
    const char *low;      /* last hex digit changed */
    const char *high;     /* first hex digit changed */
} g_outside[] = {
    {'@', "%40", "%41", "%50"}, {'[', "%5B", "%5C", "%4B"}, {'`', "%60", "%61", "%70"},
    {'{', "%7B", "%7C", "%6B"}, {'/', "%2F", "%2E", "%3F"}, {':', "%3A", "%3B", "%2A"},
};

/* The key is "Qk-0123456789abc" (in the key class); the bytes next to the class are never part of a key, so an escape of one of
 * them in the server text is left alone, whole or next to the key, and cannot split a key match. */
static void t_percent_echo_of_bytes_next_to_the_unreserved_classes(ctest_t *t) {
    char echo[32];
    for (size_t i = 0; i < COUNT(g_outside); i++) {
        snprintf(echo, sizeof echo, "x %sQk-0123456789abc y", g_outside[i].pct);
        snprintf(g_want, sizeof g_want, "x %sREDACTED y", g_outside[i].pct);
        expect_message(t, "Qk-0123456789abc", echo, g_want);
        snprintf(echo, sizeof echo, "x Q%sk-0123456789abc y", g_outside[i].pct); /* an escape inside: not the key */
        snprintf(g_want, sizeof g_want, "x Q%sk-0123456789abc y", g_outside[i].pct);
        expect_message(t, "Qk-0123456789abc", echo, g_want);
        snprintf(echo, sizeof echo, "x %s%s y", g_outside[i].low, g_outside[i].high);
        snprintf(g_want, sizeof g_want, "x %s%s y", g_outside[i].low, g_outside[i].high);
        expect_message(t, "Qk-0123456789abc", echo, g_want);
    }
}

/* The key is "q-1_0123456789ab": pct_encode(key) == key, so an escaped spelling of its bytes ("%71" is 'q', "%2D" is '-', either
 * hex case) is NOT a form of the key and is left alone; only the raw bytes are redacted. */
static void t_percent_echo_with_one_wrong_hex_digit_is_left_alone(ctest_t *t) {
    static const char *const near[] = {"%71-1_0123456789ab", "%71%2D%31_0123456789ab", "%71%2d1_0123456789ab",
                                       "q%2D1_0123456789ab", "q-%31_0123456789ab", "q-1_0123456789a", "%2C", "%2", "%"};
    char echo[32];
    for (size_t i = 0; i < COUNT(near); i++) {
        snprintf(echo, sizeof echo, "p %s z", near[i]);
        snprintf(g_want, sizeof g_want, "p %s z", near[i]);
        expect_message(t, "q-1_0123456789ab", echo, g_want);
    }
    expect_message(t, "q-1_0123456789ab", "p q-1_0123456789ab z", "p REDACTED z");
    expect_message(t, "q-1_0123456789ab", "p q-1_0123456789ab%71-1 z", "p REDACTED%71-1 z");
}

static void t_unreserved_edge_bytes_are_echoed_raw(ctest_t *t) {
    static const char edges[] = {'A', 'Z', 'a', 'z', '0', '9', '-', '.', '_', '~'};
    char echo[32];
    for (size_t i = 0; i < COUNT(edges); i++) {
        snprintf(g_key, sizeof g_key, "%cmmmmmmmmmmmmmmm", edges[i]); /* the class edge, then letters: sent and echoed raw */
        snprintf(echo, sizeof echo, "x %cmmmmmmmmmmmmmmm y", edges[i]);
        expect_message(t, g_key, echo, "x REDACTED y");
        snprintf(g_key, sizeof g_key, "mmmmmmmmmmmmmmm%c", edges[i]);
        snprintf(echo, sizeof echo, "x mmmmmmmmmmmmmmm%c y", edges[i]);
        expect_message(t, g_key, echo, "x REDACTED y");
    }
}

static void t_key_at_the_end_of_the_server_text(ctest_t *t) {
    expect_message(t, K16, "tail " K16, "tail REDACTED");
    expect_message(t, "q-1_0123456789ab", "tail %71-1_0123456789ab", "tail %71-1_0123456789ab");
    expect_message(t, "q-1_0123456789ab", "tail q-1_0123456789ab", "tail REDACTED");
    expect_message(t, "q.b_0123456789abc", "tail q.b_0123456789abc", "tail REDACTED");
    expect_message(t, "q-1_0123456789ab", "tail q-1_0123456789a", "tail q-1_0123456789a");
    expect_message(t, "q-1_0123456789ab", "tail %2", "tail %2");
    expect_message(t, KIT_KEY, "tail " KIT_KEY, "tail REDACTED");
}

static void t_download_url_starting_with_the_key_is_refused(ctest_t *t) {
    static const char *const urls[] = {KIT_KEY, KIT_KEY "/f", K16, K16 "/f"};
    for (size_t i = 0; i < COUNT(urls); i++) {
        kit_t *k = kit_begin();
        kit_set_key(k, i < 2 ? KIT_KEY : K16);
        snprintf(g_body, sizeof g_body, "{\"download_url\":\"%s\"}", urls[i]);
        kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", g_body));
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_err(t, k, kit_fast(k, &r), ANNAS_E_PARSE, 0,
                      "download URL contains the secret key");
    }
}

/* download of `format` against a good server; the file must be <md5>.<want_ext>. */
static void download_with(ctest_t *t, const char *format, const char *want_file) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, "https://dl.example.org/f/1", "{\"downloads_left\":2}")));
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1");
    kit_respond(k, kit_head(k, 200, "OK", "Content-Type: application/pdf\r\n", 4), 0);
    kit_respond(k, "data", 0);
    annas_request_t r = kit_req(KIT_MD5);
    r.format = core_str_from_cstr(format);
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, want_file, "data", 4);
}

static void t_format_edge_characters_are_accepted(ctest_t *t) {
    download_with(t, "a", KIT_MD5 ".a");
    download_with(t, "z", KIT_MD5 ".z");
    download_with(t, "A", KIT_MD5 ".a");
    download_with(t, "Z", KIT_MD5 ".z");
    download_with(t, "0", KIT_MD5 ".0");
    download_with(t, "9", KIT_MD5 ".9");
}

static void t_format_characters_next_to_the_classes_are_refused(ctest_t *t) {
    static const char *const bad[] = {"`", "{", "@", "[", "/", ":", "a`", "z{", "0/", "9:", "A@", "Z["};
    for (size_t i = 0; i < COUNT(bad); i++) {
        kit_t *k = kit_begin();
        annas_request_t r = kit_req(KIT_MD5);
        r.format = core_str_from_cstr(bad[i]);
        kit_check_err(t, k, kit_download(k, &r), ANNAS_E_ARG, 0, FORMAT_MSG);
    }
}

CTEST_SUITE("edge",
    {"one_byte_key", t_one_byte_key},
    {"key_at_the_start_of_the_server_text", t_key_at_the_start_of_the_server_text},
    {"key_at_the_end_of_the_server_text", t_key_at_the_end_of_the_server_text},
    {"download_url_starting_with_the_key_is_refused", t_download_url_starting_with_the_key_is_refused},
    {"percent_echo_of_bytes_next_to_the_unreserved_classes", t_percent_echo_of_bytes_next_to_the_unreserved_classes},
    {"percent_echo_with_one_wrong_hex_digit_is_left_alone", t_percent_echo_with_one_wrong_hex_digit_is_left_alone},
    {"unreserved_edge_bytes_are_echoed_raw", t_unreserved_edge_bytes_are_echoed_raw},
    {"format_edge_characters_are_accepted", t_format_edge_characters_are_accepted},
    {"format_characters_next_to_the_classes_are_refused", t_format_characters_next_to_the_classes_are_refused})
