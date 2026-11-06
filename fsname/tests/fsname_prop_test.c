/* Property tests for fsname/fsname.h with a seeded deterministic generator (splitmix64, no rand()).
 * Each of 10000 generated titles is checked against the security invariants AND against an
 * independent step-by-step reference of the spec section 7 algorithm written in this file. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ctest/ctest.h"
#include "fsname/fsname.h"

#define CASES 10000
#define MAXIN 400

typedef struct {
    uint64_t s;
} rng_t;

static uint64_t rng_next(rng_t *r) {
    uint64_t z;
    r->s += UINT64_C(0x9E3779B97F4A7C15);
    z = r->s;
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static size_t rng_below(rng_t *r, size_t n) { return (size_t)(rng_next(r) % n); }

/* Appends one generated element to in[*len] (at most 4 bytes). */
static void gen_element(rng_t *r, unsigned char *in, size_t *len, int uniform) {
    static const char forb[] = "<>:\"/\\|?*";
    size_t k = rng_below(r, 16);
    unsigned char *p = in + *len;
    if (uniform || k < 4) {
        p[0] = (unsigned char)rng_below(r, 256);
        *len += 1;
    } else if (k < 7) {
        p[0] = '.';
        *len += 1;
    } else if (k == 7) {
        p[0] = ' ';
        *len += 1;
    } else if (k == 8) {
        p[0] = (unsigned char)forb[rng_below(r, 9)];
        *len += 1;
    } else if (k == 9) {
        size_t c = rng_below(r, 33);
        p[0] = (unsigned char)(c == 32 ? 0x7f : c);
        *len += 1;
    } else if (k == 10) {
        p[0] = (unsigned char)(0xc2 + rng_below(r, 30));      /* valid 2-byte sequence */
        p[1] = (unsigned char)(0x80 + rng_below(r, 64));
        *len += 2;
    } else if (k == 11) {
        p[0] = (unsigned char)(0xe1 + rng_below(r, 12));      /* valid-shaped 3-byte sequence */
        p[1] = (unsigned char)(0x80 + rng_below(r, 64));
        p[2] = (unsigned char)(0x80 + rng_below(r, 64));
        *len += 3;
    } else if (k == 12) {
        p[0] = 0xf0;                                          /* 4-byte sequence */
        p[1] = (unsigned char)(0x90 + rng_below(r, 48));
        p[2] = (unsigned char)(0x80 + rng_below(r, 64));
        p[3] = (unsigned char)(0x80 + rng_below(r, 64));
        *len += 4;
    } else {
        p[0] = (unsigned char)('a' + rng_below(r, 26));
        *len += 1;
    }
}

/* Generates one title of 0..400 bytes (length chosen so that the 200-byte cut is hit often). */
static size_t gen_title(rng_t *r, unsigned char *in) {
    size_t len = 0, target, mode = rng_below(r, 8);
    int uniform = (rng_below(r, 4) == 0);
    if (mode == 0) target = 190 + rng_below(r, 30);
    else if (mode == 1) target = 396 + rng_below(r, 5);
    else if (mode == 2) target = rng_below(r, 6);
    else target = rng_below(r, MAXIN + 1);
    if (target > MAXIN) target = MAXIN;
    while (len < target) gen_element(r, in, &len, uniform);
    if (len > MAXIN) len = MAXIN;
    return len;
}

/* Independent reference of the algorithm in the header. */
static size_t reference(const unsigned char *in, size_t n, unsigned char *out) {
    static unsigned char a[MAXIN + 8], b[MAXIN + 8];
    size_t i, la = 0, lb = 0, lo = 0, hi, cut;
    for (i = 0; i < n; i++) {
        unsigned c = in[i];
        int bad = c < 0x20 || c == 0x7f || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
                  c == '\\' || c == '|' || c == '?' || c == '*';
        a[la++] = bad ? '_' : (unsigned char)c;
    }
    for (i = 0; i < la;) {
        if (a[i] == '.' && i + 1 < la && a[i + 1] == '.') {
            b[lb++] = '_';
            i += 2;
        } else {
            b[lb++] = a[i++];
        }
    }
    hi = lb;
    while (lo < hi && (b[lo] == '.' || b[lo] == ' ')) lo++;
    while (hi > lo && (b[hi - 1] == '.' || b[hi - 1] == ' ')) hi--;
    cut = hi - lo;
    if (cut > 200) {
        size_t j;
        cut = 200;
        if ((b[lo + 200] & 0xc0) == 0x80) {
            for (j = 1; j <= 3; j++) {
                unsigned char x = b[lo + 200 - j];
                if (x >= 0xc0) {
                    cut = 200 - j;
                    break;
                }
                if ((x & 0xc0) != 0x80) break;
            }
        }
    }
    if (cut == 0) {
        memcpy(out, "untitled", 8);
        return 8;
    }
    memcpy(out, b + lo, cut);
    return cut;
}

static int has_dotdot(const unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i + 1 < n; i++) {
        if (p[i] == '.' && p[i + 1] == '.') return 1;
    }
    return 0;
}

static void check_invariants(ctest_t *t, const unsigned char *o, size_t n) {
    size_t i, bad = 0;
    CHECK(t, n >= 1);
    CHECK(t, n <= 200);
    for (i = 0; i < n; i++) {
        bad += (o[i] == '/' || o[i] == '\\' || o[i] == 0 || o[i] < 0x20 || o[i] == 0x7f);
    }
    CHECK_EQ_U64(t, bad, 0);
    CHECK(t, !(n == 1 && o[0] == '.'));
    CHECK(t, !(n == 2 && o[0] == '.' && o[1] == '.'));
    CHECK(t, n == 0 || o[0] != '.');
    CHECK(t, !has_dotdot(o, n));
}

static void t_random_titles_match_invariants_and_reference(ctest_t *t) {
    rng_t r = {UINT64_C(0xA005C0FFEE123457)};
    static unsigned char in[MAXIN + 8], out[300], ref[300];
    size_t c, empty = 0, untitled = 0, full = 0, dots = 0, ctl = 0, over = 0;
    for (c = 0; c < CASES; c++) {
        size_t n = gen_title(&r, in);
        size_t rn = reference(in, n, ref);
        size_t on;
        memset(out, 0xA5, sizeof out);
        on = fsname_sanitize((core_str_t){(const char *)in, n}, (char *)out, 201);
        CHECK_EQ_U64(t, on, rn);
        if (on != rn) continue;
        CHECK_EQ_MEM(t, out, on, ref, rn);
        CHECK_EQ_U64(t, out[on], 0);
        CHECK_EQ_U64(t, out[on + 1], 0xA5);
        check_invariants(t, out, on);
        empty += (n == 0) ? 1u : 0u;
        over += (n > 200) ? 1u : 0u;
        untitled += (on == 8 && memcmp(out, "untitled", 8) == 0) ? 1u : 0u;
        full += (on >= 198) ? 1u : 0u;
        dots += (size_t)has_dotdot(in, n);
        ctl += (memchr(in, 0, n) != NULL) ? 1u : 0u;
    }
    /* the corpus really exercises the interesting classes (not a vacuous loop) */
    CHECK(t, empty >= 20);
    CHECK(t, over >= 1500);
    CHECK(t, untitled >= 100);
    CHECK(t, full >= 1000);
    CHECK(t, dots >= 500);
    CHECK(t, ctl >= 500);
}

static void t_generator_is_deterministic(ctest_t *t) {
    char one[201];
    rng_t a = {42}, b = {42}, c = {43};
    size_t i, differ = 0;
    for (i = 0; i < 100; i++) {
        uint64_t x = rng_next(&a);
        CHECK(t, x == rng_next(&b));
        differ += (x != rng_next(&c));
    }
    CHECK(t, differ >= 99);
    CHECK_EQ_U64(t, fsname_sanitize((core_str_t){"abc", 3}, one, sizeof one), 3);   /* smoke test */
    {
        rng_t z = {0};
        CHECK(t, rng_next(&z) == UINT64_C(0xE220A8397B1DCDAF));   /* splitmix64 reference value */
    }
}

static void t_sanitising_twice_is_stable_for_short_titles(ctest_t *t) {
    rng_t r = {UINT64_C(0x5151515151515151)};
    static unsigned char in[MAXIN + 8];
    size_t c, checked = 0;
    for (c = 0; c < CASES; c++) {
        static char o1[300], o2[300];
        size_t n = gen_title(&r, in), n1, n2;
        if (n > 200) continue;
        n1 = fsname_sanitize((core_str_t){(const char *)in, n}, o1, 201);
        n2 = fsname_sanitize((core_str_t){o1, n1}, o2, 201);
        CHECK(t, n1 >= 1);
        CHECK_EQ_U64(t, n2, n1);
        if (n1 == n2) CHECK_EQ_MEM(t, o2, n2, o1, n1);
        checked++;
    }
    CHECK(t, checked >= 4000);
}

static void t_output_never_escapes_a_directory(ctest_t *t) {
    rng_t r = {UINT64_C(0x0123456789ABCDEF)};
    static unsigned char in[MAXIN + 8];
    size_t c;
    for (c = 0; c < 2000; c++) {
        static char path[320];
        static char out[300];
        size_t n = gen_title(&r, in), on, k, comps = 1, depth = 0;
        on = fsname_sanitize((core_str_t){(const char *)in, n}, out, 201);
        memcpy(path, "/dl/", 4);
        memcpy(path + 4, out, on + 1);
        for (k = 0; k < 4 + on; k++) comps += (path[k] == '/');
        /* "/dl/<name>": exactly the two separators, so <name> is a single component */
        CHECK(t, on >= 1);
        CHECK_EQ_U64(t, comps, 3);
        CHECK(t, !(on == 1 && out[0] == '.') && !(on == 2 && out[0] == '.' && out[1] == '.'));
        depth = (size_t)(strstr(path, "/../") != NULL) + (size_t)(strstr(path, "/./") != NULL);
        CHECK_EQ_U64(t, depth, 0);
    }
}

CTEST_SUITE("fsname_prop",
    {"random_titles_match_invariants_and_reference", t_random_titles_match_invariants_and_reference},
    {"generator_is_deterministic", t_generator_is_deterministic},
    {"sanitising_twice_is_stable_for_short_titles", t_sanitising_twice_is_stable_for_short_titles},
    {"output_never_escapes_a_directory", t_output_never_escapes_a_directory})
