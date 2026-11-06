/* tls_stream/src/internal.h: the private state shared by tls_stream.c, bio.c and heap.c. The public contract
 * is tls_stream/tls_stream.h; the tests only see that. */
#ifndef TLS_STREAM_SRC_INTERNAL_H
#define TLS_STREAM_SRC_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include "tls_stream/tls_stream.h"

#define TLS_OBJ_MAGIC 0x544c5331u

/* The arena handed in by the caller: a chain of blocks, each led by a 16-byte header. */
typedef struct {
    unsigned char *base; /* 16-byte aligned */
    size_t size;         /* a multiple of 16 */
    size_t used;         /* bytes in allocated blocks, headers included */
    size_t peak;
    bool failed;         /* an allocation was refused since the last connect started */
} tls_heap_t;

/* The object that lives inside tls_stream_t. A zeroed object (magic 0) is "unusable". */
typedef struct {
    uint32_t magic;
    bool open;        /* connected: the handshake completed and teardown() has not run */
    bool tconn;       /* the transport is connected and must be closed once */
    bool eof;         /* the peer sent close_notify */
    ports_err_t sticky; /* the error every later read and write returns */
    ports_err_t cause;  /* why a BIO callback failed (PORTS_OK = it did not) */
    uint64_t deadline;
    tls_stream_config_t cfg;
    tls_heap_t heap;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca;
} tls_obj_t;

/* heap.c: the allocator over the arena, and the process-wide "connected object" slot (the hooks of mbedTLS
 * are global, so there is at most one connected object). */
void tls_heap_setup(tls_heap_t *h, void *mem, size_t size);
bool tls_claim(tls_obj_t *o);   /* false if another object holds the slot or the hook cannot be installed */
void tls_release(tls_obj_t *o);
const tls_obj_t *tls_active(void);

/* bio.c */
bool tls_deadline_passed(const tls_obj_t *o);
int tls_bio_send(void *ctx, const unsigned char *buf, size_t len);
int tls_bio_recv(void *ctx, unsigned char *buf, size_t len);
ports_err_t tls_transport_connect(tls_obj_t *o, core_str_t host, uint16_t port);

#endif
