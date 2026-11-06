#include "json/json.h"

/* STUB (A003): the DOM serialiser always fails. */
bool json_write(core_buf_t *b, const json_value_t *v) {
    (void)b;
    (void)v;
    return false;
}
