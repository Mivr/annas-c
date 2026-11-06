#include "http/src/internal.h"

static char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool match_ci_at(core_str_t hay, size_t at, const char *needle, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (lower_ascii(hay.ptr[at + i]) != needle[i]) {
            return false;
        }
    }
    return true;
}

static bool has_ddos_guard(core_str_t body) {
    static const char k_text[] = "ddos-guard";
    size_t n = sizeof k_text - 1;
    size_t at;
    if (body.len > HTTP_CHALLENGE_PREFIX) {
        body.len = HTTP_CHALLENGE_PREFIX;
    }
    if (body.len < n) {
        return false;
    }
    for (at = 0; at + n <= body.len; at++) {
        if (match_ci_at(body, at, k_text, n)) {
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
