#include "url/url.h"

#include <string.h>

#include "core/parse.h"

#define LIT(s) ((core_str_t){(s), sizeof(s) - 1U})

static bool bad_ptr(core_str_t s) {
    return s.ptr == NULL && s.len > 0;
}

static url_status_t finish(const core_buf_t *out) {
    return core_buf_overflow(out) ? URL_E_OVERFLOW : URL_OK;
}

static bool is_unreserved(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '.' || c == '_' || c == '~';
}

static void put_pct(core_buf_t *out, core_str_t in, bool keep_slash) {
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < in.len; i++) {
        char c = in.ptr[i];
        if (is_unreserved(c) || (keep_slash && c == '/')) {
            core_buf_put_char(out, c);
        } else {
            unsigned char u = (unsigned char)c;
            char t[3] = {'%', hex[u >> 4U], hex[u & 15U]};
            core_buf_put(out, t, 3);
        }
    }
}

url_status_t url_pct_encode(core_buf_t *out, core_str_t in) {
    if (out == NULL || bad_ptr(in)) {
        return URL_E_ARG;
    }
    put_pct(out, in, false);
    return finish(out);
}

static bool decode_ok(core_str_t in) {
    for (size_t i = 0; i < in.len; i++) {
        if (in.ptr[i] == '%') {
            if (in.len - i < 3U || core_parse_hex_nibble(in.ptr[i + 1U]) < 0 ||
                core_parse_hex_nibble(in.ptr[i + 2U]) < 0) {
                return false;
            }
            i += 2U;
        }
    }
    return true;
}

url_status_t url_pct_decode(core_buf_t *out, core_str_t in) {
    if (out == NULL || bad_ptr(in)) {
        return URL_E_ARG;
    }
    if (!decode_ok(in)) {
        return URL_E_INVALID;
    }
    for (size_t i = 0; i < in.len; i++) {
        if (in.ptr[i] == '%') {
            int hi = core_parse_hex_nibble(in.ptr[i + 1U]);
            int lo = core_parse_hex_nibble(in.ptr[i + 2U]);
            core_buf_put_char(out, (char)(unsigned char)((hi << 4) | lo));
            i += 2U;
        } else {
            core_buf_put_char(out, in.ptr[i]);
        }
    }
    return finish(out);
}

bool url_base_is_valid(core_str_t s) {
    if (s.len == 0 || s.ptr == NULL) {
        return false;
    }
    for (size_t i = 0; i < s.len; i++) {
        char c = s.ptr[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

url_status_t url_base_normalize(core_str_t raw, core_buf_t *out) {
    if (out == NULL || bad_ptr(raw)) {
        return URL_E_ARG;
    }
    if (raw.len == 0) {
        core_buf_put_str(out, core_str_from_cstr(URL_DEFAULT_BASE));
        return finish(out);
    }
    if (core_str_starts_with(raw, LIT("https://"))) {
        raw.ptr += 8;
        raw.len -= 8U;
    } else if (core_str_starts_with(raw, LIT("http://"))) {
        raw.ptr += 7;
        raw.len -= 7U;
    }
    if (raw.len > 0 && raw.ptr[raw.len - 1U] == '/') {
        raw.len--;
    }
    if (!url_base_is_valid(raw)) {
        return URL_E_INVALID;
    }
    core_buf_put_str(out, raw);
    return finish(out);
}

/* ---- builders --------------------------------------------------------------------------------- */

static void put_head(core_buf_t *out, core_str_t base) {
    core_buf_put_str(out, LIT("https://"));
    core_buf_put_str(out, base);
}

url_status_t url_build_search(core_buf_t *out, core_str_t base, core_str_t query, url_content_t content) {
    if (out == NULL || bad_ptr(base) || bad_ptr(query) ||
        (content != URL_CONTENT_BOOK && content != URL_CONTENT_JOURNAL)) {
        return URL_E_ARG;
    }
    if (!url_base_is_valid(base)) {
        return URL_E_INVALID;
    }
    put_head(out, base);
    core_buf_put_str(out, LIT("/search?q="));
    put_pct(out, query, false);
    core_buf_put_str(out, content == URL_CONTENT_BOOK ? LIT("&content=book_any") : LIT("&content=journal"));
    return finish(out);
}

url_status_t url_build_fast_download(core_buf_t *out, core_str_t base, core_str_t md5, core_str_t key) {
    if (out == NULL || bad_ptr(base) || bad_ptr(md5) || bad_ptr(key)) {
        return URL_E_ARG;
    }
    if (!url_base_is_valid(base) || !url_md5_is_valid(md5) || key.len == 0) {
        return URL_E_INVALID;
    }
    put_head(out, base);
    core_buf_put_str(out, LIT("/dyn/api/fast_download.json?md5="));
    core_buf_put_str(out, md5);
    core_buf_put_str(out, LIT("&key="));
    put_pct(out, key, false);
    return finish(out);
}

url_status_t url_build_md5_page(core_buf_t *out, core_str_t base, core_str_t md5) {
    if (out == NULL || bad_ptr(base) || bad_ptr(md5)) {
        return URL_E_ARG;
    }
    if (!url_base_is_valid(base) || !url_md5_is_valid(md5)) {
        return URL_E_INVALID;
    }
    put_head(out, base);
    core_buf_put_str(out, LIT("/md5/"));
    core_buf_put_str(out, md5);
    return finish(out);
}

url_status_t url_build_scidb_path(core_buf_t *out, core_str_t base, core_str_t doi) {
    if (out == NULL || bad_ptr(base) || bad_ptr(doi)) {
        return URL_E_ARG;
    }
    if (!url_base_is_valid(base) || !core_str_starts_with(doi, LIT("10."))) {
        return URL_E_INVALID;
    }
    put_head(out, base);
    core_buf_put_str(out, LIT("/scidb/"));
    put_pct(out, doi, true);
    return finish(out);
}

url_status_t url_build_scidb_download(core_buf_t *out, core_str_t base, core_str_t doi) {
    if (out == NULL || bad_ptr(base) || bad_ptr(doi)) {
        return URL_E_ARG;
    }
    if (!url_base_is_valid(base) || !core_str_starts_with(doi, LIT("10."))) {
        return URL_E_INVALID;
    }
    put_head(out, base);
    core_buf_put_str(out, LIT("/scidb?doi="));
    put_pct(out, doi, false);
    return finish(out);
}

/* ---- redaction -------------------------------------------------------------------------------- */

static bool is_key_prefix(char c) {
    return c == '?' || c == '&' || c == ';' || c == '#' || c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\v' || c == '\f' || c == '"' || c == '\'';
}

static bool is_value_end(char c) {
    return c == '&' || c == '#' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
           c == '\f' || c == '"' || c == '\'';
}

static bool key_at(core_str_t in, size_t i) {
    return in.len - i >= 4U && memcmp(in.ptr + i, "key=", 4) == 0 && (i == 0 || is_key_prefix(in.ptr[i - 1U]));
}

url_status_t url_redact_key(core_buf_t *out, core_str_t in) {
    if (out == NULL || bad_ptr(in)) {
        return URL_E_ARG;
    }
    size_t i = 0;
    while (i < in.len) {
        if (key_at(in, i)) {
            core_buf_put_str(out, LIT("key="));
            core_buf_put_str(out, LIT(URL_REDACTED));
            i += 4U;
            while (i < in.len && !is_value_end(in.ptr[i])) {
                i++;
            }
        } else {
            core_buf_put_char(out, in.ptr[i]);
            i++;
        }
    }
    return finish(out);
}

/* ---- identifiers ------------------------------------------------------------------------------ */

static bool all_hex32(core_str_t s) {
    if (s.len != 32U || s.ptr == NULL) {
        return false;
    }
    for (size_t i = 0; i < 32U; i++) {
        if (core_parse_hex_nibble(s.ptr[i]) < 0) {
            return false;
        }
    }
    return true;
}

bool url_md5_is_valid(core_str_t s) {
    if (!all_hex32(s)) {
        return false;
    }
    for (size_t i = 0; i < 32U; i++) {
        char c = s.ptr[i];
        if (c >= 'A' && c <= 'F') {
            return false;
        }
    }
    return true;
}

bool url_md5_normalize(core_str_t in, char *out) {
    if (out == NULL || !all_hex32(in)) {
        return false;
    }
    for (size_t i = 0; i < 32U; i++) {
        out[i] = core_ascii_lower(in.ptr[i]);
    }
    out[32] = '\0';
    return true;
}

bool url_doi_recognize(core_str_t in, core_str_t *trimmed) {
    if (bad_ptr(in)) {
        return false;
    }
    core_str_t t = core_str_trim(in);
    if (!core_str_starts_with(t, LIT("10."))) {
        return false;
    }
    if (trimmed != NULL) {
        *trimmed = t;
    }
    return true;
}
