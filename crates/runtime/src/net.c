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
#include <stddef.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Blessed Net.httpGet — live HTTP/1.0 GET or TestRuntime stub map.
 * Live hostnames query A and AAAA together (park on poll). DNS takes answer
 * RRs whose owner matches the query name (ASCII case-insensitive). CNAME
 * chains re-query both (cap 5). UDP replies must match the nameserver
 * address. Query IDs are not sequential. When both addresses exist, start
 * AAAA first and wait 250ms from connect start before the A connect so
 * working IPv6 wins (one A and one AAAA). DNS, connect, write, and the
 * response read each wait at most 1000ms. A 2xx response finishes on
 * Content-Length or EOF. Bodies cap at 1 MiB. Chunked encoding fails.
 * IPv4 literals and `http://[::1]/` skip DNS. Failures use SzError code 6. */

typedef struct {
  int is_err;
  int retry; /* 1 = accept EAGAIN; caller must poll again */
  int drop;  /* 1 = close conn; persistent serve accepts the next client */
  union {
    SzError *err;
    void *ok;
  } as;
} NetResult;

/* DELAY wraps `r` in PURE (extra retain at run). Drop that retain here. */
static SzIo *unwrap_net(void *value, void *env) {
  (void)env;
  NetResult *r = (NetResult *)value;
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

/* 1 = ok, 0 = invalid URL, -1 = invalid port. */
static int url_has_bad_bytes(const char *s, size_t n) {
  size_t i;
  if (!s)
    return 1;
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == 0 || c == '\r' || c == '\n')
      return 1;
  }
  return 0;
}

static int parse_port_digits(const char *s, const char **end, int *port) {
  unsigned long v = 0;
  int digits = 0;
  if (!s || s[0] < '0' || s[0] > '9')
    return 0;
  while (s[0] >= '0' && s[0] <= '9') {
    v = v * 10UL + (unsigned long)(s[0] - '0');
    s++;
    digits++;
    if (digits > 5 || v > 65535UL)
      return 0;
  }
  if (v < 1UL)
    return 0;
  *port = (int)v;
  if (end)
    *end = s;
  return 1;
}

static int parse_http_url(const char *url, char *host, size_t host_sz, char *path,
                          size_t path_sz, int *port, int *is_v6) {
  const char *p;
  const char *slash;
  const char *rb;
  const char *rest;
  size_t hlen;
  size_t ulen;
  if (!url || !host || !path || !port || !is_v6)
    return 0;
  ulen = strlen(url);
  if (url_has_bad_bytes(url, ulen))
    return 0;
  if (strncmp(url, "http://", 7) != 0)
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
    if (strchr(host, '@') || url_has_bad_bytes(host, hlen))
      return 0;
    *is_v6 = 1;
    p = rb + 1;
    if (p[0] == ':') {
      p++;
      if (!parse_port_digits(p, &rest, port))
        return -1;
      if (*rest && *rest != '/')
        return -1;
      p = rest;
    }
    if (p[0] == '\0') {
      memcpy(path, "/", 2);
      return 1;
    }
    if (p[0] != '/' || strlen(p) + 1 > path_sz)
      return 0;
    if (url_has_bad_bytes(p, strlen(p)))
      return 0;
    memcpy(path, p, strlen(p) + 1);
    return 1;
  }
  slash = strchr(p, '/');
  if (slash) {
    hlen = (size_t)(slash - p);
    if (hlen == 0 || hlen + 1 > host_sz || (size_t)strlen(slash) + 1 > path_sz)
      return 0;
    memcpy(host, p, hlen);
    host[hlen] = '\0';
    if (url_has_bad_bytes(slash, strlen(slash)))
      return 0;
    memcpy(path, slash, strlen(slash) + 1);
  } else {
    hlen = strlen(p);
    if (hlen == 0 || hlen + 1 > host_sz)
      return 0;
    memcpy(host, p, hlen + 1);
    memcpy(path, "/", 2);
  }
  if (strchr(host, '@') || url_has_bad_bytes(host, strlen(host)))
    return 0;
  {
    char *colon = strchr(host, ':');
    if (colon) {
      *colon = '\0';
      if (host[0] == '\0')
        return 0;
      if (!parse_port_digits(colon + 1, &rest, port))
        return -1;
      if (*rest)
        return -1;
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
  int tcp_up;
  int dns_ns_ok;
  int64_t dns_deadline_ms;
  int64_t connect_deadline_ms;
  int64_t write_deadline_ms;
  int64_t read_deadline_ms;
  int64_t he_v4_at_ms;
  int http_port;
  struct sockaddr_in dns_ns;
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
  size_t acc_cap;
  size_t total;
} GetSt;

static uint64_t g_dns_rng;
static int g_dns_rng_on;
static int g_test_ns_on;
static struct sockaddr_in g_test_ns;

static void dns_rng_seed(void) {
  FILE *f;
  uint64_t s = 0;
  if (g_dns_rng_on)
    return;
  f = fopen("/dev/urandom", "rb");
  if (f) {
    if (fread(&s, sizeof s, 1, f) != 1)
      s = 0;
    fclose(f);
  }
  if (s == 0) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
      s = ((uint64_t)ts.tv_sec << 32) ^ (uint64_t)ts.tv_nsec ^ 0x9e3779b97f4a7c15ULL;
  }
  g_dns_rng = s ? s : 1;
  g_dns_rng_on = 1;
}

static uint16_t dns_fresh_id(void) {
  uint16_t id;
  dns_rng_seed();
  do {
    g_dns_rng = g_dns_rng * 6364136223846793005ULL + 1;
    id = (uint16_t)(g_dns_rng >> 48);
  } while (id == 0);
  return id;
}

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

static int dns_name_eq(const char *a, const char *b) {
  size_t i;
  if (!a || !b)
    return 0;
  for (i = 0; a[i] || b[i]; i++) {
    unsigned char ca = (unsigned char)a[i];
    unsigned char cb = (unsigned char)b[i];
    if (ca >= 'A' && ca <= 'Z')
      ca = (unsigned char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z')
      cb = (unsigned char)(cb - 'A' + 'a');
    if (ca != cb)
      return 0;
  }
  return 1;
}

/* 1 = address, 2 = CNAME only, 3 = ignore packet, 4 = this type done, 0 = fail. */
static int dns_parse_answer(const uint8_t *buf, size_t n, uint16_t id,
                            const char *qname, struct in_addr *a_out,
                            uint8_t *aaaa, int *has_a, int *has_aaaa,
                            char *cname, size_t cname_cap) {
  uint16_t flags, qd, an, i, rcode;
  size_t o = 12;
  int got_a = 0;
  int got_aaaa = 0;
  if (has_a)
    *has_a = 0;
  if (has_aaaa)
    *has_aaaa = 0;
  if (cname && cname_cap)
    cname[0] = '\0';
  if (n < 12 || rd16(buf) != id)
    return 3;
  flags = rd16(buf + 2);
  if ((flags & 0x8000) == 0)
    return 3;
  if (flags & 0x0200)
    return 3; /* TC */
  rcode = (uint16_t)(flags & 0x000F);
  if (rcode == 3)
    return 4; /* NXDOMAIN: this type done; wait for the twin query */
  if (rcode != 0)
    return 4; /* SERVFAIL / FORMERR / REFUSED: this type done */
  qd = rd16(buf + 4);
  an = rd16(buf + 6);
  for (i = 0; i < qd; i++) {
    if (!dns_skip_name(buf, n, &o) || o + 4 > n)
      return 3;
    o += 4;
  }
  for (i = 0; i < an; i++) {
    uint16_t typ, cls, rdlen;
    char owner[256];
    if (!dns_read_name(buf, n, &o, owner, sizeof owner) || o + 10 > n)
      return 3;
    typ = rd16(buf + o);
    cls = rd16(buf + o + 2);
    rdlen = rd16(buf + o + 8);
    o += 10;
    if (o + rdlen > n)
      return 3;
    if (dns_name_eq(owner, qname) && cls == 1) {
      if (typ == 1 && rdlen == 4 && !got_a && a_out) {
        memcpy(&a_out->s_addr, buf + o, 4);
        got_a = 1;
      } else if (typ == 28 && rdlen == 16 && !got_aaaa && aaaa) {
        memcpy(aaaa, buf + o, 16);
        got_aaaa = 1;
      } else if (typ == 5 && cname && cname_cap && cname[0] == '\0') {
        char tmp[256];
          size_t c_off = o;
          if (dns_read_name(buf, n, &c_off, tmp, sizeof tmp) == 1 && tmp[0] &&
              strlen(tmp) < cname_cap)
            memcpy(cname, tmp, strlen(tmp) + 1);
      }
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
  st->dns_ns = ns;
  st->dns_ns_ok = 1;
  *id_out = dns_fresh_id();
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
#define HE_BODY_MAX (1024u * 1024u)

/* Close sockets and drop URL/buffers. A second call is a no-op. Cancel and
 * success both run this through IO.ensure. */
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
  st->req = NULL;
  sz_free(st->acc);
  st->acc = NULL;
  st->acc_cap = 0;
  sz_release(st->url);
  st->url = NULL;
}

static void *get_cleanup(void *env) {
  get_free((GetSt *)env);
  return NULL;
}

static void *get_start(void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)rc_box_zero(sizeof(NetResult));
  const char *url = sz_string_cstr(st->url);
  int port = 80;
  int is_v6 = 0;
  struct sockaddr_in *a4;
  struct sockaddr_in6 *a6;

  int url_ok;

  if (strncmp(url ? url : "", "http://", 7) != 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: only http:// URLs supported");
    return r;
  }
  url_ok = parse_http_url(url, st->host, sizeof st->host, st->path, sizeof st->path,
                          &port, &is_v6);
  if (url_ok < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: invalid port");
    return r;
  }
  if (url_ok != 1) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: invalid URL");
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
  NetResult *r = (NetResult *)rc_box_zero(sizeof(NetResult));
  uint8_t buf[512];
  ssize_t n;
  struct sockaddr_in from;
  socklen_t flen = sizeof from;

  memset(&from, 0, sizeof from);
  n = recvfrom(st->dns_fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return dns_wait_more(st, r);
  if (n < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: DNS failed");
    return r;
  }
  if (st->dns_ns_ok &&
      (from.sin_family != AF_INET || from.sin_port != st->dns_ns.sin_port ||
       from.sin_addr.s_addr != st->dns_ns.sin_addr.s_addr))
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
    kind = dns_parse_answer(buf, (size_t)n, id, st->dns_name, &a4, aaaa, &has_a,
                            &has_aaaa, cname, sizeof cname);
    if (kind == 3)
      return dns_wait_more(st, r);
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
    } else {
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

/* RFC 9110: omit the port when it is the default for the scheme (80). */
static void http_fmt_hosthdr(char *dst, size_t cap, const char *host, int port) {
  int v6 = host && strchr(host, ':') != NULL;
  if (!dst || cap == 0)
    return;
  if (!host)
    host = "";
  if (port != 80) {
    if (v6)
      snprintf(dst, cap, "[%s]:%d", host, port);
    else
      snprintf(dst, cap, "%s:%d", host, port);
    return;
  }
  if (v6)
    snprintf(dst, cap, "[%s]", host);
  else
    snprintf(dst, cap, "%s", host);
}

void sz_net_test_http_host_header(const char *host, int port, char *out,
                                 size_t cap) {
  http_fmt_hosthdr(out, cap, host, port);
}

static void get_build_req(GetSt *st) {
  char req[2048];
  char hosthdr[300];
  size_t nreq;
  http_fmt_hosthdr(hosthdr, sizeof hosthdr, st->host, st->http_port);
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
  st->acc_cap = 1;
  st->total = 0;
}

static void *get_tcp_connect(void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)rc_box_zero(sizeof(NetResult));
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
    if (st->he_wait4)
      st->he_v4_at_ms = sz_clock_monotonic_ms_sync() + HE_A_DELAY_MS;
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
  NetResult *r = (NetResult *)rc_box_zero(sizeof(NetResult));
  int w6 = fd_pollout(st->fd6);
  int w4 = fd_pollout(st->fd4);
  int e6 = fd_soerr(st->fd6);
  int e4 = fd_soerr(st->fd4);
  if (w6 && e6 == 0) {
    he_take(st, st->fd6, st->fd4);
    sz_release(r);
    return get_check_write(st);
  }
  if (w4 && e4 == 0) {
    he_take(st, st->fd4, st->fd6);
    sz_release(r);
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
  r = (NetResult *)rc_box_zero(sizeof(NetResult));
  if (!fd_pollout(st->fd)) {
    int64_t dl = st->tcp_up ? st->write_deadline_ms : st->connect_deadline_ms;
    const char *msg = st->tcp_up ? "Net.httpGet: write timed out"
                                 : "Net.httpGet: connect timed out";
    if (sz_clock_monotonic_ms_sync() >= dl) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, msg);
      return r;
    }
    r->retry = 1;
    return r;
  }

  if (!st->tcp_up) {
    if (getsockopt(st->fd, SOL_SOCKET, SO_ERROR, &so, &sl) != 0 || so != 0) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: connect failed");
      return r;
    }
    st->tcp_up = 1;
    st->write_deadline_ms = sz_clock_monotonic_ms_sync() + HE_WRITE_MS;
  }
#ifdef MSG_NOSIGNAL
  n = send(st->fd, st->req + st->req_off, st->req_len - st->req_off, MSG_NOSIGNAL);
#else
  n = write(st->fd, st->req + st->req_off, st->req_len - st->req_off);
#endif
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    if (sz_clock_monotonic_ms_sync() >= st->write_deadline_ms) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: write timed out");
      return r;
    }
    r->retry = 1;
    return r;
  }
  if (n <= 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: write failed");
    return r;
  }
  st->req_off += (size_t)n;
  if (st->req_off < st->req_len) {
    if (sz_clock_monotonic_ms_sync() >= st->write_deadline_ms) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: write timed out");
      return r;
    }
    r->retry = 1;
  }
  return r;
}

static int ascii_ieq(const char *a, const char *b, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    unsigned char ca = (unsigned char)a[i];
    unsigned char cb = (unsigned char)b[i];
    if (ca >= 'A' && ca <= 'Z')
      ca = (unsigned char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z')
      cb = (unsigned char)(cb - 'A' + 'a');
    if (ca != cb)
      return 0;
  }
  return 1;
}

static size_t http_hdr_len(const char *acc, size_t total) {
  size_t i;
  if (!acc || total < 4)
    return 0;
  for (i = 0; i + 3 < total; i++) {
    if (acc[i] == '\r' && acc[i + 1] == '\n' && acc[i + 2] == '\r' &&
        acc[i + 3] == '\n')
      return i + 4;
  }
  return 0;
}

static int http_status_code(const char *acc, size_t hdr_len) {
  size_t line = 0;
  size_t i;
  if (!acc || hdr_len < 12)
    return -1;
  while (line + 1 < hdr_len && !(acc[line] == '\r' && acc[line + 1] == '\n'))
    line++;
  if (memcmp(acc, "HTTP/1.", 7) != 0)
    return -1;
  i = 8;
  while (i < line && acc[i] != ' ')
    i++;
  while (i < line && acc[i] == ' ')
    i++;
  if (i + 3 > line)
    return -1;
  if (acc[i] < '0' || acc[i] > '9' || acc[i + 1] < '0' || acc[i + 1] > '9' ||
      acc[i + 2] < '0' || acc[i + 2] > '9')
    return -1;
  return (acc[i] - '0') * 100 + (acc[i + 1] - '0') * 10 + (acc[i + 2] - '0');
}

/* Field name match. Strip OWS around the name so `Transfer-Encoding :` still
 * matches. */
static int http_field_name_eq(const char *acc, size_t start, size_t colon,
                              const char *name) {
  size_t nlen;
  size_t a;
  size_t b;
  if (!acc || !name || colon < start)
    return 0;
  nlen = strlen(name);
  a = start;
  b = colon;
  while (a < b && (acc[a] == ' ' || acc[a] == '\t'))
    a++;
  while (b > a && (acc[b - 1] == ' ' || acc[b - 1] == '\t'))
    b--;
  if ((size_t)(b - a) != nlen)
    return 0;
  return ascii_ieq(acc + a, name, nlen);
}

static int http_header_present(const char *acc, size_t hdr_len, const char *name) {
  size_t i = 0;
  while (i + 1 < hdr_len && !(acc[i] == '\r' && acc[i + 1] == '\n'))
    i++;
  i += 2;
  while (i < hdr_len) {
    size_t start = i;
    size_t colon;
    if (acc[i] == '\r')
      break;
    while (i + 1 < hdr_len && !(acc[i] == '\r' && acc[i + 1] == '\n'))
      i++;
    colon = start;
    while (colon < i && acc[colon] != ':')
      colon++;
    if (colon < i && http_field_name_eq(acc, start, colon, name))
      return 1;
    i += 2;
  }
  return 0;
}

static int http_content_length(const char *acc, size_t hdr_len, size_t *out) {
  size_t i = 0;
  const char *name = "Content-Length";
  int found = 0;
  while (i + 1 < hdr_len && !(acc[i] == '\r' && acc[i + 1] == '\n'))
    i++;
  i += 2;
  while (i < hdr_len) {
    size_t start = i;
    size_t colon;
    if (acc[i] == '\r')
      break;
    while (i + 1 < hdr_len && !(acc[i] == '\r' && acc[i + 1] == '\n'))
      i++;
    colon = start;
    while (colon < i && acc[colon] != ':')
      colon++;
    if (colon < i && http_field_name_eq(acc, start, colon, name)) {
      unsigned long v = 0;
      size_t p = colon + 1;
      int digits = 0;
      if (found)
        return 0;
      while (p < i && (acc[p] == ' ' || acc[p] == '\t'))
        p++;
      if (p >= i || acc[p] < '0' || acc[p] > '9')
        return 0;
      while (p < i && acc[p] >= '0' && acc[p] <= '9') {
        v = v * 10UL + (unsigned long)(acc[p] - '0');
        p++;
        digits++;
        if (digits > 10 || v > (unsigned long)HE_BODY_MAX)
          return 0;
      }
      while (p < i && (acc[p] == ' ' || acc[p] == '\t'))
        p++;
      if (p != i)
        return 0;
      *out = (size_t)v;
      found = 1;
    }
    i += 2;
  }
  return found;
}

/* 1 = r is final (ok or err). 0 = retry. eof = connection closed. */
static int get_try_complete(GetSt *st, NetResult *r, int eof) {
  size_t hdr;
  int status;
  size_t clen = 0;
  int has_cl;
  size_t body_off;
  size_t body_got;
  if (!st->acc || st->total == 0) {
    if (eof) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: malformed response");
      return 1;
    }
    return 0;
  }
  hdr = http_hdr_len(st->acc, st->total);
  if (hdr == 0) {
    if (eof) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: malformed response");
      return 1;
    }
    return 0;
  }
  status = http_status_code(st->acc, hdr);
  if (status < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: malformed response");
    return 1;
  }
  if (status < 200 || status > 299) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: HTTP error");
    return 1;
  }
  if (http_header_present(st->acc, hdr, "Transfer-Encoding")) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.httpGet: chunked encoding unsupported");
    return 1;
  }
  has_cl = 0;
  if (http_header_present(st->acc, hdr, "Content-Length")) {
    if (!http_content_length(st->acc, hdr, &clen)) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: malformed response");
      return 1;
    }
    has_cl = 1;
  }
  body_off = hdr;
  body_got = st->total > body_off ? st->total - body_off : 0;
  if (has_cl) {
    if (body_got >= clen) {
      r->is_err = 0;
      r->as.ok = sz_string_from_bytes(st->acc + body_off, clen);
      return 1;
    }
    if (eof) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: malformed response");
      return 1;
    }
    return 0;
  }
  if (eof) {
    r->is_err = 0;
    r->as.ok = sz_string_from_bytes(st->acc + body_off, body_got);
    return 1;
  }
  return 0;
}

static void *get_read(void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)rc_box_zero(sizeof(NetResult));
  char buf[4096];
  ssize_t n;

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
    size_t add = (size_t)n;
    size_t need;
    if (st->total >= HE_BODY_MAX || add > HE_BODY_MAX - st->total) {
      r->is_err = 1;
      r->as.err = sz_error_new(6, "Net.httpGet: response too large");
      return r;
    }
    need = st->total + add + 1;
    if (need > st->acc_cap) {
      size_t cap = st->acc_cap ? st->acc_cap : 1;
      char *nacc;
      while (cap < need) {
        if (cap > SIZE_MAX / 2) {
          cap = need;
          break;
        }
        cap *= 2;
      }
      nacc = (char *)sz_alloc(cap);
      if (st->total)
        memcpy(nacc, st->acc, st->total);
      memcpy(nacc + st->total, buf, add);
      st->total += add;
      nacc[st->total] = '\0';
      sz_free(st->acc);
      st->acc = nacc;
      st->acc_cap = cap;
    } else {
      memcpy(st->acc + st->total, buf, add);
      st->total += add;
      st->acc[st->total] = '\0';
    }
    if (get_try_complete(st, r, 0))
      return r;
    r->retry = 1;
    return r;
  }
  if (get_try_complete(st, r, 1))
    return r;
  r->is_err = 1;
  r->as.err = sz_error_new(6, "Net.httpGet: malformed response");
  return r;
}

static SzIo *get_poll_write(void *value, void *env);
static SzIo *get_poll_read(void *value, void *env);

static SzIo *get_unwrap_write(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_release(r);
    return get_poll_write(NULL, st);
  }
  return unwrap_net(value, NULL);
}

static SzIo *get_unwrap_read(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_release(r);
    return get_poll_read(NULL, st);
  }
  return unwrap_net(value, NULL);
}

static SzIo *get_after_write_poll(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return fm_drop(sz_io_delay(get_check_write, st), get_unwrap_write, st);
}

static SzIo *handle_drop(SzIo *inner, SzErrorHandler handler, void *env) {
  SzIo *io = sz_io_handle_error_with(inner, handler, env);
  sz_release(inner);
  return io;
}

static SzIo *get_poll_write(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  SzIo *ready;
  (void)value;
  if (st->fd >= 0)
    ready = sz_io_poll_writable(st->fd);
  else if (st->he_wait4 && st->fd6 >= 0 && st->fd4 < 0) {
    int64_t delay = st->he_v4_at_ms - sz_clock_monotonic_ms_sync();
    if (delay < 1) {
      he_start_v4(st);
      if (st->fd4 >= 0 && st->fd6 >= 0)
        ready = race_drop(sz_io_poll_writable(st->fd4), sz_io_poll_writable(st->fd6));
      else if (st->fd6 >= 0)
        ready = sz_io_poll_writable(st->fd6);
      else
        ready = sz_io_poll_writable(st->fd4);
    } else
      ready = race_drop(sz_io_poll_writable(st->fd6), sz_io_sleep_ms(delay));
  } else if (st->fd4 >= 0 && st->fd6 >= 0)
    ready = race_drop(sz_io_poll_writable(st->fd4), sz_io_poll_writable(st->fd6));
  else if (st->fd6 >= 0)
    ready = sz_io_poll_writable(st->fd6);
  else
    ready = sz_io_poll_writable(st->fd4);
  {
    int64_t dl = st->tcp_up ? st->write_deadline_ms : st->connect_deadline_ms;
    int64_t left = dl - sz_clock_monotonic_ms_sync();
    if (left < 1)
      left = 1;
    ready = race_drop(ready, sz_io_sleep_ms(left));
  }
  return fm_drop(ready, get_after_write_poll, st);
}

static SzIo *get_after_read_poll(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return fm_drop(sz_io_delay(get_read, st), get_unwrap_read, st);
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
  ready = race_drop(sz_io_poll_readable(st->fd), sz_io_sleep_ms(left));
  return fm_drop(ready, get_after_read_poll, st);
}

static SzIo *get_finish(void *body, void *env) {
  get_free((GetSt *)env);
  return pure_drop(body);
}

static SzIo *get_poll_dns(void *value, void *env);

static SzIo *get_unwrap_dns(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_release(r);
    return get_poll_dns(NULL, st);
  }
  return unwrap_net(value, NULL);
}

static SzIo *get_after_dns_poll(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return fm_drop(sz_io_delay(get_dns_recv, st), get_unwrap_dns, st);
}

static SzIo *get_poll_dns(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  SzIo *ready;
  int64_t left;
  (void)value;
  left = st->dns_deadline_ms - sz_clock_monotonic_ms_sync();
  if (left < 1)
    left = 1;
  ready = race_drop(sz_io_poll_readable(st->dns_fd), sz_io_sleep_ms(left));
  return fm_drop(ready, get_after_dns_poll, st);
}

static SzIo *get_after_resolved(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  (void)value;
  return fm_drop(sz_io_delay(get_tcp_connect, st), unwrap_net, NULL);
}

static SzIo *get_after_start(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  NetResult *r = (NetResult *)value;
  if (!r || r->is_err)
    return unwrap_net(value, NULL);
  sz_release(r);
  if (st->dns_fd >= 0)
    return fm_drop(get_poll_dns(NULL, st), get_after_resolved, st);
  return get_after_resolved(NULL, st);
}

static SzIo *get_after_connect(void *value, void *env) {
  GetSt *st = (GetSt *)env;
  SzIo *io;
  (void)value;
  io = get_poll_write(NULL, st);
  io = fm_drop(io, get_poll_read, st);
  return fm_drop(io, get_finish, st);
}

static void *get_dispatch(void *env) {
  (void)env;
  return (void *)(intptr_t)(sz_testrt_net_is_fake() ? 1 : 0);
}

static SzIo *get_after_dispatch(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *url = (SzString *)pack->left;
  GetSt *st;
  SzIo *io;
  if ((intptr_t)value)
    return sz_testrt_net_http_get(url);
  sz_timeline_log_cstr("Net.httpGet", url ? sz_string_cstr(url) : "");
  st = (GetSt *)sz_rc_alloc(sizeof(GetSt), SZ_RC_BOX);
  memset(st, 0, sizeof(GetSt));
  sz_retain(url);
  st->url = url;
  st->fd = -1;
  st->fd4 = -1;
  st->fd6 = -1;
  st->dns_fd = -1;
  io = fm_drop(sz_io_delay(get_start, st), get_after_start, st);
  io = fm_drop(io, get_after_connect, st);
  {
    SzIo *fin = sz_io_delay(get_cleanup, st);
    SzIo *ens = sz_io_ensure(io, fin);
    sz_release(io);
    sz_release(fin);
    sz_release(st);
    return ens;
  }
}

SzIo *sz_net_http_get(SzString *url) {
  SzPair *pack;
  if (!url)
    sz_panic("sz_net_http_get(null)");
  pack = sz_pair_new(url, NULL);
  {
    SzIo *io = fm_drop(sz_io_delay(get_dispatch, NULL), get_after_dispatch, pack);
    sz_release(pack);
    return io;
  }
}

/* HTTP/1.0 GET server. Listen and connection fds are nonblocking. The fiber
 * parks on poll so other IO can run. Live listen is 127.0.0.1 and/or ::1
 * (V6ONLY). Bind succeeds when at least one family works so httpGet literals
 * on the bound loopback match. TestRuntime uses a per-port virtual mailbox.
 * Injected paths are ghost requests. Loopback httpGet parks on a Deferred.
 * Fake serve parks on an empty mailbox. Write completes that Deferred.
 * Request read and response write each wait at most 1000ms.
 * A timed-out, malformed, reset, or handler-failed client is dropped.
 * Persistent serve accepts the next. One request is one round that always
 * completes; the next round is built outside handleErrorWith. Error code 6.
 * serveOnce is one request. serve keeps the
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
  void *vreq_done; /* SzDeferred* for a virtual client; NULL for inject */
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

/* Close listen/conn fds and drop body/handler env. A second call is a no-op.
 * Cancel and success both run this through IO.ensure. */
static void serve_free(ServeSt *st) {
  if (!st)
    return;
  if (st->vreq_done) {
    SzError *err = sz_error_new(6, "Net.serve: cancelled");
    sz_deferred_fail_now((SzDeferred *)st->vreq_done, err);
    sz_release(err);
    sz_release(st->vreq_done);
    st->vreq_done = NULL;
  }
  sz_testrt_net_cancel_accept(st->port);
  sz_testrt_net_fail_mailbox(st->port, NULL);
  serve_close_fds(st);
  sz_release(st->body);
  st->body = NULL;
  sz_release(st->henv);
  st->henv = NULL;
}

static void *serve_cleanup(void *env) {
  serve_free((ServeSt *)env);
  return NULL;
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

static int serve_accept_wait(int err) {
  return err == EAGAIN || err == EWOULDBLOCK || err == ECONNABORTED ||
         err == EMFILE || err == ENFILE || err == EINTR;
}

static void *serve_ensure_listen(void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)rc_box_zero(sizeof(NetResult));
  int port;

  if (st->port < 1 || st->port > 65535) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: port must be 1..65535");
    return r;
  }
  if (sz_testrt_net_is_fake()) {
    r->is_err = 0;
    return r;
  }
  port = (int)st->port;
  if (st->listen_fd >= 0 || st->listen6_fd >= 0) {
    r->is_err = 0;
    return r;
  }
  {
    int fd4 = serve_bind_v4(port);
    int fd6 = serve_bind_v6(port);
    if (st->listen_fd >= 0)
      close(st->listen_fd);
    st->listen_fd = fd4;
    if (st->listen6_fd >= 0)
      close(st->listen6_fd);
    st->listen6_fd = fd6;
  }
  if (st->listen_fd < 0 && st->listen6_fd < 0) {
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
  NetResult *r = (NetResult *)rc_box_zero(sizeof(NetResult));
  int fd;
  int conn = -1;

  st->conn_fd = -1;
  st->rlen = 0;
  st->woff = 0;
  st->write_deadline_ms = 0;

  /* Chosen at step time so SCUZZ_TESTRT=1 install in runtime_main is visible. */
  if (sz_testrt_net_is_fake()) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: fake accept uses mailbox");
    return r;
  }
  fd = st->listen_fd;
  if (fd < 0 && st->listen6_fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(6, "Net.serve: not listening");
    return r;
  }
  conn = fd >= 0 ? accept(fd, NULL, NULL) : -1;
  if (conn < 0 && st->listen6_fd >= 0 &&
      (fd < 0 || serve_accept_wait(errno)))
    conn = accept(st->listen6_fd, NULL, NULL);
  if (conn < 0) {
    if (serve_accept_wait(errno)) {
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
  NetResult *r = (NetResult *)rc_box_zero(sizeof(NetResult));
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
  NetResult *r = (NetResult *)rc_box_zero(sizeof(NetResult));
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
    if (st->vreq_done) {
      void *payload = body;
      if (!payload)
        payload = sz_string_from_cstr("");
      sz_deferred_complete_now((SzDeferred *)st->vreq_done, payload);
      if (!body)
        sz_release(payload);
      sz_release(st->vreq_done);
      st->vreq_done = NULL;
    }
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

static SzIo *serve_poll_then_accept(void *value, void *env);
static SzIo *serve_poll_conn_read(void *value, void *env);
static SzIo *serve_poll_conn_write(void *value, void *env);
static SzIo *serve_after_path(void *path, void *env);

static SzIo *serve_unwrap_accept(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_release(r);
    return serve_poll_then_accept(NULL, st);
  }
  return unwrap_net(value, NULL);
}

/* Drop is success of this round. serveOnce still fails. Do not start a nested
 * server here: that would leave serve_after_path on the stack. */
static SzIo *serve_drop_conn(ServeSt *st, SzError *err) {
  serve_close_conn(st);
  if (st->vreq_done) {
    sz_deferred_fail_now((SzDeferred *)st->vreq_done, err);
    sz_release(st->vreq_done);
    st->vreq_done = NULL;
  }
  if (st->left == 1)
    return fail_drop(err);
  sz_error_free(err);
  return pure_drop(NULL);
}

static SzIo *serve_path_from_net(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzIo *io = unwrap_net(value, NULL);
  return fm_drop(io, serve_after_path, st);
}

static SzIo *serve_unwrap_read(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_release(r);
    return serve_poll_conn_read(NULL, st);
  }
  if (r && r->drop) {
    SzError *err = r->as.err;
    r->as.err = NULL;
    sz_release(r);
    return serve_drop_conn(st, err);
  }
  return serve_path_from_net(value, st);
}

static SzIo *serve_unwrap_write(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  NetResult *r = (NetResult *)value;
  if (r && r->retry) {
    sz_release(r);
    return serve_poll_conn_write(NULL, st);
  }
  if (r && r->drop) {
    SzError *err = r->as.err;
    r->as.err = NULL;
    sz_release(r);
    return serve_drop_conn(st, err);
  }
  return unwrap_net(value, NULL);
}

static SzIo *serve_after_accept_poll(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  return fm_drop(sz_io_delay(serve_accept, st), serve_unwrap_accept, st);
}

static SzIo *serve_poll_then_accept(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzIo *ready;
  (void)value;
  if (st->listen_fd >= 0 && st->listen6_fd >= 0)
    ready = race_drop(sz_io_poll_readable(st->listen_fd),
                       sz_io_poll_readable(st->listen6_fd));
  else if (st->listen6_fd >= 0)
    ready = sz_io_poll_readable(st->listen6_fd);
  else
    ready = sz_io_poll_readable(st->listen_fd);
  return fm_drop(ready, serve_after_accept_poll, st);
}

static SzIo *serve_after_conn_read_poll(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  return fm_drop(sz_io_delay(serve_read_req, st), serve_unwrap_read, st);
}

static SzIo *serve_poll_conn_read(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzIo *ready;
  int64_t left;
  (void)value;
  left = st->req_deadline_ms - sz_clock_monotonic_ms_sync();
  if (left < 1)
    left = 1;
  ready = race_drop(sz_io_poll_readable(st->conn_fd), sz_io_sleep_ms(left));
  return fm_drop(ready, serve_after_conn_read_poll, st);
}

static SzIo *serve_after_conn_write_poll(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  return fm_drop(sz_io_delay(serve_write_close, st), serve_unwrap_write, st);
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
  ready = race_drop(sz_io_poll_writable(st->conn_fd), sz_io_sleep_ms(left));
  return fm_drop(ready, serve_after_conn_write_poll, st);
}

static SzIo *serve_after_vreq(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzPair *p = (SzPair *)value;
  void *path;
  if (!p)
    return sz_io_fail_cstr("Net.serve: null request");
  path = p->left;
  st->vreq_done = p->right;
  sz_retain(path);
  sz_retain(st->vreq_done);
  sz_release(p);
  return serve_after_path(path, st);
}

static SzIo *serve_after_listen(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzIo *io;
  (void)value;
  if (sz_testrt_net_is_fake())
    return fm_drop(sz_testrt_net_accept(st->port), serve_after_vreq, st);
  io = serve_poll_then_accept(NULL, st);
  return fm_drop(io, serve_poll_conn_read, st);
}

static SzIo *serve_after_write(void *value, void *env) {
  ServeSt *st = (ServeSt *)env;
  (void)value;
  if (st->left > 0)
    st->left--;
  return pure_drop(NULL);
}

static SzIo *serve_after_body(void *body, void *env) {
  ServeSt *st = (ServeSt *)env;
  sz_release(st->body);
  st->body = body;
  st->woff = 0;
  if (sz_testrt_net_is_fake())
    return fm_drop(sz_io_delay(serve_write_close, st), unwrap_net, NULL);
  return serve_poll_conn_write(NULL, st);
}

static SzIo *serve_on_handler_err(SzError *err, void *env) {
  return serve_drop_conn((ServeSt *)env, err);
}

static SzIo *serve_after_path(void *path, void *env) {
  ServeSt *st = (ServeSt *)env;
  SzIo *io;
  if (!path)
    return pure_drop(NULL);
  io = st->handler(path, st->henv);
  sz_release(path);
  io = fm_drop(io, serve_after_body, st);
  io = fm_drop(io, serve_after_write, st);
  return handle_drop(io, serve_on_handler_err, st);
}

static SzIo *serve_one(ServeSt *st) {
  SzIo *prog = fm_drop(sz_io_delay(serve_ensure_listen, st), unwrap_net, NULL);
  return fm_drop(prog, serve_after_listen, st);
}

static int serve_should_stop(ServeSt *st) {
  if (st->left == 0)
    return 1;
  if (st->left < 0 && sz_testrt_net_is_fake() &&
      sz_testrt_net_serve_pending_port(st->port) <= 0)
    return 1;
  return 0;
}

static SzIo *serve_again(void *value, void *env);

static SzIo *serve_loop(ServeSt *st) {
  if (serve_should_stop(st)) {
    serve_free(st);
    return pure_drop(NULL);
  }
  return fm_drop(serve_one(st), serve_again, st);
}

static SzIo *serve_again(void *value, void *env) {
  (void)value;
  return serve_loop((ServeSt *)env);
}

typedef struct ServeSpec {
  int64_t port;
  int64_t n;
  SzCont handler;
} ServeSpec;

static SzIo *serve_after_kick(void *ignored, void *env) {
  SzPair *pack = (SzPair *)env;
  ServeSpec *spec = pack ? (ServeSpec *)pack->right : NULL;
  ServeSt *st = (ServeSt *)sz_rc_alloc(sizeof(ServeSt), SZ_RC_BOX);
  memset(st, 0, sizeof(ServeSt));
  (void)ignored;
  if (!spec)
    sz_panic("sz_net_serve(null spec)");
  st->port = spec->port;
  st->left = spec->n > 0 ? spec->n : -1;
  st->listen_fd = -1;
  st->listen6_fd = -1;
  st->conn_fd = -1;
  st->handler = spec->handler;
  sz_retain(pack->left);
  st->henv = pack->left;
  {
    SzIo *io = serve_loop(st);
    SzIo *fin = sz_io_delay(serve_cleanup, st);
    SzIo *ens = sz_io_ensure(io, fin);
    sz_release(io);
    sz_release(fin);
    sz_release(st);
    return ens;
  }
}

static SzIo *net_serve_n(int64_t port, int64_t n, SzCont handler, void *env) {
  ServeSpec *spec;
  SzPair *pack;
  if (!handler)
    sz_panic("sz_net_serve(null handler)");
  spec = (ServeSpec *)sz_rc_alloc(sizeof(ServeSpec), SZ_RC_BOX);
  spec->port = port;
  spec->n = n;
  spec->handler = handler;
  pack = sz_pair_new(env, spec);
  sz_release(spec);
  {
    SzIo *io = fm_drop(sz_io_pure(NULL), serve_after_kick, pack);
    sz_release(pack);
    return io;
  }
}

SzIo *sz_net_serve(int64_t port, SzCont handler, void *env) {
  return net_serve_n(port, 0, handler, env);
}

SzIo *sz_net_serve_once(int64_t port, SzCont handler, void *env) {
  return net_serve_n(port, 1, handler, env);
}
