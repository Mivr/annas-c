/* tls_stream/tls_stream.h: a `ports_stream_t` (ports/ports.h) that speaks TLS 1.2 / 1.3 as a CLIENT over a
 * byte transport, on the gate's pinned mbedTLS. This module decides whether a download can be trusted, so
 * its guarantees are stated here in full and pinned by tests/.
 *
 * TRUST (all of it mandatory, none of it configurable)
 *   - The server certificate chain is verified (VERIFY_REQUIRED) against the CA bundle in the config and
 *     only against it: unknown issuer, expired, not yet valid, or a chain that does not reach a trusted
 *     root -> PORTS_E_TLS. A bundle that cannot be parsed, or that holds no usable certificate, trusts
 *     nothing: connect fails with PORTS_E_TLS and makes no transport connection. A block that does not parse
 *     is skipped as long as at least one usable certificate remains.
 *   - The host name given to connect() is both the SNI name and the name the certificate must match
 *     (subject alternative name / common name rules of mbedTLS). A mismatch -> PORTS_E_TLS.
 *   - Certificate time comes from the injected wall clock (below), never from the OS. A wall clock that
 *     returns 0 ("unknown") makes every certificate "not yet valid": fail closed.
 *   - A peer close_notify is end of stream (read OK, n == 0). A transport end of file WITHOUT close_notify
 *     (at a record boundary or inside a record, during the handshake or after it) is PORTS_E_NET and is
 *     NEVER reported as end of stream: it is a truncated message. A fatal alert from the peer is
 *     PORTS_E_TLS. (mbedTLS reports a close_notify as MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY and a transport
 *     EOF as 0 / MBEDTLS_ERR_SSL_CONN_EOF; they must not be folded together.)
 *
 * MEMORY (no malloc anywhere in this module)
 *   mbedTLS needs heap (MBEDTLS_PLATFORM_MEMORY). The caller hands in a fixed arena (`heap`, `heap_size`);
 *   the module installs its own allocator over it with mbedtls_platform_set_calloc_free() at connect().
 *   The mbedTLS contexts live inside the tls_stream_t object (caller storage). TLS_STREAM_HEAP_MIN is the
 *   smallest arena init() accepts (enough for any handshake of the test PKI and 16 KiB records, with an
 *   empty-ish CA bundle); TLS_STREAM_HEAP_BYTES(n) is the arena to provide for a CA bundle of n bytes (the
 *   parsed certificates are held in the arena for the life of the connection). If the arena runs out at any
 *   point, connect() fails with PORTS_E_NET and everything it took is returned to the arena.
 *   After close(), and after any failed connect(), tls_stream_heap_used() is 0.
 *
 * PROCESS-GLOBAL HOOKS (a property of mbedTLS, documented so nobody is surprised)
 *   The allocator and the time function are process-global in mbedTLS. Therefore AT MOST ONE tls_stream_t
 *   is connected at any time in a process: connect() on a second object while another is connected fails
 *   with PORTS_E_NET and disturbs nothing. The hooks point at the object that is (or was last) connected.
 *
 * ERRORS (exactly the ports.h meaning)
 *   resolve / connect / transport failure, EOF without close_notify, arena exhausted, connect() while open,
 *   another object connected                                  -> PORTS_E_NET
 *   the deadline (absolute ms on cfg.clock; 0 = none)          -> PORTS_E_TIMEOUT
 *   handshake, verification, host-name, peer alert, bad CA     -> PORTS_E_TLS
 *   not open (never connected, failed, closed)                  -> PORTS_E_CLOSED
 *   NULL / invalid argument, or an object init() did not accept -> PORTS_E_ARG
 *   Check order in every call: object valid (else ARG) -> open (else CLOSED, except connect) -> arguments
 *   (else ARG) -> [zero-length write: OK] -> sticky error -> deadline -> work.
 *   connect() checks: object, arguments, already open (NET), another object connected (NET), deadline
 *   already passed (TIMEOUT, no transport call), then the work.
 *
 * DEADLINE. connect()'s deadline_ms bounds that connect and every later read/write of the connection. At
 *   the start of every read and write, and before every transport call, `now >= deadline` -> PORTS_E_TIMEOUT
 *   (so a read of buffered data after the deadline also times out). The transport's own wait()/connect()
 *   are given the same absolute deadline.
 *
 * STICKY FAILURE. A read or write that fails with NET, TIMEOUT or TLS breaks the connection: it stays "open"
 *   (close() returns PORTS_OK and releases everything) but every later read and write returns the same
 *   error. A failed connect leaves the stream not open (the transport was closed if it had connected).
 *   A read that returned EOF (close_notify) keeps returning OK with n == 0, whatever the clock says.
 *
 * Reuse: after close() or a failed connect() the object can connect again, to the same or another host,
 * with the same config. deinit() ends its life; init() again makes it usable (init() must not be called on
 * an object that is open: deinit() it first).
 */
#ifndef TLS_STREAM_TLS_STREAM_H
#define TLS_STREAM_TLS_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/str.h"
#include "ports/ports.h"

/* ---- seam 1: the byte transport (errno-style; nothing here blocks except where stated) ---------------- */

/* All results are errno-style. connect returns 0 or a positive errno; send and recv return a byte count
 * or -errno; wait returns 0 or a positive errno.
 *   connect(host, port, deadline_ms): resolve and connect. 0 = connected. ETIMEDOUT = the deadline passed
 *     (-> PORTS_E_TIMEOUT); EINTR is retried by the adapter (never visible to the caller); any other errno
 *     (ECONNREFUSED, EHOSTUNREACH, ENOENT, ...) -> PORTS_E_NET. After a non-zero result the transport is
 *     NOT connected and close() is not called.
 *   send(buf, len), len >= 1: n in 1..len accepted (a short count is normal, the adapter loops). 0 is a
 *     transport that makes no progress -> PORTS_E_NET. -EINTR: retried. -EAGAIN / -EWOULDBLOCK: the
 *     adapter calls wait(for_write = true) and retries. Any other -errno (ECONNRESET, EPIPE,
 *     ETIMEDOUT, ...) -> PORTS_E_NET.
 *   recv(buf, cap), cap >= 1: n in 1..cap received; 0 = end of file; -EINTR: retried; -EAGAIN /
 *     -EWOULDBLOCK: the adapter calls wait(for_write = false) and retries; any other -errno -> PORTS_E_NET.
 *   wait(for_write, deadline_ms): block until the transport is readable (or writable) or the deadline
 *     (absolute, on cfg.clock, 0 = none) passes. 0 = ready (the adapter then re-checks the clock itself);
 *     ETIMEDOUT = the deadline passed (-> PORTS_E_TIMEOUT); EINTR: retried; any other errno -> PORTS_E_NET.
 *   close(): release the connection; called exactly once per successful connect, never otherwise.
 * A transport call that returns more than it was given room for (n > len / n > cap) is PORTS_E_NET. */
typedef struct {
    int (*connect)(void *ctx, core_str_t host, uint16_t port, uint64_t deadline_ms);
    long (*send)(void *ctx, const void *buf, size_t len);
    long (*recv)(void *ctx, void *buf, size_t cap);
    int (*wait)(void *ctx, bool for_write, uint64_t deadline_ms);
    void (*close)(void *ctx);
} tls_stream_transport_ops_t;

typedef struct {
    const tls_stream_transport_ops_t *ops;
    void *ctx;
} tls_stream_transport_t;

/* The production transport over POSIX sockets: getaddrinfo, a non-blocking connect + poll with the
 * remaining deadline (computed on `clock`), send with MSG_NOSIGNAL, recv, poll, close. getaddrinfo itself
 * cannot be interrupted: the deadline is checked before and after it. Exactly one connection at a time.
 * Everything else (the logic above) is tested through fakes; this table is only reachable by a real
 * network and is exercised by the orchestrator's live check. tls_stream_sock_init() is false for a NULL
 * `s` or a clock without now_ms. The returned transport is valid while `s` lives.
 * Contract checkable without a network: connect() with an empty host, a host longer than 255 bytes or port 0
 * returns EINVAL; with a deadline that has already passed it returns ETIMEDOUT before resolving anything;
 * send/recv/wait on a transport that is not connected return -ENOTCONN / ENOTCONN; close() on one that is
 * not connected does nothing. */
typedef struct {
    ports_clock_t clock;
    int fd; /* -1 = not connected */
} tls_stream_sock_t;

bool tls_stream_sock_init(tls_stream_sock_t *s, ports_clock_t clock);
tls_stream_transport_t tls_stream_sock_transport(tls_stream_sock_t *s);

/* ---- seam 2: wall-clock time for certificate validity ---------------------------------------------------- */

/* unix_seconds(): seconds since 1970-01-01T00:00:00Z, or 0 when unknown (then every certificate is "not yet
 * valid"). The module installs a trampoline with mbedtls_platform_set_time() at connect(); the trampoline
 * calls this function with `ctx` of the connecting object and returns it as mbedtls_time_t. No ambient
 * time() is ever used. The monotonic cfg.clock is for deadlines only and is never used for certificates. */
typedef struct {
    uint64_t (*unix_seconds)(void *ctx);
    void *ctx;
} tls_stream_wall_t;

/* ---- the adapter ----------------------------------------------------------------------------------------- */

/* Sizing. Measured by the test author's reference implementation with the gate's mbedTLS 3.6.7 config:
 * one object needs about 1.5-2 KiB; a handshake plus 60 KB of bulk data each way peaks at 54432 arena
 * bytes with a one-certificate CA bundle of 619 bytes (the two 16 KiB record buffers dominate), the same
 * for ECDSA, RSA-2048 and chain identities and for TLS 1.2 and 1.3; each byte of CA bundle costs about 2.6
 * arena bytes. tests/sizing_test.c pins that every case runs inside these limits.
 *   TLS_STREAM_OBJECT_BYTES: storage of one object (the mbedTLS contexts live in it).
 *   TLS_STREAM_HEAP_MIN:     smallest arena init() accepts.
 *   TLS_STREAM_HEAP_BYTES(n): arena for a CA bundle of n bytes (n includes the trailing NUL). */
#define TLS_STREAM_OBJECT_BYTES 4096u
#define TLS_STREAM_HEAP_MIN 98304u
#define TLS_STREAM_HEAP_BYTES(ca_pem_len) ((size_t)TLS_STREAM_HEAP_MIN + 3u * (size_t)(ca_pem_len))

typedef struct {
    union {
        max_align_t align;
        unsigned char bytes[TLS_STREAM_OBJECT_BYTES];
    } opaque; /* treat as opaque; use only the functions below */
} tls_stream_t;

typedef struct {
    tls_stream_transport_t transport; /* ops (all five) and ctx; must outlive the object */
    ports_clock_t clock;              /* monotonic ms, for deadlines */
    tls_stream_wall_t wall;           /* Unix seconds, for certificate validity */
    core_str_t ca_pem;                /* trust anchors, PEM; the slice INCLUDES one trailing NUL byte (the
                                         mbedTLS convention); borrowed, must outlive the connection */
    void *heap;                       /* the arena */
    size_t heap_size;                 /* >= TLS_STREAM_HEAP_MIN */
} tls_stream_config_t;

/* Prepare `s` (not open). False, and `s` unusable (every port call then returns PORTS_E_ARG), when: s or
 * cfg is NULL; any transport op, clock.ops->now_ms, or wall.unix_seconds is NULL; ca_pem.ptr is NULL, ca_pem.len
 * is 0 or its last byte is not NUL; heap is NULL or heap_size < TLS_STREAM_HEAP_MIN. Nothing is parsed or
 * allocated yet. */
bool tls_stream_init(tls_stream_t *s, const tls_stream_config_t *cfg);

/* The port view of `s` (valid while `s` lives). A NULL `s` gives a port whose calls return PORTS_E_ARG. */
ports_stream_t tls_stream_port(tls_stream_t *s);

/* Close if open, release everything, make `s` unusable until init() again. NULL is a no-op. */
void tls_stream_deinit(tls_stream_t *s);

/* "TLSv1.3" or "TLSv1.2" while a connection is open (also when broken), else NULL. */
const char *tls_stream_version(const tls_stream_t *s);

/* Arena bytes in use now, and the highest value since init(); 0 for NULL / unusable objects. */
size_t tls_stream_heap_used(const tls_stream_t *s);
size_t tls_stream_heap_peak(const tls_stream_t *s);

#endif
