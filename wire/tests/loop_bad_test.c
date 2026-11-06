/* wire: the MCP loop must never hang, crash or die on bad input: binary noise, NUL bytes, bad UTF-8, huge and deeply
 * nested JSON, floods of blank lines, mutated requests. Every scenario ends with a good request that must still be
 * answered (the positive control that the loop is alive), every reply line is a well-formed JSON-RPC response on one
 * line, nothing but replies is ever written to stdout, stderr stays empty, the exit code is 0, the key never shows. */
#include <string.h>

#include "ctest/ctest.h"
#include "wire/tests/kit.h"

#define PING_LINE KW_PING_REQ "\n"
#define KEYED(s) "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"" s "\"}\n"

static unsigned char g_noise[200000];
static char g_big[400000];
static unsigned g_seed;

static void base(void) {
    kw_reset();
    kw_net_env();
}

static unsigned rnd(void) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

/* The loop ran to the end of input, answered the final ping last, and printed only well-formed one-line replies. */
static void check_alive(ctest_t *t, int rc, size_t min_lines) {
    CHECK_EQ_I64(t, rc, 0);
    CHECK_EQ_U64(t, kw_err_len(), 0);
    size_t lines = kw_check_reply_lines(t);
    CHECK(t, lines >= min_lines);
    CHECK(t, lines >= 1u);
    CHECK_EQ_STR(t, kw_out_line(lines - 1u), KW_PING_REPLY);
    kw_check_no_key(t, KW_KEY);
    kw_check_no_empty_writes(t);
}

static void t_binary_noise(ctest_t *t) {
    g_seed = 12345u;
    for (size_t i = 0; i < sizeof g_noise; i++) {
        g_noise[i] = (unsigned char)rnd();
    }
    base();
    kw_in_n((const char *)g_noise, sizeof g_noise);
    kw_in("\n" PING_LINE);
    check_alive(t, KW_MCP0(), 2);
    base(); /* the same noise one byte at a time */
    kw_in_n((const char *)g_noise, 4000);
    kw_in("\n" PING_LINE);
    kw()->in_chunk = 1;
    check_alive(t, KW_MCP0(), 2);
}

static void t_noise_with_many_newlines(ctest_t *t) {
    g_seed = 777u;
    for (size_t i = 0; i < 40000u; i++) {
        unsigned r = rnd();
        g_noise[i] = (r % 7u == 0u) ? (unsigned char)'\n' : (unsigned char)(r >> 3);
    }
    base();
    kw_in_n((const char *)g_noise, 40000u);
    kw_in("\n" PING_LINE);
    check_alive(t, KW_MCP0(), 2);
    CHECK_EQ_U64(t, kw()->w_empty, 0);
}

static void t_nul_bytes(ctest_t *t) {
    base();
    static const char nuls[] = "\0\0\0\n{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"pi\0ng\"}\n\0";
    kw_in_n(nuls, sizeof nuls - 1u);
    kw_in("\n" PING_LINE);
    check_alive(t, KW_MCP0(), 3);
}

static void t_invalid_utf8_and_lone_cr(ctest_t *t) {
    base();
    kw_in("{\"jsonrpc\":\"2.0\",\"id\":\"\xff\xfe\",\"method\":\"ping\"}\n"
          "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"\xc0\xaf\"}\n"
          "\xef\xbb\xbf{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n"
          "\r\r\r\n\r" PING_LINE);
    check_alive(t, KW_MCP0(), 3);
}

static void t_deep_nesting(ctest_t *t) {
    base();
    memset(g_big, '[', 100000);
    g_big[100000] = '\n';
    kw_in_n(g_big, 100001);
    memset(g_big, '{', 100000);
    memcpy(g_big, "{\"a\":", 5);
    g_big[100000] = '\n';
    kw_in_n(g_big, 100001);
    kw_in(PING_LINE);
    check_alive(t, KW_MCP0(), 3);
}

static void t_huge_arrays_and_numbers(ctest_t *t) {
    base();
    g_big[0] = '[';
    size_t n = 1;
    while (n + 3 < sizeof g_big) {
        g_big[n++] = '1';
        g_big[n++] = ',';
    }
    g_big[n++] = '1';
    g_big[n++] = ']';
    g_big[n++] = '\n';
    kw_in_n(g_big, n);
    memcpy(g_big, "{\"jsonrpc\":\"2.0\",\"id\":", 22);
    n = 22;
    memset(g_big + n, '9', 300000);
    n += 300000;
    memcpy(g_big + n, ",\"method\":\"ping\"}\n", 18);
    n += 18;
    kw_in_n(g_big, n);
    kw_in(PING_LINE);
    check_alive(t, KW_MCP0(), 3);
}

static void t_a_flood_of_blank_lines(ctest_t *t) {
    base();
    kw_in_fill('\n', 300000);
    kw_in_fill('\r', 70000);
    kw_in(PING_LINE);
    kw_in_fill(' ', 100000);
    kw_in("\n");
    check_alive(t, KW_MCP0(), 1);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), 1); /* nothing was answered but the ping */
}

static void t_many_requests(ctest_t *t) {
    static const char one[] = KW_PING_REQ "\n";
    base();
    size_t n = 0;
    while (n + sizeof one < sizeof g_big) {
        memcpy(g_big + n, one, sizeof one - 1u);
        n += sizeof one - 1u;
    }
    kw_in_n(g_big, n);
    CHECK_EQ_I64(t, KW_MCP0(), 0);
    CHECK_EQ_U64(t, kw_check_reply_lines(t), n / (sizeof one - 1u));
    CHECK_EQ_U64(t, kw_out_len(), n / (sizeof one - 1u) * (sizeof KW_PING_REPLY));
}

static void t_wrong_shapes(ctest_t *t) {
    base();
    kw_in("null\n1\ntrue\n\"x\"\n[]\n[1,2]\n{}\n{\"id\":1}\n{\"jsonrpc\":2,\"id\":1,\"method\":\"ping\"}\n"
          "{\"jsonrpc\":\"2.0\",\"id\":{},\"method\":\"ping\"}\n{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":7}\n"
          "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\"}\n"
          "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":[]}\n"
          "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":1}}\n"
          "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"book_download\","
          "\"arguments\":{\"hash\":[1],\"hash\":2}}}\n"
          "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}\n" PING_LINE);
    check_alive(t, KW_MCP0(), 10);
}

static void t_the_key_in_garbage(ctest_t *t) {
    base();
    kw_in(KEYED(KW_KEY) "{\"jsonrpc\":\"2.0\",\"id\":\"" KW_KEY "\",\"method\":\"x\"}\n"
          "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"book_download\","
          "\"arguments\":{\"hash\":\"" KW_KEY "\",\"title\":\"" KW_KEY "\",\"" KW_KEY "\":1}}}\n" KW_KEY "\n" PING_LINE);
    check_alive(t, KW_MCP0(), 4);
    CHECK(t, kw_contains(kw_out(), "REDACTED"));
}

/* Mutations of valid requests: flip, drop, duplicate and insert bytes; the tool-call template may even become a valid
 * call, which then fails against the unparsable CA of the kit ("TLS error") without any network. */
static const char k_templates[][320] = {
    KW_INIT_REQ, KW_PING_REQ,
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}",
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"fast_download_url\","
    "\"arguments\":{\"hash\":\"" KW_MD5 "\",\"path_index\":1,\"timeout_seconds\":5}}}",
    "{\"jsonrpc\":\"2.0\",\"id\":\"s\",\"method\":\"tools/call\",\"params\":{\"name\":\"book_download\","
    "\"arguments\":{\"hash\":\"" KW_MD5 "\",\"title\":\"t\",\"format\":\"pdf\"}}}"};

static size_t mutate(char *dst, size_t cap) {
    const char *src = k_templates[rnd() % (sizeof k_templates / sizeof k_templates[0])];
    size_t n = strlen(src);
    memcpy(dst, src, n + 1u);
    unsigned edits = rnd() % 4u;
    for (unsigned e = 0; e < edits && n > 2u; e++) {
        size_t at = rnd() % n;
        unsigned kind = rnd() % 4u;
        if (kind == 0u) {
            dst[at] = (char)(rnd() & 0xffu);
        } else if (kind == 1u) {
            memmove(dst + at, dst + at + 1u, n - at);
            n--;
        } else if (kind == 2u && n + 2u < cap) {
            memmove(dst + at + 1u, dst + at, n - at + 1u);
            n++;
        } else {
            n = at;
            dst[n] = '\0';
        }
    }
    for (size_t i = 0; i < n; i++) {
        if (dst[i] == '\n') {
            dst[i] = ' ';
        }
    }
    dst[n++] = '\n';
    return n;
}

static void t_mutated_requests(ctest_t *t) {
    for (unsigned round = 0; round < 6u; round++) {
        g_seed = 4242u + round * 977u;
        size_t n = 0;
        for (unsigned i = 0; i < 400u && n + 400u < sizeof g_big - 64u; i++) {
            n += mutate(g_big + n, 380u);
        }
        base();
        kw_in_n(g_big, n);
        kw_in(PING_LINE);
        kw()->in_chunk = round == 0u ? 1u : (round * 13u);
        int rc = KW_MCP0();
        check_alive(t, rc, 100);
        CHECK_EQ_U64(t, kw()->refused, 0); /* the unparsable bundle stops every connect before the transport */
    }
}

CTEST_SUITE("wire_loop_bad", {"binary_noise", t_binary_noise}, {"noise_with_many_newlines", t_noise_with_many_newlines},
            {"nul_bytes", t_nul_bytes}, {"invalid_utf8_and_lone_cr", t_invalid_utf8_and_lone_cr},
            {"deep_nesting", t_deep_nesting}, {"huge_arrays_and_numbers", t_huge_arrays_and_numbers},
            {"a_flood_of_blank_lines", t_a_flood_of_blank_lines}, {"many_requests", t_many_requests},
            {"wrong_shapes", t_wrong_shapes}, {"the_key_in_garbage", t_the_key_in_garbage},
            {"mutated_requests", t_mutated_requests})
