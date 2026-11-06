/* The production socket transport, as far as it can be exercised without a network: construction, the table, and
 * the argument checks that happen before any system call. (Resolution, connect, send, recv and poll need a
 * real network and are covered by the orchestrator's live check.) */
#include <errno.h>
#include <string.h>

#include "ctest/ctest.h"
#include "ports/fakes.h"
#include "tls_stream/tls_stream.h"

static ports_fake_clock_t g_clock;
static tls_stream_sock_t g_sock;

static ports_clock_t clock_at(uint64_t ms)
{
    (void) ports_fake_clock_init(&g_clock, ms);
    return ports_fake_clock_port(&g_clock);
}

static void t_init_accepts_a_clock(ctest_t *t)
{
    CHECK(t, tls_stream_sock_init(&g_sock, clock_at(1000)));
    CHECK_EQ_I64(t, g_sock.fd, -1);
}

static void t_init_rejects_bad_arguments(ctest_t *t)
{
    static const ports_clock_ops_t no_now = {NULL};
    ports_clock_t none = {NULL, NULL};
    ports_clock_t bad = {&no_now, NULL};
    CHECK(t, tls_stream_sock_init(&g_sock, clock_at(1))); /* control */
    CHECK(t, !tls_stream_sock_init(NULL, clock_at(1)));
    CHECK(t, !tls_stream_sock_init(&g_sock, none));
    CHECK(t, !tls_stream_sock_init(&g_sock, bad));
}

static void t_transport_table_is_complete(ctest_t *t)
{
    tls_stream_transport_t tr;
    CHECK(t, tls_stream_sock_init(&g_sock, clock_at(1)));
    tr = tls_stream_sock_transport(&g_sock);
    CHECK(t, tr.ops != NULL);
    CHECK(t, tr.ctx == &g_sock);
    CHECK(t, tr.ops->connect != NULL && tr.ops->send != NULL && tr.ops->recv != NULL);
    CHECK(t, tr.ops->wait != NULL && tr.ops->close != NULL);
}

static void t_connect_argument_errors_come_before_any_network(ctest_t *t)
{
    tls_stream_transport_t tr;
    core_str_t empty = {"x", 0};
    static char big[300];
    core_str_t huge = {big, 256};
    memset(big, 'a', sizeof big);
    CHECK(t, tls_stream_sock_init(&g_sock, clock_at(1000)));
    tr = tls_stream_sock_transport(&g_sock);
    CHECK_EQ_I64(t, tr.ops->connect(tr.ctx, empty, 443, 0), EINVAL);
    CHECK_EQ_I64(t, tr.ops->connect(tr.ctx, core_str_from_cstr("localhost"), 0, 0), EINVAL);
    CHECK_EQ_I64(t, tr.ops->connect(tr.ctx, huge, 443, 0), EINVAL);
    CHECK_EQ_I64(t, g_sock.fd, -1);
}

static void t_connect_with_a_passed_deadline_is_etimedout_before_resolving(ctest_t *t)
{
    tls_stream_transport_t tr;
    CHECK(t, tls_stream_sock_init(&g_sock, clock_at(1000)));
    tr = tls_stream_sock_transport(&g_sock);
    CHECK_EQ_I64(t, tr.ops->connect(tr.ctx, core_str_from_cstr("tls.test.invalid"), 443, 1000), ETIMEDOUT);
    CHECK_EQ_I64(t, tr.ops->connect(tr.ctx, core_str_from_cstr("tls.test.invalid"), 443, 999), ETIMEDOUT);
    CHECK_EQ_I64(t, g_sock.fd, -1);
}

static void t_io_on_an_unconnected_transport_is_enotconn(ctest_t *t)
{
    tls_stream_transport_t tr;
    unsigned char b[4] = {0};
    CHECK(t, tls_stream_sock_init(&g_sock, clock_at(1000)));
    tr = tls_stream_sock_transport(&g_sock);
    CHECK_EQ_I64(t, tr.ops->send(tr.ctx, b, sizeof b), -ENOTCONN);
    CHECK_EQ_I64(t, tr.ops->recv(tr.ctx, b, sizeof b), -ENOTCONN);
    CHECK_EQ_I64(t, tr.ops->wait(tr.ctx, false, 5000), ENOTCONN);
    CHECK_EQ_I64(t, tr.ops->wait(tr.ctx, true, 0), ENOTCONN);
}

static void t_close_on_an_unconnected_transport_does_nothing(ctest_t *t)
{
    tls_stream_transport_t tr;
    CHECK(t, tls_stream_sock_init(&g_sock, clock_at(1000)));
    tr = tls_stream_sock_transport(&g_sock);
    tr.ops->close(tr.ctx);
    tr.ops->close(tr.ctx);
    CHECK_EQ_I64(t, g_sock.fd, -1);
}

CTEST_SUITE("tls_stream_sock",
    { "init_accepts_a_clock", t_init_accepts_a_clock },
    { "init_rejects_bad_arguments", t_init_rejects_bad_arguments },
    { "transport_table_is_complete", t_transport_table_is_complete },
    { "connect_argument_errors_come_before_any_network", t_connect_argument_errors_come_before_any_network },
    { "connect_with_a_passed_deadline_is_etimedout_before_resolving", t_connect_with_a_passed_deadline_is_etimedout_before_resolving },
    { "io_on_an_unconnected_transport_is_enotconn", t_io_on_an_unconnected_transport_is_enotconn },
    { "close_on_an_unconnected_transport_does_nothing", t_close_on_an_unconnected_transport_does_nothing })
