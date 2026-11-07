/* The member key in a download: a title that contains it (annas.h, Messages: TITLE) and a redirect whose target
 * contains it (REDIRECT). Fake keys only. */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

#define ZK "zq9-0123456789ab" /* fake keys of 16 bytes or more */
#define K7 "7777777777777777"
#define KA16 "aaaaaaaaaaaaaaaa"
#define KAB "abababababababab"
#define AK "AbCdEfGhIjKlMnOp"
#define KQ "zqzqzqzqzqzqzqzq"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define BROWSER "the site requires browser verification; use download-by-md5 with a hash found in a browser"
#define REDIR "redirect contains the key"
#define DLURL "https://a.example.org/x"

static char g_title[600], g_name[400], g_loc[400], g_hdr[500];

static void api_ok(kit_t *k, const char *url) {
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, url, "{\"downloads_left\":2}")));
}

/* ---- the title ------------------------------------------------------------------------------------------------- */

/* Downloads "data" with `title`, format pdf, under `key`; the committed name is exactly `name`, and neither the
 * result nor its text holds the key. */
static void title_case(ctest_t *t, const char *key, const char *title, const char *name) {
    static char text[1024];
    kit_t *k = kit_begin();
    kit_set_key(k, key);
    api_ok(k, DLURL);
    kit_get(k, "a.example.org", 443, "/x", kit_http(k, 200, "OK", "", "data"));
    annas_request_t r = kit_req(KIT_MD5);
    r.title = core_str_from_cstr(title);
    r.format = core_str_from_cstr("pdf");
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, name, "data", 4);
    CHECK_EQ_STR(t, k->dl.name, name);
    CHECK_EQ_U64(t, k->dl.name_len, strlen(name));
    snprintf(g_name, sizeof g_name, "/dl/%s", name);
    CHECK_EQ_STR(t, k->dl.path, g_name);
    CHECK(t, !kit_contains(k->dl.name, key));
    CHECK(t, !kit_contains(k->dl.path, key));
    core_buf_t b;
    CHECK(t, core_buf_init(&b, text, sizeof text));
    CHECK(t, annas_format_downloaded(&b, &k->dl));
    CHECK(t, !kit_contains(core_buf_cstr(&b), key));
}

typedef struct {
    const char *key;
    const char *title;
    const char *name;
} tcase_t;

static void t_title_containing_the_key_is_redacted(ctest_t *t) {
    static const tcase_t c[] = {
        {ZK, ZK, "REDACTED.pdf"},
        {ZK, ZK " book", "REDACTED book.pdf"},
        {ZK, "my " ZK, "my REDACTED.pdf"},
        {ZK, "my " ZK " book", "my REDACTED book.pdf"},
        {ZK, ZK ZK, "REDACTEDREDACTED.pdf"},
        {ZK, ZK " and " ZK, "REDACTED and REDACTED.pdf"},
        {ZK, "a" ZK "b", "aREDACTEDb.pdf"},
        {ZK, ZK "zq", "REDACTEDzq.pdf"},
        {ZK, "zq" ZK, "zqREDACTED.pdf"},
        {ZK, ZK "/" ZK, "REDACTED_REDACTED.pdf"},
        {ZK, "zq9-0123456789a", "zq9-0123456789a.pdf"},
        {ZK, "ZQ9-0123456789AB zq 9 z-q9", "ZQ9-0123456789AB zq 9 z-q9.pdf"},
        {K7, K7, "REDACTED.pdf"},
        {K7, "a" K7 "b" K7 "c", "aREDACTEDbREDACTEDc.pdf"},
        {K7, K7 K7 K7, "REDACTEDREDACTEDREDACTED.pdf"},
        {K7, "7777777777777777777", "REDACTED777.pdf"},
        {KA16, "aaaaaaaaaaaaaaaaa", "REDACTEDa.pdf"},
        {KA16, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "REDACTEDREDACTED.pdf"},
        {KAB, "ababababababababab", "REDACTEDab.pdf"},
        {KQ, "zqzqzqzqzqzqzqzqzq", "REDACTEDzq.pdf"},
        {"q~.-_z-0123456789", "x q~.-_z-0123456789 y", "x REDACTED y.pdf"},
        {"q.z-0123456789abc", "q.z-0123456789abc", "REDACTED.pdf"},
    };
    for (size_t i = 0; i < COUNT(c); i++) {
        title_case(t, c[i].key, c[i].title, c[i].name);
    }
}

static void t_long_key_in_the_title_is_redacted(ctest_t *t) {
    static char key[64], want[300];
    memset(key, '5', 63); /* 63 bytes: the longest the kit's key buffer holds */
    key[63] = '\0';
    title_case(t, key, key, "REDACTED.pdf");
    snprintf(g_title, sizeof g_title, "%s tail", key);
    title_case(t, key, g_title, "REDACTED tail.pdf");
    snprintf(g_title, sizeof g_title, "head %s", key);
    title_case(t, key, g_title, "head REDACTED.pdf");
    snprintf(g_title, sizeof g_title, "a %s b %s c", key, key);
    title_case(t, key, g_title, "a REDACTED b REDACTED c.pdf");
    snprintf(g_title, sizeof g_title, "%s%.62s", key, key); /* the key, then 62 bytes of a second one: one hit */
    snprintf(want, sizeof want, "REDACTED%.62s.pdf", key); /* 62 bytes of '5' remain */
    title_case(t, key, g_title, want);
}

static void t_redaction_comes_before_the_cut_at_200_bytes(ctest_t *t) {
    static char want[300];
    for (unsigned i = 0; i < 30u; i++) { /* 30 hits of 8 bytes = 240 bytes, cut to 200 = 25 hits */
        memcpy(g_title + 16u * i, K7, 16);
    }
    g_title[480] = '\0';
    want[0] = '\0';
    for (unsigned i = 0; i < 25u; i++) {
        strcat(want, "REDACTED");
    }
    strcat(want, ".pdf");
    title_case(t, K7, g_title, want);
    memset(g_title, 'a', 195); /* the key starts at byte 195: the cut leaves a piece of "REDACTED", never of the key */
    memcpy(g_title + 195, ZK, 17);
    memset(want, 'a', 195);
    memcpy(want + 195, "REDAC.pdf", 10);
    title_case(t, ZK, g_title, want);
    memset(g_title, 'a', 199);
    memcpy(g_title + 199, ZK, 17);
    memset(want, 'a', 199);
    memcpy(want + 199, "R.pdf", 6);
    title_case(t, ZK, g_title, want);
    memset(g_title, 'a', 197); /* the key straddles the cut */
    memcpy(g_title + 197, ZK " tail", 22);
    memset(want, 'a', 197);
    memcpy(want + 197, "RED.pdf", 8);
    title_case(t, ZK, g_title, want);
}

static void t_a_redacted_title_collides_like_any_other_name(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_set_key(k, ZK);
    api_ok(k, DLURL);
    kit_get(k, "a.example.org", 443, "/x", kit_http(k, 200, "OK", "", "x"));
    CHECK(t, kit_preload(k, "REDACTED.pdf", "old"));
    annas_request_t r = kit_req(KIT_MD5);
    r.title = core_str_from_cstr(ZK);
    r.format = core_str_from_cstr("pdf");
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_FS, 0, "file already exists: REDACTED.pdf");
    kit_check_no_partial(t, k, 1);
}

/* ---- the complete final name (second pass) ---------------------------------------------------------------------- */

/* Download with an optional title and format; the committed name is exactly `name` and no result string holds the key. */
static void name_case(ctest_t *t, const char *key, const char *title, const char *format, const char *name) {
    static char text[1024];
    kit_t *k = kit_begin();
    kit_set_key(k, key);
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, NULL))); /* no "_" in an account text */
    kit_get(k, "a.example.org", 443, "/x", kit_http(k, 200, "OK", "", "data"));
    annas_request_t r = kit_req(KIT_MD5);
    if (title != NULL) {
        r.title = core_str_from_cstr(title);
    }
    if (format != NULL) {
        r.format = core_str_from_cstr(format);
    }
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, name, "data", 4);
    CHECK_EQ_STR(t, k->dl.name, name);
    CHECK_EQ_U64(t, k->dl.name_len, strlen(name));
    CHECK(t, !kit_contains(k->dl.name, key));
    CHECK(t, !kit_contains(k->dl.path, key));
    core_buf_t b;
    CHECK(t, core_buf_init(&b, text, sizeof text));
    CHECK(t, annas_format_downloaded(&b, &k->dl));
    CHECK(t, !kit_contains(core_buf_cstr(&b), key));
}

static void t_a_key_made_by_sanitising_is_redacted(ctest_t *t) {
    name_case(t, "aaaaaaaa_bbbbbbbb", "aaaaaaaa/bbbbbbbb", "pdf", "REDACTED.pdf");
    name_case(t, "aaaaaaaa_bbbbbbbb", "x aaaaaaaa/bbbbbbbb y", "pdf", "x REDACTED y.pdf");
    name_case(t, "aaaaaaaa_bbbbbbbb", "aaaaaaaa:bbbbbbbb aaaaaaaa|bbbbbbbb", "epub", "REDACTED REDACTED.epub");
    name_case(t, "aaaaaaaa_bbbbbbbb", "aaaaaaaa/bbbbbbbb", NULL, "REDACTED.bin");
    name_case(t, "________________", "////////////////", "pdf", "REDACTED.pdf");
    name_case(t, "________________", "a/////////////////b", "pdf", "aREDACTED_b.pdf"); /* 17 underscores */
    name_case(t, "et_cetera_et_cetera", "et/cetera:et?cetera", "txt", "REDACTED.txt");
    name_case(t, "q-r-0123456789abcd", "q-r-0123456789abcd", "pdf", "REDACTED.pdf"); /* the first pass removed it */
}

static void t_a_key_inside_the_md5_is_redacted(ctest_t *t) {
    name_case(t, "456789abcdef0123", NULL, NULL, "0123REDACTED456789abcdef.bin");
    name_case(t, "456789abcdef0123", "", "pdf", "0123REDACTED456789abcdef.pdf");
    name_case(t, "0123456789abcdef0123456789abcdef", NULL, "pdf", "REDACTED.pdf");
    name_case(t, "89abcdef01234567", NULL, NULL, "01234567REDACTED89abcdef.bin");
    name_case(t, "0123456789abcdef", NULL, "pdf", "REDACTEDREDACTED.pdf"); /* it occurs twice */
}

/* The second pass sees the joined name: a key made of the end of the title, the dot and the start of the format. */
static void t_a_key_spanning_the_joint_of_title_and_format_is_redacted(ctest_t *t) {
    name_case(t, "zzzzzzzzzzzzzz.p", "zzzzzzzzzzzzzz", "pdf", "REDACTEDdf");
    name_case(t, "zzzzzzzzzzzzzz.b", "zzzzzzzzzzzzzz", NULL, "REDACTEDin");
    name_case(t, "yyyyyyyyyyyyyy.P", "yyyyyyyyyyyyyy", "pdf", "yyyyyyyyyyyyyy.pdf"); /* lower-cased: a near miss */
}

/* ---- the redirect ---------------------------------------------------------------------------------------------- */

/* One redirect from a.example.org with `status` and `loc`; the script has no step for any other host. */
static void redirect_once(kit_t *k, const char *key, int status, const char *loc) {
    kit_set_key(k, key);
    api_ok(k, DLURL);
    snprintf(g_hdr, sizeof g_hdr, "Location: %s\r\n", loc);
    kit_get(k, "a.example.org", 443, "/x", kit_http(k, status, "Moved", KIT_FMT(k, "%s", g_hdr), ""));
}

typedef struct {
    const char *key;
    const char *loc;
    int status;
} rcase_t;

static void t_a_target_containing_the_key_is_not_followed(ctest_t *t) {
    static const rcase_t c[] = {
        {ZK, "https://" ZK ".example.org/y", 302},
        {ZK, "https://b." ZK ".example.org/y", 301},
        {ZK, "https://b.example.org/" ZK "", 303},
        {ZK, "https://b.example.org/a/" ZK "/b", 307},
        {ZK, "https://b.example.org/a/" ZK "", 308},
        {ZK, "https://b.example.org/" ZK "" ZK "", 302},
        {ZK, "https://b.example.org/" ZK "/" ZK "?" ZK "=" ZK "", 302},
        {ZK, "https://b.example.org/a?t=" ZK "", 302},
        {ZK, "https://b.example.org/a?" ZK "=1", 302},
        {ZK, "https://b.example.org/a?x=1&y=2&k=" ZK "", 302},
        {ZK, "https://b.example.org:8443/" ZK "", 302},
        {ZK, "https://b.example.org/z" ZK "", 302},
        {AK, "https://abcdefghijklmnop.example.org/y", 302},   /* the host is compared without case */
        {AK, "https://ABCDEFGHIJKLMNOP.example.org/y", 302},
        {AK, "https://xAbCdEfGhIjKlMnOpx.example.org/y", 301},
        {AK, "https://b.AbCdEfGhIjKlMnOp.example.org/y", 307},
        {ZK, "https://ZQ9-0123456789AB.example.org/y", 302},
        {ZK, "https://B.example.org/" ZK "", 302},  /* a key in the path, exactly: still caught */
        {AK, "https://b.example.org/AbCdEfGhIjKlMnOp", 302},
        {AK, "https://b.example.org/a?x=AbCdEfGhIjKlMnOp", 302},
        {ZK, "/" ZK "", 302},
        {ZK, "/a/" ZK "?x=1", 302},
        {ZK, "/a?k=" ZK "", 301},
        {K7, "https://b.example.org/a" K7, 302},
        {K7, "https://h" K7 ".example.org/a", 302},
        {K7, "/a?x=" K7, 302},
        {KQ, "https://b.example.org/" KQ "zq", 302},
        {"zq9-x.7_~-0123456789", "https://b.example.org/f/zq9-x.7_~-0123456789", 302},
    };
    for (size_t i = 0; i < COUNT(c); i++) {
        kit_t *k = kit_begin();
        redirect_once(k, c[i].key, c[i].status, c[i].loc);
        kit_check_err(t, k, kit_download(k, &(annas_request_t){.md5 = core_str_from_cstr(KIT_MD5)}), ANNAS_E_HTTP,
                      c[i].status, REDIR);
        kit_check_fs_untouched(t, k);
    }
}

static void t_a_long_key_in_the_target_is_not_followed(ctest_t *t) {
    static char key[64];
    memset(key, '5', 63);
    key[63] = '\0';
    snprintf(g_loc, sizeof g_loc, "https://b.example.org/%s", key);
    kit_t *k = kit_begin();
    redirect_once(k, key, 302, g_loc);
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_HTTP, 302, REDIR);
    kit_check_fs_untouched(t, k);
    snprintf(g_loc, sizeof g_loc, "https://b.example.org/a?q=%s&r=1", key);
    k = kit_begin();
    redirect_once(k, key, 301, g_loc);
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_HTTP, 301, REDIR);
}

static void t_the_check_applies_to_every_hop_up_to_the_fifth_redirect(ctest_t *t) {
    for (int bad = 1; bad <= 5; bad++) {
        kit_t *k = kit_begin();
        kit_set_key(k, ZK);
        api_ok(k, "https://h0.example.org/f/0");
        for (int i = 0; i < bad; i++) {
            const char *loc = i + 1 == bad ? "https://h9.example.org/" ZK "" : KIT_FMT(k, "https://h%d.example.org/f/%d", i + 1, i + 1);
            kit_get(k, KIT_FMT(k, "h%d.example.org", i), 443, KIT_FMT(k, "/f/%d", i),
                    kit_http(k, 307, "F", KIT_FMT(k, "Location: %s\r\n", loc), ""));
        }
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_err(t, k, kit_download(k, &r), ANNAS_E_HTTP, 307, REDIR);
        kit_check_fs_untouched(t, k);
    }
}

static void t_other_redirect_errors_keep_precedence(ctest_t *t) {
    static const struct {
        const char *loc;
        annas_code_t code;
        int status;
        const char *msg;
    } c[] = {
        {"http://b.example.org/" ZK "", ANNAS_E_HTTP, 302, "redirect to a non-https URL"},
        {"https://b.example.org/a " ZK "", ANNAS_E_HTTP, 302, "invalid redirect location"},
        {"https://user@b.example.org/" ZK "", ANNAS_E_HTTP, 302, "invalid redirect location"},
        {"https://b.example.org/v?check=1&" ZK "", ANNAS_E_BROWSER_CHECK, 0, BROWSER},
    };
    for (size_t i = 0; i < COUNT(c); i++) {
        kit_t *k = kit_begin();
        redirect_once(k, ZK, 302, c[i].loc);
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_err(t, k, kit_download(k, &r), c[i].code, c[i].status, c[i].msg);
        kit_check_fs_untouched(t, k);
    }
    kit_t *k = kit_begin(); /* the 6th redirect response is "too many redirects", whatever its Location says */
    kit_set_key(k, ZK);
    api_ok(k, "https://h0.example.org/f/0");
    for (int i = 0; i < 6; i++) {
        const char *loc = i == 5 ? "https://h9.example.org/" ZK "" : KIT_FMT(k, "https://h%d.example.org/f/%d", i + 1, i + 1);
        kit_get(k, KIT_FMT(k, "h%d.example.org", i), 443, KIT_FMT(k, "/f/%d", i),
                kit_http(k, 302, "F", KIT_FMT(k, "Location: %s\r\n", loc), ""));
    }
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_HTTP, 302, "too many redirects");
}

typedef struct {
    const char *loc;
    const char *host;
    const char *target;
} follow_t;

/* A target that is followed: one hop to `host` and `target`, then the file. */
static void followed(ctest_t *t, const char *key, const follow_t *f) {
    kit_t *k = kit_begin();
    redirect_once(k, key, 302, f->loc);
    kit_get(k, f->host, 443, f->target, kit_http(k, 200, "OK", "", "data"));
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, KIT_MD5 ".bin", "data", 4);
}

static void t_near_misses_in_a_target_are_followed(ctest_t *t) {
    static const follow_t c[] = {
        {"https://b.example.org/zq9-0123456789a?b", "b.example.org", "/zq9-0123456789a?b"},
        {"https://b.example.org/zq9-0123456789/ab", "b.example.org", "/zq9-0123456789/ab"},
        {"https://b.example.org/ZQ9-0123456789AB", "b.example.org", "/ZQ9-0123456789AB"},
        {"https://b.example.org/zq9-0123456789a-b", "b.example.org", "/zq9-0123456789a-b"},
        {"https://zq9-0123456789a.example.org/b", "zq9-0123456789a.example.org", "/b"},
        {"/zq9-0123456789a/b", "a.example.org", "/zq9-0123456789a/b"},
    };
    for (size_t i = 0; i < COUNT(c); i++) {
        followed(t, ZK, &c[i]);
    }
}

/* Path and query are compared exactly: another case of the key there is not the key. */
static void t_path_and_query_compare_exactly(ctest_t *t) {
    static const follow_t c[] = {
        {"https://b.example.org/abcdefghijklmnop", "b.example.org", "/abcdefghijklmnop"},
        {"https://b.example.org/ABCDEFGHIJKLMNOP", "b.example.org", "/ABCDEFGHIJKLMNOP"},
        {"/a?x=abcdefghijklmnop", "a.example.org", "/a?x=abcdefghijklmnop"},
        {"/aBcDeFgHiJkLmNoP", "a.example.org", "/aBcDeFgHiJkLmNoP"},
    };
    for (size_t i = 0; i < COUNT(c); i++) {
        followed(t, AK, &c[i]);
    }
}

CTEST_SUITE("key_flow",
    {"title_containing_the_key_is_redacted", t_title_containing_the_key_is_redacted},
    {"long_key_in_the_title_is_redacted", t_long_key_in_the_title_is_redacted},
    {"redaction_comes_before_the_cut_at_200_bytes", t_redaction_comes_before_the_cut_at_200_bytes},
    {"a_key_made_by_sanitising_is_redacted", t_a_key_made_by_sanitising_is_redacted},
    {"a_key_inside_the_md5_is_redacted", t_a_key_inside_the_md5_is_redacted},
    {"a_key_spanning_the_joint_of_title_and_format_is_redacted", t_a_key_spanning_the_joint_of_title_and_format_is_redacted},
    {"a_redacted_title_collides_like_any_other_name", t_a_redacted_title_collides_like_any_other_name},
    {"a_target_containing_the_key_is_not_followed", t_a_target_containing_the_key_is_not_followed},
    {"a_long_key_in_the_target_is_not_followed", t_a_long_key_in_the_target_is_not_followed},
    {"the_check_applies_to_every_hop_up_to_the_fifth_redirect", t_the_check_applies_to_every_hop_up_to_the_fifth_redirect},
    {"other_redirect_errors_keep_precedence", t_other_redirect_errors_keep_precedence},
    {"near_misses_in_a_target_are_followed", t_near_misses_in_a_target_are_followed},
    {"path_and_query_compare_exactly", t_path_and_query_compare_exactly})
