/* tls_stream/src/bio.c: the mbedTLS BIO callbacks over the transport seam. They block (wait() on EAGAIN), check
 * the deadline before every transport call, and record WHY they failed in o->cause, because mbedTLS only sees
 * a negative number. */
#include <errno.h>
#include <stddef.h>

#include "mbedtls/net_sockets.h"

#include "tls_stream/src/internal.h"

/* The transport contract names -EAGAIN and -EWOULDBLOCK; they are one value here, so one compare covers both. */
_Static_assert(EAGAIN == EWOULDBLOCK, "EAGAIN and EWOULDBLOCK are the same errno");

bool tls_deadline_passed(const tls_obj_t *o)
{
    return o->deadline != 0U && o->cfg.clock.ops->now_ms(o->cfg.clock.ctx) >= o->deadline;
}

static void fail(tls_obj_t *o, ports_err_t why)
{
    if (o->cause == PORTS_OK) {
        o->cause = why;
    }
}

/* Block until the transport is ready. 0 = go and retry, -1 = failed (o->cause says why). */
static int wait_ready(tls_obj_t *o, bool for_write)
{
    for (;;) {
        /* a passed deadline is the same outcome as a transport that reports ETIMEDOUT, and wait() is not called */
        int e = tls_deadline_passed(o) ? ETIMEDOUT
                                       : o->cfg.transport.ops->wait(o->cfg.transport.ctx, for_write, o->deadline);
        if (e == 0) {
            return 0;
        }
        if (e != EINTR) {
            fail(o, e == ETIMEDOUT ? PORTS_E_TIMEOUT : PORTS_E_NET);
            return -1;
        }
    }
}

int tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    tls_obj_t *o = ctx;
    for (;;) {
        long n;
        if (tls_deadline_passed(o)) {
            fail(o, PORTS_E_TIMEOUT);
            return MBEDTLS_ERR_NET_SEND_FAILED;
        }
        n = o->cfg.transport.ops->send(o->cfg.transport.ctx, buf, len);
        if (n > 0 && (size_t) n <= len) {
            return (int) n;
        }
        if (n == -(long) EAGAIN) {
            if (wait_ready(o, true) != 0) {
                return MBEDTLS_ERR_NET_SEND_FAILED;
            }
        } else if (n != -(long) EINTR) {
            fail(o, PORTS_E_NET);
            return MBEDTLS_ERR_NET_SEND_FAILED;
        }
    }
}

int tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    tls_obj_t *o = ctx;
    for (;;) {
        long n;
        if (tls_deadline_passed(o)) {
            fail(o, PORTS_E_TIMEOUT);
            return MBEDTLS_ERR_NET_RECV_FAILED;
        }
        n = o->cfg.transport.ops->recv(o->cfg.transport.ctx, buf, len);
        if (n == 0) {
            return 0; /* transport end of file: mbedTLS reports CONN_EOF, never close_notify */
        }
        /* n == 0 returned above, so a negative n (cast to size_t, far above any buffer length) fails this test */
        if ((size_t) n <= len) {
            return (int) n;
        }
        if (n == -(long) EAGAIN) {
            if (wait_ready(o, false) != 0) {
                return MBEDTLS_ERR_NET_RECV_FAILED;
            }
        } else if (n != -(long) EINTR) {
            fail(o, PORTS_E_NET);
            return MBEDTLS_ERR_NET_RECV_FAILED;
        }
    }
}

ports_err_t tls_transport_connect(tls_obj_t *o, core_str_t host, uint16_t port)
{
    for (;;) {
        int e;
        if (tls_deadline_passed(o)) {
            return PORTS_E_TIMEOUT;
        }
        e = o->cfg.transport.ops->connect(o->cfg.transport.ctx, host, port, o->deadline);
        if (e == 0) {
            return PORTS_OK;
        }
        if (e != EINTR) {
            return e == ETIMEDOUT ? PORTS_E_TIMEOUT : PORTS_E_NET;
        }
    }
}
