#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
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
