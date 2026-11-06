/* annas_download: bodies far larger than the workspace stream through the fs port without being buffered whole. */
#include <stdio.h>
#include <string.h>

#include "annas/annas.h"
#include "annas/tests/kit.h"
#include "ctest/ctest.h"

#define BIG ((size_t)600000)
#define DLURL "https://dl.example.org/f/1?x=1"

static unsigned char g_body[BIG];
static char g_chunked[BIG + BIG / 10000u * 16u + 64u];
static _Alignas(16) unsigned char g_fs_big[8u * (BIG + 1024u)];
static unsigned char g_fs_small[8u * 100000u];

static void fill(void) {
    for (size_t i = 0; i < BIG; i++) {
        g_body[i] = (unsigned char)((i * 2654435761u) >> 13);
    }
}

static void api_and_get(kit_t *k) {
    kit_api(k, KIT_MD5, "", kit_http(k, 200, "OK", "", kit_ok_json(k, DLURL, "{\"downloads_left\":2}")));
    kit_connect(k, "dl.example.org", 443);
    kit_expect_get(k, "dl.example.org", 443, "/f/1?x=1");
}

static annas_code_t run(kit_t *k) {
    annas_request_t r = kit_req(KIT_MD5);
    return kit_download(k, &r);
}

static void t_large_body_in_mtu_sized_reads(ctest_t *t) {
    fill();
    kit_t *k = kit_begin();
    kit_use_fs(k, g_fs_big, sizeof g_fs_big);
    api_and_get(k);
    kit_respond(k, kit_head(k, 200, "OK", "", BIG), 0);
    kit_respond_n(k, (const char *)g_body, BIG, 1400);
    CHECK(t, BIG > ANNAS_WORKSPACE_MIN * 2u);
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, KIT_MD5 ".bin", g_body, BIG);
    CHECK_EQ_U64(t, k->dl.bytes, BIG);
}

static void t_large_body_in_odd_reads(ctest_t *t) {
    static const size_t chunks[] = {1u << 16, 4097, 0};
    fill();
    for (size_t i = 0; i < sizeof chunks / sizeof chunks[0]; i++) {
        kit_t *k = kit_begin();
        kit_use_fs(k, g_fs_big, sizeof g_fs_big);
        api_and_get(k);
        kit_respond(k, kit_head(k, 200, "OK", "", BIG), 0);
        kit_respond_n(k, (const char *)g_body, BIG, chunks[i]);
        kit_check_ok(t, k, run(k));
        kit_check_file(t, k, KIT_MD5 ".bin", g_body, BIG);
    }
}

static size_t build_chunked(size_t piece) {
    size_t n = (size_t)snprintf(g_chunked, 64, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    for (size_t off = 0; off < BIG; off += piece) {
        size_t len = BIG - off < piece ? BIG - off : piece;
        n += (size_t)snprintf(g_chunked + n, 32, "%zx\r\n", len);
        memcpy(g_chunked + n, g_body + off, len);
        n += len;
        g_chunked[n++] = '\r';
        g_chunked[n++] = '\n';
    }
    memcpy(g_chunked + n, "0\r\n\r\n", 5);
    return n + 5u;
}

static void t_large_chunked_body(ctest_t *t) {
    fill();
    kit_t *k = kit_begin();
    kit_use_fs(k, g_fs_big, sizeof g_fs_big);
    api_and_get(k);
    size_t n = build_chunked(10000);
    kit_respond_n(k, g_chunked, n, 1400);
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, KIT_MD5 ".bin", g_body, BIG);
}

static void t_large_close_delimited_body(ctest_t *t) {
    fill();
    kit_t *k = kit_begin();
    kit_use_fs(k, g_fs_big, sizeof g_fs_big);
    api_and_get(k);
    kit_respond(k, "HTTP/1.1 200 OK\r\n\r\n", 0);
    kit_respond_n(k, (const char *)g_body, BIG, 3000);
    kit_eof(k);
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, KIT_MD5 ".bin", g_body, BIG);
}

static void t_a_full_disk_mid_stream_leaves_nothing(ctest_t *t) {
    fill();
    kit_t *k = kit_begin();
    kit_use_fs(k, g_fs_small, sizeof g_fs_small); /* 100000 bytes per file */
    api_and_get(k);
    kit_respond(k, kit_head(k, 200, "OK", "", BIG), 0);
    kit_respond_n(k, (const char *)g_body, BIG, 1400);
    k->allow_unused = true;
    kit_check_err(t, k, run(k), ANNAS_E_FS, 0, "could not write the file");
    kit_check_no_partial(t, k, 1);
    k = kit_begin(); /* control: a body of exactly the capacity fits */
    kit_use_fs(k, g_fs_small, sizeof g_fs_small);
    api_and_get(k);
    kit_respond(k, kit_head(k, 200, "OK", "", 100000), 0);
    kit_respond_n(k, (const char *)g_body, 100000, 1400);
    kit_check_ok(t, k, run(k));
    kit_check_file(t, k, KIT_MD5 ".bin", g_body, 100000);
}

static void t_a_connection_lost_mid_stream_leaves_nothing(ctest_t *t) {
    fill();
    kit_t *k = kit_begin();
    kit_use_fs(k, g_fs_big, sizeof g_fs_big);
    api_and_get(k);
    kit_respond(k, kit_head(k, 200, "OK", "", BIG), 0);
    kit_respond_n(k, (const char *)g_body, BIG - 1u, 1400); /* one byte short */
    kit_eof(k);
    kit_check_err(t, k, run(k), ANNAS_E_NET, 0, "connection closed before the response was complete");
    kit_check_no_partial(t, k, 1);
}

CTEST_SUITE("download_big",
    {"large_body_in_mtu_sized_reads", t_large_body_in_mtu_sized_reads},
    {"large_body_in_odd_reads", t_large_body_in_odd_reads},
    {"large_chunked_body", t_large_chunked_body},
    {"large_close_delimited_body", t_large_close_delimited_body},
    {"a_full_disk_mid_stream_leaves_nothing", t_a_full_disk_mid_stream_leaves_nothing},
    {"a_connection_lost_mid_stream_leaves_nothing", t_a_connection_lost_mid_stream_leaves_nothing})
