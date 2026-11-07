#include "annas/src/internal.h"

const char *annas_code_name(annas_code_t code) {
    switch (code) {
    case ANNAS_OK: return "OK";
    case ANNAS_E_ARG: return "E_ARG";
    case ANNAS_E_CONFIG: return "E_CONFIG";
    case ANNAS_E_BROWSER_CHECK: return "E_BROWSER_CHECK";
    case ANNAS_E_HTTP: return "E_HTTP";
    case ANNAS_E_API: return "E_API";
    case ANNAS_E_NET: return "E_NET";
    case ANNAS_E_TIMEOUT: return "E_TIMEOUT";
    case ANNAS_E_FS: return "E_FS";
    case ANNAS_E_PARSE: return "E_PARSE";
    default: return "E_UNKNOWN";
    }
}

int annas_exit_code(annas_code_t code) {
    switch (code) {
    case ANNAS_OK: return 0;
    case ANNAS_E_ARG:
    case ANNAS_E_CONFIG: return 2;
    case ANNAS_E_BROWSER_CHECK: return 3;
    case ANNAS_E_HTTP:
    case ANNAS_E_API: return 4;
    case ANNAS_E_NET:
    case ANNAS_E_TIMEOUT: return 5;
    case ANNAS_E_FS: return 6;
    case ANNAS_E_PARSE: return 7;
    default: return 1;
    }
}

bool annas_error_line(const annas_error_t *err, core_buf_t *out) {
    if (err == NULL || out == NULL || err->code == ANNAS_OK) {
        return false;
    }
    core_str_t msg = {err->message, err->message_len};
    core_buf_put_str(out, core_str_from_cstr("annas-c: "));
    core_buf_put_str(out, core_str_from_cstr(annas_code_name(err->code)));
    core_buf_put_str(out, core_str_from_cstr(": "));
    core_buf_put_str(out, msg);
    return !core_buf_overflow(out);
}

static void put_account(core_buf_t *out, const annas_fast_result_t *r) {
    if (r->account_len > 0) {
        core_buf_put_str(out, core_str_from_cstr("\nAccount: "));
        core_buf_put(out, r->account, r->account_len);
    }
}

bool annas_format_download_url(core_buf_t *out, const annas_fast_result_t *r) {
    if (out == NULL || r == NULL) {
        return false;
    }
    core_buf_put_str(out, core_str_from_cstr("Download URL: "));
    core_buf_put(out, r->url, r->url_len);
    put_account(out, r);
    return !core_buf_overflow(out);
}

bool annas_format_downloaded(core_buf_t *out, const annas_download_result_t *r) {
    if (out == NULL || r == NULL) {
        return false;
    }
    core_buf_put_str(out, core_str_from_cstr("Book downloaded successfully to: "));
    core_buf_put(out, r->path, r->path_len);
    put_account(out, &r->fast);
    return !core_buf_overflow(out);
}
