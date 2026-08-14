#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static void sleep_us(long us) {
  struct timespec ts;
  if (us <= 0)
    return;
  ts.tv_sec = us / 1000000L;
  ts.tv_nsec = (us % 1000000L) * 1000L;
  nanosleep(&ts, NULL);
}

static uint16_t test_rd16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint16_t dns_qtype_of(const uint8_t *buf, ssize_t n) {
  size_t o = 12;
  if (n < 16)
    return 0;
  while (o < (size_t)n) {
    uint8_t len = buf[o];
    if ((len & 0xC0) != 0)
      return 0;
    if (len == 0) {
      if (o + 3 > (size_t)n)
        return 0;
      return test_rd16(buf + o + 1);
    }
    o += 1 + (size_t)len;
  }
  return 0;
}

static void dns_set_qr(uint8_t *buf, uint16_t ancount) {
  buf[2] = (uint8_t)(buf[2] | 0x80);
  buf[3] = 0x80;
  buf[6] = (uint8_t)(ancount >> 8);
  buf[7] = (uint8_t)ancount;
}

static int delay_calls = 0;
static void *delay_inc(void *env) {
  (void)env;
  delay_calls++;
  return (void *)(intptr_t)42;
}

static void *take_hit(void *env) {
  delay_calls++;
  return sz_string_from_cstr((const char *)env);
}

static SzIo *cont_println(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_io_println_cstr("after-flatmap");
}

static int lang_released = 0;
static SzIo *lang_release(void *acquired, void *env) {
  (void)env;
  assert(acquired);
  lang_released = 1;
  return sz_io_pure(NULL);
}
static SzIo *lang_use_ok(void *acquired, void *env) {
  (void)env;
  (void)acquired;
  return sz_io_println_cstr("lang-resource-used");
}
static SzIo *lang_use_fail(void *acquired, void *env) {
  (void)env;
  (void)acquired;
  return sz_io_fail_cstr("lang-use-failed");
}

static int ensured_flag = 0;
static void *ensure_mark_thunk(void *env) {
  (void)env;
  ensured_flag = 1;
  return NULL;
}

static SzIo *lang_use_sleep(void *acquired, void *env) {
  (void)env;
  (void)acquired;
  return sz_io_sleep_ms(100);
}

static SzIo *stream_bang(void *v, void *env) {
  (void)env;
  SzString *s = (SzString *)v;
  return sz_io_pure(sz_string_concat(s, sz_string_from_cstr("!")));
}

static int64_t stream_nonempty(void *v, void *env) {
  (void)env;
  return sz_string_len((SzString *)v) > 0;
}

static void *stream_bang_sync(void *v, void *env) {
  (void)env;
  return sz_string_concat((SzString *)v, sz_string_from_cstr("!"));
}

static int64_t stream_empty(void *v, void *env) {
  (void)env;
  return sz_string_len((SzString *)v) == 0;
}

static SzIo *serve_path_ok(void *path, void *env) {
  (void)env;
  return sz_io_pure(
      sz_string_concat(sz_string_from_cstr("ok:"), (SzString *)path));
}

static int g_serve_fail_n;

static SzIo *serve_fail_then_ok(void *path, void *env) {
  (void)env;
  if (g_serve_fail_n++ == 0)
    return sz_io_fail_cstr("handler boom");
  return sz_io_pure(
      sz_string_concat(sz_string_from_cstr("ok:"), (SzString *)path));
}

static SzIo *serve_big_ok(void *path, void *env) {
  enum { N = 4 * 1024 * 1024 };
  char *blob;
  (void)path;
  (void)env;
  blob = (char *)malloc(N);
  assert(blob);
  memset(blob, 'x', N);
  {
    SzString *s = sz_string_from_bytes(blob, N);
    free(blob);
    return sz_io_pure(s);
  }
}

static SzIo *serve_padded_ok(void *path, void *env) {
  enum { N = 64 * 1024 };
  char *blob;
  SzString *s;
  (void)path;
  (void)env;
  blob = (char *)malloc(N);
  assert(blob);
  memset(blob, 'x', N);
  memcpy(blob, "ok:/x", 5);
  s = sz_string_from_bytes(blob, N);
  free(blob);
  return sz_io_pure(s);
}

static void *live_get_client(void *arg) {
  int port = *(int *)arg;
  int fd = -1;
  int i;
  struct sockaddr_in addr;
  char req[128];
  static char buf[1024];
  ssize_t n;
  size_t total = 0;
  memset(buf, 0, sizeof buf);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  snprintf(req, sizeof req,
           "GET /x HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
  for (i = 0; i < 50; i++) {
    sleep_us(10000);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      continue;
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0)
      break;
    close(fd);
    fd = -1;
  }
  if (fd < 0)
    return NULL;
  if (write(fd, req, strlen(req)) < 0) {
    close(fd);
    return NULL;
  }
  while (total + 1 < sizeof buf &&
         (n = read(fd, buf + total, sizeof buf - 1 - total)) > 0)
    total += (size_t)n;
  close(fd);
  return buf;
}

static void *two_live_gets(void *arg) {
  live_get_client(arg);
  return live_get_client(arg);
}

static void *delayed_live_get(void *arg) {
  sleep_us(1200000);
  return live_get_client(arg);
}

static void *connect_hold(void *arg) {
  int port = *(int *)arg;
  int fd = -1;
  int i;
  struct sockaddr_in addr;
  char buf[8];
  ssize_t n;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  for (i = 0; i < 50; i++) {
    sleep_us(10000);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      continue;
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0)
      break;
    close(fd);
    fd = -1;
  }
  if (fd < 0)
    return NULL;
  while ((n = read(fd, buf, sizeof buf)) > 0)
    ;
  close(fd);
  return (void *)1;
}

static void *get_then_hold(void *arg) {
  int port = *(int *)arg;
  int fd = -1;
  int i;
  int rcv = 2048;
  struct sockaddr_in addr;
  char req[128];
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  snprintf(req, sizeof req,
           "GET /x HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
  for (i = 0; i < 50; i++) {
    sleep_us(10000);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      continue;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof rcv);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0)
      break;
    close(fd);
    fd = -1;
  }
  if (fd < 0)
    return NULL;
  if (write(fd, req, strlen(req)) < 0) {
    close(fd);
    return NULL;
  }
  sleep_us(2500000);
  close(fd);
  return (void *)1;
}

static void *get_then_rst(void *arg) {
  int port = *(int *)arg;
  int fd = -1;
  int i;
  struct sockaddr_in addr;
  struct linger lin;
  char req[128];
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  snprintf(req, sizeof req,
           "GET /x HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
  lin.l_onoff = 1;
  lin.l_linger = 0;
  for (i = 0; i < 50; i++) {
    sleep_us(10000);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      continue;
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0)
      break;
    close(fd);
    fd = -1;
  }
  if (fd < 0)
    return NULL;
  setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof lin);
  if (write(fd, req, strlen(req)) < 0) {
    close(fd);
    return NULL;
  }
  close(fd);
  return (void *)1;
}

static void *connect_close(void *arg) {
  int port = *(int *)arg;
  int fd = -1;
  int i;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  for (i = 0; i < 50; i++) {
    sleep_us(10000);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      continue;
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0)
      break;
    close(fd);
    fd = -1;
  }
  if (fd >= 0)
    close(fd);
  return (void *)1;
}

static void *soon_live_get(void *arg) {
  sleep_us(80000);
  return live_get_client(arg);
}

static void *ipv6_http_once(void *arg) {
  int port = *(int *)arg;
  int fd, cfd;
  int one = 1;
  struct sockaddr_in6 addr;
  char buf[512];
  const char *resp = "HTTP/1.0 200 OK\r\n\r\nok:/x";
  ssize_t n;
  fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0)
    return NULL;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  memset(&addr, 0, sizeof addr);
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons((uint16_t)port);
  if (inet_pton(AF_INET6, "::1", &addr.sin6_addr) != 1) {
    close(fd);
    return NULL;
  }
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 1) != 0) {
    close(fd);
    return NULL;
  }
  cfd = accept(fd, NULL, NULL);
  close(fd);
  if (cfd < 0)
    return NULL;
  n = read(cfd, buf, sizeof buf);
  (void)n;
  if (write(cfd, resp, strlen(resp)) < 0) {
    close(cfd);
    return NULL;
  }
  close(cfd);
  return (void *)1;
}

static void *ipv4_http_once(void *arg) {
  int port = *(int *)arg;
  int fd, cfd;
  int one = 1;
  struct sockaddr_in addr;
  char buf[512];
  const char *resp = "HTTP/1.0 200 OK\r\n\r\nok:/x";
  ssize_t n;
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return NULL;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 1) != 0) {
    close(fd);
    return NULL;
  }
  cfd = accept(fd, NULL, NULL);
  close(fd);
  if (cfd < 0)
    return NULL;
  n = read(cfd, buf, sizeof buf);
  (void)n;
  if (write(cfd, resp, strlen(resp)) < 0) {
    close(cfd);
    return NULL;
  }
  close(cfd);
  return (void *)1;
}

static void *ipv4_http_hold(void *arg) {
  int port = *(int *)arg;
  int fd, cfd;
  int one = 1;
  struct sockaddr_in addr;
  char buf[512];
  ssize_t n;
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return NULL;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 1) != 0) {
    close(fd);
    return NULL;
  }
  cfd = accept(fd, NULL, NULL);
  close(fd);
  if (cfd < 0)
    return NULL;
  n = read(cfd, buf, sizeof buf);
  (void)n;
  while ((n = read(cfd, buf, sizeof buf)) > 0)
    ;
  close(cfd);
  return (void *)1;
}

static volatile int g_peer_flag;

static SzIo *assert_peer_quiet(void *value, void *env) {
  (void)value;
  (void)env;
  assert(g_peer_flag == 0);
  return sz_io_pure(NULL);
}

static void *pipe_late_write(void *arg) {
  int fd = *(int *)arg;
  sleep_us(40000);
  g_peer_flag = 1;
  if (write(fd, "x", 1) < 0)
    return NULL;
  return NULL;
}

static void *live_get_client_late(void *arg) {
  sleep_us(50000);
  g_peer_flag = 1;
  return live_get_client(arg);
}

static SzIo *after_sleep_http(void *value, void *env) {
  (void)value;
  return sz_net_http_get((SzString *)env);
}

static SzIo *after_sleep_dns_http(void *value, void *env) {
  (void)value;
  return sz_io_both(
      sz_net_http_get((SzString *)env),
      sz_io_flatmap(sz_io_println_cstr("peer"), assert_peer_quiet, NULL));
}

static SzIo *exec_then_flag(void *value, void *env) {
  (void)env;
  g_peer_flag = 1;
  return sz_io_pure(value);
}

static void *dns_late_a(void *arg) {
  int fd = *(int *)arg;
  uint8_t buf[512];
  uint8_t ans[16];
  struct sockaddr_in from;
  socklen_t flen;
  ssize_t n;
  int i;
  memset(ans, 0, sizeof ans);
  ans[0] = 0xC0;
  ans[1] = 0x0C;
  ans[3] = 1;
  ans[5] = 1;
  ans[9] = 60;
  ans[11] = 4;
  ans[12] = 127;
  ans[15] = 1;
  for (i = 0; i < 2; i++) {
    flen = sizeof from;
    n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
    sleep_us(40000);
    if (i == 0)
      g_peer_flag = 1;
    if (n < 12)
      return NULL;
    if (dns_qtype_of(buf, n) == 1) {
      if ((size_t)n + 16 > sizeof buf)
        return NULL;
      dns_set_qr(buf, 1);
      memcpy(buf + n, ans, 16);
      if (sendto(fd, buf, (size_t)n + 16, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
    } else {
      dns_set_qr(buf, 0);
      if (sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
    }
  }
  return NULL;
}

static void *dns_late_cname(void *arg) {
  int fd = *(int *)arg;
  uint8_t buf[512];
  uint8_t cname[26];
  uint8_t ans[16];
  struct sockaddr_in from;
  socklen_t flen;
  ssize_t n;
  int i;
  memset(cname, 0, sizeof cname);
  cname[0] = 0xC0;
  cname[1] = 0x0C;
  cname[3] = 5;
  cname[5] = 1;
  cname[9] = 60;
  cname[11] = 14;
  cname[12] = 1;
  cname[13] = 'a';
  cname[14] = 5;
  memcpy(cname + 15, "scuzz", 5);
  cname[20] = 4;
  memcpy(cname + 21, "test", 4);
  memset(ans, 0, sizeof ans);
  ans[0] = 0xC0;
  ans[1] = 0x0C;
  ans[3] = 1;
  ans[5] = 1;
  ans[9] = 60;
  ans[11] = 4;
  ans[12] = 127;
  ans[15] = 1;
  for (i = 0; i < 4; i++) {
    flen = sizeof from;
    n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
    sleep_us(40000);
    if (i == 0)
      g_peer_flag = 1;
    if (n < 12)
      return NULL;
    if (n > 13 && buf[12] == 1 && buf[13] == 'a') {
      if (dns_qtype_of(buf, n) == 1) {
        if ((size_t)n + 16 > sizeof buf)
          return NULL;
        dns_set_qr(buf, 1);
        memcpy(buf + n, ans, 16);
        if (sendto(fd, buf, (size_t)n + 16, 0, (struct sockaddr *)&from, flen) < 0)
          return NULL;
      } else {
        dns_set_qr(buf, 0);
        if (sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&from, flen) < 0)
          return NULL;
      }
    } else {
      if ((size_t)n + 26 > sizeof buf)
        return NULL;
      dns_set_qr(buf, 1);
      memcpy(buf + n, cname, 26);
      if (sendto(fd, buf, (size_t)n + 26, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
    }
  }
  return NULL;
}

static void *dns_late_aaaa(void *arg) {
  int fd = *(int *)arg;
  uint8_t buf[512];
  uint8_t ans[28];
  struct sockaddr_in from;
  socklen_t flen;
  ssize_t n;
  int i;
  memset(ans, 0, sizeof ans);
  ans[0] = 0xC0;
  ans[1] = 0x0C;
  ans[3] = 28;
  ans[5] = 1;
  ans[9] = 60;
  ans[11] = 16;
  ans[27] = 1;
  for (i = 0; i < 2; i++) {
    flen = sizeof from;
    n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
    sleep_us(40000);
    if (i == 0)
      g_peer_flag = 1;
    if (n < 12)
      return NULL;
    if (dns_qtype_of(buf, n) == 28) {
      if ((size_t)n + 28 > sizeof buf)
        return NULL;
      dns_set_qr(buf, 1);
      memcpy(buf + n, ans, 28);
      if (sendto(fd, buf, (size_t)n + 28, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
    } else {
      dns_set_qr(buf, 0);
      if (sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
    }
  }
  return NULL;
}

static void *dns_he_dead_a(void *arg) {
  int fd = *(int *)arg;
  uint8_t buf[512];
  uint8_t a[16];
  uint8_t aaaa[28];
  struct sockaddr_in from;
  socklen_t flen;
  ssize_t n;
  int i;
  memset(a, 0, sizeof a);
  a[0] = 0xC0;
  a[1] = 0x0C;
  a[3] = 1;
  a[5] = 1;
  a[9] = 60;
  a[11] = 4;
  a[12] = 192;
  a[14] = 2;
  a[15] = 1;
  memset(aaaa, 0, sizeof aaaa);
  aaaa[0] = 0xC0;
  aaaa[1] = 0x0C;
  aaaa[3] = 28;
  aaaa[5] = 1;
  aaaa[9] = 60;
  aaaa[11] = 16;
  aaaa[27] = 1;
  for (i = 0; i < 2; i++) {
    flen = sizeof from;
    n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
    sleep_us(40000);
    if (n < 12)
      return NULL;
    if (dns_qtype_of(buf, n) == 1) {
      if ((size_t)n + 16 > sizeof buf)
        return NULL;
      dns_set_qr(buf, 1);
      memcpy(buf + n, a, 16);
      if (sendto(fd, buf, (size_t)n + 16, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
    } else {
      if ((size_t)n + 28 > sizeof buf)
        return NULL;
      dns_set_qr(buf, 1);
      memcpy(buf + n, aaaa, 28);
      if (sendto(fd, buf, (size_t)n + 28, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
    }
  }
  return NULL;
}

static void *dns_he_v4_loopback(void *arg) {
  int fd = *(int *)arg;
  uint8_t buf[512];
  uint8_t a[16];
  uint8_t aaaa[28];
  struct sockaddr_in from;
  socklen_t flen;
  ssize_t n;
  int i;
  memset(a, 0, sizeof a);
  a[0] = 0xC0;
  a[1] = 0x0C;
  a[3] = 1;
  a[5] = 1;
  a[9] = 60;
  a[11] = 4;
  a[12] = 127;
  a[15] = 1;
  memset(aaaa, 0, sizeof aaaa);
  aaaa[0] = 0xC0;
  aaaa[1] = 0x0C;
  aaaa[3] = 28;
  aaaa[5] = 1;
  aaaa[9] = 60;
  aaaa[11] = 16;
  aaaa[27] = 1;
  for (i = 0; i < 2; i++) {
    flen = sizeof from;
    n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
    sleep_us(40000);
    if (n < 12)
      return NULL;
    if (dns_qtype_of(buf, n) == 1) {
      if ((size_t)n + 16 > sizeof buf)
        return NULL;
      dns_set_qr(buf, 1);
      memcpy(buf + n, a, 16);
      if (sendto(fd, buf, (size_t)n + 16, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
    } else {
      if ((size_t)n + 28 > sizeof buf)
        return NULL;
      dns_set_qr(buf, 1);
      memcpy(buf + n, aaaa, 28);
      if (sendto(fd, buf, (size_t)n + 28, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
    }
  }
  return NULL;
}

static void *stdin_late_write(void *arg) {
  int fd = *(int *)arg;
  sleep_us(40000);
  g_peer_flag = 1;
  if (write(fd, "hello\n", 6) < 0)
    return NULL;
  return NULL;
}

static SzIo *recover_boom(SzError *err, void *env) {
  (void)env;
  assert(err && strstr(sz_string_cstr(err->message), "boom"));
  sz_error_free(err);
  return sz_io_println_cstr("recovered");
}

static SzIo *recover_unit(SzError *err, void *env) {
  (void)env;
  assert(err && strstr(sz_string_cstr(err->message), "interrupted"));
  sz_error_free(err);
  return sz_io_pure(NULL);
}

static SzIo *after_ref_get(void *value, void *env) {
  (void)env;
  SzString *s = (SzString *)value;
  assert(s && strcmp(sz_string_cstr(s), "b") == 0);
  return sz_io_pure(NULL);
}

static SzIo *after_ref_set(void *value, void *env) {
  (void)value;
  SzRef *r = (SzRef *)env;
  return sz_io_flatmap(sz_ref_get(r), after_ref_get, NULL);
}

static SzIo *after_ref(void *value, void *env) {
  (void)env;
  SzRef *r = (SzRef *)value;
  return sz_io_flatmap(sz_ref_set_cstr(r, "b"), after_ref_set, r);
}

static SzIo *fiber_join_cont(void *fiber, void *env) {
  (void)env;
  return sz_fiber_join(fiber);
}

static SzIo *after_fork_ignore(void *fiber, void *env) {
  (void)fiber;
  (void)env;
  return sz_io_pure(NULL);
}

static SzIo *fiber_join_recover(void *ignored, void *fiber) {
  (void)ignored;
  return sz_io_handle_error_with(sz_fiber_join(fiber), recover_unit, NULL);
}

static SzIo *fiber_interrupt_then_join(void *fiber, void *env) {
  (void)env;
  return sz_io_flatmap(sz_fiber_interrupt(fiber), fiber_join_recover, fiber);
}

static int retry_hits = 0;
static void *retry_count(void *env) {
  (void)env;
  retry_hits++;
  return (void *)(intptr_t)retry_hits;
}

static SzIo *retry_until_3(void *value, void *env) {
  (void)env;
  if ((intptr_t)value < 3)
    return sz_io_fail_cstr("not-yet");
  return sz_io_pure(value);
}

static SzIo *always_fail_cont(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_io_fail_cstr("always");
}

static SzIo *after_sleep_tag(void *value, void *env) {
  (void)value;
  return sz_io_pure(env);
}

int main(void) {
  /* delay */
  delay_calls = 0;
  SzIo *d = sz_io_delay(delay_inc, NULL);
  SzIoResult r = sz_io_unsafe_run(d);
  assert(r.ok);
  assert((intptr_t)r.value == 42);
  assert(delay_calls == 1);

  /* println + flatMap */
  SzIo *prog =
      sz_io_flatmap(sz_io_println_cstr("hello"), cont_println, NULL);
  r = sz_io_unsafe_run(prog);
  assert(r.ok);

  /* fail */
  r = sz_io_unsafe_run(sz_io_fail(sz_error_new(9, "boom")));
  assert(!r.ok);
  assert(r.error && strstr(sz_string_cstr(r.error->message), "boom"));
  sz_error_free(r.error);

  /* handleErrorWith */
  r = sz_io_unsafe_run(sz_io_handle_error_with(sz_io_fail_cstr("boom"),
                                               recover_boom, NULL));
  assert(r.ok);

  /* attempt */
  r = sz_io_unsafe_run(sz_io_attempt(sz_io_fail_cstr("nope")));
  assert(r.ok);
  {
    SzEither *e = (SzEither *)r.value;
    assert(e && !e->is_right);
    assert(e->as.left && strstr(sz_string_cstr(e->as.left->message), "nope"));
    sz_either_free(e);
  }
  r = sz_io_unsafe_run(sz_io_attempt(sz_io_pure((void *)(intptr_t)3)));
  assert(r.ok);
  {
    SzEither *e = (SzEither *)r.value;
    assert(e && e->is_right && (intptr_t)e->as.right == 3);
    sz_either_free(e);
  }

  /* Resource.make / use (IO acquire + IO release) */
  lang_released = 0;
  SzLangResource *lr = sz_lang_resource_make(
      sz_io_pure(sz_string_from_cstr("tok")), lang_release, NULL);
  r = sz_io_unsafe_run(sz_lang_resource_use(lr, lang_use_ok, NULL));
  assert(r.ok);
  assert(lang_released == 1);
  sz_lang_resource_free(lr);

  lang_released = 0;
  lr = sz_lang_resource_make(sz_io_pure(sz_string_from_cstr("tok")),
                             lang_release, NULL);
  r = sz_io_unsafe_run(sz_lang_resource_use(lr, lang_use_fail, NULL));
  assert(!r.ok);
  assert(lang_released == 1);
  sz_error_free(r.error);
  sz_lang_resource_free(lr);

  /* IO.ensure runs finalizer on success */
  ensured_flag = 0;
  r = sz_io_unsafe_run(
      sz_io_ensure(sz_io_pure((void *)(intptr_t)1),
                   sz_io_delay(ensure_mark_thunk, NULL)));
  assert(r.ok);
  assert((intptr_t)r.value == 1);
  assert(ensured_flag == 1);

  /* IO.ensure runs finalizer on failure */
  ensured_flag = 0;
  r = sz_io_unsafe_run(
      sz_io_ensure(sz_io_fail_cstr("boom"), sz_io_delay(ensure_mark_thunk, NULL)));
  assert(!r.ok);
  assert(ensured_flag == 1);
  sz_error_free(r.error);

  /* Resource releases when cancelled as race loser (TestRuntime: both park, then short wins). */
  {
    sz_testrt_install();
    lang_released = 0;
    lr = sz_lang_resource_make(sz_io_pure(sz_string_from_cstr("tok")),
                               lang_release, NULL);
    r = sz_io_unsafe_run(
        sz_io_race(sz_lang_resource_use(lr, lang_use_sleep, NULL),
                   sz_io_sleep_ms(1)));
    assert(r.ok);
    assert(lang_released == 1);
    sz_lang_resource_free(lr);
    sz_testrt_reset();
  }

  /* IO.timeout: inner wins and keeps its value. */
  {
    sz_testrt_install();
    r = sz_io_unsafe_run(sz_io_timeout(50, sz_io_pure((void *)(intptr_t)7)));
    assert(r.ok);
    assert((intptr_t)r.value == 7);
    sz_testrt_reset();
  }

  /* IO.timeout: inner failure is not rewritten as timeout. */
  {
    sz_testrt_install();
    r = sz_io_unsafe_run(sz_io_timeout(50, sz_io_fail_cstr("boom")));
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "boom") != NULL);
    sz_error_free(r.error);
    sz_testrt_reset();
  }

  /* IO.timeout: sleep-fail wins; Resource release still runs. */
  {
    sz_testrt_install();
    lang_released = 0;
    lr = sz_lang_resource_make(sz_io_pure(sz_string_from_cstr("tok")),
                               lang_release, NULL);
    r = sz_io_unsafe_run(
        sz_io_timeout(1, sz_lang_resource_use(lr, lang_use_sleep, NULL)));
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "timeout") != NULL);
    assert(lang_released == 1);
    sz_error_free(r.error);
    sz_lang_resource_free(lr);
    sz_testrt_reset();
  }

  /* IO.repeatN: n extra runs after the first; last value wins. */
  delay_calls = 0;
  r = sz_io_unsafe_run(sz_io_repeat_n(2, sz_io_delay(delay_inc, NULL)));
  assert(r.ok);
  assert((intptr_t)r.value == 42);
  assert(delay_calls == 3);

  delay_calls = 0;
  r = sz_io_unsafe_run(sz_io_repeat_n(0, sz_io_delay(delay_inc, NULL)));
  assert(r.ok);
  assert(delay_calls == 1);

  delay_calls = 0;
  r = sz_io_unsafe_run(sz_io_repeat_n(-3, sz_io_delay(delay_inc, NULL)));
  assert(r.ok);
  assert(delay_calls == 1);

  delay_calls = 0;
  r = sz_io_unsafe_run(sz_io_repeat_n(5, sz_io_fail_cstr("boom")));
  assert(!r.ok);
  assert(delay_calls == 0);
  assert(r.error && strstr(sz_string_cstr(r.error->message), "boom") != NULL);
  sz_error_free(r.error);

  /* IO.retryN: extra retries on failure; last success / last error. */
  retry_hits = 0;
  r = sz_io_unsafe_run(sz_io_retry_n(
      5, sz_io_flatmap(sz_io_delay(retry_count, NULL), retry_until_3, NULL)));
  assert(r.ok);
  assert((intptr_t)r.value == 3);
  assert(retry_hits == 3);

  retry_hits = 0;
  r = sz_io_unsafe_run(sz_io_retry_n(
      1, sz_io_flatmap(sz_io_delay(retry_count, NULL), always_fail_cont, NULL)));
  assert(!r.ok);
  assert(retry_hits == 2);
  assert(r.error && strstr(sz_string_cstr(r.error->message), "always") != NULL);
  sz_error_free(r.error);

  retry_hits = 0;
  r = sz_io_unsafe_run(sz_io_retry_n(
      0, sz_io_flatmap(sz_io_delay(retry_count, NULL), always_fail_cont, NULL)));
  assert(!r.ok);
  assert(retry_hits == 1);
  sz_error_free(r.error);

  retry_hits = 0;
  r = sz_io_unsafe_run(sz_io_retry_n(2, sz_io_pure((void *)(intptr_t)9)));
  assert(r.ok);
  assert((intptr_t)r.value == 9);
  assert(retry_hits == 0);

  /* IO.forever: race loser when a sibling sleeper wins (TestRuntime). */
  {
    sz_testrt_install();
    r = sz_io_unsafe_run(
        sz_io_race(sz_io_forever(sz_io_sleep_ms(1)), sz_io_sleep_ms(5)));
    assert(r.ok);
    sz_testrt_reset();
  }

  /* IO.forever cancelled through Fiber.interrupt. Resource release still runs. */
  {
    sz_testrt_install();
    lang_released = 0;
    lr = sz_lang_resource_make(sz_io_pure(sz_string_from_cstr("tok")),
                               lang_release, NULL);
    r = sz_io_unsafe_run(sz_io_flatmap(
        sz_fiber_fork(sz_io_forever(
            sz_lang_resource_use(lr, lang_use_sleep, NULL))),
        fiber_interrupt_then_join, NULL));
    assert(r.ok);
    assert(lang_released == 1);
    sz_lang_resource_free(lr);
    sz_testrt_reset();
  }

  /* Fiber.fork/join: child value is the join result. */
  {
    SzIo *child = sz_io_pure((void *)(intptr_t)42);
    r = sz_io_unsafe_run(
        sz_io_flatmap(sz_fiber_fork(child), fiber_join_cont, NULL));
    assert(r.ok);
    assert((intptr_t)r.value == 42);
  }

  /* Fiber.interrupt cancels a sleeper; join fails; Resource release runs. */
  {
    sz_testrt_install();
    lang_released = 0;
    lr = sz_lang_resource_make(sz_io_pure(sz_string_from_cstr("tok")),
                               lang_release, NULL);
    r = sz_io_unsafe_run(sz_io_flatmap(
        sz_fiber_fork(sz_lang_resource_use(lr, lang_use_sleep, NULL)),
        fiber_interrupt_then_join, NULL));
    assert(r.ok);
    assert(lang_released == 1);
    sz_lang_resource_free(lr);
    sz_testrt_reset();
  }

  /* Unjoined fork is cancelled when the root completes (no long sleep). */
  {
    int64_t t0 = sz_clock_monotonic_ms_sync();
    int64_t t1;
    r = sz_io_unsafe_run(
        sz_io_flatmap(sz_fiber_fork(sz_io_sleep_ms(300)), after_fork_ignore,
                      NULL));
    t1 = sz_clock_monotonic_ms_sync();
    assert(r.ok);
    assert(t1 - t0 < 80);
  }

  /* Ref */
  r = sz_io_unsafe_run(sz_io_flatmap(sz_ref_of_cstr("a"), after_ref, NULL));
  assert(r.ok);

  /* Deferred */
  {
    SzDeferred *def = sz_deferred_make();
    r = sz_io_unsafe_run(sz_deferred_complete_cstr(def, "ok"));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_deferred_get(def));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "ok") == 0);
    sz_deferred_free(def);
  }

  /* Queue */
  {
    SzQueue *q = sz_queue_make();
    r = sz_io_unsafe_run(sz_queue_offer_cstr(q, "x"));
    assert(r.ok);
    assert(sz_queue_size(q) == 1);
    r = sz_io_unsafe_run(sz_queue_take(q));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "x") == 0);
    sz_queue_free(q);
  }

  /* Stream — emit / eval / concat / evalMap / map / take / drop / filter / compileToList / drain */
  {
    SzList *xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    SzStream *s = sz_stream_concat(
        sz_stream_evalmap(sz_stream_emits(xs), stream_bang, NULL),
        sz_stream_eval(sz_io_pure(sz_string_from_cstr("c"))));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(s));
    assert(r.ok);
    SzString *joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a!,b!,c") == 0);

    r = sz_io_unsafe_run(sz_stream_drain(sz_stream_emit(sz_string_from_cstr("d"))));
    assert(r.ok);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"),
                     sz_list_cons(sz_string_from_cstr("c"), sz_list_nil())));
    r = sz_io_unsafe_run(
        sz_stream_compile_to_list(sz_stream_take(sz_stream_emits(xs), 2)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,b") == 0);

    delay_calls = 0;
    s = sz_stream_take(
        sz_stream_concat(sz_stream_eval(sz_io_delay(take_hit, (void *)"a")),
                         sz_stream_eval(sz_io_delay(take_hit, (void *)"b"))),
        1);
    r = sz_io_unsafe_run(sz_stream_compile_to_list(s));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a") == 0);
    assert(delay_calls == 1);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"),
                     sz_list_cons(sz_string_from_cstr("c"), sz_list_nil())));
    r = sz_io_unsafe_run(
        sz_stream_compile_to_list(sz_stream_drop(sz_stream_emits(xs), 1)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "b,c") == 0);

    delay_calls = 0;
    s = sz_stream_drop(
        sz_stream_concat(sz_stream_eval(sz_io_delay(take_hit, (void *)"a")),
                         sz_stream_eval(sz_io_delay(take_hit, (void *)"b"))),
        1);
    r = sz_io_unsafe_run(sz_stream_compile_to_list(s));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "b") == 0);
    assert(delay_calls == 2);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr(""),
                     sz_list_cons(sz_string_from_cstr("b"), sz_list_nil())));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_filter(sz_stream_emits(xs), stream_nonempty, NULL)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,b") == 0);

    delay_calls = 0;
    s = sz_stream_filter(
        sz_stream_concat(
            sz_stream_concat(sz_stream_eval(sz_io_delay(take_hit, (void *)"a")),
                             sz_stream_eval(sz_io_delay(take_hit, (void *)""))),
            sz_stream_eval(sz_io_delay(take_hit, (void *)"b"))),
        stream_nonempty, NULL);
    r = sz_io_unsafe_run(sz_stream_compile_to_list(s));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,b") == 0);
    assert(delay_calls == 3);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_map(sz_stream_emits(xs), stream_bang_sync, NULL)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a!,b!") == 0);

    delay_calls = 0;
    s = sz_stream_take(
        sz_stream_map(
            sz_stream_concat(sz_stream_eval(sz_io_delay(take_hit, (void *)"a")),
                             sz_stream_eval(sz_io_delay(take_hit, (void *)"b"))),
            stream_bang_sync, NULL),
        1);
    r = sz_io_unsafe_run(sz_stream_compile_to_list(s));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a!") == 0);
    assert(delay_calls == 1);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"),
                     sz_list_cons(sz_string_from_cstr(""),
                                  sz_list_cons(sz_string_from_cstr("c"),
                                               sz_list_nil()))));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_takewhile(sz_stream_emits(xs), stream_nonempty, NULL)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,b") == 0);

    delay_calls = 0;
    s = sz_stream_takewhile(
        sz_stream_concat(
            sz_stream_concat(sz_stream_eval(sz_io_delay(take_hit, (void *)"a")),
                             sz_stream_eval(sz_io_delay(take_hit, (void *)""))),
            sz_stream_eval(sz_io_delay(take_hit, (void *)"b"))),
        stream_nonempty, NULL);
    r = sz_io_unsafe_run(sz_stream_compile_to_list(s));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a") == 0);
    assert(delay_calls == 2);

    xs = sz_list_cons(
        sz_string_from_cstr(""),
        sz_list_cons(sz_string_from_cstr(""),
                     sz_list_cons(sz_string_from_cstr("a"),
                                  sz_list_cons(sz_string_from_cstr("b"),
                                               sz_list_nil()))));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_dropwhile(sz_stream_emits(xs), stream_empty, NULL)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,b") == 0);

    delay_calls = 0;
    s = sz_stream_dropwhile(
        sz_stream_concat(
            sz_stream_concat(sz_stream_eval(sz_io_delay(take_hit, (void *)"")),
                             sz_stream_eval(sz_io_delay(take_hit, (void *)""))),
            sz_stream_eval(sz_io_delay(take_hit, (void *)"a"))),
        stream_empty, NULL);
    r = sz_io_unsafe_run(sz_stream_compile_to_list(s));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a") == 0);
    assert(delay_calls == 3);

    xs = sz_list_cons(
        sz_string_from_cstr(""),
        sz_list_cons(sz_string_from_cstr("a"),
                     sz_list_cons(sz_string_from_cstr("b"), sz_list_nil())));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_find(sz_stream_emits(xs), stream_nonempty, NULL)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a") == 0);

    xs = sz_list_cons(sz_string_from_cstr(""),
                      sz_list_cons(sz_string_from_cstr("a"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_find(sz_stream_emits(xs), stream_empty, NULL)));
    assert(r.ok);
    assert(sz_list_len((SzList *)r.value) == 1);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head((SzList *)r.value)),
                  "") == 0);

    xs = sz_list_cons(sz_string_from_cstr("a"),
                      sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_find(sz_stream_emits(xs), stream_empty, NULL)));
    assert(r.ok);
    assert(sz_list_len((SzList *)r.value) == 0);

    delay_calls = 0;
    s = sz_stream_find(
        sz_stream_concat(
            sz_stream_concat(sz_stream_eval(sz_io_delay(take_hit, (void *)"")),
                             sz_stream_eval(sz_io_delay(take_hit, (void *)"a"))),
            sz_stream_eval(sz_io_delay(take_hit, (void *)"b"))),
        stream_nonempty, NULL);
    r = sz_io_unsafe_run(sz_stream_compile_to_list(s));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a") == 0);
    assert(delay_calls == 2);

    delay_calls = 0;
    r = sz_io_unsafe_run(sz_stream_exists(
        sz_stream_concat(
            sz_stream_concat(sz_stream_eval(sz_io_delay(take_hit, (void *)"")),
                             sz_stream_eval(sz_io_delay(take_hit, (void *)"a"))),
            sz_stream_eval(sz_io_delay(take_hit, (void *)"b"))),
        stream_nonempty, NULL));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 1);
    assert(delay_calls == 2);

    delay_calls = 0;
    r = sz_io_unsafe_run(sz_stream_exists(
        sz_stream_concat(sz_stream_eval(sz_io_delay(take_hit, (void *)"")),
                         sz_stream_eval(sz_io_delay(take_hit, (void *)""))),
        stream_nonempty, NULL));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
    assert(delay_calls == 2);
  }

  /* sleep */
  r = sz_io_unsafe_run(sz_io_sleep_ms(1));
  assert(r.ok);

  /* race prefers non-sleep winner */
  r = sz_io_unsafe_run(
      sz_io_race(sz_io_sleep_ms(20), sz_io_pure((void *)(intptr_t)99)));
  assert(r.ok);
  assert((intptr_t)r.value == 99);

  /* Live: losing sleep must not block the winner for the full duration. */
  {
    int64_t t0 = sz_clock_monotonic_ms_sync();
    int64_t t1;
    r = sz_io_unsafe_run(
        sz_io_race(sz_io_sleep_ms(300), sz_io_sleep_ms(1)));
    t1 = sz_clock_monotonic_ms_sync();
    assert(r.ok);
    assert(t1 - t0 < 80);
  }

  /* both */
  r = sz_io_unsafe_run(
      sz_io_both(sz_io_pure((void *)(intptr_t)1), sz_io_pure((void *)(intptr_t)2)));
  assert(r.ok);
  {
    SzPair *p = (SzPair *)r.value;
    assert(p && (intptr_t)p->left == 1 && (intptr_t)p->right == 2);
    sz_pair_free(p);
  }

  /* string ops */
  {
    SzString *a = sz_string_from_cstr("foo");
    SzString *b = sz_string_from_cstr("bar");
    SzString *c = sz_string_concat(a, b);
    assert(strcmp(sz_string_cstr(c), "foobar") == 0);
    assert(sz_string_len(c) == 6);
    assert(sz_string_eq(c, sz_string_from_cstr("foobar")));
    assert(sz_string_char_at(c, 0) == 'f');
    SzString *sl = sz_string_slice(c, 3, 6);
    assert(strcmp(sz_string_cstr(sl), "bar") == 0);
    assert(sz_string_index_of(c, b) == 3);
    assert(strcmp(sz_string_cstr(sz_string_from_int(42)), "42") == 0);
  }

  /* list */
  {
    SzList *xs = sz_list_cons(sz_string_from_cstr("a"),
                              sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    assert(sz_list_len(xs) == 2);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(xs)), "a") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_at(xs, 1)), "b") == 0);
    SzString *j = sz_list_join(xs, ",");
    assert(strcmp(sz_string_cstr(j), "a,b") == 0);
  }

  /* box */
  assert(sz_unbox_i64(sz_box_i64(7)) == 7);

  /* Fs roundtrip (live) */
  {
    const char *path = "build/test_fs_roundtrip.txt";
    r = sz_io_unsafe_run(sz_fs_mkdirs(sz_string_from_cstr("build")));
    assert(r.ok);
    r = sz_io_unsafe_run(
        sz_fs_write(sz_string_from_cstr(path), sz_string_from_cstr("fs-note")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_read(sz_string_from_cstr(path)));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "fs-note") == 0);
    r = sz_io_unsafe_run(sz_fs_list(sz_string_from_cstr("build")));
    assert(r.ok);
    assert(!sz_list_is_empty((SzList *)r.value));
    r = sz_io_unsafe_run(sz_fs_canonicalize(sz_string_from_cstr("build")));
    assert(r.ok);
    assert(sz_string_len((SzString *)r.value) > 0);
    remove(path);
  }

  /* TestRuntime: Fs graph built before install still uses mem-FS. */
  {
    SzIo *w = sz_fs_write(sz_string_from_cstr("step/x.txt"),
                          sz_string_from_cstr("step-mem"));
    SzIo *rd = sz_fs_read(sz_string_from_cstr("step/x.txt"));
    SzIo *ls = sz_fs_list(sz_string_from_cstr("step"));
    sz_testrt_install();
    r = sz_io_unsafe_run(w);
    assert(r.ok);
    r = sz_io_unsafe_run(rd);
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "step-mem") == 0);
    r = sz_io_unsafe_run(ls);
    assert(r.ok);
    assert(!sz_list_is_empty((SzList *)r.value));
    assert(access("step/x.txt", F_OK) != 0);
    sz_testrt_reset();
  }

  /* TestRuntime: fake clock sleep without wall wait */
  {
    int64_t t0, t1;
    sz_testrt_install();
    sz_testrt_net_stub("http://example.test/ping", "pong");
    t0 = sz_testrt_clock_now_ms();
    r = sz_io_unsafe_run(sz_io_sleep_ms(1000));
    assert(r.ok);
    t1 = sz_testrt_clock_now_ms();
    assert(t1 == t0 + 1000);

    r = sz_io_unsafe_run(sz_clock_real_time());
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == t1);

    r = sz_io_unsafe_run(sz_random_next_int(10));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) >= 0 && sz_unbox_i64(r.value) < 10);

    r = sz_io_unsafe_run(
        sz_fs_write(sz_string_from_cstr("a/b.txt"), sz_string_from_cstr("mem")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_read(sz_string_from_cstr("a/b.txt")));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "mem") == 0);

    r = sz_io_unsafe_run(
        sz_net_http_get(sz_string_from_cstr("http://example.test/ping")));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "pong") == 0);

    sz_testrt_net_inject_request("/hello");
    r = sz_io_unsafe_run(sz_net_serve_once(8080, serve_path_ok, NULL));
    assert(r.ok);
    assert(strcmp(sz_testrt_net_last_serve_body(), "ok:/hello") == 0);

    sz_testrt_net_inject_request("/a");
    sz_testrt_net_queue_request("/b");
    r = sz_io_unsafe_run(sz_net_serve(8080, serve_path_ok, NULL));
    assert(r.ok);
    assert(strcmp(sz_testrt_net_last_serve_body(), "ok:/b") == 0);
    assert(sz_testrt_net_serve_pending() == 0);

    g_serve_fail_n = 0;
    sz_testrt_net_inject_request("/a");
    sz_testrt_net_queue_request("/b");
    r = sz_io_unsafe_run(sz_net_serve(8080, serve_fail_then_ok, NULL));
    assert(r.ok);
    assert(strcmp(sz_testrt_net_last_serve_body(), "ok:/b") == 0);

    /* Console: argv override, stdin feed, println capture (+ echo) */
    {
      char *argv[] = {"x", "y"};
      sz_testrt_sys_set_args(2, argv);
      sz_testrt_stdin_feed("one\ntwo\n");
      sz_testrt_stdout_reset();

      r = sz_io_unsafe_run(sz_sys_args());
      assert(r.ok);
      assert(sz_list_len((SzList *)r.value) == 2);
      assert(strcmp(sz_string_cstr((SzString *)sz_list_head((SzList *)r.value)),
                    "x") == 0);

      r = sz_io_unsafe_run(sz_sys_read_line());
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "one") == 0);
      r = sz_io_unsafe_run(sz_sys_read_line());
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "two") == 0);
      r = sz_io_unsafe_run(sz_sys_read_line());
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "") == 0);

      sz_testrt_stdin_feed("abcdef");
      sz_testrt_stdout_reset();
      r = sz_io_unsafe_run(sz_sys_read(3));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "abc") == 0);
      r = sz_io_unsafe_run(sz_sys_read(3));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "def") == 0);
      r = sz_io_unsafe_run(sz_sys_write(sz_string_from_cstr("raw")));
      assert(r.ok);
      assert(strstr(sz_testrt_stdout_cstr(), "raw") != NULL);

      r = sz_io_unsafe_run(sz_io_println_cstr("cap"));
      assert(r.ok);
      assert(strstr(sz_testrt_stdout_cstr(), "cap\n") != NULL);
    }

    r = sz_io_unsafe_run(sz_impurity_run_kit());
    assert(r.ok);
    assert(strstr(sz_testrt_stdout_cstr(), "args:") != NULL);
    assert(strstr(sz_testrt_stdout_cstr(), "line:") != NULL);
    assert(strstr(sz_testrt_stdout_cstr(), "impurity-ok\n") != NULL);

    sz_testrt_reset();
    assert(!sz_testrt_clock_is_fake());
    assert(!sz_testrt_fs_is_fake());
    assert(!sz_testrt_sys_is_fake());
  }

  /* Cooperative fibers: race/both/Queue/Deferred + deterministic TestRuntime. */
  {
    int64_t t0, t1;
    const char *out1;
    const char *out2;
    SzQueue *q;
    SzDeferred *def;

    sz_testrt_install();

    /* race(sleep(100), sleep(1)) wins the short sleep; clock jumps by 1. */
    t0 = sz_testrt_clock_now_ms();
    r = sz_io_unsafe_run(sz_io_race(
        sz_io_flatmap(sz_io_sleep_ms(100), after_sleep_tag, (void *)(intptr_t)100),
        sz_io_flatmap(sz_io_sleep_ms(1), after_sleep_tag, (void *)(intptr_t)1)));
    assert(r.ok);
    assert((intptr_t)r.value == 1);
    t1 = sz_testrt_clock_now_ms();
    assert(t1 == t0 + 1);

    /* race(sleep, println) wins println without advancing the sleep. */
    t0 = sz_testrt_clock_now_ms();
    sz_testrt_stdout_reset();
    r = sz_io_unsafe_run(
        sz_io_race(sz_io_sleep_ms(50), sz_io_println_cstr("race-win")));
    assert(r.ok);
    assert(sz_testrt_clock_now_ms() == t0);
    assert(strstr(sz_testrt_stdout_cstr(), "race-win\n") != NULL);

    /* both println order is stable across runs. */
    sz_testrt_stdout_reset();
    r = sz_io_unsafe_run(
        sz_io_both(sz_io_println_cstr("a"), sz_io_println_cstr("b")));
    assert(r.ok);
    {
      char saved[64];
      const char *s = sz_testrt_stdout_cstr();
      assert(strlen(s) < sizeof(saved));
      memcpy(saved, s, strlen(s) + 1);
      sz_testrt_stdout_reset();
      r = sz_io_unsafe_run(
          sz_io_both(sz_io_println_cstr("a"), sz_io_println_cstr("b")));
      assert(r.ok);
      out2 = sz_testrt_stdout_cstr();
      assert(strcmp(saved, out2) == 0);
      assert(strstr(saved, "a\nb\n") != NULL);
      (void)out1;
    }

    /* both(take, offer) parks take until offer wakes it. */
    q = sz_queue_make();
    r = sz_io_unsafe_run(
        sz_io_both(sz_queue_take(q), sz_queue_offer_cstr(q, "parked")));
    assert(r.ok);
    {
      SzPair *p = (SzPair *)r.value;
      assert(p);
      assert(strcmp(sz_string_cstr((SzString *)p->left), "parked") == 0);
      sz_pair_free(p);
    }
    sz_queue_free(q);

    /* both(get, complete) parks get until complete wakes it. */
    def = sz_deferred_make();
    r = sz_io_unsafe_run(sz_io_both(sz_deferred_get(def),
                                    sz_deferred_complete_cstr(def, "go")));
    assert(r.ok);
    {
      SzPair *p = (SzPair *)r.value;
      assert(p);
      assert(strcmp(sz_string_cstr((SzString *)p->left), "go") == 0);
      sz_pair_free(p);
    }
    sz_deferred_free(def);

    sz_testrt_reset();
  }

  /* Seed-driven schedule pick (SCUZZ_SCHED_SEED): seed 0 → first 2-way pick is right. */
  {
    char saved[64];
    const char *s;

    sz_testrt_install();
    setenv("SCUZZ_SCHED_SEED", "0", 1);

    sz_testrt_stdout_reset();
    r = sz_io_unsafe_run(
        sz_io_both(sz_io_println_cstr("a"), sz_io_println_cstr("b")));
    assert(r.ok);
    s = sz_testrt_stdout_cstr();
    assert(strstr(s, "b\na\n") != NULL);
    assert(strlen(s) < sizeof(saved));
    memcpy(saved, s, strlen(s) + 1);

    sz_testrt_stdout_reset();
    r = sz_io_unsafe_run(
        sz_io_both(sz_io_println_cstr("a"), sz_io_println_cstr("b")));
    assert(r.ok);
    assert(strcmp(saved, sz_testrt_stdout_cstr()) == 0);

    unsetenv("SCUZZ_SCHED_SEED");
    sz_testrt_stdout_reset();
    r = sz_io_unsafe_run(
        sz_io_both(sz_io_println_cstr("a"), sz_io_println_cstr("b")));
    assert(r.ok);
    assert(strstr(sz_testrt_stdout_cstr(), "a\nb\n") != NULL);

    sz_testrt_reset();
  }

  /* Sys.spawn returns a live pid; Sys.alive reaps after SIGTERM. */
  {
    SzIoResult r;
    int64_t pid;
    int i;
    r = sz_io_unsafe_run(sz_sys_spawn(sz_string_from_cstr("sleep 5")));
    assert(r.ok);
    pid = sz_unbox_i64(r.value);
    assert(pid > 0);
    r = sz_io_unsafe_run(sz_sys_alive(pid));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 1);
    kill((pid_t)pid, SIGTERM);
    r = sz_io_unsafe_run(sz_sys_alive(pid));
    for (i = 0; i < 50 && r.ok && sz_unbox_i64(r.value) == 1; i++) {
      sleep_us(20000);
      r = sz_io_unsafe_run(sz_sys_alive(pid));
    }
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
  }

  /* Sys.kill sends SIGTERM; Sys.alive then reports the child gone. */
  {
    SzIoResult r;
    int64_t pid;
    int i;
    r = sz_io_unsafe_run(sz_sys_spawn(sz_string_from_cstr("sleep 5")));
    assert(r.ok);
    pid = sz_unbox_i64(r.value);
    assert(pid > 0);
    r = sz_io_unsafe_run(sz_sys_kill(pid));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_sys_alive(pid));
    for (i = 0; i < 50 && r.ok && sz_unbox_i64(r.value) == 1; i++) {
      sleep_us(20000);
      r = sz_io_unsafe_run(sz_sys_alive(pid));
    }
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
  }
  {
    r = sz_io_unsafe_run(sz_sys_exec(sz_string_from_cstr("true")));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
    r = sz_io_unsafe_run(sz_sys_exec(sz_string_from_cstr("exit 7")));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 7);
  }

  /* Sys.exec parks; a peer fiber runs before the child exits. */
  {
    SzPair *pair;
    g_peer_flag = 0;
    r = sz_io_unsafe_run(sz_io_both(
        sz_io_flatmap(sz_sys_exec(sz_string_from_cstr("sleep 0.08")),
                      exec_then_flag, NULL),
        sz_io_flatmap(sz_io_println_cstr("peer"), assert_peer_quiet, NULL)));
    assert(r.ok);
    pair = (SzPair *)r.value;
    assert(pair && pair->left);
    assert(sz_unbox_i64(pair->left) == 0);
    assert(g_peer_flag == 1);
  }

  /* Live Net.serveOnce: client thread GET while this fiber accepts. */
  {
    pthread_t th;
    int port = 18473;
    void *ret = NULL;
    pthread_create(&th, NULL, live_get_client, &port);
    r = sz_io_unsafe_run(sz_net_serve_once(port, serve_path_ok, NULL));
    pthread_join(th, &ret);
    assert(r.ok);
    assert(ret && strstr((char *)ret, "ok:/x") != NULL);
  }

  /* Client connects and never sends GET: serve fails in ~1s instead of hanging. */
  {
    pthread_t th;
    int port = 18585;
    int64_t t0;
    int64_t t1;
    pthread_create(&th, NULL, connect_hold, &port);
    t0 = sz_clock_monotonic_ms_sync();
    r = sz_io_unsafe_run(sz_net_serve_once(port, serve_path_ok, NULL));
    t1 = sz_clock_monotonic_ms_sync();
    pthread_join(th, NULL);
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "request timed out"));
    sz_error_free(r.error);
    assert(t1 - t0 >= 900);
    assert(t1 - t0 < 2500);
  }

  /* Persistent serve: a timed-out idle client does not block the next GET. */
  {
    pthread_t th_idle;
    pthread_t th_get;
    int port = 18587;
    void *ret = NULL;
    pthread_create(&th_idle, NULL, connect_hold, &port);
    pthread_create(&th_get, NULL, delayed_live_get, &port);
    r = sz_io_unsafe_run(sz_io_race(sz_net_serve(port, serve_path_ok, NULL),
                                   sz_io_sleep_ms(2500)));
    pthread_join(th_idle, NULL);
    pthread_join(th_get, &ret);
    assert(r.ok);
    assert(ret && strstr((char *)ret, "ok:/x") != NULL);
  }

  /* Client connects and closes without a GET: serveOnce fails. */
  {
    pthread_t th;
    int port = 18589;
    pthread_create(&th, NULL, connect_close, &port);
    r = sz_io_unsafe_run(sz_net_serve_once(port, serve_path_ok, NULL));
    pthread_join(th, NULL);
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "expected HTTP GET"));
    sz_error_free(r.error);
  }

  /* Persistent serve: a closed-without-GET client does not block the next GET. */
  {
    pthread_t th_bad;
    pthread_t th_get;
    int port = 18588;
    void *ret = NULL;
    pthread_create(&th_bad, NULL, connect_close, &port);
    pthread_create(&th_get, NULL, soon_live_get, &port);
    r = sz_io_unsafe_run(sz_io_race(sz_net_serve(port, serve_path_ok, NULL),
                                   sz_io_sleep_ms(400)));
    pthread_join(th_bad, NULL);
    pthread_join(th_get, &ret);
    assert(r.ok);
    assert(ret && strstr((char *)ret, "ok:/x") != NULL);
  }

  /* Client GET then never reads: serve fails in ~1s instead of hanging on write. */
  {
    pthread_t th;
    int port = 18586;
    int64_t t0;
    int64_t t1;
    pthread_create(&th, NULL, get_then_hold, &port);
    t0 = sz_clock_monotonic_ms_sync();
    r = sz_io_unsafe_run(sz_net_serve_once(port, serve_big_ok, NULL));
    t1 = sz_clock_monotonic_ms_sync();
    pthread_join(th, NULL);
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "write timed out"));
    sz_error_free(r.error);
    assert(t1 - t0 >= 900);
    assert(t1 - t0 < 2500);
  }

  /* Client GET then RST: serveOnce fails with write failed. */
  {
    pthread_t th;
    int port = 18590;
    pthread_create(&th, NULL, get_then_rst, &port);
    r = sz_io_unsafe_run(sz_net_serve_once(port, serve_padded_ok, NULL));
    pthread_join(th, NULL);
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "write failed"));
    sz_error_free(r.error);
  }

  /* Persistent serve: a reset-during-write client does not block the next GET. */
  {
    pthread_t th_bad;
    pthread_t th_get;
    int port = 18591;
    void *ret = NULL;
    pthread_create(&th_bad, NULL, get_then_rst, &port);
    pthread_create(&th_get, NULL, soon_live_get, &port);
    r = sz_io_unsafe_run(sz_io_race(sz_net_serve(port, serve_padded_ok, NULL),
                                   sz_io_sleep_ms(400)));
    pthread_join(th_bad, NULL);
    pthread_join(th_get, &ret);
    assert(r.ok);
    assert(ret && strstr((char *)ret, "ok:/x") != NULL);
  }

  /* Handler fail: serveOnce fails. */
  {
    pthread_t th;
    int port = 18592;
    g_serve_fail_n = 0;
    pthread_create(&th, NULL, live_get_client, &port);
    r = sz_io_unsafe_run(sz_net_serve_once(port, serve_fail_then_ok, NULL));
    pthread_join(th, NULL);
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "handler boom"));
    sz_error_free(r.error);
  }

  /* Persistent serve: a failing handler does not block the next GET. */
  {
    pthread_t th;
    int port = 18593;
    void *ret = NULL;
    g_serve_fail_n = 0;
    pthread_create(&th, NULL, two_live_gets, &port);
    r = sz_io_unsafe_run(sz_io_race(sz_net_serve(port, serve_fail_then_ok, NULL),
                                   sz_io_sleep_ms(400)));
    pthread_join(th, &ret);
    assert(r.ok);
    assert(ret && strstr((char *)ret, "ok:/x") != NULL);
  }

  /* poll parks so a peer fiber can run before the fd is readable. */
  {
    pthread_t th;
    int fds[2];
    char c = 0;
    assert(pipe(fds) == 0);
    g_peer_flag = 0;
    pthread_create(&th, NULL, pipe_late_write, &fds[1]);
    r = sz_io_unsafe_run(sz_io_both(
        sz_io_poll_readable(fds[0]),
        sz_io_flatmap(sz_io_println_cstr("peer"), assert_peer_quiet, NULL)));
    pthread_join(th, NULL);
    assert(r.ok);
    assert(read(fds[0], &c, 1) == 1 && c == 'x');
    close(fds[0]);
    close(fds[1]);
  }

  /* Live accept parks; a peer fiber runs before the client dials. */
  {
    pthread_t th;
    int port = 18475;
    void *ret = NULL;
    g_peer_flag = 0;
    pthread_create(&th, NULL, live_get_client_late, &port);
    r = sz_io_unsafe_run(sz_io_both(
        sz_net_serve_once(port, serve_path_ok, NULL),
        sz_io_flatmap(sz_io_println_cstr("peer"), assert_peer_quiet, NULL)));
    pthread_join(th, &ret);
    assert(r.ok);
    assert(ret && strstr((char *)ret, "ok:/x") != NULL);
  }

  /* Live httpGet parks on connect/read; serve peer answers without a client thread. */
  {
    int port = 18576;
    char url[64];
    SzPair *pair;
    snprintf(url, sizeof url, "http://127.0.0.1:%d/x", port);
    r = sz_io_unsafe_run(sz_io_both(
        sz_net_serve_once(port, serve_path_ok, NULL),
        sz_io_flatmap(sz_io_sleep_ms(30), after_sleep_http,
                      sz_string_from_cstr(url))));
    assert(r.ok);
    pair = (SzPair *)r.value;
    assert(pair && pair->right);
    assert(strcmp(sz_string_cstr((SzString *)pair->right), "ok:/x") == 0);
  }

  /* Live Net.serveOnce on ::1: httpGet through IPv6 literal. */
  {
    int port = 18581;
    char url[64];
    SzPair *pair;
    snprintf(url, sizeof url, "http://[::1]:%d/x", port);
    r = sz_io_unsafe_run(sz_io_both(
        sz_net_serve_once(port, serve_path_ok, NULL),
        sz_io_flatmap(sz_io_sleep_ms(30), after_sleep_http,
                      sz_string_from_cstr(url))));
    assert(r.ok);
    pair = (SzPair *)r.value;
    assert(pair && pair->right);
    assert(strcmp(sz_string_cstr((SzString *)pair->right), "ok:/x") == 0);
  }

  /* IPv6 literals skip DNS: http://[::1]:port/x */
  {
    pthread_t th;
    int port = 18579;
    char url[64];
    void *ret = NULL;
    pthread_create(&th, NULL, ipv6_http_once, &port);
    snprintf(url, sizeof url, "http://[::1]:%d/x", port);
    r = sz_io_unsafe_run(sz_io_flatmap(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    pthread_join(th, &ret);
    assert(r.ok);
    assert(ret != NULL);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "ok:/x") == 0);
  }

  /* Hostname A NODATA then AAAA ::1, then HTTP on IPv6 loopback. */
  {
    pthread_t th_dns;
    pthread_t th_http;
    int dns_fd;
    int http_port = 18580;
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    char url[80];
    void *http_ret = NULL;
    dns_fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(dns_fd >= 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(bind(dns_fd, (struct sockaddr *)&addr, sizeof addr) == 0);
    alen = sizeof addr;
    assert(getsockname(dns_fd, (struct sockaddr *)&addr, &alen) == 0);
    sz_net_test_set_nameserver("127.0.0.1", (int)ntohs(addr.sin_port));
    pthread_create(&th_http, NULL, ipv6_http_once, &http_port);
    pthread_create(&th_dns, NULL, dns_late_aaaa, &dns_fd);
    snprintf(url, sizeof url, "http://scuzz.test:%d/x", http_port);
    r = sz_io_unsafe_run(sz_io_flatmap(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    pthread_join(th_dns, NULL);
    pthread_join(th_http, &http_ret);
    close(dns_fd);
    sz_net_test_set_nameserver(NULL, 0);
    assert(r.ok);
    assert(http_ret != NULL);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "ok:/x") == 0);
  }

  /* Happy Eyeballs: dead A 192.0.2.1 plus AAAA ::1; v6 connect wins. */
  {
    pthread_t th_dns;
    pthread_t th_http;
    int dns_fd;
    int http_port = 18582;
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    char url[80];
    void *http_ret = NULL;
    dns_fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(dns_fd >= 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(bind(dns_fd, (struct sockaddr *)&addr, sizeof addr) == 0);
    alen = sizeof addr;
    assert(getsockname(dns_fd, (struct sockaddr *)&addr, &alen) == 0);
    sz_net_test_set_nameserver("127.0.0.1", (int)ntohs(addr.sin_port));
    pthread_create(&th_http, NULL, ipv6_http_once, &http_port);
    pthread_create(&th_dns, NULL, dns_he_dead_a, &dns_fd);
    snprintf(url, sizeof url, "http://scuzz.test:%d/x", http_port);
    r = sz_io_unsafe_run(sz_io_flatmap(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    pthread_join(th_dns, NULL);
    pthread_join(th_http, &http_ret);
    close(dns_fd);
    sz_net_test_set_nameserver(NULL, 0);
    assert(r.ok);
    assert(http_ret != NULL);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "ok:/x") == 0);
  }

  /* Dual-stack DNS: v6 ::1 refused, then A 127.0.0.1 after preference delay. */
  {
    pthread_t th_dns;
    pthread_t th_http;
    int dns_fd;
    int http_port = 18583;
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    char url[80];
    void *http_ret = NULL;
    dns_fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(dns_fd >= 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(bind(dns_fd, (struct sockaddr *)&addr, sizeof addr) == 0);
    alen = sizeof addr;
    assert(getsockname(dns_fd, (struct sockaddr *)&addr, &alen) == 0);
    sz_net_test_set_nameserver("127.0.0.1", (int)ntohs(addr.sin_port));
    pthread_create(&th_http, NULL, ipv4_http_once, &http_port);
    pthread_create(&th_dns, NULL, dns_he_v4_loopback, &dns_fd);
    snprintf(url, sizeof url, "http://scuzz.test:%d/x", http_port);
    r = sz_io_unsafe_run(sz_io_flatmap(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    pthread_join(th_dns, NULL);
    pthread_join(th_http, &http_ret);
    close(dns_fd);
    sz_net_test_set_nameserver(NULL, 0);
    assert(r.ok);
    assert(http_ret != NULL);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "ok:/x") == 0);
  }

  /* TEST-NET-1 blackhole: connect fails in ~1s instead of hanging on the OS. */
  {
    int64_t t0 = sz_clock_monotonic_ms_sync();
    int64_t t1;
    r = sz_io_unsafe_run(
        sz_net_http_get(sz_string_from_cstr("http://192.0.2.1:9/x")));
    t1 = sz_clock_monotonic_ms_sync();
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "timed out"));
    sz_error_free(r.error);
    assert(t1 - t0 >= 900);
    assert(t1 - t0 < 2500);
  }

  /* Silent nameserver: DNS fails in ~1s instead of parking on UDP recv. */
  {
    int dns_fd;
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    int64_t t0;
    int64_t t1;
    dns_fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(dns_fd >= 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(bind(dns_fd, (struct sockaddr *)&addr, sizeof addr) == 0);
    alen = sizeof addr;
    assert(getsockname(dns_fd, (struct sockaddr *)&addr, &alen) == 0);
    sz_net_test_set_nameserver("127.0.0.1", (int)ntohs(addr.sin_port));
    t0 = sz_clock_monotonic_ms_sync();
    r = sz_io_unsafe_run(
        sz_net_http_get(sz_string_from_cstr("http://silent.test/x")));
    t1 = sz_clock_monotonic_ms_sync();
    close(dns_fd);
    sz_net_test_set_nameserver(NULL, 0);
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "DNS timed out"));
    sz_error_free(r.error);
    assert(t1 - t0 >= 900);
    assert(t1 - t0 < 2500);
  }

  /* Peer accepts and never responds: read fails in ~1s instead of hanging. */
  {
    pthread_t th;
    int port = 18584;
    char url[64];
    int64_t t0;
    int64_t t1;
    pthread_create(&th, NULL, ipv4_http_hold, &port);
    snprintf(url, sizeof url, "http://127.0.0.1:%d/x", port);
    t0 = sz_clock_monotonic_ms_sync();
    r = sz_io_unsafe_run(sz_io_flatmap(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    t1 = sz_clock_monotonic_ms_sync();
    pthread_join(th, NULL);
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "read timed out"));
    sz_error_free(r.error);
    assert(t1 - t0 >= 900);
    assert(t1 - t0 < 2500);
  }

  /* Live httpGet DNS parks on UDP poll; a peer fiber runs before the answer. */
  {
    pthread_t th;
    int dns_fd;
    int http_port = 18577;
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    char url[80];
    SzPair *outer;
    SzPair *inner;
    dns_fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(dns_fd >= 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(bind(dns_fd, (struct sockaddr *)&addr, sizeof addr) == 0);
    alen = sizeof addr;
    assert(getsockname(dns_fd, (struct sockaddr *)&addr, &alen) == 0);
    sz_net_test_set_nameserver("127.0.0.1", (int)ntohs(addr.sin_port));
    g_peer_flag = 0;
    pthread_create(&th, NULL, dns_late_a, &dns_fd);
    snprintf(url, sizeof url, "http://scuzz.test:%d/x", http_port);
    r = sz_io_unsafe_run(sz_io_both(
        sz_net_serve_once(http_port, serve_path_ok, NULL),
        sz_io_flatmap(sz_io_sleep_ms(30), after_sleep_dns_http,
                      sz_string_from_cstr(url))));
    pthread_join(th, NULL);
    close(dns_fd);
    sz_net_test_set_nameserver(NULL, 0);
    assert(r.ok);
    outer = (SzPair *)r.value;
    assert(outer && outer->right);
    inner = (SzPair *)outer->right;
    assert(inner && inner->left);
    assert(strcmp(sz_string_cstr((SzString *)inner->left), "ok:/x") == 0);
  }

  /* CNAME-only answer re-queries the target, then A. */
  {
    pthread_t th;
    int dns_fd;
    int http_port = 18578;
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    char url[80];
    SzPair *outer;
    SzPair *inner;
    dns_fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(dns_fd >= 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(bind(dns_fd, (struct sockaddr *)&addr, sizeof addr) == 0);
    alen = sizeof addr;
    assert(getsockname(dns_fd, (struct sockaddr *)&addr, &alen) == 0);
    sz_net_test_set_nameserver("127.0.0.1", (int)ntohs(addr.sin_port));
    g_peer_flag = 0;
    pthread_create(&th, NULL, dns_late_cname, &dns_fd);
    snprintf(url, sizeof url, "http://scuzz.test:%d/x", http_port);
    r = sz_io_unsafe_run(sz_io_both(
        sz_net_serve_once(http_port, serve_path_ok, NULL),
        sz_io_flatmap(sz_io_sleep_ms(30), after_sleep_dns_http,
                      sz_string_from_cstr(url))));
    pthread_join(th, NULL);
    close(dns_fd);
    sz_net_test_set_nameserver(NULL, 0);
    assert(r.ok);
    outer = (SzPair *)r.value;
    assert(outer && outer->right);
    inner = (SzPair *)outer->right;
    assert(inner && inner->left);
    assert(strcmp(sz_string_cstr((SzString *)inner->left), "ok:/x") == 0);
  }

  /* Live Sys.readLine leftover: one write, two lines. */
  {
    int fds[2];
    int saved = dup(STDIN_FILENO);
    assert(saved >= 0);
    assert(pipe(fds) == 0);
    assert(dup2(fds[0], STDIN_FILENO) == 0);
    close(fds[0]);
    assert(write(fds[1], "a\nb\n", 4) == 4);
    close(fds[1]);
    r = sz_io_unsafe_run(sz_sys_read_line());
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "a") == 0);
    r = sz_io_unsafe_run(sz_sys_read_line());
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "b") == 0);
    assert(dup2(saved, STDIN_FILENO) == 0);
    close(saved);
  }

  /* Live Sys.readLine parks; a peer fiber runs before stdin is written. */
  {
    pthread_t th;
    int fds[2];
    int saved = dup(STDIN_FILENO);
    int wr;
    SzPair *pair;
    assert(saved >= 0);
    assert(pipe(fds) == 0);
    assert(dup2(fds[0], STDIN_FILENO) == 0);
    close(fds[0]);
    wr = fds[1];
    g_peer_flag = 0;
    pthread_create(&th, NULL, stdin_late_write, &wr);
    r = sz_io_unsafe_run(sz_io_both(
        sz_sys_read_line(),
        sz_io_flatmap(sz_io_println_cstr("peer"), assert_peer_quiet, NULL)));
    pthread_join(th, NULL);
    assert(dup2(saved, STDIN_FILENO) == 0);
    close(saved);
    close(wr);
    assert(r.ok);
    pair = (SzPair *)r.value;
    assert(pair && pair->left);
    assert(strcmp(sz_string_cstr((SzString *)pair->left), "hello") == 0);
  }

  /* Alloc accounting: live_count returns to baseline after free. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    void *p;
    void *z;
    sz_alloc_stats(&base_bytes, &base_count);
    p = sz_alloc(128);
    assert(p);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count + 1);
    assert(live_bytes == base_bytes + 128);
    z = sz_alloc_zero(64);
    assert(z);
    assert(((unsigned char *)z)[0] == 0);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count + 2);
    assert(live_bytes == base_bytes + 128 + 64);
    sz_free(p);
    sz_free(z);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
    sz_alloc_reset_stats();
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List spine free: cons cells go away; heads are not owned. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *s = sz_string_from_cstr("milk");
    SzList *xs;
    sz_alloc_stats(&base_bytes, &base_count);
    xs = sz_list_cons(s, sz_list_nil());
    sz_list_free(xs);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
    sz_string_free(s);
  }

  puts("runtime io tests ok");
  return 0;
}
