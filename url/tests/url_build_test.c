/* Tests for url/url.h: request URL builders (spec sections 1, 3.1-3.4). Public header only.
 * Keys in this file are made-up test strings. */
#include <stddef.h>
#include <string.h>

#include "ctest/ctest.h"
#include "url/url.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define EMPTY ((core_str_t){NULL, 0})
#define BASE LIT("annas-archive.gl")
#define MD5 LIT("0123456789abcdef0123456789abcdef")
#define MD5S "0123456789abcdef0123456789abcdef"

static char store[1024];
static core_buf_t B;

static void fresh(size_t cap) {
    memset(store, 0xA5, sizeof store);
    core_buf_init(&B, store, cap);
}

static void is_str(ctest_t *t, const char *ex) {
    CHECK_EQ_U64(t, core_buf_len(&B), strlen(ex));
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), ex, strlen(ex));
    CHECK(t, !core_buf_overflow(&B));
}

/* The buffer is left exactly as "pre" after a rejected call (and not overflowed). */
static void untouched(ctest_t *t) {
    CHECK_EQ_STR(t, core_buf_cstr(&B), "pre");
    CHECK(t, !core_buf_overflow(&B));
}

static void prep(void) {
    fresh(sizeof store);
    core_buf_put_str(&B, LIT("pre"));
}

static void t_search_book(ctest_t *t) {
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_build_search(&B, BASE, LIT("dune"), URL_CONTENT_BOOK), URL_OK);
    is_str(t, "https://annas-archive.gl/search?q=dune&content=book_any");
}

static void t_search_journal(ctest_t *t) {
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_build_search(&B, BASE, LIT("dune"), URL_CONTENT_JOURNAL), URL_OK);
    is_str(t, "https://annas-archive.gl/search?q=dune&content=journal");
}

static void t_search_other_base(ctest_t *t) {
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_build_search(&B, LIT("127.0.0.1"), LIT("x"), URL_CONTENT_BOOK), URL_OK);
    is_str(t, "https://127.0.0.1/search?q=x&content=book_any");
}

static void search_is(ctest_t *t, core_str_t q, const char *ex) {
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_build_search(&B, BASE, q, URL_CONTENT_BOOK), URL_OK);
    is_str(t, ex);
}

static void t_search_query_is_encoded(ctest_t *t) {
    search_is(t, LIT("hello world"), "https://annas-archive.gl/search?q=hello%20world&content=book_any");
    search_is(t, LIT("a&b=c"), "https://annas-archive.gl/search?q=a%26b%3Dc&content=book_any");
    search_is(t, LIT("c++ & c#"), "https://annas-archive.gl/search?q=c%2B%2B%20%26%20c%23&content=book_any");
    search_is(t, LIT("100%"), "https://annas-archive.gl/search?q=100%25&content=book_any");
    search_is(t, LIT("caf\xc3\xa9"), "https://annas-archive.gl/search?q=caf%C3%A9&content=book_any");
    search_is(t, LIT("a/b?c"), "https://annas-archive.gl/search?q=a%2Fb%3Fc&content=book_any");
    search_is(t, LIT("a\0b"), "https://annas-archive.gl/search?q=a%00b&content=book_any");
    search_is(t, LIT("-._~"), "https://annas-archive.gl/search?q=-._~&content=book_any");
}

static void t_search_query_cannot_inject_parameters(ctest_t *t) {
    search_is(t, LIT("x&content=journal"), "https://annas-archive.gl/search?q=x%26content%3Djournal&content=book_any");
    search_is(t, LIT("x#frag"), "https://annas-archive.gl/search?q=x%23frag&content=book_any");
    search_is(t, LIT("../../etc"), "https://annas-archive.gl/search?q=..%2F..%2Fetc&content=book_any");
    search_is(t, LIT("x\r\nHost: evil"), "https://annas-archive.gl/search?q=x%0D%0AHost%3A%20evil&content=book_any");
}

static void t_search_empty_query(ctest_t *t) {
    search_is(t, LIT(""), "https://annas-archive.gl/search?q=&content=book_any");
    search_is(t, EMPTY, "https://annas-archive.gl/search?q=&content=book_any");
}

static void t_search_rejections_have_positive_control(ctest_t *t) {
    static const core_str_t bad_bases[] = {
        {"", 0}, {"x/y", 3}, {"https://x.org", 13}, {"X.org", 5}, {"x.org/", 6}, {"x.org:80", 8},
        {"x y", 3}, {"x_y", 3}, {"a\0b", 3}, {"x.org\n", 6}};
    size_t i;
    prep();
    CHECK_EQ_I64(t, url_build_search(&B, BASE, LIT("q"), URL_CONTENT_BOOK), URL_OK);   /* control */
    for (i = 0; i < sizeof bad_bases / sizeof bad_bases[0]; i++) {
        prep();
        CHECK_EQ_I64(t, url_build_search(&B, bad_bases[i], LIT("q"), URL_CONTENT_BOOK), URL_E_INVALID);
        untouched(t);
    }
    prep();
    CHECK_EQ_I64(t, url_build_search(&B, BASE, LIT("q"), (url_content_t)2), URL_E_ARG);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_search(&B, BASE, LIT("q"), (url_content_t)-1), URL_E_ARG);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_search(&B, BASE, LIT("q"), (url_content_t)99), URL_E_ARG);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_search(&B, BASE, (core_str_t){NULL, 2}, URL_CONTENT_BOOK), URL_E_ARG);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_search(&B, (core_str_t){NULL, 2}, LIT("q"), URL_CONTENT_BOOK), URL_E_ARG);
    untouched(t);
    CHECK_EQ_I64(t, url_build_search(NULL, BASE, LIT("q"), URL_CONTENT_BOOK), URL_E_ARG);
    /* precedence: ARG before INVALID */
    prep();
    CHECK_EQ_I64(t, url_build_search(&B, LIT("BAD"), LIT("q"), (url_content_t)7), URL_E_ARG);
    untouched(t);
    CHECK_EQ_I64(t, url_build_search(NULL, LIT("BAD"), LIT("q"), URL_CONTENT_BOOK), URL_E_ARG);
}

static void t_fast_download_exact(ctest_t *t) {
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_build_fast_download(&B, BASE, MD5, LIT("FAKEKEY123")), URL_OK);
    is_str(t, "https://annas-archive.gl/dyn/api/fast_download.json?md5=" MD5S "&key=FAKEKEY123");
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_build_fast_download(&B, LIT("x.org"), LIT("ffffffffffffffffffffffffffffffff"),
                                            LIT("k")), URL_OK);
    is_str(t, "https://x.org/dyn/api/fast_download.json?md5=ffffffffffffffffffffffffffffffff&key=k");
}

static void key_is(ctest_t *t, core_str_t key, const char *ex) {
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_build_fast_download(&B, BASE, MD5, key), URL_OK);
    is_str(t, ex);
}

static void t_fast_download_key_is_encoded(ctest_t *t) {
    key_is(t, LIT("a b"), "https://annas-archive.gl/dyn/api/fast_download.json?md5=" MD5S "&key=a%20b");
    key_is(t, LIT("ab+/="), "https://annas-archive.gl/dyn/api/fast_download.json?md5=" MD5S "&key=ab%2B%2F%3D");
    key_is(t, LIT("k&md5=evil"),
           "https://annas-archive.gl/dyn/api/fast_download.json?md5=" MD5S "&key=k%26md5%3Devil");
    key_is(t, LIT("k#x?y"), "https://annas-archive.gl/dyn/api/fast_download.json?md5=" MD5S "&key=k%23x%3Fy");
    key_is(t, LIT("\xc3\xa9"), "https://annas-archive.gl/dyn/api/fast_download.json?md5=" MD5S "&key=%C3%A9");
    key_is(t, LIT("A-Z_a.z~09"), "https://annas-archive.gl/dyn/api/fast_download.json?md5=" MD5S "&key=A-Z_a.z~09");
    key_is(t, LIT("%41"), "https://annas-archive.gl/dyn/api/fast_download.json?md5=" MD5S "&key=%2541");
}

static void t_fast_download_rejections(ctest_t *t) {
    static const core_str_t bad_md5[] = {
        {"", 0},
        {"0123456789ABCDEF0123456789ABCDEF", 32},    /* upper case: caller must normalise */
        {"0123456789abcdef0123456789abcdeF", 32},
        {"0123456789abcdef0123456789abcde", 31},
        {"0123456789abcdef0123456789abcdef0", 33},
        {"0123456789abcdef0123456789abcdeg", 32},
        {"g123456789abcdef0123456789abcdef", 32},
        {" 123456789abcdef0123456789abcdef", 32},
        {"0123456789abcdef0123456789abcde\0", 32},
        {"0123456789abcdef0123456789abcdef\n", 33},
        {"../../../etc/passwd/../../../etc/p", 34}};
    size_t i;
    prep();
    CHECK_EQ_I64(t, url_build_fast_download(&B, BASE, MD5, LIT("k")), URL_OK);   /* control */
    for (i = 0; i < sizeof bad_md5 / sizeof bad_md5[0]; i++) {
        prep();
        CHECK_EQ_I64(t, url_build_fast_download(&B, BASE, bad_md5[i], LIT("k")), URL_E_INVALID);
        untouched(t);
    }
    prep();
    CHECK_EQ_I64(t, url_build_fast_download(&B, BASE, MD5, LIT("")), URL_E_INVALID);   /* empty key */
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_fast_download(&B, BASE, MD5, EMPTY), URL_E_INVALID);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_fast_download(&B, LIT("X.org"), MD5, LIT("k")), URL_E_INVALID);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_fast_download(&B, BASE, MD5, (core_str_t){NULL, 3}), URL_E_ARG);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_fast_download(&B, BASE, (core_str_t){NULL, 32}, LIT("k")), URL_E_ARG);
    untouched(t);
    CHECK_EQ_I64(t, url_build_fast_download(NULL, BASE, MD5, LIT("k")), URL_E_ARG);
}

static void t_md5_page(ctest_t *t) {
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, MD5), URL_OK);
    is_str(t, "https://annas-archive.gl/md5/" MD5S);
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_build_md5_page(&B, LIT("a.b"), LIT("d41d8cd98f00b204e9800998ecf8427e")), URL_OK);
    is_str(t, "https://a.b/md5/d41d8cd98f00b204e9800998ecf8427e");
}

static void t_md5_page_rejections(ctest_t *t) {
    prep();
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, LIT("0123456789ABCDEF0123456789ABCDEF")), URL_E_INVALID);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, LIT("0123456789abcdef0123456789abcde")), URL_E_INVALID);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, LIT("0123456789abcdef0123456789abcdef/")), URL_E_INVALID);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, LIT("")), URL_E_INVALID);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_md5_page(&B, LIT("x/y"), MD5), URL_E_INVALID);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_md5_page(&B, EMPTY, MD5), URL_E_INVALID);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, (core_str_t){NULL, 1}), URL_E_ARG);
    untouched(t);
    CHECK_EQ_I64(t, url_build_md5_page(NULL, BASE, MD5), URL_E_ARG);
    prep();
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, MD5), URL_OK);   /* control */
    CHECK_EQ_STR(t, core_buf_cstr(&B), "pre" "https://annas-archive.gl/md5/" MD5S);
}

static void scidb_is(ctest_t *t, core_str_t doi, const char *path_ex, const char *dl_ex) {
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_build_scidb_path(&B, BASE, doi), URL_OK);
    is_str(t, path_ex);
    fresh(sizeof store);
    CHECK_EQ_I64(t, url_build_scidb_download(&B, BASE, doi), URL_OK);
    is_str(t, dl_ex);
}

static void t_scidb_urls(ctest_t *t) {
    scidb_is(t, LIT("10.1000/xyz123"), "https://annas-archive.gl/scidb/10.1000/xyz123",
             "https://annas-archive.gl/scidb?doi=10.1000%2Fxyz123");
    scidb_is(t, LIT("10.1038/nature12373"), "https://annas-archive.gl/scidb/10.1038/nature12373",
             "https://annas-archive.gl/scidb?doi=10.1038%2Fnature12373");
    scidb_is(t, LIT("10.1/a/b/c"), "https://annas-archive.gl/scidb/10.1/a/b/c",
             "https://annas-archive.gl/scidb?doi=10.1%2Fa%2Fb%2Fc");
    scidb_is(t, LIT("10."), "https://annas-archive.gl/scidb/10.", "https://annas-archive.gl/scidb?doi=10.");
}

static void t_scidb_special_characters(ctest_t *t) {
    scidb_is(t, LIT("10.1002/(SICI)1097-4571(199806)49:8<693::AID-ASI4>3.0.CO;2-O"),
             "https://annas-archive.gl/scidb/10.1002/%28SICI%291097-4571%28199806%2949%3A8%3C693%3A%3AAID-ASI4%3E3.0.CO%3B2-O",
             "https://annas-archive.gl/scidb?doi=10.1002%2F%28SICI%291097-4571%28199806%2949%3A8%3C693%3A%3AAID-ASI4%3E3.0.CO%3B2-O");
    scidb_is(t, LIT("10.1/a b"), "https://annas-archive.gl/scidb/10.1/a%20b",
             "https://annas-archive.gl/scidb?doi=10.1%2Fa%20b");
    scidb_is(t, LIT("10.1/a?b#c"), "https://annas-archive.gl/scidb/10.1/a%3Fb%23c",
             "https://annas-archive.gl/scidb?doi=10.1%2Fa%3Fb%23c");
    scidb_is(t, LIT("10.1/a&b=c"), "https://annas-archive.gl/scidb/10.1/a%26b%3Dc",
             "https://annas-archive.gl/scidb?doi=10.1%2Fa%26b%3Dc");
    scidb_is(t, LIT("10.1/x%41+y"), "https://annas-archive.gl/scidb/10.1/x%2541%2By",
             "https://annas-archive.gl/scidb?doi=10.1%2Fx%2541%2By");
    scidb_is(t, LIT("10.1/caf\xc3\xa9"), "https://annas-archive.gl/scidb/10.1/caf%C3%A9",
             "https://annas-archive.gl/scidb?doi=10.1%2Fcaf%C3%A9");
    scidb_is(t, LIT("10.1/x "), "https://annas-archive.gl/scidb/10.1/x%20",   /* only the prefix is checked */
             "https://annas-archive.gl/scidb?doi=10.1%2Fx%20");
    scidb_is(t, LIT("10.1/a\\b"), "https://annas-archive.gl/scidb/10.1/a%5Cb",
             "https://annas-archive.gl/scidb?doi=10.1%2Fa%5Cb");
}

static void t_scidb_rejections(ctest_t *t) {
    static const core_str_t bad[] = {
        {"", 0}, {"11.1/x", 6}, {" 10.1/x", 7}, {"\n10.1/x", 7}, {"doi:10.1/x", 10},
        {"10", 2}, {"1.0.1/x", 7}, {"10,1/x", 6}, {"/10.1/x", 7}, {"10x1/x", 6}, {"10/1", 4},
        {"https://doi.org/10.1/x", 22}, {"x10.1/y", 7}};
    size_t i;
    prep();
    CHECK_EQ_I64(t, url_build_scidb_path(&B, BASE, LIT("10.1/x")), URL_OK);   /* controls */
    prep();
    CHECK_EQ_I64(t, url_build_scidb_download(&B, BASE, LIT("10.1/x")), URL_OK);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        prep();
        CHECK_EQ_I64(t, url_build_scidb_path(&B, BASE, bad[i]), URL_E_INVALID);
        untouched(t);
        prep();
        CHECK_EQ_I64(t, url_build_scidb_download(&B, BASE, bad[i]), URL_E_INVALID);
        untouched(t);
    }
    prep();
    CHECK_EQ_I64(t, url_build_scidb_path(&B, LIT("Bad"), LIT("10.1/x")), URL_E_INVALID);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_scidb_download(&B, LIT("Bad"), LIT("10.1/x")), URL_E_INVALID);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_scidb_path(&B, BASE, (core_str_t){NULL, 3}), URL_E_ARG);
    untouched(t);
    prep();
    CHECK_EQ_I64(t, url_build_scidb_download(&B, BASE, (core_str_t){NULL, 3}), URL_E_ARG);
    untouched(t);
    CHECK_EQ_I64(t, url_build_scidb_path(NULL, BASE, LIT("10.1/x")), URL_E_ARG);
    CHECK_EQ_I64(t, url_build_scidb_download(NULL, BASE, LIT("10.1/x")), URL_E_ARG);
}

/* Builds with capacity len+1 (must succeed) and len (must overflow) for each builder. */
static void t_capacity_boundaries_of_every_builder(ctest_t *t) {
    static const char s1[] = "https://annas-archive.gl/search?q=a%20b&content=journal";
    static const char s2[] = "https://annas-archive.gl/dyn/api/fast_download.json?md5=" MD5S "&key=k%20";
    static const char s3[] = "https://annas-archive.gl/md5/" MD5S;
    static const char s4[] = "https://annas-archive.gl/scidb/10.1/a%20b";
    static const char s5[] = "https://annas-archive.gl/scidb?doi=10.1%2Fa%20b";
    size_t n;
    for (n = 0; n < 2; n++) {
        size_t d = n;   /* d == 0: exactly fits (cap = len + 1); d == 1: one byte short */
        fresh(sizeof s1 - d);
        CHECK_EQ_I64(t, url_build_search(&B, BASE, LIT("a b"), URL_CONTENT_JOURNAL), d ? URL_E_OVERFLOW : URL_OK);
        CHECK_EQ_I64(t, core_buf_overflow(&B), (int)d);
        fresh(sizeof s2 - d);
        CHECK_EQ_I64(t, url_build_fast_download(&B, BASE, MD5, LIT("k ")), d ? URL_E_OVERFLOW : URL_OK);
        CHECK_EQ_I64(t, core_buf_overflow(&B), (int)d);
        fresh(sizeof s3 - d);
        CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, MD5), d ? URL_E_OVERFLOW : URL_OK);
        CHECK_EQ_I64(t, core_buf_overflow(&B), (int)d);
        fresh(sizeof s4 - d);
        CHECK_EQ_I64(t, url_build_scidb_path(&B, BASE, LIT("10.1/a b")), d ? URL_E_OVERFLOW : URL_OK);
        CHECK_EQ_I64(t, core_buf_overflow(&B), (int)d);
        fresh(sizeof s5 - d);
        CHECK_EQ_I64(t, url_build_scidb_download(&B, BASE, LIT("10.1/a b")), d ? URL_E_OVERFLOW : URL_OK);
        CHECK_EQ_I64(t, core_buf_overflow(&B), (int)d);
    }
    fresh(sizeof s3);
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, MD5), URL_OK);
    CHECK_EQ_STR(t, core_buf_cstr(&B), s3);
    fresh(0);
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, MD5), URL_E_OVERFLOW);
}

static void t_invalid_beats_overflow_and_sticky_overflow(ctest_t *t) {
    fresh(8);
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, LIT("nothex")), URL_E_INVALID);
    CHECK(t, !core_buf_overflow(&B));
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, MD5), URL_E_OVERFLOW);   /* control: valid input overflows */
    CHECK(t, core_buf_overflow(&B));
    fresh(2);
    core_buf_put_str(&B, LIT("toolong"));
    CHECK(t, core_buf_overflow(&B));
    CHECK_EQ_I64(t, url_build_search(&B, BASE, LIT("q"), URL_CONTENT_BOOK), URL_E_OVERFLOW);
}

static void t_builders_append_after_existing_text(ctest_t *t) {
    fresh(sizeof store);
    core_buf_put_str(&B, LIT("GET "));
    CHECK_EQ_I64(t, url_build_search(&B, BASE, LIT("a"), URL_CONTENT_BOOK), URL_OK);
    CHECK_EQ_I64(t, url_build_md5_page(&B, BASE, MD5), URL_OK);
    is_str(t, "GET https://annas-archive.gl/search?q=a&content=book_anyhttps://annas-archive.gl/md5/" MD5S);
}

/* ---- NULL base (R004 T3). url.h lines 12-14: `ptr == NULL` with `len > 0` is URL_E_ARG, nothing is
 * appended, and URL_E_ARG takes precedence over URL_E_INVALID. */
#define NULLBASE ((core_str_t){NULL, 5})
#define PAT 0x5A

static char snap[sizeof store];

/* Fill with a known pattern, init the buffer, then snapshot the whole store (init may write its own NUL). */
static void pattern(void) {
    memset(store, PAT, sizeof store);
    core_buf_init(&B, store, sizeof store);
    memcpy(snap, store, sizeof store);
}

/* Every byte of the store, including past the would-be end, equals the snapshot. */
static void pattern_intact(ctest_t *t) {
    CHECK_EQ_MEM(t, store, sizeof store, snap, sizeof snap);
    CHECK_EQ_U64(t, core_buf_len(&B), 0);
    CHECK(t, !core_buf_overflow(&B));
}

static void t_fast_download_null_base(ctest_t *t) {
    pattern();
    CHECK_EQ_I64(t, url_build_fast_download(&B, NULLBASE, MD5, LIT("k")), URL_E_ARG);
    pattern_intact(t);
    pattern();
    CHECK_EQ_I64(t, url_build_fast_download(&B, NULLBASE, LIT("BAD"), LIT("k")), URL_E_ARG);
    pattern_intact(t);
    pattern();
    CHECK_EQ_I64(t, url_build_fast_download(&B, NULLBASE, MD5, EMPTY), URL_E_ARG);   /* invalid key too */
    pattern_intact(t);
    /* Empty base: url.h "`base` must be a normalised domain (url_base_is_valid)" and
     * url_base_is_valid is false for empty, so URL_E_INVALID. */
    pattern();
    CHECK_EQ_I64(t, url_build_fast_download(&B, EMPTY, MD5, LIT("k")), URL_E_INVALID);
    pattern_intact(t);
}

static void t_md5_page_null_base(ctest_t *t) {
    pattern();
    CHECK_EQ_I64(t, url_build_md5_page(&B, NULLBASE, MD5), URL_E_ARG);
    pattern_intact(t);
    pattern();
    CHECK_EQ_I64(t, url_build_md5_page(&B, NULLBASE, LIT("BAD")), URL_E_ARG);
    pattern_intact(t);
    pattern();
    CHECK_EQ_I64(t, url_build_md5_page(&B, EMPTY, MD5), URL_E_INVALID);   /* url.h: base must be url_base_is_valid */
    pattern_intact(t);
}

static void t_scidb_path_null_base(ctest_t *t) {
    pattern();
    CHECK_EQ_I64(t, url_build_scidb_path(&B, NULLBASE, LIT("10.1/x")), URL_E_ARG);
    pattern_intact(t);
    pattern();
    CHECK_EQ_I64(t, url_build_scidb_path(&B, NULLBASE, LIT("nope")), URL_E_ARG);
    pattern_intact(t);
    pattern();
    CHECK_EQ_I64(t, url_build_scidb_path(&B, EMPTY, LIT("10.1/x")), URL_E_INVALID);   /* url.h: base must be valid */
    pattern_intact(t);
}

static void t_scidb_download_null_base(ctest_t *t) {
    pattern();
    CHECK_EQ_I64(t, url_build_scidb_download(&B, NULLBASE, LIT("10.1/x")), URL_E_ARG);
    pattern_intact(t);
    pattern();
    CHECK_EQ_I64(t, url_build_scidb_download(&B, NULLBASE, LIT("nope")), URL_E_ARG);
    pattern_intact(t);
    pattern();
    CHECK_EQ_I64(t, url_build_scidb_download(&B, EMPTY, LIT("10.1/x")), URL_E_INVALID);   /* url.h: base must be valid */
    pattern_intact(t);
}

CTEST_SUITE("url_build",
    {"search_book", t_search_book},
    {"search_journal", t_search_journal},
    {"search_other_base", t_search_other_base},
    {"search_query_is_encoded", t_search_query_is_encoded},
    {"search_query_cannot_inject_parameters", t_search_query_cannot_inject_parameters},
    {"search_empty_query", t_search_empty_query},
    {"search_rejections_have_positive_control", t_search_rejections_have_positive_control},
    {"fast_download_exact", t_fast_download_exact},
    {"fast_download_key_is_encoded", t_fast_download_key_is_encoded},
    {"fast_download_rejections", t_fast_download_rejections},
    {"md5_page", t_md5_page},
    {"md5_page_rejections", t_md5_page_rejections},
    {"scidb_urls", t_scidb_urls},
    {"scidb_special_characters", t_scidb_special_characters},
    {"scidb_rejections", t_scidb_rejections},
    {"capacity_boundaries_of_every_builder", t_capacity_boundaries_of_every_builder},
    {"invalid_beats_overflow_and_sticky_overflow", t_invalid_beats_overflow_and_sticky_overflow},
    {"builders_append_after_existing_text", t_builders_append_after_existing_text},
    {"fast_download_null_base", t_fast_download_null_base},
    {"md5_page_null_base", t_md5_page_null_base},
    {"scidb_path_null_base", t_scidb_path_null_base},
    {"scidb_download_null_base", t_scidb_download_null_base})
