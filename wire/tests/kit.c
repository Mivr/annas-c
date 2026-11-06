/* wire/tests/kit.c: the fake seams and post-conditions of the wire tests. Test code, not product. */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "wire/tests/kit.h"

static kw_t g_kw;
static wire_mem_t g_mem;
static wire_io_t g_io;
static char g_line[KW_CAP];

kw_t *kw(void) {
    return &g_kw;
}

wire_mem_t *kw_mem(void) {
    return &g_mem;
}

/* ---- wire_sys_t ------------------------------------------------------------------------------------------------ */

static int64_t s_read_file(void *ctx, const char *path, void *buf, size_t cap) {
    kw_t *k = ctx;
    if (k->n_log < KW_LOG) {
        strncpy(k->log[k->n_log], path, sizeof k->log[0] - 1u);
    }
    k->n_log++;
    k->last_cap = cap;
    for (size_t i = 0; i < k->n_files; i++) {
        const kw_file_t *f = &k->files[i];
        if (strcmp(f->path, path) != 0) {
            continue;
        }
        if (f->err != 0) {
            return f->err;
        }
        size_t n = f->len < cap ? f->len : cap;
        if (f->data != NULL) {
            memcpy(buf, f->data, n);
        } else {
            memset(buf, f->fill, n);
        }
        return (int64_t)n;
    }
    return -ENOENT;
}

static int64_t s_read_in(void *ctx, void *buf, size_t cap) {
    kw_t *k = ctx;
    k->in_calls++;
    if (cap > k->last_in_cap) {
        k->last_in_cap = cap;
    }
    if (k->in_eintr_left > 0u) {
        k->in_eintr_left--;
        return -EINTR;
    }
    if (k->in_reads < sizeof k->out_at_read / sizeof k->out_at_read[0]) {
        k->out_at_read[k->in_reads] = k->len[1];
    }
    k->in_reads++;
    k->in_eintr_left = k->in_eintr;
    if (k->in_over) {
        return (int64_t)cap + 1;
    }
    if (k->seg >= k->n_segs) {
        return k->in_err;
    }
    const kw_seg_t *s = &k->segs[k->seg];
    size_t n = s->len - k->seg_off;
    if (n > cap) {
        n = cap;
    }
    if (k->in_chunk != 0u && n > k->in_chunk) {
        n = k->in_chunk;
    }
    if (s->data != NULL) {
        memcpy(buf, s->data + k->seg_off, n);
    } else {
        memset(buf, s->fill, n);
    }
    k->seg_off += n;
    if (k->seg_off >= s->len) {
        k->seg++;
        k->seg_off = 0;
    }
    return (int64_t)n;
}

static int64_t s_write_out(void *ctx, int fd, const void *buf, size_t len) {
    kw_t *k = ctx;
    if (fd < 1 || fd > 2) {
        return -EBADF;
    }
    k->w_calls[fd]++;
    if (len == 0u) {
        k->w_empty++;
    }
    if (k->w_eintr_left[fd] > 0u) {
        k->w_eintr_left[fd]--;
        return -EINTR;
    }
    k->w_eintr_left[fd] = (k->w_eintr_fd == 0 || k->w_eintr_fd == fd) ? k->w_eintr : 0u;
    k->w_real[fd]++;
    if (k->w_fail_fd == fd && k->w_fail_nth != 0u && k->w_real[fd] >= k->w_fail_nth) {
        return k->w_fail_err;
    }
    if (k->w_zero_fd == fd) {
        return 0;
    }
    if (k->w_over_fd == fd) {
        return (int64_t)len + 1;
    }
    size_t n = len;
    if (k->accept_max != 0u && n > k->accept_max) {
        n = k->accept_max;
    }
    if (k->len[fd] + n > KW_CAP - 1u) {
        return -ENOSPC;
    }
    memcpy(k->buf[fd] + k->len[fd], buf, n);
    k->len[fd] += n;
    k->buf[fd][k->len[fd]] = '\0';
    return (int64_t)n;
}

static uint64_t s_unix(void *ctx) {
    kw_t *k = ctx;
    k->unix_calls++;
    return k->unix_s;
}

/* The default transport: refuses every connection, so that no test can ever reach a network. */
static int n_connect(void *ctx, core_str_t host, uint16_t port, uint64_t deadline_ms) {
    kw_t *k = ctx;
    (void)host;
    (void)port;
    (void)deadline_ms;
    k->refused++;
    return ECONNREFUSED;
}

static long n_send(void *ctx, const void *buf, size_t len) {
    (void)ctx;
    (void)buf;
    return len > 0u ? -ENOTCONN : 0;
}

static long n_recv(void *ctx, void *buf, size_t cap) {
    (void)ctx;
    (void)buf;
    return cap > 0u ? -ENOTCONN : 0;
}

static int n_wait(void *ctx, bool for_write, uint64_t deadline_ms) {
    (void)ctx;
    (void)for_write;
    (void)deadline_ms;
    return ENOTCONN;
}

static void n_close(void *ctx) {
    (void)ctx;
}

static const tls_stream_transport_ops_t k_refuse_ops = {n_connect, n_send, n_recv, n_wait, n_close};
static tls_stream_transport_t g_refuse;

static const wire_sys_t k_sys = {s_read_file, s_read_in, s_write_out, s_unix};

/* ---- posix_fs_sys_t: one directory, one temp file ----------------------------------------------------------------- */

#define FD_DIR 20
#define FD_TMP 21

static int64_t f_open_dir(void *ctx, const char *path) {
    kw_t *k = ctx;
    k->fs.opens++;
    strncpy(k->fs.dir, path, sizeof k->fs.dir - 1u);
    return k->fs.open_err != 0 ? k->fs.open_err : FD_DIR;
}

static int64_t f_open_tmp(void *ctx, int dir_fd, unsigned mode) {
    kw_t *k = ctx;
    (void)dir_fd;
    (void)mode;
    k->fs.tmp_opens++;
    k->fs.tmp_len = 0;
    return FD_TMP;
}

static int64_t f_pwrite(void *ctx, int fd, const void *buf, size_t len, uint64_t off) {
    kw_t *k = ctx;
    (void)fd;
    if (off != k->fs.tmp_len || off + len > KW_FS_CAP) {
        return -ENOSPC;
    }
    memcpy(k->fs.tmp + off, buf, len);
    k->fs.tmp_len += len;
    return (int64_t)len;
}

static int64_t f_ftruncate(void *ctx, int fd, uint64_t len) {
    kw_t *k = ctx;
    (void)fd;
    if (len <= k->fs.tmp_len) {
        k->fs.tmp_len = (size_t)len;
    }
    return 0;
}

static int64_t f_fsync(void *ctx, int fd) {
    (void)ctx;
    (void)fd;
    return 0;
}

static int64_t f_link(void *ctx, int fd, int dir_fd, const char *name) {
    kw_t *k = ctx;
    (void)fd;
    (void)dir_fd;
    if (k->fs.links > 0u && strcmp(name, k->fs.name) == 0) {
        return -EEXIST;
    }
    k->fs.links++;
    strncpy(k->fs.name, name, sizeof k->fs.name - 1u);
    memcpy(k->fs.content, k->fs.tmp, k->fs.tmp_len);
    k->fs.content_len = k->fs.tmp_len;
    return 0;
}

static int64_t f_close(void *ctx, int fd) {
    kw_t *k = ctx;
    (void)fd;
    k->fs.closes++;
    return 0;
}

static const posix_fs_sys_t k_fs_sys = {f_open_dir, f_open_tmp, f_pwrite, f_ftruncate, f_fsync, f_link, f_close};

static int64_t c_gettime(void *ctx, int64_t *sec, int64_t *nsec) {
    kw_t *k = ctx;
    *sec = (int64_t)k->mono_s;
    *nsec = 0;
    return 0;
}

static const posix_clock_sys_t k_clock_sys = {c_gettime};

/* ---- setup ----------------------------------------------------------------------------------------------------- */

kw_t *kw_reset(void) {
    memset(&g_kw, 0, sizeof g_kw);
    memset(&g_io, 0, sizeof g_io);
    g_kw.unix_s = KW_UNIX_NOW;
    g_kw.mono_s = KW_MONO_S;
    g_kw.envp[0] = NULL;
    g_io.sys = &k_sys;
    g_io.sys_ctx = &g_kw;
    g_io.fs_sys = &k_fs_sys;
    g_io.fs_sys_ctx = &g_kw;
    g_io.clock_sys = &k_clock_sys;
    g_io.clock_sys_ctx = &g_kw;
    g_refuse.ops = &k_refuse_ops;
    g_refuse.ctx = &g_kw;
    g_io.transport = &g_refuse;
    return &g_kw;
}

const wire_io_t *kw_io(void) {
    return &g_io;
}

void kw_use_transport(const tls_stream_transport_t *tr) {
    g_io.transport = tr;
}

static kw_file_t *add_file(const char *path) {
    if (g_kw.n_files >= KW_FILES) {
        return NULL;
    }
    kw_file_t *f = &g_kw.files[g_kw.n_files++];
    f->path = path;
    return f;
}

void kw_file_n(const char *path, const char *data, size_t len) {
    kw_file_t *f = add_file(path);
    if (f != NULL) {
        f->data = data;
        f->len = len;
    }
}

void kw_file(const char *path, const char *data) {
    kw_file_n(path, data, strlen(data));
}

void kw_file_fill(const char *path, char fill, size_t len) {
    kw_file_t *f = add_file(path);
    if (f != NULL) {
        f->data = NULL;
        f->fill = fill;
        f->len = len;
    }
}

void kw_file_err(const char *path, int neg_errno) {
    kw_file_t *f = add_file(path);
    if (f != NULL) {
        f->err = neg_errno;
    }
}

void kw_env(const char *name, const char *value) {
    if (g_kw.n_env >= KW_ENVS) {
        return;
    }
    char *dst = g_kw.envbuf[g_kw.n_env];
    size_t nl = strlen(name);
    size_t vl = strlen(value);
    if (nl + vl + 2u > sizeof g_kw.envbuf[0]) {
        return;
    }
    memcpy(dst, name, nl);
    dst[nl] = '=';
    memcpy(dst + nl + 1u, value, vl + 1u);
    g_kw.envp[g_kw.n_env] = dst;
    g_kw.n_env++;
    g_kw.envp[g_kw.n_env] = NULL;
}

static void add_seg(const char *data, size_t len, char fill) {
    if (g_kw.n_segs >= KW_SEGS) {
        return;
    }
    kw_seg_t *s = &g_kw.segs[g_kw.n_segs++];
    s->data = data;
    s->len = len;
    s->fill = fill;
}

void kw_in_n(const char *data, size_t len) {
    if (len > KW_POOL - g_kw.pool_len) {
        return;
    }
    memcpy(g_kw.pool + g_kw.pool_len, data, len);
    add_seg(g_kw.pool + g_kw.pool_len, len, 0);
    g_kw.pool_len += len;
}

void kw_in(const char *data) {
    kw_in_n(data, strlen(data));
}

void kw_in_fill(char fill, size_t len) {
    add_seg(NULL, len, fill);
}

void kw_ca(const char *pem) {
    kw_env("SSL_CERT_FILE", KW_CA_PATH);
    kw_file(KW_CA_PATH, pem);
}

void kw_net_env(void) {
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("ANNAS_DOWNLOAD_PATH", "/dl");
    kw_ca(KW_CA_FAKE);
}

int kw_run(wire_mode_t mode, int argc, const char *const *argv) {
    g_kw.in_eintr_left = g_kw.in_eintr;
    for (int fd = 1; fd <= 2; fd++) {
        g_kw.w_eintr_left[fd] = (g_kw.w_eintr_fd == 0 || g_kw.w_eintr_fd == fd) ? g_kw.w_eintr : 0u;
    }
    return wire_run(&g_mem, mode, &g_io, argc, argv, g_kw.envp);
}

/* ---- inspection -------------------------------------------------------------------------------------------------- */

const char *kw_out(void) {
    return g_kw.buf[1];
}

const char *kw_err(void) {
    return g_kw.buf[2];
}

size_t kw_out_len(void) {
    return g_kw.len[1];
}

size_t kw_err_len(void) {
    return g_kw.len[2];
}

bool kw_contains_n(const char *hay, size_t len, const char *needle) {
    size_t nl = strlen(needle);
    if (nl == 0u) {
        return true;
    }
    for (size_t i = 0; i + nl <= len; i++) {
        if (memcmp(hay + i, needle, nl) == 0) {
            return true;
        }
    }
    return false;
}

bool kw_contains(const char *hay, const char *needle) {
    return kw_contains_n(hay, strlen(hay), needle);
}

size_t kw_reads(void) {
    return g_kw.n_log;
}

const char *kw_read_path(size_t i) {
    return i < g_kw.n_log && i < KW_LOG ? g_kw.log[i] : "";
}

void kw_check_no_key(ctest_t *t, const char *key) {
    CHECK(t, !kw_contains_n(g_kw.buf[1], g_kw.len[1], key));
    CHECK(t, !kw_contains_n(g_kw.buf[2], g_kw.len[2], key));
}

void kw_check(ctest_t *t, int got, int want, const char *out, const char *err) {
    CHECK_EQ_I64(t, got, want);
    CHECK_EQ_MEM(t, g_kw.buf[1], g_kw.len[1], out, strlen(out));
    CHECK_EQ_MEM(t, g_kw.buf[2], g_kw.len[2], err, strlen(err));
}

void kw_check_no_empty_writes(ctest_t *t) {
    CHECK_EQ_U64(t, g_kw.w_empty, 0);
}

const char *kw_out_line(size_t i) {
    const char *p = g_kw.buf[1];
    const char *end = p + g_kw.len[1];
    while (i > 0u && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (nl == NULL) {
            p = end;
            break;
        }
        p = nl + 1;
        i--;
    }
    if (i > 0u || p >= end) {
        g_line[0] = '\0';
        return g_line;
    }
    const char *nl = memchr(p, '\n', (size_t)(end - p));
    size_t n = nl != NULL ? (size_t)(nl - p) : (size_t)(end - p);
    memcpy(g_line, p, n);
    g_line[n] = '\0';
    return g_line;
}

size_t kw_check_reply_lines(ctest_t *t) {
    static const char head[] = "{\"jsonrpc\":\"2.0\",\"id\":";
    size_t lines = 0;
    size_t i = 0;
    const char *buf = g_kw.buf[1];
    size_t len = g_kw.len[1];
    CHECK(t, len == 0u || buf[len - 1u] == '\n');
    while (i < len) {
        const char *nl = memchr(buf + i, '\n', len - i);
        size_t n = nl != NULL ? (size_t)(nl - (buf + i)) : len - i;
        CHECK(t, n > sizeof head && memcmp(buf + i, head, sizeof head - 1u) == 0);
        CHECK(t, n > 0u && buf[i + n - 1u] == '}');
        CHECK(t, memchr(buf + i, '\r', n) == NULL);
        lines++;
        i += n + 1u;
    }
    return lines;
}

void kw_clear_output(void) {
    memset(g_kw.buf[1], 0, g_kw.len[1]);
    memset(g_kw.buf[2], 0, g_kw.len[2]);
    g_kw.len[1] = 0;
    g_kw.len[2] = 0;
    g_kw.n_log = 0;
    g_kw.seg = 0;
    g_kw.seg_off = 0;
    g_kw.n_segs = 0;
    g_kw.pool_len = 0;
    memset(g_kw.w_calls, 0, sizeof g_kw.w_calls);
    memset(g_kw.w_real, 0, sizeof g_kw.w_real);
    g_kw.w_empty = 0;
    g_kw.in_reads = 0;
    g_kw.in_calls = 0;
}

void kw_env_clear(void) {
    g_kw.n_env = 0;
    g_kw.envp[0] = NULL;
}

void kw_files_clear(void) {
    g_kw.n_files = 0;
}
