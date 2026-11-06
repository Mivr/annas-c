/* Tests for fsname/fsname.h: truncation to 200 bytes never splits a UTF-8 sequence. Public header only. */
#include <stddef.h>
#include <string.h>

#include "ctest/ctest.h"
#include "fsname/fsname.h"

#define OUTSZ ((size_t)300)

static const char SEQ2[] = "\xc3\xa9";                 /* U+00E9 */
static const char SEQ3[] = "\xe2\x82\xac";             /* U+20AC */
static const char SEQ4[] = "\xf0\x9f\x98\x80";         /* U+1F600 */

/* Structural UTF-8 check: every lead byte is followed by exactly the right number of continuations. */
static int valid_utf8(const unsigned char *p, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned c = p[i];
        size_t need = c < 0x80 ? 0 : (c >= 0xc2 && c <= 0xdf) ? 1 : (c >= 0xe0 && c <= 0xef) ? 2 : (c >= 0xf0 && c <= 0xf4) ? 3 : 99;
        size_t k;
        if (need == 99) return 0;
        for (k = 1; k <= need; k++) {
            if (i + k >= n || (p[i + k] & 0xc0) != 0x80) return 0;
        }
        i += need + 1;
    }
    return 1;
}

/* in = `start` filler 'a', then seq, then `tail` filler 'z'. Returns total length. */
static size_t build(char *in, size_t start, const char *seq, size_t seqlen, size_t tail) {
    memset(in, 'a', start);
    memcpy(in + start, seq, seqlen);
    memset(in + start + seqlen, 'z', tail);
    return start + seqlen + tail;
}

/* Expect the output to be the first `cut` bytes of `in`, and valid UTF-8. */
static void expect_cut(ctest_t *t, const char *in, size_t inlen, size_t cut) {
    char out[OUTSZ];
    size_t n = fsname_sanitize((core_str_t){in, inlen}, out, sizeof out);
    CHECK_EQ_U64(t, n, cut);
    if (n == cut) {
        CHECK_EQ_MEM(t, out, n, in, cut);
        CHECK(t, valid_utf8((const unsigned char *)out, n));
    }
}

static void t_two_byte_straddle(ctest_t *t) {
    char in[OUTSZ];
    size_t len;
    len = build(in, 199, SEQ2, 2, 50);   /* bytes 199,200: split by the cut at 200 */
    expect_cut(t, in, len, 199);
    len = build(in, 198, SEQ2, 2, 50);   /* bytes 198,199: ends exactly at the cut */
    expect_cut(t, in, len, 200);
    len = build(in, 200, SEQ2, 2, 50);   /* starts at the cut: dropped whole */
    expect_cut(t, in, len, 200);
    len = build(in, 197, SEQ2, 2, 50);
    expect_cut(t, in, len, 200);
    len = build(in, 201, SEQ2, 2, 50);
    expect_cut(t, in, len, 200);
}

static void t_three_byte_straddle(ctest_t *t) {
    char in[OUTSZ];
    size_t len;
    len = build(in, 198, SEQ3, 3, 50);   /* bytes 198,199,200 */
    expect_cut(t, in, len, 198);
    len = build(in, 199, SEQ3, 3, 50);   /* bytes 199,200,201 */
    expect_cut(t, in, len, 199);
    len = build(in, 197, SEQ3, 3, 50);   /* bytes 197..199: ends exactly at the cut */
    expect_cut(t, in, len, 200);
    len = build(in, 200, SEQ3, 3, 50);
    expect_cut(t, in, len, 200);
    len = build(in, 196, SEQ3, 3, 50);
    expect_cut(t, in, len, 200);
}

static void t_four_byte_straddle(ctest_t *t) {
    char in[OUTSZ];
    size_t len;
    len = build(in, 197, SEQ4, 4, 50);   /* 197..200 */
    expect_cut(t, in, len, 197);
    len = build(in, 198, SEQ4, 4, 50);   /* 198..201 */
    expect_cut(t, in, len, 198);
    len = build(in, 199, SEQ4, 4, 50);   /* 199..202 */
    expect_cut(t, in, len, 199);
    len = build(in, 196, SEQ4, 4, 50);   /* 196..199: ends exactly at the cut */
    expect_cut(t, in, len, 200);
    len = build(in, 200, SEQ4, 4, 50);
    expect_cut(t, in, len, 200);
    len = build(in, 195, SEQ4, 4, 50);
    expect_cut(t, in, len, 200);
}

static void t_every_start_offset_for_every_length(ctest_t *t) {
    static const char *const seqs[] = {SEQ2, SEQ3, SEQ4};
    static const size_t lens[] = {2, 3, 4};
    size_t k, s;
    for (k = 0; k < 3; k++) {
        for (s = 180; s <= 205; s++) {
            char in[OUTSZ];
            size_t len = build(in, s, seqs[k], lens[k], 40);
            size_t cut = (s < 200 && 200 < s + lens[k]) ? s : 200;
            expect_cut(t, in, len, cut);
        }
    }
}

static void t_runs_of_multibyte_text(ctest_t *t) {
    static char in[OUTSZ + 200];
    size_t i;
    for (i = 0; i < 150; i++) memcpy(in + 2 * i, SEQ2, 2);          /* 300 bytes, 200 is a boundary */
    expect_cut(t, in, 300, 200);
    for (i = 0; i < 100; i++) memcpy(in + 3 * i, SEQ3, 3);          /* 300 bytes, 200 = 66*3 + 2 */
    expect_cut(t, in, 300, 198);
    for (i = 0; i < 100; i++) memcpy(in + 4 * i, SEQ4, 4);          /* 400 bytes, 200 = 50*4 */
    expect_cut(t, in, 400, 200);
    in[0] = 'a';
    for (i = 0; i < 100; i++) memcpy(in + 1 + 4 * i, SEQ4, 4);      /* sequences at 1,5,..,197 */
    expect_cut(t, in, 401, 197);
    in[0] = 'a';
    in[1] = 'b';
    for (i = 0; i < 100; i++) memcpy(in + 2 + 4 * i, SEQ4, 4);      /* 198 */
    expect_cut(t, in, 402, 198);
}

static void t_multibyte_below_the_limit_untouched(ctest_t *t) {
    char in[OUTSZ];
    size_t len = build(in, 195, SEQ4, 4, 0);   /* 199 bytes */
    expect_cut(t, in, len, 199);
    len = build(in, 196, SEQ4, 4, 0);          /* exactly 200 bytes */
    expect_cut(t, in, len, 200);
    len = build(in, 196, SEQ4, 4, 1);          /* 201 bytes, the extra is ASCII */
    expect_cut(t, in, len, 200);
    len = build(in, 197, SEQ4, 4, 0);          /* 201 bytes, last sequence straddles */
    expect_cut(t, in, len, 197);
    len = build(in, 198, SEQ3, 3, 0);          /* 201 bytes, 3-byte straddles */
    expect_cut(t, in, len, 198);
    len = build(in, 199, SEQ2, 2, 0);          /* 201 bytes, 2-byte straddles */
    expect_cut(t, in, len, 199);
}

static void t_cut_sequence_after_rewriting(ctest_t *t) {
    /* ".." inside the text shrinks the byte count before the cut: 4 pairs of dots save 4 bytes. */
    static char in[OUTSZ], ex[OUTSZ];
    static char out[OUTSZ];
    size_t n;
    memset(in, 'a', 8);
    memcpy(in + 8, "........", 8);               /* becomes "____", saves 4 bytes */
    build(in + 16, 183, SEQ2, 2, 40);           /* in-index 199 and 200 -> out-index 195, 196 */
    n = fsname_sanitize((core_str_t){in, 16 + 183 + 2 + 40}, out, sizeof out);
    memset(ex, 'a', 8);
    memset(ex + 8, '_', 4);
    memset(ex + 12, 'a', 183);
    memcpy(ex + 195, SEQ2, 2);
    memset(ex + 197, 'z', 3);
    CHECK_EQ_U64(t, n, 200);
    CHECK_EQ_MEM(t, out, n, ex, 200);
}

static void t_stray_continuation_bytes_cut_at_200(ctest_t *t) {
    static char in[OUTSZ], out[OUTSZ];
    size_t n;
    memset(in, 0x80, 300);
    n = fsname_sanitize((core_str_t){in, 300}, out, sizeof out);
    CHECK_EQ_U64(t, n, 200);
    CHECK_EQ_MEM(t, out, 200, in, 200);
    memset(in, 'a', 100);
    memset(in + 100, 0xbf, 200);
    n = fsname_sanitize((core_str_t){in, 300}, out, sizeof out);
    CHECK_EQ_U64(t, n, 200);
    CHECK_EQ_MEM(t, out, 200, in, 200);
    /* a lead byte more than three bytes before the cut does not count */
    memset(in, 'a', 196);
    in[196] = (char)0xc3;
    memset(in + 197, 0xa9, 50);
    n = fsname_sanitize((core_str_t){in, 247}, out, sizeof out);
    CHECK_EQ_U64(t, n, 200);
    CHECK_EQ_MEM(t, out, 200, in, 200);
}

static void t_lone_lead_bytes_and_ff(ctest_t *t) {
    static char in[OUTSZ], out[OUTSZ];
    size_t n;
    memset(in, 'a', 199);
    in[199] = (char)0xc3;                       /* lead at 199 followed by ASCII: not a straddle */
    memset(in + 200, 'z', 50);
    n = fsname_sanitize((core_str_t){in, 250}, out, sizeof out);
    CHECK_EQ_U64(t, n, 200);
    CHECK_EQ_MEM(t, out, 200, in, 200);
    memset(in, 0xff, 300);
    n = fsname_sanitize((core_str_t){in, 300}, out, sizeof out);
    CHECK_EQ_U64(t, n, 200);
    CHECK_EQ_MEM(t, out, 200, in, 200);
    memset(in, 0xc3, 300);
    n = fsname_sanitize((core_str_t){in, 300}, out, sizeof out);
    CHECK_EQ_U64(t, n, 200);
    CHECK_EQ_MEM(t, out, 200, in, 200);
}

CTEST_SUITE("fsname_utf8",
    {"two_byte_straddle", t_two_byte_straddle},
    {"three_byte_straddle", t_three_byte_straddle},
    {"four_byte_straddle", t_four_byte_straddle},
    {"every_start_offset_for_every_length", t_every_start_offset_for_every_length},
    {"runs_of_multibyte_text", t_runs_of_multibyte_text},
    {"multibyte_below_the_limit_untouched", t_multibyte_below_the_limit_untouched},
    {"cut_sequence_after_rewriting", t_cut_sequence_after_rewriting},
    {"stray_continuation_bytes_cut_at_200", t_stray_continuation_bytes_cut_at_200},
    {"lone_lead_bytes_and_ff", t_lone_lead_bytes_and_ff})
