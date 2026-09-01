#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"
#include "rt_util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

/* Blessed TCP and UDP. Listen/bind is localhost. Connect takes IPv4/IPv6
 * literals or localhost. No DNS. Fibers park on poll. TestRuntime uses a
 * mailbox. Opaque SzNetSock owns the fds. */

enum { NS_TCP = 1, NS_LISTEN = 2, NS_UDP = 3 };
enum { NS_IO_MS = 1000 };
enum { NS_READ_MAX = 1024u * 1024u };

typedef struct {
  int is_err;
  int retry;
  union {
    SzError *err;
    void *ok;
  } as;
} SockResult;

static SzIo *unwrap_sock(void *value, void *env) {
  SockResult *r = (SockResult *)value;
  (void)env;
  if (!r)
    return sz_io_fail_cstr("Net: null result");
  if (r->is_err) {
    SzError *err = r->as.err;
    r->as.err = NULL;
    sz_release(r);
    return fail_drop(err);
  }
  {
    void *ok = r->as.ok;
    r->as.ok = NULL;
    sz_release(r);
    return pure_drop(ok);
  }
}

static int set_nonblock(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl < 0)
    return -1;
  return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static SzNetSock *sock_new(int kind) {
  SzNetSock *s = (SzNetSock *)sz_rc_alloc(sizeof(SzNetSock), SZ_RC_NETSOCK);
  s->fd = -1;
  s->fd6 = -1;
  s->kind = kind;
  s->fake_id = 0;
  s->port = 0;
  return s;
}

void sz_net_sock_on_free(SzNetSock *s) {
  if (!s)
    return;
  if (sz_testrt_net_is_fake()) {
    sz_testrt_net_sock_gone(s);
    return;
  }
  if (s->fd >= 0) {
    close(s->fd);
    s->fd = -1;
  }
  if (s->fd6 >= 0) {
    close(s->fd6);
    s->fd6 = -1;
  }
}

static int host_eq_ci(const char *a, const char *b) {
  if (!a || !b)
    return 0;
  while (*a && *b) {
    unsigned char ca = (unsigned char)*a;
    unsigned char cb = (unsigned char)*b;
    if (ca >= 'A' && ca <= 'Z')
      ca = (unsigned char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z')
      cb = (unsigned char)(cb - 'A' + 'a');
    if (ca != cb)
      return 0;
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

static int parse_tcp_host(const char *host, struct sockaddr_storage *ss,
                         socklen_t *len, int port) {
  struct sockaddr_in *a4;
  struct sockaddr_in6 *a6;
  const char *h = host ? host : "";
  if (port < 1 || port > 65535 || !ss || !len)
    return 0;
  memset(ss, 0, sizeof *ss);
  if (host_eq_ci(h, "localhost") || host_eq_ci(h, "127.0.0.1")) {
    a4 = (struct sockaddr_in *)ss;
    a4->sin_family = AF_INET;
    a4->sin_port = htons((uint16_t)port);
    a4->sin_addr.s_addr = inet_addr("127.0.0.1");
    *len = sizeof(*a4);
    return 1;
  }
  if (host_eq_ci(h, "::1")) {
    a6 = (struct sockaddr_in6 *)ss;
    a6->sin6_family = AF_INET6;
#ifdef __APPLE__
    a6->sin6_len = (uint8_t)sizeof(*a6);
#endif
    a6->sin6_port = htons((uint16_t)port);
    if (inet_pton(AF_INET6, "::1", &a6->sin6_addr) != 1)
      return 0;
    *len = sizeof(*a6);
    return 1;
  }
  a4 = (struct sockaddr_in *)ss;
  if (inet_pton(AF_INET, h, &a4->sin_addr) == 1) {
    a4->sin_family = AF_INET;
    a4->sin_port = htons((uint16_t)port);
    *len = sizeof(*a4);
    return 1;
  }
  a6 = (struct sockaddr_in6 *)ss;
  if (inet_pton(AF_INET6, h, &a6->sin6_addr) == 1) {
    a6->sin6_family = AF_INET6;
#ifdef __APPLE__
    a6->sin6_len = (uint8_t)sizeof(*a6);
#endif
    a6->sin6_port = htons((uint16_t)port);
    *len = sizeof(*a6);
    return 1;
  }
  return 0;
}

static int tcp_begin(const struct sockaddr *sa, socklen_t len) {
  int fd;
  if (!sa || len == 0)
    return -1;
  fd = socket(sa->sa_family, SOCK_STREAM, 0);
  if (fd < 0 || set_nonblock(fd) != 0) {
    if (fd >= 0)
      close(fd);
    return -1;
  }
#ifdef SO_NOSIGPIPE
  {
    int nosig = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof nosig);
  }
#endif
  if (connect(fd, sa, len) != 0 && errno != EINPROGRESS && errno != EAGAIN) {
    close(fd);
    return -1;
  }
  return fd;
}

static int listen_v4(int port) {
  struct sockaddr_in addr;
  int fd;
  int one = 1;
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
      listen(fd, 16) != 0 || set_nonblock(fd) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static int listen_v6(int port) {
  struct sockaddr_in6 addr;
  int fd;
  int one = 1;
  fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef IPV6_V6ONLY
  setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
#endif
  memset(&addr, 0, sizeof addr);
  addr.sin6_family = AF_INET6;
#ifdef __APPLE__
  addr.sin6_len = (uint8_t)sizeof(addr);
#endif
  addr.sin6_port = htons((uint16_t)port);
  if (inet_pton(AF_INET6, "::1", &addr.sin6_addr) != 1 ||
      bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
      listen(fd, 16) != 0 || set_nonblock(fd) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static int udp_bind_v4(int port, int *out_port) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd;
  int one = 1;
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    return -1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)(port < 0 ? 0 : port));
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
      set_nonblock(fd) != 0) {
    close(fd);
    return -1;
  }
  if (getsockname(fd, (struct sockaddr *)&addr, &alen) == 0 && out_port)
    *out_port = (int)ntohs(addr.sin_port);
  return fd;
}

typedef struct ConnOp {
  SzNetSock *sock;
  SzString *data;
  int64_t n;
  int64_t deadline_ms;
  char *acc;
  size_t acc_len;
  size_t acc_cap;
  size_t woff;
} ConnOp;

static void conn_op_free(ConnOp *op) {
  if (!op)
    return;
  sz_release(op->sock);
  op->sock = NULL;
  sz_release(op->data);
  op->data = NULL;
  sz_free(op->acc);
  op->acc = NULL;
}

static void *conn_op_cleanup(void *env) {
  conn_op_free((ConnOp *)env);
  return NULL;
}

static SzIo *sock_ensure(SzIo *io, ConnOp *op) {
  SzIo *fin = sz_io_delay(conn_op_cleanup, op);
  SzIo *ens = sz_io_ensure(io, fin);
  sz_release(io);
  sz_release(fin);
  sz_release(op);
  return ens;
}

static SzIo *unwrap_retry(void *value, void *env, SzIo *(*poll)(void *, void *),
                          void *penv) {
  SockResult *r = (SockResult *)value;
  if (r && r->retry) {
    sz_release(r);
    return poll(NULL, penv);
  }
  return unwrap_sock(value, env);
}

static int fd_pollout(int fd) {
  struct pollfd p;
  if (fd < 0)
    return 0;
  memset(&p, 0, sizeof p);
  p.fd = fd;
  p.events = POLLOUT;
  return poll(&p, 1, 0) > 0 && (p.revents & POLLOUT);
}

typedef struct ConnectSt {
  SzString *host;
  int64_t port;
  int fd;
  int64_t deadline_ms;
  struct sockaddr_storage peer;
  socklen_t peer_len;
} ConnectSt;

static void connect_free(ConnectSt *st) {
  if (!st)
    return;
  if (st->fd >= 0) {
    close(st->fd);
    st->fd = -1;
  }
  sz_release(st->host);
  st->host = NULL;
}

static void *connect_cleanup(void *env) {
  connect_free((ConnectSt *)env);
  return NULL;
}

static void *connect_start(void *env) {
  ConnectSt *st = (ConnectSt *)env;
  SockResult *r = (SockResult *)rc_box_zero(sizeof(SockResult));
  int port = (int)st->port;
  if (st->port < 1 || st->port > 65535) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpConnect: invalid port");
    return r;
  }
  if (!parse_tcp_host(sz_string_cstr(st->host), &st->peer, &st->peer_len, port)) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpConnect: host must be an IP literal");
    return r;
  }
  st->fd = tcp_begin((struct sockaddr *)&st->peer, st->peer_len);
  if (st->fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpConnect: connect failed");
    return r;
  }
  st->deadline_ms = sz_clock_monotonic_ms_sync() + NS_IO_MS;
  r->is_err = 0;
  return r;
}

static void *connect_check(void *env) {
  ConnectSt *st = (ConnectSt *)env;
  SockResult *r = (SockResult *)rc_box_zero(sizeof(SockResult));
  int so = 0;
  socklen_t sl = sizeof so;
  if (!fd_pollout(st->fd)) {
    if (sz_clock_monotonic_ms_sync() >= st->deadline_ms) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.tcpConnect: connect timed out");
      return r;
    }
    r->retry = 1;
    return r;
  }
  if (getsockopt(st->fd, SOL_SOCKET, SO_ERROR, &so, &sl) != 0 || so != 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpConnect: connect failed");
    return r;
  }
  {
    SzNetSock *s = sock_new(NS_TCP);
    s->fd = st->fd;
    s->port = st->port;
    st->fd = -1;
    r->as.ok = s;
  }
  return r;
}

static SzIo *connect_poll(void *value, void *env);

static SzIo *connect_unwrap(void *value, void *env) {
  return unwrap_retry(value, env, connect_poll, env);
}

static SzIo *connect_after_poll(void *value, void *env) {
  ConnectSt *st = (ConnectSt *)env;
  (void)value;
  return fm_drop(sz_io_delay(connect_check, st), connect_unwrap, st);
}

static SzIo *connect_poll(void *value, void *env) {
  ConnectSt *st = (ConnectSt *)env;
  SzIo *ready;
  int64_t left;
  (void)value;
  left = st->deadline_ms - sz_clock_monotonic_ms_sync();
  if (left < 1)
    left = 1;
  ready = race_drop(sz_io_poll_writable(st->fd), sz_io_sleep_ms(left));
  return fm_drop(ready, connect_after_poll, st);
}

static SzIo *connect_after_start(void *value, void *env) {
  ConnectSt *st = (ConnectSt *)env;
  SockResult *r = (SockResult *)value;
  if (!r || r->is_err)
    return unwrap_sock(value, NULL);
  sz_release(r);
  return connect_poll(NULL, st);
}

static SzIo *connect_finish(void *ok, void *env) {
  connect_free((ConnectSt *)env);
  return pure_drop(ok);
}

SzIo *sz_net_tcp_connect(SzString *host, int64_t port) {
  ConnectSt *st;
  if (!host)
    sz_panic("sz_net_tcp_connect(null)");
  if (sz_testrt_net_is_fake())
    return sz_testrt_net_tcp_connect(host, port);
  sz_timeline_log_cstr("Net.tcpConnect", sz_string_cstr(host));
  st = (ConnectSt *)sz_rc_alloc(sizeof(ConnectSt), SZ_RC_BOX);
  memset(st, 0, sizeof(ConnectSt));
  sz_retain(host);
  st->host = host;
  st->port = port;
  st->fd = -1;
  {
    SzIo *io = fm_drop(sz_io_delay(connect_start, st), connect_after_start, st);
    io = fm_drop(io, connect_finish, st);
    {
      SzIo *fin = sz_io_delay(connect_cleanup, st);
      SzIo *ens = sz_io_ensure(io, fin);
      sz_release(io);
      sz_release(fin);
      sz_release(st);
      return ens;
    }
  }
}

static void *listen_start(void *env) {
  int64_t *portp = (int64_t *)env;
  SockResult *r = (SockResult *)rc_box_zero(sizeof(SockResult));
  int port;
  SzNetSock *s;
  if (!portp || *portp < 1 || *portp > 65535) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpListen: port must be 1..65535");
    return r;
  }
  port = (int)*portp;
  s = sock_new(NS_LISTEN);
  s->fd = listen_v4(port);
  s->fd6 = listen_v6(port);
  s->port = port;
  if (s->fd < 0 && s->fd6 < 0) {
    sz_release(s);
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpListen: bind/listen failed");
    return r;
  }
  r->as.ok = s;
  return r;
}

SzIo *sz_net_tcp_listen(int64_t port) {
  int64_t *box;
  if (sz_testrt_net_is_fake())
    return sz_testrt_net_tcp_listen(port);
  sz_timeline_log_cstr("Net.tcpListen", "");
  box = (int64_t *)rc_box_zero(sizeof(int64_t));
  *box = port;
  {
    SzIo *io = fm_drop(sz_io_delay(listen_start, box), unwrap_sock, NULL);
    sz_release(box);
    return io;
  }
}

typedef struct AcceptSt {
  SzNetSock *ln;
  int64_t deadline_ms;
} AcceptSt;

static void *accept_cleanup(void *env) {
  AcceptSt *st = (AcceptSt *)env;
  sz_release(st->ln);
  st->ln = NULL;
  return NULL;
}

static int accept_wait_err(int err) {
  return err == EAGAIN || err == EWOULDBLOCK || err == ECONNABORTED ||
         err == EMFILE || err == ENFILE || err == EINTR;
}

static void *accept_try(void *env) {
  AcceptSt *st = (AcceptSt *)env;
  SockResult *r = (SockResult *)rc_box_zero(sizeof(SockResult));
  SzNetSock *ln = st->ln;
  int conn = -1;
  if (!ln || ln->kind != NS_LISTEN) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpAccept: not a listener");
    return r;
  }
  if (ln->fd >= 0)
    conn = accept(ln->fd, NULL, NULL);
  if (conn < 0 && ln->fd6 >= 0 && (ln->fd < 0 || accept_wait_err(errno)))
    conn = accept(ln->fd6, NULL, NULL);
  if (conn < 0) {
    if (accept_wait_err(errno)) {
      r->retry = 1;
      return r;
    }
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpAccept: accept failed");
    return r;
  }
  if (set_nonblock(conn) != 0) {
    close(conn);
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpAccept: accept failed");
    return r;
  }
#ifdef SO_NOSIGPIPE
  {
    int nosig = 1;
    setsockopt(conn, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof nosig);
  }
#endif
  {
    SzNetSock *s = sock_new(NS_TCP);
    s->fd = conn;
    s->port = ln->port;
    r->as.ok = s;
  }
  return r;
}

static SzIo *accept_poll(void *value, void *env);

static SzIo *accept_unwrap(void *value, void *env) {
  return unwrap_retry(value, env, accept_poll, env);
}

static SzIo *accept_after_poll(void *value, void *env) {
  AcceptSt *st = (AcceptSt *)env;
  (void)value;
  return fm_drop(sz_io_delay(accept_try, st), accept_unwrap, st);
}

static SzIo *accept_poll(void *value, void *env) {
  AcceptSt *st = (AcceptSt *)env;
  SzNetSock *ln = st->ln;
  SzIo *ready;
  (void)value;
  if (ln->fd >= 0 && ln->fd6 >= 0)
    ready = race_drop(sz_io_poll_readable(ln->fd), sz_io_poll_readable(ln->fd6));
  else if (ln->fd6 >= 0)
    ready = sz_io_poll_readable(ln->fd6);
  else
    ready = sz_io_poll_readable(ln->fd);
  return fm_drop(ready, accept_after_poll, st);
}

SzIo *sz_net_tcp_accept(void *listener) {
  AcceptSt *st;
  if (!listener)
    sz_panic("sz_net_tcp_accept(null)");
  if (sz_testrt_net_is_fake())
    return sz_testrt_net_tcp_accept(listener);
  sz_timeline_log_cstr("Net.tcpAccept", "");
  st = (AcceptSt *)sz_rc_alloc(sizeof(AcceptSt), SZ_RC_BOX);
  memset(st, 0, sizeof(AcceptSt));
  sz_retain(listener);
  st->ln = (SzNetSock *)listener;
  {
    SzIo *io = accept_poll(NULL, st);
    SzIo *fin = sz_io_delay(accept_cleanup, st);
    SzIo *ens = sz_io_ensure(io, fin);
    sz_release(io);
    sz_release(fin);
    sz_release(st);
    return ens;
  }
}

static void *read_try(void *env) {
  ConnOp *op = (ConnOp *)env;
  SockResult *r = (SockResult *)rc_box_zero(sizeof(SockResult));
  char buf[4096];
  ssize_t n;
  size_t want;
  if (!op->sock || op->sock->fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpRead: closed");
    return r;
  }
  want = op->n > 0 ? (size_t)op->n : 0;
  if (want == 0) {
    r->as.ok = sz_string_from_cstr("");
    return r;
  }
  if (want > NS_READ_MAX)
    want = NS_READ_MAX;
  n = read(op->sock->fd, buf, want < sizeof buf ? want : sizeof buf);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    if (sz_clock_monotonic_ms_sync() >= op->deadline_ms) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.tcpRead: timed out");
      return r;
    }
    r->retry = 1;
    return r;
  }
  if (n < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpRead: read failed");
    return r;
  }
  r->as.ok = sz_string_from_bytes(buf, (size_t)n);
  return r;
}

static SzIo *read_poll(void *value, void *env);

static SzIo *read_unwrap(void *value, void *env) {
  return unwrap_retry(value, env, read_poll, env);
}

static SzIo *read_after_poll(void *value, void *env) {
  ConnOp *op = (ConnOp *)env;
  (void)value;
  return fm_drop(sz_io_delay(read_try, op), read_unwrap, op);
}

static SzIo *read_poll(void *value, void *env) {
  ConnOp *op = (ConnOp *)env;
  SzIo *ready;
  int64_t left;
  (void)value;
  if (op->deadline_ms == 0)
    op->deadline_ms = sz_clock_monotonic_ms_sync() + NS_IO_MS;
  left = op->deadline_ms - sz_clock_monotonic_ms_sync();
  if (left < 1)
    left = 1;
  ready = race_drop(sz_io_poll_readable(op->sock->fd), sz_io_sleep_ms(left));
  return fm_drop(ready, read_after_poll, op);
}

SzIo *sz_net_tcp_read(void *conn, int64_t n) {
  ConnOp *op;
  if (!conn)
    sz_panic("sz_net_tcp_read(null)");
  if (sz_testrt_net_is_fake())
    return sz_testrt_net_tcp_read(conn, n);
  sz_timeline_log_cstr("Net.tcpRead", "");
  op = (ConnOp *)sz_rc_alloc(sizeof(ConnOp), SZ_RC_BOX);
  memset(op, 0, sizeof(ConnOp));
  sz_retain(conn);
  op->sock = (SzNetSock *)conn;
  op->n = n;
  return sock_ensure(read_poll(NULL, op), op);
}

static void *write_try(void *env) {
  ConnOp *op = (ConnOp *)env;
  SockResult *r = (SockResult *)rc_box_zero(sizeof(SockResult));
  const char *data = op->data ? sz_string_cstr(op->data) : "";
  size_t len = op->data ? (size_t)sz_string_len(op->data) : 0;
  ssize_t n;
  if (!op->sock || op->sock->fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpWrite: closed");
    return r;
  }
  if (op->woff >= len)
    return r;
#ifdef MSG_NOSIGNAL
  n = send(op->sock->fd, data + op->woff, len - op->woff, MSG_NOSIGNAL);
#else
  n = write(op->sock->fd, data + op->woff, len - op->woff);
#endif
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    if (sz_clock_monotonic_ms_sync() >= op->deadline_ms) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.tcpWrite: timed out");
      return r;
    }
    r->retry = 1;
    return r;
  }
  if (n <= 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.tcpWrite: write failed");
    return r;
  }
  op->woff += (size_t)n;
  if (op->woff < len)
    r->retry = 1;
  return r;
}

static SzIo *write_poll(void *value, void *env);

static SzIo *write_unwrap(void *value, void *env) {
  return unwrap_retry(value, env, write_poll, env);
}

static SzIo *write_after_poll(void *value, void *env) {
  ConnOp *op = (ConnOp *)env;
  (void)value;
  return fm_drop(sz_io_delay(write_try, op), write_unwrap, op);
}

static SzIo *write_poll(void *value, void *env) {
  ConnOp *op = (ConnOp *)env;
  SzIo *ready;
  int64_t left;
  (void)value;
  if (op->deadline_ms == 0)
    op->deadline_ms = sz_clock_monotonic_ms_sync() + NS_IO_MS;
  left = op->deadline_ms - sz_clock_monotonic_ms_sync();
  if (left < 1)
    left = 1;
  ready = race_drop(sz_io_poll_writable(op->sock->fd), sz_io_sleep_ms(left));
  return fm_drop(ready, write_after_poll, op);
}

SzIo *sz_net_tcp_write(void *conn, SzString *s) {
  ConnOp *op;
  if (!conn || !s)
    sz_panic("sz_net_tcp_write(null)");
  if (sz_testrt_net_is_fake())
    return sz_testrt_net_tcp_write(conn, s);
  sz_timeline_log_cstr("Net.tcpWrite", sz_string_cstr(s));
  op = (ConnOp *)sz_rc_alloc(sizeof(ConnOp), SZ_RC_BOX);
  memset(op, 0, sizeof(ConnOp));
  sz_retain(conn);
  sz_retain(s);
  op->sock = (SzNetSock *)conn;
  op->data = s;
  return sock_ensure(write_poll(NULL, op), op);
}

static void *close_now(void *env) {
  SzNetSock *s = (SzNetSock *)env;
  if (s) {
    if (s->fd >= 0) {
      close(s->fd);
      s->fd = -1;
    }
    if (s->fd6 >= 0) {
      close(s->fd6);
      s->fd6 = -1;
    }
  }
  return NULL;
}

SzIo *sz_net_tcp_close(void *sock) {
  if (!sock)
    sz_panic("sz_net_tcp_close(null)");
  if (sz_testrt_net_is_fake())
    return sz_testrt_net_tcp_close(sock);
  sz_timeline_log_cstr("Net.tcpClose", "");
  return sz_io_delay(close_now, sock);
}

static void *udp_bind_start(void *env) {
  int64_t *portp = (int64_t *)env;
  SockResult *r = (SockResult *)rc_box_zero(sizeof(SockResult));
  int port;
  int bound = 0;
  int fd;
  SzNetSock *s;
  if (!portp || *portp < 0 || *portp > 65535) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.udpBind: port must be 0..65535");
    return r;
  }
  port = (int)*portp;
  fd = udp_bind_v4(port, &bound);
  if (fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.udpBind: bind failed");
    return r;
  }
  s = sock_new(NS_UDP);
  s->fd = fd;
  s->port = bound;
  r->as.ok = s;
  return r;
}

SzIo *sz_net_udp_bind(int64_t port) {
  int64_t *box;
  if (sz_testrt_net_is_fake())
    return sz_testrt_net_udp_bind(port);
  sz_timeline_log_cstr("Net.udpBind", "");
  box = (int64_t *)rc_box_zero(sizeof(int64_t));
  *box = port;
  {
    SzIo *io = fm_drop(sz_io_delay(udp_bind_start, box), unwrap_sock, NULL);
    sz_release(box);
    return io;
  }
}

typedef struct UdpSendSt {
  SzNetSock *sock;
  SzString *host;
  SzString *data;
  int64_t port;
} UdpSendSt;

static void *udp_send_cleanup(void *env) {
  UdpSendSt *st = (UdpSendSt *)env;
  sz_release(st->sock);
  sz_release(st->host);
  sz_release(st->data);
  st->sock = NULL;
  st->host = NULL;
  st->data = NULL;
  return NULL;
}

static void *udp_send_now(void *env) {
  UdpSendSt *st = (UdpSendSt *)env;
  SockResult *r = (SockResult *)rc_box_zero(sizeof(SockResult));
  struct sockaddr_storage ss;
  socklen_t slen = 0;
  const char *data;
  size_t len;
  ssize_t n;
  if (!st->sock || st->sock->kind != NS_UDP || st->sock->fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.udpSend: closed");
    return r;
  }
  if (st->port < 1 || st->port > 65535 ||
      !parse_tcp_host(sz_string_cstr(st->host), &ss, &slen, (int)st->port)) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.udpSend: host must be an IP literal");
    return r;
  }
  data = st->data ? sz_string_cstr(st->data) : "";
  len = st->data ? (size_t)sz_string_len(st->data) : 0;
  n = sendto(st->sock->fd, data, len, 0, (struct sockaddr *)&ss, slen);
  if (n < 0 || (size_t)n != len) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.udpSend: send failed");
    return r;
  }
  return r;
}

SzIo *sz_net_udp_send(void *sock, SzString *host, int64_t port, SzString *data) {
  UdpSendSt *st;
  if (!sock || !host || !data)
    sz_panic("sz_net_udp_send(null)");
  if (sz_testrt_net_is_fake())
    return sz_testrt_net_udp_send(sock, host, port, data);
  sz_timeline_log_cstr("Net.udpSend", sz_string_cstr(host));
  st = (UdpSendSt *)sz_rc_alloc(sizeof(UdpSendSt), SZ_RC_BOX);
  memset(st, 0, sizeof(UdpSendSt));
  sz_retain(sock);
  sz_retain(host);
  sz_retain(data);
  st->sock = (SzNetSock *)sock;
  st->host = host;
  st->port = port;
  st->data = data;
  {
    SzIo *io = fm_drop(sz_io_delay(udp_send_now, st), unwrap_sock, NULL);
    SzIo *fin = sz_io_delay(udp_send_cleanup, st);
    SzIo *ens = sz_io_ensure(io, fin);
    sz_release(io);
    sz_release(fin);
    sz_release(st);
    return ens;
  }
}

typedef struct UdpRecvSt {
  SzNetSock *sock;
  int64_t n;
  int64_t deadline_ms;
} UdpRecvSt;

static void *udp_recv_cleanup(void *env) {
  UdpRecvSt *st = (UdpRecvSt *)env;
  sz_release(st->sock);
  st->sock = NULL;
  return NULL;
}

static SzPair *udp_pack(const char *host, int port, const char *data, size_t n) {
  SzString *hs = sz_string_from_cstr(host ? host : "");
  void *pn = sz_box_i64((int64_t)port);
  SzString *ds = sz_string_from_bytes(data ? data : "", n);
  SzPair *inner = sz_pair_new(pn, ds);
  SzPair *outer = sz_pair_new(hs, inner);
  sz_release(hs);
  sz_release(pn);
  sz_release(ds);
  sz_release(inner);
  return outer;
}

static void *udp_recv_try(void *env) {
  UdpRecvSt *st = (UdpRecvSt *)env;
  SockResult *r = (SockResult *)rc_box_zero(sizeof(SockResult));
  char buf[65536];
  char host[INET6_ADDRSTRLEN];
  struct sockaddr_storage from;
  socklen_t flen = sizeof from;
  ssize_t n;
  size_t want;
  int port = 0;
  if (!st->sock || st->sock->kind != NS_UDP || st->sock->fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.udpRecv: closed");
    return r;
  }
  want = st->n > 0 ? (size_t)st->n : 0;
  if (want == 0)
    want = 1;
  if (want > sizeof buf)
    want = sizeof buf;
  memset(&from, 0, sizeof from);
  n = recvfrom(st->sock->fd, buf, want, 0, (struct sockaddr *)&from, &flen);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    if (sz_clock_monotonic_ms_sync() >= st->deadline_ms) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.udpRecv: timed out");
      return r;
    }
    r->retry = 1;
    return r;
  }
  if (n < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.udpRecv: recv failed");
    return r;
  }
  host[0] = '\0';
  if (from.ss_family == AF_INET) {
    struct sockaddr_in *a = (struct sockaddr_in *)&from;
    inet_ntop(AF_INET, &a->sin_addr, host, sizeof host);
    port = (int)ntohs(a->sin_port);
  } else if (from.ss_family == AF_INET6) {
    struct sockaddr_in6 *a = (struct sockaddr_in6 *)&from;
    inet_ntop(AF_INET6, &a->sin6_addr, host, sizeof host);
    port = (int)ntohs(a->sin6_port);
  }
  r->as.ok = udp_pack(host, port, buf, (size_t)n);
  return r;
}

static SzIo *udp_recv_poll(void *value, void *env);

static SzIo *udp_recv_unwrap(void *value, void *env) {
  return unwrap_retry(value, env, udp_recv_poll, env);
}

static SzIo *udp_recv_after_poll(void *value, void *env) {
  UdpRecvSt *st = (UdpRecvSt *)env;
  (void)value;
  return fm_drop(sz_io_delay(udp_recv_try, st), udp_recv_unwrap, st);
}

static SzIo *udp_recv_poll(void *value, void *env) {
  UdpRecvSt *st = (UdpRecvSt *)env;
  SzIo *ready;
  int64_t left;
  (void)value;
  if (st->deadline_ms == 0)
    st->deadline_ms = sz_clock_monotonic_ms_sync() + NS_IO_MS;
  left = st->deadline_ms - sz_clock_monotonic_ms_sync();
  if (left < 1)
    left = 1;
  ready = race_drop(sz_io_poll_readable(st->sock->fd), sz_io_sleep_ms(left));
  return fm_drop(ready, udp_recv_after_poll, st);
}

SzIo *sz_net_udp_recv(void *sock, int64_t n) {
  UdpRecvSt *st;
  if (!sock)
    sz_panic("sz_net_udp_recv(null)");
  if (sz_testrt_net_is_fake())
    return sz_testrt_net_udp_recv(sock, n);
  sz_timeline_log_cstr("Net.udpRecv", "");
  st = (UdpRecvSt *)sz_rc_alloc(sizeof(UdpRecvSt), SZ_RC_BOX);
  memset(st, 0, sizeof(UdpRecvSt));
  sz_retain(sock);
  st->sock = (SzNetSock *)sock;
  st->n = n;
  {
    SzIo *io = udp_recv_poll(NULL, st);
    SzIo *fin = sz_io_delay(udp_recv_cleanup, st);
    SzIo *ens = sz_io_ensure(io, fin);
    sz_release(io);
    sz_release(fin);
    sz_release(st);
    return ens;
  }
}

SzIo *sz_net_udp_close(void *sock) {
  if (!sock)
    sz_panic("sz_net_udp_close(null)");
  if (sz_testrt_net_is_fake())
    return sz_testrt_net_udp_close(sock);
  sz_timeline_log_cstr("Net.udpClose", "");
  return sz_io_delay(close_now, sock);
}
