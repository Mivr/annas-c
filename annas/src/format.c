#include "annas/src/internal.h"

/* STUB (test-author task A012). */

const char *annas_code_name(annas_code_t code) {
    (void)code;
    return "";
}

int annas_exit_code(annas_code_t code) {
    (void)code;
    return -1;
}

bool annas_error_line(const annas_error_t *err, core_buf_t *out) {
    (void)err;
    (void)out;
    return false;
}

bool annas_format_download_url(core_buf_t *out, const annas_fast_result_t *r) {
    (void)out;
    (void)r;
    return false;
}

bool annas_format_downloaded(core_buf_t *out, const annas_download_result_t *r) {
    (void)out;
    (void)r;
    return false;
}
