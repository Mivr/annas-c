/* Tests for http/http.h: http_detect_challenge (spec section 2). Public header only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/str.h"
#include "ctest/ctest.h"
#include "http/http.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1})
#define NOTHING ((core_str_t){NULL, 0})

static char g_body[HTTP_CHALLENGE_PREFIX + 64];

static http_status_t chk(int status, const char *loc, const char *body) {
    return http_detect_challenge(status, core_str_from_cstr(loc), core_str_from_cstr(body));
}

static void t_ddos_guard_on_403_and_503(ctest_t *t) {
    CHECK_EQ_I64(t, chk(403, "", "<html>DDoS-Guard</html>"), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(503, "", "<html>DDoS-Guard</html>"), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(403, "", "ddos-guard"), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(503, "", "DDOS-GUARD"), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(403, "", "xx dDoS-gUaRd yy"), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(403, "", "<title>DDOS-GUARD checking your browser</title>"), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(503, "", "a ddos-guard b ddos-guard"), HTTP_E_BROWSER_CHECK);
}

static void t_ddos_guard_controls(ctest_t *t) {
    /* near misses on the text */
    CHECK_EQ_I64(t, chk(403, "", "ddos guard"), HTTP_OK);
    CHECK_EQ_I64(t, chk(403, "", "ddosguard"), HTTP_OK);
    CHECK_EQ_I64(t, chk(403, "", "ddos-guar"), HTTP_OK);
    CHECK_EQ_I64(t, chk(403, "", "dos-guard"), HTTP_OK);
    CHECK_EQ_I64(t, chk(403, "", "ddos_guard"), HTTP_OK);
    CHECK_EQ_I64(t, chk(403, "", "ddos-gua"), HTTP_OK);
    CHECK_EQ_I64(t, chk(403, "", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(403, "", "Forbidden"), HTTP_OK);
    CHECK_EQ_I64(t, chk(503, "", "Service Unavailable"), HTTP_OK);
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, LIT("ddos\0guard")), HTTP_OK);
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, LIT("ddos-gu\0ard")), HTTP_OK);
    /* a NUL elsewhere does not hide a match */
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, LIT("\0\0ddos-guard\0")), HTTP_E_BROWSER_CHECK);
}

static void t_ddos_guard_needs_403_or_503(ctest_t *t) {
    int s;
    for (s = 0; s < 1000; s++) {
        http_status_t want = (s == 403 || s == 503) ? HTTP_E_BROWSER_CHECK : HTTP_OK;
        CHECK_EQ_I64(t, chk(s, "", "ddos-guard"), want);
    }
    CHECK_EQ_I64(t, chk(403 + 65536, "", "ddos-guard"), HTTP_OK);
    CHECK_EQ_I64(t, chk(-403, "", "ddos-guard"), HTTP_OK);
}

static void t_ddos_guard_in_location_does_not_count(ctest_t *t) {
    CHECK_EQ_I64(t, chk(403, "ddos-guard", "Forbidden"), HTTP_OK);
    CHECK_EQ_I64(t, chk(503, "ddos-guard", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(302, "ddos-guard", "ddos-guard"), HTTP_OK);
}

static void t_prefix_window(ctest_t *t) {
    static const char key[] = "ddos-guard";
    size_t kl = sizeof key - 1, at;
    memset(g_body, 'x', sizeof g_body);
    /* match ending exactly at the window end (inside) */
    at = HTTP_CHALLENGE_PREFIX - kl;
    memcpy(g_body + at, key, kl);
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, (core_str_t){g_body, sizeof g_body}), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, (core_str_t){g_body, HTTP_CHALLENGE_PREFIX}), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, (core_str_t){g_body, HTTP_CHALLENGE_PREFIX - 1}), HTTP_OK);
    /* one byte later: crosses the end of the window */
    memset(g_body, 'x', sizeof g_body);
    memcpy(g_body + at + 1, key, kl);
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, (core_str_t){g_body, sizeof g_body}), HTTP_OK);
    CHECK_EQ_I64(t, http_detect_challenge(503, NOTHING, (core_str_t){g_body, HTTP_CHALLENGE_PREFIX + kl}), HTTP_OK);
    CHECK_EQ_I64(t, http_detect_challenge(503, NOTHING, (core_str_t){g_body, HTTP_CHALLENGE_PREFIX + 1}), HTTP_OK);
    CHECK_EQ_I64(t, http_detect_challenge(503, NOTHING, (core_str_t){g_body, HTTP_CHALLENGE_PREFIX + 2}), HTTP_OK);
    /* far beyond the window */
    memset(g_body, 'x', sizeof g_body);
    memcpy(g_body + HTTP_CHALLENGE_PREFIX + 20, key, kl);
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, (core_str_t){g_body, sizeof g_body}), HTTP_OK);
    /* at the very start */
    memset(g_body, 'x', sizeof g_body);
    memcpy(g_body, key, kl);
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, (core_str_t){g_body, sizeof g_body}), HTTP_E_BROWSER_CHECK);
    /* exactly the key */
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, (core_str_t){g_body, kl}), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, (core_str_t){g_body, kl - 1}), HTTP_OK);
}

static void t_check_param_on_redirects(ctest_t *t) {
    CHECK_EQ_I64(t, chk(302, "/search?q=lakos&content=book_any&check=1", ""), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(301, "/x?check=1", ""), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(303, "/x?check=1", ""), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(307, "/x?check=1", ""), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(308, "/x?check=1", ""), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(302, "https://annas-archive.gl/x?a=b&check=1&c=d", ""), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(302, "check=1", ""), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(302, "/x#check=1", ""), HTTP_E_BROWSER_CHECK);
    /* the spec says "contains": a longer value or name still matches */
    CHECK_EQ_I64(t, chk(302, "/x?check=10", ""), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(302, "/x?recheck=1", ""), HTTP_E_BROWSER_CHECK);
}

static void t_check_param_controls(ctest_t *t) {
    CHECK_EQ_I64(t, chk(302, "/x?check=0", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(302, "/x?check=", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(302, "/x?check", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(302, "/x?check1", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(302, "/x?check =1", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(302, "/x?chec=1", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(302, "/x?CHECK=1", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(302, "/x?Check=1", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(302, "/x?check%3D1", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(302, "", ""), HTTP_OK);
    CHECK_EQ_I64(t, chk(302, "/search?q=lakos", ""), HTTP_OK);
    CHECK_EQ_I64(t, http_detect_challenge(302, LIT("/x?chec\0k=1"), NOTHING), HTTP_OK);
}

static void t_check_param_needs_redirect_status(ctest_t *t) {
    int s;
    for (s = 0; s < 1000; s++) {
        http_status_t want = http_is_redirect_status(s) ? HTTP_E_BROWSER_CHECK : HTTP_OK;
        CHECK_EQ_I64(t, chk(s, "/x?check=1", ""), want);
    }
    CHECK_EQ_I64(t, chk(302 + 65536, "/x?check=1", ""), HTTP_OK);
}

static void t_body_check_does_not_apply_to_redirect(ctest_t *t) {
    CHECK_EQ_I64(t, chk(302, "/x", "check=1"), HTTP_OK);
    CHECK_EQ_I64(t, chk(200, "/x?check=1", "ddos-guard"), HTTP_OK);
    CHECK_EQ_I64(t, chk(403, "/x?check=1", "Forbidden"), HTTP_OK);
}

static void t_both_conditions(ctest_t *t) {
    CHECK_EQ_I64(t, chk(403, "/x?check=1", "ddos-guard"), HTTP_E_BROWSER_CHECK);
    CHECK_EQ_I64(t, chk(302, "/x?check=1", "ddos-guard"), HTTP_E_BROWSER_CHECK);
}

static void t_args(ctest_t *t) {
    CHECK_EQ_I64(t, http_detect_challenge(403, (core_str_t){NULL, 1}, LIT("ddos-guard")), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_detect_challenge(403, NOTHING, (core_str_t){NULL, 1}), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_detect_challenge(200, (core_str_t){NULL, 1}, NOTHING), HTTP_E_ARG);
    CHECK_EQ_I64(t, http_detect_challenge(200, NOTHING, NOTHING), HTTP_OK);
}

CTEST_SUITE("http_challenge",
            { "ddos_guard_on_403_and_503", t_ddos_guard_on_403_and_503 },
            { "ddos_guard_controls", t_ddos_guard_controls },
            { "ddos_guard_needs_403_or_503", t_ddos_guard_needs_403_or_503 },
            { "ddos_guard_in_location_does_not_count", t_ddos_guard_in_location_does_not_count },
            { "prefix_window", t_prefix_window },
            { "check_param_on_redirects", t_check_param_on_redirects },
            { "check_param_controls", t_check_param_controls },
            { "check_param_needs_redirect_status", t_check_param_needs_redirect_status },
            { "body_check_does_not_apply_to_redirect", t_body_check_does_not_apply_to_redirect },
            { "both_conditions", t_both_conditions },
            { "args", t_args })
