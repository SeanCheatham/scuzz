#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Blessed Net.httpGet — live HTTP/1.0 GET or TestRuntime stub map.
 * Error code 6 (see docs/vision.md IO errors). */

typedef struct {
  int is_err;
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

static void *net_http_get_live(void *env) {
  SzString *url_s = (SzString *)env;
  NetResult *r = (NetResult *)sz_alloc(sizeof(NetResult));
  const char *url = sz_string_cstr(url_s);
  char host[256];
  char path[1024];
  int port = 80;
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  int fd = -1;
  char port_str[16];
  char req[2048];
  char buf[4096];
  size_t total = 0;
  char *acc = NULL;
  ssize_t n;
  char *body;

  if (!parse_http_url(url, host, sizeof host, path, sizeof path, &port)) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: only http:// URLs supported");
    return r;
  }

  memset(&hints, 0, sizeof hints);
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  snprintf(port_str, sizeof port_str, "%d", port);
  if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
    return r;
  }
  fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    if (fd >= 0)
      close(fd);
    freeaddrinfo(res);
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: connect failed");
    return r;
  }
  freeaddrinfo(res);

  snprintf(req, sizeof req,
           "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path,
           host);
  if (write(fd, req, strlen(req)) < 0) {
    close(fd);
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: write failed");
    return r;
  }

  acc = (char *)sz_alloc(1);
  acc[0] = '\0';
  while ((n = read(fd, buf, sizeof buf)) > 0) {
    char *nacc = (char *)sz_alloc(total + (size_t)n + 1);
    if (total)
      memcpy(nacc, acc, total);
    memcpy(nacc + total, buf, (size_t)n);
    total += (size_t)n;
    nacc[total] = '\0';
    sz_free(acc);
    acc = nacc;
  }
  close(fd);

  body = strstr(acc, "\r\n\r\n");
  if (!body) {
    sz_free(acc);
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: malformed response");
    return r;
  }
  body += 4;
  r->is_err = 0;
  r->as.ok = sz_string_from_cstr(body);
  sz_free(acc);
  return r;
}

SzIo *sz_net_http_get(SzString *url) {
  if (!url)
    sz_panic("sz_net_http_get(null)");
  if (sz_testrt_net_is_fake())
    return sz_testrt_net_http_get(url);
  return sz_io_flatmap(sz_io_delay(net_http_get_live, url), unwrap_net, NULL);
}

/* One-shot HTTP/1.0 GET server. Live accept/read/write blocks the fiber (same
 * as httpGet). TestRuntime injects a path and skips sockets. Error code 6. */

typedef struct ServeSt {
  int64_t port;
  int listen_fd;
  int conn_fd;
  SzCont handler;
  void *henv;
  void *body;
} ServeSt;

static void serve_close_fds(ServeSt *st) {
  if (!st)
    return;
  if (st->conn_fd >= 0) {
    close(st->conn_fd);
    st->conn_fd = -1;
  }
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

static void *serve_accept_read(void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)sz_alloc(sizeof(NetResult));
  struct sockaddr_in addr;
  int fd = -1;
  int conn = -1;
  int one = 1;
  char buf[4096];
  size_t total = 0;
  ssize_t n;
  char path[1024];
  int port;

  st->listen_fd = -1;
  st->conn_fd = -1;

  /* Chosen at step time so SCUZZ_TESTRT=1 install in runtime_main is visible. */
  if (sz_testrt_net_is_fake()) {
    r->is_err = 0;
    r->as.ok = sz_string_from_cstr(sz_testrt_net_serve_path());
    return r;
  }
  port = (int)st->port;
  if (port <= 0 || port > 65535) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serveOnce: port must be 1..65535");
    return r;
  }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serveOnce: socket failed");
    return r;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 1) != 0) {
    close(fd);
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serveOnce: bind/listen failed");
    return r;
  }
  st->listen_fd = fd;
  conn = accept(fd, NULL, NULL);
  if (conn < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serveOnce: accept failed");
    return r;
  }
  st->conn_fd = conn;

  while (total + 1 < sizeof buf) {
    n = read(conn, buf + total, sizeof buf - 1 - total);
    if (n <= 0)
      break;
    total += (size_t)n;
    buf[total] = '\0';
    if (strstr(buf, "\r\n\r\n"))
      break;
  }
  buf[total] = '\0';
  if (!parse_get_path(buf, path, sizeof path)) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serveOnce: expected HTTP GET");
    return r;
  }
  r->is_err = 0;
  r->as.ok = sz_string_from_cstr(path);
  return r;
}

static void *serve_write_close(void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)sz_alloc(sizeof(NetResult));
  SzString *body = (SzString *)st->body;
  const char *data = body ? sz_string_cstr(body) : "";
  size_t len = body ? (size_t)sz_string_len(body) : 0;
  char hdr[160];
  int hn;

  if (sz_testrt_net_is_fake()) {
    sz_testrt_net_set_last_serve_body(data);
    r->is_err = 0;
    r->as.ok = NULL;
    return r;
  }

  hn = snprintf(hdr, sizeof hdr,
                "HTTP/1.0 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                len);
  if (hn < 0 || st->conn_fd < 0 || write(st->conn_fd, hdr, (size_t)hn) < 0 ||
      (len > 0 && write(st->conn_fd, data, len) < 0)) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serveOnce: write failed");
    return r;
  }
  serve_close_fds(st);
  r->is_err = 0;
  r->as.ok = NULL;
  return r;
}

static SzIo *serve_after_write(void *value, void *env) {
  (void)value;
  serve_free((ServeSt *)env);
  return sz_io_pure(NULL);
}

static SzIo *serve_after_body(void *body, void *env) {
  ServeSt *st = (ServeSt *)env;
  st->body = body;
  return sz_io_flatmap(sz_io_delay(serve_write_close, st), unwrap_net, NULL);
}

static SzIo *serve_after_path(void *path, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzIo *io = st->handler(path, st->henv);
  return sz_io_flatmap(io, serve_after_body, st);
}

static SzIo *serve_on_err(SzError *err, void *env) {
  serve_free((ServeSt *)env);
  return sz_io_fail(err);
}

SzIo *sz_net_serve_once(int64_t port, SzCont handler, void *env) {
  ServeSt *st;
  SzIo *prog;
  if (!handler)
    sz_panic("sz_net_serve_once(null handler)");
  st = (ServeSt *)sz_alloc_zero(sizeof(ServeSt));
  st->port = port;
  st->listen_fd = -1;
  st->conn_fd = -1;
  st->handler = handler;
  st->henv = env;
  prog = sz_io_flatmap(sz_io_delay(serve_accept_read, st), unwrap_net, NULL);
  prog = sz_io_flatmap(prog, serve_after_path, st);
  prog = sz_io_flatmap(prog, serve_after_write, st);
  return sz_io_handle_error_with(prog, serve_on_err, st);
}
