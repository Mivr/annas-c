/* Tests for url/url.h: inputs that are slices of a longer buffer, and inputs that end exactly at the end of
 * readable memory. The functions must look only at the `len` bytes they are given (header: "`in`", "`s`",
 * "`raw`" are slices). Public header only. */
#define _DEFAULT_SOURCE 1
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ctest/ctest.h"
#include "url/url.h"

static char store[256];
static core_buf_t B;

static void fresh(void) {
    memset(store, 0xA5, sizeof store);
    core_buf_init(&B, store, sizeof store);
}

/* A readable copy of `s` (len bytes) that ends exactly at a page boundary; the next page is inaccessible, so
 * a read of one byte past the slice is a fault. Returns NULL when the mapping is not available. */
static const char *guarded(const char *s, size_t len) {
    long page = sysconf(_SC_PAGESIZE);
    char *map;
    if (page <= 0 || len > (size_t)page) {
        return NULL;
    }
    map = mmap(NULL, 2U * (size_t)page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED) {
        return NULL;
    }
    if (mprotect(map + page, (size_t)page, PROT_NONE) != 0) {
        return NULL;
    }
    memcpy(map + page - (long)len, s, len);
    return map + page - (long)len;
}

#define G(t, var, lit)                                  \
    const char *var = guarded((lit), sizeof(lit) - 1U); \
    CHECK(t, var != NULL);                              \
    if (var == NULL) {                                  \
        return;                                         \
    }

/* ---- slices of a longer buffer ---------------------------------------------------------------------- */

static void t_decode_percent_cut_by_slice_end(ctest_t *t) {
    const char *whole = "ab%41";   /* the bytes after the slice would complete a valid escape */
    fresh();
    CHECK_EQ_I64(t, url_pct_decode(&B, (core_str_t){whole, 4}), URL_E_INVALID);   /* "ab%4" */
    CHECK_EQ_U64(t, core_buf_len(&B), 0);
    CHECK_EQ_I64(t, url_pct_decode(&B, (core_str_t){whole, 3}), URL_E_INVALID);   /* "ab%" */
    CHECK_EQ_U64(t, core_buf_len(&B), 0);
    CHECK_EQ_I64(t, url_pct_decode(&B, (core_str_t){whole, 5}), URL_OK);
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), "abA", 3);
}

static void t_redact_key_cut_by_slice_end(ctest_t *t) {
    const char *whole = "&key=SECRET";
    fresh();
    CHECK_EQ_I64(t, url_redact_key(&B, (core_str_t){whole, 4}), URL_OK);   /* "&key": no '=' inside the slice */
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), "&key", 4);
    fresh();
    CHECK_EQ_I64(t, url_redact_key(&B, (core_str_t){whole, 3}), URL_OK);   /* "&ke" */
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), "&ke", 3);
    fresh();
    CHECK_EQ_I64(t, url_redact_key(&B, (core_str_t){whole, 5}), URL_OK);   /* "&key=" */
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), "&key=REDACTED", 13);
    fresh();
    CHECK_EQ_I64(t, url_redact_key(&B, (core_str_t){whole, 8}), URL_OK);   /* "&key=SEC": value ends with the slice */
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), "&key=REDACTED", 13);
}

static void t_md5_validity_ignores_bytes_after_the_slice(ctest_t *t) {
    static const char tail[] = "0123456789abcdef0123456789abcdef";
    const char *const after[] = {"A", "F", "a", "G", "f", "0", "%"};
    char buf[34];
    char out[URL_MD5_BUF];
    size_t i;
    for (i = 0; i < sizeof after / sizeof after[0]; i++) {
        memcpy(buf, tail, 32);
        buf[32] = after[i][0];
        buf[33] = '\0';
        CHECK(t, url_md5_is_valid((core_str_t){buf, 32}));
        CHECK(t, url_md5_normalize((core_str_t){buf, 32}, out));
        CHECK_EQ_MEM(t, out, 32, tail, 32);
        CHECK_EQ_U64(t, (unsigned char)out[32], 0);
    }
}

static void t_md5_normalize_writes_exactly_33_bytes(ctest_t *t) {
    char out[URL_MD5_BUF + 2U];
    memset(out, 0x5A, sizeof out);
    CHECK(t, url_md5_normalize((core_str_t){"0123456789ABCDEF0123456789abcdef", 32}, out));
    CHECK_EQ_MEM(t, out, 32, "0123456789abcdef0123456789abcdef", 32);
    CHECK_EQ_U64(t, (unsigned char)out[32], 0);
    CHECK_EQ_U64(t, (unsigned char)out[33], 0x5A);
    CHECK_EQ_U64(t, (unsigned char)out[34], 0x5A);
}

/* ---- inputs that end at the end of readable memory ------------------------------------------------------ */

static void t_base_scheme_only_at_end_of_memory(ctest_t *t) {
    G(t, https, "https://");
    G(t, http, "http://");
    G(t, slash, "/");
    G(t, schemeslash, "https:///");
    fresh();
    CHECK_EQ_I64(t, url_base_normalize((core_str_t){https, 8}, &B), URL_E_INVALID);
    CHECK_EQ_I64(t, url_base_normalize((core_str_t){http, 7}, &B), URL_E_INVALID);
    CHECK_EQ_I64(t, url_base_normalize((core_str_t){slash, 1}, &B), URL_E_INVALID);
    CHECK_EQ_I64(t, url_base_normalize((core_str_t){schemeslash, 9}, &B), URL_E_INVALID);
    CHECK_EQ_U64(t, core_buf_len(&B), 0);
}

static void t_base_normalize_at_end_of_memory(ctest_t *t) {
    G(t, a, "https://x.org/");
    G(t, b, "x.org");
    fresh();
    CHECK_EQ_I64(t, url_base_normalize((core_str_t){a, 14}, &B), URL_OK);
    CHECK_EQ_I64(t, url_base_normalize((core_str_t){b, 5}, &B), URL_OK);
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), "x.orgx.org", 10);
}

static void t_redact_key_value_ends_at_end_of_memory(ctest_t *t) {
    G(t, a, "key=SECRET");
    G(t, b, "a&key=");
    G(t, c, "key=");
    fresh();
    CHECK_EQ_I64(t, url_redact_key(&B, (core_str_t){a, 10}), URL_OK);
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), "key=REDACTED", 12);
    fresh();
    CHECK_EQ_I64(t, url_redact_key(&B, (core_str_t){b, 6}), URL_OK);
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), "a&key=REDACTED", 14);
    fresh();
    CHECK_EQ_I64(t, url_redact_key(&B, (core_str_t){c, 4}), URL_OK);
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), "key=REDACTED", 12);
}

static void t_redact_near_miss_at_end_of_memory(ctest_t *t) {
    G(t, a, "&key");
    G(t, b, "x key");
    fresh();
    CHECK_EQ_I64(t, url_redact_key(&B, (core_str_t){a, 4}), URL_OK);
    CHECK_EQ_I64(t, url_redact_key(&B, (core_str_t){b, 5}), URL_OK);
    CHECK_EQ_MEM(t, core_buf_cstr(&B), core_buf_len(&B), "&keyx key", 9);
}

static void t_md5_at_end_of_memory(ctest_t *t) {
    static const char hex[] = "0123456789abcdef0123456789abcdef";
    char out[URL_MD5_BUF];
    G(t, m, "0123456789abcdef0123456789abcdef");
    CHECK(t, url_md5_is_valid((core_str_t){m, 32}));
    CHECK(t, url_md5_normalize((core_str_t){m, 32}, out));
    CHECK_EQ_MEM(t, out, 32, hex, 32);
}

CTEST_SUITE("url_edge",
    {"decode_percent_cut_by_slice_end", t_decode_percent_cut_by_slice_end},
    {"redact_key_cut_by_slice_end", t_redact_key_cut_by_slice_end},
    {"md5_validity_ignores_bytes_after_the_slice", t_md5_validity_ignores_bytes_after_the_slice},
    {"md5_normalize_writes_exactly_33_bytes", t_md5_normalize_writes_exactly_33_bytes},
    {"base_scheme_only_at_end_of_memory", t_base_scheme_only_at_end_of_memory},
    {"base_normalize_at_end_of_memory", t_base_normalize_at_end_of_memory},
    {"redact_key_value_ends_at_end_of_memory", t_redact_key_value_ends_at_end_of_memory},
    {"redact_near_miss_at_end_of_memory", t_redact_near_miss_at_end_of_memory},
    {"md5_at_end_of_memory", t_md5_at_end_of_memory})
