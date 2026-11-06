/* Worst-case complexity tests for http/http.h. Public header only.
 *
 * There are no wall-clock assertions: the ctest 10 s per-test timeout is the only clock. Each test runs its
 * input to completion and asserts the exact result. A linear implementation finishes every test here in well
 * under 1 s, even under `san`.
 *
 * The tests marked QUADRATIC target http_redirect_resolve's dot-segment removal, which is O(n^2) in the
 * length n of the path when a ".." is present (each surviving segment re-scans the rest of the path).
 * http_redirect_resolve documents no cap on `location` (the parser caps a header line at 8192 bytes, but the
 * function takes any slice), and at 8192 bytes n^2/4 is only 1.7e7 steps, harmless. So these tests use
 * n about 500000, which is what a caller that resolves a Location from any other source would hand in:
 * the O(n^2) inner loop then needs n^2/4 = 6e10 byte steps (>= 20 s even at 0.3 ns a step), while a linear
 * resolver does about 3n = 1.5e6 steps. The other tests exercise the parser, URL and challenge entry points
 * at their documented caps and pass on a quadratic redirect resolver.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/buf.h"
#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define BASE "https://example.org"
#define BASE_LEN (sizeof BASE - 1)

static char g_loc[600000];
static char g_exp[600000 + 64];
static char g_store[1u << 21];
static char g_big[8u << 20];
static core_buf_t g_b;
static http_parser_t g_p;
static http_url_t g_cur;
static http_redirects_t g_r;

static core_str_t S(const char *p, size_t n) { return (core_str_t){p, n}; }

/* ---- redirects ---- */

static void resolve_setup(void) {
    (void)http_url_split(LIT("https://example.org/dir/sub/page?old=1"), &g_cur);
    memset(&g_r, 0, sizeof g_r);
    (void)core_buf_init(&g_b, g_store, sizeof g_store);
}

/* Resolves g_loc[0..n) (status 302) and compares with g_exp[0..en). */
static void expect(ctest_t *t, size_t n, size_t en) {
    resolve_setup();
    CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, S(g_loc, n), &g_b), HTTP_OK);
    CHECK_EQ_U64(t, g_r.count, 1);
    CHECK_EQ_U64(t, core_buf_len(&g_b), en);
    CHECK_EQ_MEM(t, core_buf_cstr(&g_b), core_buf_len(&g_b), g_exp, en);
}

static size_t put(char *d, size_t at, const char *s, size_t reps) {
    size_t l = strlen(s), i;
    for (i = 0; i < reps; i++) {
        memcpy(d + at, s, l);
        at += l;
    }
    return at;
}

static size_t exp_base(void) {
    memcpy(g_exp, BASE, BASE_LEN);
    return BASE_LEN;
}

/* QUADRATIC. "/" then 250000 x "a/" then "..": n = 500003. The ".." removes the last a. Expected path:
 * "/a" x 249999, then "/". n^2/4 = 6.3e10 steps for a quadratic resolver. */
#define K_A 250000u
static void t_redirect_climb(ctest_t *t) {
    size_t n = 0, en = exp_base();
    g_loc[n++] = '/';
    n = put(g_loc, n, "a/", K_A);
    n = put(g_loc, n, "..", 1);
    en = put(g_exp, en, "/a", K_A - 1);
    en = put(g_exp, en, "/", 1);
    expect(t, n, en);
}

/* QUADRATIC. 100000 x "/a/..": every a is cancelled by the following "..". n = 500000. Expected path "/". */
static void t_redirect_pairs(ctest_t *t) {
    size_t n = put(g_loc, 0, "/a/..", 100000u), en = exp_base();
    en = put(g_exp, en, "/", 1);
    expect(t, n, en);
}

/* QUADRATIC. "/p" then 500000 x "/" then "..": the p segment and 499999 empty segments, then "..", which
 * removes one empty segment. Expected path: "/p" followed by 499999 slashes. n = 500004. */
#define M_SLASH 500000u
static void t_redirect_empty_segments(ctest_t *t) {
    size_t n = put(g_loc, 0, "/p", 1), en = exp_base();
    n = put(g_loc, n, "/", M_SLASH);
    n = put(g_loc, n, "..", 1);
    en = put(g_exp, en, "/p", 1);
    en = put(g_exp, en, "/", M_SLASH - 1);
    expect(t, n, en);
}

/* QUADRATIC. 125000 x "/a" then 125000 x "/..": climbs all the way back to the root. n = 625000, which
 * needs a 600000-byte buffer: use 120000 pairs instead (n = 600000). Expected path "/". */
static void t_redirect_up_down(ctest_t *t) {
    size_t n = put(g_loc, 0, "/a", 120000u), en = exp_base();
    n = put(g_loc, n, "/..", 120000u);
    en = put(g_exp, en, "/", 1);
    expect(t, n, en);
}

/* QUADRATIC (the path is still walked in full when the output overflows). The "climb" shape into a
 * 64-byte buffer: HTTP_E_OVERFLOW, counter not incremented. */
static void t_redirect_overflow(ctest_t *t) {
    static char small[64];
    size_t n = 0;
    g_loc[n++] = '/';
    n = put(g_loc, n, "a/", K_A);
    n = put(g_loc, n, "..", 1);
    resolve_setup();
    CHECK(t, core_buf_init(&g_b, small, sizeof small));
    CHECK_EQ_I64(t, http_redirect_resolve(&g_r, &g_cur, 302, S(g_loc, n), &g_b), HTTP_E_OVERFLOW);
    CHECK_EQ_U64(t, g_r.count, 0);
    CHECK(t, core_buf_overflow(&g_b));
}

/* Linear on a quadratic resolver too (no ".." anywhere): 250000 x "./" is dropped entirely; n = 500001. */
static void t_redirect_dots(ctest_t *t) {
    size_t n = 0, en = exp_base();
    g_loc[n++] = '/';
    n = put(g_loc, n, "./", 250000u);
    en = put(g_exp, en, "/", 1);
    expect(t, n, en);
}

/* No dot segments, 500000 empty segments: kept as they are. */
static void t_redirect_slashes(ctest_t *t) {
    size_t n = put(g_loc, 0, "/p", 1), en = exp_base();
    n = put(g_loc, n, "/", M_SLASH);
    en = put(g_exp, en, "/p", 1);
    en = put(g_exp, en, "/", M_SLASH);
    expect(t, n, en);
}

/* A relative reference with 200000 x "../": climbing never goes above the root, the merged directory is
 * /dir/sub/, the query is the reference's, the fragment is dropped. n = 600000 - 12. */
static void t_redirect_relative_climb(ctest_t *t) {
    size_t n = put(g_loc, 0, "../", 199990u), en = exp_base();
    n = put(g_loc, n, "x?q=1#frag", 1);
    en = put(g_exp, en, "/x?q=1", 1);
    expect(t, n, en);
}

/* A 500000-byte query with a fragment, an absolute https form with a 500000-byte path of plain segments,
 * and a scheme-relative form with a port. */
static void t_redirect_long_tail(ctest_t *t) {
    size_t n = put(g_loc, 0, "/p?", 1), en = exp_base();
    n = put(g_loc, n, "q", 500000u);
    n = put(g_loc, n, "#", 1);
    n = put(g_loc, n, "f", 1000u);
    en = put(g_exp, en, "/p?", 1);
    en = put(g_exp, en, "q", 500000u);
    expect(t, n, en);
    n = put(g_loc, 0, "HTTPS://Other.Example:443/", 1);
    n = put(g_loc, n, "a", 500000u);
    en = put(g_exp, 0, "https://Other.Example/", 1);
    en = put(g_exp, en, "a", 500000u);
    expect(t, n, en);
    n = put(g_loc, 0, "//h.example:8443/a/./b/../c", 1);
    en = put(g_exp, 0, "https://h.example:8443/a/c", 1);
    expect(t, n, en);
}

/* ---- response parser ---- */

static http_event_t g_ev;

typedef struct {
    size_t headers, trailers, bodies, bytes, bad;
    bool end, hdr_end;
    http_framing_t framing;
} rec_t;

/* Expected header "X-Hnn: " + 8185 'v' (see build_headers), seen as "x-hnn" with a value of 8185 bytes. */
static bool header_ok(size_t idx) {
    char want[8];
    (void)snprintf(want, sizeof want, "x-h%02zu", idx);
    return g_ev.name.len == 5 && memcmp(g_ev.name.ptr, want, 5) == 0 && g_ev.value.len == 8185u &&
           g_ev.value.ptr[0] == 'v' && g_ev.value.ptr[8184] == 'v';
}

static void record(rec_t *r, bool pattern) {
    switch (g_ev.kind) {
    case HTTP_EV_HEADER:
        if (pattern && r->headers < 99u && !header_ok(r->headers)) {
            r->bad++;
        }
        r->headers++;
        break;
    case HTTP_EV_TRAILER: r->trailers++; break;
    case HTTP_EV_BODY:
        r->bodies++;
        r->bytes += g_ev.data.len;
        break;
    case HTTP_EV_HEADERS_END:
        r->hdr_end = true;
        r->framing = g_ev.framing;
        break;
    case HTTP_EV_END: r->end = true; break;
    default: break;
    }
}

/* Feeds d[0..n) `step` bytes per fresh input slice, pulling every event each slice yields. Returns the first
 * parser error, else HTTP_OK. */
static http_status_t drive(const char *d, size_t n, size_t step, rec_t *r, bool pattern) {
    size_t off = 0;
    memset(r, 0, sizeof *r);
    (void)http_parser_init(&g_p);
    while (off < n) {
        size_t len = n - off < step ? n - off : step;
        core_str_t in = S(d + off, len);
        for (;;) {
            http_status_t st = http_parser_next(&g_p, &in, &g_ev);
            if (st != HTTP_OK) {
                return st;
            }
            if (g_ev.kind == HTTP_EV_NONE) {
                break;
            }
            record(r, pattern);
        }
        off += len;
    }
    return HTTP_OK;
}

#define CHUNK_HEAD "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"

/* 1,000,000 minimal chunks "1\r\nX\r\n" (6 bytes each) = 6 MB in one input, then "0\r\n\r\n". The body bytes
 * are checked by the totals: 1000000 BODY events, 1000000 bytes, END. Linear: 6e6 byte steps. */
static void t_many_minimal_chunks(ctest_t *t) {
    size_t n = sizeof CHUNK_HEAD - 1, i;
    rec_t r;
    memcpy(g_big, CHUNK_HEAD, n);
    for (i = 0; i < 1000000u; i++) {
        memcpy(g_big + n, "1\r\nX\r\n", 6);
        g_big[n + 3] = (char)('a' + (int)(i % 26u));
        n += 6;
    }
    memcpy(g_big + n, "0\r\n\r\n", 5);
    n += 5;
    CHECK_EQ_I64(t, drive(g_big, n, n, &r, false), HTTP_OK);
    CHECK(t, r.hdr_end);
    CHECK_EQ_I64(t, r.framing, HTTP_FRAMING_CHUNKED);
    CHECK_EQ_U64(t, r.bodies, 1000000u);
    CHECK_EQ_U64(t, r.bytes, 1000000u);
    CHECK_EQ_U64(t, r.trailers, 0);
    CHECK(t, r.end);
}

/* The same shape with 100000 chunks "3\r\nabc\r\n", fed ONE BYTE per call (head included). Each chunk gives
 * three one-byte BODY events: 300000 events and bytes; 800000+ calls. */
static void t_chunks_byte_at_a_time(ctest_t *t) {
    size_t n = sizeof CHUNK_HEAD - 1, i;
    rec_t r;
    memcpy(g_big, CHUNK_HEAD, n);
    for (i = 0; i < 100000u; i++) {
        memcpy(g_big + n, "3\r\nabc\r\n", 8);
        n += 8;
    }
    memcpy(g_big + n, "0\r\n\r\n", 5);
    n += 5;
    CHECK_EQ_I64(t, drive(g_big, n, 1, &r, false), HTTP_OK);
    CHECK_EQ_I64(t, r.framing, HTTP_FRAMING_CHUNKED);
    CHECK_EQ_U64(t, r.bodies, 300000u);
    CHECK_EQ_U64(t, r.bytes, 300000u);
    CHECK(t, r.end);
}

/* A header block at the limits: the status line, 99 lines "X-Hnn: " + 8185 'v' of exactly HTTP_MAX_LINE
 * (8192) bytes each, then "Content-Length: 0" (the 100th line) and the blank line: 811 KB. All 100 headers
 * are delivered with lower-cased names. Fed whole and one byte at a time (811000 calls). */
static size_t build_headers(size_t n, unsigned lines) {
    unsigned i;
    for (i = 0; i < lines; i++) {
        n += (size_t)snprintf(g_big + n, 16, "X-H%02u: ", i);
        memset(g_big + n, 'v', 8185u);
        n += 8185u;
        memcpy(g_big + n, "\r\n", 2);
        n += 2;
    }
    return n;
}

static void check_header_block(ctest_t *t, size_t step) {
    size_t n;
    rec_t r;
    memcpy(g_big, "HTTP/1.1 200 OK\r\n", 17);
    n = build_headers(17, 99u);
    memcpy(g_big + n, "Content-Length: 0\r\n\r\n", 21);
    n += 21;
    CHECK_EQ_I64(t, drive(g_big, n, step, &r, true), HTTP_OK);
    CHECK_EQ_U64(t, r.headers, 100u);
    CHECK_EQ_U64(t, r.bad, 0);
    CHECK(t, r.hdr_end);
    CHECK_EQ_I64(t, r.framing, HTTP_FRAMING_LENGTH);
    CHECK(t, r.end);
}

static void t_header_block_whole(ctest_t *t) { check_header_block(t, sizeof g_big); }
static void t_header_block_bytewise(ctest_t *t) { check_header_block(t, 1); }

/* 100 full-length headers and then a short 101st line "Y: z": HTTP_E_TOO_MANY_HEADERS, sticky. */
static void t_header_block_too_many(ctest_t *t) {
    size_t n;
    rec_t r;
    http_status_t st;
    core_str_t none = {NULL, 0};
    memcpy(g_big, "HTTP/1.1 200 OK\r\n", 17);
    n = build_headers(17, 100u);
    memcpy(g_big + n, "Y: z\r\n\r\n", 9);
    n += 9;
    st = drive(g_big, n, sizeof g_big, &r, true);
    CHECK_EQ_I64(t, st, HTTP_E_TOO_MANY_HEADERS);
    CHECK_EQ_U64(t, r.headers, 100u);
    CHECK_EQ_U64(t, r.bad, 0);
    CHECK_EQ_I64(t, http_parser_next(&g_p, &none, &g_ev), HTTP_E_TOO_MANY_HEADERS);
}

/* A hostile 1 MiB header line (no CRLF): rejected as soon as byte 8193 arrives. */
static void t_line_too_long(ctest_t *t) {
    rec_t r;
    memcpy(g_big, "HTTP/1.1 200 OK\r\nX: ", 20);
    memset(g_big + 20, 'v', 1u << 20);
    CHECK_EQ_I64(t, drive(g_big, 20 + (1u << 20), 4096u, &r, false), HTTP_E_LINE_TOO_LONG);
    CHECK_EQ_U64(t, r.headers, 0);
}

/* 4000 chunks whose size line is the longest allowed: "1;" + 8190 'e' = 8192 bytes, then "\r\nx\r\n"
 * (8197 bytes a chunk, 33 MB in all, fed in 8197-byte slices). Each yields one 1-byte BODY; then "0" and
 * 98 trailers (the TE header is line 1, so the limit of 100 is not reached) and END. */
static void t_long_chunk_extensions(ctest_t *t) {
    static char blk[8197];
    rec_t r;
    size_t i, bodies = 0, trailers = 0;
    core_str_t in;
    http_status_t st = HTTP_OK;
    bool end = false;
    memcpy(blk, "1;", 2);
    memset(blk + 2, 'e', 8190u);
    memcpy(blk + 8192, "\r\nx\r\n", 5);
    CHECK(t, http_parser_init(&g_p));
    CHECK_EQ_I64(t, drive(CHUNK_HEAD, sizeof CHUNK_HEAD - 1, sizeof CHUNK_HEAD, &r, false), HTTP_OK);
    CHECK(t, r.hdr_end);
    for (i = 0; i < 4000u && st == HTTP_OK; i++) {
        in = S(blk, sizeof blk);
        for (;;) {
            st = http_parser_next(&g_p, &in, &g_ev);
            bodies += g_ev.kind == HTTP_EV_BODY ? 1u : 0u;
            if (st != HTTP_OK || g_ev.kind == HTTP_EV_NONE) {
                break;
            }
        }
    }
    CHECK_EQ_I64(t, st, HTTP_OK);
    CHECK_EQ_U64(t, bodies, 4000u);
    for (i = 0; i < 98u; i++) {
        char line[24];
        int l = snprintf(line, sizeof line, "%sT-%02zu: v\r\n", i == 0 ? "0\r\n" : "", i);
        in = S(line, (size_t)l);
        st = http_parser_next(&g_p, &in, &g_ev);
        if (g_ev.kind == HTTP_EV_TRAILER) {
            trailers++;
        }
        CHECK_EQ_I64(t, st, HTTP_OK);
    }
    in = LIT("\r\n");
    st = http_parser_next(&g_p, &in, &g_ev);
    end = g_ev.kind == HTTP_EV_END;
    CHECK_EQ_I64(t, st, HTTP_OK);
    CHECK(t, end);
    CHECK_EQ_U64(t, trailers, 98u);
}

/* Content-Length 8000000: the body arrives as ONE 8,000,000-byte BODY event when it is all in the input,
 * then END. Fed in 100000-byte slices (the first holds the 44-byte head): 81 BODY events. */
static void t_big_body(ctest_t *t) {
    static const char head[] = "HTTP/1.1 200 OK\r\nContent-Length: 8000000\r\n\r\n";
    size_t n = sizeof head - 1;
    rec_t r;
    memcpy(g_big, head, n);
    memset(g_big + n, 'b', 8000000u);
    n += 8000000u;
    CHECK_EQ_I64(t, drive(g_big, n, n, &r, false), HTTP_OK);
    CHECK_EQ_U64(t, r.bodies, 1);
    CHECK_EQ_U64(t, r.bytes, 8000000u);
    CHECK(t, r.end);
    CHECK_EQ_I64(t, drive(g_big, n, 100000u, &r, false), HTTP_OK);
    CHECK_EQ_U64(t, r.bytes, 8000000u);
    CHECK_EQ_U64(t, r.bodies, 81u);
    CHECK(t, r.end);
}

/* ---- URL, request writer and challenge detection at their caps ---- */

/* A URL of exactly HTTP_MAX_URL (8192) bytes with a 253-byte host (63.63.63.61), port 65535 and a long
 * path: split succeeds, the target is the rest; one byte more is HTTP_E_URL. The request writer then emits
 * the exact request (length checked by arithmetic). */
static void t_url_and_request_at_cap(ctest_t *t) {
    static const char tail[] = " HTTP/1.1\r\nHost: ";
    static const char req_rest[] = "\r\nUser-Agent: annas-c/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    size_t n = 0, i;
    http_url_t u;
    char *h;
    memcpy(g_big, "https://", 8);
    n = 8;
    h = g_big + n;
    for (i = 0; i < 4u; i++) {
        size_t len = i < 3u ? 63u : 61u;
        memset(g_big + n, (int)('a' + (int)i), len);
        n += len;
        if (i < 3u) {
            g_big[n++] = '.';
        }
    }
    CHECK_EQ_U64(t, n - 8, 253u);
    memcpy(g_big + n, ":65535/", 7);
    n += 7;
    memset(g_big + n, 'p', HTTP_MAX_URL - n);
    n = HTTP_MAX_URL;
    CHECK_EQ_I64(t, http_url_split(S(g_big, n), &u), HTTP_OK);
    CHECK_EQ_U64(t, u.host.len, 253u);
    CHECK(t, u.host.ptr == h);
    CHECK_EQ_U64(t, u.port, 65535u);
    CHECK_EQ_U64(t, u.target.len, HTTP_MAX_URL - (8u + 253u + 6u));
    g_big[n] = 'p';
    CHECK_EQ_I64(t, http_url_split(S(g_big, n + 1), &u), HTTP_E_URL);
    CHECK_EQ_I64(t, http_url_split(S(g_big, n), &u), HTTP_OK);
    CHECK(t, core_buf_init(&g_b, g_store, sizeof g_store));
    CHECK_EQ_I64(t, http_request_write(&g_b, &u, LIT("1.0")), HTTP_OK);
    /* "GET " + target + " HTTP/1.1\r\nHost: " + host + ":65535" + the fixed trailer lines. */
    CHECK_EQ_U64(t, core_buf_len(&g_b), 4u + u.target.len + (sizeof tail - 1) + 253u + 6u + sizeof req_rest - 1);
    CHECK_EQ_MEM(t, core_buf_cstr(&g_b), 4, "GET ", 4);
    CHECK_EQ_MEM(t, core_buf_cstr(&g_b) + 4, u.target.len, u.target.ptr, u.target.len);
    CHECK_EQ_MEM(t, core_buf_cstr(&g_b) + core_buf_len(&g_b) - 4, 4, "\r\n\r\n", 4);
}

/* Challenge detection looks at no more than HTTP_CHALLENGE_PREFIX body bytes and does a bounded scan of the
 * Location: an 8 MB body with "DDoS-Guard" at 1014 (inside the prefix) matches, at 1015 (the last byte is
 * past the 1024 prefix) it does not; a 4 MB Location of "check=" repeated, ending in "check=1", matches,
 * and without the final "1" does not. */
static void t_challenge_big_inputs(ctest_t *t) {
    core_str_t body = S(g_big, sizeof g_big);
    core_str_t loc;
    size_t n = 0;
    memset(g_big, 'x', sizeof g_big);
    memcpy(g_big + 1014, "DDoS-Guard", 10);
    CHECK_EQ_I64(t, http_detect_challenge(403, S("", 0), body), HTTP_E_BROWSER_CHECK);
    memset(g_big, 'x', 2048u);
    memcpy(g_big + 1015, "DDoS-Guard", 10);
    CHECK_EQ_I64(t, http_detect_challenge(503, S("", 0), body), HTTP_OK);
    while (n + 6 <= sizeof g_loc - 8u) {
        memcpy(g_loc + n, "check=", 6);
        n += 6;
    }
    loc = S(g_loc, n);
    CHECK_EQ_I64(t, http_detect_challenge(302, loc, S("", 0)), HTTP_OK);
    memcpy(g_loc + n, "1", 1);
    loc = S(g_loc, n + 1);
    CHECK_EQ_I64(t, http_detect_challenge(302, loc, S("", 0)), HTTP_E_BROWSER_CHECK);
}

CTEST_SUITE("http_complexity", { "redirect_climb", t_redirect_climb }, { "redirect_pairs", t_redirect_pairs },
            { "redirect_empty_segments", t_redirect_empty_segments },
            { "redirect_up_down", t_redirect_up_down }, { "redirect_overflow", t_redirect_overflow },
            { "redirect_dots", t_redirect_dots }, { "redirect_slashes", t_redirect_slashes },
            { "redirect_relative_climb", t_redirect_relative_climb },
            { "redirect_long_tail", t_redirect_long_tail }, { "many_minimal_chunks", t_many_minimal_chunks },
            { "chunks_byte_at_a_time", t_chunks_byte_at_a_time },
            { "header_block_whole", t_header_block_whole }, { "header_block_bytewise", t_header_block_bytewise },
            { "header_block_too_many", t_header_block_too_many }, { "line_too_long", t_line_too_long },
            { "long_chunk_extensions", t_long_chunk_extensions }, { "big_body", t_big_body },
            { "url_and_request_at_cap", t_url_and_request_at_cap },
            { "challenge_big_inputs", t_challenge_big_inputs })
