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
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

/* Blessed Net.httpGet — live HTTP/1.0 GET or TestRuntime stub map.
 * Live hostnames query A and AAAA together (park on poll). CNAME chains
 * re-query both (cap 5). When both addresses exist, start AAAA first and wait
 * 250ms (RFC 8305) before the A connect so working IPv6 wins. DNS, connect,
 * and the response read each wait at most 1000ms. A partial DNS answer
 * proceeds. IPv4 literals and `http://[::1]/` skip DNS. Failures use SzError
 * code 6. */

typedef struct {
  int is_err;
  int retry; /* 1 = accept EAGAIN; caller must poll again */
  int drop;  /* 1 = close conn; persistent serve accepts the next client */
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
                          size_t path_sz, int *port, int *is_v6) {
  const char *p;
  const char *slash;
  const char *rb;
  size_t hlen;
  if (!url || !is_v6 || strncmp(url, "http://", 7) != 0)
    return 0;
  *is_v6 = 0;
  *port = 80;
  p = url + 7;
  if (p[0] == '[') {
    rb = strchr(p, ']');
    if (!rb)
      return 0;
    hlen = (size_t)(rb - (p + 1));
    if (hlen == 0 || hlen + 1 > host_sz)
      return 0;
    memcpy(host, p + 1, hlen);
    host[hlen] = '\0';
    *is_v6 = 1;
    p = rb + 1;
    if (p[0] == ':') {
      p++;
      *port = atoi(p);
      if (*port <= 0)
        *port = 80;
      while (*p && *p != '/')
        p++;
    }
    if (p[0] == '\0') {
      memcpy(path, "/", 2);
      return 1;
    }
    if (p[0] != '/' || strlen(p) + 1 > path_sz)
      return 0;
    memcpy(path, p, strlen(p) + 1);
    return 1;
  }
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
  int fd4;
  int fd6;
  int dns_fd;
  uint16_t dns_id_a;
  uint16_t dns_id_aaaa;
  int dns_hops;
  int got_a;
  int got_aaaa;
  int a_done;
  int aaaa_done;
  int he_wait4;
  int64_t dns_deadline_ms;
  int64_t connect_deadline_ms;
  int64_t read_deadline_ms;
  int http_port;
  struct sockaddr_storage peer;
  struct sockaddr_storage peer4;
  struct sockaddr_storage peer6;
  socklen_t peer_len;
  socklen_t peer4_len;
  socklen_t peer6_len;
  char host[256];
  char dns_name[256];
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
                              const char *host, uint16_t qtype) {
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
  wr16(buf + 12 + n, qtype);
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

static int dns_read_name(const uint8_t *buf, size_t n, size_t *off, char *out,
                         size_t cap) {
  size_t o = *off;
  size_t jumps = 0;
  int jumped = 0;
  size_t ret = 0;
  size_t w = 0;
  if (!out || cap == 0)
    return 0;
  out[0] = '\0';
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
      if (w >= cap)
        return 0;
      out[w] = '\0';
      return 1;
    }
    if ((len & 0xC0) != 0)
      return 0;
    if (o + 1 + (size_t)len > n)
      return 0;
    if (w > 0) {
      if (w + 1 >= cap)
        return 0;
      out[w++] = '.';
    }
    if (w + (size_t)len >= cap)
      return 0;
    memcpy(out + w, buf + o + 1, (size_t)len);
    w += (size_t)len;
    o += 1 + (size_t)len;
  }
  return 0;
}

/* 1 = address (A and/or AAAA), 2 = CNAME only, 4 = NODATA, 0 = fail. */
static int dns_parse_answer(const uint8_t *buf, size_t n, uint16_t id,
                            struct in_addr *a_out, uint8_t *aaaa, int *has_a,
                            int *has_aaaa, char *cname, size_t cname_cap) {
  uint16_t flags, qd, rr, i;
  size_t o = 12;
  int got_a = 0;
  int got_aaaa = 0;
  if (has_a)
    *has_a = 0;
  if (has_aaaa)
    *has_aaaa = 0;
  if (n < 12 || rd16(buf) != id)
    return 0;
  flags = rd16(buf + 2);
  if ((flags & 0x8000) == 0 || (flags & 0x000F) != 0)
    return 0;
  qd = rd16(buf + 4);
  rr = (uint16_t)(rd16(buf + 6) + rd16(buf + 8) + rd16(buf + 10));
  if (cname && cname_cap)
    cname[0] = '\0';
  for (i = 0; i < qd; i++) {
    if (!dns_skip_name(buf, n, &o) || o + 4 > n)
      return 0;
    o += 4;
  }
  for (i = 0; i < rr; i++) {
    uint16_t typ, cls, rdlen;
    if (!dns_skip_name(buf, n, &o) || o + 10 > n)
      return 0;
    typ = rd16(buf + o);
    cls = rd16(buf + o + 2);
    rdlen = rd16(buf + o + 8);
    o += 10;
    if (o + rdlen > n)
      return 0;
    if (typ == 1 && cls == 1 && rdlen == 4 && !got_a && a_out) {
      memcpy(&a_out->s_addr, buf + o, 4);
      got_a = 1;
    } else if (typ == 28 && cls == 1 && rdlen == 16 && !got_aaaa && aaaa) {
      memcpy(aaaa, buf + o, 16);
      got_aaaa = 1;
    } else if (typ == 5 && cls == 1 && cname && cname_cap && cname[0] == '\0') {
      size_t name_off = o;
      char tmp[256];
      if (dns_read_name(buf, n, &name_off, tmp, sizeof tmp) == 1 && tmp[0] &&
          strlen(tmp) < cname_cap)
        memcpy(cname, tmp, strlen(tmp) + 1);
    }
    o += rdlen;
  }
  if (got_a && has_a)
    *has_a = 1;
  if (got_aaaa && has_aaaa)
    *has_aaaa = 1;
  if (got_a || got_aaaa)
    return 1;
  if (cname && cname[0])
    return 2;
  return 4;
}

static int dns_send_one(GetSt *st, uint16_t qtype, uint16_t *id_out) {
  struct sockaddr_in ns;
  uint8_t q[512];
  size_t qn;
  ssize_t nsent;
  if (!st || !id_out || !nameserver_addr(&ns))
    return 0;
  g_dns_qid++;
  if (g_dns_qid == 0)
    g_dns_qid = 1;
  *id_out = g_dns_qid;
  qn = dns_build_query(q, sizeof q, *id_out, st->dns_name, qtype);
  if (qn == 0)
    return 0;
  if (st->dns_fd < 0) {
    st->dns_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (st->dns_fd < 0)
      return 0;
    if (set_nonblock(st->dns_fd) != 0) {
      close(st->dns_fd);
      st->dns_fd = -1;
      return 0;
    }
  }
  nsent = sendto(st->dns_fd, q, qn, 0, (struct sockaddr *)&ns, sizeof ns);
  return nsent == (ssize_t)qn;
}

static int dns_send_pair(GetSt *st) {
  if (!st)
    return 0;
  st->got_a = 0;
  st->got_aaaa = 0;
  st->a_done = 0;
  st->aaaa_done = 0;
  if (!dns_send_one(st, 1, &st->dns_id_a))
    return 0;
  return dns_send_one(st, 28, &st->dns_id_aaaa);
}

static void addr_set_v4(struct sockaddr_storage *ss, socklen_t *len,
                       struct in_addr a, int port) {
  struct sockaddr_in *a4;
  if (!ss || !len)
    return;
  memset(ss, 0, sizeof(*ss));
  a4 = (struct sockaddr_in *)ss;
  a4->sin_family = AF_INET;
  a4->sin_port = htons((uint16_t)port);
  a4->sin_addr = a;
  *len = sizeof(*a4);
}

static void addr_set_v6(struct sockaddr_storage *ss, socklen_t *len,
                       const uint8_t *addr16, int port) {
  struct sockaddr_in6 *a6;
  if (!ss || !len || !addr16)
    return;
  memset(ss, 0, sizeof(*ss));
  a6 = (struct sockaddr_in6 *)ss;
  a6->sin6_family = AF_INET6;
#ifdef __APPLE__
  a6->sin6_len = (uint8_t)sizeof(*a6);
#endif
  a6->sin6_port = htons((uint16_t)port);
  memcpy(&a6->sin6_addr, addr16, 16);
  *len = sizeof(*a6);
}

#define HE_A_DELAY_MS 250
#define HE_CONNECT_MS 1000
#define HE_DNS_MS 1000
#define HE_READ_MS 1000
#define HE_REQ_MS 1000
#define HE_WRITE_MS 1000

static void get_free(GetSt *st) {
  if (!st)
    return;
  if (st->fd4 >= 0 && st->fd4 != st->fd)
    close(st->fd4);
  if (st->fd6 >= 0 && st->fd6 != st->fd)
    close(st->fd6);
  st->fd4 = -1;
  st->fd6 = -1;
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
  int is_v6 = 0;
  struct sockaddr_in *a4;
  struct sockaddr_in6 *a6;

  if (!parse_http_url(url, st->host, sizeof st->host, st->path, sizeof st->path,
                      &port, &is_v6)) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: only http:// URLs supported");
    return r;
  }
  st->http_port = port;
  memset(&st->peer, 0, sizeof st->peer);
  if (is_v6) {
    a6 = (struct sockaddr_in6 *)&st->peer;
    a6->sin6_family = AF_INET6;
#ifdef __APPLE__
    a6->sin6_len = (uint8_t)sizeof(*a6);
#endif
    a6->sin6_port = htons((uint16_t)port);
    st->peer_len = sizeof(*a6);
    if (inet_pton(AF_INET6, st->host, &a6->sin6_addr) != 1) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: invalid IPv6 literal");
      return r;
    }
    r->is_err = 0;
    return r;
  }
  a4 = (struct sockaddr_in *)&st->peer;
  a4->sin_family = AF_INET;
  a4->sin_port = htons((uint16_t)port);
  st->peer_len = sizeof(*a4);
  if (inet_pton(AF_INET, st->host, &a4->sin_addr) == 1) {
    r->is_err = 0;
    return r;
  }
  if (strlen(st->host) >= sizeof st->dns_name) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
    return r;
  }
  memcpy(st->dns_name, st->host, strlen(st->host) + 1);
  if (!dns_send_pair(st)) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
    return r;
  }
  st->dns_deadline_ms = sz_clock_monotonic_ms_sync() + HE_DNS_MS;
  r->is_err = 0;
  return r;
}

static void *dns_wait_more(GetSt *st, NetResult *r) {
  if (st->a_done && st->aaaa_done) {
    if (!st->got_a && !st->got_aaaa) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
      return r;
    }
    close(st->dns_fd);
    st->dns_fd = -1;
    r->is_err = 0;
    return r;
  }
  if (sz_clock_monotonic_ms_sync() >= st->dns_deadline_ms) {
    if (st->got_a || st->got_aaaa) {
      close(st->dns_fd);
      st->dns_fd = -1;
      r->is_err = 0;
      return r;
    }
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS timed out");
    return r;
  }
  r->retry = 1;
  return r;
}

static void *get_dns_recv(void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  uint8_t buf[512];
  ssize_t n;

  n = recvfrom(st->dns_fd, buf, sizeof buf, 0, NULL, NULL);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return dns_wait_more(st, r);
  if (n < 12) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
    return r;
  }
  {
    uint16_t id = rd16(buf);
    char cname[256];
    uint8_t aaaa[16];
    struct in_addr a4;
    int has_a = 0;
    int has_aaaa = 0;
    int kind;
    if (id != st->dns_id_a && id != st->dns_id_aaaa)
      return dns_wait_more(st, r);
    kind = dns_parse_answer(buf, (size_t)n, id, &a4, aaaa, &has_a, &has_aaaa,
                            cname, sizeof cname);
    if (kind == 1) {
      if (has_a) {
        addr_set_v4(&st->peer4, &st->peer4_len, a4, st->http_port);
        st->got_a = 1;
        st->a_done = 1;
      }
      if (has_aaaa) {
        addr_set_v6(&st->peer6, &st->peer6_len, aaaa, st->http_port);
        st->got_aaaa = 1;
        st->aaaa_done = 1;
      }
    } else if (kind == 2 && st->dns_hops < 5 && strlen(cname) > 0 &&
               strlen(cname) < sizeof st->dns_name) {
      st->dns_hops++;
      memcpy(st->dns_name, cname, strlen(cname) + 1);
      if (!dns_send_pair(st)) {
        r->is_err = 1;
        r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
        return r;
      }
    } else if (kind == 4) {
      if (id == st->dns_id_a)
        st->a_done = 1;
      if (id == st->dns_id_aaaa)
        st->aaaa_done = 1;
    } else if (kind != 1) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
      return r;
    }
    return dns_wait_more(st, r);
  }
}

static void *get_check_write(void *env);

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
  if (connect(fd, sa, len) != 0 && errno != EINPROGRESS && errno != EAGAIN) {
    close(fd);
    return -1;
  }
  return fd;
}

static void get_build_req(GetSt *st) {
  char req[2048];
  char hosthdr[300];
  size_t nreq;
  if (strchr(st->host, ':'))
    snprintf(hosthdr, sizeof hosthdr, "[%s]", st->host);
  else
    snprintf(hosthdr, sizeof hosthdr, "%s", st->host);
  nreq = (size_t)snprintf(req, sizeof req,
                          "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
                          st->path, hosthdr);
  if (nreq >= sizeof req)
    nreq = sizeof req - 1;
  st->req = (char *)sz_alloc(nreq + 1);
  memcpy(st->req, req, nreq + 1);
  st->req_len = nreq;
  st->req_off = 0;
  st->acc = (char *)sz_alloc(1);
  st->acc[0] = '\0';
  st->total = 0;
}

static void *get_tcp_connect(void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  int fd;

  if (st->got_aaaa)
    st->fd6 = tcp_begin((struct sockaddr *)&st->peer6, st->peer6_len);
  if (st->got_a && st->fd6 < 0)
    st->fd4 = tcp_begin((struct sockaddr *)&st->peer4, st->peer4_len);
  st->he_wait4 = st->got_a && st->fd6 >= 0;
  if (st->got_a || st->got_aaaa) {
    if (st->fd4 < 0 && st->fd6 < 0) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: connect failed");
      return r;
    }
    if (st->he_wait4 || (st->fd4 >= 0 && st->fd6 >= 0))
      st->fd = -1;
    else {
      st->fd = st->fd4 >= 0 ? st->fd4 : st->fd6;
      st->fd4 = -1;
      st->fd6 = -1;
    }
    get_build_req(st);
    st->connect_deadline_ms = sz_clock_monotonic_ms_sync() + HE_CONNECT_MS;
    r->is_err = 0;
    return r;
  }
  fd = tcp_begin((struct sockaddr *)&st->peer, st->peer_len);
  if (fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: connect failed");
    return r;
  }
  st->fd = fd;
  get_build_req(st);
  st->connect_deadline_ms = sz_clock_monotonic_ms_sync() + HE_CONNECT_MS;
  r->is_err = 0;
  return r;
}

static int fd_pollout(int fd) {
  struct pollfd p;
  if (fd < 0)
    return 0;
  memset(&p, 0, sizeof p);
  p.fd = fd;
  p.events = POLLOUT;
  return poll(&p, 1, 0) > 0 && (p.revents & (POLLOUT | POLLERR | POLLHUP));
}

static int fd_soerr(int fd) {
  int so = 0;
  socklen_t sl = sizeof so;
  if (fd < 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &so, &sl) != 0)
    return -1;
  return so;
}

static void he_take(GetSt *st, int win, int lose) {
  if (lose >= 0)
    close(lose);
  st->fd = win;
  st->fd4 = -1;
  st->fd6 = -1;
  st->he_wait4 = 0;
}

static int he_start_v4(GetSt *st) {
  st->he_wait4 = 0;
  if (!st->got_a || st->fd4 >= 0)
    return 1;
  st->fd4 = tcp_begin((struct sockaddr *)&st->peer4, st->peer4_len);
  return st->fd4 >= 0;
}

static void *get_he_pick(GetSt *st) {
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  int w6 = fd_pollout(st->fd6);
  int w4 = fd_pollout(st->fd4);
  int e6 = fd_soerr(st->fd6);
  int e4 = fd_soerr(st->fd4);
  if (w6 && e6 == 0) {
    he_take(st, st->fd6, st->fd4);
    sz_free(r);
    return get_check_write(st);
  }
  if (w4 && e4 == 0) {
    he_take(st, st->fd4, st->fd6);
    sz_free(r);
    return get_check_write(st);
  }
  if (w6 && e6 != 0) {
    close(st->fd6);
    st->fd6 = -1;
  }
  if (w4 && e4 != 0) {
    close(st->fd4);
    st->fd4 = -1;
  }
  if (st->he_wait4 && st->fd4 < 0)
    he_start_v4(st);
  if (st->fd4 < 0 && st->fd6 < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: connect failed");
    return r;
  }
  if (sz_clock_monotonic_ms_sync() >= st->connect_deadline_ms) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: connect timed out");
    return r;
  }
  r->retry = 1;
  return r;
}

static void *get_check_write(void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r;
  int so = 0;
  socklen_t sl = sizeof so;
  ssize_t n;

  if (st->fd < 0)
    return get_he_pick(st);
  r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
  if (!fd_pollout(st->fd)) {
    if (sz_clock_monotonic_ms_sync() >= st->connect_deadline_ms) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: connect timed out");
      return r;
    }
    r->retry = 1;
    return r;
  }

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
    if (sz_clock_monotonic_ms_sync() >= st->read_deadline_ms) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: read timed out");
      return r;
    }
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
  SzIo *ready;
  (void)value;
  if (st->fd >= 0)
    ready = sz_io_poll_writable(st->fd);
  else if (st->fd4 >= 0 && st->fd6 >= 0)
    ready = sz_io_race(sz_io_poll_writable(st->fd4), sz_io_poll_writable(st->fd6));
  else if (st->he_wait4 && st->fd6 >= 0)
    ready = sz_io_race(sz_io_poll_writable(st->fd6), sz_io_sleep_ms(HE_A_DELAY_MS));
  else if (st->fd6 >= 0)
    ready = sz_io_poll_writable(st->fd6);
  else
    ready = sz_io_poll_writable(st->fd4);
  {
    int64_t left = st->connect_deadline_ms - sz_clock_monotonic_ms_sync();
    if (left < 1)
      left = 1;
    ready = sz_io_race(ready, sz_io_sleep_ms(left));
  }
  return sz_io_flatmap(ready, get_after_write_poll, st);
}

static SzIo *get_after_read_poll(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_delay(get_read, st), get_unwrap_read, st);
}

static SzIo *get_poll_read(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  SzIo *ready;
  int64_t left;
  (void)value;
  if (st->read_deadline_ms == 0)
    st->read_deadline_ms = sz_clock_monotonic_ms_sync() + HE_READ_MS;
  left = st->read_deadline_ms - sz_clock_monotonic_ms_sync();
  if (left < 1)
    left = 1;
  ready = sz_io_race(sz_io_poll_readable(st->fd), sz_io_sleep_ms(left));
  return sz_io_flatmap(ready, get_after_read_poll, st);
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
  SzIo *ready;
  int64_t left;
  (void)value;
  left = st->dns_deadline_ms - sz_clock_monotonic_ms_sync();
  if (left < 1)
    left = 1;
  ready = sz_io_race(sz_io_poll_readable(st->dns_fd), sz_io_sleep_ms(left));
  return sz_io_flatmap(ready, get_after_dns_poll, st);
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
  st->fd4 = -1;
  st->fd6 = -1;
  st->dns_fd = -1;
  return sz_io_flatmap(sz_io_delay(get_dispatch, st), get_after_dispatch, st);
}

/* HTTP/1.0 GET server. Listen and connection fds are nonblocking. The fiber
 * parks on poll so other IO can run. Live listen is 127.0.0.1 and ::1 (V6ONLY)
 * so httpGet literals on either loopback match. TestRuntime injects paths and
 * skips sockets. Request read and response write each wait at most 1000ms.
 * A timed-out, malformed, reset, or handler-failed client is dropped.
 * Persistent serve accepts the next. Error code 6. serveOnce is one request.
 * serve keeps the
 * listen sockets (n<=0 forever live, or until the TestRuntime queue is empty). */

typedef struct ServeSt {
  int64_t port;
  int64_t left; /* >0 remaining; <=0 forever (TESTRT drains the queue) */
  int listen_fd;
  int listen6_fd;
  int conn_fd;
  SzCont handler;
  void *henv;
  void *body;
  char rbuf[4096];
  size_t rlen;
  size_t woff;
  int64_t req_deadline_ms;
  int64_t write_deadline_ms;
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
  if (st->listen6_fd >= 0) {
    close(st->listen6_fd);
    st->listen6_fd = -1;
  }
}

static void serve_free(ServeSt *st) {
  if (!st)
    return;
  serve_close_fds(st);
  sz_release(st->henv);
  st->henv = NULL;
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

static int serve_bind_v4(int port) {
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

static int serve_bind_v6(int port) {
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

static void *serve_ensure_listen(void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)sz_alloc_zero(sizeof(NetResult));
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
  st->listen_fd = serve_bind_v4(port);
  st->listen6_fd = serve_bind_v6(port);
  if (st->listen_fd < 0 || st->listen6_fd < 0) {
    serve_close_fds(st);
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: bind/listen failed");
    return r;
  }
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
  st->write_deadline_ms = 0;

  /* Chosen at step time so SCUZZ_TESTRT=1 install in runtime_main is visible. */
  if (sz_testrt_net_is_fake()) {
    char *path_s = sz_testrt_net_pop_request();
    r->is_err = 0;
    r->as.ok = sz_string_from_cstr(path_s ? path_s : "/");
    sz_free(path_s);
    return r;
  }
  fd = st->listen_fd;
  if (fd < 0 && st->listen6_fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: not listening");
    return r;
  }
  conn = fd >= 0 ? accept(fd, NULL, NULL) : -1;
  if (conn < 0 && (fd < 0 || errno == EAGAIN || errno == EWOULDBLOCK) &&
      st->listen6_fd >= 0)
    conn = accept(st->listen6_fd, NULL, NULL);
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
  {
    int snd = 4096;
    setsockopt(conn, SOL_SOCKET, SO_SNDBUF, &snd, sizeof snd);
#ifdef SO_NOSIGPIPE
    {
      int nosig = 1;
      setsockopt(conn, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof nosig);
    }
#endif
  }
  st->conn_fd = conn;
  st->req_deadline_ms = sz_clock_monotonic_ms_sync() + HE_REQ_MS;
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
    if (sz_clock_monotonic_ms_sync() >= st->req_deadline_ms) {
      r->is_err = 1;
      r->drop = 1;
      r->as.err = sz_error_new(6, "Net.serve: request timed out");
      return r;
    }
    r->retry = 1;
    return r;
  }
  if (n <= 0) {
    r->is_err = 1;
    r->drop = 1;
    r->as.err = sz_error_new(6, "Net.serve: expected HTTP GET");
    return r;
  }
  st->rlen += (size_t)n;
  st->rbuf[st->rlen] = '\0';
  if (!strstr(st->rbuf, "\r\n\r\n")) {
    if (st->rlen + 1 >= sizeof st->rbuf) {
      r->is_err = 1;
      r->drop = 1;
      r->as.err = sz_error_new(6, "Net.serve: expected HTTP GET");
      return r;
    }
    if (sz_clock_monotonic_ms_sync() >= st->req_deadline_ms) {
      r->is_err = 1;
      r->drop = 1;
      r->as.err = sz_error_new(6, "Net.serve: request timed out");
      return r;
    }
    r->retry = 1;
    return r;
  }
  if (!parse_get_path(st->rbuf, path, sizeof path)) {
    r->is_err = 1;
    r->drop = 1;
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
    r->drop = 1;
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
#ifdef MSG_NOSIGNAL
  n = send(st->conn_fd, src + src_off, src_len - src_off, MSG_NOSIGNAL);
#else
  n = write(st->conn_fd, src + src_off, src_len - src_off);
#endif
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    if (sz_clock_monotonic_ms_sync() >= st->write_deadline_ms) {
      r->is_err = 1;
      r->drop = 1;
      r->as.err = sz_error_new(6, "Net.serve: write timed out");
      return r;
    }
    r->retry = 1;
    return r;
  }
  if (n <= 0) {
    r->is_err = 1;
    r->drop = 1;
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

static SzIo *serve_drop_conn(ServeSt *st, SzError *err) {
  serve_close_conn(st);
  if (st->left == 1)
    return sz_io_fail(err);
  sz_error_free(err);
  return serve_round(st);
}

static SzIo *serve_unwrap_read(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_free(r);
    return serve_poll_conn_read(NULL, st);
  }
  if (r && r->drop) {
    SzError *err = r->as.err;
    sz_free(r);
    return serve_drop_conn(st, err);
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
  if (r && r->drop) {
    SzError *err = r->as.err;
    sz_free(r);
    return serve_drop_conn(st, err);
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
  SzIo *ready;
  (void)value;
  if (st->listen6_fd >= 0)
    ready = sz_io_race(sz_io_poll_readable(st->listen_fd),
                       sz_io_poll_readable(st->listen6_fd));
  else
    ready = sz_io_poll_readable(st->listen_fd);
  return sz_io_flatmap(ready, serve_after_accept_poll, st);
}

static SzIo *serve_after_conn_read_poll(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_delay(serve_read_req, st), serve_unwrap_read, st);
}

static SzIo *serve_poll_conn_read(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzIo *ready;
  int64_t left;
  (void)value;
  left = st->req_deadline_ms - sz_clock_monotonic_ms_sync();
  if (left < 1)
    left = 1;
  ready = sz_io_race(sz_io_poll_readable(st->conn_fd), sz_io_sleep_ms(left));
  return sz_io_flatmap(ready, serve_after_conn_read_poll, st);
}

static SzIo *serve_after_conn_write_poll(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  return sz_io_flatmap(sz_io_delay(serve_write_close, st), serve_unwrap_write, st);
}

static SzIo *serve_poll_conn_write(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzIo *ready;
  int64_t left;
  (void)value;
  if (st->write_deadline_ms == 0)
    st->write_deadline_ms = sz_clock_monotonic_ms_sync() + HE_WRITE_MS;
  left = st->write_deadline_ms - sz_clock_monotonic_ms_sync();
  if (left < 1)
    left = 1;
  ready = sz_io_race(sz_io_poll_writable(st->conn_fd), sz_io_sleep_ms(left));
  return sz_io_flatmap(ready, serve_after_conn_write_poll, st);
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

static SzIo *serve_on_handler_err(SzError *err, void *env) {
  return serve_drop_conn((ServeSt *)env, err);
}

static SzIo *serve_after_path(void *path, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzIo *io = st->handler(path, st->henv);
  io = sz_io_flatmap(io, serve_after_body, st);
  io = sz_io_flatmap(io, serve_after_write, st);
  return sz_io_handle_error_with(io, serve_on_handler_err, st);
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
  return sz_io_flatmap(prog, serve_after_path, st);
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
  st->listen6_fd = -1;
  st->conn_fd = -1;
  st->handler = handler;
  sz_retain(env);
  st->henv = env;
  return sz_io_handle_error_with(serve_round(st), serve_on_err, st);
}

SzIo *sz_net_serve(int64_t port, SzCont handler, void *env) {
  return net_serve_n(port, 0, handler, env);
}

SzIo *sz_net_serve_once(int64_t port, SzCont handler, void *env) {
  return net_serve_n(port, 1, handler, env);
}
