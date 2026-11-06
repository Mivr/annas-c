#include <string.h>

#include "http/src/internal.h"

/* STUB (test-author task A008). */

bool http_parser_init(http_parser_t *p) {
    (void)p;
    return false;
}

http_status_t http_parser_next(http_parser_t *p, core_str_t *input, http_event_t *ev) {
    (void)p;
    (void)input;
    if (ev != NULL) {
        memset(ev, 0, sizeof *ev);
    }
    return HTTP_E_ARG;
}

http_status_t http_parser_eof(http_parser_t *p, http_event_t *ev) {
    (void)p;
    if (ev != NULL) {
        memset(ev, 0, sizeof *ev);
    }
    return HTTP_E_ARG;
}
