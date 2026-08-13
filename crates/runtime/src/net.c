#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Blessed Net.httpGet — live HTTP/1.0 GET or TestRuntime stub map.
 * Live hostnames resolve via UDP A (park on poll); IPv4 literals skip DNS.
 * Failures use SzError code 6. */

typedef struct {
  int is_err;
  int retry; /* 1 = accept EAGAIN; caller should poll again */
  union {
    SzError *err;
    void *ok;
  } as;
} NetResult;

static SzIo *unwrap_net(void *value, void *env) {
  (void)env;
  NetResult *r = (NetResult *)value;
  if (!r)
    return sz_io_fail_cstr("Net: null result");
  if (r->is_err)
    return sz_io_fail(r->as.err);
  return sz_io_pure(r->as.ok);
}

static int set_nonblock(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl < 0)
    return -1;
  return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int parse_http_url(const char *url, char *host, size_t host_sz, char *path,
                          size_t path_sz, int *port) {
  const char *p;
  const char *slash;
  size_t hlen;
  if (!url || strncmp(url, "http://", 7) != 0)
    return 0;
  p = url + 7;
  slash = strchr(p, '/');
  if (slash) {
    hlen = (size_t)(slash - p);
    if (hlen + 1 > host_sz || (size_t)strlen(slash) + 1 > path_sz)
      return 0;
    memcpy(host, p, hlen);
    host[hlen] = '\0';
    memcpy(path, slash, strlen(slash) + 1);
  } else {
    if (strlen(p) + 1 > host_sz)
      return 0;
    memcpy(host, p, strlen(p) + 1);
    memcpy(path, "/", 2);
  }
  *port = 80;
  {
    char *colon = strchr(host, ':');
    if (colon) {
      *colon = '\0';
      *port = atoi(colon + 1);
      if (*port <= 0)
        *port = 80;
    }
  }
  return 1;
}

typedef struct GetSt {
  SzString *url;
  int fd;
  int dns_fd;
  uint16_t dns_id;
  int http_port;
  struct sockaddr_in peer;
  char host[256];
  char path[1024];
  char *req;
  size_t req_len;
  size_t req_off;
  char *acc;
  size_t total;
} GetSt;

static uint16_t g_dns_qid;
static int g_test_ns_on;
static struct sockaddr_in g_test_ns;

void sz_net_test_set_nameserver(const char *ipv4, int port) {
  memset(&g_test_ns, 0, sizeof g_test_ns);
  g_test_ns_on = 0;
  if (!ipv4)
    return;
  g_test_ns.sin_family = AF_INET;
  g_test_ns.sin_port = htons((uint16_t)(port > 0 ? port : 53));
  if (inet_pton(AF_INET, ipv4, &g_test_ns.sin_addr) != 1)
    return;
  g_test_ns_on = 1;
}

static int nameserver_addr(struct sockaddr_in *out) {
  FILE *f;
  char line[256];
  if (g_test_ns_on) {
    *out = g_test_ns;
    return 1;
  }
  f = fopen("/etc/resolv.conf", "r");
  if (!f)
    return 0;
  while (fgets(line, sizeof line, f)) {
    char ip[64];
    size_t i = 0;
    const char *p;
    if (strncmp(line, "nameserver", 10) != 0)
      continue;
    p = line + 10;
    while (*p == ' ' || *p == '\t')
      p++;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i + 1 < sizeof ip)
      ip[i++] = *p++;
    ip[i] = '\0';
    memset(out, 0, sizeof *out);
    out->sin_family = AF_INET;
    out->sin_port = htons(53);
    if (inet_pton(AF_INET, ip, &out->sin_addr) == 1) {
      fclose(f);
      return 1;
    }
  }
  fclose(f);
  return 0;
}

static uint16_t rd16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void wr16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static size_t dns_put_name(uint8_t *dst, size_t cap, const char *host) {
  size_t o = 0;
  const char *p = host;
  while (*p) {
    const char *dot = strchr(p, '.');
    size_t lab = dot ? (size_t)(dot - p) : strlen(p);
    if (lab == 0 || lab > 63 || o + 1 + lab + 1 > cap)
      return 0;
    dst[o++] = (uint8_t)lab;
    memcpy(dst + o, p, lab);
    o += lab;
    p = dot ? dot + 1 : p + lab;
  }
  if (o + 1 > cap)
    return 0;
  dst[o++] = 0;
  return o;
}

static size_t dns_build_query(uint8_t *buf, size_t cap, uint16_t id,
                              const char *host) {
  size_t n;
  if (cap < 18)
    return 0;
  memset(buf, 0, 12);
  wr16(buf, id);
  wr16(buf + 2, 0x0100); /* RD */
  wr16(buf + 4, 1);
  n = dns_put_name(buf + 12, cap - 16, host);
  if (n == 0)
    return 0;
  wr16(buf + 12 + n, 1);     /* A */
  wr16(buf + 12 + n + 2, 1); /* IN */
  return 12 + n + 4;
}

static int dns_skip_name(const uint8_t *buf, size_t n, size_t *off) {
  size_t o = *off;
  size_t jumps = 0;
  int jumped = 0;
  size_t ret = 0;
  while (o < n) {
    uint8_t len = buf[o];
    if ((len & 0xC0) == 0xC0) {
      if (o + 1 >= n)
        return 0;
      if (!jumped) {
        ret = o + 2;
        jumped = 1;
      }
      o = (size_t)(((len & 0x3F) << 8) | buf[o + 1]);
      if (++jumps > 10)
        return 0;
      continue;
    }
    if (len == 0) {
      o++;
      *off = jumped ? ret : o;
      return 1;
    }
    if ((len & 0xC0) != 0)
      return 0;
    if (o + 1 + (size_t)len > n)
      return 0;
    o += 1 + (size_t)len;
  }
  return 0;
}

static int dns_parse_a(const uint8_t *buf, size_t n, uint16_t id,
                       struct in_addr *out) {
  uint16_t flags, qd, an, i;
  size_t o = 12;
  if (n < 12 || rd16(buf) != id)
    return 0;
  flags = rd16(buf + 2);
  if ((flags & 0x8000) == 0 || (flags & 0x000F) != 0)
    return 0;
  qd = rd16(buf + 4);
  an = rd16(buf + 6);
  for (i = 0; i < qd; i++) {
    if (!dns_skip_name(buf, n, &o) || o + 4 > n)
      return 0;
    o += 4;
  }
  for (i = 0; i < an; i++) {
    uint16_t typ, cls, rdlen;
    if (!dns_skip_name(buf, n, &o) || o + 10 > n)
      return 0;
    typ = rd16(buf + o);
    cls = rd16(buf + o + 2);
    rdlen = rd16(buf + o + 8);
    o += 10;
    if (o + rdlen > n)
      return 0;
    if (typ == 1 && cls == 1 && rdlen == 4) {
      memcpy(&out->s_addr, buf + o, 4);
      return 1;
    }
    o += rdlen;
  }
  return 0;
}

static void get_free(GetSt *st) {
  if (!st)
    return;
  if (st->fd >= 0) {
    close(st->fd);
    st->fd = -1;
  }
  if (st->dns_fd >= 0) {
    close(st->dns_fd);
    st->dns_fd = -1;
  }
  sz_free(st->req);
  sz_free(st->acc);
  sz_free(st);
}

static void *get_start(void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  const char *url = sz_string_cstr(st->url);
  int port = 80;
  struct in_addr addr;
  struct sockaddr_in ns;
  uint8_t q[512];
  size_t qn;
  ssize_t nsent;

  if (!parse_http_url(url, st->host, sizeof st->host, st->path, sizeof st->path,
                      &port)) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: only http:// URLs supported");
    return r;
  }
  st->http_port = port;
  memset(&st->peer, 0, sizeof st->peer);
  st->peer.sin_family = AF_INET;
  st->peer.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, st->host, &addr) == 1) {
    st->peer.sin_addr = addr;
    r->is_err = 0;
    return r;
  }
  if (!nameserver_addr(&ns)) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
    return r;
  }
  g_dns_qid++;
  if (g_dns_qid == 0)
    g_dns_qid = 1;
  st->dns_id = g_dns_qid;
  qn = dns_build_query(q, sizeof q, st->dns_id, st->host);
  if (qn == 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
    return r;
  }
  st->dns_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (st->dns_fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
    return r;
  }
  nsent = sendto(st->dns_fd, q, qn, 0, (struct sockaddr *)&ns, sizeof ns);
  if (nsent != (ssize_t)qn || set_nonblock(st->dns_fd) != 0) {
    close(st->dns_fd);
    st->dns_fd = -1;
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
    return r;
  }
  r->is_err = 0;
  return r;
}

static void *get_dns_recv(void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  uint8_t buf[512];
  ssize_t n;

  n = recvfrom(st->dns_fd, buf, sizeof buf, 0, NULL, NULL);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    r->retry = 1;
    return r;
  }
  if (n < 12) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
    return r;
  }
  if (rd16(buf) != st->dns_id) {
    r->retry = 1;
    return r;
  }
  if (!dns_parse_a(buf, (size_t)n, st->dns_id, &st->peer.sin_addr)) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
    return r;
  }
  close(st->dns_fd);
  st->dns_fd = -1;
  r->is_err = 0;
  return r;
}

static void *get_tcp_connect(void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  int fd;
  char req[2048];
  size_t nreq;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0 || set_nonblock(fd) != 0) {
    if (fd >= 0)
      close(fd);
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: socket failed");
    return r;
  }
  if (connect(fd, (struct sockaddr *)&st->peer, sizeof st->peer) != 0 &&
      errno != EINPROGRESS && errno != EAGAIN) {
    close(fd);
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: connect failed");
    return r;
  }
  st->fd = fd;
  nreq = (size_t)snprintf(req, sizeof req,
                          "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
                          st->path, st->host);
  if (nreq >= sizeof req)
    nreq = sizeof req - 1;
  st->req = (char *)sz_alloc(nreq + 1);
  memcpy(st->req, req, nreq + 1);
  st->req_len = nreq;
  st->req_off = 0;
  st->acc = (char *)sz_alloc(1);
  st->acc[0] = '\0';
  st->total = 0;
  r->is_err = 0;
  return r;
}

static void *get_check_write(void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  int so = 0;
  socklen_t sl = sizeof so;
  ssize_t n;

  if (getsockopt(st->fd, SOL_SOCKET, SO_ERROR, &so, &sl) != 0 || so != 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: connect failed");
    return r;
  }
  n = write(st->fd, st->req + st->req_off, st->req_len - st->req_off);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    r->retry = 1;
    return r;
  }
  if (n <= 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: write failed");
    return r;
  }
  st->req_off += (size_t)n;
  if (st->req_off < st->req_len)
    r->retry = 1;
  return r;
}

static void *get_read(void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  char buf[4096];
  ssize_t n;
  char *body;

  n = read(st->fd, buf, sizeof buf);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    r->retry = 1;
    return r;
  }
  if (n < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: read failed");
    return r;
  }
  if (n > 0) {
    char *nacc = (char *)sz_alloc(st->total + (size_t)n + 1);
    if (st->total)
      memcpy(nacc, st->acc, st->total);
    memcpy(nacc + st->total, buf, (size_t)n);
    st->total += (size_t)n;
    nacc[st->total] = '\0';
    sz_free(st->acc);
    st->acc = nacc;
    r->retry = 1;
    return r;
  }
  body = strstr(st->acc ? st->acc : "", "\r\n\r\n");
  if (!body) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: malformed response");
    return r;
  }
  r->is_err = 0;
  r->as.ok = sz_string_from_cstr(body + 4);
  return r;
}

static SzIo *get_poll_write(void *value, void *env);
static SzIo *get_poll_read(void *value, void *env);

static SzIo *get_unwrap_write(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_free(r);
    return get_poll_write(NULL, st);
  }
  return unwrap_net(value, NULL);
}

static SzIo *get_unwrap_read(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_free(r);
    return get_poll_read(NULL, st);
  }
  return unwrap_net(value, NULL);
}

static SzIo *get_after_write_poll(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_delay(get_check_write, st), get_unwrap_write, st);
}

static SzIo *get_poll_write(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_poll_writable(st->fd), get_after_write_poll, st);
}

static SzIo *get_after_read_poll(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_delay(get_read, st), get_unwrap_read, st);
}

static SzIo *get_poll_read(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_poll_readable(st->fd), get_after_read_poll, st);
}

static SzIo *get_finish(void *body, void *env) {
  get_free((GetSt *)env);
  return sz_io_pure(body);
}

static SzIo *get_on_err(SzError *err, void *env) {
  get_free((GetSt *)env);
  return sz_io_fail(err);
}

static SzIo *get_poll_dns(void *value, void *env);

static SzIo *get_unwrap_dns(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_free(r);
    return get_poll_dns(NULL, st);
  }
  return unwrap_net(value, NULL);
}

static SzIo *get_after_dns_poll(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_delay(get_dns_recv, st), get_unwrap_dns, st);
}

static SzIo *get_poll_dns(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_poll_readable(st->dns_fd), get_after_dns_poll, st);
}

static SzIo *get_after_resolved(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_delay(get_tcp_connect, st), unwrap_net, NULL);
}

static SzIo *get_after_start(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)value;
  if (!r || r->is_err)
    return unwrap_net(value, NULL);
  sz_free(r);
  if (st->dns_fd >= 0)
    return sz_io_flatmap(get_poll_dns(NULL, st), get_after_resolved, st);
  return get_after_resolved(NULL, st);
}

static SzIo *get_after_connect(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  SzIo *io;
  (void)value;
  io = get_poll_write(NULL, st);
  io = sz_io_flatmap(io, get_poll_read, st);
  return sz_io_flatmap(io, get_finish, st);
}

static void *get_dispatch(void *env) {
  (void)env;
  return (void *)(intptr_t)(sz_testrt_net_is_fake() ? 1 : 0);
}

static SzIo *get_after_dispatch(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  SzIo *io;
  if ((intptr_t)value) {
    SzString *url = st->url;
    get_free(st);
    return sz_testrt_net_http_get(url);
  }
  io = sz_io_flatmap(sz_io_delay(get_start, st), get_after_start, st);
  io = sz_io_flatmap(io, get_after_connect, st);
  return sz_io_handle_error_with(io, get_on_err, st);
}

SzIo *sz_net_http_get(SzString *url) {
  GetSt *st;
  if (!url)
    sz_panic("sz_net_http_get(null)");
  st = (GetSt *)sz_alloc_zero(sizeof(GetSt));
  st->url = url;
  st->fd = -1;
  st->dns_fd = -1;
  return sz_io_flatmap(sz_io_delay(get_dispatch, st), get_after_dispatch, st);
}

/* HTTP/1.0 GET server. Listen and connection fds are nonblocking; the fiber
 * parks on poll so other IO can run. TestRuntime injects paths and skips
 * sockets. Error code 6. serveOnce is one request; serve keeps the listen
 * socket (n<=0 forever live, or until the TestRuntime queue is empty). */

typedef struct ServeSt {
  int64_t port;
  int64_t left; /* >0 remaining; <=0 forever (TESTRT drains the queue) */
  int listen_fd;
  int conn_fd;
  SzCont handler;
  void *henv;
  void *body;
  char rbuf[4096];
  size_t rlen;
  size_t woff;
} ServeSt;

static void serve_close_conn(ServeSt *st) {
  if (!st)
    return;
  if (st->conn_fd >= 0) {
    close(st->conn_fd);
    st->conn_fd = -1;
  }
}

static void serve_close_fds(ServeSt *st) {
  if (!st)
    return;
  serve_close_conn(st);
  if (st->listen_fd >= 0) {
    close(st->listen_fd);
    st->listen_fd = -1;
  }
}

static void serve_free(ServeSt *st) {
  if (!st)
    return;
  serve_close_fds(st);
  sz_free(st);
}

static int parse_get_path(const char *req, char *path, size_t path_sz) {
  const char *p;
  const char *end;
  size_t n;
  if (!req || strncmp(req, "GET ", 4) != 0)
    return 0;
  p = req + 4;
  end = strchr(p, ' ');
  if (!end)
    end = strstr(p, "\r\n");
  if (!end)
    return 0;
  n = (size_t)(end - p);
  if (n == 0 || n + 1 > path_sz)
    return 0;
  memcpy(path, p, n);
  path[n] = '\0';
  return 1;
}

static void *serve_ensure_listen(void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  struct sockaddr_in addr;
  int fd;
  int one = 1;
  int port;

  if (sz_testrt_net_is_fake()) {
    r->is_err = 0;
    return r;
  }
  port = (int)st->port;
  if (port <= 0 || port > 65535) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: port must be 1..65535");
    return r;
  }
  if (st->listen_fd >= 0) {
    r->is_err = 0;
    return r;
  }
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: socket failed");
    return r;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
      listen(fd, 16) != 0 || set_nonblock(fd) != 0) {
    close(fd);
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: bind/listen failed");
    return r;
  }
  st->listen_fd = fd;
  r->is_err = 0;
  return r;
}

static void *serve_accept(void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  int fd;
  int conn = -1;

  st->conn_fd = -1;
  st->rlen = 0;
  st->woff = 0;

  /* Chosen at step time so SCUZZ_TESTRT=1 install in runtime_main is visible. */
  if (sz_testrt_net_is_fake()) {
    char *path_s = sz_testrt_net_pop_request();
    r->is_err = 0;
    r->as.ok = sz_string_from_cstr(path_s ? path_s : "/");
    sz_free(path_s);
    return r;
  }
  fd = st->listen_fd;
  if (fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: not listening");
    return r;
  }
  conn = accept(fd, NULL, NULL);
  if (conn < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      r->retry = 1;
      return r;
    }
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: accept failed");
    return r;
  }
  if (set_nonblock(conn) != 0) {
    close(conn);
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: accept failed");
    return r;
  }
  st->conn_fd = conn;
  r->is_err = 0;
  return r;
}

static void *serve_read_req(void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  ssize_t n;
  char path[1024];

  n = read(st->conn_fd, st->rbuf + st->rlen, sizeof st->rbuf - 1 - st->rlen);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    r->retry = 1;
    return r;
  }
  if (n <= 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: expected HTTP GET");
    return r;
  }
  st->rlen += (size_t)n;
  st->rbuf[st->rlen] = '\0';
  if (!strstr(st->rbuf, "\r\n\r\n")) {
    if (st->rlen + 1 >= sizeof st->rbuf) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.serve: expected HTTP GET");
      return r;
    }
    r->retry = 1;
    return r;
  }
  if (!parse_get_path(st->rbuf, path, sizeof path)) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: expected HTTP GET");
    return r;
  }
  r->is_err = 0;
  r->as.ok = sz_string_from_cstr(path);
  return r;
}

static void *serve_write_close(void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  SzString *body = (SzString *)st->body;
  const char *data = body ? sz_string_cstr(body) : "";
  size_t len = body ? (size_t)sz_string_len(body) : 0;
  char hdr[160];
  int hn;
  size_t total;
  ssize_t n;
  const char *src;
  size_t src_off;
  size_t src_len;

  if (sz_testrt_net_is_fake()) {
    sz_testrt_net_set_last_serve_body(data);
    r->is_err = 0;
    r->as.ok = NULL;
    return r;
  }

  hn = snprintf(hdr, sizeof hdr,
                "HTTP/1.0 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                len);
  if (hn < 0 || st->conn_fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: write failed");
    return r;
  }
  total = (size_t)hn + len;
  if (st->woff < (size_t)hn) {
    src = hdr;
    src_off = st->woff;
    src_len = (size_t)hn;
  } else {
    src = data;
    src_off = st->woff - (size_t)hn;
    src_len = len;
  }
  n = write(st->conn_fd, src + src_off, src_len - src_off);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    r->retry = 1;
    return r;
  }
  if (n < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: write failed");
    return r;
  }
  st->woff += (size_t)n;
  if (st->woff < total) {
    r->retry = 1;
    return r;
  }
  serve_close_conn(st);
  r->is_err = 0;
  r->as.ok = NULL;
  return r;
}

static SzIo *serve_round(ServeSt *st);

static SzIo *serve_poll_then_accept(void *value, void *env);
static SzIo *serve_poll_conn_read(void *value, void *env);
static SzIo *serve_poll_conn_write(void *value, void *env);

static SzIo *serve_unwrap_accept(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_free(r);
    return serve_poll_then_accept(NULL, st);
  }
  return unwrap_net(value, NULL);
}

static SzIo *serve_unwrap_read(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_free(r);
    return serve_poll_conn_read(NULL, st);
  }
  return unwrap_net(value, NULL);
}

static SzIo *serve_unwrap_write(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_free(r);
    return serve_poll_conn_write(NULL, st);
  }
  return unwrap_net(value, NULL);
}

static SzIo *serve_after_accept_poll(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_delay(serve_accept, st), serve_unwrap_accept, st);
}

static SzIo *serve_poll_then_accept(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_poll_readable(st->listen_fd), serve_after_accept_poll,
                       st);
}

static SzIo *serve_after_conn_read_poll(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_delay(serve_read_req, st), serve_unwrap_read, st);
}

static SzIo *serve_poll_conn_read(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_poll_readable(st->conn_fd), serve_after_conn_read_poll,
                       st);
}

static SzIo *serve_after_conn_write_poll(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_delay(serve_write_close, st), serve_unwrap_write, st);
}

static SzIo *serve_poll_conn_write(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_poll_writable(st->conn_fd), serve_after_conn_write_poll,
                       st);
}

static SzIo *serve_after_listen(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzIo *io;
  (void)value;
  if (sz_testrt_net_is_fake())
    return sz_io_flatmap(sz_io_delay(serve_accept, st), unwrap_net, NULL);
  io = serve_poll_then_accept(NULL, st);
  return sz_io_flatmap(io, serve_poll_conn_read, st);
}

static SzIo *serve_after_write(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  if (st->left > 0)
    st->left--;
  if (st->left == 0) {
    serve_free(st);
    return sz_io_pure(NULL);
  }
  if (st->left < 0 && sz_testrt_net_is_fake() &&
      sz_testrt_net_serve_pending() <= 0) {
    serve_free(st);
    return sz_io_pure(NULL);
  }
  return serve_round(st);
}

static SzIo *serve_after_body(void *body, void *env) {
  ServeSt *st = (ServeSt *)env;
  st->body = body;
  st->woff = 0;
  if (sz_testrt_net_is_fake())
    return sz_io_flatmap(sz_io_delay(serve_write_close, st), unwrap_net, NULL);
  return serve_poll_conn_write(NULL, st);
}

static SzIo *serve_after_path(void *path, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzIo *io = st->handler(path, st->henv);
  return sz_io_flatmap(io, serve_after_body, st);
}

static SzIo *serve_round(ServeSt *st) {
  SzIo *prog;
  if (st->left < 0 && sz_testrt_net_is_fake() &&
      sz_testrt_net_serve_pending() <= 0) {
    serve_free(st);
    return sz_io_pure(NULL);
  }
  prog = sz_io_flatmap(sz_io_delay(serve_ensure_listen, st), unwrap_net, NULL);
  prog = sz_io_flatmap(prog, serve_after_listen, st);
  prog = sz_io_flatmap(prog, serve_after_path, st);
  return sz_io_flatmap(prog, serve_after_write, st);
}

static SzIo *serve_on_err(SzError *err, void *env) {
  serve_free((ServeSt *)env);
  return sz_io_fail(err);
}

static SzIo *net_serve_n(int64_t port, int64_t n, SzCont handler, void *env) {
  ServeSt *st;
  if (!handler)
    sz_panic("sz_net_serve(null handler)");
  st = (ServeSt *)sz_alloc_zero(sizeof(ServeSt));
  st->port = port;
  st->left = n > 0 ? n : -1;
  st->listen_fd = -1;
  st->conn_fd = -1;
  st->handler = handler;
  st->henv = env;
  return sz_io_handle_error_with(serve_round(st), serve_on_err, st);
}

SzIo *sz_net_serve(int64_t port, SzCont handler, void *env) {
  return net_serve_n(port, 0, handler, env);
}

SzIo *sz_net_serve_once(int64_t port, SzCont handler, void *env) {
  return net_serve_n(port, 1, handler, env);
}
