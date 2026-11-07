/* tls_stream/src/sock.c: the production transport over POSIX sockets (see tls_stream.h, seam 1). One connection at
 * a time, non-blocking, every wait bounded by the absolute deadline on the injected clock. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "tls_stream/tls_stream.h"

#define HOST_MAX 255u

static uint64_t now_ms(const tls_stream_sock_t *s)
{
    return s->clock.ops->now_ms(s->clock.ctx);
}

static bool passed(const tls_stream_sock_t *s, uint64_t deadline_ms)
{
    return deadline_ms != 0U && now_ms(s) >= deadline_ms;
}

/* poll() timeout for the time left: -1 = none, else 1..INT_MAX milliseconds (never 0 before the deadline). */
static int poll_timeout(const tls_stream_sock_t *s, uint64_t deadline_ms)
{
    uint64_t left;
    if (deadline_ms == 0U) {
        return -1;
    }
    left = deadline_ms - now_ms(s);
    return left > (uint64_t) INT_MAX ? INT_MAX : (int) left;
}

/* 0 = ready (or an error/hang-up the next call will report), ETIMEDOUT, or another errno. */
static int wait_fd(const tls_stream_sock_t *s, short events, uint64_t deadline_ms)
{
    for (;;) {
        struct pollfd pfd;
        int rc;
        if (passed(s, deadline_ms)) {
            return ETIMEDOUT;
        }
        pfd.fd = s->fd;
        pfd.events = events; /* revents is output only: poll() sets it */
        rc = poll(&pfd, 1, poll_timeout(s, deadline_ms));
        if (rc > 0) {
            return 0;
        }
        if (rc == 0) {
            return ETIMEDOUT;
        }
        if (errno != EINTR) {
            return errno;
        }
    }
}

static int resolve_error(int gai)
{
    if (gai == EAI_SYSTEM) {
        return errno != 0 ? errno : EIO;
    }
    return gai == EAI_AGAIN ? EHOSTUNREACH : ENOENT;
}

/* Finish a non-blocking connect: wait for writability, then read SO_ERROR. */
static int finish_connect(const tls_stream_sock_t *s, uint64_t deadline_ms)
{
    int err; /* written by getsockopt() whenever it succeeds, the only way to read it */
    socklen_t len = (socklen_t) sizeof err;
    int e = wait_fd(s, POLLOUT, deadline_ms);
    if (e != 0) {
        return e;
    }
    if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
        return errno;
    }
    return err;
}

static int try_address(tls_stream_sock_t *s, const struct addrinfo *ai, uint64_t deadline_ms)
{
    int e;
    s->fd = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, ai->ai_protocol);
    if (s->fd < 0) { /* socket() returned -1, which is what s->fd now holds */
        return errno;
    }
    if (connect(s->fd, ai->ai_addr, ai->ai_addrlen) == 0) {
        return 0;
    }
    e = errno == EINPROGRESS || errno == EINTR ? finish_connect(s, deadline_ms) : errno;
    if (e != 0) {
        (void) close(s->fd);
        s->fd = -1;
    }
    return e;
}

static int connect_any(tls_stream_sock_t *s, const struct addrinfo *list, uint64_t deadline_ms)
{
    int e = ENOENT;
    const struct addrinfo *ai;
    for (ai = list; ai != NULL; ai = ai->ai_next) {
        e = try_address(s, ai, deadline_ms);
        if (e == 0 || e == ETIMEDOUT) {
            return e;
        }
    }
    return e;
}

static int sock_connect(void *ctx, core_str_t host, uint16_t port, uint64_t deadline_ms)
{
    tls_stream_sock_t *s = ctx;
    char name[HOST_MAX + 1u] = ""; /* all zero: host.len <= HOST_MAX leaves the terminator in place */
    char service[8];            /* "65535" and the NUL: a uint16_t port always fits */
    struct addrinfo hints = {.ai_socktype = SOCK_STREAM, .ai_flags = AI_NUMERICSERV};
    struct addrinfo *list = NULL;
    int rc;
    int e;
    if (host.ptr == NULL || host.len == 0U || host.len > HOST_MAX || port == 0U) {
        return EINVAL;
    }
    if (s->fd >= 0) {
        return EISCONN;
    }
    if (passed(s, deadline_ms)) {
        return ETIMEDOUT;
    }
    memcpy(name, host.ptr, host.len);
    (void) snprintf(service, sizeof service, "%u", (unsigned) port); /* cannot truncate or fail, see service[] */
    rc = getaddrinfo(name, service, &hints, &list);
    if (rc != 0) {
        return resolve_error(rc);
    }
    e = passed(s, deadline_ms) ? ETIMEDOUT : connect_any(s, list, deadline_ms);
    freeaddrinfo(list);
    return e;
}

static long sock_send(void *ctx, const void *buf, size_t len)
{
    const tls_stream_sock_t *s = ctx;
    ssize_t n;
    if (s->fd < 0) {
        return -(long) ENOTCONN;
    }
    n = send(s->fd, buf, len, MSG_NOSIGNAL);
    return n < 0 ? -(long) errno : (long) n;
}

static long sock_recv(void *ctx, void *buf, size_t cap)
{
    const tls_stream_sock_t *s = ctx;
    ssize_t n;
    if (s->fd < 0) {
        return -(long) ENOTCONN;
    }
    n = recv(s->fd, buf, cap, 0);
    return n < 0 ? -(long) errno : (long) n;
}

static int sock_wait(void *ctx, bool for_write, uint64_t deadline_ms)
{
    const tls_stream_sock_t *s = ctx;
    if (s->fd < 0) {
        return ENOTCONN;
    }
    return wait_fd(s, for_write ? POLLOUT : POLLIN, deadline_ms);
}

static void sock_close(void *ctx)
{
    tls_stream_sock_t *s = ctx;
    if (s->fd >= 0) {
        (void) close(s->fd);
        s->fd = -1;
    }
}

static const tls_stream_transport_ops_t k_ops = {sock_connect, sock_send, sock_recv, sock_wait, sock_close};

bool tls_stream_sock_init(tls_stream_sock_t *s, ports_clock_t clock)
{
    if (s == NULL || clock.ops == NULL || clock.ops->now_ms == NULL) {
        return false;
    }
    s->clock = clock;
    s->fd = -1;
    return true;
}

tls_stream_transport_t tls_stream_sock_transport(tls_stream_sock_t *s)
{
    tls_stream_transport_t t;
    t.ops = &k_ops;
    t.ctx = s;
    return t;
}
