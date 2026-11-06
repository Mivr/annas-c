#include "http/src/internal.h"

/* STUB (test-author task A008): every function fails. */

bool http_host_valid(core_str_t s) {
    (void)s;
    return false;
}

http_status_t http_url_split(core_str_t url, http_url_t *out) {
    (void)url;
    (void)out;
    return HTTP_E_ARG;
}
