/* The member key in a download: a title that contains it (annas.h, Messages: TITLE) and a redirect whose target
 * contains it (REDIRECT). Fake keys only. */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

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
        {"zq9", "zq9", "REDACTED.pdf"},
        {"zq9", "zq9 book", "REDACTED book.pdf"},
        {"zq9", "my zq9", "my REDACTED.pdf"},
        {"zq9", "my zq9 book", "my REDACTED book.pdf"},
        {"zq9", "zq9zq9", "REDACTEDREDACTED.pdf"},
        {"zq9", "zq9 and zq9", "REDACTED and REDACTED.pdf"},
        {"zq9", "azq9b", "aREDACTEDb.pdf"},
        {"zq9", "zq9zq", "REDACTEDzq.pdf"},
        {"zq9", "zqzq9", "zqREDACTED.pdf"},
        {"zq9", "zq9/zq9", "REDACTED_REDACTED.pdf"},
        {"zq9", "zq", "zq.pdf"},
        {"zq9", "ZQ9 zq 9 z-q9", "ZQ9 zq 9 z-q9.pdf"},
        {"7", "7", "REDACTED.pdf"},
        {"7", "a7b7c", "aREDACTEDbREDACTEDc.pdf"},
        {"7", "777", "REDACTEDREDACTEDREDACTED.pdf"},
        {"aa", "aaa", "REDACTEDa.pdf"},
        {"aa", "aaaa", "REDACTEDREDACTED.pdf"},
        {"aba", "ababa", "REDACTEDba.pdf"},
        {"zqz", "zqzqz", "REDACTEDqz.pdf"},
        {"q~.-_z", "x q~.-_z y", "x REDACTED y.pdf"},
        {"q.z", "q.z", "REDACTED.pdf"},
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
    memset(g_title, '7', 30); /* 30 hits of 8 bytes = 240 bytes, cut to 200 = 25 hits */
    g_title[30] = '\0';
    want[0] = '\0';
    for (unsigned i = 0; i < 25u; i++) {
        strcat(want, "REDACTED");
    }
    strcat(want, ".pdf");
    title_case(t, "7", g_title, want);
    memset(g_title, 'a', 195); /* the key starts at byte 195: the cut leaves a piece of "REDACTED", never of the key */
    memcpy(g_title + 195, "zq9", 4);
    memset(want, 'a', 195);
    memcpy(want + 195, "REDAC.pdf", 10);
    title_case(t, "zq9", g_title, want);
    memset(g_title, 'a', 199);
    memcpy(g_title + 199, "zq9", 4);
    memset(want, 'a', 199);
    memcpy(want + 199, "R.pdf", 6);
    title_case(t, "zq9", g_title, want);
    memset(g_title, 'a', 197); /* the key straddles the cut */
    memcpy(g_title + 197, "zq9 tail", 9);
    memset(want, 'a', 197);
    memcpy(want + 197, "RED.pdf", 8);
    title_case(t, "zq9", g_title, want);
}

static void t_a_redacted_title_collides_like_any_other_name(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_set_key(k, "zq9");
    api_ok(k, DLURL);
    kit_get(k, "a.example.org", 443, "/x", kit_http(k, 200, "OK", "", "x"));
    CHECK(t, kit_preload(k, "REDACTED.pdf", "old"));
    annas_request_t r = kit_req(KIT_MD5);
    r.title = core_str_from_cstr("zq9");
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
    name_case(t, "a_b", "a/b", "pdf", "REDACTED.pdf");
    name_case(t, "a_b", "x a/b y", "pdf", "x REDACTED y.pdf");
    name_case(t, "a_b", "a:b a|b", "epub", "REDACTED REDACTED.epub");
    name_case(t, "a_b", "a/b", NULL, "REDACTED.bin");
    name_case(t, "_", "a/b", "pdf", "aREDACTEDb.pdf");
    name_case(t, "__", "a//b", "pdf", "aREDACTEDb.pdf");
    name_case(t, "et_c", "et/c x", "txt", "REDACTED x.txt");
    name_case(t, "q-r", "q-r", "pdf", "REDACTED.pdf"); /* the first pass already removed it */
}

static void t_a_key_inside_the_md5_is_redacted(ctest_t *t) {
    name_case(t, "abcdef0123", NULL, NULL, "0123456789REDACTED456789abcdef.bin");
    name_case(t, "abcdef0123", "", "pdf", "0123456789REDACTED456789abcdef.pdf");
    name_case(t, "0123456789abcdef0123456789abcdef", NULL, "pdf", "REDACTED.pdf");
    name_case(t, "89ab", NULL, NULL, "01234567REDACTEDcdef01234567REDACTEDcdef.bin");
    name_case(t, "abcdef", NULL, NULL, "0123456789REDACTED0123456789REDACTED.bin");
    name_case(t, "0123", NULL, "pdf", "REDACTED456789abcdefREDACTED456789abcdef.pdf");
}

static void t_a_key_equal_to_the_format_is_redacted(ctest_t *t) {
    name_case(t, "pdf", "Book", "pdf", "Book.REDACTED");
    name_case(t, "pdf", "Book", "PDF", "Book.REDACTED");   /* the format is lower-cased before the pass */
    name_case(t, "pdf", "pdf", "pdf", "REDACTED.REDACTED");
    name_case(t, "bin", "Book", NULL, "Book.REDACTED");
    name_case(t, "bin", NULL, NULL, KIT_MD5 ".REDACTED");
    name_case(t, "df", "Book", "pdf", "Book.pREDACTED");
    name_case(t, ".pdf", "Book", "pdf", "BookREDACTED");
    name_case(t, "k.p", "Book.pdf", "pdf", "BooREDACTEDdf.pdf"); /* the title pass; the name pass finds nothing */
}

static void t_the_redacted_name_is_cut_at_the_name_limit(ctest_t *t) {
    static char title[260], want[260];
    memset(title, '/', 200); /* sanitised to 200 underscores, then ".pdf" */
    title[200] = '\0';
    for (size_t i = 0; i < sizeof want - 1u; i++) {
        want[i] = "REDACTED"[i % 8u];
    }
    want[ANNAS_NAME_MAX - 1u] = '\0'; /* ANNAS_NAME_MAX-1 bytes */
    name_case(t, "_", title, "pdf", want);
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
        {"zq9", "https://zq9.example.org/y", 302},
        {"zq9", "https://b.zq9.example.org/y", 301},
        {"zq9", "https://b.example.org/zq9", 303},
        {"zq9", "https://b.example.org/a/zq9/b", 307},
        {"zq9", "https://b.example.org/a/zq9", 308},
        {"zq9", "https://b.example.org/zq9zq9", 302},
        {"zq9", "https://b.example.org/zq9/zq9?zq9=zq9", 302},
        {"zq9", "https://b.example.org/a?t=zq9", 302},
        {"zq9", "https://b.example.org/a?zq9=1", 302},
        {"zq9", "https://b.example.org/a?x=1&y=2&k=zq9", 302},
        {"zq9", "https://b.example.org:8443/zq9", 302},
        {"zq9", "https://b.example.org/zzq9", 302},
        {"AbC", "https://abc.example.org/y", 302},   /* the host is compared without case */
        {"AbC", "https://ABC.example.org/y", 302},
        {"AbC", "https://xAbCx.example.org/y", 301},
        {"AbC", "https://b.AbC.example.org/y", 307},
        {"zq9", "https://ZQ9.example.org/y", 302},
        {"zq9", "https://B.example.org/zq9", 302},  /* a key in the path, exactly: still caught */
        {"AbC", "https://b.example.org/AbC", 302},
        {"AbC", "https://b.example.org/a?x=AbC", 302},
        {"zq9", "/zq9", 302},
        {"zq9", "/a/zq9?x=1", 302},
        {"zq9", "/a?k=zq9", 301},
        {"7", "https://b.example.org/a7", 302},
        {"7", "https://h7.example.org/a", 302},
        {"7", "/a?x=7", 302},
        {"zqzq", "https://b.example.org/zqzqzq", 302},
        {"zq9-x.7_~", "https://b.example.org/f/zq9-x.7_~", 302},
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
        kit_set_key(k, "zq9");
        api_ok(k, "https://h0.example.org/f/0");
        for (int i = 0; i < bad; i++) {
            const char *loc = i + 1 == bad ? "https://h9.example.org/zq9" : KIT_FMT(k, "https://h%d.example.org/f/%d", i + 1, i + 1);
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
        {"http://b.example.org/zq9", ANNAS_E_HTTP, 302, "redirect to a non-https URL"},
        {"https://b.example.org/a zq9", ANNAS_E_HTTP, 302, "invalid redirect location"},
        {"https://user@b.example.org/zq9", ANNAS_E_HTTP, 302, "invalid redirect location"},
        {"https://b.example.org/v?check=1&zq9", ANNAS_E_BROWSER_CHECK, 0, BROWSER},
    };
    for (size_t i = 0; i < COUNT(c); i++) {
        kit_t *k = kit_begin();
        redirect_once(k, "zq9", 302, c[i].loc);
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_err(t, k, kit_download(k, &r), c[i].code, c[i].status, c[i].msg);
        kit_check_fs_untouched(t, k);
    }
    kit_t *k = kit_begin(); /* the 6th redirect response is "too many redirects", whatever its Location says */
    kit_set_key(k, "zq9");
    api_ok(k, "https://h0.example.org/f/0");
    for (int i = 0; i < 6; i++) {
        const char *loc = i == 5 ? "https://h9.example.org/zq9" : KIT_FMT(k, "https://h%d.example.org/f/%d", i + 1, i + 1);
        kit_get(k, KIT_FMT(k, "h%d.example.org", i), 443, KIT_FMT(k, "/f/%d", i),
                kit_http(k, 302, "F", KIT_FMT(k, "Location: %s\r\n", loc), ""));
    }
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_err(t, k, kit_download(k, &r), ANNAS_E_HTTP, 302, "too many redirects");
}

static void t_near_misses_in_a_target_are_followed(ctest_t *t) {
    static const char *const loc[] = {"https://b.example.org/zq?9", "https://b.example.org/z/q9",
                                      "https://b.example.org/ZQ9", "https://b.example.org/zq-9",
                                      "https://zq.example.org/9", "/zq/9"};
    for (size_t i = 0; i < COUNT(loc); i++) {
        kit_t *k = kit_begin();
        redirect_once(k, "zq9", 302, loc[i]);
        const char *host = loc[i][0] == '/' ? "a.example.org" : (i == 4 ? "zq.example.org" : "b.example.org");
        const char *target = loc[i][0] == '/' ? loc[i] : strchr(loc[i] + 8, '/');
        kit_get(k, host, 443, target, kit_http(k, 200, "OK", "", "data"));
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_ok(t, k, kit_download(k, &r));
        kit_check_file(t, k, KIT_MD5 ".bin", "data", 4);
    }
}

/* Path and query are compared exactly: another case of the key there is not the key. */
static void t_path_and_query_compare_exactly(ctest_t *t) {
    static const char *const loc[] = {"https://b.example.org/abc", "https://b.example.org/ABC", "/a?x=abc", "/aBC"};
    for (size_t i = 0; i < COUNT(loc); i++) {
        kit_t *k = kit_begin();
        redirect_once(k, "AbC", 302, loc[i]);
        const char *host = loc[i][0] == '/' ? "a.example.org" : "b.example.org";
        const char *target = loc[i][0] == '/' ? loc[i] : strchr(loc[i] + 8, '/');
        kit_get(k, host, 443, target, kit_http(k, 200, "OK", "", "data"));
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_ok(t, k, kit_download(k, &r));
        kit_check_file(t, k, KIT_MD5 ".bin", "data", 4);
    }
}

CTEST_SUITE("key_flow",
    {"title_containing_the_key_is_redacted", t_title_containing_the_key_is_redacted},
    {"long_key_in_the_title_is_redacted", t_long_key_in_the_title_is_redacted},
    {"redaction_comes_before_the_cut_at_200_bytes", t_redaction_comes_before_the_cut_at_200_bytes},
    {"a_key_made_by_sanitising_is_redacted", t_a_key_made_by_sanitising_is_redacted},
    {"a_key_inside_the_md5_is_redacted", t_a_key_inside_the_md5_is_redacted},
    {"a_key_equal_to_the_format_is_redacted", t_a_key_equal_to_the_format_is_redacted},
    {"the_redacted_name_is_cut_at_the_name_limit", t_the_redacted_name_is_cut_at_the_name_limit},
    {"a_redacted_title_collides_like_any_other_name", t_a_redacted_title_collides_like_any_other_name},
    {"a_target_containing_the_key_is_not_followed", t_a_target_containing_the_key_is_not_followed},
    {"a_long_key_in_the_target_is_not_followed", t_a_long_key_in_the_target_is_not_followed},
    {"the_check_applies_to_every_hop_up_to_the_fifth_redirect", t_the_check_applies_to_every_hop_up_to_the_fifth_redirect},
    {"other_redirect_errors_keep_precedence", t_other_redirect_errors_keep_precedence},
    {"near_misses_in_a_target_are_followed", t_near_misses_in_a_target_are_followed},
    {"path_and_query_compare_exactly", t_path_and_query_compare_exactly})
