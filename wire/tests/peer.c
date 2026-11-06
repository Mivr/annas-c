/* wire/tests/peer.c: an in-memory TLS server as a tls_stream transport, for the end-to-end tests of wire. Test code, not
 * product; only a c_test may name the gate's tls_peer. Each connect() starts a fresh peer; the bytes the client sends go
 * into the peer, the bytes it produces are queued for recv(). The request (up to its blank line) is collected from the
 * peer's application data; the scripted response and a close_notify are produced on the first recv() that finds
 * the queue empty after the request is complete. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "tls_peer.h"
#include "wire/tests/kit.h"

#define PEER_CONNS 6u
#define PEER_REQ 4096u
#define PEER_QUEUE 65536u

typedef struct {
    char host[128];
    uint16_t port;
    uint64_t deadline;
    char request[PEER_REQ + 1u];
    size_t request_len;
    const char *response;
    bool responded;
} peer_conn_t;

static struct {
    peer_conn_t conn[PEER_CONNS];
    unsigned connects;
    unsigned closes;
    int identity;
    int connect_err;
    unsigned char queue[PEER_QUEUE];
    size_t queue_len;
    unsigned cur;
    char http[8][70000];
    unsigned http_i;
} g;
static unsigned char g_tmp[2048];

const char *kw_peer_ca(void) {
    return tls_peer_ca_pem();
}

const char *kw_peer_other_ca(void) {
    return tls_peer_other_ca_pem();
}

void kw_peer_reset(void) {
    tls_peer_stop();
    memset(&g, 0, sizeof g);
    g.identity = (int)TLS_PEER_EC_VALID;
}

void kw_peer_identity(int identity) {
    g.identity = identity;
}

void kw_peer_respond(unsigned conn, const char *response) {
    if (conn < PEER_CONNS) {
        g.conn[conn].response = response;
    }
}

void kw_peer_connect_err(int positive_errno) {
    g.connect_err = positive_errno;
}

unsigned kw_peer_connects(void) {
    return g.connects;
}

unsigned kw_peer_closes(void) {
    return g.closes;
}

const char *kw_peer_host(unsigned conn) {
    return conn < PEER_CONNS ? g.conn[conn].host : "";
}

uint16_t kw_peer_port(unsigned conn) {
    return conn < PEER_CONNS ? g.conn[conn].port : 0u;
}

uint64_t kw_peer_deadline(unsigned conn) {
    return conn < PEER_CONNS ? g.conn[conn].deadline : 0u;
}

const char *kw_peer_request(unsigned conn) {
    return conn < PEER_CONNS ? g.conn[conn].request : "";
}

const char *kw_http(int status, const char *reason, const char *body) {
    char *b = g.http[g.http_i++ % 8u];
    (void)snprintf(b, sizeof g.http[0], "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                   status, reason, strlen(body), body);
    return b;
}

static void drain_out(void) {
    size_t n = tls_peer_take(g_tmp, sizeof g_tmp);
    while (n > 0u) {
        if (n <= sizeof g.queue - g.queue_len) {
            memcpy(g.queue + g.queue_len, g_tmp, n);
            g.queue_len += n;
        }
        n = tls_peer_take(g_tmp, sizeof g_tmp);
    }
}

static void drain_rx(void) {
    peer_conn_t *c = &g.conn[g.cur];
    size_t n = tls_peer_received(g_tmp, sizeof g_tmp);
    while (n > 0u) {
        if (n <= PEER_REQ - c->request_len) {
            memcpy(c->request + c->request_len, g_tmp, n);
            c->request_len += n;
            c->request[c->request_len] = '\0';
        }
        n = tls_peer_received(g_tmp, sizeof g_tmp);
    }
}

static void pump(void) {
    for (int i = 0; i < 8; i++) {
        (void)tls_peer_step();
        drain_out();
        drain_rx();
    }
}

static int t_connect(void *ctx, core_str_t host, uint16_t port, uint64_t deadline_ms) {
    (void)ctx;
    unsigned idx = g.connects++;
    if (idx >= PEER_CONNS) {
        return ECONNREFUSED;
    }
    peer_conn_t *c = &g.conn[idx];
    size_t n = host.len < sizeof c->host - 1u ? host.len : sizeof c->host - 1u;
    memcpy(c->host, host.ptr, n);
    c->port = port;
    c->deadline = deadline_ms;
    if (g.connect_err != 0) {
        return g.connect_err;
    }
    g.cur = idx;
    g.queue_len = 0;
    return tls_peer_start((tls_peer_identity_t)g.identity, TLS_PEER_BOTH_VERSIONS) == 0 ? 0 : EIO;
}

static long t_send(void *ctx, const void *buf, size_t len) {
    (void)ctx;
    if (tls_peer_put((const unsigned char *)buf, len) != 0) {
        return -EIO;
    }
    pump();
    return (long)len;
}

static void respond_if_ready(void) {
    peer_conn_t *c = &g.conn[g.cur];
    if (c->responded || strstr(c->request, "\r\n\r\n") == NULL) {
        return;
    }
    c->responded = true;
    if (c->response != NULL) {
        (void)tls_peer_send((const unsigned char *)c->response, strlen(c->response));
    }
    (void)tls_peer_close_notify();
    drain_out();
}

static long t_recv(void *ctx, void *buf, size_t cap) {
    (void)ctx;
    if (g.queue_len == 0u) {
        respond_if_ready();
    }
    size_t n = g.queue_len < cap ? g.queue_len : cap;
    memcpy(buf, g.queue, n);
    memmove(g.queue, g.queue + n, g.queue_len - n);
    g.queue_len -= n;
    return (long)n;
}

static int t_wait(void *ctx, bool for_write, uint64_t deadline_ms) {
    (void)ctx;
    (void)for_write;
    (void)deadline_ms;
    return 0;
}

static void t_close(void *ctx) {
    (void)ctx;
    g.closes++;
    tls_peer_stop();
}

static const tls_stream_transport_ops_t k_ops = {t_connect, t_send, t_recv, t_wait, t_close};

tls_stream_transport_t kw_peer_transport(void) {
    tls_stream_transport_t t;
    t.ops = &k_ops;
    t.ctx = NULL;
    return t;
}

static tls_stream_transport_t g_transport;

void kw_e2e_setup(void) {
    (void)kw_reset();
    kw_peer_reset();
    g_transport = kw_peer_transport();
    kw_use_transport(&g_transport);
    kw_env("ANNAS_SECRET_KEY", KW_KEY);
    kw_env("ANNAS_DOWNLOAD_PATH", "/dl");
    kw_env("ANNAS_BASE_URL", KW_PEER_HOST);
    kw_ca(tls_peer_ca_pem());
}
