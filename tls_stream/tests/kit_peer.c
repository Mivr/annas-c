/* Test scaffolding: the glue between the kit pipe and the gate's TLS test peer. */
#include <stddef.h>
#include <string.h>

#include "tls_peer.h"
#include "tls_stream/tests/kit.h"

static unsigned char g_rx[KIT_IO_MAX];
static size_t g_rx_len;
static unsigned char g_tmp[4096];

static size_t drain_out(void)
{
    size_t total = 0;
    size_t n = tls_peer_take(g_tmp, sizeof g_tmp);
    while (n > 0) {
        kit_inbox_push(g_tmp, n);
        total += n;
        n = tls_peer_take(g_tmp, sizeof g_tmp);
    }
    return total;
}

static size_t drain_rx(void)
{
    size_t total = 0;
    size_t n = tls_peer_received(g_tmp, sizeof g_tmp);
    while (n > 0 && g_rx_len + n <= sizeof g_rx) {
        memcpy(g_rx + g_rx_len, g_tmp, n);
        g_rx_len += n;
        total += n;
        n = tls_peer_received(g_tmp, sizeof g_tmp);
    }
    return total;
}

void kit_peer_ingest(const void *data, size_t len)
{
    int rounds;
    size_t progress = 1;
    (void) tls_peer_put((const unsigned char *) data, len);
    for (rounds = 0; rounds < 256 && progress > 0; rounds++) {
        (void) tls_peer_step();
        progress = drain_out() + drain_rx();
    }
}

int kit_peer_start(int identity, bool tls12_only)
{
    g_rx_len = 0;
    kit_inbox_truncate(0); /* whatever an earlier peer left in the pipe is gone with it */
    return tls_peer_start((tls_peer_identity_t) identity,
                          tls12_only ? TLS_PEER_TLS12_ONLY : TLS_PEER_BOTH_VERSIONS);
}

void kit_peer_stop(void)
{
    tls_peer_stop();
}

int kit_peer_send(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *) data;
    int rc = 0;
    while (len > 0 && rc == 0) {
        size_t n = len < 8192U ? len : 8192U;
        rc = tls_peer_send(p, n);
        (void) drain_out();
        p += n;
        len -= n;
    }
    return rc;
}

int kit_peer_close_notify(void)
{
    int rc = tls_peer_close_notify();
    (void) drain_out();
    return rc;
}

int kit_peer_alert(int alert)
{
    int rc = tls_peer_alert((tls_peer_alert_t) alert);
    (void) drain_out();
    return rc;
}

const unsigned char *kit_peer_data(void)
{
    return g_rx;
}

size_t kit_peer_data_len(void)
{
    return g_rx_len;
}

void kit_peer_data_clear(void)
{
    g_rx_len = 0;
}

bool kit_peer_established(void)
{
    return tls_peer_established() != 0;
}

bool kit_peer_client_closed(void)
{
    return tls_peer_client_closed() != 0;
}

int kit_peer_error(void)
{
    return tls_peer_error();
}

const char *kit_peer_version(void)
{
    return tls_peer_version();
}

static core_str_t g_ca;
static char g_ca_buf[8192];
static char g_big[400000];

static core_str_t build(const char *a, const char *b)
{
    size_t la = strlen(a);
    size_t lb = b == NULL ? 0 : strlen(b);
    core_str_t r = {NULL, 0};
    if (la + lb + 1U <= sizeof g_ca_buf) {
        memcpy(g_ca_buf, a, la);
        if (b != NULL) {
            memcpy(g_ca_buf + la, b, lb);
        }
        g_ca_buf[la + lb] = '\0';
        r.ptr = g_ca_buf;
        r.len = la + lb + 1U;
    }
    g_ca = r;
    return g_ca;
}

core_str_t kit_ca_root(void)
{
    return build(tls_peer_ca_pem(), NULL);
}

core_str_t kit_ca_other(void)
{
    return build(tls_peer_other_ca_pem(), NULL);
}

core_str_t kit_ca_both(bool root_first)
{
    return root_first ? build(tls_peer_ca_pem(), tls_peer_other_ca_pem())
                      : build(tls_peer_other_ca_pem(), tls_peer_ca_pem());
}

core_str_t kit_ca_garbage(void)
{
    return build("-----BEGIN CERTIFICATE-----\nbm90IGEgY2VydGlmaWNhdGUgYXQgYWxs\n-----END CERTIFICATE-----\n",
                 NULL);
}

core_str_t kit_ca_mixed(void)
{
    static const char bad[] = "-----BEGIN CERTIFICATE-----\nbm90IGEgY2VydGlmaWNhdGUgYXQgYWxs\n-----END CERTIFICATE-----\n";
    return build(bad, tls_peer_ca_pem());
}

core_str_t kit_ca_blank(void)
{
    return build("\n", NULL);
}

core_str_t kit_ca_repeated(size_t copies)
{
    size_t one = strlen(tls_peer_ca_pem());
    core_str_t r = {NULL, 0};
    size_t i;
    if (copies * one + 1U > sizeof g_big) {
        return r;
    }
    for (i = 0; i < copies; i++) {
        memcpy(g_big + i * one, tls_peer_ca_pem(), one);
    }
    g_big[copies * one] = '\0';
    r.ptr = g_big;
    r.len = copies * one + 1U;
    return r;
}

static unsigned char g_req[KIT_IO_MAX];
static unsigned char g_resp[KIT_IO_MAX];

static bool read_back(ports_stream_t p, size_t resp_len, size_t cap)
{
    size_t got = 0;
    while (got < resp_len) {
        size_t n = 0;
        size_t room = resp_len - got < cap ? resp_len - got : cap;
        if (p.ops->read(p.ctx, g_resp + got, room, &n) != PORTS_OK || n == 0U || n > room) {
            return false;
        }
        got += n;
    }
    return kit_same(g_resp, resp_len, 2U);
}

bool kit_exchange(ports_stream_t p, size_t req_len, size_t resp_len, size_t cap)
{
    if (req_len > sizeof g_req || resp_len > sizeof g_resp) {
        return false;
    }
    kit_peer_data_clear();
    kit_fill(g_req, req_len, 1U);
    if (p.ops->write(p.ctx, g_req, req_len) != PORTS_OK || kit_peer_data_len() != req_len ||
        !kit_same(kit_peer_data(), req_len, 1U)) {
        return false;
    }
    kit_fill(g_resp, resp_len, 2U);
    if (kit_peer_send(g_resp, resp_len) != 0) {
        return false;
    }
    memset(g_resp, 0, resp_len);
    return read_back(p, resp_len, cap);
}
