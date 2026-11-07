#include "http/src/internal.h"

/* True if hay[at..at+n) is `lower` in any letter case; `upper` is the same text in capitals. */
static bool match_ci_at(core_str_t hay, size_t at, const char *lower, const char *upper, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (hay.ptr[at + i] != lower[i] && hay.ptr[at + i] != upper[i]) {
            return false;
        }
    }
    return true;
}

static bool has_ddos_guard(core_str_t body) {
    static const char k_lower[] = "ddos-guard";
    static const char k_upper[] = "DDOS-GUARD";
    size_t n = sizeof k_lower - 1;
    size_t at;
    /* Only the first HTTP_CHALLENGE_PREFIX bytes count; a body shorter than the needle runs no iteration. */
    for (at = 0; at + n <= body.len && at + n <= HTTP_CHALLENGE_PREFIX; at++) {
        if (match_ci_at(body, at, k_lower, k_upper, n)) {
            return true;
        }
    }
    return false;
}

http_status_t http_detect_challenge(int status, core_str_t location, core_str_t body_prefix) {
    static const char k_check[] = "check=1";
    if ((location.ptr == NULL && location.len > 0) || (body_prefix.ptr == NULL && body_prefix.len > 0)) {
        return HTTP_E_ARG;
    }
    if ((status == 403 || status == 503) && has_ddos_guard(body_prefix)) {
        return HTTP_E_BROWSER_CHECK;
    }
    if (http_is_redirect_status(status) &&
        core_str_find(location, (core_str_t){k_check, sizeof k_check - 1}) != CORE_NPOS) {
        return HTTP_E_BROWSER_CHECK;
    }
    return HTTP_OK;
}
