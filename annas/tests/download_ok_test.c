/* annas_download: successful downloads, naming, redirects, framings. */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define ACCT "{\"downloads_left\":2}"
#define DLURL "https://dl.example.org/f/1?x=1"

static char g_body[3000];
static char g_name[400];
static char g_path[600];

/* API exchange answering `url`, with account info. */
static void api_ok(kit_t *k, const char *url) {
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, url, ACCT)));
}

/* Whole download script for one 200 response (head and body in separate reads). */
static void simple(kit_t *k, const char *body, size_t chunk) {
    api_ok(k, DLURL);
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1?x=1");
    kit_respond(k, kit_head(k, 200, "OK", "Content-Type: application/pdf\r\n", strlen(body)), 0);
    if (body[0] != '\0') {
        kit_respond(k, body, chunk);
    }
}

static void fill_pattern(size_t n) {
    for (size_t i = 0; i < n; i++) {
        g_body[i] = (char)('a' + (i * 7u + i / 13u) % 26u);
    }
    g_body[n] = '\0';
}

static void t_basic_download(ctest_t *t) {
    kit_t *k = kit_begin();
    simple(k, "hello world", 0);
    annas_request_t r = kit_req("0123456789ABCDEF0123456789abcdef");
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, KIT_MD5 ".bin", "hello world", 11);
    CHECK_EQ_STR(t, k->dl.name, KIT_MD5 ".bin");
    CHECK_EQ_U64(t, k->dl.name_len, 36);
    CHECK_EQ_STR(t, k->dl.path, "/dl/" KIT_MD5 ".bin");
    CHECK_EQ_U64(t, k->dl.path_len, 40);
    CHECK_EQ_U64(t, k->dl.bytes, 11);
    CHECK_EQ_STR(t, k->dl.fast.url, DLURL);
    CHECK_EQ_U64(t, k->dl.fast.url_len, 30);
    CHECK_EQ_STR(t, k->dl.fast.account, ACCT);
    CHECK_EQ_U64(t, k->dl.fast.account_len, 20);
    static char out[256];
    core_buf_t b;
    CHECK(t, core_buf_init(&b, out, sizeof out));
    CHECK(t, annas_format_downloaded(&b, &k->dl));
    CHECK_EQ_STR(t, core_buf_cstr(&b), "Book downloaded successfully to: /dl/" KIT_MD5 ".bin\nAccount: " ACCT);
}

typedef struct {
    const char *title;
    const char *format;
    const char *name;
} naming_t;

static void t_naming(ctest_t *t) {
    static const naming_t cases[] = {
        {"Title", "EpuB", "Title.epub"},
        {"Title", NULL, "Title.bin"},
        {"Title", "", "Title.bin"},
        {NULL, "pdf", KIT_MD5 ".pdf"},
        {"", "", KIT_MD5 ".bin"},
        {"My Book: Vol/1", "PDF", "My Book_ Vol_1.pdf"},
        {"../../etc/passwd", "txt", "____etc_passwd.txt"},
        {"   ", "pdf", "untitled.pdf"},
        {"...", "x", "_.x"},
        {".hidden", "djvu", "hidden.djvu"},
        {"a\\b|c?d*e<f>g\"h", "mobi", "a_b_c_d_e_f_g_h.mobi"},
        {"line\nbreak\t\x7f", "azw3", "line_break__.azw3"},
        {"Caf\xc3\xa9 \xc3\x91" "and\xc3\xba", "Fb2", "Caf\xc3\xa9 \xc3\x91" "and\xc3\xba.fb2"},
        {"abcdefghijklmnop", "ABCDEFGHIJKLMNOP", "abcdefghijklmnop.abcdefghijklmnop"},
        {"x", "A1b2", "x.a1b2"},
    };
    for (size_t i = 0; i < COUNT(cases); i++) {
        kit_t *k = kit_begin();
        simple(k, "data", 0);
        annas_request_t r = kit_req(KIT_MD5);
        if (cases[i].title != NULL) {
            r.title = core_str_from_cstr(cases[i].title);
        }
        if (cases[i].format != NULL) {
            r.format = core_str_from_cstr(cases[i].format);
        }
        kit_check_ok(t, k, kit_download(k, &r));
        kit_check_file(t, k, cases[i].name, "data", 4);
        CHECK_EQ_STR(t, k->dl.name, cases[i].name);
        CHECK_EQ_U64(t, k->dl.name_len, strlen(cases[i].name));
        snprintf(g_path, sizeof g_path, "/dl/%s", cases[i].name);
        CHECK_EQ_STR(t, k->dl.path, g_path);
    }
}

static void t_long_titles_are_cut_at_200_bytes(ctest_t *t) {
    kit_t *k = kit_begin();
    simple(k, "data", 0);
    memset(g_name, 'a', 300);
    g_name[300] = '\0';
    annas_request_t r = kit_req(KIT_MD5);
    r.title = core_str_from_cstr(g_name);
    r.format = core_str_from_cstr("pdf");
    kit_check_ok(t, k, kit_download(k, &r));
    memset(g_path, 'a', 200);
    memcpy(g_path + 200, ".pdf", 5);
    kit_check_file(t, k, g_path, "data", 4);
    CHECK_EQ_U64(t, k->dl.name_len, 204);
    /* the cut never splits a UTF-8 sequence: 199 bytes + a two-byte letter */
    k = kit_begin();
    simple(k, "data", 0);
    memset(g_name, 'a', 199);
    memcpy(g_name + 199, "\xc3\xa9zzz", 6);
    r.title = core_str_from_cstr(g_name);
    kit_check_ok(t, k, kit_download(k, &r));
    memset(g_path, 'a', 199);
    memcpy(g_path + 199, ".pdf", 5);
    kit_check_file(t, k, g_path, "data", 4);
}

static void t_directory_forms(ctest_t *t) {
    static const char *const dirs[] = {"/dl", "/dl/", "/dl//"};
    for (size_t i = 0; i < COUNT(dirs); i++) {
        kit_t *k = kit_begin();
        kit_set_dir(k, dirs[i]);
        simple(k, "data", 0);
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_ok(t, k, kit_download(k, &r));
        CHECK_EQ_STR(t, k->dl.path, "/dl/" KIT_MD5 ".bin");
    }
    kit_t *k = kit_begin();
    kit_set_dir(k, "/");
    simple(k, "data", 0);
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_download(k, &r));
    CHECK_EQ_STR(t, k->dl.path, "/" KIT_MD5 ".bin");
    k = kit_begin();
    kit_set_dir(k, "/srv/my books/x");
    simple(k, "data", 0);
    kit_check_ok(t, k, kit_download(k, &r));
    CHECK_EQ_STR(t, k->dl.path, "/srv/my books/x/" KIT_MD5 ".bin");
    k = kit_begin(); /* the longest directory */
    memset(g_name, 'd', 256);
    g_name[0] = '/';
    g_name[256] = '\0';
    kit_set_dir(k, g_name);
    simple(k, "data", 0);
    kit_check_ok(t, k, kit_download(k, &r));
    CHECK_EQ_U64(t, k->dl.path_len, 256 + 1 + 36);
}

static void t_read_sizes_do_not_matter(ctest_t *t) {
    static const size_t chunks[] = {0, 1, 2, 7, 64, 1000, 4096};
    fill_pattern(1000);
    for (size_t i = 0; i < COUNT(chunks); i++) {
        kit_t *k = kit_begin();
        simple(k, g_body, chunks[i]);
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_ok(t, k, kit_download(k, &r));
        kit_check_file(t, k, KIT_MD5 ".bin", g_body, 1000);
        CHECK_EQ_U64(t, k->dl.bytes, 1000);
    }
}

static void t_head_and_body_in_one_read(ctest_t *t) {
    kit_t *k = kit_begin();
    api_ok(k, DLURL);
    kit_get(k, "dl.example.org", 443, "/f/1?x=1", kit_http(k, 200, "OK", "", "all at once"));
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, KIT_MD5 ".bin", "all at once", 11);
}

static void t_one_byte_reads_of_the_whole_message(ctest_t *t) {
    kit_t *k = kit_begin();
    api_ok(k, DLURL);
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1?x=1");
    kit_respond(k, kit_http(k, 200, "OK", "A: b\r\n", "tiny"), 1);
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, KIT_MD5 ".bin", "tiny", 4);
}

static void t_chunked_body(ctest_t *t) {
    kit_t *k = kit_begin();
    api_ok(k, DLURL);
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1?x=1");
    kit_respond(k, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5;a=b\r\nhello\r\n1\r\n \r\n5\r\nworld\r\n0\r\nX: y\r\n\r\n", 0);
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, KIT_MD5 ".bin", "hello world", 11);
    CHECK_EQ_U64(t, k->dl.bytes, 11);
}

static void t_read_until_close_body(ctest_t *t) {
    kit_t *k = kit_begin();
    api_ok(k, DLURL);
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1?x=1");
    kit_respond(k, "HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\n\r\n", 0);
    kit_respond(k, "part one, ", 3);
    kit_respond(k, "part two", 0);
    kit_eof(k);
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, KIT_MD5 ".bin", "part one, part two", 18);
}

static void t_empty_body_commits_an_empty_file(ctest_t *t) {
    kit_t *k = kit_begin();
    simple(k, "", 0);
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_download(k, &r));
    kit_check_file(t, k, KIT_MD5 ".bin", "", 0);
    CHECK_EQ_U64(t, k->dl.bytes, 0);
}

static void t_api_indices_reach_the_api_only(ctest_t *t) {
    kit_t *k = kit_begin();
    kit_api(k, KIT_MD5, "&path_index=1&domain_index=2", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, NULL)));
    kit_get(k, "dl.example.org", 443, "/f/1?x=1", kit_http(k, 200, "OK", "", "x"));
    annas_request_t r = kit_req(KIT_MD5);
    r.has_path_index = r.has_domain_index = true;
    r.path_index = 1;
    r.domain_index = 2;
    kit_check_ok(t, k, kit_download(k, &r));
    CHECK_EQ_STR(t, k->dl.fast.account, "");
    kit_check_file(t, k, KIT_MD5 ".bin", "x", 1);
    static char out[128];
    core_buf_t b;
    CHECK(t, core_buf_init(&b, out, sizeof out));
    CHECK(t, annas_format_downloaded(&b, &k->dl)); /* no Account line without account info */
    CHECK_EQ_STR(t, core_buf_cstr(&b), "Book downloaded successfully to: /dl/" KIT_MD5 ".bin");
}

static void t_download_host_and_port(ctest_t *t) {
    kit_t *k = kit_begin();
    api_ok(k, "https://Files.Example.org:8443/a/b?c=d&e=f");
    kit_get(k, "Files.Example.org", 8443, "/a/b?c=d&e=f", kit_http(k, 200, "OK", "", "p"));
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_download(k, &r));
    k = kit_begin(); /* an explicit :443 is dropped from the Host header; the fragment is not sent */
    api_ok(k, "https://dl.example.org:443/a#frag");
    kit_get(k, "dl.example.org", 443, "/a", kit_http(k, 200, "OK", "", "p"));
    kit_check_ok(t, k, kit_download(k, &r));
}

static void t_url_on_the_api_host_does_not_get_the_key(ctest_t *t) {
    kit_t *k = kit_begin();
    api_ok(k, "https://annas-archive.gl/fast_download/abc/0/0");
    kit_get(k, "annas-archive.gl", 443, "/fast_download/abc/0/0", kit_http(k, 200, "OK", "", "p"));
    annas_request_t r = kit_req(KIT_MD5);
    kit_check_ok(t, k, kit_download(k, &r));
}

static void t_two_downloads_on_one_context(ctest_t *t) {
    kit_t *k = kit_begin();
    simple(k, "first", 0);
    api_ok(k, DLURL);
    kit_get(k, "dl.example.org", 443, "/f/1?x=1", kit_http(k, 200, "OK", "", "second"));
    annas_request_t r = kit_req(KIT_MD5);
    r.title = core_str_from_cstr("One");
    CHECK_EQ_I64(t, kit_download(k, &r), ANNAS_OK);
    r.title = core_str_from_cstr("Two");
    CHECK_EQ_I64(t, annas_download(&k->ctx, &r, &k->dl, &k->err), ANNAS_OK);
    core_str_t c;
    CHECK(t, ports_fake_fs_find(&k->fs, core_str_from_cstr("One.bin"), &c));
    CHECK_EQ_MEM(t, c.ptr, c.len, "first", 5);
    CHECK(t, ports_fake_fs_find(&k->fs, core_str_from_cstr("Two.bin"), &c));
    CHECK_EQ_MEM(t, c.ptr, c.len, "second", 6);
    CHECK_EQ_U64(t, ports_fake_fs_commits(&k->fs), 2);
    CHECK_EQ_STR(t, k->dl.name, "Two.bin");
    kit_check_ok(t, k, ANNAS_OK);
}

/* ---- redirects ------------------------------------------------------------------------------------------ */

static const char *redirect_head(kit_t *k, int status, const char *location) {
    return kit_http(k, status, "Redirect", KIT_FMT(k, "Location: %s\r\n", location), "");
}

static void t_redirect_chains(ctest_t *t) {
    for (int n = 0; n <= 5; n++) {
        kit_t *k = kit_begin();
        api_ok(k, "https://h0.example.org/f/0");
        for (int i = 0; i < n; i++) {
            const char *host = KIT_FMT(k, "h%d.example.org", i);
            const char *target = KIT_FMT(k, "/f/%d", i);
            const char *loc = KIT_FMT(k, "https://h%d.example.org/f/%d", i + 1, i + 1);
            kit_get(k, host, 443, target, redirect_head(k, 302, loc));
        }
        kit_get(k, KIT_FMT(k, "h%d.example.org", n), 443, KIT_FMT(k, "/f/%d", n), kit_http(k, 200, "OK", "", "final"));
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_ok(t, k, kit_download(k, &r));
        kit_check_file(t, k, KIT_MD5 ".bin", "final", 5);
        CHECK_EQ_U64(t, ports_fake_stream_closes(&k->stream), (uint64_t)n + 2u);
    }
}

static void t_every_redirect_status_is_followed(ctest_t *t) {
    static const int codes[] = {301, 302, 303, 307, 308};
    for (size_t i = 0; i < COUNT(codes); i++) {
        kit_t *k = kit_begin();
        api_ok(k, "https://a.example.org/x");
        kit_get(k, "a.example.org", 443, "/x", redirect_head(k, codes[i], "https://b.example.org/y"));
        kit_get(k, "b.example.org", 443, "/y", kit_http(k, 200, "OK", "", "ok"));
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_ok(t, k, kit_download(k, &r));
        kit_check_file(t, k, KIT_MD5 ".bin", "ok", 2);
    }
}

static void t_location_forms(ctest_t *t) {
    static const struct {
        const char *loc;
        const char *host;
        uint16_t port;
        const char *target;
    } c[] = {
        {"/abs/path?q=1", "a.example.org", 443, "/abs/path?q=1"},
        {"rel/file", "a.example.org", 443, "/dir/rel/file"},
        {"../up.bin", "a.example.org", 443, "/up.bin"},
        {"?only=query", "a.example.org", 443, "/dir/start?only=query"},
        {"//other.example.org/z", "other.example.org", 443, "/z"},
        {"https://x.example.org:9443/q#frag", "x.example.org", 9443, "/q"},
        {"HTTPS://Y.example.org", "Y.example.org", 443, "/"},
    };
    for (size_t i = 0; i < COUNT(c); i++) {
        kit_t *k = kit_begin();
        api_ok(k, "https://a.example.org/dir/start?old=1#f");
        kit_get(k, "a.example.org", 443, "/dir/start?old=1", redirect_head(k, 302, c[i].loc));
        kit_get(k, c[i].host, c[i].port, c[i].target, kit_http(k, 200, "OK", "", "ok"));
        annas_request_t r = kit_req(KIT_MD5);
        kit_check_ok(t, k, kit_download(k, &r));
        kit_check_file(t, k, KIT_MD5 ".bin", "ok", 2);
    }
}

CTEST_SUITE("download_ok",
    {"basic_download", t_basic_download},
    {"naming", t_naming},
    {"long_titles_are_cut_at_200_bytes", t_long_titles_are_cut_at_200_bytes},
    {"directory_forms", t_directory_forms},
    {"read_sizes_do_not_matter", t_read_sizes_do_not_matter},
    {"head_and_body_in_one_read", t_head_and_body_in_one_read},
    {"one_byte_reads_of_the_whole_message", t_one_byte_reads_of_the_whole_message},
    {"chunked_body", t_chunked_body},
    {"read_until_close_body", t_read_until_close_body},
    {"empty_body_commits_an_empty_file", t_empty_body_commits_an_empty_file},
    {"api_indices_reach_the_api_only", t_api_indices_reach_the_api_only},
    {"download_host_and_port", t_download_host_and_port},
    {"url_on_the_api_host_does_not_get_the_key", t_url_on_the_api_host_does_not_get_the_key},
    {"two_downloads_on_one_context", t_two_downloads_on_one_context},
    {"redirect_chains", t_redirect_chains},
    {"every_redirect_status_is_followed", t_every_redirect_status_is_followed},
    {"location_forms", t_location_forms})
