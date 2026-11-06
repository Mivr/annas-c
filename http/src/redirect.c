#include "http/src/internal.h"

/* STUB (test-author task A008). */

bool http_is_redirect_status(int status) {
    (void)status;
    return false;
}

http_status_t http_redirect_resolve(http_redirects_t *r, const http_url_t *cur, int status, core_str_t location,
                                    core_buf_t *out) {
    (void)r;
    (void)cur;
    (void)status;
    (void)location;
    (void)out;
    return HTTP_E_ARG;
}
