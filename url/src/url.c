#include "url/url.h"

/* Stubs: the implementation task replaces these. Every function fails. */
url_status_t url_pct_encode(core_buf_t *out, core_str_t in) {
    (void)out;
    (void)in;
    return URL_E_ARG;
}

url_status_t url_pct_decode(core_buf_t *out, core_str_t in) {
    (void)out;
    (void)in;
    return URL_E_ARG;
}

bool url_base_is_valid(core_str_t s) {
    (void)s;
    return false;
}

url_status_t url_base_normalize(core_str_t raw, core_buf_t *out) {
    (void)raw;
    (void)out;
    return URL_E_ARG;
}

url_status_t url_build_search(core_buf_t *out, core_str_t base, core_str_t query, url_content_t content) {
    (void)out;
    (void)base;
    (void)query;
    (void)content;
    return URL_E_ARG;
}

url_status_t url_build_fast_download(core_buf_t *out, core_str_t base, core_str_t md5, core_str_t key) {
    (void)out;
    (void)base;
    (void)md5;
    (void)key;
    return URL_E_ARG;
}

url_status_t url_build_md5_page(core_buf_t *out, core_str_t base, core_str_t md5) {
    (void)out;
    (void)base;
    (void)md5;
    return URL_E_ARG;
}

url_status_t url_build_scidb_path(core_buf_t *out, core_str_t base, core_str_t doi) {
    (void)out;
    (void)base;
    (void)doi;
    return URL_E_ARG;
}

url_status_t url_build_scidb_download(core_buf_t *out, core_str_t base, core_str_t doi) {
    (void)out;
    (void)base;
    (void)doi;
    return URL_E_ARG;
}

url_status_t url_redact_key(core_buf_t *out, core_str_t in) {
    (void)out;
    (void)in;
    return URL_E_ARG;
}

bool url_md5_is_valid(core_str_t s) {
    (void)s;
    return false;
}

bool url_md5_normalize(core_str_t in, char *out) {
    (void)in;
    (void)out;
    return false;
}

bool url_doi_recognize(core_str_t in, core_str_t *trimmed) {
    (void)in;
    (void)trimmed;
    return false;
}
