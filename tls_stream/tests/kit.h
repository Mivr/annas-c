/* Test scaffolding for tls_stream: an in-memory byte pipe between the adapter under test and the gate's TLS
 * test peer (kit_peer.c), a fault-injecting transport (kit_net.c) and a manual clock. Test code, not product.
 * One global kit per test process (every test runs in its own forked child). Both .c files are sources of
 * every c_test that uses them, because only a c_test may name the peer. Plain C types only: this header
 * does not include tls_peer.h. */
#ifndef TLS_STREAM_TESTS_KIT_H
#define TLS_STREAM_TESTS_KIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/str.h"
#include "ports/ports.h"
#include "tls_stream/tls_stream.h"

#define KIT_PORT 443u
#define KIT_T0 1000u            /* the manual clock starts here (ms) */
#define KIT_DEADLINE 60000u     /* the usual absolute deadline (ms) */
#define KIT_IO_MAX 131072u      /* capacity of the pipe and of the capture buffers */
#define KIT_HEAP_BYTES 393216u  /* the arena handed to the adapter in tests */
#define KIT_TIME_VALID 1798761600u
#define KIT_TIME_EXPIRED_WINDOW 1593561600u
#define KIT_TIME_FUTURE_WINDOW 4417977600u

typedef enum {
    KIT_OP_CONNECT = 0,
    KIT_OP_SEND,
    KIT_OP_RECV,
    KIT_OP_WAIT,
    KIT_OP_CLOSE,
    KIT_OP_COUNT
} kit_op_t;

typedef struct {
    /* ---- inputs (set after kit_reset(), before the call they affect) ---- */
    uint64_t wall_s;          /* the wall clock; 0 = unknown */
    kit_op_t fail_op;         /* inject `fail_err` into the fail_nth-th call (1-based, 0 = off) of this op */
    uint32_t fail_nth;
    int fail_err;             /* errno value; for send/recv it is returned negated; EAGAIN arms wait() */
    bool fail_persist;        /* every call from fail_nth on fails */
    size_t send_max;          /* a send accepts at most this many bytes (0 = all) */
    size_t recv_max;          /* a recv delivers at most this many bytes (0 = all) */
    size_t first_recv_max;    /* the first recv delivers at most this many bytes (0 = recv_max rule) */
    size_t inbound_limit;     /* after this many bytes were delivered in total, recv reports EOF */
    bool eof_when_idle;       /* an empty pipe means transport EOF (0) instead of EAGAIN */
    bool mute;                /* the peer hears nothing: sends are swallowed */
    uint64_t connect_advance_ms; /* connect() moves the clock forward by this */
    bool wait_eintr_first;    /* the first wait() returns EINTR */
    bool wait_timeout;        /* wait() moves the clock to the deadline and returns ETIMEDOUT */
    bool wait_ready;          /* wait() advances the clock by wait_advance_ms and returns 0 */
    uint64_t wait_advance_ms;
    bool send_zero;           /* send() returns 0 */
    bool send_over;           /* send() returns len + 1 */
    bool recv_over;           /* recv() returns cap + 1 */
    size_t heap_size;         /* arena size handed to the adapter (0 = KIT_HEAP_BYTES) */
    /* ---- outputs ---- */
    uint32_t calls[KIT_OP_COUNT];
    uint32_t wait_write_calls;   /* wait() calls with for_write */
    char host[128];              /* host of the last connect() */
    uint16_t port;
    uint64_t connect_deadline;   /* deadline passed to the last connect() */
    uint64_t wait_deadline;      /* deadline passed to the last wait() */
    unsigned char sent_first[4096]; /* the first bytes the client sent (the ClientHello) */
    size_t sent_first_len;
    size_t sent_total;
    size_t delivered_total;
} kit_t;

kit_t *kit(void);
/* Zero everything, stop the peer, clock = KIT_T0, wall = KIT_TIME_VALID, no limits, no faults. */
void kit_reset(void);

/* manual monotonic clock */
ports_clock_t kit_clock(void);
uint64_t kit_now(void);
void kit_clock_advance(uint64_t ms);

/* The adapter's config over the kit transport, kit clock, kit wall clock and the static arena. */
tls_stream_config_t kit_config(core_str_t ca_pem);
bool kit_init(tls_stream_t *s, core_str_t ca_pem);
/* Start the peer (identity = tls_peer_identity_t), init `s` with `ca_pem`, take the port and connect to
 * `host`:KIT_PORT with `deadline`. Returns connect's result. Does NOT reset the kit. */
ports_err_t kit_open(tls_stream_t *s, ports_stream_t *p, int identity, bool tls12_only, core_str_t ca_pem,
                     const char *host, uint64_t deadline);
/* deinit `s` and stop the peer. */
void kit_done(tls_stream_t *s);

/* trust anchors, each with its trailing NUL included in the slice */
core_str_t kit_ca_root(void);
core_str_t kit_ca_other(void);
core_str_t kit_ca_both(bool root_first);
core_str_t kit_ca_garbage(void);
core_str_t kit_ca_mixed(void); /* an unparsable block, then the root CA */
core_str_t kit_ca_blank(void); /* whitespace only */
/* the root CA repeated `copies` times (copies * strlen + 1 bytes); empty slice if it does not fit */
core_str_t kit_ca_repeated(size_t copies);

/* ---- the peer ---- */
int kit_peer_start(int identity, bool tls12_only); /* 0 or the peer's error */
void kit_peer_stop(void);
/* Application data from the peer to the client (queued in the pipe). Returns the peer's result. */
int kit_peer_send(const void *data, size_t len);
int kit_peer_close_notify(void);
int kit_peer_alert(int alert);
/* what the client has written so far (application data) */
const unsigned char *kit_peer_data(void);
size_t kit_peer_data_len(void);
void kit_peer_data_clear(void);
bool kit_peer_established(void);
bool kit_peer_client_closed(void);
int kit_peer_error(void);
const char *kit_peer_version(void);

/* ---- the pipe ---- */
size_t kit_inbox_len(void);
/* Keep only the first `keep` bytes of what the peer has produced and the client has not read yet. */
void kit_inbox_truncate(size_t keep);
/* The next recv() delivers at most `k` bytes (re-arms first_recv_max). */
void kit_split_next_recv(size_t k);
/* Flip a bit of byte `index` of what the peer has produced and the client has not read yet. */
void kit_inbox_corrupt(size_t index);
/* internal: used by kit_peer.c and kit_net.c */
void kit_inbox_push(const void *data, size_t len);
void kit_peer_ingest(const void *data, size_t len);

/* One request/response exchange: write() `req_len` bytes (pattern seed 1); the peer must have received exactly
 * them; the peer then sends `resp_len` bytes (seed 2) and they are read back with read() calls of capacity
 * `cap`. True iff every call returned PORTS_OK with 1 <= n <= cap and every byte matches. */
bool kit_exchange(ports_stream_t p, size_t req_len, size_t resp_len, size_t cap);

/* Deterministic test payload: byte i of stream `seed` */
void kit_fill(unsigned char *buf, size_t len, unsigned seed);
bool kit_same(const unsigned char *buf, size_t len, unsigned seed);

#endif
