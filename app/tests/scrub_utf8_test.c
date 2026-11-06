/* (1) The key scrubbing is TWO SEQUENTIAL PASSES (app.h "Key secrecy"): the raw key over the text, then the
 *     percent-encoded key over the output of the first pass. Every case where this differs from a single combined
 *     scan has its controls (the plain forms that both readings handle the same way).
 * (2) MCP texts are made valid UTF-8 before the JSON escaping: an ill-formed byte becomes one '?'; the CLI writes
 *     the bytes verbatim. Every rejected sequence has well-formed neighbours as positive controls. */
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/tests/kit.h"
#include "ctest/ctest.h"

#define MD5 AK_MD5
#define JSON_HDR "Content-Type: application/json\r\n"
#define HASH "\"hash\":\"" MD5 "\""

static char g_line[1024];
static char g_exp[2048];
static char g_name[512];
static char g_args[512];
static char nm[512];
static char shw[512];
static char cli_want[1024];
static char want_q[256];

/* ---- (1) scrubbing ------------------------------------------------------------------------------------------- */

static void ping_with_id(ctest_t *t, ak_t *a, const char *id, const char *want_id) {
    (void)snprintf(g_line, sizeof g_line, "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"method\":\"ping\"}", id);
    (void)snprintf(want_q, sizeof want_q, "\"%s\"", want_id);
    (void)snprintf(g_exp, sizeof g_exp, AK_OKR("%s", "{}"), want_q);
    ak_check_reply(t, a, ak_mcp(a, g_line), g_exp);
}

static ak_t *begin_with_key(const char *key) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_SECRET_KEY", key);
    kit_set_key(a->k, key);
    return a;
}

/* Key "%20 " (its encoded form is "%2520%20"): the encoded key overlaps a raw occurrence in the last case. */
static void t_encoded_key_overlapping_a_raw_occurrence(ctest_t *t) {
    ak_t *a = begin_with_key("%20 ");
    CHECK_EQ_STR(t, a->k->key_pct, "%2520%20");
    ping_with_id(t, a, "%20 ", "REDACTED");               /* control: raw only */
    ping_with_id(t, a, "%2520%20", "REDACTED");           /* control: encoded only */
    ping_with_id(t, a, "a%20 b", "aREDACTEDb");           /* control: raw inside text */
    ping_with_id(t, a, "a%2520%20b", "aREDACTEDb");       /* control: encoded inside text */
    ping_with_id(t, a, "%2520%20 ", "%2520REDACTED");     /* pass 1 takes the raw "%20 " at 5; the encoded key is gone */
    ping_with_id(t, a, "z%2520%20 z", "z%2520REDACTEDz");
    ping_with_id(t, a, "%2520%20 %2520%20", "%2520REDACTEDREDACTED"); /* pass 2 finds the one that is left */
    ping_with_id(t, a, "%2520%2", "%2520%2");             /* control: near miss of both forms */
    ping_with_id(t, a, "%20", "%20");
    ak_check_no_io(t, a);
}

/* Key "REDACTED " (encoded "REDACTED%20"): what pass 1 writes can combine with the following bytes into the encoded
 * key, and pass 2 reads the output of pass 1. */
static void t_pass_two_reads_the_output_of_pass_one(ctest_t *t) {
    ak_t *a = begin_with_key("REDACTED ");
    CHECK_EQ_STR(t, a->k->key_pct, "REDACTED%20");
    ping_with_id(t, a, "REDACTED %20", "REDACTED");       /* "REDACTED" + "%20" is the encoded key after pass 1 */
    ping_with_id(t, a, "xREDACTED %20y", "xREDACTEDy");
    ping_with_id(t, a, "REDACTED%20", "REDACTED");        /* control: encoded form directly */
    ping_with_id(t, a, "REDACTED ", "REDACTED");          /* control: raw form directly */
    ping_with_id(t, a, "REDACTED", "REDACTED");           /* control: the word alone is not the key */
    ping_with_id(t, a, "REDACTED %2", "REDACTED%2");      /* control: pass 2 needs the whole encoded key */
    ak_check_no_io(t, a);
}

static void t_occurrences_do_not_overlap_and_run_left_to_right(ctest_t *t) {
    ak_t *a = begin_with_key("aa");
    CHECK_EQ_STR(t, a->k->key_pct, "aa"); /* encoded == raw: a single pass */
    ping_with_id(t, a, "aa", "REDACTED");
    ping_with_id(t, a, "aaa", "REDACTEDa");
    ping_with_id(t, a, "aaaa", "REDACTEDREDACTED");
    ping_with_id(t, a, "baaab", "bREDACTEDab");
    ping_with_id(t, a, "a", "a");
    ak_check_no_io(t, a);
    a = begin_with_key("a a"); /* the encoded form differs ("a%20a"); pass 1 is left to right and non-overlapping */
    ping_with_id(t, a, "a a a", "REDACTED a");
    ping_with_id(t, a, "a a a a", "REDACTED REDACTED");
    ping_with_id(t, a, "a%20a", "REDACTED");
    ping_with_id(t, a, "a%20a%20a", "REDACTED%20a");     /* pass 2 consumes "a%20a", leaving "%20a" */
    ak_check_no_io(t, a);
}

/* ---- (2) UTF-8 ------------------------------------------------------------------------------------------------- */

static app_mcp_result_t call(ak_t *a, const char *tool, const char *args) {
    (void)snprintf(g_line, sizeof g_line,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}",
                   tool, args);
    return ak_mcp(a, g_line);
}

static void check_tool_text(ctest_t *t, ak_t *a, app_mcp_result_t r, const char *text, bool is_error) {
    (void)snprintf(g_exp, sizeof g_exp,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"%s\"}],\"isError\":%s}}",
                   text, is_error ? "true" : "false");
    ak_check_reply(t, a, r, g_exp);
}

typedef struct {
    const char *body; /* the text of an HTTP 500 response (not JSON) */
    const char *want; /* what the MCP tool text carries after "annas-c: E_API: 500: " */
} utf8_case_t;

static const utf8_case_t k_bad[] = {
    {"x\xff" "y", "x?y"},                       /* 0xFF is never UTF-8 */
    {"x\xfe\xff" "y", "x??y"},
    {"x\x80" "y", "x?y"},                       /* a stray continuation byte */
    {"x\xbf" "y", "x?y"},
    {"x\xc0\x80" "y", "x??y"},                  /* overlong NUL */
    {"x\xc1\xbf" "y", "x??y"},                  /* overlong 2-byte */
    {"x\xe0\x9f\xbf" "y", "x???y"},             /* overlong 3-byte */
    {"x\xf0\x8f\xbf\xbf" "y", "x????y"},        /* overlong 4-byte */
    {"x\xed\xa0\x80" "y", "x???y"},             /* U+D800, a surrogate */
    {"x\xed\xbf\xbf" "y", "x???y"},             /* U+DFFF */
    {"x\xf4\x90\x80\x80" "y", "x????y"},        /* U+110000 */
    {"x\xf5\x80\x80\x80" "y", "x????y"},        /* 0xF5 never starts a sequence */
    {"x\xc3" "y", "x?y"},                       /* truncated 2-byte, then ASCII */
    {"x\xe2\x82" "y", "x??y"},                  /* truncated 3-byte */
    {"x\xf0\x9f\x98" "y", "x???y"},             /* truncated 4-byte */
    {"x\xe2\x82\xe2\x82\xac", "x??\xe2\x82\xac"}, /* a truncated sequence directly before a good one */
    {"x\xc3", "x?"},                            /* truncated at the very end of the text */
    {"x\xe2\x82", "x??"},
    {"x\xc3\xc3\xa9", "x?\xc3\xa9"},            /* the bad byte is not swallowed into the next sequence */
    {"x\xc3\xa9\xa9", "x\xc3\xa9?"}};

static const char *const k_good[] = {
    "x\xc3\xa9y",             /* U+00E9 */
    "x\xc2\x80y",             /* U+0080, the smallest 2-byte */
    "x\xdf\xbfy",             /* U+07FF, the largest 2-byte */
    "x\xe0\xa0\x80y",         /* U+0800, the smallest 3-byte */
    "x\xe2\x82\xacy",         /* U+20AC */
    "x\xed\x9f\xbfy",         /* U+D7FF, just below the surrogates */
    "x\xee\x80\x80y",         /* U+E000, just above them */
    "x\xef\xbf\xbfy",         /* U+FFFF */
    "x\xf0\x90\x80\x80y",     /* U+10000, the smallest 4-byte */
    "x\xf0\x9f\x98\x80y",     /* U+1F600 */
    "x\xf4\x8f\xbf\xbfy"};    /* U+10FFFF, the largest */

static void script_500(ak_t *a, const char *body) {
    kit_api(a->k, MD5, "", kit_http(a->k, 500, "Error", "", body));
}

static void t_mcp_server_text_ill_formed_bytes_become_question_marks(ctest_t *t) {
    for (size_t i = 0; i < sizeof k_bad / sizeof k_bad[0]; i++) {
        ak_t *a = ak_begin();
        script_500(a, k_bad[i].body);
        (void)snprintf(g_name, sizeof g_name, "annas-c: E_API: 500: %s", k_bad[i].want);
        check_tool_text(t, a, call(a, "fast_download_url", "{" HASH "}"), g_name, true);
        ak_check_stream(t, a);
    }
}

static void t_mcp_server_text_well_formed_sequences_are_kept(ctest_t *t) {
    for (size_t i = 0; i < sizeof k_good / sizeof k_good[0]; i++) {
        ak_t *a = ak_begin();
        script_500(a, k_good[i]);
        (void)snprintf(g_name, sizeof g_name, "annas-c: E_API: 500: %s", k_good[i]);
        check_tool_text(t, a, call(a, "book_download", "{" HASH "}"), g_name, true);
        ak_check_stream(t, a);
    }
}

static void t_cli_writes_the_bytes_verbatim(ctest_t *t) {
    for (size_t i = 0; i < sizeof k_bad / sizeof k_bad[0]; i++) {
        ak_t *a = ak_begin();
        script_500(a, k_bad[i].body);
        (void)snprintf(g_name, sizeof g_name, "annas-c: E_API: 500: %s\n", k_bad[i].body);
        int rc = AK_CLI(a, "download-url", MD5);
        ak_check_cli(t, a, rc, 4, "", g_name);
        ak_check_stream(t, a);
    }
}

static void t_mcp_environment_text_is_cleaned_too(ctest_t *t) {
    ak_t *a = ak_begin();
    ak_env(a, "ANNAS_BASE_URL", "bad\xff");
    check_tool_text(t, a, call(a, "fast_download_url", "{" HASH "}"),
                    "annas-c: E_CONFIG: ANNAS_BASE_URL is not a valid domain, got: bad?", true);
    ak_check_no_io(t, a);
    a = ak_begin();
    ak_env(a, "ANNAS_BASE_URL", "bad\xff");
    int rc = AK_CLI(a, "download-url", MD5);
    ak_check_cli(t, a, rc, 2, "", "annas-c: E_CONFIG: ANNAS_BASE_URL is not a valid domain, got: bad\xff\n");
    a = ak_begin(); /* a value cut by annas to 200 bytes in the middle of a sequence */
    size_t n = 0;
    g_name[n++] = 'r';
    for (int i = 0; i < 120; i++) {
        g_name[n++] = (char)0xc3;
        g_name[n++] = (char)0xa9;
    }
    g_name[n] = '\0';
    ak_env(a, "ANNAS_DOWNLOAD_PATH", g_name);
    n = (size_t)snprintf(g_exp, sizeof g_exp, "annas-c: E_CONFIG: ANNAS_DOWNLOAD_PATH must be an absolute path, got: r");
    for (int i = 0; i < 99; i++) {
        g_exp[n++] = (char)0xc3;
        g_exp[n++] = (char)0xa9;
    }
    g_exp[n] = '\0';
    (void)snprintf(cli_want, sizeof cli_want, "%s\xc3\n", g_exp); /* verbatim: the cut sequence stays cut */
    rc = AK_CLI(a, "download", MD5);
    ak_check_cli(t, a, rc, 2, "", cli_want);
    a = ak_begin();
    ak_env(a, "ANNAS_DOWNLOAD_PATH", g_name);
    (void)snprintf(g_args, sizeof g_args, "%s?", g_exp); /* g_name is the environment value: keep it */
    check_tool_text(t, a, call(a, "book_download", "{" HASH "}"), g_args, true);
    ak_check_no_io(t, a);
}

/* <name> is cut to 100 bytes and then made valid UTF-8. */
static void unknown_name_reply(ctest_t *t, ak_t *a, app_mcp_result_t r, const char *what, const char *shown) {
    (void)snprintf(g_exp, sizeof g_exp,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32602,\"message\":\"Invalid params: unknown %s: %s\"}}",
                   what, shown);
    ak_check_reply(t, a, r, g_exp);
    ak_check_no_io(t, a);
}

static void repeat(char *d, size_t *n, const char *unit, int times) {
    size_t u = strlen(unit);
    for (int i = 0; i < times; i++) {
        memcpy(d + *n, unit, u);
        *n += u;
    }
    d[*n] = '\0';
}

static void t_mcp_cut_names_are_made_valid(ctest_t *t) {
    size_t n = 0;
    size_t m = 0;
    ak_t *a = ak_begin();
    nm[0] = 'a'; /* 'a' + 60 x U+00E9 = 121 bytes; the cut at 100 splits the 50th sequence */
    n = 1;
    repeat(nm, &n, "\xc3\xa9", 60);
    shw[0] = 'a';
    m = 1;
    repeat(shw, &m, "\xc3\xa9", 49);
    shw[m++] = '?';
    shw[m] = '\0';
    unknown_name_reply(t, a, call(a, nm, "{}"), "tool", shw);
    (void)snprintf(g_args, sizeof g_args, "{\"%s\":1}", nm);
    unknown_name_reply(t, a, call(a, "book_download", g_args), "argument", shw);
    n = 0; /* control: 60 x U+00E9 = 120 bytes; the cut falls between two sequences */
    repeat(nm, &n, "\xc3\xa9", 60);
    m = 0;
    repeat(shw, &m, "\xc3\xa9", 50);
    unknown_name_reply(t, a, call(a, nm, "{}"), "tool", shw);
    (void)snprintf(g_args, sizeof g_args, "{\"%s\":1}", nm);
    unknown_name_reply(t, a, call(a, "fast_download_url", g_args), "argument", shw);
    nm[0] = 'a'; /* 'a' + 30 x U+1F600: 97 bytes, then 3 of the 4 bytes of the 25th emoji */
    n = 1;
    repeat(nm, &n, "\xf0\x9f\x98\x80", 30);
    shw[0] = 'a';
    m = 1;
    repeat(shw, &m, "\xf0\x9f\x98\x80", 24);
    repeat(shw, &m, "?", 3);
    unknown_name_reply(t, a, call(a, nm, "{}"), "tool", shw);
    n = 0; /* control: 30 emoji = 120 bytes, the cut at 100 is exactly after the 25th */
    repeat(nm, &n, "\xf0\x9f\x98\x80", 30);
    m = 0;
    repeat(shw, &m, "\xf0\x9f\x98\x80", 25);
    unknown_name_reply(t, a, call(a, nm, "{}"), "tool", shw);
}

CTEST_SUITE("scrub_utf8",
    {"encoded_key_overlapping_a_raw_occurrence", t_encoded_key_overlapping_a_raw_occurrence},
    {"pass_two_reads_the_output_of_pass_one", t_pass_two_reads_the_output_of_pass_one},
    {"occurrences_do_not_overlap_and_run_left_to_right", t_occurrences_do_not_overlap_and_run_left_to_right},
    {"mcp_server_text_ill_formed_bytes_become_question_marks", t_mcp_server_text_ill_formed_bytes_become_question_marks},
    {"mcp_server_text_well_formed_sequences_are_kept", t_mcp_server_text_well_formed_sequences_are_kept},
    {"cli_writes_the_bytes_verbatim", t_cli_writes_the_bytes_verbatim},
    {"mcp_environment_text_is_cleaned_too", t_mcp_environment_text_is_cleaned_too},
    {"mcp_cut_names_are_made_valid", t_mcp_cut_names_are_made_valid})
