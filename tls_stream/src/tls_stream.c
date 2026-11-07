/* tls_stream/src/tls_stream.c: a ports_stream_t over mbedTLS (client only). See tls_stream/tls_stream.h for the
 * contract; the trust rules live here: VERIFY_REQUIRED, the host name checked, certificate time from the injected
 * wall clock, and a transport end of file without close_notify is an error, never an end of stream. */
#include <stddef.h>
#include <string.h>

#include "mbedtls/platform.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/psa_util.h"
#include "psa/crypto.h"

#include "tls_stream/src/internal.h"

#define HOST_MAX 255u

_Static_assert(sizeof(tls_obj_t) <= sizeof(tls_stream_t), "the object must fit in tls_stream_t");

static tls_obj_t *obj_of(tls_stream_t *s)
{
    tls_obj_t *o = (tls_obj_t *) s;
    return s != NULL && o->magic == TLS_OBJ_MAGIC ? o : NULL;
}

static const tls_obj_t *cobj_of(const tls_stream_t *s)
{
    const tls_obj_t *o = (const tls_obj_t *) s;
    return s != NULL && o->magic == TLS_OBJ_MAGIC ? o : NULL;
}

/* ---- certificate time: the injected wall clock, installed as mbedTLS's time function ---- */

#define WALL_MAX_SECONDS UINT64_C(253402300799) /* 9999-12-31T23:59:59Z; later is treated as unknown (0) */

static mbedtls_time_t wall_now(mbedtls_time_t *out)
{
    const tls_obj_t *o = tls_active();
    uint64_t secs = 0;
    mbedtls_time_t v;
    if (o != NULL) {
        secs = o->cfg.wall.unix_seconds(o->cfg.wall.ctx);
    }
    if (secs > WALL_MAX_SECONDS) {
        secs = 0;
    }
    v = (mbedtls_time_t) secs;
    if (out != NULL) {
        *out = v;
    }
    return v;
}

/* ---- error mapping ---- */

static ports_err_t map_error(const tls_obj_t *o, int ret)
{
    if (o->cause != PORTS_OK) {
        return o->cause;
    }
    if (o->heap.failed || ret == 0 || ret == MBEDTLS_ERR_SSL_CONN_EOF) {
        return PORTS_E_NET;
    }
    return PORTS_E_TLS;
}

/* ---- connect ---- */

static void teardown(tls_obj_t *o)
{
    if (o->tconn) {
        o->cfg.transport.ops->close(o->cfg.transport.ctx);
        o->tconn = false;
    }
    mbedtls_ssl_free(&o->ssl);
    mbedtls_ssl_config_free(&o->conf);
    mbedtls_x509_crt_free(&o->ca);
    mbedtls_psa_crypto_free();
    o->open = false;
    tls_release(o);
}

static ports_err_t load_ca(tls_obj_t *o)
{
    int ret = mbedtls_x509_crt_parse(&o->ca, (const unsigned char *) o->cfg.ca_pem.ptr, o->cfg.ca_pem.len);
    if (o->heap.failed) {
        return PORTS_E_NET;
    }
    /* a block that does not parse is skipped; with no usable certificate at all nothing is trusted */
    return ret != 0 && o->ca.version == 0 ? PORTS_E_TLS : PORTS_OK;
}

static ports_err_t configure(tls_obj_t *o, core_str_t host)
{
    char name[HOST_MAX + 1u];
    int defaults;
    memcpy(name, host.ptr, host.len);
    name[host.len] = '\0';
    defaults = mbedtls_ssl_config_defaults(&o->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_authmode(&o->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&o->conf, &o->ca, NULL);
    mbedtls_ssl_conf_rng(&o->conf, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
    if (defaults != 0 || mbedtls_ssl_setup(&o->ssl, &o->conf) != 0 || mbedtls_ssl_set_hostname(&o->ssl, name) != 0) {
        return PORTS_E_NET;
    }
    mbedtls_ssl_set_bio(&o->ssl, o, tls_bio_send, tls_bio_recv, NULL);
    return PORTS_OK;
}

static ports_err_t prepare(tls_obj_t *o, core_str_t host)
{
    ports_err_t e;
    mbedtls_ssl_init(&o->ssl);
    mbedtls_ssl_config_init(&o->conf);
    mbedtls_x509_crt_init(&o->ca);
    if (mbedtls_platform_set_time(wall_now) != 0 || psa_crypto_init() != PSA_SUCCESS) {
        return PORTS_E_NET;
    }
    e = load_ca(o);
    return e != PORTS_OK ? e : configure(o, host);
}

static ports_err_t handshake(tls_obj_t *o)
{
    int ret = mbedtls_ssl_handshake(&o->ssl);
    return ret == 0 ? PORTS_OK : map_error(o, ret);
}

static ports_err_t establish(tls_obj_t *o, core_str_t host, uint16_t port)
{
    ports_err_t e = prepare(o, host);
    if (e == PORTS_OK) {
        e = tls_transport_connect(o, host, port);
        o->tconn = e == PORTS_OK;
    }
    return e == PORTS_OK ? handshake(o) : e;
}

static bool host_ok(core_str_t host, uint16_t port)
{
    return host.ptr != NULL && host.len >= 1u && host.len <= HOST_MAX && port != 0u &&
           memchr(host.ptr, '\0', host.len) == NULL;
}

static ports_err_t port_connect(void *ctx, core_str_t host, uint16_t port, uint64_t deadline_ms)
{
    tls_obj_t *o = obj_of(ctx);
    ports_err_t e;
    if (o == NULL || !host_ok(host, port)) {
        return PORTS_E_ARG;
    }
    if (o->open || !tls_claim(o)) {
        return PORTS_E_NET;
    }
    o->deadline = deadline_ms;
    o->sticky = PORTS_OK;
    o->cause = PORTS_OK;
    o->eof = false;
    o->heap.failed = false;
    e = tls_deadline_passed(o) ? PORTS_E_TIMEOUT : establish(o, host, port);
    if (e != PORTS_OK) {
        teardown(o);
        return e;
    }
    o->open = true;
    return PORTS_OK;
}

/* ---- read / write / close ---- */

/* The sticky error, or (and it becomes sticky) a deadline that has passed. */
static ports_err_t io_precheck(tls_obj_t *o)
{
    if (o->sticky == PORTS_OK && tls_deadline_passed(o)) {
        o->sticky = PORTS_E_TIMEOUT;
    }
    return o->sticky;
}

static ports_err_t port_write(void *ctx, const void *buf, size_t len)
{
    tls_obj_t *o = obj_of(ctx);
    const unsigned char *p = buf;
    size_t off = 0;
    if (o == NULL) {
        return PORTS_E_ARG;
    }
    if (!o->open) {
        return PORTS_E_CLOSED;
    }
    if (len == 0u) {
        return PORTS_OK;
    }
    if (buf == NULL) {
        return PORTS_E_ARG;
    }
    if (io_precheck(o) != PORTS_OK) {
        return o->sticky;
    }
    o->cause = PORTS_OK;
    while (off < len) {
        int ret = mbedtls_ssl_write(&o->ssl, p + off, len - off);
        if (ret <= 0) {
            o->sticky = map_error(o, ret);
            return o->sticky;
        }
        off += (size_t) ret;
    }
    return PORTS_OK;
}

static ports_err_t port_read(void *ctx, void *buf, size_t cap, size_t *n)
{
    tls_obj_t *o = obj_of(ctx);
    int ret;
    if (o == NULL) {
        return PORTS_E_ARG;
    }
    if (!o->open) {
        return PORTS_E_CLOSED;
    }
    if (buf == NULL || cap == 0u || n == NULL) {
        return PORTS_E_ARG;
    }
    if (o->eof) {
        *n = 0;
        return PORTS_OK;
    }
    if (io_precheck(o) != PORTS_OK) {
        return o->sticky;
    }
    o->cause = PORTS_OK;
    ret = mbedtls_ssl_read(&o->ssl, buf, cap);
    if (ret > 0) {
        *n = (size_t) ret;
        return PORTS_OK;
    }
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        o->eof = true;
        *n = 0;
        return PORTS_OK;
    }
    o->sticky = map_error(o, ret);
    return o->sticky;
}

static ports_err_t port_close(void *ctx)
{
    tls_obj_t *o = obj_of(ctx);
    if (o == NULL) {
        return PORTS_E_ARG;
    }
    if (!o->open) {
        return PORTS_E_CLOSED;
    }
    if (o->sticky == PORTS_OK) {
        /* best effort: the peer may be gone, and close() still releases everything */
        int rc = mbedtls_ssl_close_notify(&o->ssl);
        (void) rc;
    }
    teardown(o);
    return PORTS_OK;
}

static const ports_stream_ops_t k_ops = {port_connect, port_write, port_read, port_close};

/* ---- the public functions ---- */

static bool config_ok(const tls_stream_config_t *c)
{
    return c->transport.ops != NULL && c->transport.ops->connect != NULL && c->transport.ops->send != NULL &&
           c->transport.ops->recv != NULL && c->transport.ops->wait != NULL && c->transport.ops->close != NULL &&
           c->clock.ops != NULL && c->clock.ops->now_ms != NULL && c->wall.unix_seconds != NULL &&
           c->ca_pem.ptr != NULL && c->ca_pem.len != 0u && c->ca_pem.ptr[c->ca_pem.len - 1u] == '\0' &&
           c->heap != NULL && c->heap_size >= TLS_STREAM_HEAP_MIN;
}

bool tls_stream_init(tls_stream_t *s, const tls_stream_config_t *cfg)
{
    tls_obj_t *o = (tls_obj_t *) s;
    if (s == NULL) {
        return false;
    }
    memset(s, 0, sizeof *s);
    if (cfg == NULL || !config_ok(cfg)) {
        return false;
    }
    o->cfg = *cfg;
    tls_heap_setup(&o->heap, cfg->heap, cfg->heap_size);
    o->magic = TLS_OBJ_MAGIC;
    return true;
}

ports_stream_t tls_stream_port(tls_stream_t *s)
{
    ports_stream_t p;
    p.ops = &k_ops;
    p.ctx = s;
    return p;
}

void tls_stream_deinit(tls_stream_t *s)
{
    if (s == NULL) {
        return;
    }
    if (obj_of(s) != NULL) {
        (void) port_close(s); /* CLOSED when not open: nothing to do */
    }
    memset(s, 0, sizeof *s);
}

const char *tls_stream_version(const tls_stream_t *s)
{
    const tls_obj_t *o = cobj_of(s);
    return o != NULL && o->open ? mbedtls_ssl_get_version(&o->ssl) : NULL;
}

size_t tls_stream_heap_used(const tls_stream_t *s)
{
    const tls_obj_t *o = cobj_of(s);
    return o != NULL ? o->heap.used : 0u;
}

size_t tls_stream_heap_peak(const tls_stream_t *s)
{
    const tls_obj_t *o = cobj_of(s);
    return o != NULL ? o->heap.peak : 0u;
}
