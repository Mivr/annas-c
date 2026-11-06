/* Tests for fsname/fsname.h: exact-output cases of the spec section 7 sanitiser. Public header only. */
#include <stddef.h>
#include <string.h>

#include "ctest/ctest.h"
#include "fsname/fsname.h"

#define CANARY ((char)0xA5)
#define OUTSZ ((size_t)400)

/* Sanitises (in, inlen) into a canary-filled buffer of OUTSZ bytes and expects exactly (ex, exlen).
 * Also checks: NUL terminator, nothing written past it, the input is untouched. */
static void expect(ctest_t *t, const char *in, size_t inlen, const char *ex, size_t exlen) {
    static char out[OUTSZ];
    static char copy[4096];
    size_t n, i;
    memset(out, CANARY, sizeof out);
    memcpy(copy, in, inlen);
    n = fsname_sanitize((core_str_t){in, inlen}, out, sizeof out);
    CHECK_EQ_U64(t, n, exlen);
    if (n == exlen) {
        CHECK_EQ_MEM(t, out, n, ex, exlen);
        CHECK_EQ_U64(t, (unsigned char)out[n], 0);
        for (i = n + 1; i < sizeof out; i++) {
            if (out[i] != CANARY) {
                CHECK(t, out[i] == CANARY);
                break;
            }
        }
    }
    CHECK_EQ_MEM(t, in, inlen, copy, inlen);
}
#define E(t, in, ex) expect((t), (in), sizeof(in) - 1, (ex), sizeof(ex) - 1)

static int is_forbidden(unsigned c) {
    return c < 0x20 || c == 0x7f || (c != 0 && strchr("<>:\"/\\|?*", (int)c) != NULL);
}

static void t_plain_names_unchanged(ctest_t *t) {
    E(t, "hello", "hello");
    E(t, "x", "x");
    E(t, "Hello World.pdf", "Hello World.pdf");
    E(t, "a b", "a b");
    E(t, "a  b", "a  b");
    E(t, "a.b", "a.b");
    E(t, "a.b.c", "a.b.c");
    E(t, "a_b-c", "a_b-c");
    E(t, "Dune (1965) [2nd ed.]", "Dune (1965) [2nd ed.]");
    E(t, "it's; 100% #1 & {more}, ok!", "it's; 100% #1 & {more}, ok!");
    E(t, "a+b=c~d@e$f^g`h", "a+b=c~d@e$f^g`h");
    E(t, "untitled", "untitled");
    E(t, "_", "_");
    E(t, "a. b", "a. b");
    E(t, "a .b", "a .b");
}

static void t_each_forbidden_char_becomes_underscore(ctest_t *t) {
    E(t, "a<b", "a_b");
    E(t, "a>b", "a_b");
    E(t, "a:b", "a_b");
    E(t, "a\"b", "a_b");
    E(t, "a/b", "a_b");
    E(t, "a\\b", "a_b");
    E(t, "a|b", "a_b");
    E(t, "a?b", "a_b");
    E(t, "a*b", "a_b");
    E(t, "<", "_");
    E(t, "*", "_");
    E(t, "/", "_");
    E(t, "a/", "a_");
    E(t, "/a", "_a");
    E(t, "<<>>", "____");
    E(t, "a//b", "a__b");
}

static void t_every_byte_in_the_middle(ctest_t *t) {
    unsigned c;
    for (c = 0; c < 256; c++) {
        char in[3] = {'a', (char)c, 'b'};
        char ex[3] = {'a', is_forbidden(c) ? '_' : (char)c, 'b'};
        expect(t, in, 3, ex, 3);
    }
}

static void t_control_bytes_at_start_middle_end(ctest_t *t) {
    unsigned c;
    for (c = 0; c < 0x21; c++) {
        unsigned b = (c == 0x20) ? 0x7f : c;
        char s[2] = {(char)b, 'a'};
        char m[3] = {'x', (char)b, 'y'};
        char e[2] = {'a', (char)b};
        expect(t, s, 2, "_a", 2);
        expect(t, m, 3, "x_y", 3);
        expect(t, e, 2, "a_", 2);
    }
}

static void t_nul_inside_the_slice(ctest_t *t) {
    E(t, "a\0b", "a_b");
    E(t, "ab\0cd", "ab_cd");
    E(t, "\0", "_");
    E(t, "\0\0", "__");
    E(t, "\0abc", "_abc");
    E(t, "abc\0", "abc_");
    E(t, "abc\0\0def", "abc__def");
    E(t, "../\0../", "_____");
}

static void t_tab_newline_are_control_bytes(ctest_t *t) {
    E(t, "\t", "_");
    E(t, "\n\r", "__");
    E(t, "a\tb", "a_b");
    E(t, "a\n", "a_");
    E(t, "\v\f", "__");
    E(t, "\033[31mred", "_[31mred");
}

static void t_traversal_titles(ctest_t *t) {
    E(t, "../../etc/passwd", "____etc_passwd");
    E(t, "/etc/passwd", "_etc_passwd");
    E(t, "..\\..\\windows\\system32", "____windows_system32");
    E(t, "../", "__");
    E(t, "./", "_");
    E(t, "a/../b", "a___b");
    E(t, "a/./b", "a_._b");
    E(t, "foo/..", "foo__");
    E(t, "../..", "___");
    E(t, "....//....//", "________");
    E(t, "%2e%2e/etc", "%2e%2e_etc");
    E(t, "C:\\Windows\\win.ini", "C__Windows_win.ini");
    E(t, "~/../.ssh/id_rsa", "~___.ssh_id_rsa");
    E(t, "a\0../b", "a___b");
}

static void t_dots_alone_and_runs(ctest_t *t) {
    E(t, ".", "untitled");
    E(t, "..", "_");
    E(t, "...", "_");
    E(t, "....", "__");
    E(t, ".....", "__");
    E(t, "......", "___");
    E(t, "........", "____");
    E(t, "a..b", "a_b");
    E(t, "a...b", "a_.b");
    E(t, "a....b", "a__b");
    E(t, "a.....b", "a__.b");
    E(t, "a..", "a_");
    E(t, "a...", "a_");
    E(t, "..a", "_a");
    E(t, "...a", "_.a");
    E(t, ".a", "a");
    E(t, "..a..", "_a_");
    E(t, "a.b..c.d", "a.b_c.d");
}

static void t_leading_dots_and_spaces(ctest_t *t) {
    E(t, ".hidden", "hidden");
    E(t, " a", "a");
    E(t, "     a", "a");
    E(t, ". . .a", "a");
    E(t, " . . a", "a");
    E(t, " .. a", "_ a");
    E(t, ".. a", "_ a");
    E(t, " ..a", "_a");
    E(t, ".\x80", "\x80");
    E(t, " \xc2\xa0x", "\xc2\xa0x");
}

static void t_trailing_dots_and_spaces(ctest_t *t) {
    E(t, "a.", "a");
    E(t, "a ", "a");
    E(t, "a   ", "a");
    E(t, "a. . .", "a");
    E(t, "a . . ", "a");
    E(t, "a.pdf.", "a.pdf");
    E(t, "a.pdf  ", "a.pdf");
    E(t, "a.pdf . ", "a.pdf");
    E(t, "  a  ", "a");
    E(t, "..a..", "_a_");
    E(t, " .. ", "_");
    E(t, ".. ..", "_ _");
    E(t, "a\xc2\xa0", "a\xc2\xa0");
    E(t, "a\x80 ", "a\x80");
}

static void t_empty_and_degenerate_become_untitled(ctest_t *t) {
    static char out[OUTSZ];
    size_t n;
    E(t, "", "untitled");
    E(t, " ", "untitled");
    E(t, "     ", "untitled");
    E(t, ".", "untitled");
    E(t, ". .", "untitled");
    E(t, " . . . ", "untitled");
    E(t, ". . .", "untitled");
    memset(out, CANARY, sizeof out);
    n = fsname_sanitize((core_str_t){NULL, 0}, out, sizeof out);
    CHECK_EQ_U64(t, n, 8);
    CHECK_EQ_STR(t, out, "untitled");
}

static void t_all_forbidden_title(ctest_t *t) {
    E(t, "<>:\"/\\|?*", "_________");
    E(t, "////", "____");
    E(t, "\\\\\\", "___");
    E(t, "?", "_");
    E(t, "***", "___");
    E(t, "|<>|", "____");
}

static void t_non_ascii_text_is_kept(ctest_t *t) {
    E(t, "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82.pdf", "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82.pdf");
    E(t, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    E(t, "caf\xc3\xa9", "caf\xc3\xa9");
    E(t, "\xf0\x9f\x98\x80 smile", "\xf0\x9f\x98\x80 smile");
    E(t, "\xc2\xa0", "\xc2\xa0");   /* NBSP is not a space for the sanitiser */
    E(t, "\xef\xbc\x8f", "\xef\xbc\x8f");   /* fullwidth solidus is kept: not '/' */
    E(t, "\xe2\x80\xae" "gpj.exe", "\xe2\x80\xae" "gpj.exe");
}

static void t_invalid_utf8_passes_through(ctest_t *t) {
    E(t, "\xff\xfe", "\xff\xfe");
    E(t, "a\x80" "b", "a\x80" "b");
    E(t, "\xc0\xaf", "\xc0\xaf");              /* overlong '/' stays two bytes, never becomes '/' */
    E(t, "\xc0\xae\xc0\xae/x", "\xc0\xae\xc0\xae_x");
    E(t, "\xe2\x82", "\xe2\x82");              /* truncated sequence at the end */
    E(t, "\xed\xa0\x80", "\xed\xa0\x80");      /* surrogate */
    E(t, "\xf8\x88\x80\x80\x80", "\xf8\x88\x80\x80\x80");
    E(t, "\x80\x80\x80", "\x80\x80\x80");
}

static void t_length_boundaries(ctest_t *t) {
    static char in[1100], ex[256];
    static const char pat[] = "0123456789abcdefghijABCDEFGHIJ";
    size_t i, len;
    static const size_t lens[] = {1, 100, 198, 199, 200, 201, 202, 300, 1000};
    for (i = 0; i < 1100; i++) in[i] = pat[i % (sizeof pat - 1)];
    for (len = 0; len < sizeof lens / sizeof lens[0]; len++) {
        size_t l = lens[len], want = l > 200 ? 200 : l;
        memcpy(ex, in, want);
        expect(t, in, l, ex, want);
    }
}

static void t_length_counts_after_rewriting(ctest_t *t) {
    static char in[1100], ex[1100];
    size_t i;
    /* 199 'a' then ".." is 201 bytes in but 200 bytes after ".." -> "_": no cut at all. */
    memset(in, 'a', 199);
    memcpy(in + 199, "..", 2);
    memset(ex, 'a', 199);
    ex[199] = '_';
    expect(t, in, 201, ex, 200);
    /* 200 'a' then ".." -> 201 bytes: cut leaves 200 'a'. */
    memset(in, 'a', 200);
    memcpy(in + 200, "..", 2);
    expect(t, in, 202, in, 200);
    /* leading dots/spaces are removed BEFORE the cut: 50 spaces then 200 'a'. */
    memset(in, ' ', 50);
    memset(in + 50, 'a', 200);
    memset(ex, 'a', 200);
    expect(t, in, 250, ex, 200);
    for (i = 0; i < 50; i++) in[i] = (i % 2) ? ' ' : '.';
    expect(t, in, 250, ex, 200);
    /* trailing junk is removed BEFORE the cut too: 200 'a' then 100 dots. */
    memset(in, 'a', 200);
    memset(in + 200, '.', 100);
    expect(t, in, 300, ex, 200);
    memset(in + 200, ' ', 100);
    expect(t, in, 300, ex, 200);
    /* 1000 forbidden bytes -> 200 underscores; 1000 dots -> 500 underscores -> 200. */
    memset(in, '/', 1000);
    memset(ex, '_', 200);
    expect(t, in, 1000, ex, 200);
    memset(in, '.', 1000);
    expect(t, in, 1000, ex, 200);
    for (i = 0; i < 1000; i++) in[i] = (char)(i % 32);
    expect(t, in, 1000, ex, 200);
}

static void t_cut_result_is_not_stripped_again(ctest_t *t) {
    static char in[300], ex[300];
    /* The cut happens after the strip and the strip is not repeated (see the header). */
    memset(in, 'a', 199);
    in[199] = ' ';
    in[200] = 'b';
    memcpy(ex, in, 200);
    expect(t, in, 201, ex, 200);
    in[199] = '.';
    memcpy(ex, in, 200);
    expect(t, in, 201, ex, 200);
    memset(in, 'a', 190);
    memset(in + 190, ' ', 20);
    in[210] = 'b';
    memcpy(ex, in, 200);
    expect(t, in, 211, ex, 200);
}

static void t_huge_input(ctest_t *t) {
    static char in[200000], ex[200];
    memset(in, 'z', sizeof in);
    memset(ex, 'z', sizeof ex);
    expect(t, in, 200, ex, 200);
    {
        static char out[OUTSZ];
        size_t n = fsname_sanitize((core_str_t){in, sizeof in}, out, sizeof out);
        CHECK_EQ_U64(t, n, 200);
        CHECK_EQ_MEM(t, out, 200, ex, 200);
        CHECK_EQ_U64(t, (unsigned char)out[200], 0);
    }
}

static void t_exact_buffer_size_201(ctest_t *t) {
    static char in[300], out[201];
    size_t n;
    memset(in, 'q', sizeof in);
    n = fsname_sanitize((core_str_t){in, 300}, out, 201);   /* 200 bytes + NUL fill it exactly */
    CHECK_EQ_U64(t, n, 200);
    CHECK_EQ_U64(t, (unsigned char)out[199], 'q');
    CHECK_EQ_U64(t, (unsigned char)out[200], 0);
    n = fsname_sanitize((core_str_t){"untitled", 0}, out, 201);
    CHECK_EQ_U64(t, n, 8);
    CHECK_EQ_STR(t, out, "untitled");
}

static void t_bad_arguments_return_zero(ctest_t *t) {
    static char out[OUTSZ];
    size_t n;
    static const char in[] = "good";
    core_str_t ok = {in, 4};
    /* positive control for every rejection below */
    memset(out, CANARY, sizeof out);
    n = fsname_sanitize(ok, out, sizeof out);
    CHECK_EQ_U64(t, n, 4);
    CHECK_EQ_STR(t, out, "good");
    /* out == NULL */
    CHECK_EQ_U64(t, fsname_sanitize(ok, NULL, sizeof out), 0);
    CHECK_EQ_U64(t, fsname_sanitize(ok, NULL, 0), 0);
    /* cap too small: 200 is one short, writes nothing */
    memset(out, CANARY, sizeof out);
    CHECK_EQ_U64(t, fsname_sanitize(ok, out, 200), 0);
    CHECK_EQ_U64(t, fsname_sanitize(ok, out, 5), 0);
    CHECK_EQ_U64(t, fsname_sanitize(ok, out, 1), 0);
    CHECK_EQ_U64(t, fsname_sanitize(ok, out, 0), 0);
    CHECK_EQ_U64(t, fsname_sanitize((core_str_t){NULL, 0}, out, 200), 0);
    {
        size_t i, bad = 0;
        for (i = 0; i < sizeof out; i++) bad += (out[i] != CANARY);
        CHECK_EQ_U64(t, bad, 0);
    }
    /* NULL pointer with a length */
    memset(out, CANARY, sizeof out);
    CHECK_EQ_U64(t, fsname_sanitize((core_str_t){NULL, 5}, out, sizeof out), 0);
    CHECK_EQ_U64(t, (unsigned char)out[0], (unsigned char)CANARY);
}

#define S(x) {(x), sizeof(x) - 1}
static void t_join_with_directory_stays_inside(ctest_t *t) {
    static const core_str_t bad[] = {
        S("../../etc/passwd"), S(".."), S("."), S("..."), S("/abs/path"), S("..\\x"), S("a/../../b"),
        S(".. /.."), S(" ../ "), S("....//....//x"), S("\0../x"), S("/"), S("//"), S("..//.."),
        S("./."), S("a/.."), S("%2e%2e/%2e%2e"), S("x/.. "), S("C:\\..\\.."), S("\0"), S("\\.."), S(" ")};
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char out[OUTSZ];
        size_t n = fsname_sanitize(bad[i], out, sizeof out);
        size_t k, slashes = 0;
        CHECK(t, n >= 1);
        CHECK(t, n <= 200);
        CHECK(t, !(n == 1 && out[0] == '.'));
        CHECK(t, !(n == 2 && out[0] == '.' && out[1] == '.'));
        for (k = 0; k < n; k++) slashes += (out[k] == '/' || out[k] == '\\' || out[k] == 0);
        CHECK_EQ_U64(t, slashes, 0);
        CHECK(t, out[0] != '.');
        CHECK_EQ_U64(t, (unsigned char)out[n], 0);
    }
}

CTEST_SUITE("fsname",
    {"plain_names_unchanged", t_plain_names_unchanged},
    {"each_forbidden_char_becomes_underscore", t_each_forbidden_char_becomes_underscore},
    {"every_byte_in_the_middle", t_every_byte_in_the_middle},
    {"control_bytes_at_start_middle_end", t_control_bytes_at_start_middle_end},
    {"nul_inside_the_slice", t_nul_inside_the_slice},
    {"tab_newline_are_control_bytes", t_tab_newline_are_control_bytes},
    {"traversal_titles", t_traversal_titles},
    {"dots_alone_and_runs", t_dots_alone_and_runs},
    {"leading_dots_and_spaces", t_leading_dots_and_spaces},
    {"trailing_dots_and_spaces", t_trailing_dots_and_spaces},
    {"empty_and_degenerate_become_untitled", t_empty_and_degenerate_become_untitled},
    {"all_forbidden_title", t_all_forbidden_title},
    {"non_ascii_text_is_kept", t_non_ascii_text_is_kept},
    {"invalid_utf8_passes_through", t_invalid_utf8_passes_through},
    {"length_boundaries", t_length_boundaries},
    {"length_counts_after_rewriting", t_length_counts_after_rewriting},
    {"cut_result_is_not_stripped_again", t_cut_result_is_not_stripped_again},
    {"huge_input", t_huge_input},
    {"exact_buffer_size_201", t_exact_buffer_size_201},
    {"bad_arguments_return_zero", t_bad_arguments_return_zero},
    {"join_with_directory_stays_inside", t_join_with_directory_stays_inside})
