/* wire/tests/kit.h: test-only scaffolding for the wire tests (not part of the product). One kit per test process (every
 * ctest case runs in a fresh fork, so state never leaks between cases). It provides the fake seams of wire_io_t: files
 * (with a log of the paths asked for), a scripted stdin, capturing stdout/stderr writers with every kind of fault, the
 * fake posix_fs syscall table (one directory, one temp file), a fake monotonic clock, and the process environment.
 * Talks to wire only through wire/wire.h. The TLS test peer glue (peer.c) is separate: only a c_test may name the peer. */
#ifndef WIRE_TESTS_KIT_H
#define WIRE_TESTS_KIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ctest/ctest.h"
#include "wire/wire.h"

#define KW_KEY "FAKEKEY-not-a-real-key-0123456789"
#define KW_MD5 "0123456789abcdef0123456789abcdef"
#define KW_CA_FAKE "-----BEGIN CERTIFICATE-----\nZmFrZQ==\n-----END CERTIFICATE-----\n"
#define KW_CA_PATH "/test/ca.pem"
#define KW_UNIX_NOW 1798761600u /* 2027-01-01: inside the validity window of the test PKI */
#define KW_MONO_S 100u          /* the fake monotonic clock reads this many seconds */
#define KW_FILES 8u
#define KW_SEGS 24u
#define KW_LOG 24u
#define KW_ENVS 16u
#define KW_CAP ((size_t)(1024u * 1024u)) /* capacity of each capture buffer */
#define KW_FS_CAP ((size_t)65536)
#define KW_POOL ((size_t)(1536u * 1024u))

typedef struct {
    const char *path;
    const char *data; /* NULL: `len` bytes of `fill` */
    size_t len;
    char fill;
    int err; /* negative errno to return instead, 0 = none */
} kw_file_t;

typedef struct {
    const char *data; /* NULL: `len` bytes of `fill` */
    size_t len;
    char fill;
} kw_seg_t;

typedef struct {
    unsigned opens; /* open_dir calls */
    char dir[300];  /* the last path */
    int open_err;   /* negative errno returned by open_dir, 0 = ok */
    unsigned tmp_opens;
    unsigned char tmp[KW_FS_CAP];
    size_t tmp_len;
    unsigned links; /* link_tmp calls that succeeded */
    char name[300];
    unsigned char content[KW_FS_CAP];
    size_t content_len;
    unsigned closes;
} kw_fs_t;

typedef struct {
    /* files */
    kw_file_t files[KW_FILES];
    size_t n_files;
    char log[KW_LOG][300]; /* every path read_file was asked for, in order */
    size_t n_log;          /* number of calls (may exceed KW_LOG) */
    size_t last_cap;       /* the cap of the last read_file call */
    /* stdin */
    char pool[KW_POOL]; /* kw_in_n copies here */
    size_t pool_len;
    kw_seg_t segs[KW_SEGS];
    size_t n_segs;
    size_t seg;
    size_t seg_off;
    size_t in_chunk;     /* a read returns at most this many bytes (0 = no limit); it never spans segments */
    unsigned in_eintr;   /* -EINTR this many times before every successful read */
    unsigned in_eintr_left;
    int in_err;          /* negative errno returned once the segments are used up (0 = end of input) */
    bool in_over;        /* read_in returns cap + 1 */
    unsigned in_reads;   /* calls of read_in that were not -EINTR */
    unsigned in_calls;   /* all calls */
    size_t out_at_read[64]; /* stdout bytes captured when read number i (not -EINTR) started */
    size_t last_in_cap;  /* the largest cap asked of read_in */
    /* stdout (fd 1) and stderr (fd 2) */
    char buf[3][KW_CAP];
    size_t len[3];
    size_t accept_max;   /* a write accepts at most this many bytes (0 = all) */
    unsigned w_eintr;    /* -EINTR this many times before every write that is not failed */
    int w_eintr_fd;      /* only for this fd (0 = both) */
    unsigned w_eintr_left[3];
    int w_fail_fd;       /* writes to this fd fail from the w_fail_nth-th real attempt on (0 = off) */
    unsigned w_fail_nth;
    int w_fail_err;      /* negative errno */
    int w_zero_fd;       /* writes to this fd return 0 */
    int w_over_fd;       /* writes to this fd return len + 1 */
    unsigned w_calls[3]; /* all calls */
    unsigned w_real[3];  /* calls that were not -EINTR */
    unsigned w_empty;    /* calls with len 0 (a contract violation of wire) */
    /* clocks */
    unsigned refused;    /* connect() calls on the default transport (it always refuses: no test touches a network) */
    uint64_t unix_s;
    uint64_t mono_s;
    unsigned unix_calls;
    /* environment */
    char envbuf[KW_ENVS][512];
    char *envp[KW_ENVS + 1u];
    size_t n_env;
    /* posix_fs */
    kw_fs_t fs;
} kw_t;

kw_t *kw_reset(void); /* zero everything, unix_s = KW_UNIX_NOW, mono_s = KW_MONO_S; returns the kit */
kw_t *kw(void);

void kw_file(const char *path, const char *data);
void kw_file_n(const char *path, const char *data, size_t len);
void kw_file_fill(const char *path, char fill, size_t len);
void kw_file_err(const char *path, int neg_errno);
void kw_env(const char *name, const char *value);
void kw_env_clear(void);   /* an empty process environment */
void kw_files_clear(void); /* no files at all (the path log is kept) */
void kw_in(const char *data);
void kw_in_n(const char *data, size_t len); /* the bytes are copied */
void kw_in_fill(char fill, size_t len);
void kw_ca(const char *pem); /* SSL_CERT_FILE=KW_CA_PATH and that file = pem */
void kw_net_env(void);       /* ANNAS_SECRET_KEY, ANNAS_DOWNLOAD_PATH=/dl, and the fake CA bundle */

const wire_io_t *kw_io(void);
void kw_use_transport(const tls_stream_transport_t *tr);
wire_mem_t *kw_mem(void);

/* Forget what was captured so far (stdout, stderr, the path log, counters of the writers and readers). */
void kw_clear_output(void);

int kw_run(wire_mode_t mode, int argc, const char *const *argv);
#define KW_ARGC(...) ((int)(sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *)))
#define KW_RUN(mode, ...) kw_run((mode), KW_ARGC(__VA_ARGS__), (const char *const[]){__VA_ARGS__})
#define KW_CLI(...) KW_RUN(WIRE_CLI, "annas-c", __VA_ARGS__)
#define KW_CLI0() KW_RUN(WIRE_CLI, "annas-c")
#define KW_MCP0() KW_RUN(WIRE_MCP, "annas-c-mcp")

const char *kw_out(void);
const char *kw_err(void);
size_t kw_out_len(void);
size_t kw_err_len(void);

bool kw_contains(const char *hay, const char *needle);
bool kw_contains_n(const char *hay, size_t len, const char *needle);
/* The path log: the number of read_file calls and the i-th path ("" beyond the log). */
size_t kw_reads(void);
const char *kw_read_path(size_t i);

/* Post-conditions. */
/* The key (raw) occurs in neither stdout nor stderr. */
void kw_check_no_key(ctest_t *t, const char *key);
/* Exit code, stdout, stderr exactly. */
void kw_check(ctest_t *t, int got, int want, const char *out, const char *err);
/* The writer was never called with len 0 and stdout/stderr contents are as given (NULL = not checked). */
void kw_check_no_empty_writes(ctest_t *t);
/* One reply per line in stdout: every line is `{"jsonrpc":"2.0","id":` ... `}`, output ends with '\n'. Returns the
 * number of lines. */
size_t kw_check_reply_lines(ctest_t *t);

/* The i-th line of stdout (without its '\n') as a pointer into a static copy; "" beyond the output. */
const char *kw_out_line(size_t i);

/* Server messages */
#define KW_INIT_REQ "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}"
#define KW_INIT_REPLY                                                                                           \
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{\"tools\":{}}," \
    "\"serverInfo\":{\"name\":\"annas-c\",\"version\":\"0.1\"}}}"
#define KW_PING_REQ "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"ping\"}"
#define KW_PING_REPLY "{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{}}"

/* ---- peer.c (only in tests whose srcs list tests/peer.c and which depend on the gate's tls_peer) ---------------- */

/* kw_reset + kw_peer_reset; the process env gets the key, ANNAS_DOWNLOAD_PATH=/dl, ANNAS_BASE_URL=KW_PEER_HOST and
 * SSL_CERT_FILE (the file holds the peer's CA); the io uses the peer transport. */
void kw_e2e_setup(void);
const char *kw_peer_ca(void);       /* PEM of the root CA the peer's certificates chain to */
const char *kw_peer_other_ca(void); /* an unrelated CA */
#define KW_PEER_HOST "tls.test.invalid"
/* A transport that is an in-memory TLS server. Every connect() starts a fresh server; the first request on a
 * connection (up to its blank line) is answered with the response scripted for the connection's index, then a
 * close_notify. kw_peer_reset() forgets all scripts and counters. */
tls_stream_transport_t kw_peer_transport(void);
void kw_peer_reset(void);
void kw_peer_identity(int identity); /* tls_peer_identity_t, default TLS_PEER_EC_VALID */
void kw_peer_respond(unsigned conn, const char *response);
void kw_peer_connect_err(int positive_errno); /* connect() returns this on every connection (0 = off) */
unsigned kw_peer_connects(void);
unsigned kw_peer_closes(void);
const char *kw_peer_host(unsigned conn);
uint16_t kw_peer_port(unsigned conn);
uint64_t kw_peer_deadline(unsigned conn);
const char *kw_peer_request(unsigned conn); /* the plaintext request received on connection `conn` */
/* "HTTP/1.1 <status> <reason>\r\nContent-Length: n\r\nConnection: close\r\n\r\n<body>" in a static ring of buffers. */
const char *kw_http(int status, const char *reason, const char *body);
#define KW_API_OK_BODY(url) "{\"download_url\":\"" url "\",\"account_fast_download_info\":{\"downloads_left\":24}}"

#endif
