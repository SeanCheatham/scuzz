#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static SzIo *pure_drop(void *value);

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

static SzIo *cont_pure_unit(void *value, void *env) {
  (void)value;
  (void)env;
  return pure_drop(NULL);
}

static int lang_released = 0;
static void *lang_release_thunk(void *env) {
  (void)env;
  lang_released = 1;
  return NULL;
}
static SzIo *lang_release(void *acquired, void *env) {
  (void)env;
  assert(acquired);
  return sz_io_delay(lang_release_thunk, NULL);
}
static SzIo *lang_release_pure_str(void *acquired, void *env) {
  (void)acquired;
  (void)env;
  return pure_drop(sz_string_from_cstr("x"));
}
static SzLangResource *lang_make_tok(void) {
  SzIo *acq = pure_drop(sz_string_from_cstr("tok"));
  SzLangResource *lr = sz_lang_resource_make(acq, lang_release, NULL);
  sz_release(acq);
  return lr;
}
static SzIo *repeat_n_drop(int64_t n, SzIo *inner) {
  SzIo *io = sz_io_repeat_n(n, inner);
  sz_release(inner);
  return io;
}
static SzIo *retry_n_drop(int64_t n, SzIo *inner) {
  SzIo *io = sz_io_retry_n(n, inner);
  sz_release(inner);
  return io;
}
static SzIo *fork_drop(SzIo *inner) {
  SzIo *io = sz_fiber_fork(inner);
  sz_release(inner);
  return io;
}
static SzIo *timeout_drop(int64_t ms, SzIo *inner) {
  SzIo *io = sz_io_timeout(ms, inner);
  sz_release(inner);
  return io;
}
static SzIo *ensure_drop(SzIo *inner, SzIo *finalizer) {
  SzIo *io = sz_io_ensure(inner, finalizer);
  sz_release(inner);
  sz_release(finalizer);
  return io;
}
static SzIo *race_drop(SzIo *left, SzIo *right) {
  SzIo *io = sz_io_race(left, right);
  sz_release(left);
  sz_release(right);
  return io;
}
static SzIo *both_drop(SzIo *left, SzIo *right) {
  SzIo *io = sz_io_both(left, right);
  sz_release(left);
  sz_release(right);
  return io;
}
static SzIo *handle_drop(SzIo *inner, SzErrorHandler handler, void *env) {
  SzIo *io = sz_io_handle_error_with(inner, handler, env);
  sz_release(inner);
  return io;
}

static SzIo *fm_drop(SzIo *inner, SzCont cont, void *env) {
  SzIo *io = sz_io_flatmap(inner, cont, env);
  sz_release(inner);
  return io;
}


static SzIo *pure_drop(void *value) {
  SzIo *io = sz_io_pure(value);
  sz_release(value);
  return io;
}

static SzIo *fail_drop(SzError *err) {
  SzIo *io = sz_io_fail(err);
  sz_release(err);
  return io;
}

static SzIo *attempt_drop(SzIo *inner) {
  SzIo *io = sz_io_attempt(inner);
  sz_release(inner);
  return io;
}
static SzIo *lang_use_ok(void *acquired, void *env) {
  (void)env;
  (void)acquired;
  return sz_io_println_cstr("lang-resource-used");
}
static SzIo *lang_use_println_env(void *acquired, void *env) {
  (void)acquired;
  return sz_io_println((SzString *)sz_list_head((SzList *)env));
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

static int lang_used = 0;
static SzIo *lang_use_mark(void *acquired, void *env) {
  (void)acquired;
  (void)env;
  lang_used = 1;
  return pure_drop(NULL);
}

static int lang_use_stepped = 0;
static void *use_step_thunk(void *env) {
  (void)env;
  lang_use_stepped = 1;
  return NULL;
}
static SzIo *lang_use_step(void *acquired, void *env) {
  (void)acquired;
  (void)env;
  return sz_io_delay(use_step_thunk, NULL);
}

static SzIo *fiber_interrupt_direct(void *fiber, void *env) {
  (void)env;
  return sz_fiber_interrupt(fiber);
}

static SzIo *stream_bang(void *v, void *env) {
  (void)env;
  SzString *bang = sz_string_from_cstr("!");
  SzString *out = sz_string_concat((SzString *)v, bang);
  sz_release(bang);
  return pure_drop(out);
}

static int foreach_hits = 0;

static SzIo *foreach_fail_second(void *v, void *env) {
  (void)env;
  foreach_hits++;
  if (foreach_hits >= 2)
    return sz_io_fail_cstr("foreach-boom");
  sz_retain(v);
  return pure_drop(v);
}

static SzIo *foreach_unit(void *v, void *env) {
  (void)v;
  (void)env;
  foreach_hits++;
  return pure_drop(NULL);
}

static int64_t stream_nonempty(void *v, void *env) {
  (void)env;
  return sz_string_len((SzString *)v) > 0;
}

static void *stream_bang_sync(void *v, void *env) {
  (void)env;
  SzString *bang = sz_string_from_cstr("!");
  SzString *out = sz_string_concat((SzString *)v, bang);
  sz_release(bang);
  return out;
}

static int64_t stream_empty(void *v, void *env) {
  (void)env;
  return sz_string_len((SzString *)v) == 0;
}

static void *stream_dup(void *v, void *env) {
  (void)env;
  return sz_stream_concat(sz_stream_emit(v), sz_stream_emit(v));
}

static void *stream_scan_concat(void *pack, void *env) {
  SzPair *p = (SzPair *)pack;
  (void)env;
  return sz_string_concat((SzString *)p->left, (SzString *)p->right);
}

static void *stream_fold_concat(void *pack, void *env) {
  return stream_scan_concat(pack, env);
}

static void *stream_list_dup(void *v, void *env) {
  SzList *nil = sz_list_nil();
  SzList *one;
  SzList *two;
  (void)env;
  one = sz_list_cons(v, nil);
  sz_release(nil);
  two = sz_list_cons(v, one);
  sz_release(one);
  return two;
}

static void *stream_zip_concat(void *pack, void *env) {
  SzPair *p = (SzPair *)pack;
  (void)env;
  return sz_string_concat((SzString *)p->left, (SzString *)p->right);
}

static int tap_n;

static SzIo *stream_tap_count(void *v, void *env) {
  (void)v;
  (void)env;
  tap_n += 1;
  return pure_drop(NULL);
}

static void *stream_inc(void *v, void *env) {
  (void)env;
  return sz_box_i64(sz_unbox_i64(v) + 1);
}

static void *stream_unfold_step(void *v, void *env) {
  int64_t n = sz_unbox_i64(v);
  void *a;
  void *next;
  SzPair *p;
  SzList *nil;
  SzList *xs;
  (void)env;
  if (n >= 3)
    return sz_list_nil();
  a = sz_box_i64(n);
  next = sz_box_i64(n + 1);
  p = sz_pair_new(a, next);
  sz_release(a);
  sz_release(next);
  nil = sz_list_nil();
  xs = sz_list_cons(p, nil);
  sz_release(nil);
  sz_release(p);
  return xs;
}

static SzIo *serve_path_ok(void *path, void *env) {
  SzString *prefix;
  SzString *out;
  (void)env;
  prefix = sz_string_from_cstr("ok:");
  out = sz_string_concat(prefix, (SzString *)path);
  sz_release(prefix);
  return pure_drop(out);
}

static int g_serve_fail_n;

static SzIo *serve_fail_then_ok(void *path, void *env) {
  (void)env;
  if (g_serve_fail_n++ == 0)
    return sz_io_fail_cstr("handler boom");
  return pure_drop(
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
    return pure_drop(s);
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
  return pure_drop(s);
}

enum { SERVE_LEAK_N = 10000 };
static int g_serve_rounds;
static size_t g_serve_bytes[2];
static size_t g_serve_count[2];
static size_t g_serve_raw[2];
static size_t g_serve_io[2];

static SzIo *serve_leak_ok(void *path, void *env) {
  int slot = -1;
  g_serve_rounds++;
  if (g_serve_rounds == 8)
    slot = 0;
  else if (g_serve_rounds == SERVE_LEAK_N)
    slot = 1;
  if (slot >= 0) {
    sz_alloc_stats(&g_serve_bytes[slot], &g_serve_count[slot]);
    sz_alloc_kind_stats(SZ_RC_RAW, NULL, &g_serve_raw[slot]);
    sz_alloc_kind_stats(SZ_RC_IO, NULL, &g_serve_io[slot]);
  }
  if (g_serve_rounds < SERVE_LEAK_N)
    sz_testrt_net_queue_request("/x");
  return serve_path_ok(path, env);
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

static void *live_get_client_v6(void *arg) {
  int port = *(int *)arg;
  int fd = -1;
  int i;
  struct sockaddr_in6 addr;
  char req[128];
  static char buf[1024];
  ssize_t n;
  size_t total = 0;
  memset(buf, 0, sizeof buf);
  memset(&addr, 0, sizeof addr);
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons((uint16_t)port);
  if (inet_pton(AF_INET6, "::1", &addr.sin6_addr) != 1)
    return NULL;
  snprintf(req, sizeof req,
           "GET /x HTTP/1.0\r\nHost: [::1]\r\nConnection: close\r\n\r\n");
  for (i = 0; i < 50; i++) {
    sleep_us(10000);
    fd = socket(AF_INET6, SOCK_STREAM, 0);
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

static void *two_live_gets_v6(void *arg) {
  live_get_client_v6(arg);
  return live_get_client_v6(arg);
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
static int64_t g_pipe_pid;

static SzIo *pipe_write_hello(void *value, void *env) {
  SzString *msg = sz_string_from_cstr("hello");
  SzIo *io;
  (void)value;
  (void)env;
  io = sz_sys_child_write(g_pipe_pid, msg);
  sz_release(msg);
  return io;
}

static SzIo *assert_peer_quiet(void *value, void *env) {
  (void)value;
  (void)env;
  assert(g_peer_flag == 0);
  return pure_drop(NULL);
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
  return both_drop(
      sz_net_http_get((SzString *)env),
      fm_drop(sz_io_println_cstr("peer"), assert_peer_quiet, NULL));
}

static SzIo *exec_then_flag(void *value, void *env) {
  (void)env;
  g_peer_flag = 1;
  return pure_drop(value);
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

/* AAAA NXDOMAIN first, then A 127.0.0.1. */
static void *dns_aaaa_nxdomain_then_a(void *arg) {
  int fd = *(int *)arg;
  uint8_t buf[512];
  uint8_t held[512];
  uint8_t ans[16];
  struct sockaddr_in from;
  struct sockaddr_in held_from;
  socklen_t flen;
  socklen_t held_flen = 0;
  ssize_t n;
  ssize_t held_n = 0;
  int got_aaaa = 0;
  int got_a = 0;
  memset(ans, 0, sizeof ans);
  ans[0] = 0xC0;
  ans[1] = 0x0C;
  ans[3] = 1;
  ans[5] = 1;
  ans[9] = 60;
  ans[11] = 4;
  ans[12] = 127;
  ans[15] = 1;
  while (!got_aaaa || !got_a) {
    flen = sizeof from;
    n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
    if (n < 12)
      return NULL;
    if (dns_qtype_of(buf, n) == 28) {
      dns_set_qr(buf, 0);
      buf[3] = (uint8_t)((buf[3] & 0xF0) | 3);
      if (sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
      got_aaaa = 1;
      if (held_n >= 12) {
        if ((size_t)held_n + 16 > sizeof held)
          return NULL;
        dns_set_qr(held, 1);
        memcpy(held + held_n, ans, 16);
        if (sendto(fd, held, (size_t)held_n + 16, 0,
                   (struct sockaddr *)&held_from, held_flen) < 0)
          return NULL;
        got_a = 1;
        held_n = 0;
      }
    } else if (dns_qtype_of(buf, n) == 1) {
      if (!got_aaaa) {
        if ((size_t)n > sizeof held)
          return NULL;
        memcpy(held, buf, (size_t)n);
        held_n = n;
        held_from = from;
        held_flen = flen;
      } else {
        if ((size_t)n + 16 > sizeof buf)
          return NULL;
        dns_set_qr(buf, 1);
        memcpy(buf + n, ans, 16);
        if (sendto(fd, buf, (size_t)n + 16, 0, (struct sockaddr *)&from, flen) <
            0)
          return NULL;
        got_a = 1;
      }
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

static void dns_set_ar(uint8_t *buf, uint16_t arcount) {
  buf[10] = (uint8_t)(arcount >> 8);
  buf[11] = (uint8_t)arcount;
}

static void *dns_glue_only(void *arg) {
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
    if (n < 12)
      return NULL;
    dns_set_qr(buf, 0);
    if (dns_qtype_of(buf, n) == 1) {
      if ((size_t)n + 16 > sizeof buf)
        return NULL;
      dns_set_ar(buf, 1);
      memcpy(buf + n, a, 16);
      if (sendto(fd, buf, (size_t)n + 16, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
    } else {
      if ((size_t)n + 28 > sizeof buf)
        return NULL;
      dns_set_ar(buf, 1);
      memcpy(buf + n, aaaa, 28);
      if (sendto(fd, buf, (size_t)n + 28, 0, (struct sockaddr *)&from, flen) < 0)
        return NULL;
    }
  }
  return NULL;
}

static void *dns_wrong_src(void *arg) {
  int fd = *(int *)arg;
  int spoof;
  uint8_t buf[512];
  uint8_t ans[16];
  struct sockaddr_in from;
  socklen_t flen;
  ssize_t n;
  int i;
  spoof = socket(AF_INET, SOCK_DGRAM, 0);
  if (spoof < 0)
    return NULL;
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
    if (n < 12) {
      close(spoof);
      return NULL;
    }
    if (dns_qtype_of(buf, n) == 1) {
      if ((size_t)n + 16 > sizeof buf) {
        close(spoof);
        return NULL;
      }
      dns_set_qr(buf, 1);
      memcpy(buf + n, ans, 16);
      if (sendto(spoof, buf, (size_t)n + 16, 0, (struct sockaddr *)&from, flen) <
          0) {
        close(spoof);
        return NULL;
      }
    } else {
      dns_set_qr(buf, 0);
      if (sendto(spoof, buf, (size_t)n, 0, (struct sockaddr *)&from, flen) < 0) {
        close(spoof);
        return NULL;
      }
    }
  }
  close(spoof);
  return NULL;
}

static void *ipv4_http_hold_complete(void *arg) {
  int port = *(int *)arg;
  int fd, cfd;
  int one = 1;
  struct sockaddr_in addr;
  char buf[512];
  const char *resp = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nok:/x";
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
  sleep_us(1500000);
  close(cfd);
  return (void *)1;
}

static void *ipv4_http_404(void *arg) {
  int port = *(int *)arg;
  int fd, cfd;
  int one = 1;
  struct sockaddr_in addr;
  char buf[512];
  const char *resp = "HTTP/1.0 404 Not Found\r\nContent-Length: 3\r\n\r\nerr";
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

static void *ipv4_http_rst(void *arg) {
  int port = *(int *)arg;
  int fd, cfd;
  int one = 1;
  struct linger lin;
  struct sockaddr_in addr;
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
  lin.l_onoff = 1;
  lin.l_linger = 0;
  setsockopt(cfd, SOL_SOCKET, SO_LINGER, &lin, sizeof lin);
  close(cfd);
  return (void *)1;
}

static char g_http_captured[2048];

static void *capture_http_req4(void *arg) {
  int port = *(int *)arg;
  int fd, cfd;
  int one = 1;
  struct sockaddr_in addr;
  const char *resp = "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nok";
  ssize_t n;
  memset(g_http_captured, 0, sizeof g_http_captured);
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
  n = read(cfd, g_http_captured, sizeof g_http_captured - 1);
  if (n > 0)
    g_http_captured[n] = '\0';
  if (write(cfd, resp, strlen(resp)) < 0) {
    close(cfd);
    return NULL;
  }
  close(cfd);
  return (void *)1;
}

static void *capture_http_req6(void *arg) {
  int port = *(int *)arg;
  int fd, cfd;
  int one = 1;
  struct sockaddr_in6 addr;
  const char *resp = "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nok";
  ssize_t n;
  memset(g_http_captured, 0, sizeof g_http_captured);
  fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0)
    return NULL;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef IPV6_V6ONLY
  setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
#endif
  memset(&addr, 0, sizeof addr);
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons((uint16_t)port);
  if (inet_pton(AF_INET6, "::1", &addr.sin6_addr) != 1 ||
      bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 1) != 0) {
    close(fd);
    return NULL;
  }
  cfd = accept(fd, NULL, NULL);
  close(fd);
  if (cfd < 0)
    return NULL;
  n = read(cfd, g_http_captured, sizeof g_http_captured - 1);
  if (n > 0)
    g_http_captured[n] = '\0';
  if (write(cfd, resp, strlen(resp)) < 0) {
    close(cfd);
    return NULL;
  }
  close(cfd);
  return (void *)1;
}

typedef struct HttpRespArg {
  int port;
  const char *resp;
} HttpRespArg;

static void *ipv4_http_resp(void *arg) {
  HttpRespArg *a = (HttpRespArg *)arg;
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
  addr.sin_port = htons((uint16_t)a->port);
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
  if (write(cfd, a->resp, strlen(a->resp)) < 0) {
    close(cfd);
    return NULL;
  }
  close(cfd);
  return (void *)1;
}

static int try_bind_v4(int port) {
  int fd;
  int one = 1;
  struct sockaddr_in addr;
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
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
  return pure_drop(NULL);
}

static SzIo *after_ref_get(void *value, void *env) {
  (void)env;
  SzString *s = (SzString *)value;
  assert(s && strcmp(sz_string_cstr(s), "b") == 0);
  sz_release(value);
  return pure_drop(NULL);
}

static SzIo *after_ref_set(void *value, void *env) {
  (void)value;
  SzRef *r = (SzRef *)env;
  return fm_drop(sz_ref_get(r), after_ref_get, r);
}

static void *inc_i64(void *head, void *env) {
  (void)env;
  return sz_box_i64(sz_unbox_i64(head) + 1);
}

static void *fold_add_i64(void *pack, void *env) {
  SzPair *p = (SzPair *)pack;
  (void)env;
  return sz_box_i64(sz_unbox_i64(p->left) + sz_unbox_i64(p->right));
}

static void *fold_concat_pair(void *pack, void *env) {
  SzPair *p = (SzPair *)pack;
  (void)env;
  return sz_string_concat((SzString *)p->left, (SzString *)p->right);
}

static SzIo *after_ref(void *value, void *env) {
  (void)env;
  SzRef *r = (SzRef *)value;
  SzIo *io = fm_drop(sz_ref_set_cstr(r, "b"), after_ref_set, r);
  sz_release(r);
  return io;
}

static SzIo *pct_take_after_both(void *pair, void *q) {
  sz_release(pair);
  return sz_queue_take((SzQueue *)q);
}

static SzIo *pct_offer_lr_then_take(SzQueue *q) {
  return fm_drop(
      both_drop(sz_queue_offer_cstr(q, "L"), sz_queue_offer_cstr(q, "R")),
      pct_take_after_both, q);
}

static SzIo *fiber_join_cont(void *fiber, void *env) {
  (void)env;
  return sz_fiber_join(fiber);
}

static SzIo *after_fork_ignore(void *fiber, void *env) {
  (void)fiber;
  (void)env;
  return pure_drop(NULL);
}

static SzIo *fiber_join_recover(void *ignored, void *fiber) {
  (void)ignored;
  return handle_drop(sz_fiber_join(fiber), recover_unit, NULL);
}

static SzIo *fiber_interrupt_then_join(void *fiber, void *env) {
  (void)env;
  return fm_drop(sz_fiber_interrupt(fiber), fiber_join_recover, fiber);
}

static SzIo *recover_any_unit(SzError *err, void *env) {
  (void)env;
  sz_error_free(err);
  return pure_drop(NULL);
}

static SzIo *cancel_handoff_then_join(void *fiber, void *q) {
  SzString *x = sz_string_from_cstr("x");
  int hit = sz_queue_cancel_ready_handoff((SzQueue *)q, x);
  sz_release(x);
  assert(hit == 1);
  return handle_drop(sz_fiber_join(fiber), recover_unit, NULL);
}

static SzIo *after_offer_interrupt_join(void *ignored, void *fiber) {
  (void)ignored;
  return fiber_interrupt_then_join(fiber, NULL);
}

static SzIo *fork_take_offer_interrupt(void *fiber, void *q) {
  return fm_drop(sz_queue_offer_cstr((SzQueue *)q, "x"),
                 after_offer_interrupt_join, fiber);
}

static int value_is_cstr(void *v, const char *want) {
  return v && want && strcmp(sz_string_cstr((SzString *)v), want) == 0;
}

static void assert_queue_holds_x_if_take_lost(SzQueue *q, int take_won) {
  SzIoResult t;
  if (take_won) {
    assert(sz_queue_size(q) == 0);
    return;
  }
  assert(sz_queue_size(q) == 1);
  t = sz_io_unsafe_run(sz_queue_take(q));
  assert(t.ok);
  assert(value_is_cstr(t.value, "x"));
  sz_release(t.value);
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
  return pure_drop(value);
}

static SzIo *always_fail_cont(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_io_fail_cstr("always");
}

static int64_t keep_not_b(void *head, void *env) {
  (void)env;
  return strcmp(sz_string_cstr((SzString *)head), "b") != 0;
}

static int64_t keep_a(void *head, void *env) {
  (void)env;
  return strcmp(sz_string_cstr((SzString *)head), "a") == 0;
}

static void *str_len_key(void *head, void *env) {
  (void)env;
  return sz_box_i64(sz_string_len((SzString *)head));
}

static void *str_take1(void *head, void *env) {
  (void)env;
  return sz_string_take((SzString *)head, 1);
}

static int64_t keep_none(void *head, void *env) {
  (void)head;
  (void)env;
  return 0;
}

static int64_t keep_true(void *head, void *env) {
  (void)head;
  (void)env;
  return 1;
}

static void *map_bang(void *head, void *env) {
  SzString *bang;
  SzString *out;
  (void)env;
  bang = sz_string_from_cstr("!");
  out = sz_string_concat((SzString *)head, bang);
  sz_release(bang);
  return out;
}

static void *map_singleton(void *head, void *env) {
  (void)env;
  return sz_list_cons(head, NULL);
}

static void *map_id(void *head, void *env) {
  (void)env;
  sz_retain(head);
  return head;
}

static void *map_int_to_str(void *head, void *env) {
  (void)env;
  return sz_string_from_int(sz_unbox_i64(head));
}

static void *map_dup_list(void *head, void *env) {
  (void)env;
  return sz_list_cons(head, sz_list_cons(head, sz_list_nil()));
}

static void *map_empty_list(void *head, void *env) {
  (void)head;
  (void)env;
  return sz_list_nil();
}

static SzIo *after_sleep_tag(void *value, void *env) {
  (void)value;
  return pure_drop(env);
}

typedef struct {
  int n;
  int strings;
  int lists;
  int raw;
  int maps;
  int boxes;
  size_t bytes;
} LiveVisit;

static void live_visit_acc(void *ptr, uint32_t kind, size_t bytes, uint32_t rc,
                           void *ctx) {
  LiveVisit *v = (LiveVisit *)ctx;
  (void)ptr;
  (void)rc;
  v->n += 1;
  v->bytes += bytes;
  if (kind == SZ_RC_STRING)
    v->strings += 1;
  else if (kind == SZ_RC_LIST)
    v->lists += 1;
  else if (kind == SZ_RC_RAW)
    v->raw += 1;
  else if (kind == SZ_RC_MAP)
    v->maps += 1;
  else if (kind == SZ_RC_BOX)
    v->boxes += 1;
}

static size_t read_fd_all(int fd, char *buf, size_t cap) {
  size_t n = 0;
  if (!buf || cap == 0)
    return 0;
  while (n + 1 < cap) {
    ssize_t r = read(fd, buf + n, cap - 1 - n);
    if (r <= 0)
      break;
    n += (size_t)r;
  }
  buf[n] = '\0';
  return n;
}

static int wait_aborted(pid_t pid) {
  int st = 0;
  if (waitpid(pid, &st, 0) != pid)
    return 0;
  return WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT;
}

static void write_text(const char *path, const char *text) {
  FILE *f = fopen(path, "w");
  assert(f);
  fputs(text, f);
  fclose(f);
}

static char *slurp_path(const char *path) {
  FILE *f = fopen(path, "r");
  char *buf;
  long n;
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0)
    n = 0;
  buf = (char *)malloc((size_t)n + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  n = (long)fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[n] = '\0';
  return buf;
}

static int64_t session_ok(void) { return 1; }

static int64_t session_fail(void) { return 0; }

static SzVerdict *verify_len_ok(void *tl) {
  if (sz_timeline_len(tl) >= 1)
    return sz_verdict_ok();
  return sz_verdict_fail(-1, "timeline empty");
}

static SzVerdict *verify_never(void *tl) {
  (void)tl;
  return sz_verdict_fail(0, "never holds");
}

static int64_t verify_pred_true(void *head, void *env) {
  (void)head;
  (void)env;
  return 1;
}

static int64_t verify_pred_skip_second(void *head, void *env) {
  (void)env;
  return sz_unbox_i64(head) < 1;
}

/* Verdict.every fails at the first state where the pred is false. */
static SzVerdict *verify_every_second(void *tl) {
  return sz_verdict_every(tl, (void *)verify_pred_skip_second, NULL);
}

/* Kit combo that must stay valid: and(ok, or(ok, fail)). */
static SzVerdict *verify_kit_combo(void *tl) {
  SzVerdict *every = sz_verdict_every(tl, (void *)verify_pred_true, NULL);
  SzVerdict *any = sz_verdict_any(tl, (void *)verify_pred_true, NULL);
  SzVerdict *fallback = sz_verdict_fail(0, "unused");
  return sz_verdict_and(every, sz_verdict_or(any, fallback));
}

/* Relation claim: final int signal 0 must match across the pair. */
static SzVerdict *rel_final_int_eq(void *a, void *b) {
  int64_t ai = sz_timeline_len(a) - 1;
  int64_t bi = sz_timeline_len(b) - 1;
  if (sz_timeline_signal_int(a, ai, 0) == sz_timeline_signal_int(b, bi, 0))
    return sz_verdict_ok();
  return sz_verdict_fail(0, "final count differs across schedules");
}

static void json_expect_err(const char *src) {
  SzString *s = sz_string_from_cstr(src);
  SzAdt *r = sz_json_parse(s);
  assert(sz_adt_tag(r) == 0);
  sz_release(r);
  sz_release(s);
}

static SzAdt *json_expect_ok(const char *src) {
  SzString *s = sz_string_from_cstr(src);
  SzAdt *r = sz_json_parse(s);
  assert(sz_adt_tag(r) == 1);
  sz_release(s);
  return r;
}

static void json_expect_roundtrip(const char *src) {
  SzAdt *pr = json_expect_ok(src);
  SzAdt *sr = sz_json_stringify((SzAdt *)sz_adt_payload(pr));
  SzString *out;
  assert(sz_adt_tag(sr) == 1);
  out = (SzString *)sz_adt_payload(sr);
  assert(strcmp(sz_string_cstr(out), src) == 0);
  sz_release(sr);
  sz_release(pr);
}

static SzAdt *json_float_of(double x) {
  int64_t bits = 0;
  void *box;
  SzAdt *jf;
  memcpy(&bits, &x, sizeof bits);
  box = sz_box_i64(bits);
  jf = sz_adt_new(3, box);
  sz_release(box);
  return jf;
}

static void json_expect_stringify_err(SzAdt *j) {
  SzAdt *sr = sz_json_stringify(j);
  assert(sz_adt_tag(sr) == 0);
  sz_release(sr);
  sz_release(j);
}

int main(void) {
  /* delay */
  delay_calls = 0;
  SzIo *d = sz_io_delay(delay_inc, NULL);
  SzIoResult r = sz_io_unsafe_run(d);
  assert(r.ok);
  assert((intptr_t)r.value == 42);
  assert(delay_calls == 1);

  /* Peak tracks high-water live bytes; reset keeps live and lowers peak. */
  {
    size_t live_bytes = 0, live_count = 0;
    size_t peak0, peak1;
    void *p;
    sz_alloc_stats(&live_bytes, &live_count);
    sz_alloc_reset_stats();
    peak0 = sz_alloc_peak_bytes();
    assert(peak0 == live_bytes);
    p = sz_alloc(4096);
    peak1 = sz_alloc_peak_bytes();
    assert(peak1 >= peak0 + 4096);
    sz_free(p);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(sz_alloc_peak_bytes() == peak1);
    sz_alloc_reset_stats();
    assert(sz_alloc_peak_bytes() == live_bytes);
  }

  /* Heap census tracks RC kinds and dump delta. */
  {
    size_t kb0 = 0, kc0 = 0, kb1 = 0, kc1 = 0;
    size_t live_bytes = 0, live_count = 0;
    int64_t db = 0, dc = 0;
    char heap[1536];
    void *raw;
    SzString *s;
    SzList *xs;
    void *box;
    SzMap *m;
    SzIo *io;
    SzStream *st;
    SzError *err;
    SzRef *ref;
    SzQueue *q;
    SzDeferred *d;
    SzEither *ei;
    SzPair *pair;
    SzAdt *adt;
    SzLangResource *lr;
    int i;

    assert(strcmp(sz_alloc_kind_name(SZ_RC_RAW), "raw") == 0);
    assert(strcmp(sz_alloc_kind_name(SZ_RC_STRING), "string") == 0);
    assert(strcmp(sz_alloc_kind_name(SZ_RC_PAIR), "pair") == 0);
    assert(strcmp(sz_alloc_kind_name(99), "raw") == 0);

    sz_alloc_kind_stats(SZ_RC_RAW, &kb0, &kc0);
    sz_alloc_mark();
    raw = sz_alloc(64);
    sz_alloc_kind_stats(SZ_RC_RAW, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    assert(kb1 >= kb0 + 64);
    sz_alloc_delta(&db, &dc);
    assert(dc == 1);
    assert(db >= 64);
    sz_alloc_format_heap(heap, sizeof heap, 0);
    assert(strstr(heap, "live_bytes=") != NULL);
    assert(strstr(heap, "delta_count=1") != NULL);
    assert(strstr(heap, "delta_bytes=") != NULL);
    for (i = 0; i < SZ_RC_KIND_COUNT; i++) {
      char line[64];
      snprintf(line, sizeof line, "%s=", sz_alloc_kind_name((uint32_t)i));
      assert(strstr(heap, line) != NULL);
    }
    sz_alloc_format_heap(heap, sizeof heap, 1);
    sz_free(raw);
    sz_alloc_delta(&db, &dc);
    assert(dc == -1);
    sz_alloc_format_heap(heap, sizeof heap, 0);
    assert(strstr(heap, "delta_count=-1") != NULL);

    sz_alloc_kind_stats(SZ_RC_STRING, &kb0, &kc0);
    sz_alloc_stats(&live_bytes, &live_count);
    s = sz_string_from_cstr("census");
    sz_alloc_kind_stats(SZ_RC_STRING, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(s);
    sz_alloc_kind_stats(SZ_RC_STRING, &kb1, &kc1);
    assert(kc1 == kc0);
    sz_alloc_stats(&kb1, &kc1);
    assert(kc1 == live_count);
    assert(kb1 == live_bytes);

    sz_alloc_kind_stats(SZ_RC_LIST, &kb0, &kc0);
    sz_alloc_kind_stats(SZ_RC_STRING, &kb1, &kc1);
    s = sz_string_from_cstr("h");
    xs = sz_list_cons(s, NULL);
    sz_release(s);
    sz_alloc_kind_stats(SZ_RC_LIST, &live_bytes, &live_count);
    assert(live_count == kc0 + 1);
    sz_alloc_kind_stats(SZ_RC_STRING, &live_bytes, &live_count);
    assert(live_count == kc1 + 1);
    sz_release(xs);
    sz_alloc_kind_stats(SZ_RC_LIST, &live_bytes, &live_count);
    assert(live_count == kc0);
    sz_alloc_kind_stats(SZ_RC_STRING, &live_bytes, &live_count);
    assert(live_count == kc1);

    sz_alloc_kind_stats(SZ_RC_BOX, &kb0, &kc0);
    box = sz_box_i64(7);
    sz_alloc_kind_stats(SZ_RC_BOX, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(box);
    sz_alloc_kind_stats(SZ_RC_BOX, &kb1, &kc1);
    assert(kc1 == kc0);

    sz_alloc_kind_stats(SZ_RC_MAP, &kb0, &kc0);
    s = sz_string_from_cstr("k");
    box = sz_box_i64(1);
    m = sz_map_set(NULL, s, box, 1);
    sz_release(s);
    sz_release(box);
    sz_alloc_kind_stats(SZ_RC_MAP, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(m);
    sz_alloc_kind_stats(SZ_RC_MAP, &kb1, &kc1);
    assert(kc1 == kc0);

    sz_alloc_kind_stats(SZ_RC_IO, &kb0, &kc0);
    io = sz_io_pure(NULL);
    sz_alloc_kind_stats(SZ_RC_IO, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(io);
    sz_alloc_kind_stats(SZ_RC_IO, &kb1, &kc1);
    assert(kc1 == kc0);

    sz_alloc_kind_stats(SZ_RC_STREAM, &kb0, &kc0);
    s = sz_string_from_cstr("e");
    st = sz_stream_emit(s);
    sz_release(s);
    sz_alloc_kind_stats(SZ_RC_STREAM, &kb1, &kc1);
    assert(kc1 == kc0 + 2);
    sz_release(st);
    sz_alloc_kind_stats(SZ_RC_STREAM, &kb1, &kc1);
    assert(kc1 == kc0);

    sz_alloc_kind_stats(SZ_RC_ERROR, &kb0, &kc0);
    err = sz_error_new(1, "boom");
    sz_alloc_kind_stats(SZ_RC_ERROR, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(err);
    sz_alloc_kind_stats(SZ_RC_ERROR, &kb1, &kc1);
    assert(kc1 == kc0);

    sz_alloc_kind_stats(SZ_RC_REF, &kb0, &kc0);
    s = sz_string_from_cstr("r");
    ref = sz_ref_make(s);
    sz_release(s);
    sz_alloc_kind_stats(SZ_RC_REF, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(ref);
    sz_alloc_kind_stats(SZ_RC_REF, &kb1, &kc1);
    assert(kc1 == kc0);

    sz_alloc_kind_stats(SZ_RC_QUEUE, &kb0, &kc0);
    q = sz_queue_make();
    sz_alloc_kind_stats(SZ_RC_QUEUE, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(q);
    sz_alloc_kind_stats(SZ_RC_QUEUE, &kb1, &kc1);
    assert(kc1 == kc0);

    sz_alloc_kind_stats(SZ_RC_DEFERRED, &kb0, &kc0);
    d = sz_deferred_make();
    sz_alloc_kind_stats(SZ_RC_DEFERRED, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(d);
    sz_alloc_kind_stats(SZ_RC_DEFERRED, &kb1, &kc1);
    assert(kc1 == kc0);

    sz_alloc_kind_stats(SZ_RC_EITHER, &kb0, &kc0);
    s = sz_string_from_cstr("ok");
    ei = sz_either_right(s);
    sz_release(s);
    sz_alloc_kind_stats(SZ_RC_EITHER, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(ei);
    sz_alloc_kind_stats(SZ_RC_EITHER, &kb1, &kc1);
    assert(kc1 == kc0);

    sz_alloc_kind_stats(SZ_RC_PAIR, &kb0, &kc0);
    s = sz_string_from_cstr("l");
    box = sz_box_i64(2);
    pair = sz_pair_new(s, box);
    sz_release(s);
    sz_release(box);
    sz_alloc_kind_stats(SZ_RC_PAIR, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(pair);
    sz_alloc_kind_stats(SZ_RC_PAIR, &kb1, &kc1);
    assert(kc1 == kc0);

    sz_alloc_kind_stats(SZ_RC_ADT, &kb0, &kc0);
    s = sz_string_from_cstr("p");
    adt = sz_adt_new(1, s);
    sz_release(s);
    sz_alloc_kind_stats(SZ_RC_ADT, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(adt);
    sz_alloc_kind_stats(SZ_RC_ADT, &kb1, &kc1);
    assert(kc1 == kc0);

    sz_alloc_kind_stats(SZ_RC_RESOURCE, &kb0, &kc0);
    io = pure_drop(sz_string_from_cstr("a"));
    lr = sz_lang_resource_make(io, lang_release, NULL);
    sz_release(io);
    sz_alloc_kind_stats(SZ_RC_RESOURCE, &kb1, &kc1);
    assert(kc1 == kc0 + 1);
    sz_release(lr);
    sz_alloc_kind_stats(SZ_RC_RESOURCE, &kb1, &kc1);
    assert(kc1 == kc0);
  }

  /* Live list walks remaining blocks; panic dumps them and sweeps. */
  {
    size_t live_bytes = 0, live_count = 0;
    size_t kb0 = 0, kc0 = 0, kb1 = 0, kc1 = 0;
    LiveVisit before, after;
    char live[2048];
    char panic[4096];
    void *raw;
    SzString *s;
    SzList *xs;
    void *box;
    SzMap *m;
    SzError *err;
    SzAdt *adt;
    int i;
    pid_t pid;
    int fds[2];
    char child_err[8192];
    const char *panic_path = "/tmp/scuzz_panic.dump";
    char *dumped;

    assert(sz_alloc_format_live(NULL, 16, 4) == 0);
    assert(sz_alloc_format_live(live, 0, 4) == 0);
    assert(sz_alloc_format_panic(NULL, 16, "x") == 0);
    assert(sz_alloc_format_panic(live, 0, "x") == 0);
    sz_alloc_walk(NULL, NULL);
    sz_alloc_set_panic_dump(NULL);
    sz_alloc_set_panic_dump("");
    assert(sz_alloc_panic_dump_path() == NULL);
    sz_alloc_set_panic_dump("/tmp/scuzz_panic_path.dump");
    assert(strcmp(sz_alloc_panic_dump_path(), "/tmp/scuzz_panic_path.dump") == 0);
    sz_alloc_set_panic_dump(NULL);
    assert(sz_alloc_panic_dump_path() == NULL);

    memset(&before, 0, sizeof before);
    sz_alloc_walk(live_visit_acc, &before);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(before.n == (int)live_count);
    assert(before.bytes == live_bytes);

    sz_alloc_kind_stats(SZ_RC_STRING, &kb0, &kc0);
    s = sz_string_from_cstr("live-row");
    xs = sz_list_cons(s, NULL);
    sz_release(s);
    raw = sz_alloc(48);
    box = sz_box_i64(9);
    {
      SzString *key = sz_string_from_cstr("k");
      SzString *payload = sz_string_from_cstr("p");
      m = sz_map_set(NULL, key, box, 1);
      sz_release(key);
      adt = sz_adt_new(3, payload);
      sz_release(payload);
    }
    sz_release(box);
    err = sz_error_new(2, "e");

    memset(&after, 0, sizeof after);
    sz_alloc_walk(live_visit_acc, &after);
    assert(after.n > before.n);
    assert(after.strings > before.strings);
    assert(after.lists == before.lists + 1);
    assert(after.raw >= before.raw + 1);
    assert(after.boxes >= before.boxes + 1);
    assert(after.maps == before.maps + 1);

    sz_alloc_format_live(live, sizeof live, -1);
    assert(strstr(live, "string rc=") != NULL);
    assert(strstr(live, "list rc=") != NULL);
    assert(strstr(live, "raw rc=0 bytes=") != NULL);
    assert(strstr(live, "box rc=") != NULL);
    assert(strstr(live, "map rc=") != NULL);
    assert(strstr(live, "error rc=") != NULL);
    assert(strstr(live, "adt rc=") != NULL);
    assert(strstr(live, "truncated=") == NULL);

    sz_alloc_format_live(live, sizeof live, 1);
    assert(strstr(live, "truncated=") != NULL);

    sz_alloc_format_live(live, sizeof live, 0);
    assert(strstr(live, "truncated=") != NULL);
    assert(strstr(live, "string rc=") == NULL);

    sz_alloc_format_panic(panic, sizeof panic, "probe");
    assert(strstr(panic, "scuzz panic: probe") != NULL);
    assert(strstr(panic, "[heap]") != NULL);
    assert(strstr(panic, "live_bytes=") != NULL);
    assert(strstr(panic, "[live]") != NULL);
    assert(strstr(panic, "string rc=") != NULL);
    sz_alloc_format_panic(panic, sizeof panic, NULL);
    assert(strstr(panic, "scuzz panic: (null)") != NULL);

    sz_free(raw);
    sz_release(xs);
    sz_release(m);
    sz_release(err);
    sz_release(adt);
    sz_alloc_kind_stats(SZ_RC_STRING, &kb1, &kc1);
    assert(kc1 == kc0);

    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      s = sz_string_from_cstr("sweep-child");
      xs = sz_list_cons(s, NULL);
      sz_release(s);
      raw = sz_alloc(32);
      (void)raw;
      (void)xs;
      sz_alloc_sweep();
      sz_alloc_stats(&live_bytes, &live_count);
      _exit(live_count == 0 && live_bytes == 0 ? 0 : 1);
    }
    {
      int st = 0;
      assert(waitpid(pid, &st, 0) == pid);
      assert(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }

    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      sz_alloc_sweep();
      sz_alloc_sweep();
      sz_alloc_stats(&live_bytes, &live_count);
      _exit(live_count == 0 ? 0 : 1);
    }
    {
      int st = 0;
      assert(waitpid(pid, &st, 0) == pid);
      assert(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }

    assert(pipe(fds) == 0);
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      dup2(fds[1], STDERR_FILENO);
      close(fds[0]);
      close(fds[1]);
      s = sz_string_from_cstr("panic-live");
      (void)s;
      sz_panic("boom");
    }
    close(fds[1]);
    read_fd_all(fds[0], child_err, sizeof child_err);
    close(fds[0]);
    assert(wait_aborted(pid));
    assert(strstr(child_err, "scuzz panic: boom") != NULL);
    assert(strstr(child_err, "[heap]") != NULL);
    assert(strstr(child_err, "[live]") != NULL);
    assert(strstr(child_err, "string rc=") != NULL);

    remove(panic_path);
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      freopen("/dev/null", "w", stderr);
      sz_alloc_set_panic_dump(panic_path);
      s = sz_string_from_cstr("file-live");
      xs = sz_list_cons(s, NULL);
      sz_release(s);
      (void)xs;
      sz_panic("file boom");
    }
    assert(wait_aborted(pid));
    dumped = slurp_path(panic_path);
    assert(dumped);
    assert(strstr(dumped, "scuzz panic: file boom") != NULL);
    assert(strstr(dumped, "[heap]") != NULL);
    assert(strstr(dumped, "[live]") != NULL);
    assert(strstr(dumped, "list rc=") != NULL);
    free(dumped);
    remove(panic_path);

    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      freopen("/dev/null", "w", stderr);
      setenv("SCUZZ_PANIC_DUMP", panic_path, 1);
      sz_alloc_set_panic_dump(NULL);
      sz_panic("env boom");
    }
    assert(wait_aborted(pid));
    dumped = slurp_path(panic_path);
    assert(dumped);
    assert(strstr(dumped, "scuzz panic: env boom") != NULL);
    assert(strstr(dumped, "[live]") != NULL);
    free(dumped);
    remove(panic_path);

    /* Nested RC graph still sweeps to zero. */
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      SzString *a = sz_string_from_cstr("a");
      SzString *b = sz_string_from_cstr("b");
      SzList *tail = sz_list_cons(b, NULL);
      SzList *head = sz_list_cons(a, tail);
      SzMap *mm;
      SzEither *ei;
      SzPair *pair;
      SzRef *ref;
      SzQueue *q;
      SzDeferred *d;
      SzIo *io;
      sz_release(a);
      sz_release(b);
      sz_release(tail);
      mm = sz_map_set(NULL, sz_string_from_cstr("mk"), sz_box_i64(1), 1);
      ei = sz_either_right(sz_string_from_cstr("ok"));
      pair = sz_pair_new(sz_string_from_cstr("l"), sz_box_i64(2));
      ref = sz_ref_make(sz_string_from_cstr("r"));
      q = sz_queue_make();
      d = sz_deferred_make();
      io = sz_io_pure(sz_string_from_cstr("io"));
      (void)head;
      (void)mm;
      (void)ei;
      (void)pair;
      (void)ref;
      (void)q;
      (void)d;
      (void)io;
      sz_alloc_sweep();
      sz_alloc_stats(&live_bytes, &live_count);
      for (i = 0; i < SZ_RC_KIND_COUNT; i++) {
        size_t kb = 0, kc = 0;
        sz_alloc_kind_stats((uint32_t)i, &kb, &kc);
        if (kc != 0 || kb != 0)
          _exit(2);
      }
      _exit(live_count == 0 && live_bytes == 0 ? 0 : 1);
    }
    {
      int st = 0;
      assert(waitpid(pid, &st, 0) == pid);
      assert(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }
  }

  /* Leftover IO.pure payloads drop on last-use / free. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *s;
    SzIo *io;

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(pure_drop(sz_string_from_cstr("ok")));
    assert(r.ok);
    assert(r.value && strcmp(sz_string_cstr((SzString *)r.value), "ok") == 0);
    sz_release(r.value);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
    assert(sz_alloc_peak_bytes() >= live_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    s = sz_string_from_cstr("drop");
    io = sz_io_pure(s);
    sz_release(s);
    sz_release(io);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Leftover delay env drops on last-use / free. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzIo *io;
    SzString *s;

    sz_alloc_stats(&base_bytes, &base_count);
    io = sz_ref_of(sz_string_from_cstr("v"));
    sz_release(io);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    s = sz_string_from_cstr("env");
    io = sz_io_delay(delay_inc, s);
    sz_release(s);
    sz_release(io);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(sz_ref_of(sz_string_from_cstr("run")));
    assert(r.ok);
    sz_release(r.value);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *s0 = sz_string_from_cstr("a");
      SzRef *ref = sz_ref_make(s0);
      sz_release(s0);
      s = sz_string_from_cstr("b");
      io = sz_ref_set(ref, s);
      sz_release(s);
      sz_release(io);
      sz_ref_free(ref);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      int64_t *p = (int64_t *)sz_alloc(sizeof(int64_t));
      *p = 7;
      io = sz_io_delay(delay_inc, p);
      sz_release(io);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* println + flatMap */
  SzIo *prog =
      fm_drop(sz_io_println_cstr("hello"), cont_println, NULL);
  r = sz_io_unsafe_run(prog);
  assert(r.ok);

  /* fail */
  r = sz_io_unsafe_run(fail_drop(sz_error_new(9, "boom")));
  assert(!r.ok);
  assert(r.error && strstr(sz_string_cstr(r.error->message), "boom"));
  sz_error_free(r.error);

  /* Leftover IO.fail errors drop on last-use / free. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzError *err;
    SzIo *io;

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(fail_drop(sz_error_new(9, "boom")));
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "boom"));
    sz_error_free(r.error);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    err = sz_error_new(1, "drop");
    io = sz_io_fail(err);
    sz_release(err);
    sz_release(io);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  {
    SzError *err = sz_error_new(1, "boom");
    SzString *a = sz_error_message(err);
    SzString *b = sz_error_message(err);
    assert(a == b);
    assert(a && strstr(sz_string_cstr(a), "boom") != NULL);
    sz_release(a);
    sz_release(b);
    sz_error_free(err);
  }

  /* handleErrorWith */
  r = sz_io_unsafe_run(handle_drop(sz_io_fail_cstr("boom"),
                                               recover_boom, NULL));
  assert(r.ok);

  /* attempt */
  r = sz_io_unsafe_run(attempt_drop(sz_io_fail_cstr("nope")));
  assert(r.ok);
  {
    SzEither *e = (SzEither *)r.value;
    assert(e && !e->is_right);
    assert(e->as.left && strstr(sz_string_cstr(e->as.left->message), "nope"));
    sz_either_free(e);
  }

  {
    SzError *err = sz_error_new(1, "nope");
    SzEither *e = sz_either_left(err);
    sz_release(err);
    assert(e && !e->is_right);
    assert(e->as.left && strstr(sz_string_cstr(e->as.left->message), "nope"));
    sz_either_free(e);
  }

  r = sz_io_unsafe_run(attempt_drop(pure_drop((void *)(intptr_t)3)));
  assert(r.ok);
  {
    SzEither *e = (SzEither *)r.value;
    assert(e && e->is_right && (intptr_t)e->as.right == 3);
    sz_either_free(e);
  }

  {
    SzString *s = sz_string_from_cstr("ok");
    SzEither *e = sz_either_right(s);
    sz_release(s);
    assert(e && e->is_right);
    assert(e->as.right && strcmp(sz_string_cstr((SzString *)e->as.right), "ok") == 0);
    sz_either_free(e);
  }

  /* Leftover Either payloads drop on free. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *s;
    SzAdt *adt;

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(attempt_drop(pure_drop(sz_string_from_cstr("ok"))));
    assert(r.ok);
    sz_either_free((SzEither *)r.value);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(attempt_drop(sz_io_fail_cstr("nope")));
    assert(r.ok);
    sz_either_free((SzEither *)r.value);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    s = sz_string_from_cstr("ok");
    {
      SzIo *inner = pure_drop(s);
      SzIo *att = sz_io_attempt_as_result(inner);
      sz_release(inner);
      r = sz_io_unsafe_run(att);
    }
    assert(r.ok);
    adt = (SzAdt *)r.value;
    assert(adt && sz_adt_tag(adt) == 1);
    sz_release(adt);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Resource.make / use (IO acquire + IO release) */
  SzLangResource *lr;
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;

    lang_released = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    lr = lang_make_tok();
    r = sz_io_unsafe_run(sz_lang_resource_use(lr, lang_use_ok, NULL));
    assert(r.ok);
    assert(lang_released == 1);
    sz_lang_resource_free(lr);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    lang_released = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    lr = lang_make_tok();
    r = sz_io_unsafe_run(sz_lang_resource_use(lr, lang_use_fail, NULL));
    assert(!r.ok);
    assert(lang_released == 1);
    sz_error_free(r.error);
    sz_lang_resource_free(lr);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Release IO success payload drops in lang_fin_free_ok. */
  {
    SzIo *acq;
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;

    sz_alloc_stats(&base_bytes, &base_count);
    acq = pure_drop(sz_string_from_cstr("tok"));
    lr = sz_lang_resource_make(acq, lang_release_pure_str, NULL);
    sz_release(acq);
    r = sz_io_unsafe_run(sz_lang_resource_use(lr, lang_use_ok, NULL));
    assert(r.ok);
    sz_lang_resource_free(lr);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Resource.use keeps a captured list env after the caller drops it. */
  {
    SzString *msg;
    SzList *env;
    SzIo *acq;
    SzIo *use_io;
    size_t base_count = 0, base_bytes = 0;
    size_t live_count = 0, live_bytes = 0;

    acq = pure_drop(sz_string_from_cstr("tok"));
    sz_alloc_stats(&base_bytes, &base_count);
    msg = sz_string_from_cstr("cap");
    env = sz_list_cons(msg, sz_list_nil());
    sz_release(msg);
    lr = sz_lang_resource_make(acq, lang_release, env);
    sz_release(env);
    sz_lang_resource_free(lr);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    sz_release(acq);

    lang_released = 0;
    msg = sz_string_from_cstr("cap");
    env = sz_list_cons(msg, sz_list_nil());
    sz_release(msg);
    lr = lang_make_tok();
    use_io = sz_lang_resource_use(lr, lang_use_println_env, env);
    sz_release(env);
    r = sz_io_unsafe_run(use_io);
    assert(r.ok);
    assert(lang_released == 1);
    sz_lang_resource_free(lr);
  }

  /* IO.ensure runs finalizer on success */
  ensured_flag = 0;
  r = sz_io_unsafe_run(
      ensure_drop(pure_drop((void *)(intptr_t)1),
                  sz_io_delay(ensure_mark_thunk, NULL)));
  assert(r.ok);
  assert((intptr_t)r.value == 1);
  assert(ensured_flag == 1);

  /* IO.ensure runs finalizer on failure */
  ensured_flag = 0;
  r = sz_io_unsafe_run(
      ensure_drop(sz_io_fail_cstr("boom"), sz_io_delay(ensure_mark_thunk, NULL)));
  assert(!r.ok);
  assert(ensured_flag == 1);
  sz_error_free(r.error);

  /* Inner success payload drops when the finalizer fails. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(
        ensure_drop(pure_drop(sz_string_from_cstr("x")), sz_io_fail_cstr("boom")));
    assert(!r.ok);
    sz_error_free(r.error);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Resource releases when cancelled as race loser (TestRuntime: both park, then short wins). */
  {
    sz_testrt_install();
    lang_released = 0;
    lr = lang_make_tok();
    r = sz_io_unsafe_run(
        race_drop(sz_lang_resource_use(lr, lang_use_sleep, NULL),
                   sz_io_sleep_ms(1)));
    assert(r.ok);
    assert(lang_released == 1);
    sz_lang_resource_free(lr);
    sz_testrt_reset();
  }

  /* IO.timeout: inner wins and keeps its value. */
  {
    sz_testrt_install();
    r = sz_io_unsafe_run(timeout_drop(50, pure_drop((void *)(intptr_t)7)));
    assert(r.ok);
    assert((intptr_t)r.value == 7);
    sz_testrt_reset();
  }

  /* IO.timeout: inner failure is not rewritten as timeout. */
  {
    sz_testrt_install();
    r = sz_io_unsafe_run(timeout_drop(50, sz_io_fail_cstr("boom")));
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "boom") != NULL);
    sz_error_free(r.error);
    sz_testrt_reset();
  }

  /* IO.timeout: sleep-fail wins; Resource release still runs. */
  {
    sz_testrt_install();
    lang_released = 0;
    lr = lang_make_tok();
    r = sz_io_unsafe_run(
        timeout_drop(1, sz_lang_resource_use(lr, lang_use_sleep, NULL)));
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "timeout") != NULL);
    assert(lang_released == 1);
    sz_error_free(r.error);
    sz_lang_resource_free(lr);
    sz_testrt_reset();
  }

  /* IO.repeatN: n extra runs after the first; last value wins. */
  delay_calls = 0;
  r = sz_io_unsafe_run(repeat_n_drop(2, sz_io_delay(delay_inc, NULL)));
  assert(r.ok);
  assert((intptr_t)r.value == 42);
  assert(delay_calls == 3);

  delay_calls = 0;
  r = sz_io_unsafe_run(repeat_n_drop(0, sz_io_delay(delay_inc, NULL)));
  assert(r.ok);
  assert(delay_calls == 1);

  delay_calls = 0;
  r = sz_io_unsafe_run(repeat_n_drop(-3, sz_io_delay(delay_inc, NULL)));
  assert(r.ok);
  assert(delay_calls == 1);

  delay_calls = 0;
  r = sz_io_unsafe_run(repeat_n_drop(5, sz_io_fail_cstr("boom")));
  assert(!r.ok);
  assert(delay_calls == 0);
  assert(r.error && strstr(sz_string_cstr(r.error->message), "boom") != NULL);
  sz_error_free(r.error);

  /* Repeat discards extra IO.pure retains; last value is the run result. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(repeat_n_drop(2, pure_drop(sz_string_from_cstr("rep"))));
    assert(r.ok);
    assert(r.value && strcmp(sz_string_cstr((SzString *)r.value), "rep") == 0);
    sz_release(r.value);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Shared kit delay (repeatN/retryN) borrows env. Loops must not free it. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *s;
    SzRef *ref;
    SzQueue *q;
    SzDeferred *def;
    int i;

    sz_alloc_stats(&base_bytes, &base_count);
    s = sz_string_from_cstr("v");
    ref = sz_ref_make(s);
    sz_release(s);
    r = sz_io_unsafe_run(repeat_n_drop(0, sz_ref_get(ref)));
    assert(r.ok);
    assert(r.value && strcmp(sz_string_cstr((SzString *)r.value), "v") == 0);
    sz_release(r.value);
    r = sz_io_unsafe_run(repeat_n_drop(2, sz_ref_get(ref)));
    assert(r.ok);
    sz_release(r.value);
    r = sz_io_unsafe_run(retry_n_drop(2, sz_ref_get(ref)));
    assert(r.ok);
    sz_release(r.value);
    r = sz_io_unsafe_run(repeat_n_drop(2, sz_ref_set(ref, ref->value)));
    assert(r.ok);
    assert(ref->value && strcmp(sz_string_cstr((SzString *)ref->value), "v") == 0);
    r = sz_io_unsafe_run(repeat_n_drop(0, sz_ref_of_cstr("of")));
    assert(r.ok);
    sz_release(r.value);
    sz_ref_free(ref);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    q = sz_queue_make();
    s = sz_string_from_cstr("x");
    r = sz_io_unsafe_run(repeat_n_drop(2, sz_queue_offer(q, s)));
    sz_release(s);
    assert(r.ok);
    assert(sz_queue_size(q) == 3);
    for (i = 0; i < 3; i++) {
      r = sz_io_unsafe_run(sz_queue_take(q));
      assert(r.ok);
      sz_release(r.value);
    }
    sz_queue_free(q);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    def = sz_deferred_make();
    s = sz_string_from_cstr("y");
    r = sz_io_unsafe_run(repeat_n_drop(2, sz_deferred_complete(def, s)));
    sz_release(s);
    assert(r.ok);
    r = sz_io_unsafe_run(sz_deferred_get(def));
    assert(r.ok);
    assert(r.value && strcmp(sz_string_cstr((SzString *)r.value), "y") == 0);
    sz_release(r.value);
    sz_deferred_free(def);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* IO.retryN: extra retries on failure; last success / last error. */
  retry_hits = 0;
  r = sz_io_unsafe_run(retry_n_drop(
      5, fm_drop(sz_io_delay(retry_count, NULL), retry_until_3, NULL)));
  assert(r.ok);
  assert((intptr_t)r.value == 3);
  assert(retry_hits == 3);

  retry_hits = 0;
  r = sz_io_unsafe_run(retry_n_drop(
      1, fm_drop(sz_io_delay(retry_count, NULL), always_fail_cont, NULL)));
  assert(!r.ok);
  assert(retry_hits == 2);
  assert(r.error && strstr(sz_string_cstr(r.error->message), "always") != NULL);
  sz_error_free(r.error);

  retry_hits = 0;
  r = sz_io_unsafe_run(retry_n_drop(
      0, fm_drop(sz_io_delay(retry_count, NULL), always_fail_cont, NULL)));
  assert(!r.ok);
  assert(retry_hits == 1);
  sz_error_free(r.error);

  retry_hits = 0;
  r = sz_io_unsafe_run(retry_n_drop(2, pure_drop((void *)(intptr_t)9)));
  assert(r.ok);
  assert((intptr_t)r.value == 9);
  assert(retry_hits == 0);

  /* Retry drops extra IO.fail retains; last error is the run result. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(retry_n_drop(2, sz_io_fail_cstr("boom")));
    assert(!r.ok);
    sz_error_free(r.error);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* IO.forever: race loser when a sibling sleeper wins (TestRuntime). */
  {
    SzIo *inner;
    SzIo *loop;
    sz_testrt_install();
    inner = sz_io_sleep_ms(1);
    loop = sz_io_forever(inner);
    sz_release(inner);
    r = sz_io_unsafe_run(race_drop(loop, sz_io_sleep_ms(5)));
    assert(r.ok);
    sz_testrt_reset();
  }

  /* IO.forever cancelled through Fiber.interrupt. Resource release still runs. */
  {
    SzIo *body;
    SzIo *loop;
    sz_testrt_install();
    lang_released = 0;
    lr = lang_make_tok();
    body = sz_lang_resource_use(lr, lang_use_sleep, NULL);
    loop = sz_io_forever(body);
    sz_release(body);
    r = sz_io_unsafe_run(fm_drop(
        fork_drop(loop), fiber_interrupt_then_join, NULL));
    assert(r.ok);
    assert(lang_released == 1);
    sz_lang_resource_free(lr);
    sz_testrt_reset();
  }

  /* IO.foreach collects mapper results. Empty is nil. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_nil()));
    SzList *out;
    SzIo *io = sz_io_foreach(xs, stream_bang, NULL);
    r = sz_io_unsafe_run(io);
    assert(r.ok);
    out = (SzList *)r.value;
    assert(sz_list_len(out) == 2);
    assert(strcmp(sz_string_cstr((SzString *)out->head), "a!") == 0);
    assert(out->tail &&
           strcmp(sz_string_cstr((SzString *)out->tail->head), "b!") == 0);
    sz_list_free(out);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
  }

  {
    SzIo *io = sz_io_foreach(NULL, stream_bang, NULL);
    r = sz_io_unsafe_run(io);
    assert(r.ok);
    assert(r.value == NULL);
  }

  /* IO.foreachDiscard walks every cell and yields Unit. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_nil()));
    foreach_hits = 0;
    r = sz_io_unsafe_run(sz_io_foreach_discard(xs, foreach_unit, NULL));
    assert(r.ok);
    assert(r.value == NULL);
    assert(foreach_hits == 2);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
  }

  /* Failure stops later cells and frees the pack. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_nil()));
    foreach_hits = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(sz_io_foreach(xs, foreach_fail_second, NULL));
    assert(!r.ok);
    sz_error_free(r.error);
    assert(foreach_hits == 2);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
  }

  /* IO.when / unless pick inner or Unit. Unused inner drops. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *s = sz_string_from_cstr("when-ok");
    SzIo *inner = sz_io_println(s);
    SzIo *io;
    sz_release(s);
    io = sz_io_when(1, inner);
    sz_release(inner);
    r = sz_io_unsafe_run(io);
    assert(r.ok);

    sz_alloc_stats(&base_bytes, &base_count);
    inner = sz_io_fail_cstr("when-skip");
    io = sz_io_when(0, inner);
    sz_release(inner);
    r = sz_io_unsafe_run(io);
    assert(r.ok);
    assert(r.value == NULL);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    inner = sz_io_println_cstr("unless-ok");
    io = sz_io_unless(0, inner);
    sz_release(inner);
    r = sz_io_unsafe_run(io);
    assert(r.ok);

    inner = sz_io_fail_cstr("unless-skip");
    io = sz_io_unless(1, inner);
    sz_release(inner);
    r = sz_io_unsafe_run(io);
    assert(r.ok);
  }

  /* Fiber.fork/join: child value is the join result. */
  {
    SzIo *child = pure_drop((void *)(intptr_t)42);
    r = sz_io_unsafe_run(
        fm_drop(fork_drop(child), fiber_join_cont, NULL));
    assert(r.ok);
    assert((intptr_t)r.value == 42);
  }

  /* Join retains a distinct RC. Fiber free drops the slot. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(fm_drop(
        fork_drop(pure_drop(sz_string_from_cstr("join"))), fiber_join_cont,
        NULL));
    assert(r.ok);
    assert(r.value && strcmp(sz_string_cstr((SzString *)r.value), "join") == 0);
    sz_release(r.value);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(fm_drop(
        fork_drop(pure_drop(sz_string_from_cstr("orphan"))), after_fork_ignore,
        NULL));
    assert(r.ok);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Fiber.interrupt cancels a sleeper; join fails; Resource release runs. */
  {
    sz_testrt_install();
    lang_released = 0;
    lr = lang_make_tok();
    r = sz_io_unsafe_run(fm_drop(
        fork_drop(sz_lang_resource_use(lr, lang_use_sleep, NULL)),
        fiber_interrupt_then_join, NULL));
    assert(r.ok);
    assert(lang_released == 1);
    sz_lang_resource_free(lr);
    sz_testrt_reset();
  }

  /* Cancel Resource.use after acquire, before the ensure step. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;

    lang_released = 0;
    lang_use_stepped = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    lr = lang_make_tok();
    r = sz_io_unsafe_run(fm_drop(
        fork_drop(sz_lang_resource_use(lr, lang_use_step, NULL)),
        fiber_interrupt_direct, NULL));
    assert(r.ok);
    assert(lang_use_stepped == 0);
    assert(lang_released == 1);
    sz_lang_resource_free(lr);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Cancel Resource.use during a parking acquire. Release must not run. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzIo *acq;

    sz_testrt_install();
    lang_released = 0;
    lang_used = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    acq = sz_io_sleep_ms(100);
    lr = sz_lang_resource_make(acq, lang_release, NULL);
    sz_release(acq);
    r = sz_io_unsafe_run(fm_drop(
        fork_drop(sz_lang_resource_use(lr, lang_use_mark, NULL)),
        fiber_interrupt_direct, NULL));
    assert(r.ok);
    assert(lang_used == 0);
    assert(lang_released == 0);
    sz_lang_resource_free(lr);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
    sz_testrt_reset();
  }

  /* Unjoined fork is cancelled when the root completes (no long sleep). */
  {
    int64_t t0 = sz_clock_monotonic_ms_sync();
    int64_t t1;
    r = sz_io_unsafe_run(
        fm_drop(fork_drop(sz_io_sleep_ms(300)), after_fork_ignore, NULL));
    t1 = sz_clock_monotonic_ms_sync();
    assert(r.ok);
    assert(t1 - t0 < 80);
  }

  /* Ref */
  r = sz_io_unsafe_run(fm_drop(sz_ref_of_cstr("a"), after_ref, NULL));
  assert(r.ok);

  {
    SzString *s = sz_string_from_cstr("a");
    SzRef *ref = sz_ref_make(s);
    sz_release(s);
    assert(ref && strcmp(sz_string_cstr((SzString *)ref->value), "a") == 0);
    {
      SzString *n = sz_string_from_cstr("b");
      r = sz_io_unsafe_run(sz_ref_set(ref, n));
      sz_release(n);
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)ref->value), "b") == 0);
    }
    r = sz_io_unsafe_run(sz_ref_get(ref));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "b") == 0);
    sz_release(r.value);
    assert(strcmp(sz_string_cstr((SzString *)ref->value), "b") == 0);
    sz_ref_free(ref);
  }

  /* Ref[Int] + update */
  {
    void *b0 = sz_box_i64(1);
    SzRef *ref = sz_ref_make(b0);
    sz_release(b0);
    {
      void *b2 = sz_box_i64(2);
      r = sz_io_unsafe_run(sz_ref_set(ref, b2));
      sz_release(b2);
      assert(r.ok);
    }
    r = sz_io_unsafe_run(sz_ref_get(ref));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 2);
    sz_release(r.value);
    r = sz_io_unsafe_run(sz_ref_update(ref, inc_i64, NULL));
    assert(r.ok);
    assert(sz_unbox_i64(ref->value) == 3);
    r = sz_io_unsafe_run(sz_ref_update_and_get(ref, inc_i64, NULL));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 4);
    sz_release(r.value);
    assert(sz_unbox_i64(ref->value) == 4);
    sz_ref_free(ref);
  }

  /* Deferred */
  {
    SzDeferred *def = sz_deferred_make();
    r = sz_io_unsafe_run(sz_deferred_complete_cstr(def, "ok"));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_deferred_get(def));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "ok") == 0);
    sz_release(r.value);
    assert(strcmp(sz_string_cstr((SzString *)def->value), "ok") == 0);

    {
      SzDeferred *d2 = sz_deferred_make();
      SzString *s = sz_string_from_cstr("y");
      r = sz_io_unsafe_run(sz_deferred_complete(d2, s));
      sz_release(s);
      assert(r.ok);
      r = sz_io_unsafe_run(sz_deferred_get(d2));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "y") == 0);
      sz_release(r.value);
      assert(strcmp(sz_string_cstr((SzString *)d2->value), "y") == 0);
      {
        SzString *late = sz_string_from_cstr("z");
        r = sz_io_unsafe_run(sz_deferred_complete(d2, late));
        sz_release(late);
        assert(r.ok);
      }
      r = sz_io_unsafe_run(sz_deferred_get(d2));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "y") == 0);
      sz_release(r.value);
      assert(strcmp(sz_string_cstr((SzString *)d2->value), "y") == 0);
      sz_deferred_free(d2);
    }
    {
      SzDeferred *d3 = sz_deferred_make();
      SzError *err = sz_error_new(1, "nope");
      r = sz_io_unsafe_run(sz_deferred_fail(d3, err));
      sz_release(err);
      assert(r.ok);
      r = sz_io_unsafe_run(sz_deferred_get(d3));
      assert(!r.ok);
      assert(r.error &&
             strstr(sz_string_cstr(r.error->message), "nope") != NULL);
      sz_error_free(r.error);
      sz_deferred_free(d3);
    }
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
    sz_release(r.value);

    {
      SzString *s = sz_string_from_cstr("y");
      r = sz_io_unsafe_run(sz_queue_offer(q, s));
      sz_release(s);
      assert(r.ok);
      r = sz_io_unsafe_run(sz_queue_take(q));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "y") == 0);
      sz_release(r.value);
    }
    sz_queue_free(q);
  }

  /* Queue[Int] / Deferred[Int] */
  {
    SzQueue *q = sz_queue_make();
    void *n = sz_box_i64(7);
    r = sz_io_unsafe_run(sz_queue_offer(q, n));
    sz_release(n);
    assert(r.ok);
    r = sz_io_unsafe_run(sz_queue_take(q));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 7);
    sz_release(r.value);
    sz_queue_free(q);
  }
  {
    SzDeferred *d = sz_deferred_make();
    void *n = sz_box_i64(8);
    r = sz_io_unsafe_run(sz_deferred_complete(d, n));
    sz_release(n);
    assert(r.ok);
    r = sz_io_unsafe_run(sz_deferred_get(d));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 8);
    sz_release(r.value);
    sz_deferred_free(d);
  }

  /* Queue ring: take from the front, then offer until the ring wraps and grows. */
  {
    static const char *items[] = {"0", "1", "2", "3", "4", "5", "6", "7",
                                  "8", "9", "10", "11"};
    SzQueue *q = sz_queue_make();
    size_t i;
    for (i = 0; i < 8; i++) {
      r = sz_io_unsafe_run(sz_queue_offer_cstr(q, items[i]));
      assert(r.ok);
    }
    assert(sz_queue_size(q) == 8);
    for (i = 0; i < 3; i++) {
      r = sz_io_unsafe_run(sz_queue_take(q));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), items[i]) == 0);
      sz_release(r.value);
    }
    assert(sz_queue_size(q) == 5);
    for (i = 8; i < 12; i++) {
      r = sz_io_unsafe_run(sz_queue_offer_cstr(q, items[i]));
      assert(r.ok);
    }
    assert(sz_queue_size(q) == 9);
    for (i = 3; i < 12; i++) {
      r = sz_io_unsafe_run(sz_queue_take(q));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), items[i]) == 0);
      sz_release(r.value);
    }
    assert(sz_queue_size(q) == 0);
    sz_queue_free(q);
  }

  /* Leftover Queue / Ref / Deferred payloads drop on free. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzQueue *q;
    SzRef *ref;
    SzDeferred *def;
    SzString *s;

    sz_alloc_stats(&base_bytes, &base_count);
    q = sz_queue_make();
    r = sz_io_unsafe_run(sz_queue_offer_cstr(q, "leftover"));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_queue_offer_cstr(q, "also"));
    assert(r.ok);
    assert(sz_queue_size(q) == 2);
    sz_queue_free(q);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    q = sz_queue_make();
    s = sz_string_from_cstr("offer");
    {
      SzIo *io = sz_queue_offer(q, s);
      sz_release(s);
      sz_release(io);
    }
    sz_queue_free(q);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    s = sz_string_from_cstr("keep");
    ref = sz_ref_make(s);
    sz_release(s);
    {
      SzString *n = sz_string_from_cstr("next");
      r = sz_io_unsafe_run(sz_ref_set(ref, n));
      sz_release(n);
      assert(r.ok);
    }
    sz_ref_free(ref);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    /* Ref.set of the same pointer must not leak. */
    sz_alloc_stats(&base_bytes, &base_count);
    s = sz_string_from_cstr("same");
    ref = sz_ref_make(s);
    sz_release(s);
    r = sz_io_unsafe_run(sz_ref_set(ref, ref->value));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_ref_get(ref));
    assert(r.ok);
    {
      void *v = r.value;
      r = sz_io_unsafe_run(sz_ref_set(ref, v));
      assert(r.ok);
      sz_release(v);
    }
    sz_ref_free(ref);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    def = sz_deferred_make();
    sz_deferred_free(def);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    def = sz_deferred_make();
    r = sz_io_unsafe_run(sz_deferred_complete_cstr(def, "done"));
    assert(r.ok);
    sz_deferred_free(def);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    def = sz_deferred_make();
    s = sz_string_from_cstr("unused");
    {
      SzIo *io = sz_deferred_complete(def, s);
      sz_release(s);
      sz_release(io);
    }
    sz_deferred_free(def);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *path = sz_string_from_cstr("x.txt");
      SzString *body = sz_string_from_cstr("hi");
      SzIo *io = sz_fs_write(path, body);
      sz_release(path);
      sz_release(body);
      sz_release(io);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *url = sz_string_from_cstr("http://example.test/ping");
      SzIo *io = sz_net_http_get(url);
      sz_release(url);
      sz_release(io);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *url = sz_string_from_cstr("http://192.0.2.1:9/x");
      r = sz_io_unsafe_run(
          race_drop(sz_net_http_get(url), sz_io_sleep_ms(20)));
      sz_release(url);
      assert(r.ok);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *henv = sz_string_from_cstr("henv");
      r = sz_io_unsafe_run(
          race_drop(sz_net_serve(18601, serve_path_ok, henv),
                    sz_io_sleep_ms(20)));
      sz_release(henv);
      assert(r.ok);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *cmd = sz_string_from_cstr("true");
      SzIo *io = sz_sys_exec(cmd);
      sz_release(cmd);
      sz_release(io);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *henv = sz_string_from_cstr("henv");
      SzIo *once = sz_net_serve_once(8080, serve_path_ok, henv);
      SzIo *loop = sz_net_serve(8080, serve_path_ok, henv);
      sz_release(henv);
      sz_release(once);
      sz_release(loop);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzLangResource *lr = lang_make_tok();
      SzString *uen = sz_string_from_cstr("use-env");
      SzIo *io = sz_lang_resource_use(lr, lang_use_ok, uen);
      sz_release(uen);
      sz_lang_resource_free(lr);
      sz_release(io);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *key = sz_string_from_cstr("SCUZZ_LEFTOVER_GETENV");
      SzIo *io = sz_sys_getenv(key);
      sz_release(key);
      sz_release(io);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzIo *alive = sz_sys_alive(4242);
      SzIo *killed = sz_sys_kill(4242);
      sz_release(alive);
      sz_release(killed);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *cmd = sz_string_from_cstr("true");
      SzIo *io = sz_sys_spawn(cmd);
      sz_release(cmd);
      sz_release(io);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *s = sz_string_from_cstr("leftover-write");
      SzIo *io = sz_sys_write(s);
      sz_release(s);
      sz_release(io);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzIo *io = sz_sys_read(3);
      sz_release(io);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzIo *io = sz_testrt_sys_read(3);
      sz_release(io);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzIo *io = sz_random_next_int(10);
      sz_release(io);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *path = sz_string_from_cstr("leftover-fs.txt");
      SzIo *rd = sz_fs_read(path);
      SzIo *ls = sz_fs_list(path);
      SzIo *mk = sz_fs_mkdirs(path);
      SzIo *cn = sz_fs_canonicalize(path);
      SzIo *ex = sz_fs_exists(path);
      SzIo *del = sz_fs_delete(path);
      SzIo *ren = sz_fs_rename(path, path);
      SzIo *wk = sz_fs_walk(path);
      sz_release(path);
      sz_release(rd);
      sz_release(ls);
      sz_release(mk);
      sz_release(cn);
      sz_release(ex);
      sz_release(del);
      sz_release(ren);
      sz_release(wk);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Json.parse / stringify — Result[Json] / Result[String]; compact ASCII. */
  {
    json_expect_roundtrip(
        "[{\"severity\":\"error\",\"message\":\"x\",\"line\":2,\"column\":3}]");
    json_expect_roundtrip("null");
    json_expect_roundtrip("{\"ok\":true,\"n\":1,\"s\":\"a\"}");
    json_expect_err("9223372036854775808");
    {
      SzAdt *r = json_expect_ok("\"\\uD834\\uDD1E\"");
      SzAdt *js = (SzAdt *)sz_adt_payload(r);
      SzString *payload = (SzString *)sz_adt_payload(js);
      const unsigned char *bytes = (const unsigned char *)sz_string_cstr(payload);
      assert(sz_adt_tag(js) == 4);
      assert(sz_string_len(payload) == 4);
      assert(bytes[0] == 0xF0 && bytes[1] == 0x9D && bytes[2] == 0x84 &&
             bytes[3] == 0x9E);
      sz_release(r);
    }
    json_expect_err("\"\\uD834\"");
    json_expect_err("\"\\uDD1E\"");
    json_expect_stringify_err(json_float_of(INFINITY));
    json_expect_stringify_err(json_float_of(NAN));
    json_expect_err("1e999");
    {
      SzAdt *deep = sz_adt_new(0, NULL);
      int i;
      for (i = 0; i < 257; i++) {
        SzList *nil = sz_list_nil();
        SzList *xs = sz_list_cons(deep, nil);
        sz_release(deep);
        sz_release(nil);
        deep = sz_adt_new(5, xs);
        sz_release(xs);
      }
      json_expect_stringify_err(deep);
    }
    json_expect_err("{");
    json_expect_err("not-json");
  }

  /* Json query kit: get / keys / arr / at / has / as* / *Or / merge. */
  {
    SzAdt *pr = json_expect_ok("{\"a\":1,\"b\":[true,null],\"s\":\"hi\"}");
    SzAdt *j = (SzAdt *)sz_adt_payload(pr);
    SzString *ka = sz_string_from_cstr("a");
    SzString *kb = sz_string_from_cstr("b");
    SzString *ks = sz_string_from_cstr("s");
    SzString *km = sz_string_from_cstr("missing");
    SzString *dflt = sz_string_from_cstr("z");
    SzString *got_s;
    SzList *got;
    SzList *keys;
    SzList *bgot;
    SzList *arr;
    SzList *at1;
    SzList *miss;
    SzAdt *merged;
    assert(sz_json_is_obj(j) == 1);
    assert(sz_json_is_null(j) == 0);
    assert(sz_json_has(j, ka) == 1);
    assert(sz_json_has(j, km) == 0);
    assert(sz_json_get_int(j, ka, -1) == 1);
    assert(sz_json_get_int(j, km, -1) == -1);
    got_s = sz_json_get_str(j, ks, dflt);
    assert(strcmp(sz_string_cstr(got_s), "hi") == 0);
    sz_release(got_s);
    keys = sz_json_keys(j);
    assert(sz_list_len(keys) == 3);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(keys)), "a") == 0);
    got = sz_json_get(j, ka);
    assert(sz_list_len(got) == 1);
    assert(sz_json_is_int((SzAdt *)sz_list_head(got)) == 1);
    assert(sz_json_int_or((SzAdt *)sz_list_head(got), 0) == 1);
    bgot = sz_json_get(j, kb);
    arr = sz_json_arr((SzAdt *)sz_list_head(bgot));
    assert(sz_list_len(arr) == 2);
    assert(sz_json_is_bool((SzAdt *)sz_list_head(arr)) == 1);
    assert(sz_json_bool_or((SzAdt *)sz_list_head(arr), 0) == 1);
    at1 = sz_json_at((SzAdt *)sz_list_head(bgot), 1);
    assert(sz_json_is_null((SzAdt *)sz_list_head(at1)) == 1);
    miss = sz_json_at(j, 0);
    assert(sz_list_is_empty(miss));
    merged = sz_json_merge(j, j);
    assert(sz_json_is_obj(merged) == 1);
    assert(sz_json_get_int(merged, ka, 0) == 1);
    sz_release(merged);
    sz_release(keys);
    sz_release(got);
    sz_release(bgot);
    sz_release(arr);
    sz_release(at1);
    sz_release(miss);
    sz_release(ka);
    sz_release(kb);
    sz_release(ks);
    sz_release(km);
    sz_release(dflt);
    sz_release(pr);
  }

  {
    SzAdt *pr = json_expect_ok("[1,2,3]");
    SzAdt *j = (SzAdt *)sz_adt_payload(pr);
    SzString *ka = sz_string_from_cstr("a");
    SzList *xs;
    SzList *one;
    SzList *oob;
    SzList *keys;
    SzList *got;
    assert(sz_json_is_arr(j) == 1);
    xs = sz_json_arr(j);
    assert(sz_list_len(xs) == 3);
    one = sz_json_at(j, 1);
    assert(sz_json_int_or((SzAdt *)sz_list_head(one), 0) == 2);
    oob = sz_json_at(j, 9);
    assert(sz_list_is_empty(oob));
    keys = sz_json_keys(j);
    assert(sz_list_is_empty(keys));
    got = sz_json_get(j, ka);
    assert(sz_list_is_empty(got));
    sz_release(xs);
    sz_release(one);
    sz_release(oob);
    sz_release(keys);
    sz_release(got);
    sz_release(ka);
    sz_release(pr);
  }

  {
    SzAdt *left_r = json_expect_ok("{\"a\":1,\"b\":2}");
    SzAdt *right_r = json_expect_ok("{\"b\":9,\"c\":3}");
    SzAdt *left = (SzAdt *)sz_adt_payload(left_r);
    SzAdt *right = (SzAdt *)sz_adt_payload(right_r);
    SzString *ka = sz_string_from_cstr("a");
    SzString *kb = sz_string_from_cstr("b");
    SzString *kc = sz_string_from_cstr("c");
    SzAdt *merged = sz_json_merge(left, right);
    SzList *pairs;
    SzList *asb;
    SzList *asi;
    SzList *ass;
    SzAdt *tb;
    SzAdt *ti;
    SzAdt *ts;
    SzAdt *tn;
    SzString *dflt;
    SzString *got;
    assert(sz_json_is_obj(merged) == 1);
    assert(sz_json_get_int(merged, ka, 0) == 1);
    assert(sz_json_get_int(merged, kb, 0) == 9);
    assert(sz_json_get_int(merged, kc, 0) == 3);
    assert(sz_json_get_bool(merged, ka, 1) == 1);
    pairs = sz_json_pairs(merged);
    assert(sz_list_len(pairs) == 3);
    tb = json_expect_ok("true");
    ti = json_expect_ok("4");
    ts = json_expect_ok("\"hi\"");
    tn = json_expect_ok("null");
    asb = sz_json_as_bool((SzAdt *)sz_adt_payload(tb));
    asi = sz_json_as_int((SzAdt *)sz_adt_payload(ti));
    ass = sz_json_as_str((SzAdt *)sz_adt_payload(ts));
    assert(sz_list_len(asb) == 1);
    assert(sz_json_bool_or((SzAdt *)sz_adt_payload(tb), 0) == 1);
    assert(sz_json_int_or((SzAdt *)sz_adt_payload(ti), 0) == 4);
    dflt = sz_string_from_cstr("z");
    got = sz_json_str_or((SzAdt *)sz_adt_payload(ts), dflt);
    assert(strcmp(sz_string_cstr(got), "hi") == 0);
    sz_release(got);
    got = sz_json_str_or((SzAdt *)sz_adt_payload(tn), dflt);
    assert(strcmp(sz_string_cstr(got), "z") == 0);
    sz_release(got);
    assert(sz_json_is_null((SzAdt *)sz_adt_payload(tn)) == 1);
    assert(sz_json_is_bool((SzAdt *)sz_adt_payload(tb)) == 1);
    assert(sz_json_is_int((SzAdt *)sz_adt_payload(ti)) == 1);
    assert(sz_json_is_str((SzAdt *)sz_adt_payload(ts)) == 1);
    sz_release(asb);
    sz_release(asi);
    sz_release(ass);
    sz_release(tb);
    sz_release(ti);
    sz_release(ts);
    sz_release(tn);
    sz_release(dflt);
    sz_release(pairs);
    sz_release(merged);
    sz_release(ka);
    sz_release(kb);
    sz_release(kc);
    sz_release(left_r);
    sz_release(right_r);
  }

  {
    SzAdt *pr = json_expect_ok("{\"a\":1,\"b\":2}");
    SzAdt *j = (SzAdt *)sz_adt_payload(pr);
    SzAdt *vr = json_expect_ok("9");
    SzAdt *v = (SzAdt *)sz_adt_payload(vr);
    SzAdt *nr = json_expect_ok("3");
    SzAdt *nv = (SzAdt *)sz_adt_payload(nr);
    SzAdt *nl = json_expect_ok("null");
    SzAdt *fr = json_expect_ok("1.5");
    SzAdt *fv = (SzAdt *)sz_adt_payload(fr);
    SzAdt *ar = json_expect_ok("[1,2,3]");
    SzAdt *arr = (SzAdt *)sz_adt_payload(ar);
    SzString *ka = sz_string_from_cstr("a");
    SzString *kb = sz_string_from_cstr("b");
    SzString *kc = sz_string_from_cstr("c");
    SzString *kx = sz_string_from_cstr("x");
    SzAdt *set_a = sz_json_set(j, ka, v);
    SzAdt *set_c = sz_json_set(j, kc, nv);
    SzAdt *removed = sz_json_remove(j, ka);
    SzAdt *drop_miss = sz_json_remove(j, kc);
    SzAdt *from_null = sz_json_set((SzAdt *)sz_adt_payload(nl), ka, v);
    SzAdt *app = sz_json_append(arr, nv);
    SzAdt *pre = sz_json_prepend(arr, nv);
    SzAdt *sat = sz_json_set_at(arr, 1, nv);
    SzAdt *dat = sz_json_drop_at(arr, 1);
    SzAdt *sat_oob = sz_json_set_at(arr, 9, nv);
    SzAdt *dat_oob = sz_json_drop_at(arr, 9);
    SzAdt *app_null = sz_json_append((SzAdt *)sz_adt_payload(nl), nv);
    SzList *app_xs;
    SzList *app_at;
    SzList *pre_xs;
    SzList *pre_at;
    SzList *sat_at;
    SzList *dat_xs;
    SzList *dat_at;
    SzList *null_xs;
    assert(sz_json_get_int(set_a, ka, 0) == 9);
    assert(sz_json_get_int(set_a, kb, 0) == 2);
    assert(sz_json_get_int(set_c, kc, 0) == 3);
    assert(sz_json_get_int(set_c, ka, 0) == 1);
    assert(sz_json_has(removed, ka) == 0);
    assert(sz_json_get_int(removed, kb, 0) == 2);
    assert(sz_json_get_int(drop_miss, ka, 0) == 1);
    assert(sz_json_is_obj(from_null) == 1);
    assert(sz_json_get_int(from_null, ka, 0) == 9);
    app_xs = sz_json_arr(app);
    app_at = sz_json_at(app, 3);
    pre_xs = sz_json_arr(pre);
    pre_at = sz_json_at(pre, 0);
    sat_at = sz_json_at(sat, 1);
    dat_xs = sz_json_arr(dat);
    dat_at = sz_json_at(dat, 1);
    null_xs = sz_json_arr(app_null);
    assert(sz_list_len(app_xs) == 4);
    assert(sz_json_int_or((SzAdt *)sz_list_head(app_at), 0) == 3);
    assert(sz_list_len(pre_xs) == 4);
    assert(sz_json_int_or((SzAdt *)sz_list_head(pre_at), 0) == 3);
    assert(sz_json_int_or((SzAdt *)sz_list_head(sat_at), 0) == 3);
    assert(sz_list_len(dat_xs) == 2);
    assert(sz_json_int_or((SzAdt *)sz_list_head(dat_at), 0) == 3);
    assert(sat_oob == arr);
    assert(dat_oob == arr);
    assert(sz_json_is_arr(app_null) == 1);
    assert(sz_list_len(null_xs) == 1);
    assert(sz_json_float_or(fv, 0.0) > 1.4 && sz_json_float_or(fv, 0.0) < 1.6);
    assert(sz_json_get_float(j, kx, 2.5) > 2.4 && sz_json_get_float(j, kx, 2.5) < 2.6);
    sz_release(app_xs);
    sz_release(app_at);
    sz_release(pre_xs);
    sz_release(pre_at);
    sz_release(sat_at);
    sz_release(dat_xs);
    sz_release(dat_at);
    sz_release(null_xs);
    sz_release(from_null);
    sz_release(set_a);
    sz_release(set_c);
    sz_release(removed);
    sz_release(drop_miss);
    sz_release(app);
    sz_release(pre);
    sz_release(sat);
    sz_release(dat);
    sz_release(sat_oob);
    sz_release(dat_oob);
    sz_release(app_null);
    sz_release(ka);
    sz_release(kb);
    sz_release(kc);
    sz_release(kx);
    sz_release(pr);
    sz_release(vr);
    sz_release(nr);
    sz_release(nl);
    sz_release(fr);
    sz_release(ar);
  }

  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzAdt *pr = json_expect_ok("[1,true,null]");
      SzAdt *sr = sz_json_stringify((SzAdt *)sz_adt_payload(pr));
      assert(sz_adt_tag(sr) == 1);
      sz_release(pr);
      sz_release(sr);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzAdt *pr = json_expect_ok("{\"a\":1,\"b\":[true]}");
      SzAdt *j = (SzAdt *)sz_adt_payload(pr);
      SzString *ka = sz_string_from_cstr("a");
      SzList *keys = sz_json_keys(j);
      SzList *got = sz_json_get(j, ka);
      SzAdt *merged = sz_json_merge(j, j);
      sz_release(keys);
      sz_release(got);
      sz_release(merged);
      sz_release(ka);
      sz_release(pr);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzAdt *pr = json_expect_ok("{\"a\":1}");
      SzAdt *j = (SzAdt *)sz_adt_payload(pr);
      SzAdt *vr = json_expect_ok("2");
      SzAdt *v = (SzAdt *)sz_adt_payload(vr);
      SzAdt *ar = json_expect_ok("[1]");
      SzAdt *arr = (SzAdt *)sz_adt_payload(ar);
      SzString *ka = sz_string_from_cstr("a");
      SzString *kb = sz_string_from_cstr("b");
      SzAdt *set = sz_json_set(j, kb, v);
      SzAdt *rem = sz_json_remove(j, ka);
      SzAdt *app = sz_json_append(arr, v);
      SzAdt *pre = sz_json_prepend(arr, v);
      SzAdt *sat = sz_json_set_at(arr, 0, v);
      SzAdt *dat = sz_json_drop_at(arr, 0);
      sz_release(set);
      sz_release(rem);
      sz_release(app);
      sz_release(pre);
      sz_release(sat);
      sz_release(dat);
      sz_release(ka);
      sz_release(kb);
      sz_release(pr);
      sz_release(vr);
      sz_release(ar);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    json_expect_err("{");
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Stream — emit / eval / concat / evalMap / map / take / drop / filter / compileToList / drain */
  {
    SzList *xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    SzStream *s = sz_stream_concat(
        sz_stream_evalmap(sz_stream_emits(xs), stream_bang, NULL),
        sz_stream_eval(pure_drop(sz_string_from_cstr("c"))));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(s));
    assert(r.ok);
    SzString *    joined = sz_list_join((SzList *)r.value, ",");
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

    delay_calls = 0;
    s = sz_stream_take(
        sz_stream_filter(
            sz_stream_concat(
                sz_stream_concat(
                    sz_stream_eval(sz_io_delay(take_hit, (void *)"a")),
                    sz_stream_eval(sz_io_delay(take_hit, (void *)""))),
                sz_stream_eval(sz_io_delay(take_hit, (void *)"b"))),
            stream_nonempty, NULL),
        1);
    r = sz_io_unsafe_run(sz_stream_compile_to_list(s));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a") == 0);
    assert(delay_calls == 1);

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

    delay_calls = 0;
    s = sz_stream_take(
        sz_stream_dropwhile(
            sz_stream_concat(
                sz_stream_concat(
                    sz_stream_eval(sz_io_delay(take_hit, (void *)"")),
                    sz_stream_eval(sz_io_delay(take_hit, (void *)"a"))),
                sz_stream_eval(sz_io_delay(take_hit, (void *)"b"))),
            stream_empty, NULL),
        1);
    r = sz_io_unsafe_run(sz_stream_compile_to_list(s));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a") == 0);
    assert(delay_calls == 2);

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

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_zip_with_index(sz_stream_emits(xs))));
    assert(r.ok);
    assert(sz_list_len((SzList *)r.value) == 2);
    {
      SzPair *pair = (SzPair *)sz_list_head((SzList *)r.value);
      assert(sz_unbox_i64(pair->left) == 0);
      assert(strcmp(sz_string_cstr((SzString *)pair->right), "a") == 0);
    }

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_intersperse(sz_stream_emits(xs), sz_string_from_cstr("|"))));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,|,b") == 0);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"),
                     sz_list_cons(sz_string_from_cstr("c"), sz_list_nil())));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_grouped(sz_stream_emits(xs), 2)));
    assert(r.ok);
    assert(sz_list_len((SzList *)r.value) == 2);
    joined = sz_list_join((SzList *)sz_list_head((SzList *)r.value), "");
    assert(strcmp(sz_string_cstr(joined), "ab") == 0);

    r = sz_io_unsafe_run(sz_stream_compile_to_list(sz_stream_range(3, 6)));
    assert(r.ok);
    assert(sz_list_len((SzList *)r.value) == 3);
    assert(sz_unbox_i64(sz_list_head((SzList *)r.value)) == 3);

    xs = sz_list_cons(sz_string_from_cstr("x"), sz_list_nil());
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_repeat_n(sz_stream_emits(xs), 3)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "x,x,x") == 0);

    {
      SzList *as = sz_list_cons(
          sz_string_from_cstr("a"),
          sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
      SzList *bs = sz_list_cons(
          sz_string_from_cstr("1"),
          sz_list_cons(sz_string_from_cstr("2"),
                       sz_list_cons(sz_string_from_cstr("3"), sz_list_nil())));
      r = sz_io_unsafe_run(sz_stream_compile_to_list(
          sz_stream_zip(sz_stream_emits(as), sz_stream_emits(bs))));
      assert(r.ok);
      assert(sz_list_len((SzList *)r.value) == 2);
    }

    {
      SzList *as = sz_list_cons(
          sz_string_from_cstr("a"),
          sz_list_cons(sz_string_from_cstr("c"), sz_list_nil()));
      SzList *bs = sz_list_cons(sz_string_from_cstr("b"), sz_list_nil());
      r = sz_io_unsafe_run(sz_stream_compile_to_list(
          sz_stream_interleave(sz_stream_emits(as), sz_stream_emits(bs))));
      assert(r.ok);
      joined = sz_list_join((SzList *)r.value, ",");
      assert(strcmp(sz_string_cstr(joined), "a,b,c") == 0);
    }

    {
      SzList *as = sz_list_cons(
          sz_string_from_cstr("a"),
          sz_list_cons(sz_string_from_cstr("c"), sz_list_nil()));
      SzList *bs = sz_list_cons(
          sz_string_from_cstr("b"),
          sz_list_cons(sz_string_from_cstr("d"),
                       sz_list_cons(sz_string_from_cstr("e"), sz_list_nil())));
      r = sz_io_unsafe_run(sz_stream_compile_to_list(
          sz_stream_interleave(sz_stream_emits(as), sz_stream_emits(bs))));
      assert(r.ok);
      joined = sz_list_join((SzList *)r.value, ",");
      assert(strcmp(sz_string_cstr(joined), "a,b,c,d,e") == 0);
    }

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_flatmap(sz_stream_emits(xs), stream_dup, NULL)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,a,b,b") == 0);

    {
      SzList *nested = sz_list_cons(
          sz_list_cons(sz_string_from_cstr("a"),
                       sz_list_cons(sz_string_from_cstr("b"), sz_list_nil())),
          sz_list_cons(sz_list_cons(sz_string_from_cstr("c"), sz_list_nil()),
                       sz_list_nil()));
      r = sz_io_unsafe_run(sz_stream_compile_to_list(
          sz_stream_flatten(sz_stream_emits(nested))));
      assert(r.ok);
      joined = sz_list_join((SzList *)r.value, ",");
      assert(strcmp(sz_string_cstr(joined), "a,b,c") == 0);
    }

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("a"),
                     sz_list_cons(sz_string_from_cstr("b"), sz_list_nil())));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_changes(sz_stream_emits(xs))));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,b") == 0);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(sz_stream_scan(
        sz_stream_emits(xs), sz_string_from_cstr(""), stream_scan_concat,
        NULL)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), ",a,ab") == 0);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_fold(sz_stream_emits(xs),
                                       sz_string_from_cstr(""),
                                       stream_fold_concat, NULL));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "ab") == 0);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(
        sz_stream_forall(sz_stream_emits(xs), stream_nonempty, NULL));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 1);
    r = sz_io_unsafe_run(sz_stream_forall(
        sz_stream_emits(sz_list_cons(sz_string_from_cstr(""), sz_list_nil())),
        stream_nonempty, NULL));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);

    r = sz_io_unsafe_run(sz_stream_compile_to_list(sz_stream_range(5, 5)));
    assert(r.ok);
    assert(sz_list_is_empty((SzList *)r.value));

    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_repeat_n(sz_stream_emit(sz_string_from_cstr("x")), 0)));
    assert(r.ok);
    assert(sz_list_is_empty((SzList *)r.value));

    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_grouped(sz_stream_emits(sz_list_nil()), 2)));
    assert(r.ok);
    assert(sz_list_is_empty((SzList *)r.value));

    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_grouped(sz_stream_emit(sz_string_from_cstr("a")), 0)));
    assert(r.ok);
    assert(sz_list_is_empty((SzList *)r.value));

    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_zip(sz_stream_nil(), sz_stream_emit(sz_string_from_cstr("a")))));
    assert(r.ok);
    assert(sz_list_is_empty((SzList *)r.value));

    r = sz_io_unsafe_run(sz_stream_compile_to_list(sz_stream_interleave(
        sz_stream_nil(), sz_stream_emit(sz_string_from_cstr("a")))));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a") == 0);

    r = sz_io_unsafe_run(sz_stream_compile_to_list(sz_stream_interleave(
        sz_stream_emit(sz_string_from_cstr("a")), sz_stream_nil())));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a") == 0);

    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_take(sz_stream_interleave(sz_stream_emits(sz_list_cons(
                                               sz_string_from_cstr("a"),
                                               sz_list_cons(sz_string_from_cstr("c"),
                                                            sz_list_nil()))),
                                           sz_stream_emits(sz_list_cons(
                                               sz_string_from_cstr("b"),
                                               sz_list_cons(sz_string_from_cstr("d"),
                                                            sz_list_nil())))),
                       3)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,b,c") == 0);

    r = sz_io_unsafe_run(sz_stream_fold(sz_stream_nil(), sz_string_from_cstr("z"),
                                       stream_fold_concat, NULL));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "z") == 0);

    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_scan(sz_stream_nil(), sz_string_from_cstr("z"),
                       stream_scan_concat, NULL)));
    assert(r.ok);
    assert(sz_list_len((SzList *)r.value) == 1);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head((SzList *)r.value)),
                  "z") == 0);

    r = sz_io_unsafe_run(
        sz_stream_forall(sz_stream_nil(), stream_nonempty, NULL));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 1);

    xs = sz_list_cons(
        sz_string_from_cstr(""),
        sz_list_cons(sz_string_from_cstr("a"),
                     sz_list_cons(sz_string_from_cstr("b"), sz_list_nil())));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_filter_not(sz_stream_emits(xs), stream_empty, NULL)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,b") == 0);

    xs = sz_list_cons(sz_string_from_cstr("a"),
                      sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_map_concat(sz_stream_emits(xs), stream_list_dup, NULL)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,a,b,b") == 0);

    {
      SzList *as = sz_list_cons(sz_string_from_cstr("a"),
                               sz_list_cons(sz_string_from_cstr("b"),
                                            sz_list_nil()));
      SzList *bs = sz_list_cons(sz_string_from_cstr("1"),
                               sz_list_cons(sz_string_from_cstr("2"),
                                            sz_list_nil()));
      r = sz_io_unsafe_run(sz_stream_compile_to_list(sz_stream_zip_with(
          sz_stream_emits(as), sz_stream_emits(bs), stream_zip_concat, NULL)));
      assert(r.ok);
      joined = sz_list_join((SzList *)r.value, ",");
      assert(strcmp(sz_string_cstr(joined), "a1,b2") == 0);
    }

    {
      SzList *as = sz_list_cons(sz_string_from_cstr("a"), sz_list_nil());
      SzList *bs = sz_list_cons(
          sz_string_from_cstr("1"),
          sz_list_cons(sz_string_from_cstr("2"), sz_list_nil()));
      SzString *pa = sz_string_from_cstr("x");
      SzString *pb = sz_string_from_cstr("y");
      r = sz_io_unsafe_run(sz_stream_compile_to_list(
          sz_stream_zip_all(sz_stream_emits(as), sz_stream_emits(bs), pa, pb)));
      assert(r.ok);
      assert(sz_list_len((SzList *)r.value) == 2);
    }

    r = sz_io_unsafe_run(sz_stream_compile_to_list(sz_stream_or_else(
        sz_stream_nil(), sz_stream_emit(sz_string_from_cstr("z")))));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "z") == 0);

    r = sz_io_unsafe_run(sz_stream_compile_to_list(sz_stream_or_else(
        sz_stream_emit(sz_string_from_cstr("a")),
        sz_stream_emit(sz_string_from_cstr("z")))));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a") == 0);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"),
                     sz_list_cons(sz_string_from_cstr("c"), sz_list_nil())));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_sliding(sz_stream_emits(xs), 2)));
    assert(r.ok);
    assert(sz_list_len((SzList *)r.value) == 2);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"),
                     sz_list_cons(sz_string_from_cstr("c"), sz_list_nil())));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_take_right(sz_stream_emits(xs), 2)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "b,c") == 0);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr("b"),
                     sz_list_cons(sz_string_from_cstr("c"), sz_list_nil())));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_drop_right(sz_stream_emits(xs), 1)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,b") == 0);

    xs = sz_list_cons(
        sz_string_from_cstr("a"),
        sz_list_cons(sz_string_from_cstr(""),
                     sz_list_cons(sz_string_from_cstr("b"), sz_list_nil())));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_find_last(sz_stream_emits(xs), stream_nonempty, NULL)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "b") == 0);

    tap_n = 0;
    xs = sz_list_cons(sz_string_from_cstr("a"),
                      sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_evaltap(sz_stream_emits(xs), stream_tap_count, NULL)));
    assert(r.ok);
    joined = sz_list_join((SzList *)r.value, ",");
    assert(strcmp(sz_string_cstr(joined), "a,b") == 0);
    assert(tap_n == 2);

    {
      void *z = sz_box_i64(0);
      r = sz_io_unsafe_run(
          sz_stream_compile_to_list(sz_stream_iterate(z, 4, stream_inc, NULL)));
      assert(r.ok);
      assert(sz_list_len((SzList *)r.value) == 4);
      assert(sz_unbox_i64(sz_list_head((SzList *)r.value)) == 0);
    }

    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_iterate(sz_box_i64(0), 0, stream_inc, NULL)));
    assert(r.ok);
    assert(sz_list_is_empty((SzList *)r.value));

    {
      void *z = sz_box_i64(0);
      r = sz_io_unsafe_run(sz_stream_compile_to_list(
          sz_stream_unfold(z, stream_unfold_step, NULL)));
      assert(r.ok);
      assert(sz_list_len((SzList *)r.value) == 3);
      assert(sz_unbox_i64(sz_list_head((SzList *)r.value)) == 0);
    }

    xs = sz_list_cons(sz_string_from_cstr("a"),
                      sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_head(sz_stream_emits(xs)));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "a") == 0);

    r = sz_io_unsafe_run(sz_stream_head(sz_stream_nil()));
    assert(!r.ok);

    xs = sz_list_cons(sz_string_from_cstr("a"),
                      sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_last(sz_stream_emits(xs)));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "b") == 0);

    r = sz_io_unsafe_run(sz_stream_last(sz_stream_nil()));
    assert(!r.ok);

    xs = sz_list_cons(sz_string_from_cstr("a"),
                      sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
    r = sz_io_unsafe_run(sz_stream_count(sz_stream_emits(xs)));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 2);

    r = sz_io_unsafe_run(sz_stream_count(sz_stream_nil()));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);

    xs = sz_list_cons(sz_string_from_cstr(""),
                      sz_list_cons(sz_string_from_cstr(""), sz_list_nil()));
    r = sz_io_unsafe_run(
        sz_stream_none(sz_stream_emits(xs), stream_nonempty, NULL));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 1);

    xs = sz_list_cons(sz_string_from_cstr(""),
                      sz_list_cons(sz_string_from_cstr("a"), sz_list_nil()));
    r = sz_io_unsafe_run(
        sz_stream_none(sz_stream_emits(xs), stream_nonempty, NULL));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);

    r = sz_io_unsafe_run(sz_stream_compile_to_list(
        sz_stream_changes(sz_stream_emit(sz_string_from_cstr("a")))));
    assert(r.ok);
    assert(sz_list_len((SzList *)r.value) == 1);

    {
      int64_t live = -1;
      int64_t ready = -1;
      int64_t parked = -1;
      int64_t done = -1;
      sz_fiber_census(&live, &ready, &parked, &done);
      assert(live == 0);
      assert(ready == 0);
      assert(parked == 0);
      assert(done == 0);
    }

    {
      size_t base_bytes = 0, base_count = 0;
      size_t live_bytes = 0, live_count = 0;
      SzString *a;
      SzString *b;
      SzString *empty;
      SzList *ys;
      SzList *tail;
      SzList *mid;
      SzStream *st;
      SzIo *fail;

      sz_alloc_stats(&base_bytes, &base_count);
      a = sz_string_from_cstr("a");
      st = sz_stream_emit(a);
      sz_release(a);
      r = sz_io_unsafe_run(sz_stream_compile_to_list(st));
      assert(r.ok);
      sz_release(r.value);
      sz_release(st);
      sz_alloc_stats(&live_bytes, &live_count);
      assert(live_count == base_count);
      assert(live_bytes == base_bytes);

      sz_alloc_stats(&base_bytes, &base_count);
      a = sz_string_from_cstr("a");
      b = sz_string_from_cstr("b");
      {
        SzStream *left = sz_stream_emit(a);
        SzStream *right = sz_stream_emit(b);
        st = sz_stream_interleave(left, right);
        sz_release(left);
        sz_release(right);
      }
      sz_release(a);
      sz_release(b);
      r = sz_io_unsafe_run(sz_stream_compile_to_list(st));
      assert(r.ok);
      sz_release(r.value);
      sz_release(st);
      sz_alloc_stats(&live_bytes, &live_count);
      assert(live_count == base_count);
      assert(live_bytes == base_bytes);

      sz_alloc_stats(&base_bytes, &base_count);
      a = sz_string_from_cstr("a");
      b = sz_string_from_cstr("b");
      tail = sz_list_cons(b, NULL);
      sz_release(b);
      ys = sz_list_cons(a, tail);
      sz_release(a);
      sz_release(tail);
      {
        SzStream *inner = sz_stream_emits(ys);
        st = sz_stream_map(inner, stream_bang_sync, NULL);
        sz_release(inner);
      }
      sz_release(ys);
      r = sz_io_unsafe_run(sz_stream_compile_to_list(st));
      assert(r.ok);
      sz_release(r.value);
      sz_release(st);
      sz_alloc_stats(&live_bytes, &live_count);
      assert(live_count == base_count);
      assert(live_bytes == base_bytes);

      sz_alloc_stats(&base_bytes, &base_count);
      a = sz_string_from_cstr("a");
      empty = sz_string_from_cstr("");
      b = sz_string_from_cstr("b");
      tail = sz_list_cons(b, NULL);
      sz_release(b);
      mid = sz_list_cons(empty, tail);
      sz_release(empty);
      sz_release(tail);
      ys = sz_list_cons(a, mid);
      sz_release(a);
      sz_release(mid);
      {
        SzStream *inner = sz_stream_emits(ys);
        st = sz_stream_filter(inner, stream_nonempty, NULL);
        sz_release(inner);
      }
      sz_release(ys);
      r = sz_io_unsafe_run(sz_stream_compile_to_list(st));
      assert(r.ok);
      sz_release(r.value);
      sz_release(st);
      sz_alloc_stats(&live_bytes, &live_count);
      assert(live_count == base_count);
      assert(live_bytes == base_bytes);

      sz_alloc_stats(&base_bytes, &base_count);
      a = sz_string_from_cstr("a");
      b = sz_string_from_cstr("b");
      tail = sz_list_cons(b, NULL);
      sz_release(b);
      ys = sz_list_cons(a, tail);
      sz_release(a);
      sz_release(tail);
      {
        SzStream *inner = sz_stream_emits(ys);
        st = sz_stream_drop(inner, 1);
        sz_release(inner);
      }
      sz_release(ys);
      r = sz_io_unsafe_run(sz_stream_compile_to_list(st));
      assert(r.ok);
      sz_release(r.value);
      sz_release(st);
      sz_alloc_stats(&live_bytes, &live_count);
      assert(live_count == base_count);
      assert(live_bytes == base_bytes);

      sz_alloc_stats(&base_bytes, &base_count);
      a = sz_string_from_cstr("d");
      st = sz_stream_emit(a);
      sz_release(a);
      r = sz_io_unsafe_run(sz_stream_drain(st));
      assert(r.ok);
      sz_release(r.value);
      sz_release(st);
      sz_alloc_stats(&live_bytes, &live_count);
      assert(live_count == base_count);
      assert(live_bytes == base_bytes);

      sz_alloc_stats(&base_bytes, &base_count);
      a = sz_string_from_cstr("");
      b = sz_string_from_cstr("a");
      tail = sz_list_cons(b, NULL);
      sz_release(b);
      ys = sz_list_cons(a, tail);
      sz_release(a);
      sz_release(tail);
      st = sz_stream_emits(ys);
      sz_release(ys);
      r = sz_io_unsafe_run(sz_stream_exists(st, stream_nonempty, NULL));
      assert(r.ok);
      sz_release(r.value);
      sz_release(st);
      sz_alloc_stats(&live_bytes, &live_count);
      assert(live_count == base_count);
      assert(live_bytes == base_bytes);

      sz_alloc_stats(&base_bytes, &base_count);
      fail = sz_io_fail_cstr("boom");
      st = sz_stream_eval(fail);
      sz_release(fail);
      r = sz_io_unsafe_run(sz_stream_compile_to_list(st));
      assert(!r.ok);
      sz_error_free(r.error);
      sz_release(st);
      sz_alloc_stats(&live_bytes, &live_count);
      assert(live_count == base_count);
      assert(live_bytes == base_bytes);
    }
  }

  /* sleep */
  r = sz_io_unsafe_run(sz_io_sleep_ms(1));
  assert(r.ok);

  /* race prefers non-sleep winner */
  r = sz_io_unsafe_run(
      race_drop(sz_io_sleep_ms(20), pure_drop((void *)(intptr_t)99)));
  assert(r.ok);
  assert((intptr_t)r.value == 99);

  /* Live: losing sleep must not block the winner for the full duration. */
  {
    int64_t t0 = sz_clock_monotonic_ms_sync();
    int64_t t1;
    r = sz_io_unsafe_run(
        race_drop(sz_io_sleep_ms(300), sz_io_sleep_ms(1)));
    t1 = sz_clock_monotonic_ms_sync();
    assert(r.ok);
    assert(t1 - t0 < 80);
  }

  /* both */
  r = sz_io_unsafe_run(
      both_drop(pure_drop((void *)(intptr_t)1), pure_drop((void *)(intptr_t)2)));
  assert(r.ok);
  {
    SzPair *p = (SzPair *)r.value;
    assert(p && (intptr_t)p->left == 1 && (intptr_t)p->right == 2);
    sz_pair_free(p);
  }

  {
    SzString *a = sz_string_from_cstr("L");
    SzString *b = sz_string_from_cstr("R");
    SzPair *p = sz_pair_new(a, b);
    sz_release(a);
    sz_release(b);
    assert(p && strcmp(sz_string_cstr((SzString *)p->left), "L") == 0);
    assert(strcmp(sz_string_cstr((SzString *)p->right), "R") == 0);
    assert(sz_pair_left(p) == p->left);
    assert(sz_pair_right(p) == p->right);
    sz_pair_free(p);
  }

  /* Leftover pair payloads drop on free. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(both_drop(pure_drop(sz_string_from_cstr("L")),
                                    pure_drop(sz_string_from_cstr("R"))));
    assert(r.ok);
    sz_pair_free((SzPair *)r.value);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
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
    assert(sz_string_starts_with(c, a) == 1);
    assert(sz_string_starts_with(c, b) == 0);
    assert(sz_string_starts_with(c, sz_string_from_cstr("")) == 1);
    assert(sz_string_starts_with(c, sz_string_from_cstr("foobarbaz")) == 0);
    assert(sz_string_contains(c, b) == 1);
    assert(sz_string_contains(c, sz_string_from_cstr("zz")) == 0);
    assert(sz_string_contains(c, sz_string_from_cstr("")) == 1);
    assert(sz_string_ends_with(c, b) == 1);
    assert(sz_string_ends_with(c, a) == 0);
    assert(sz_string_ends_with(c, sz_string_from_cstr("")) == 1);
    assert(sz_string_to_int(sz_string_from_cstr("42"), 0) == 42);
    assert(sz_string_to_int(sz_string_from_cstr("-7"), 0) == -7);
    assert(sz_string_to_int(sz_string_from_cstr("x"), 9) == 9);
    assert(sz_string_to_int(sz_string_from_cstr("42x"), 9) == 9);
    assert(sz_string_to_int(sz_string_from_cstr(""), 9) == 9);
    {
      SzString *rep = sz_string_replace(c, a, b);
      assert(strcmp(sz_string_cstr(rep), "barbar") == 0);
      sz_string_free(rep);
      rep = sz_string_replace(sz_string_from_cstr("aaa"), sz_string_from_cstr("aa"),
                              sz_string_from_cstr("b"));
      assert(strcmp(sz_string_cstr(rep), "ba") == 0);
      sz_string_free(rep);
      rep = sz_string_replace(c, sz_string_from_cstr(""), b);
      assert(strcmp(sz_string_cstr(rep), "foobar") == 0);
      sz_string_free(rep);
    }
    {
      SzString *s = sz_string_from_cstr("a,,b");
      SzString *sep = sz_string_from_cstr(",");
      SzString *empty = sz_string_from_cstr("");
      SzList *xs = sz_string_split(s, sep);
      SzList *one;
      SzList *blank;
      assert(sz_list_len(xs) == 3);
      assert(strcmp(sz_string_cstr(xs->head), "a") == 0);
      assert(strcmp(sz_string_cstr(xs->tail->head), "") == 0);
      assert(strcmp(sz_string_cstr(xs->tail->tail->head), "b") == 0);
      sz_list_free(xs);
      one = sz_string_split(s, empty);
      assert(sz_list_len(one) == 1);
      assert(strcmp(sz_string_cstr(one->head), "a,,b") == 0);
      sz_list_free(one);
      blank = sz_string_split(empty, sep);
      assert(sz_list_len(blank) == 1);
      assert(strcmp(sz_string_cstr(blank->head), "") == 0);
      sz_list_free(blank);
      sz_string_free(s);
      sz_string_free(sep);
      sz_string_free(empty);
    }
    {
      assert(sz_string_is_empty(sz_string_from_cstr("")) == 1);
      assert(sz_string_is_empty(sz_string_from_cstr("a")) == 0);
      assert(sz_string_is_empty(NULL) == 1);
      assert(sz_string_non_empty(sz_string_from_cstr("")) == 0);
      assert(sz_string_non_empty(sz_string_from_cstr("a")) == 1);
      assert(sz_string_non_empty(NULL) == 0);
      {
        SzString *low = sz_string_to_lower(sz_string_from_cstr("AbC!"));
        SzString *up = sz_string_to_upper(sz_string_from_cstr("AbC!"));
        SzString *rep = sz_string_repeat(sz_string_from_cstr("ab"), 3);
        SzString *none = sz_string_repeat(sz_string_from_cstr("ab"), 0);
        assert(strcmp(sz_string_cstr(low), "abc!") == 0);
        assert(strcmp(sz_string_cstr(up), "ABC!") == 0);
        {
          SzString *cap = sz_string_capitalize(sz_string_from_cstr("hello"));
          SzString *keep = sz_string_capitalize(sz_string_from_cstr("Hello"));
          SzString *empty = sz_string_capitalize(sz_string_from_cstr(""));
          SzString *digit = sz_string_capitalize(sz_string_from_cstr("1ab"));
          assert(strcmp(sz_string_cstr(cap), "Hello") == 0);
          assert(strcmp(sz_string_cstr(keep), "Hello") == 0);
          assert(sz_string_is_empty(empty) == 1);
          assert(strcmp(sz_string_cstr(digit), "1ab") == 0);
          sz_string_free(cap);
          sz_string_free(keep);
          sz_string_free(empty);
          sz_string_free(digit);
        }
        assert(strcmp(sz_string_cstr(rep), "ababab") == 0);
        assert(sz_string_is_empty(none) == 1);
        sz_string_free(low);
        sz_string_free(up);
        sz_string_free(rep);
        sz_string_free(none);
      }
    }
    {
      SzString *pre = sz_string_strip_prefix(sz_string_from_cstr("abc"), sz_string_from_cstr("a"));
      SzString *keep = sz_string_strip_prefix(sz_string_from_cstr("abc"), sz_string_from_cstr("z"));
      SzString *suf = sz_string_strip_suffix(sz_string_from_cstr("abc"), sz_string_from_cstr("c"));
      SzString *left = sz_string_pad_left(sz_string_from_cstr("a"), 3, sz_string_from_cstr("x"));
      SzString *right = sz_string_pad_right(sz_string_from_cstr("a"), 3, sz_string_from_cstr("x"));
      SzString *shortp = sz_string_pad_left(sz_string_from_cstr("abcd"), 2, sz_string_from_cstr("x"));
      SzString *nopad = sz_string_pad_left(sz_string_from_cstr("a"), 3, sz_string_from_cstr(""));
      assert(strcmp(sz_string_cstr(pre), "bc") == 0);
      assert(strcmp(sz_string_cstr(keep), "abc") == 0);
      assert(strcmp(sz_string_cstr(suf), "ab") == 0);
      assert(strcmp(sz_string_cstr(left), "xxa") == 0);
      assert(strcmp(sz_string_cstr(right), "axx") == 0);
      assert(strcmp(sz_string_cstr(shortp), "abcd") == 0);
      assert(strcmp(sz_string_cstr(nopad), "a") == 0);
      assert(sz_string_is_blank(sz_string_from_cstr(" \t\n")) == 1);
      assert(sz_string_is_blank(sz_string_from_cstr(" a")) == 0);
      assert(sz_string_is_blank(NULL) == 1);
      {
        SzString *hay = sz_string_from_cstr("ababa");
        SzString *ba = sz_string_from_cstr("ba");
        SzString *t = sz_string_take(hay, 2);
        SzString *d = sz_string_drop(hay, 1);
        SzString *t0 = sz_string_take(hay, 0);
        SzString *d0 = sz_string_drop(hay, 0);
        SzString *over = sz_string_take(hay, 9);
        SzString *gone = sz_string_drop(hay, 9);
        SzString *tr = sz_string_take_right(hay, 2);
        SzString *dr = sz_string_drop_right(hay, 1);
        assert(sz_string_last_index_of(hay, ba) == 3);
        assert(sz_string_last_index_of(hay, sz_string_from_cstr("z")) == -1);
        assert(sz_string_last_index_of(hay, sz_string_from_cstr("")) == 5);
        assert(strcmp(sz_string_cstr(t), "ab") == 0);
        assert(strcmp(sz_string_cstr(d), "baba") == 0);
        assert(strcmp(sz_string_cstr(t0), "") == 0);
        assert(strcmp(sz_string_cstr(d0), "ababa") == 0);
        assert(strcmp(sz_string_cstr(over), "ababa") == 0);
        assert(strcmp(sz_string_cstr(gone), "") == 0);
        assert(strcmp(sz_string_cstr(tr), "ba") == 0);
        assert(strcmp(sz_string_cstr(dr), "abab") == 0);
        {
          SzString *rev = sz_string_reverse(sz_string_from_cstr("abc"));
          SzString *empty = sz_string_reverse(sz_string_from_cstr(""));
          assert(strcmp(sz_string_cstr(rev), "cba") == 0);
          assert(strcmp(sz_string_cstr(empty), "") == 0);
          sz_string_free(rev);
          sz_string_free(empty);
        }
        sz_string_free(t);
        sz_string_free(d);
        sz_string_free(t0);
        sz_string_free(d0);
        sz_string_free(over);
        sz_string_free(gone);
        sz_string_free(tr);
        sz_string_free(dr);
        sz_string_free(hay);
        sz_string_free(ba);
      }
      sz_string_free(pre);
      sz_string_free(keep);
      sz_string_free(suf);
      sz_string_free(left);
      sz_string_free(right);
      sz_string_free(shortp);
      sz_string_free(nopad);
    }
    {
      SzString *tr = sz_string_trim(sz_string_from_cstr("  foo\t"));
      assert(strcmp(sz_string_cstr(tr), "foo") == 0);
      tr = sz_string_trim(sz_string_from_cstr("bar"));
      assert(strcmp(sz_string_cstr(tr), "bar") == 0);
      tr = sz_string_trim(sz_string_from_cstr(" \t\n"));
      assert(sz_string_len(tr) == 0);
    }
    assert(strcmp(sz_string_cstr(sz_string_from_int(42)), "42") == 0);
    assert(strcmp(sz_string_cstr(sz_string_from_float(1.5)), "1.5") == 0);
    assert(strcmp(sz_string_cstr(sz_string_from_float(2.0)), "2.0") == 0);
    assert(strcmp(sz_string_cstr(sz_string_from_float(-1.5)), "-1.5") == 0);
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

  /* Structural ptr eq: lists, maps, ADTs, pairs, Either, errors. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *a;
    SzString *a2;
    SzString *b;
    SzString *c;
    SzString *v1;
    SzString *v2;
    SzList *xs;
    SzList *xs_tail;
    SzList *ys;
    SzList *ys_tail;
    SzList *zs;
    SzList *zs_tail;
    SzList *inner1;
    SzList *inner2;
    SzList *nest1;
    SzList *nest2;
    SzMap *m0;
    SzMap *m0b;
    SzMap *m1;
    SzMap *m2;
    SzMap *m3;
    SzAdt *red1;
    SzAdt *red2;
    SzAdt *blue;
    SzAdt *some1;
    SzAdt *some2;
    SzAdt *none;
    void *box1;
    void *box2;
    SzPair *p1;
    SzPair *p2;
    SzPair *p3;
    SzEither *er1;
    SzEither *er2;
    SzEither *el1;
    SzError *err1;
    SzError *err2;
    SzError *err3;
    sz_alloc_stats(&base_bytes, &base_count);
    a = sz_string_from_cstr("a");
    a2 = sz_string_from_cstr("a");
    b = sz_string_from_cstr("b");
    c = sz_string_from_cstr("c");
    xs_tail = sz_list_cons(b, NULL);
    xs = sz_list_cons(a, xs_tail);
    ys_tail = sz_list_cons(b, NULL);
    ys = sz_list_cons(a2, ys_tail);
    zs_tail = sz_list_cons(c, NULL);
    zs = sz_list_cons(a, zs_tail);
    assert(sz_ptr_eq(xs, ys) == 1);
    assert(sz_ptr_eq(xs, zs) == 0);
    assert(sz_ptr_eq(xs, NULL) == 0);
    inner1 = sz_list_cons(a, NULL);
    inner2 = sz_list_cons(a2, NULL);
    nest1 = sz_list_cons(inner1, NULL);
    nest2 = sz_list_cons(inner2, NULL);
    assert(sz_ptr_eq(nest1, nest2) == 1);
    v1 = sz_string_from_cstr("1");
    v2 = sz_string_from_cstr("2");
    m0 = sz_map_set(NULL, a, v1, 1);
    m1 = sz_map_set(m0, b, v2, 1);
    m0b = sz_map_set(NULL, b, v2, 1);
    m2 = sz_map_set(m0b, a, v1, 1);
    m3 = sz_map_set(NULL, a, v2, 1);
    assert(sz_ptr_eq(m1, m2) == 1);
    assert(sz_ptr_eq(m1, m3) == 0);
    red1 = sz_adt_new(0, NULL);
    red2 = sz_adt_new(0, NULL);
    blue = sz_adt_new(1, NULL);
    assert(sz_ptr_eq(red1, red2) == 1);
    assert(sz_ptr_eq(red1, blue) == 0);
    box1 = sz_box_i64(1);
    box2 = sz_box_i64(1);
    some1 = sz_adt_new(0, box1);
    some2 = sz_adt_new(0, box2);
    none = sz_adt_new(1, NULL);
    assert(sz_ptr_eq(some1, some2) == 1);
    assert(sz_ptr_eq(some1, none) == 0);
    p1 = sz_pair_new(a, b);
    p2 = sz_pair_new(a2, b);
    p3 = sz_pair_new(a, c);
    assert(sz_ptr_eq(p1, p2) == 1);
    assert(sz_ptr_eq(p1, p3) == 0);
    er1 = sz_either_right(a);
    er2 = sz_either_right(a2);
    err1 = sz_error_new(1, "boom");
    el1 = sz_either_left(err1);
    assert(sz_ptr_eq(er1, er2) == 1);
    assert(sz_ptr_eq(er1, el1) == 0);
    err2 = sz_error_new(1, "boom");
    err3 = sz_error_new(2, "boom");
    assert(sz_ptr_eq(err1, err2) == 1);
    assert(sz_ptr_eq(err1, err3) == 0);
    sz_release(err3);
    sz_release(err2);
    sz_either_free(el1);
    sz_release(err1);
    sz_either_free(er2);
    sz_either_free(er1);
    sz_pair_free(p3);
    sz_pair_free(p2);
    sz_pair_free(p1);
    sz_release(none);
    sz_release(some2);
    sz_release(some1);
    sz_release(box2);
    sz_release(box1);
    sz_release(blue);
    sz_release(red2);
    sz_release(red1);
    sz_release(m3);
    sz_release(m2);
    sz_release(m0b);
    sz_release(m1);
    sz_release(m0);
    sz_string_free(v2);
    sz_string_free(v1);
    sz_list_free(nest2);
    sz_list_free(nest1);
    sz_list_free(inner2);
    sz_list_free(inner1);
    sz_list_free(zs);
    sz_list_free(zs_tail);
    sz_list_free(ys);
    sz_list_free(ys_tail);
    sz_list_free(xs);
    sz_list_free(xs_tail);
    sz_string_free(c);
    sz_string_free(b);
    sz_string_free(a2);
    sz_string_free(a);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

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
    {
      SzList *xs = (SzList *)r.value;
      int saw_file = 0;
      while (xs && !sz_list_is_empty(xs)) {
        SzPair *ent = (SzPair *)sz_list_head(xs);
        SzString *name = (SzString *)sz_pair_left(ent);
        int64_t is_dir = sz_unbox_i64(sz_pair_right(ent));
        if (strcmp(sz_string_cstr(name), "test_fs_roundtrip.txt") == 0) {
          saw_file = 1;
          assert(is_dir == 0);
        }
        xs = sz_list_tail(xs);
      }
      assert(saw_file);
    }
    r = sz_io_unsafe_run(sz_fs_exists(sz_string_from_cstr(path)));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 1);
    r = sz_io_unsafe_run(sz_fs_exists(sz_string_from_cstr("build/missing-fs-b1")));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
    {
      SzString *j = sz_fs_join(sz_string_from_cstr("a"), sz_string_from_cstr("b"));
      SzString *d = sz_fs_dirname(sz_string_from_cstr("a/b/c"));
      SzString *b = sz_fs_basename(sz_string_from_cstr("a/b/c"));
      assert(strcmp(sz_string_cstr(j), "a/b") == 0);
      assert(strcmp(sz_string_cstr(d), "a/b") == 0);
      assert(strcmp(sz_string_cstr(b), "c") == 0);
      sz_release(j);
      sz_release(d);
      sz_release(b);
    }
    r = sz_io_unsafe_run(
        sz_fs_write(sz_string_from_cstr("build/b2_src.txt"), sz_string_from_cstr("src")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_rename(sz_string_from_cstr("build/b2_src.txt"),
                                     sz_string_from_cstr("build/b2_dst.txt")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_exists(sz_string_from_cstr("build/b2_src.txt")));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
    r = sz_io_unsafe_run(sz_fs_read(sz_string_from_cstr("build/b2_dst.txt")));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "src") == 0);
    r = sz_io_unsafe_run(sz_fs_delete(sz_string_from_cstr("build/b2_dst.txt")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_exists(sz_string_from_cstr("build/b2_dst.txt")));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
    r = sz_io_unsafe_run(sz_fs_mkdirs(sz_string_from_cstr("build/b2_walk/a")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_write(sz_string_from_cstr("build/b2_walk/a/x.txt"),
                                    sz_string_from_cstr("w")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_walk(sz_string_from_cstr("build/b2_walk")));
    assert(r.ok);
    {
      SzList *xs = (SzList *)r.value;
      int saw_dir = 0;
      int saw_file = 0;
      while (xs && !sz_list_is_empty(xs)) {
        SzPair *ent = (SzPair *)sz_list_head(xs);
        SzString *name = (SzString *)sz_pair_left(ent);
        int64_t is_dir = sz_unbox_i64(sz_pair_right(ent));
        if (strcmp(sz_string_cstr(name), "a") == 0) {
          saw_dir = 1;
          assert(is_dir == 1);
        }
        if (strcmp(sz_string_cstr(name), "a/x.txt") == 0) {
          saw_file = 1;
          assert(is_dir == 0);
        }
        xs = sz_list_tail(xs);
      }
      assert(saw_dir);
      assert(saw_file);
    }
    r = sz_io_unsafe_run(sz_fs_rename(sz_string_from_cstr("build/b2_walk/a/x.txt"),
                                     sz_string_from_cstr("build/b2_walk/a/y.txt")));
    assert(r.ok);
    r = sz_io_unsafe_run(
        sz_fs_rename(sz_string_from_cstr("build/b2_walk/a/y.txt"),
                     sz_string_from_cstr("build/b2_missing/z.txt")));
    assert(!r.ok);
    sz_error_free(r.error);
    r = sz_io_unsafe_run(sz_fs_write(sz_string_from_cstr("build/b2_walk/a/clash.txt"),
                                    sz_string_from_cstr("c")));
    assert(r.ok);
    r = sz_io_unsafe_run(
        sz_fs_rename(sz_string_from_cstr("build/b2_walk/a/y.txt"),
                     sz_string_from_cstr("build/b2_walk/a/clash.txt")));
    assert(!r.ok);
    sz_error_free(r.error);
    r = sz_io_unsafe_run(sz_fs_delete(sz_string_from_cstr("build/b2_walk")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_exists(sz_string_from_cstr("build/b2_walk")));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
    r = sz_io_unsafe_run(sz_fs_canonicalize(sz_string_from_cstr("build")));
    assert(r.ok);
    assert(sz_string_len((SzString *)r.value) > 0);

    r = sz_io_unsafe_run(sz_fs_read(sz_string_from_cstr("build")));
    assert(!r.ok);
    sz_error_free(r.error);

    r = sz_io_unsafe_run(sz_fs_write(
        sz_string_from_cstr("build/test_fs_mkdirs_file"), sz_string_from_cstr("x")));
    assert(r.ok);
    r = sz_io_unsafe_run(
        sz_fs_mkdirs(sz_string_from_cstr("build/test_fs_mkdirs_file")));
    assert(!r.ok);
    sz_error_free(r.error);
    remove("build/test_fs_mkdirs_file");
    remove(path);
  }

  /* TestRuntime: Fs graph built before install still uses mem-FS. */
  {
    SzIo *mk = sz_fs_mkdirs(sz_string_from_cstr("step"));
    SzIo *w = sz_fs_write(sz_string_from_cstr("step/x.txt"),
                          sz_string_from_cstr("step-mem"));
    SzIo *rd = sz_fs_read(sz_string_from_cstr("step/x.txt"));
    SzIo *ls = sz_fs_list(sz_string_from_cstr("step"));
    sz_testrt_install();
    r = sz_io_unsafe_run(mk);
    assert(r.ok);
    r = sz_io_unsafe_run(w);
    assert(r.ok);
    r = sz_io_unsafe_run(rd);
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "step-mem") == 0);
    r = sz_io_unsafe_run(ls);
    assert(r.ok);
    assert(!sz_list_is_empty((SzList *)r.value));
    {
      SzList *xs = (SzList *)r.value;
      SzPair *ent = (SzPair *)sz_list_head(xs);
      SzString *name = (SzString *)sz_pair_left(ent);
      int64_t is_dir = sz_unbox_i64(sz_pair_right(ent));
      assert(strcmp(sz_string_cstr(name), "x.txt") == 0);
      assert(is_dir == 0);
    }
    r = sz_io_unsafe_run(sz_fs_exists(sz_string_from_cstr("step/x.txt")));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 1);
    r = sz_io_unsafe_run(sz_fs_exists(sz_string_from_cstr("missing-mem")));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
    r = sz_io_unsafe_run(sz_fs_rename(sz_string_from_cstr("step/x.txt"),
                                    sz_string_from_cstr("step/y.txt")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_read(sz_string_from_cstr("step/y.txt")));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "step-mem") == 0);
    r = sz_io_unsafe_run(sz_fs_walk(sz_string_from_cstr("step")));
    assert(r.ok);
    {
      SzList *xs = (SzList *)r.value;
      SzPair *ent = (SzPair *)sz_list_head(xs);
      SzString *name = (SzString *)sz_pair_left(ent);
      int64_t is_dir = sz_unbox_i64(sz_pair_right(ent));
      assert(strcmp(sz_string_cstr(name), "y.txt") == 0);
      assert(is_dir == 0);
    }
    r = sz_io_unsafe_run(sz_fs_delete(sz_string_from_cstr("step")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_exists(sz_string_from_cstr("step")));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
    assert(access("step/x.txt", F_OK) != 0);
    sz_testrt_reset();
  }

  /* TestRuntime: fake clock sleep without wall wait */
  {
    int64_t t0, t1;
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    sz_testrt_install();
    sz_testrt_net_stub("http://example.test/ping", "pong");
    t0 = sz_testrt_clock_now_ms();
    r = sz_io_unsafe_run(sz_io_sleep_ms(1000));
    assert(r.ok);
    t1 = sz_testrt_clock_now_ms();
    assert(t1 == t0 + 1000);

    {
      int64_t saved = sz_testrt_clock_now_ms();
      r = sz_io_unsafe_run(sz_io_sleep_ms(INT64_MAX));
      assert(r.ok);
      assert(sz_testrt_clock_now_ms() == INT64_MAX);
      sz_testrt_clock_install(saved);
    }

    r = sz_io_unsafe_run(sz_clock_real_time());
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == t1);

    r = sz_io_unsafe_run(sz_random_next_int(10));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) >= 0 && sz_unbox_i64(r.value) < 10);
    sz_release(r.value);

    {
      int i;
      int saw_hi = 0;
      int64_t bound = (int64_t)1 << 40;
      for (i = 0; i < 64; i++) {
        r = sz_io_unsafe_run(sz_random_next_int(bound));
        assert(r.ok);
        assert(sz_unbox_i64(r.value) >= 0 && sz_unbox_i64(r.value) < bound);
        if (sz_unbox_i64(r.value) >= ((int64_t)1 << 31))
          saw_hi = 1;
        sz_release(r.value);
      }
      assert(saw_hi);
    }

    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(sz_random_next_int(10));
    assert(r.ok);
    sz_release(r.value);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    r = sz_io_unsafe_run(sz_fs_mkdirs(sz_string_from_cstr("a")));
    assert(r.ok);
    r = sz_io_unsafe_run(
        sz_fs_write(sz_string_from_cstr("a/b.txt"), sz_string_from_cstr("mem")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_read(sz_string_from_cstr("a/b.txt")));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "mem") == 0);
    sz_release(r.value);
    r = sz_io_unsafe_run(sz_io_sleep_ms(50));
    assert(r.ok);
    r = sz_io_unsafe_run(
        sz_fs_write(sz_string_from_cstr("a/b.txt"), sz_string_from_cstr("poll")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_read(sz_string_from_cstr("a/b.txt")));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "poll") == 0);
    sz_release(r.value);

    r = sz_io_unsafe_run(sz_fs_mkdirs(sz_string_from_cstr("tree/a")));
    assert(r.ok);
    r = sz_io_unsafe_run(
        sz_fs_write(sz_string_from_cstr("tree/a/x.txt"), sz_string_from_cstr("w")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_walk(sz_string_from_cstr("tree")));
    assert(r.ok);
    {
      SzList *xs = (SzList *)r.value;
      int saw_dir = 0;
      int saw_file = 0;
      while (xs && !sz_list_is_empty(xs)) {
        SzPair *ent = (SzPair *)sz_list_head(xs);
        SzString *name = (SzString *)sz_pair_left(ent);
        int64_t is_dir = sz_unbox_i64(sz_pair_right(ent));
        if (strcmp(sz_string_cstr(name), "a") == 0) {
          saw_dir = 1;
          assert(is_dir == 1);
        }
        if (strcmp(sz_string_cstr(name), "a/x.txt") == 0) {
          saw_file = 1;
          assert(is_dir == 0);
        }
        xs = sz_list_tail(xs);
      }
      assert(saw_dir);
      assert(saw_file);
    }
    r = sz_io_unsafe_run(
        sz_fs_rename(sz_string_from_cstr("tree/a"), sz_string_from_cstr("tree/b")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_read(sz_string_from_cstr("tree/b/x.txt")));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "w") == 0);
    sz_release(r.value);
    r = sz_io_unsafe_run(
        sz_fs_rename(sz_string_from_cstr("tree/b/x.txt"),
                     sz_string_from_cstr("missing/z.txt")));
    assert(!r.ok);
    sz_error_free(r.error);
    r = sz_io_unsafe_run(sz_fs_write(sz_string_from_cstr("tree/b/clash.txt"),
                                    sz_string_from_cstr("c")));
    assert(r.ok);
    r = sz_io_unsafe_run(
        sz_fs_rename(sz_string_from_cstr("tree/b/x.txt"),
                     sz_string_from_cstr("tree/b/clash.txt")));
    assert(!r.ok);
    sz_error_free(r.error);
    r = sz_io_unsafe_run(sz_fs_delete(sz_string_from_cstr("tree")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_exists(sz_string_from_cstr("tree")));
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
    r = sz_io_unsafe_run(sz_fs_delete(sz_string_from_cstr("missing-del")));
    assert(!r.ok);
    sz_error_free(r.error);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *file = sz_string_from_cstr("a/b.txt");
      SzString *dir = sz_string_from_cstr("a");
      r = sz_io_unsafe_run(sz_fs_read(file));
      assert(r.ok);
      sz_release(r.value);
      r = sz_io_unsafe_run(sz_fs_list(dir));
      assert(r.ok);
      sz_release(r.value);
      r = sz_io_unsafe_run(sz_fs_walk(dir));
      assert(r.ok);
      sz_release(r.value);
      r = sz_io_unsafe_run(sz_fs_mkdirs(dir));
      assert(r.ok);
      r = sz_io_unsafe_run(sz_fs_canonicalize(dir));
      assert(r.ok);
      sz_release(r.value);
      sz_release(file);
      sz_release(dir);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    r = sz_io_unsafe_run(
        sz_fs_write(sz_string_from_cstr("missing/x.txt"), sz_string_from_cstr("z")));
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "no parent") != NULL);
    sz_error_free(r.error);
    assert(access("missing/x.txt", F_OK) != 0);
    assert(access("missing", F_OK) != 0);

    r = sz_io_unsafe_run(
        sz_fs_write(sz_string_from_cstr("not-a-dir"), sz_string_from_cstr("x")));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_fs_mkdirs(sz_string_from_cstr("not-a-dir")));
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "path is file (mem)") !=
               NULL);
    sz_error_free(r.error);

    {
      char deep[1024];
      size_t n = 0;
      int i;
      for (i = 0; i < 260; i++) {
        if (i)
          deep[n++] = '/';
        deep[n++] = 'a';
      }
      deep[n] = '\0';
      r = sz_io_unsafe_run(sz_fs_canonicalize(sz_string_from_cstr(deep)));
      assert(!r.ok);
      assert(r.error &&
             strstr(sz_string_cstr(r.error->message), "path too deep") != NULL);
      sz_error_free(r.error);
    }

    r = sz_io_unsafe_run(
        sz_net_http_get(sz_string_from_cstr("http://example.test/ping")));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "pong") == 0);
    sz_release(r.value);

    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *url = sz_string_from_cstr("http://example.test/ping");
      r = sz_io_unsafe_run(sz_net_http_get(url));
      sz_release(url);
      assert(r.ok);
      sz_release(r.value);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_testrt_net_inject_request("/hello");
    r = sz_io_unsafe_run(sz_net_serve_once(8080, serve_path_ok, NULL));
    assert(r.ok);
    assert(strcmp(sz_testrt_net_last_serve_body(), "ok:/hello") == 0);

    sz_testrt_net_set_last_serve_body(NULL);
    sz_alloc_stats(&base_bytes, &base_count);
    sz_testrt_net_inject_request("/hello");
    r = sz_io_unsafe_run(sz_net_serve_once(8080, serve_path_ok, NULL));
    assert(r.ok);
    sz_testrt_net_set_last_serve_body(NULL);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    sz_testrt_net_inject_request("/a");
    sz_testrt_net_queue_request("/b");
    r = sz_io_unsafe_run(sz_net_serve(8080, serve_path_ok, NULL));
    assert(r.ok);
    assert(strcmp(sz_testrt_net_last_serve_body(), "ok:/b") == 0);
    assert(sz_testrt_net_serve_pending() == 0);

    sz_testrt_net_set_last_serve_body(NULL);
    sz_alloc_stats(&base_bytes, &base_count);
    sz_testrt_net_inject_request("/a");
    sz_testrt_net_queue_request("/b");
    r = sz_io_unsafe_run(sz_net_serve(8080, serve_path_ok, NULL));
    assert(r.ok);
    sz_testrt_net_set_last_serve_body(NULL);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);

    g_serve_fail_n = 0;
    sz_testrt_net_inject_request("/a");
    sz_testrt_net_queue_request("/b");
    r = sz_io_unsafe_run(sz_net_serve(8080, serve_fail_then_ok, NULL));
    assert(r.ok);
    assert(strcmp(sz_testrt_net_last_serve_body(), "ok:/b") == 0);

    /* Persistent serve: many rounds must not grow the continuation stack. */
    g_serve_rounds = 0;
    memset(g_serve_bytes, 0, sizeof g_serve_bytes);
    memset(g_serve_count, 0, sizeof g_serve_count);
    memset(g_serve_raw, 0, sizeof g_serve_raw);
    memset(g_serve_io, 0, sizeof g_serve_io);
    sz_testrt_net_set_last_serve_body(NULL);
    sz_testrt_net_inject_request("/x");
    r = sz_io_unsafe_run(sz_net_serve(8080, serve_leak_ok, NULL));
    assert(r.ok);
    assert(g_serve_rounds == SERVE_LEAK_N);
    assert(g_serve_count[1] <= g_serve_count[0] + 2);
    assert(g_serve_raw[1] <= g_serve_raw[0] + 2);
    assert(g_serve_io[1] <= g_serve_io[0] + 2);
    assert(g_serve_bytes[1] <= g_serve_bytes[0] + 256);
    sz_testrt_net_set_last_serve_body(NULL);

    sz_testrt_net_inject_request("/");
    r = sz_io_unsafe_run(sz_net_serve((int64_t)((1ULL << 32) | 8080),
                                     serve_path_ok, NULL));
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "port must be 1..65535") !=
               NULL);
    sz_error_free(r.error);

    sz_testrt_net_inject_request("/");
    r = sz_io_unsafe_run(sz_net_serve(-4294967216LL, serve_path_ok, NULL));
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "port must be 1..65535") !=
               NULL);
    sz_error_free(r.error);

    /* Virtual Net: loopback httpGet parks on a mailbox; serveOnce parks on
     * accept. Stubs still win. Injects stay ghost requests. */
    {
      int64_t port = 0;
      char path[64];
      SzPair *pair;
      SzString *url;
      char *inj;

      while ((inj = sz_testrt_net_pop_request()) != NULL)
        sz_free(inj);

      assert(sz_testrt_net_parse_loopback("http://127.0.0.1:8080/ping", &port,
                                         path, sizeof path) == 1);
      assert(port == 8080);
      assert(strcmp(path, "/ping") == 0);
      assert(sz_testrt_net_parse_loopback("http://localhost/x", &port, path,
                                         sizeof path) == 1);
      assert(port == 80);
      assert(strcmp(path, "/x") == 0);
      assert(sz_testrt_net_parse_loopback("http://[::1]:9/a", &port, path,
                                         sizeof path) == 1);
      assert(port == 9);
      assert(strcmp(path, "/a") == 0);
      assert(sz_testrt_net_parse_loopback("http://example.test/x", &port, path,
                                         sizeof path) == 0);
      assert(sz_testrt_net_parse_loopback("https://127.0.0.1/x", &port, path,
                                         sizeof path) == 0);
      assert(sz_testrt_net_parse_loopback("http://127.0.0.1:99999/x", &port,
                                         path, sizeof path) == 0);

      sz_testrt_net_set_last_serve_body(NULL);
      url = sz_string_from_cstr("http://127.0.0.1:8080/ping");
      r = sz_io_unsafe_run(both_drop(sz_net_serve_once(8080, serve_path_ok, NULL),
                                    sz_net_http_get(url)));
      sz_release(url);
      assert(r.ok);
      pair = (SzPair *)r.value;
      assert(pair && pair->right);
      assert(strcmp(sz_string_cstr((SzString *)pair->right), "ok:/ping") == 0);
      assert(strcmp(sz_testrt_net_last_serve_body(), "ok:/ping") == 0);
      assert(sz_testrt_net_serve_pending() == 0);
      sz_pair_free(pair);

      sz_testrt_net_set_last_serve_body(NULL);
      url = sz_string_from_cstr("http://127.0.0.1:8080/pong");
      r = sz_io_unsafe_run(both_drop(sz_net_http_get(url),
                                    sz_net_serve_once(8080, serve_path_ok, NULL)));
      sz_release(url);
      assert(r.ok);
      pair = (SzPair *)r.value;
      assert(pair && pair->left);
      assert(strcmp(sz_string_cstr((SzString *)pair->left), "ok:/pong") == 0);
      sz_pair_free(pair);

      sz_testrt_net_set_last_serve_body(NULL);
      url = sz_string_from_cstr("http://localhost:8080/hi");
      r = sz_io_unsafe_run(both_drop(sz_net_serve_once(8080, serve_path_ok, NULL),
                                    sz_net_http_get(url)));
      sz_release(url);
      assert(r.ok);
      pair = (SzPair *)r.value;
      assert(pair && pair->right);
      assert(strcmp(sz_string_cstr((SzString *)pair->right), "ok:/hi") == 0);
      sz_pair_free(pair);

      sz_testrt_net_set_last_serve_body(NULL);
      url = sz_string_from_cstr("http://[::1]:8080/v6");
      r = sz_io_unsafe_run(both_drop(sz_net_serve_once(8080, serve_path_ok, NULL),
                                    sz_net_http_get(url)));
      sz_release(url);
      assert(r.ok);
      pair = (SzPair *)r.value;
      assert(pair && pair->right);
      assert(strcmp(sz_string_cstr((SzString *)pair->right), "ok:/v6") == 0);
      sz_pair_free(pair);

      sz_testrt_net_stub("http://127.0.0.1:8080/stub", "from-stub");
      url = sz_string_from_cstr("http://127.0.0.1:8080/stub");
      r = sz_io_unsafe_run(sz_net_http_get(url));
      sz_release(url);
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "from-stub") == 0);
      sz_release(r.value);

      sz_testrt_net_set_last_serve_body(NULL);
      sz_alloc_stats(&base_bytes, &base_count);
      url = sz_string_from_cstr("http://127.0.0.1:8080/leak");
      r = sz_io_unsafe_run(both_drop(sz_net_serve_once(8080, serve_path_ok, NULL),
                                    sz_net_http_get(url)));
      sz_release(url);
      assert(r.ok);
      sz_pair_free((SzPair *)r.value);
      sz_testrt_net_set_last_serve_body(NULL);
      sz_alloc_stats(&live_bytes, &live_count);
      assert(live_count == base_count);
      assert(live_bytes == base_bytes);
    }

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

      {
        size_t base_bytes = 0, base_count = 0;
        size_t live_bytes = 0, live_count = 0;
        sz_testrt_stdin_feed("xy");
        sz_alloc_stats(&base_bytes, &base_count);
        r = sz_io_unsafe_run(sz_sys_read(2));
        assert(r.ok);
        sz_release(r.value);
        sz_alloc_stats(&live_bytes, &live_count);
        assert(live_count == base_count);
        assert(live_bytes == base_bytes);
      }
      r = sz_io_unsafe_run(sz_sys_write(sz_string_from_cstr("raw")));
      assert(r.ok);
      assert(strstr(sz_testrt_stdout_cstr(), "raw") != NULL);

      {
        size_t base_bytes = 0, base_count = 0;
        size_t live_bytes = 0, live_count = 0;
        SzString *s;
        sz_alloc_stats(&base_bytes, &base_count);
        s = sz_string_from_cstr("w");
        r = sz_io_unsafe_run(sz_sys_write(s));
        sz_release(s);
        assert(r.ok);
        sz_alloc_stats(&live_bytes, &live_count);
        assert(live_count == base_count);
        assert(live_bytes == base_bytes);
      }

      r = sz_io_unsafe_run(sz_io_println_cstr("cap"));
      assert(r.ok);
      assert(strstr(sz_testrt_stdout_cstr(), "cap\n") != NULL);

      /* Sealed env: host PATH does not leak; overlay keys resolve. */
      setenv("SCZ_LIVE_ENV_PROBE", "live", 1);
      r = sz_io_unsafe_run(sz_sys_getenv(sz_string_from_cstr("SCZ_LIVE_ENV_PROBE")));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "") == 0);
      r = sz_io_unsafe_run(sz_sys_getenv(sz_string_from_cstr("PATH")));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "") == 0);
      sz_testrt_env_set("SCUZZ_KIT", "sealed");
      r = sz_io_unsafe_run(sz_sys_getenv(sz_string_from_cstr("SCUZZ_KIT")));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "sealed") == 0);
      sz_release(r.value);

      {
        size_t base_bytes = 0, base_count = 0;
        size_t live_bytes = 0, live_count = 0;
        SzString *key;
        sz_alloc_stats(&base_bytes, &base_count);
        key = sz_string_from_cstr("SCUZZ_KIT");
        r = sz_io_unsafe_run(sz_sys_getenv(key));
        sz_release(key);
        assert(r.ok);
        sz_release(r.value);
        sz_alloc_stats(&live_bytes, &live_count);
        assert(live_count == base_count);
        assert(live_bytes == base_bytes);
      }

      /* Fake pid table: host pid is dead unless registered; kill never SIGTERM. */
      r = sz_io_unsafe_run(sz_sys_alive((int64_t)getpid()));
      assert(r.ok);
      assert(sz_unbox_i64(r.value) == 0);
      sz_testrt_proc_put(4242);
      r = sz_io_unsafe_run(sz_sys_alive(4242));
      assert(r.ok);
      assert(sz_unbox_i64(r.value) == 1);
      r = sz_io_unsafe_run(sz_sys_kill(4242));
      assert(r.ok);
      r = sz_io_unsafe_run(sz_sys_alive(4242));
      assert(r.ok);
      assert(sz_unbox_i64(r.value) == 0);
      r = sz_io_unsafe_run(sz_sys_kill((int64_t)getpid()));
      assert(r.ok);

      {
        size_t base_bytes = 0, base_count = 0;
        size_t live_bytes = 0, live_count = 0;
        sz_alloc_stats(&base_bytes, &base_count);
        sz_testrt_proc_put(7);
        r = sz_io_unsafe_run(sz_sys_alive(7));
        assert(r.ok);
        sz_release(r.value);
        r = sz_io_unsafe_run(sz_sys_kill(7));
        assert(r.ok);
        sz_alloc_stats(&live_bytes, &live_count);
        assert(live_count == base_count);
        assert(live_bytes == base_bytes);
      }
    }

    {
      size_t base_bytes = 0, base_count = 0;
      size_t live_bytes = 0, live_count = 0;
      char *argv[] = {"alpha", "beta"};
      int i;
      sz_testrt_net_stub("http://example.test/v1", "stub-body");
      sz_testrt_sys_set_args(2, argv);
      sz_testrt_stdin_feed("hello-line\n");
      r = sz_io_unsafe_run(sz_fs_write(sz_string_from_cstr("note.txt"),
                                      sz_string_from_cstr("kit-note")));
      assert(r.ok);
      for (i = 0; i < 64; i++)
        sz_testrt_stdout_append("warm-line-for-cap");
      sz_testrt_stdout_reset();
      sz_alloc_stats(&base_bytes, &base_count);
      r = sz_io_unsafe_run(sz_impurity_run_kit());
      assert(r.ok);
      assert(strstr(sz_testrt_stdout_cstr(), "args:") != NULL);
      assert(strstr(sz_testrt_stdout_cstr(), "line:") != NULL);
      assert(strstr(sz_testrt_stdout_cstr(), "impurity-ok\n") != NULL);
      sz_alloc_stats(&live_bytes, &live_count);
      assert(live_count == base_count);
      assert(live_bytes == base_bytes);
    }

    sz_testrt_reset();
    assert(!sz_testrt_clock_is_fake());
    assert(!sz_testrt_fs_is_fake());
    assert(!sz_testrt_sys_is_fake());
    r = sz_io_unsafe_run(sz_impurity_run_kit());
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "TestRuntime") != NULL);
    sz_error_free(r.error);
    r = sz_io_unsafe_run(sz_sys_getenv(sz_string_from_cstr("SCZ_LIVE_ENV_PROBE")));
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "live") == 0);
    unsetenv("SCZ_LIVE_ENV_PROBE");
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
    r = sz_io_unsafe_run(race_drop(
        fm_drop(sz_io_sleep_ms(100), after_sleep_tag, (void *)(intptr_t)100),
        fm_drop(sz_io_sleep_ms(1), after_sleep_tag, (void *)(intptr_t)1)));
    assert(r.ok);
    assert((intptr_t)r.value == 1);
    t1 = sz_testrt_clock_now_ms();
    assert(t1 == t0 + 1);

    /* race(sleep, println) wins println without advancing the sleep. */
    t0 = sz_testrt_clock_now_ms();
    sz_testrt_stdout_reset();
    r = sz_io_unsafe_run(
        race_drop(sz_io_sleep_ms(50), sz_io_println_cstr("race-win")));
    assert(r.ok);
    assert(sz_testrt_clock_now_ms() == t0);
    assert(strstr(sz_testrt_stdout_cstr(), "race-win\n") != NULL);

    /* both println order is stable across runs. */
    sz_testrt_stdout_reset();
    r = sz_io_unsafe_run(
        both_drop(sz_io_println_cstr("a"), sz_io_println_cstr("b")));
    assert(r.ok);
    {
      char saved[64];
      const char *s = sz_testrt_stdout_cstr();
      assert(strlen(s) < sizeof(saved));
      memcpy(saved, s, strlen(s) + 1);
      sz_testrt_stdout_reset();
      r = sz_io_unsafe_run(
          both_drop(sz_io_println_cstr("a"), sz_io_println_cstr("b")));
      assert(r.ok);
      out2 = sz_testrt_stdout_cstr();
      assert(strcmp(saved, out2) == 0);
      assert(strstr(saved, "a\nb\n") != NULL);
      (void)out1;
    }

    /* both(take, offer) parks take until offer wakes it. */
    q = sz_queue_make();
    r = sz_io_unsafe_run(
        both_drop(sz_queue_take(q), sz_queue_offer_cstr(q, "parked")));
    assert(r.ok);
    {
      SzPair *p = (SzPair *)r.value;
      assert(p);
      assert(strcmp(sz_string_cstr((SzString *)p->left), "parked") == 0);
      sz_pair_free(p);
    }
    sz_queue_free(q);

    /* Two parked takes get offers in waiter order (oldest take first). */
    q = sz_queue_make();
    r = sz_io_unsafe_run(both_drop(
        both_drop(sz_queue_take(q), sz_queue_take(q)),
        both_drop(sz_queue_offer_cstr(q, "a"), sz_queue_offer_cstr(q, "b"))));
    assert(r.ok);
    {
      SzPair *outer = (SzPair *)r.value;
      SzPair *takes;
      assert(outer);
      takes = (SzPair *)outer->left;
      assert(takes);
      assert(strcmp(sz_string_cstr((SzString *)takes->left), "a") == 0);
      assert(strcmp(sz_string_cstr((SzString *)takes->right), "b") == 0);
      sz_pair_free(outer);
    }
    sz_queue_free(q);

    /* both(get, complete) parks get until complete wakes it. */
    def = sz_deferred_make();
    r = sz_io_unsafe_run(both_drop(sz_deferred_get(def),
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

  /* PCT (SCUZZ_SCHED_SEED): packed seed is deterministic; FIFO stays a then b. */
  {
    char saved[64];
    const char *s;
    int seed;
    int reversed = 0;
    char seedbuf[16];

    sz_testrt_install();
    unsetenv("SCUZZ_PCT_D");
    unsetenv("SCUZZ_PCT_K");
    setenv("SCUZZ_SCHED_SEED", "0", 1);

    sz_testrt_stdout_reset();
    r = sz_io_unsafe_run(
        both_drop(sz_io_println_cstr("a"), sz_io_println_cstr("b")));
    assert(r.ok);
    s = sz_testrt_stdout_cstr();
    assert(strlen(s) < sizeof(saved));
    memcpy(saved, s, strlen(s) + 1);

    sz_testrt_stdout_reset();
    r = sz_io_unsafe_run(
        both_drop(sz_io_println_cstr("a"), sz_io_println_cstr("b")));
    assert(r.ok);
    assert(strcmp(saved, sz_testrt_stdout_cstr()) == 0);

    unsetenv("SCUZZ_SCHED_SEED");
    sz_testrt_stdout_reset();
    r = sz_io_unsafe_run(
        both_drop(sz_io_println_cstr("a"), sz_io_println_cstr("b")));
    assert(r.ok);
    assert(strstr(sz_testrt_stdout_cstr(), "a\nb\n") != NULL);

    for (seed = 0; seed <= 64; seed++) {
      snprintf(seedbuf, sizeof seedbuf, "%d", seed);
      setenv("SCUZZ_SCHED_SEED", seedbuf, 1);
      sz_testrt_stdout_reset();
      r = sz_io_unsafe_run(
          both_drop(sz_io_println_cstr("a"), sz_io_println_cstr("b")));
      assert(r.ok);
      if (strstr(sz_testrt_stdout_cstr(), "b\na\n") != NULL)
        reversed = 1;
    }
    assert(reversed);

    /* FIFO both(offer L, offer R) then take is L. PCT can take R. Replay. */
    {
      SzQueue *q;
      int right_first = 0;
      int right_seed = -1;

      unsetenv("SCUZZ_SCHED_SEED");
      q = sz_queue_make();
      r = sz_io_unsafe_run(pct_offer_lr_then_take(q));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "L") == 0);
      sz_release(r.value);
      sz_queue_free(q);

      for (seed = 0; seed <= 64; seed++) {
        snprintf(seedbuf, sizeof seedbuf, "%d", seed);
        setenv("SCUZZ_SCHED_SEED", seedbuf, 1);
        q = sz_queue_make();
        r = sz_io_unsafe_run(pct_offer_lr_then_take(q));
        assert(r.ok);
        if (strcmp(sz_string_cstr((SzString *)r.value), "R") == 0) {
          right_first = 1;
          right_seed = seed;
          sz_release(r.value);
          sz_queue_free(q);
          break;
        }
        assert(strcmp(sz_string_cstr((SzString *)r.value), "L") == 0);
        sz_release(r.value);
        sz_queue_free(q);
      }
      assert(right_first);
      snprintf(seedbuf, sizeof seedbuf, "%d", right_seed);
      setenv("SCUZZ_SCHED_SEED", seedbuf, 1);
      q = sz_queue_make();
      r = sz_io_unsafe_run(pct_offer_lr_then_take(q));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "R") == 0);
      sz_release(r.value);
      sz_queue_free(q);
      q = sz_queue_make();
      r = sz_io_unsafe_run(pct_offer_lr_then_take(q));
      assert(r.ok);
      assert(strcmp(sz_string_cstr((SzString *)r.value), "R") == 0);
      sz_release(r.value);
      sz_queue_free(q);
    }

    unsetenv("SCUZZ_SCHED_SEED");
    sz_testrt_reset();
  }

  /* Queue.take handoff must survive race / timeout / interrupt cancel. */
  {
    SzQueue *q;
    int seed;
    int race_loser = 0;
    char seedbuf[16];
    char old_seed_buf[32];
    int had_seed = 0;
    const char *old_seed = getenv("SCUZZ_SCHED_SEED");
    if (old_seed) {
      had_seed = 1;
      snprintf(old_seed_buf, sizeof old_seed_buf, "%s", old_seed);
    }

    sz_testrt_install();

    /* Deterministic: cancel the READY take after wake, before it steps. */
    q = sz_queue_make();
    r = sz_io_unsafe_run(fm_drop(fork_drop(sz_queue_take(q)),
                                 cancel_handoff_then_join, q));
    assert(r.ok);
    assert_queue_holds_x_if_take_lost(q, 0);
    sz_queue_free(q);

    for (seed = 0; seed <= 64; seed++) {
      SzPair *p;
      int take_won;
      snprintf(seedbuf, sizeof seedbuf, "%d", seed);
      setenv("SCUZZ_SCHED_SEED", seedbuf, 1);

      /* race winner may be take or println; item must not vanish */
      q = sz_queue_make();
      r = sz_io_unsafe_run(both_drop(
          race_drop(sz_queue_take(q), sz_io_println_cstr("win")),
          sz_queue_offer_cstr(q, "x")));
      assert(r.ok);
      p = (SzPair *)r.value;
      assert(p);
      take_won = value_is_cstr(p->left, "x");
      if (!take_won)
        race_loser = 1;
      sz_pair_free(p);
      assert_queue_holds_x_if_take_lost(q, take_won);
      sz_queue_free(q);

      q = sz_queue_make();
      r = sz_io_unsafe_run(both_drop(
          handle_drop(timeout_drop(seed & 1, sz_queue_take(q)), recover_any_unit,
                      NULL),
          sz_queue_offer_cstr(q, "x")));
      assert(r.ok);
      p = (SzPair *)r.value;
      assert(p);
      take_won = value_is_cstr(p->left, "x");
      sz_pair_free(p);
      assert_queue_holds_x_if_take_lost(q, take_won);
      sz_queue_free(q);

      q = sz_queue_make();
      r = sz_io_unsafe_run(fm_drop(fork_drop(sz_queue_take(q)),
                                   fork_take_offer_interrupt, q));
      assert(r.ok);
      take_won = value_is_cstr(r.value, "x");
      if (r.value)
        sz_release(r.value);
      assert_queue_holds_x_if_take_lost(q, take_won);
      sz_queue_free(q);
    }

    assert(race_loser);
    if (had_seed)
      setenv("SCUZZ_SCHED_SEED", old_seed_buf, 1);
    else
      unsetenv("SCUZZ_SCHED_SEED");
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

  /* Sys.spawn pipes: write stdin, read stdout (dd copies five bytes). */
  {
    SzIoResult pr;
    int64_t pid;
    SzString *cmd;
    SzString *msg;
    SzString *got;
    cmd = sz_string_from_cstr("dd bs=1 count=5 2>/dev/null");
    pr = sz_io_unsafe_run(sz_sys_spawn(cmd));
    sz_release(cmd);
    assert(pr.ok);
    pid = sz_unbox_i64(pr.value);
    sz_release(pr.value);
    assert(pid > 0);
    msg = sz_string_from_cstr("hello");
    pr = sz_io_unsafe_run(sz_sys_child_write(pid, msg));
    sz_release(msg);
    assert(pr.ok);
    pr = sz_io_unsafe_run(sz_sys_child_read(pid, 5));
    assert(pr.ok);
    got = (SzString *)pr.value;
    assert(got && got->len == 5);
    assert(memcmp(sz_string_cstr(got), "hello", 5) == 0);
    sz_release(got);
    pr = sz_io_unsafe_run(sz_sys_child_close(pid));
    assert(pr.ok);
    pr = sz_io_unsafe_run(sz_sys_kill(pid));
    assert(pr.ok);
  }

  /* Close child stdin so cat flushes and later reads are EOF. */
  {
    SzIoResult pr;
    int64_t pid;
    SzString *cmd;
    SzString *msg;
    SzString *got;
    cmd = sz_string_from_cstr("cat");
    pr = sz_io_unsafe_run(sz_sys_spawn(cmd));
    sz_release(cmd);
    assert(pr.ok);
    pid = sz_unbox_i64(pr.value);
    sz_release(pr.value);
    msg = sz_string_from_cstr("xy");
    pr = sz_io_unsafe_run(sz_sys_child_write(pid, msg));
    sz_release(msg);
    assert(pr.ok);
    pr = sz_io_unsafe_run(sz_sys_child_close(pid));
    assert(pr.ok);
    pr = sz_io_unsafe_run(sz_sys_child_read(pid, 2));
    assert(pr.ok);
    got = (SzString *)pr.value;
    assert(got && got->len == 2);
    assert(memcmp(sz_string_cstr(got), "xy", 2) == 0);
    sz_release(got);
    pr = sz_io_unsafe_run(sz_sys_child_read(pid, 1));
    assert(pr.ok);
    got = (SzString *)pr.value;
    assert(got && got->len == 0);
    sz_release(got);
    pr = sz_io_unsafe_run(sz_sys_kill(pid));
    assert(pr.ok);
  }

  /* Child read parks; a peer fiber writes before the copy finishes. */
  {
    SzIoResult pr;
    SzPair *pair;
    SzString *cmd;
    SzString *got;
    cmd = sz_string_from_cstr("dd bs=1 count=5 2>/dev/null");
    pr = sz_io_unsafe_run(sz_sys_spawn(cmd));
    sz_release(cmd);
    assert(pr.ok);
    g_pipe_pid = sz_unbox_i64(pr.value);
    sz_release(pr.value);
    pr = sz_io_unsafe_run(both_drop(
        sz_sys_child_read(g_pipe_pid, 5),
        fm_drop(sz_io_sleep_ms(30), pipe_write_hello, NULL)));
    assert(pr.ok);
    pair = (SzPair *)pr.value;
    assert(pair && pair->left);
    got = (SzString *)pair->left;
    assert(got && got->len == 5);
    assert(memcmp(sz_string_cstr(got), "hello", 5) == 0);
    sz_release(pair);
    pr = sz_io_unsafe_run(sz_sys_child_close(g_pipe_pid));
    assert(pr.ok);
    pr = sz_io_unsafe_run(sz_sys_kill(g_pipe_pid));
    assert(pr.ok);
    g_pipe_pid = 0;
  }
  {
    r = sz_io_unsafe_run(sz_sys_exec(sz_string_from_cstr("true")));
    assert(r.ok);
    {
      SzPair *tup = (SzPair *)r.value;
      SzPair *io = (SzPair *)tup->right;
      assert(sz_unbox_i64(tup->left) == 0);
      assert(sz_string_len((SzString *)io->left) == 0);
      assert(sz_string_len((SzString *)io->right) == 0);
    }
    sz_release(r.value);
    r = sz_io_unsafe_run(sz_sys_exec(sz_string_from_cstr("exit 7")));
    assert(r.ok);
    {
      SzPair *tup = (SzPair *)r.value;
      assert(sz_unbox_i64(tup->left) == 7);
    }
    sz_release(r.value);
    r = sz_io_unsafe_run(
        sz_sys_exec(sz_string_from_cstr("echo out; echo err >&2; exit 3")));
    assert(r.ok);
    {
      SzPair *tup = (SzPair *)r.value;
      SzPair *io = (SzPair *)tup->right;
      assert(sz_unbox_i64(tup->left) == 3);
      assert(strcmp(sz_string_cstr((SzString *)io->left), "out\n") == 0);
      assert(strcmp(sz_string_cstr((SzString *)io->right), "err\n") == 0);
    }
    sz_release(r.value);
  }

  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *cmd = sz_string_from_cstr("true");
      r = sz_io_unsafe_run(sz_sys_exec(cmd));
      sz_release(cmd);
      assert(r.ok);
      sz_release(r.value);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    int status = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *cmd = sz_string_from_cstr("true");
      int64_t pid;
      r = sz_io_unsafe_run(sz_sys_spawn(cmd));
      sz_release(cmd);
      assert(r.ok);
      pid = sz_unbox_i64(r.value);
      sz_release(r.value);
      assert(waitpid((pid_t)pid, &status, 0) == (pid_t)pid);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* TestRuntime rejects Sys.exec / Sys.spawn so sim cannot fork a child. */
  {
    SzIoResult tr;
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    sz_testrt_install();
    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *cmd = sz_string_from_cstr("true");
      tr = sz_io_unsafe_run(sz_sys_exec(cmd));
      sz_release(cmd);
      assert(!tr.ok);
      assert(tr.error &&
             strstr(sz_string_cstr(tr.error->message), "TestRuntime") != NULL);
      sz_error_free(tr.error);
      cmd = sz_string_from_cstr("true");
      tr = sz_io_unsafe_run(sz_sys_spawn(cmd));
      sz_release(cmd);
      assert(!tr.ok);
      assert(tr.error &&
             strstr(sz_string_cstr(tr.error->message), "TestRuntime") != NULL);
      sz_error_free(tr.error);
      {
        SzString *msg = sz_string_from_cstr("x");
        tr = sz_io_unsafe_run(sz_sys_child_write(1, msg));
        sz_release(msg);
        assert(!tr.ok);
        assert(tr.error &&
               strstr(sz_string_cstr(tr.error->message), "TestRuntime") != NULL);
        sz_error_free(tr.error);
        tr = sz_io_unsafe_run(sz_sys_child_read(1, 1));
        assert(!tr.ok);
        assert(tr.error &&
               strstr(sz_string_cstr(tr.error->message), "TestRuntime") != NULL);
        sz_error_free(tr.error);
        tr = sz_io_unsafe_run(sz_sys_child_close(1));
        assert(!tr.ok);
        assert(tr.error &&
               strstr(sz_string_cstr(tr.error->message), "TestRuntime") != NULL);
        sz_error_free(tr.error);
      }
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
    sz_testrt_reset();
  }

  /* Sys.exec / Sys.spawn built before TestRuntime still reject at run. */
  {
    SzIoResult tr;
    SzString *cmd = sz_string_from_cstr("true");
    SzIo *exec_io = sz_sys_exec(cmd);
    SzIo *spawn_io = sz_sys_spawn(cmd);
    sz_release(cmd);
    sz_testrt_install();
    tr = sz_io_unsafe_run(exec_io);
    assert(!tr.ok);
    assert(tr.error &&
           strstr(sz_string_cstr(tr.error->message), "TestRuntime") != NULL);
    sz_error_free(tr.error);
    tr = sz_io_unsafe_run(spawn_io);
    assert(!tr.ok);
    assert(tr.error &&
           strstr(sz_string_cstr(tr.error->message), "TestRuntime") != NULL);
    sz_error_free(tr.error);
    sz_testrt_reset();
  }

  /* Sys.exec parks; a peer fiber runs before the child exits. */
  {
    SzPair *pair;
    g_peer_flag = 0;
    r = sz_io_unsafe_run(both_drop(
        fm_drop(sz_sys_exec(sz_string_from_cstr("sleep 0.08")),
                      exec_then_flag, NULL),
        fm_drop(sz_io_println_cstr("peer"), assert_peer_quiet, NULL)));
    assert(r.ok);
    pair = (SzPair *)r.value;
    assert(pair && pair->left);
    {
      SzPair *got = (SzPair *)pair->left;
      assert(sz_unbox_i64(got->left) == 0);
    }
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
    r = sz_io_unsafe_run(race_drop(sz_net_serve(port, serve_path_ok, NULL),
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
    r = sz_io_unsafe_run(race_drop(sz_net_serve(port, serve_path_ok, NULL),
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
    r = sz_io_unsafe_run(race_drop(sz_net_serve(port, serve_padded_ok, NULL),
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
    r = sz_io_unsafe_run(race_drop(sz_net_serve(port, serve_fail_then_ok, NULL),
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
    r = sz_io_unsafe_run(both_drop(
        sz_io_poll_readable(fds[0]),
        fm_drop(sz_io_println_cstr("peer"), assert_peer_quiet, NULL)));
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
    r = sz_io_unsafe_run(both_drop(
        sz_net_serve_once(port, serve_path_ok, NULL),
        fm_drop(sz_io_println_cstr("peer"), assert_peer_quiet, NULL)));
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
    r = sz_io_unsafe_run(both_drop(
        sz_net_serve_once(port, serve_path_ok, NULL),
        fm_drop(sz_io_sleep_ms(30), after_sleep_http,
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
    r = sz_io_unsafe_run(both_drop(
        sz_net_serve_once(port, serve_path_ok, NULL),
        fm_drop(sz_io_sleep_ms(30), after_sleep_http,
                      sz_string_from_cstr(url))));
    assert(r.ok);
    pair = (SzPair *)r.value;
    assert(pair && pair->right);
    assert(strcmp(sz_string_cstr((SzString *)pair->right), "ok:/x") == 0);
  }

  /* Live httpGet Host includes a non-default port (RFC 9110). */
  {
    pthread_t th;
    int port = 18620;
    char url[64];
    char want[64];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/x", port);
    snprintf(want, sizeof want, "Host: 127.0.0.1:%d", port);
    pthread_create(&th, NULL, capture_http_req4, &port);
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    pthread_join(th, NULL);
    assert(r.ok);
    sz_release(r.value);
    assert(strstr(g_http_captured, want) != NULL);
    assert(strstr(g_http_captured, "Host: 127.0.0.1\r\n") == NULL);
  }

  /* Live httpGet Host for IPv6 includes the non-default port in brackets. */
  {
    pthread_t th;
    int port = 18621;
    char url[64];
    char want[64];
    snprintf(url, sizeof url, "http://[::1]:%d/x", port);
    snprintf(want, sizeof want, "Host: [::1]:%d", port);
    pthread_create(&th, NULL, capture_http_req6, &port);
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    pthread_join(th, NULL);
    assert(r.ok);
    sz_release(r.value);
    assert(strstr(g_http_captured, want) != NULL);
  }

  /* Port 80 Host is host-only. Bind 80 is not required. */
  {
    char host[64];
    sz_net_test_http_host_header("127.0.0.1", 80, host, sizeof host);
    assert(strcmp(host, "127.0.0.1") == 0);
    sz_net_test_http_host_header("127.0.0.1", 18620, host, sizeof host);
    assert(strcmp(host, "127.0.0.1:18620") == 0);
    sz_net_test_http_host_header("::1", 80, host, sizeof host);
    assert(strcmp(host, "[::1]") == 0);
    sz_net_test_http_host_header("::1", 18621, host, sizeof host);
    assert(strcmp(host, "[::1]:18621") == 0);
  }

  /* Live Net.serve: a wide port that truncates to 18622 must not bind 18622. */
  {
    int port = 18622;
    r = sz_io_unsafe_run(
        sz_net_serve((int64_t)((1ULL << 32) | (unsigned)port), serve_path_ok,
                     NULL));
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "port must be 1..65535") !=
               NULL);
    sz_error_free(r.error);
    assert(try_bind_v4(port) == 0);
    r = sz_io_unsafe_run(sz_net_serve(-4294967216LL, serve_path_ok, NULL));
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "port must be 1..65535") !=
               NULL);
    sz_error_free(r.error);
  }

  /* v4 listen occupied: persistent serve stays on ::1 for two GETs. */
  {
    int port = 18603;
    int v4hold = -1;
    int v6probe = -1;
    int one = 1;
    struct sockaddr_in a4;
    struct sockaddr_in6 a6;
    pthread_t th;
    void *ret = NULL;
    v6probe = socket(AF_INET6, SOCK_STREAM, 0);
    if (v6probe >= 0) {
      setsockopt(v6probe, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef IPV6_V6ONLY
      setsockopt(v6probe, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
#endif
      memset(&a6, 0, sizeof a6);
      a6.sin6_family = AF_INET6;
      a6.sin6_port = htons((uint16_t)port);
      if (inet_pton(AF_INET6, "::1", &a6.sin6_addr) == 1 &&
          bind(v6probe, (struct sockaddr *)&a6, sizeof a6) == 0)
        listen(v6probe, 1);
      else {
        close(v6probe);
        v6probe = -1;
      }
    }
    if (v6probe >= 0) {
      close(v6probe);
      v4hold = socket(AF_INET, SOCK_STREAM, 0);
      if (v4hold >= 0) {
        setsockopt(v4hold, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        memset(&a4, 0, sizeof a4);
        a4.sin_family = AF_INET;
        a4.sin_port = htons((uint16_t)port);
        a4.sin_addr.s_addr = inet_addr("127.0.0.1");
        if (bind(v4hold, (struct sockaddr *)&a4, sizeof a4) != 0 ||
            listen(v4hold, 1) != 0) {
          close(v4hold);
          v4hold = -1;
        }
      }
    }
    if (v4hold >= 0) {
      pthread_create(&th, NULL, two_live_gets_v6, &port);
      r = sz_io_unsafe_run(race_drop(sz_net_serve(port, serve_path_ok, NULL),
                                     sz_io_sleep_ms(400)));
      pthread_join(th, &ret);
      close(v4hold);
      assert(r.ok);
      assert(ret && strstr((char *)ret, "ok:/x") != NULL);
    }
  }

  /* IPv6 literals skip DNS: http://[::1]:port/x */
  {
    pthread_t th;
    int port = 18579;
    char url[64];
    void *ret = NULL;
    pthread_create(&th, NULL, ipv6_http_once, &port);
    snprintf(url, sizeof url, "http://[::1]:%d/x", port);
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
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
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
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
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
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
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    pthread_join(th_dns, NULL);
    pthread_join(th_http, &http_ret);
    close(dns_fd);
    sz_net_test_set_nameserver(NULL, 0);
    assert(r.ok);
    assert(http_ret != NULL);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "ok:/x") == 0);
  }

  /* AAAA NXDOMAIN first, then A 127.0.0.1; GET uses the A record. */
  {
    pthread_t th_dns;
    pthread_t th_http;
    int dns_fd;
    int http_port = 18604;
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
    pthread_create(&th_dns, NULL, dns_aaaa_nxdomain_then_a, &dns_fd);
    snprintf(url, sizeof url, "http://scuzz.test:%d/x", http_port);
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
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

  /* Cancelled GET drops the URL and sockets. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    {
      SzString *url = sz_string_from_cstr("http://192.0.2.1:9/x");
      r = sz_io_unsafe_run(
          race_drop(sz_net_http_get(url), sz_io_sleep_ms(20)));
      sz_release(url);
      assert(r.ok);
    }
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Cancelled serve drops handler env; the port can accept again. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    int port = 18602;
    pthread_t th;
    void *ret = NULL;
    SzString *henv;
    sz_alloc_stats(&base_bytes, &base_count);
    henv = sz_string_from_cstr("henv");
    r = sz_io_unsafe_run(
        race_drop(sz_net_serve(port, serve_path_ok, henv), sz_io_sleep_ms(20)));
    sz_release(henv);
    assert(r.ok);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
    pthread_create(&th, NULL, live_get_client, &port);
    r = sz_io_unsafe_run(sz_net_serve_once(port, serve_path_ok, NULL));
    pthread_join(th, &ret);
    assert(r.ok);
    assert(ret && strstr((char *)ret, "ok:/x") != NULL);
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
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
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
    r = sz_io_unsafe_run(both_drop(
        sz_net_serve_once(http_port, serve_path_ok, NULL),
        fm_drop(sz_io_sleep_ms(30), after_sleep_dns_http,
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
    r = sz_io_unsafe_run(both_drop(
        sz_net_serve_once(http_port, serve_path_ok, NULL),
        fm_drop(sz_io_sleep_ms(30), after_sleep_dns_http,
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

  /* Glue A/AAAA in ADDITIONAL is not an answer. */
  {
    pthread_t th;
    int dns_fd;
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
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
    pthread_create(&th, NULL, dns_glue_only, &dns_fd);
    r = sz_io_unsafe_run(
        sz_net_http_get(sz_string_from_cstr("http://scuzz.test/x")));
    pthread_join(th, NULL);
    close(dns_fd);
    sz_net_test_set_nameserver(NULL, 0);
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "DNS failed"));
    sz_error_free(r.error);
  }

  /* Answer from a different UDP source port is ignored. */
  {
    pthread_t th;
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
    pthread_create(&th, NULL, dns_wrong_src, &dns_fd);
    t0 = sz_clock_monotonic_ms_sync();
    r = sz_io_unsafe_run(
        sz_net_http_get(sz_string_from_cstr("http://silent.test/x")));
    t1 = sz_clock_monotonic_ms_sync();
    pthread_join(th, NULL);
    close(dns_fd);
    sz_net_test_set_nameserver(NULL, 0);
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "DNS timed out"));
    sz_error_free(r.error);
    assert(t1 - t0 >= 900);
    assert(t1 - t0 < 2500);
  }

  /* URL: CR/LF, userinfo, and port out of range fail in get_start. */
  {
    r = sz_io_unsafe_run(
        sz_net_http_get(sz_string_from_cstr("http://127.0.0.1/\r\nHost: x")));
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "invalid URL"));
    sz_error_free(r.error);
    r = sz_io_unsafe_run(
        sz_net_http_get(sz_string_from_cstr("http://user@host/")));
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "invalid URL"));
    sz_error_free(r.error);
    r = sz_io_unsafe_run(
        sz_net_http_get(sz_string_from_cstr("http://127.0.0.1:99999/")));
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "invalid port"));
    sz_error_free(r.error);
  }

  /* Content-Length complete without close: httpGet succeeds before EOF. */
  {
    pthread_t th;
    int port = 18594;
    char url[64];
    int64_t t0;
    int64_t t1;
    pthread_create(&th, NULL, ipv4_http_hold_complete, &port);
    snprintf(url, sizeof url, "http://127.0.0.1:%d/x", port);
    t0 = sz_clock_monotonic_ms_sync();
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    t1 = sz_clock_monotonic_ms_sync();
    pthread_join(th, NULL);
    assert(r.ok);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "ok:/x") == 0);
    sz_release(r.value);
    assert(t1 - t0 < 900);
  }

  /* Duplicate Content-Length is malformed. */
  {
    pthread_t th;
    int port = 18623;
    char url[64];
    HttpRespArg arg;
    arg.port = port;
    arg.resp = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nok:/x";
    snprintf(url, sizeof url, "http://127.0.0.1:%d/x", port);
    pthread_create(&th, NULL, ipv4_http_resp, &arg);
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    pthread_join(th, NULL);
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "malformed response") !=
               NULL);
    sz_error_free(r.error);
  }

  /* Transfer-Encoding with OWS around the name is still rejected. */
  {
    pthread_t th;
    int port = 18624;
    char url[64];
    HttpRespArg arg;
    arg.port = port;
    arg.resp = "HTTP/1.0 200 OK\r\nTransfer-Encoding : chunked\r\n\r\n"
               "5\r\nok:/x\r\n0\r\n\r\n";
    snprintf(url, sizeof url, "http://127.0.0.1:%d/x", port);
    pthread_create(&th, NULL, ipv4_http_resp, &arg);
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    pthread_join(th, NULL);
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "chunked") != NULL);
    sz_error_free(r.error);
  }

  /* Non-2xx fails. */
  {
    pthread_t th;
    int port = 18595;
    char url[64];
    pthread_create(&th, NULL, ipv4_http_404, &port);
    snprintf(url, sizeof url, "http://127.0.0.1:%d/x", port);
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    pthread_join(th, NULL);
    assert(!r.ok);
    assert(r.error && strstr(sz_string_cstr(r.error->message), "HTTP error"));
    sz_error_free(r.error);
  }

  /* Peer RST after accept: IO fails; process stays alive (no SIGPIPE). */
  {
    pthread_t th;
    int port = 18596;
    char url[64];
    pthread_create(&th, NULL, ipv4_http_rst, &port);
    snprintf(url, sizeof url, "http://127.0.0.1:%d/x", port);
    r = sz_io_unsafe_run(fm_drop(sz_io_sleep_ms(30), after_sleep_http,
                                      sz_string_from_cstr(url)));
    pthread_join(th, NULL);
    assert(!r.ok);
    assert(r.error &&
           (strstr(sz_string_cstr(r.error->message), "write failed") ||
            strstr(sz_string_cstr(r.error->message), "connect failed") ||
            strstr(sz_string_cstr(r.error->message), "read failed")));
    sz_error_free(r.error);
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
    r = sz_io_unsafe_run(both_drop(
        sz_sys_read_line(),
        fm_drop(sz_io_println_cstr("peer"), assert_peer_quiet, NULL)));
    pthread_join(th, NULL);
    assert(dup2(saved, STDIN_FILENO) == 0);
    close(saved);
    close(wr);
    assert(r.ok);
    pair = (SzPair *)r.value;
    assert(pair && pair->left);
    assert(strcmp(sz_string_cstr((SzString *)pair->left), "hello") == 0);
    sz_pair_free(pair);
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

  /* List spine free: cons cells go away; heads stay with the caller. */
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

  /* List.filter: keep order, share heads, drop unmatched. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("a");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *ys;
    ys = sz_list_filter(xs, keep_not_b, NULL);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == a);
    assert(ys->tail && ys->tail->head == c);
    assert(sz_list_len(xs) == 3);
    sz_list_free(ys);
    ys = sz_list_filter(xs, keep_none, NULL);
    assert(sz_list_is_empty(ys));
    sz_list_free(ys);
    ys = sz_list_filter(NULL, keep_not_b, NULL);
    assert(sz_list_is_empty(ys));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.map: new spine, mapped heads, empty stays empty. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_nil()));
    SzList *ys;
    SzList *id;
    ys = sz_list_map(xs, map_bang, NULL);
    assert(sz_list_len(ys) == 2);
    assert(strcmp(sz_string_cstr((SzString *)ys->head), "a!") == 0);
    assert(ys->tail && strcmp(sz_string_cstr((SzString *)ys->tail->head), "b!") == 0);
    assert(ys->head != a);
    id = sz_list_map(xs, map_id, NULL);
    assert(id->head == a);
    assert(id->tail && id->tail->head == b);
    sz_list_free(ys);
    sz_list_free(id);
    ys = sz_list_map(NULL, map_bang, NULL);
    assert(sz_list_is_empty(ys));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
  }

  /* List.flatMap: mapper lists concatenate. Empty mapper cells drop. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_nil()));
    SzList *ys;
    ys = sz_list_flat_map(xs, map_dup_list, NULL);
    assert(sz_list_len(ys) == 4);
    assert(ys->head == a);
    assert(ys->tail && ys->tail->head == a);
    assert(ys->tail->tail && ys->tail->tail->head == b);
    sz_list_free(ys);
    ys = sz_list_flat_map(xs, map_empty_list, NULL);
    assert(sz_list_is_empty(ys));
    sz_list_free(ys);
    ys = sz_list_flat_map(NULL, map_dup_list, NULL);
    assert(sz_list_is_empty(ys));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
  }

  /* List.padTo: n <= len shares; n > len appends fill. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *z = sz_string_from_cstr("z");
    SzList *xs = sz_list_cons(a, sz_list_nil());
    SzList *ys;
    ys = sz_list_pad_to(xs, 3, z);
    assert(sz_list_len(ys) == 3);
    assert(ys->head == a);
    assert(ys->tail && ys->tail->head == z);
    assert(ys->tail->tail && ys->tail->tail->head == z);
    sz_list_free(ys);
    ys = sz_list_pad_to(xs, 1, z);
    assert(sz_list_len(ys) == 1);
    assert(ys->head == a);
    sz_list_free(ys);
    ys = sz_list_pad_to(xs, 0, z);
    assert(sz_list_len(ys) == 1);
    sz_list_free(ys);
    ys = sz_list_pad_to(NULL, 2, z);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == z);
    sz_list_free(ys);
    assert(sz_list_non_empty(xs) == 1);
    assert(sz_list_non_empty(NULL) == 0);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(z);
  }

  /* List.range: [from, until). Empty when until <= from. */
  {
    SzList *xs;
    xs = sz_list_range(1, 4);
    assert(sz_list_len(xs) == 3);
    assert(sz_unbox_i64(sz_list_head(xs)) == 1);
    assert(sz_unbox_i64(sz_list_at(xs, 2)) == 3);
    sz_list_free(xs);
    xs = sz_list_range(5, 5);
    assert(sz_list_is_empty(xs));
    xs = sz_list_range(3, 1);
    assert(sz_list_is_empty(xs));
    xs = sz_list_range(-1, 2);
    assert(sz_list_len(xs) == 3);
    assert(sz_unbox_i64(sz_list_head(xs)) == -1);
    sz_list_free(xs);
  }

  /* List.tabulate: f(0)..f(n-1). n <= 0 is empty. */
  {
    SzList *ys;
    ys = sz_list_tabulate(3, map_int_to_str, NULL);
    assert(sz_list_len(ys) == 3);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(ys)), "0") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_at(ys, 2)), "2") == 0);
    sz_list_free(ys);
    ys = sz_list_tabulate(0, map_int_to_str, NULL);
    assert(sz_list_is_empty(ys));
    ys = sz_list_tabulate(-2, map_int_to_str, NULL);
    assert(sz_list_is_empty(ys));
  }

  /* List.intersperse: insert x between cells. Empty/one share. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzString *sep = sz_string_from_cstr("|");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *ys;
    ys = sz_list_intersperse(xs, sep);
    assert(sz_list_len(ys) == 5);
    assert(ys->head == a);
    assert(ys->tail && ys->tail->head == sep);
    assert(ys->tail->tail && ys->tail->tail->head == b);
    sz_list_free(ys);
    {
      SzList *one = sz_list_cons(a, sz_list_nil());
      ys = sz_list_intersperse(one, sep);
      assert(sz_list_len(ys) == 1);
      assert(ys->head == a);
      sz_list_free(ys);
      sz_list_free(one);
    }
    ys = sz_list_intersperse(NULL, sep);
    assert(sz_list_is_empty(ys));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
    sz_string_free(sep);
  }

  /* List.grouped: chunks of n. Last chunk may be short. n <= 0 is empty. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *gs;
    SzList *g0;
    SzList *g1;
    gs = sz_list_grouped(xs, 2);
    assert(sz_list_len(gs) == 2);
    g0 = (SzList *)sz_list_head(gs);
    assert(sz_list_len(g0) == 2);
    assert(g0->head == a);
    assert(g0->tail && g0->tail->head == b);
    g1 = (SzList *)sz_list_at(gs, 1);
    assert(sz_list_len(g1) == 1);
    assert(g1->head == c);
    sz_list_free(gs);
    gs = sz_list_grouped(xs, 0);
    assert(sz_list_is_empty(gs));
    gs = sz_list_grouped(xs, -1);
    assert(sz_list_is_empty(gs));
    gs = sz_list_grouped(NULL, 2);
    assert(sz_list_is_empty(gs));
    gs = sz_list_grouped(xs, 5);
    assert(sz_list_len(gs) == 1);
    assert(sz_list_len((SzList *)sz_list_head(gs)) == 3);
    sz_list_free(gs);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.sliding: windows of n. n > len or n <= 0 is empty. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *ws;
    SzList *w0;
    SzList *w1;
    ws = sz_list_sliding(xs, 2);
    assert(sz_list_len(ws) == 2);
    w0 = (SzList *)sz_list_head(ws);
    assert(sz_list_len(w0) == 2);
    assert(w0->head == a);
    assert(w0->tail && w0->tail->head == b);
    w1 = (SzList *)sz_list_at(ws, 1);
    assert(sz_list_len(w1) == 2);
    assert(w1->head == b);
    assert(w1->tail && w1->tail->head == c);
    sz_list_free(ws);
    ws = sz_list_sliding(xs, 3);
    assert(sz_list_len(ws) == 1);
    assert(sz_list_len((SzList *)sz_list_head(ws)) == 3);
    sz_list_free(ws);
    ws = sz_list_sliding(xs, 4);
    assert(sz_list_is_empty(ws));
    ws = sz_list_sliding(xs, 0);
    assert(sz_list_is_empty(ws));
    ws = sz_list_sliding(NULL, 2);
    assert(sz_list_is_empty(ws));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.slice: [from, until). Negative is 0. until <= from is empty. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *ys;
    ys = sz_list_slice(xs, 1, 3);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == b);
    assert(ys->tail && ys->tail->head == c);
    sz_list_free(ys);
    ys = sz_list_slice(xs, 0, 3);
    assert(sz_list_len(ys) == 3);
    sz_list_free(ys);
    ys = sz_list_slice(xs, 2, 2);
    assert(sz_list_is_empty(ys));
    ys = sz_list_slice(xs, -1, 1);
    assert(sz_list_len(ys) == 1);
    assert(ys->head == a);
    sz_list_free(ys);
    ys = sz_list_slice(xs, 1, 9);
    assert(sz_list_len(ys) == 2);
    sz_list_free(ys);
    ys = sz_list_slice(NULL, 0, 2);
    assert(sz_list_is_empty(ys));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.indices: boxed 0 .. len-1. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_nil()));
    SzList *ix = sz_list_indices(xs);
    SzList *empty;
    assert(sz_list_len(ix) == 2);
    assert(sz_unbox_i64(ix->head) == 0);
    assert(ix->tail && sz_unbox_i64(ix->tail->head) == 1);
    sz_list_free(ix);
    empty = sz_list_indices(NULL);
    assert(sz_list_is_empty(empty));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
  }

  /* List.indexWhere / lastIndexWhere: first/last match or -1. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    assert(sz_list_index_where(xs, keep_not_b, NULL) == 0);
    assert(sz_list_last_index_where(xs, keep_not_b, NULL) == 2);
    assert(sz_list_index_where(NULL, keep_not_b, NULL) == -1);
    assert(sz_list_last_index_where(NULL, keep_not_b, NULL) == -1);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.append retains the element. The caller drops their ref. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *a;
    SzString *b;
    SzList *xs;
    SzList *ys;
    sz_alloc_stats(&base_bytes, &base_count);
    a = sz_string_from_cstr("milk");
    b = sz_string_from_cstr("f");
    xs = sz_list_cons(a, sz_list_nil());
    ys = sz_list_append(xs, b);
    sz_release(xs);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == a);
    assert(ys->tail && ys->tail->head == b);
    sz_list_free(ys);
    sz_string_free(a);
    sz_string_free(b);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List.setAt: replace in bounds; past-end and negative keep the list. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_nil()));
    SzList *ys;
    ys = sz_list_set_at(xs, 0, c);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == c);
    assert(ys->tail && ys->tail->head == b);
    assert(xs->head == a);
    sz_list_free(ys);
    ys = sz_list_set_at(xs, 1, c);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == a);
    assert(ys->tail && ys->tail->head == c);
    sz_list_free(ys);
    ys = sz_list_set_at(xs, 2, c);
    assert(ys == xs);
    sz_release(ys);
    ys = sz_list_set_at(xs, -1, c);
    assert(ys == xs);
    sz_release(ys);
    ys = sz_list_set_at(NULL, 0, c);
    assert(sz_list_is_empty(ys));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.take: prefix spine; n <= 0 is empty. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *ys;
    ys = sz_list_take(xs, 2);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == a);
    assert(ys->tail && ys->tail->head == b);
    assert(sz_list_len(xs) == 3);
    sz_list_free(ys);
    ys = sz_list_take(xs, 0);
    assert(sz_list_is_empty(ys));
    ys = sz_list_take(NULL, 2);
    assert(sz_list_is_empty(ys));
    ys = sz_list_take(xs, 9);
    assert(sz_list_len(ys) == 3);
    assert(ys->head == a);
    sz_list_free(ys);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.drop: shared suffix; n <= 0 retains xs. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *ys;
    ys = sz_list_drop(xs, 1);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == b);
    assert(ys->tail && ys->tail->head == c);
    sz_list_free(ys);
    ys = sz_list_drop(xs, 0);
    assert(ys == xs);
    sz_release(ys);
    ys = sz_list_drop(xs, 9);
    assert(sz_list_is_empty(ys));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.takeRight / dropRight: suffix / prefix; n <= 0 matches take / drop. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *ys;
    ys = sz_list_take_right(xs, 2);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == b);
    assert(ys->tail && ys->tail->head == c);
    sz_list_free(ys);
    ys = sz_list_take_right(xs, 0);
    assert(sz_list_is_empty(ys));
    ys = sz_list_take_right(NULL, 2);
    assert(sz_list_is_empty(ys));
    ys = sz_list_take_right(xs, 9);
    assert(ys == xs);
    sz_release(ys);
    ys = sz_list_drop_right(xs, 1);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == a);
    assert(ys->tail && ys->tail->head == b);
    sz_list_free(ys);
    ys = sz_list_drop_right(xs, 0);
    assert(ys == xs);
    sz_release(ys);
    ys = sz_list_drop_right(xs, 9);
    assert(sz_list_is_empty(ys));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.init / last: drop last; last as one cell. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *ys;
    ys = sz_list_init(xs);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == a);
    assert(ys->tail && ys->tail->head == b);
    sz_list_free(ys);
    {
      SzList *one = sz_list_cons(a, sz_list_nil());
      ys = sz_list_init(one);
      assert(sz_list_is_empty(ys));
      sz_list_free(one);
    }
    ys = sz_list_init(NULL);
    assert(sz_list_is_empty(ys));
    ys = sz_list_last(xs);
    assert(sz_list_len(ys) == 1);
    assert(ys->head == c);
    sz_list_free(ys);
    ys = sz_list_last(NULL);
    assert(sz_list_is_empty(ys));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.getOrElse: index or default. List.fill: n copies. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *miss = sz_string_from_cstr("z");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_nil()));
    SzList *ys;
    assert(sz_list_get_or(xs, 0, miss) == a);
    assert(sz_list_get_or(xs, 1, miss) == b);
    assert(sz_list_get_or(xs, 2, miss) == miss);
    assert(sz_list_get_or(xs, -1, miss) == miss);
    assert(sz_list_get_or(NULL, 0, miss) == miss);
    ys = sz_list_fill(3, a);
    assert(sz_list_len(ys) == 3);
    assert(ys->head == a);
    assert(ys->tail && ys->tail->head == a);
    sz_list_free(ys);
    ys = sz_list_fill(0, a);
    assert(sz_list_is_empty(ys));
    ys = sz_list_fill(-1, a);
    assert(sz_list_is_empty(ys));
    ys = sz_list_fill(1, b);
    assert(sz_list_len(ys) == 1);
    assert(ys->head == b);
    sz_list_free(ys);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(miss);
  }
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *ys;
    ys = sz_list_find(xs, keep_not_b, NULL);
    assert(sz_list_len(ys) == 1);
    assert(ys->head == a);
    sz_list_free(ys);
    ys = sz_list_find(xs, keep_none, NULL);
    assert(sz_list_is_empty(ys));
    assert(sz_list_exists(xs, keep_not_b, NULL) == 1);
    assert(sz_list_exists(xs, keep_none, NULL) == 0);
    assert(sz_list_exists(NULL, keep_not_b, NULL) == 0);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.takeWhile / dropWhile / forall: prefix, suffix, all-match. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *ys;
    ys = sz_list_takewhile(xs, keep_not_b, NULL);
    assert(sz_list_len(ys) == 1);
    assert(ys->head == a);
    sz_list_free(ys);
    ys = sz_list_takewhile(xs, keep_none, NULL);
    assert(sz_list_is_empty(ys));
    ys = sz_list_takewhile(NULL, keep_not_b, NULL);
    assert(sz_list_is_empty(ys));
    ys = sz_list_dropwhile(xs, keep_not_b, NULL);
    assert(sz_list_len(ys) == 2);
    assert(ys->head == b);
    assert(ys->tail && ys->tail->head == c);
    sz_list_free(ys);
    ys = sz_list_dropwhile(xs, keep_none, NULL);
    assert(ys == xs);
    sz_release(ys);
    ys = sz_list_dropwhile(NULL, keep_not_b, NULL);
    assert(sz_list_is_empty(ys));
    assert(sz_list_forall(xs, keep_not_b, NULL) == 0);
    assert(sz_list_forall(xs, keep_none, NULL) == 0);
    assert(sz_list_forall(NULL, keep_not_b, NULL) == 1);
    {
      SzList *as = sz_list_cons(a, NULL);
      assert(sz_list_forall(as, keep_not_b, NULL) == 1);
      sz_list_free(as);
    }
    assert(sz_list_count(xs, keep_not_b, NULL) == 2);
    assert(sz_list_count(xs, keep_none, NULL) == 0);
    assert(sz_list_count(NULL, keep_not_b, NULL) == 0);
    ys = sz_list_filter_not(xs, keep_not_b, NULL);
    assert(sz_list_len(ys) == 1);
    assert(ys->head == b);
    sz_list_free(ys);
    ys = sz_list_filter_not(xs, keep_none, NULL);
    assert(sz_list_len(ys) == 3);
    sz_list_free(ys);
    ys = sz_list_filter_not(NULL, keep_not_b, NULL);
    assert(sz_list_is_empty(ys));
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.splitAt / span / partition: two lists packed as List[List]. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_cons(b, sz_list_cons(c, sz_list_nil())));
    SzList *pair;
    SzList *left;
    SzList *right;
    pair = sz_list_split_at(xs, 1);
    assert(sz_list_len(pair) == 2);
    left = (SzList *)sz_list_head(pair);
    right = (SzList *)sz_list_head(sz_list_tail(pair));
    assert(sz_list_len(left) == 1);
    assert(left->head == a);
    assert(sz_list_len(right) == 2);
    assert(right->head == b);
    assert(right->tail && right->tail->head == c);
    sz_list_free(pair);
    pair = sz_list_split_at(xs, 0);
    left = (SzList *)sz_list_head(pair);
    right = (SzList *)sz_list_head(sz_list_tail(pair));
    assert(sz_list_is_empty(left));
    assert(right == xs);
    sz_list_free(pair);
    pair = sz_list_span(xs, keep_not_b, NULL);
    left = (SzList *)sz_list_head(pair);
    right = (SzList *)sz_list_head(sz_list_tail(pair));
    assert(sz_list_len(left) == 1);
    assert(left->head == a);
    assert(sz_list_len(right) == 2);
    assert(right->head == b);
    sz_list_free(pair);
    pair = sz_list_partition(xs, keep_not_b, NULL);
    left = (SzList *)sz_list_head(pair);
    right = (SzList *)sz_list_head(sz_list_tail(pair));
    assert(sz_list_len(left) == 2);
    assert(left->head == a);
    assert(left->tail && left->tail->head == c);
    assert(sz_list_len(right) == 1);
    assert(right->head == b);
    sz_list_free(pair);
    pair = sz_list_split_at(NULL, 1);
    assert(sz_list_len(pair) == 2);
    assert(sz_list_is_empty((SzList *)sz_list_head(pair)));
    assert(sz_list_is_empty((SzList *)sz_list_head(sz_list_tail(pair))));
    sz_list_free(pair);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* List.inits / tails: prefixes and suffixes including empty. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *a;
    SzString *b;
    SzString *c;
    SzList *xs;
    SzList *tmp;
    SzList *gs;
    SzList *g0;
    SzList *g1;
    SzList *glast;
    sz_alloc_stats(&base_bytes, &base_count);
    a = sz_string_from_cstr("a");
    b = sz_string_from_cstr("b");
    c = sz_string_from_cstr("c");
    xs = sz_list_cons(a, NULL);
    tmp = sz_list_append(xs, b);
    sz_release(xs);
    xs = sz_list_append(tmp, c);
    sz_release(tmp);
    gs = sz_list_inits(xs);
    assert(sz_list_len(gs) == 4);
    g0 = (SzList *)sz_list_head(gs);
    assert(sz_list_is_empty(g0));
    g1 = (SzList *)sz_list_at(gs, 1);
    assert(sz_list_len(g1) == 1);
    assert(g1->head == a);
    glast = (SzList *)sz_list_at(gs, 3);
    assert(sz_list_len(glast) == 3);
    assert(glast->head == a);
    sz_list_free(gs);
    gs = sz_list_tails(xs);
    assert(sz_list_len(gs) == 4);
    g0 = (SzList *)sz_list_head(gs);
    assert(sz_list_len(g0) == 3);
    assert(g0->head == a);
    glast = (SzList *)sz_list_at(gs, 3);
    assert(sz_list_is_empty(glast));
    g1 = (SzList *)sz_list_at(gs, 1);
    assert(sz_list_len(g1) == 2);
    assert(g1->head == b);
    sz_list_free(gs);
    gs = sz_list_inits(NULL);
    assert(sz_list_len(gs) == 1);
    assert(sz_list_is_empty((SzList *)sz_list_head(gs)));
    sz_list_free(gs);
    gs = sz_list_tails(NULL);
    assert(sz_list_len(gs) == 1);
    assert(sz_list_is_empty((SzList *)sz_list_head(gs)));
    sz_list_free(gs);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List.zip / zipAll / unzip / transpose: zip cells as (A, B) pairs. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *a;
    SzString *b;
    SzString *c;
    SzString *one;
    SzString *two;
    SzString *nine;
    SzString *zee;
    SzList *xs;
    SzList *ys;
    SzList *tmp;
    SzList *zs;
    SzPair *pair;
    SzPair *uz;
    SzList *g0;
    SzList *g1;
    SzList *rows;
    SzList *row0;
    SzList *row1;
    sz_alloc_stats(&base_bytes, &base_count);
    a = sz_string_from_cstr("a");
    b = sz_string_from_cstr("b");
    c = sz_string_from_cstr("c");
    one = sz_string_from_cstr("1");
    two = sz_string_from_cstr("2");
    nine = sz_string_from_cstr("9");
    zee = sz_string_from_cstr("z");
    xs = sz_list_cons(a, NULL);
    tmp = sz_list_append(xs, b);
    sz_release(xs);
    xs = sz_list_append(tmp, c);
    sz_release(tmp);
    ys = sz_list_cons(one, NULL);
    tmp = sz_list_append(ys, two);
    sz_release(ys);
    ys = tmp;
    zs = sz_list_zip(xs, ys);
    assert(sz_list_len(zs) == 2);
    pair = (SzPair *)sz_list_head(zs);
    assert(pair->left == a);
    assert(pair->right == one);
    pair = (SzPair *)sz_list_at(zs, 1);
    assert(pair->left == b);
    assert(pair->right == two);
    sz_list_free(zs);
    zs = sz_list_zip(xs, NULL);
    assert(sz_list_is_empty(zs));
    zs = sz_list_zip(NULL, ys);
    assert(sz_list_is_empty(zs));
    zs = sz_list_zip(NULL, NULL);
    assert(sz_list_is_empty(zs));
    zs = sz_list_interleave(xs, ys);
    assert(sz_list_len(zs) == 5);
    assert(sz_list_head(zs) == a);
    assert(sz_list_at(zs, 1) == one);
    assert(sz_list_at(zs, 2) == b);
    assert(sz_list_at(zs, 3) == two);
    assert(sz_list_at(zs, 4) == c);
    sz_list_free(zs);
    zs = sz_list_interleave(NULL, ys);
    assert(sz_list_len(zs) == 2);
    assert(sz_list_head(zs) == one);
    sz_list_free(zs);
    zs = sz_list_interleave(xs, NULL);
    assert(sz_list_len(zs) == 3);
    assert(sz_list_head(zs) == a);
    sz_list_free(zs);
    zs = sz_list_interleave(NULL, NULL);
    assert(sz_list_is_empty(zs));
    zs = sz_list_zip_all(xs, ys, zee, nine);
    assert(sz_list_len(zs) == 3);
    pair = (SzPair *)sz_list_at(zs, 2);
    assert(pair->left == c);
    assert(pair->right == nine);
    sz_list_free(zs);
    zs = sz_list_zip_all(ys, xs, zee, nine);
    assert(sz_list_len(zs) == 3);
    pair = (SzPair *)sz_list_at(zs, 2);
    assert(pair->left == zee);
    assert(pair->right == c);
    sz_list_free(zs);
    zs = sz_list_zip_all(NULL, NULL, zee, nine);
    assert(sz_list_is_empty(zs));
    zs = sz_list_zip(xs, ys);
    uz = sz_list_unzip(zs);
    sz_list_free(zs);
    g0 = (SzList *)uz->left;
    g1 = (SzList *)uz->right;
    assert(sz_list_len(g0) == 2);
    assert(g0->head == a);
    assert(sz_list_at(g0, 1) == b);
    assert(sz_list_len(g1) == 2);
    assert(g1->head == one);
    assert(sz_list_at(g1, 1) == two);
    sz_pair_free(uz);
    uz = sz_list_unzip(NULL);
    assert(sz_list_is_empty((SzList *)uz->left));
    assert(sz_list_is_empty((SzList *)uz->right));
    sz_pair_free(uz);
    zs = sz_list_cons(NULL, NULL);
    uz = sz_list_unzip(zs);
    sz_list_free(zs);
    assert(sz_list_is_empty((SzList *)uz->left));
    assert(sz_list_is_empty((SzList *)uz->right));
    sz_pair_free(uz);
    row0 = sz_list_cons(a, NULL);
    tmp = sz_list_append(row0, b);
    sz_release(row0);
    row0 = sz_list_append(tmp, c);
    sz_release(tmp);
    row1 = sz_list_cons(one, NULL);
    tmp = sz_list_append(row1, two);
    sz_release(row1);
    row1 = tmp;
    rows = sz_list_cons(row0, NULL);
    tmp = sz_list_append(rows, row1);
    sz_release(rows);
    sz_release(row0);
    sz_release(row1);
    rows = tmp;
    zs = sz_list_transpose(rows);
    assert(sz_list_len(zs) == 2);
    g0 = (SzList *)sz_list_head(zs);
    g1 = (SzList *)sz_list_at(zs, 1);
    assert(sz_list_len(g0) == 2);
    assert(g0->head == a);
    assert(sz_list_at(g0, 1) == one);
    assert(sz_list_len(g1) == 2);
    assert(g1->head == b);
    assert(sz_list_at(g1, 1) == two);
    sz_list_free(zs);
    zs = sz_list_transpose(NULL);
    assert(sz_list_is_empty(zs));
    tmp = sz_list_cons(NULL, NULL);
    zs = sz_list_transpose(tmp);
    sz_list_free(tmp);
    assert(sz_list_is_empty(zs));
    sz_list_free(rows);
    sz_list_free(xs);
    sz_list_free(ys);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
    sz_string_free(one);
    sz_string_free(two);
    sz_string_free(nine);
    sz_string_free(zee);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List.zipWithIndex / foldLeft / foldRight. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *a;
    SzString *b;
    SzString *bang;
    SzList *xs;
    SzList *tmp;
    SzList *zix;
    SzPair *pair;
    void *n1;
    void *n2;
    void *n3;
    void *z0;
    void *sum;
    SzList *ns;
    SzString *folded;
    sz_alloc_stats(&base_bytes, &base_count);
    a = sz_string_from_cstr("a");
    b = sz_string_from_cstr("b");
    bang = sz_string_from_cstr("!");
    xs = sz_list_cons(a, NULL);
    tmp = sz_list_append(xs, b);
    sz_release(xs);
    xs = tmp;
    zix = sz_list_zip_with_index(xs);
    assert(sz_list_len(zix) == 2);
    pair = (SzPair *)sz_list_head(zix);
    assert(sz_unbox_i64(pair->left) == 0);
    assert(pair->right == a);
    pair = (SzPair *)sz_list_at(zix, 1);
    assert(sz_unbox_i64(pair->left) == 1);
    assert(pair->right == b);
    sz_list_free(zix);
    zix = sz_list_zip_with_index(NULL);
    assert(sz_list_is_empty(zix));
    n1 = sz_box_i64(1);
    n2 = sz_box_i64(2);
    n3 = sz_box_i64(3);
    ns = sz_list_cons(n1, NULL);
    tmp = sz_list_append(ns, n2);
    sz_release(ns);
    ns = sz_list_append(tmp, n3);
    sz_release(tmp);
    z0 = sz_box_i64(0);
    sum = sz_list_fold_left(ns, z0, fold_add_i64, NULL);
    assert(sz_unbox_i64(sum) == 6);
    sz_release(sum);
    sum = sz_list_fold_left(NULL, z0, fold_add_i64, NULL);
    assert(sz_unbox_i64(sum) == 0);
    sz_release(sum);
    sum = sz_list_fold_right(ns, z0, fold_add_i64, NULL);
    assert(sz_unbox_i64(sum) == 6);
    sz_release(sum);
    folded = (SzString *)sz_list_fold_left(xs, bang, fold_concat_pair, NULL);
    assert(strcmp(sz_string_cstr(folded), "!ab") == 0);
    sz_release(folded);
    folded = (SzString *)sz_list_fold_right(xs, bang, fold_concat_pair, NULL);
    assert(strcmp(sz_string_cstr(folded), "ab!") == 0);
    sz_release(folded);
    folded = (SzString *)sz_list_fold_left(NULL, bang, fold_concat_pair, NULL);
    assert(strcmp(sz_string_cstr(folded), "!") == 0);
    sz_release(folded);
    sz_list_free(ns);
    sz_list_free(xs);
    sz_release(n1);
    sz_release(n2);
    sz_release(n3);
    sz_release(z0);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(bang);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List.scanLeft / scanRight / reduceLeft / reduceRight. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *a;
    SzString *b;
    SzString *bang;
    SzList *xs;
    SzList *tmp;
    SzList *scanned;
    void *n1;
    void *n2;
    void *n3;
    void *z0;
    void *sum;
    SzList *ns;
    SzString *folded;
    sz_alloc_stats(&base_bytes, &base_count);
    a = sz_string_from_cstr("a");
    b = sz_string_from_cstr("b");
    bang = sz_string_from_cstr("!");
    xs = sz_list_cons(a, NULL);
    tmp = sz_list_append(xs, b);
    sz_release(xs);
    xs = tmp;
    n1 = sz_box_i64(1);
    n2 = sz_box_i64(2);
    n3 = sz_box_i64(3);
    ns = sz_list_cons(n1, NULL);
    tmp = sz_list_append(ns, n2);
    sz_release(ns);
    ns = sz_list_append(tmp, n3);
    sz_release(tmp);
    z0 = sz_box_i64(0);
    scanned = sz_list_scan_left(ns, z0, fold_add_i64, NULL);
    assert(sz_list_len(scanned) == 4);
    assert(sz_unbox_i64(sz_list_at(scanned, 0)) == 0);
    assert(sz_unbox_i64(sz_list_at(scanned, 1)) == 1);
    assert(sz_unbox_i64(sz_list_at(scanned, 2)) == 3);
    assert(sz_unbox_i64(sz_list_at(scanned, 3)) == 6);
    sz_list_free(scanned);
    scanned = sz_list_scan_right(ns, z0, fold_add_i64, NULL);
    assert(sz_list_len(scanned) == 4);
    assert(sz_unbox_i64(sz_list_at(scanned, 0)) == 6);
    assert(sz_unbox_i64(sz_list_at(scanned, 1)) == 5);
    assert(sz_unbox_i64(sz_list_at(scanned, 2)) == 3);
    assert(sz_unbox_i64(sz_list_at(scanned, 3)) == 0);
    sz_list_free(scanned);
    scanned = sz_list_scan_left(NULL, z0, fold_add_i64, NULL);
    assert(sz_list_len(scanned) == 1);
    assert(sz_unbox_i64(sz_list_head(scanned)) == 0);
    sz_list_free(scanned);
    scanned = sz_list_scan_right(NULL, z0, fold_add_i64, NULL);
    assert(sz_list_len(scanned) == 1);
    assert(sz_unbox_i64(sz_list_head(scanned)) == 0);
    sz_list_free(scanned);
    scanned = sz_list_scan_left(xs, bang, fold_concat_pair, NULL);
    assert(sz_list_len(scanned) == 3);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_at(scanned, 0)), "!") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_at(scanned, 1)), "!a") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_at(scanned, 2)), "!ab") == 0);
    sz_list_free(scanned);
    scanned = sz_list_scan_right(xs, bang, fold_concat_pair, NULL);
    assert(sz_list_len(scanned) == 3);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_at(scanned, 0)), "ab!") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_at(scanned, 1)), "b!") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_at(scanned, 2)), "!") == 0);
    sz_list_free(scanned);
    sum = sz_list_reduce_left(ns, fold_add_i64, NULL);
    assert(sz_unbox_i64(sum) == 6);
    sz_release(sum);
    sum = sz_list_reduce_right(ns, fold_add_i64, NULL);
    assert(sz_unbox_i64(sum) == 6);
    sz_release(sum);
    folded = (SzString *)sz_list_reduce_left(xs, fold_concat_pair, NULL);
    assert(strcmp(sz_string_cstr(folded), "ab") == 0);
    sz_release(folded);
    folded = (SzString *)sz_list_reduce_right(xs, fold_concat_pair, NULL);
    assert(strcmp(sz_string_cstr(folded), "ab") == 0);
    sz_release(folded);
    tmp = sz_list_cons(a, NULL);
    folded = (SzString *)sz_list_reduce_left(tmp, fold_concat_pair, NULL);
    assert(strcmp(sz_string_cstr(folded), "a") == 0);
    sz_release(folded);
    sz_list_free(tmp);
    sz_list_free(ns);
    sz_list_free(xs);
    sz_release(n1);
    sz_release(n2);
    sz_release(n3);
    sz_release(z0);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(bang);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *a;
    SzString *a2;
    SzString *b;
    SzString *c;
    SzString *z;
    SzList *xs;
    SzList *tmp;
    SzList *ys;
    SzList *out;
    void *n1;
    void *n2;
    void *n9;
    SzList *ns;
    sz_alloc_stats(&base_bytes, &base_count);
    a = sz_string_from_cstr("a");
    a2 = sz_string_from_cstr("a");
    b = sz_string_from_cstr("b");
    c = sz_string_from_cstr("c");
    z = sz_string_from_cstr("z");
    xs = sz_list_cons(a, NULL);
    tmp = sz_list_append(xs, b);
    sz_release(xs);
    xs = sz_list_append(tmp, a);
    sz_release(tmp);
    tmp = sz_list_append(xs, c);
    sz_release(xs);
    xs = tmp;
    assert(sz_ptr_eq(a, a2) == 1);
    assert(sz_ptr_eq(a, b) == 0);
    assert(sz_ptr_eq(NULL, NULL) == 1);
    assert(sz_ptr_eq(a, NULL) == 0);
    assert(sz_list_contains(xs, a2) == 1);
    assert(sz_list_contains(xs, z) == 0);
    assert(sz_list_contains(NULL, a) == 0);
    assert(sz_list_index_of(xs, a2) == 0);
    assert(sz_list_index_of(xs, b) == 1);
    assert(sz_list_index_of(xs, z) == -1);
    assert(sz_list_index_of(NULL, a) == -1);
    assert(sz_list_last_index_of(xs, a2) == 2);
    assert(sz_list_last_index_of(xs, b) == 1);
    assert(sz_list_last_index_of(xs, z) == -1);
    out = sz_list_distinct(xs);
    assert(sz_list_len(out) == 3);
    assert(out->head == a);
    assert(sz_list_at(out, 1) == b);
    assert(sz_list_at(out, 2) == c);
    sz_list_free(out);
    out = sz_list_distinct(NULL);
    assert(sz_list_is_empty(out));
    ys = sz_list_cons(a2, NULL);
    tmp = sz_list_append(ys, z);
    sz_release(ys);
    ys = tmp;
    out = sz_list_diff(xs, ys);
    assert(sz_list_len(out) == 2);
    assert(out->head == b);
    assert(sz_list_at(out, 1) == c);
    sz_list_free(out);
    out = sz_list_diff(xs, NULL);
    assert(sz_list_len(out) == 4);
    assert(out->head == a);
    sz_list_free(out);
    out = sz_list_diff(NULL, ys);
    assert(sz_list_is_empty(out));
    out = sz_list_intersect(xs, ys);
    assert(sz_list_len(out) == 2);
    assert(out->head == a);
    assert(sz_list_at(out, 1) == a);
    sz_list_free(out);
    out = sz_list_intersect(xs, NULL);
    assert(sz_list_is_empty(out));
    out = sz_list_intersect(NULL, ys);
    assert(sz_list_is_empty(out));
    n1 = sz_box_i64(1);
    n2 = sz_box_i64(2);
    n9 = sz_box_i64(1);
    ns = sz_list_cons(n1, NULL);
    tmp = sz_list_append(ns, n2);
    sz_release(ns);
    ns = sz_list_append(tmp, n1);
    sz_release(tmp);
    assert(sz_ptr_eq(n1, n9) == 1);
    assert(sz_list_contains(ns, n9) == 1);
    assert(sz_list_index_of(ns, n9) == 0);
    assert(sz_list_last_index_of(ns, n9) == 2);
    out = sz_list_distinct(ns);
    assert(sz_list_len(out) == 2);
    assert(sz_unbox_i64(sz_list_head(out)) == 1);
    assert(sz_unbox_i64(sz_list_at(out, 1)) == 2);
    sz_list_free(out);
    sz_list_free(ns);
    sz_release(n1);
    sz_release(n2);
    sz_release(n9);
    sz_list_free(xs);
    sz_list_free(ys);
    sz_string_free(a);
    sz_string_free(a2);
    sz_string_free(b);
    sz_string_free(c);
    sz_string_free(z);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List.startsWith / endsWith / sameElements / patch / findLast / prefixLength. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *a;
    SzString *a2;
    SzString *b;
    SzString *c;
    SzString *x;
    SzString *y;
    SzList *xs;
    SzList *tmp;
    SzList *pre;
    SzList *suf;
    SzList *other;
    SzList *out;
    void *n1;
    void *n2;
    void *n3;
    SzList *ns;
    sz_alloc_stats(&base_bytes, &base_count);
    a = sz_string_from_cstr("a");
    a2 = sz_string_from_cstr("a");
    b = sz_string_from_cstr("b");
    c = sz_string_from_cstr("c");
    x = sz_string_from_cstr("x");
    y = sz_string_from_cstr("y");
    xs = sz_list_cons(a, NULL);
    tmp = sz_list_append(xs, b);
    sz_release(xs);
    xs = sz_list_append(tmp, a);
    sz_release(tmp);
    tmp = sz_list_append(xs, c);
    sz_release(xs);
    xs = tmp;
    pre = sz_list_cons(a2, NULL);
    tmp = sz_list_append(pre, b);
    sz_release(pre);
    pre = tmp;
    suf = sz_list_cons(a2, NULL);
    tmp = sz_list_append(suf, c);
    sz_release(suf);
    suf = tmp;
    assert(sz_list_starts_with(xs, pre) == 1);
    assert(sz_list_starts_with(xs, suf) == 0);
    assert(sz_list_starts_with(xs, NULL) == 1);
    assert(sz_list_starts_with(NULL, NULL) == 1);
    assert(sz_list_starts_with(NULL, pre) == 0);
    assert(sz_list_ends_with(xs, suf) == 1);
    assert(sz_list_ends_with(xs, pre) == 0);
    assert(sz_list_ends_with(xs, NULL) == 1);
    assert(sz_list_ends_with(NULL, NULL) == 1);
    assert(sz_list_ends_with(NULL, suf) == 0);
    assert(sz_list_same_elements(xs, xs) == 1);
    assert(sz_list_same_elements(xs, pre) == 0);
    assert(sz_list_same_elements(NULL, NULL) == 1);
    assert(sz_list_same_elements(xs, NULL) == 0);
    other = sz_list_cons(x, NULL);
    tmp = sz_list_append(other, y);
    sz_release(other);
    other = tmp;
    out = sz_list_patch(xs, 1, other, 2);
    assert(sz_list_len(out) == 4);
    assert(out->head == a);
    assert(sz_list_at(out, 1) == x);
    assert(sz_list_at(out, 2) == y);
    assert(sz_list_at(out, 3) == c);
    sz_list_free(out);
    out = sz_list_patch(xs, 0, other, 0);
    assert(sz_list_len(out) == 6);
    assert(out->head == x);
    assert(sz_list_at(out, 2) == a);
    sz_list_free(out);
    out = sz_list_patch(xs, 10, other, 2);
    assert(sz_list_len(out) == 6);
    assert(sz_list_at(out, 4) == x);
    sz_list_free(out);
    out = sz_list_patch(NULL, 0, other, 1);
    assert(sz_list_len(out) == 2);
    assert(out->head == x);
    sz_list_free(out);
    out = sz_list_patch(xs, -1, other, -3);
    assert(sz_list_len(out) == 6);
    assert(out->head == x);
    sz_list_free(out);
    out = sz_list_find_last(xs, keep_a, NULL);
    assert(sz_list_len(out) == 1);
    assert(out->head == a);
    sz_list_free(out);
    out = sz_list_find_last(xs, keep_none, NULL);
    assert(sz_list_is_empty(out));
    out = sz_list_find_last(NULL, keep_a, NULL);
    assert(sz_list_is_empty(out));
    assert(sz_list_prefix_length(xs, keep_a, NULL) == 1);
    assert(sz_list_prefix_length(xs, keep_none, NULL) == 0);
    assert(sz_list_prefix_length(NULL, keep_a, NULL) == 0);
    n1 = sz_box_i64(1);
    n2 = sz_box_i64(2);
    n3 = sz_box_i64(1);
    ns = sz_list_cons(n1, NULL);
    tmp = sz_list_append(ns, n2);
    sz_release(ns);
    ns = sz_list_append(tmp, n1);
    sz_release(tmp);
    tmp = sz_list_cons(n3, NULL);
    assert(sz_list_starts_with(ns, tmp) == 1);
    sz_list_free(tmp);
    tmp = sz_list_cons(n2, NULL);
    assert(sz_list_ends_with(ns, tmp) == 0);
    sz_list_free(tmp);
    sz_list_free(ns);
    sz_release(n1);
    sz_release(n2);
    sz_release(n3);
    sz_list_free(xs);
    sz_list_free(pre);
    sz_list_free(suf);
    sz_list_free(other);
    sz_string_free(a);
    sz_string_free(a2);
    sz_string_free(b);
    sz_string_free(c);
    sz_string_free(x);
    sz_string_free(y);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List.indexOfSlice / lastIndexOfSlice / segmentLength / isDefinedAt /
   * lengthCompare. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *a;
    SzString *a2;
    SzString *b;
    SzString *c;
    SzList *xs;
    SzList *tmp;
    SzList *ab;
    SzList *ba;
    SzList *aa;
    void *n1;
    void *n2;
    void *n9;
    SzList *ns;
    SzList *nslice;
    sz_alloc_stats(&base_bytes, &base_count);
    a = sz_string_from_cstr("a");
    a2 = sz_string_from_cstr("a");
    b = sz_string_from_cstr("b");
    c = sz_string_from_cstr("c");
    xs = sz_list_cons(a, NULL);
    tmp = sz_list_append(xs, b);
    sz_release(xs);
    xs = sz_list_append(tmp, a);
    sz_release(tmp);
    tmp = sz_list_append(xs, b);
    sz_release(xs);
    xs = tmp;
    ab = sz_list_cons(a2, NULL);
    tmp = sz_list_append(ab, b);
    sz_release(ab);
    ab = tmp;
    ba = sz_list_cons(b, NULL);
    tmp = sz_list_append(ba, a2);
    sz_release(ba);
    ba = tmp;
    aa = sz_list_cons(a2, NULL);
    tmp = sz_list_append(aa, a2);
    sz_release(aa);
    aa = tmp;
    assert(sz_list_index_of_slice(xs, ab) == 0);
    assert(sz_list_last_index_of_slice(xs, ab) == 2);
    assert(sz_list_index_of_slice(xs, ba) == 1);
    assert(sz_list_last_index_of_slice(xs, ba) == 1);
    assert(sz_list_index_of_slice(xs, aa) == -1);
    assert(sz_list_last_index_of_slice(xs, aa) == -1);
    assert(sz_list_index_of_slice(xs, NULL) == 0);
    assert(sz_list_last_index_of_slice(xs, NULL) == 4);
    assert(sz_list_index_of_slice(NULL, NULL) == 0);
    assert(sz_list_last_index_of_slice(NULL, NULL) == 0);
    assert(sz_list_index_of_slice(NULL, ab) == -1);
    assert(sz_list_last_index_of_slice(NULL, ab) == -1);
    tmp = sz_list_cons(a, NULL);
    {
      SzList *mid = sz_list_append(tmp, b);
      sz_release(tmp);
      tmp = sz_list_append(mid, c);
      sz_release(mid);
    }
    assert(sz_list_index_of_slice(xs, tmp) == -1);
    sz_list_free(tmp);
    assert(sz_list_segment_length(xs, keep_a, NULL, 0) == 1);
    assert(sz_list_segment_length(xs, keep_a, NULL, 2) == 1);
    assert(sz_list_segment_length(xs, keep_a, NULL, 1) == 0);
    assert(sz_list_segment_length(xs, keep_a, NULL, -3) == 1);
    assert(sz_list_segment_length(xs, keep_a, NULL, 9) == 0);
    assert(sz_list_segment_length(NULL, keep_a, NULL, 0) == 0);
    assert(sz_list_is_defined_at(xs, 0) == 1);
    assert(sz_list_is_defined_at(xs, 3) == 1);
    assert(sz_list_is_defined_at(xs, 4) == 0);
    assert(sz_list_is_defined_at(xs, -1) == 0);
    assert(sz_list_is_defined_at(NULL, 0) == 0);
    assert(sz_list_length_compare(xs, 4) == 0);
    assert(sz_list_length_compare(xs, 3) > 0);
    assert(sz_list_length_compare(xs, 5) < 0);
    assert(sz_list_length_compare(NULL, 0) == 0);
    assert(sz_list_length_compare(NULL, 1) < 0);
    assert(sz_list_length_compare(xs, -1) > 0);
    n1 = sz_box_i64(1);
    n2 = sz_box_i64(2);
    n9 = sz_box_i64(1);
    ns = sz_list_cons(n1, NULL);
    tmp = sz_list_append(ns, n2);
    sz_release(ns);
    ns = sz_list_append(tmp, n1);
    sz_release(tmp);
    nslice = sz_list_cons(n9, NULL);
    tmp = sz_list_append(nslice, n2);
    sz_release(nslice);
    nslice = tmp;
    assert(sz_list_index_of_slice(ns, nslice) == 0);
    assert(sz_list_last_index_of_slice(ns, nslice) == 0);
    assert(sz_list_is_defined_at(ns, 2) == 1);
    assert(sz_list_length_compare(ns, 3) == 0);
    sz_list_free(nslice);
    sz_list_free(ns);
    sz_release(n1);
    sz_release(n2);
    sz_release(n9);
    sz_list_free(xs);
    sz_list_free(ab);
    sz_list_free(ba);
    sz_list_free(aa);
    sz_string_free(a);
    sz_string_free(a2);
    sz_string_free(b);
    sz_string_free(c);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List.sort / sortBy / max / min / maxBy / minBy. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *a;
    SzString *b;
    SzString *c;
    SzString *aa;
    SzList *xs;
    SzList *out;
    SzList *ns;
    void *n1;
    void *n2;
    void *n3;
    sz_alloc_stats(&base_bytes, &base_count);
    a = sz_string_from_cstr("c");
    b = sz_string_from_cstr("a");
    c = sz_string_from_cstr("b");
    aa = sz_string_from_cstr("aa");
    xs = sz_list_cons(a, NULL);
    {
      SzList *tmp = sz_list_append(xs, b);
      sz_release(xs);
      xs = sz_list_append(tmp, c);
      sz_release(tmp);
    }
    out = sz_list_sort(xs, 0);
    assert(sz_list_len(out) == 3);
    assert(out->head == b);
    assert(sz_list_at(out, 1) == c);
    assert(sz_list_at(out, 2) == a);
    sz_list_free(out);
    out = sz_list_sort(NULL, 0);
    assert(sz_list_is_empty(out));
    assert(sz_list_max(xs, 0) == a);
    assert(sz_list_min(xs, 0) == b);
    sz_list_free(xs);
    xs = sz_list_cons(aa, NULL);
    {
      SzList *tmp = sz_list_append(xs, b);
      sz_release(xs);
      xs = sz_list_append(tmp, a);
      sz_release(tmp);
    }
    out = sz_list_sort_by(xs, str_len_key, NULL);
    assert(sz_list_len(out) == 3);
    assert(out->head == b);
    assert(sz_list_at(out, 1) == a);
    assert(sz_list_at(out, 2) == aa);
    sz_list_free(out);
    assert(sz_list_max_by(xs, str_len_key, NULL, 1) == aa);
    assert(sz_list_max_by(xs, str_len_key, NULL, 0) == b);
    sz_list_free(xs);
    n1 = sz_box_i64(3);
    n2 = sz_box_i64(1);
    n3 = sz_box_i64(2);
    ns = sz_list_cons(n1, NULL);
    {
      SzList *tmp = sz_list_append(ns, n2);
      sz_release(ns);
      ns = sz_list_append(tmp, n3);
      sz_release(tmp);
    }
    out = sz_list_sort(ns, 1);
    assert(sz_unbox_i64(sz_list_head(out)) == 1);
    assert(sz_unbox_i64(sz_list_at(out, 1)) == 2);
    assert(sz_unbox_i64(sz_list_at(out, 2)) == 3);
    sz_list_free(out);
    assert(sz_unbox_i64(sz_list_max(ns, 1)) == 3);
    assert(sz_unbox_i64(sz_list_min(ns, 1)) == 1);
    sz_list_free(ns);
    sz_release(n1);
    sz_release(n2);
    sz_release(n3);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
    sz_string_free(aa);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List.groupBy / sum / product. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *aa;
    SzString *b;
    SzString *cc;
    SzString *ab;
    SzString *ac;
    SzList *xs;
    SzList *ns;
    SzMap *g;
    SzList *keys;
    SzList *vals;
    SzList *row;
    void *n1;
    void *n2;
    void *n3;
    sz_alloc_stats(&base_bytes, &base_count);
    aa = sz_string_from_cstr("aa");
    b = sz_string_from_cstr("b");
    cc = sz_string_from_cstr("cc");
    xs = sz_list_cons(aa, NULL);
    {
      SzList *tmp = sz_list_append(xs, b);
      sz_release(xs);
      xs = sz_list_append(tmp, cc);
      sz_release(tmp);
    }
    g = sz_list_group_by(xs, str_len_key, NULL, 0);
    assert(sz_map_size(g) == 2);
    keys = sz_map_keys(g);
    assert(sz_list_len(keys) == 2);
    assert(sz_unbox_i64(sz_list_head(keys)) == 1);
    assert(sz_unbox_i64(sz_list_at(keys, 1)) == 2);
    vals = sz_map_values(g);
    row = (SzList *)sz_list_head(vals);
    assert(sz_list_len(row) == 1);
    assert(row->head == b);
    row = (SzList *)sz_list_at(vals, 1);
    assert(sz_list_len(row) == 2);
    assert(row->head == aa);
    assert(sz_list_at(row, 1) == cc);
    sz_list_free(keys);
    sz_list_free(vals);
    sz_release(g);
    g = sz_list_group_by(NULL, str_len_key, NULL, 0);
    assert(g == NULL);
    sz_list_free(xs);
    ab = sz_string_from_cstr("ab");
    ac = sz_string_from_cstr("ac");
    xs = sz_list_cons(ab, NULL);
    {
      SzList *tmp = sz_list_append(xs, ac);
      sz_release(xs);
      xs = sz_list_append(tmp, b);
      sz_release(tmp);
    }
    g = sz_list_group_by(xs, str_take1, NULL, 1);
    assert(sz_map_size(g) == 2);
    keys = sz_map_keys(g);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(keys)), "a") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_at(keys, 1)), "b") == 0);
    vals = sz_map_values(g);
    row = (SzList *)sz_list_head(vals);
    assert(sz_list_len(row) == 2);
    assert(row->head == ab);
    assert(sz_list_at(row, 1) == ac);
    row = (SzList *)sz_list_at(vals, 1);
    assert(sz_list_len(row) == 1);
    assert(row->head == b);
    sz_list_free(keys);
    sz_list_free(vals);
    sz_release(g);
    sz_list_free(xs);
    n1 = sz_box_i64(3);
    n2 = sz_box_i64(1);
    n3 = sz_box_i64(3);
    ns = sz_list_cons(n1, NULL);
    {
      SzList *tmp = sz_list_append(ns, n2);
      sz_release(ns);
      ns = sz_list_append(tmp, n3);
      sz_release(tmp);
    }
    g = sz_list_group_by(ns, map_id, NULL, 0);
    assert(sz_map_size(g) == 2);
    keys = sz_map_keys(g);
    assert(sz_unbox_i64(sz_list_head(keys)) == 1);
    assert(sz_unbox_i64(sz_list_at(keys, 1)) == 3);
    vals = sz_map_values(g);
    row = (SzList *)sz_list_at(vals, 1);
    assert(sz_list_len(row) == 2);
    sz_list_free(keys);
    sz_list_free(vals);
    sz_release(g);
    assert(sz_list_sum(ns) == 7);
    assert(sz_list_product(ns) == 9);
    assert(sz_list_sum(NULL) == 0);
    assert(sz_list_product(NULL) == 1);
    sz_list_free(ns);
    sz_release(n1);
    sz_release(n2);
    sz_release(n3);
    sz_string_free(aa);
    sz_string_free(b);
    sz_string_free(cc);
    sz_string_free(ab);
    sz_string_free(ac);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List.distinctBy / toMap / toSet / Map.toList. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *aa;
    SzString *b;
    SzString *cc;
    SzString *d;
    SzString *ka;
    SzString *kb;
    SzString *va;
    SzString *vb;
    SzString *v9;
    SzList *xs;
    SzList *tmp;
    SzList *out;
    SzPair *p1;
    SzPair *p2;
    SzPair *p3;
    SzList *pairs;
    SzMap *m;
    SzMap *s;
    SzList *keys;
    SzList *rows;
    SzPair *row;
    void *n1;
    void *n2;
    void *n3;
    SzList *ns;
    sz_alloc_stats(&base_bytes, &base_count);
    aa = sz_string_from_cstr("aa");
    b = sz_string_from_cstr("b");
    cc = sz_string_from_cstr("cc");
    d = sz_string_from_cstr("d");
    xs = sz_list_cons(aa, NULL);
    tmp = sz_list_append(xs, b);
    sz_release(xs);
    xs = sz_list_append(tmp, cc);
    sz_release(tmp);
    tmp = sz_list_append(xs, d);
    sz_release(xs);
    xs = tmp;
    out = sz_list_distinct_by(xs, str_len_key, NULL, 0);
    assert(sz_list_len(out) == 2);
    assert(out->head == aa);
    assert(sz_list_at(out, 1) == b);
    sz_list_free(out);
    out = sz_list_distinct_by(NULL, str_len_key, NULL, 0);
    assert(out == NULL);
    sz_list_free(xs);
    ka = sz_string_from_cstr("a");
    kb = sz_string_from_cstr("b");
    va = sz_string_from_cstr("1");
    vb = sz_string_from_cstr("2");
    v9 = sz_string_from_cstr("9");
    p1 = sz_pair_new(ka, va);
    p2 = sz_pair_new(kb, vb);
    p3 = sz_pair_new(ka, v9);
    pairs = sz_list_cons(p1, NULL);
    tmp = sz_list_append(pairs, p2);
    sz_release(pairs);
    pairs = sz_list_append(tmp, p3);
    sz_release(tmp);
    m = sz_list_to_map(pairs);
    assert(sz_map_size(m) == 2);
    assert(strcmp(sz_string_cstr((SzString *)sz_map_get_or(m, ka, NULL)), "9") ==
           0);
    assert(strcmp(sz_string_cstr((SzString *)sz_map_get_or(m, kb, NULL)), "2") ==
           0);
    rows = sz_map_to_list(m);
    assert(sz_list_len(rows) == 2);
    row = (SzPair *)sz_list_head(rows);
    assert(row->left == ka);
    assert(strcmp(sz_string_cstr((SzString *)row->right), "9") == 0);
    sz_list_free(rows);
    sz_release(m);
    m = sz_list_to_map(NULL);
    assert(m == NULL);
    rows = sz_map_to_list(NULL);
    assert(rows == NULL);
    sz_list_free(pairs);
    sz_release(p1);
    sz_release(p2);
    sz_release(p3);
    xs = sz_list_cons(kb, NULL);
    tmp = sz_list_append(xs, ka);
    sz_release(xs);
    xs = sz_list_append(tmp, kb);
    sz_release(tmp);
    s = sz_list_to_set(xs, 1);
    assert(sz_map_size(s) == 2);
    keys = sz_map_keys(s);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(keys)), "a") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_at(keys, 1)), "b") == 0);
    sz_list_free(keys);
    sz_release(s);
    s = sz_list_to_set(NULL, 1);
    assert(s == NULL);
    sz_list_free(xs);
    n1 = sz_box_i64(1);
    n2 = sz_box_i64(2);
    n3 = sz_box_i64(1);
    ns = sz_list_cons(n1, NULL);
    tmp = sz_list_append(ns, n2);
    sz_release(ns);
    ns = sz_list_append(tmp, n3);
    sz_release(tmp);
    s = sz_list_to_set(ns, 0);
    assert(sz_map_size(s) == 2);
    keys = sz_map_keys(s);
    assert(sz_unbox_i64(sz_list_head(keys)) == 1);
    assert(sz_unbox_i64(sz_list_at(keys, 1)) == 2);
    sz_list_free(keys);
    sz_release(s);
    sz_list_free(ns);
    sz_release(n1);
    sz_release(n2);
    sz_release(n3);
    sz_string_free(aa);
    sz_string_free(b);
    sz_string_free(cc);
    sz_string_free(d);
    sz_string_free(ka);
    sz_string_free(kb);
    sz_string_free(va);
    sz_string_free(vb);
    sz_string_free(v9);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List.concat: left spine copy; empty left retains right. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *xs = sz_list_cons(a, sz_list_nil());
    SzList *ys = sz_list_cons(b, sz_list_cons(c, sz_list_nil()));
    SzList *zs = sz_list_concat(xs, ys);
    SzList *empty;
    assert(sz_list_len(zs) == 3);
    assert(zs->head == a);
    assert(zs->tail && zs->tail->head == b);
    assert(zs->tail->tail && zs->tail->tail->head == c);
    assert(sz_list_len(xs) == 1);
    sz_list_free(zs);
    empty = sz_list_concat(NULL, ys);
    assert(empty == ys);
    sz_release(empty);
    empty = sz_list_concat(xs, NULL);
    assert(sz_list_len(empty) == 1);
    assert(empty->head == a);
    sz_list_free(empty);
    sz_list_free(xs);
    sz_list_free(ys);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }

  /* Spine combinators loop: 10k cells finish and do not leak. One shared
   * string is the head of every cons. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *s;
    SzString *y;
    SzList *xs = NULL;
    SzList *tmp;
    SzList *ys;
    SzList *sing;
    SzList *nums = NULL;
    void *one;
    void *z0;
    void *sum;
    int i;
    sz_alloc_stats(&base_bytes, &base_count);
    s = sz_string_from_cstr("x");
    y = sz_string_from_cstr("y");
    for (i = 0; i < 10000; i++) {
      tmp = sz_list_cons(s, xs);
      sz_release(xs);
      xs = tmp;
    }
    ys = sz_list_map(xs, map_id, NULL);
    assert(sz_list_len(ys) == 10000);
    sz_list_free(ys);
    ys = sz_list_filter(xs, keep_not_b, NULL);
    assert(sz_list_len(ys) == 10000);
    sz_list_free(ys);
    ys = sz_list_concat(xs, NULL);
    assert(sz_list_len(ys) == 10000);
    sz_list_free(ys);
    ys = sz_list_init(xs);
    assert(sz_list_len(ys) == 9999);
    sz_list_free(ys);
    ys = sz_list_set_at(xs, 0, y);
    assert(sz_list_len(ys) == 10000);
    assert(ys->head == y);
    sz_list_free(ys);
    sing = sz_list_map(xs, map_singleton, NULL);
    ys = sz_list_flatten(sing);
    assert(sz_list_len(ys) == 10000);
    sz_list_free(ys);
    sz_list_free(sing);
    ys = sz_list_zip(xs, xs);
    assert(sz_list_len(ys) == 10000);
    sz_list_free(ys);
    ys = sz_list_tails(xs);
    assert(sz_list_len(ys) == 10001);
    sz_list_free(ys);
    ys = sz_list_takewhile(xs, keep_true, NULL);
    assert(sz_list_len(ys) == 10000);
    sz_list_free(ys);
    ys = sz_list_intersperse(xs, s);
    assert(sz_list_len(ys) == 19999);
    sz_list_free(ys);
    ys = sz_list_sliding(xs, 2);
    assert(sz_list_len(ys) == 9999);
    sz_list_free(ys);
    ys = sz_list_grouped(xs, 10);
    assert(sz_list_len(ys) == 1000);
    sz_list_free(ys);
    one = sz_box_i64(1);
    z0 = sz_box_i64(0);
    for (i = 0; i < 10000; i++) {
      tmp = sz_list_cons(one, nums);
      sz_release(nums);
      nums = tmp;
    }
    sum = sz_list_fold_right(nums, z0, fold_add_i64, NULL);
    assert(sz_unbox_i64(sum) == 10000);
    sz_release(sum);
    sz_list_free(nums);
    sz_release(one);
    sz_release(z0);
    sz_list_free(xs);
    sz_string_free(s);
    sz_string_free(y);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* 100k spine: init, takeWhile, and sliding must loop, not recurse. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *s;
    SzList *xs = NULL;
    SzList *tmp;
    SzList *ys;
    int i;
    sz_alloc_stats(&base_bytes, &base_count);
    s = sz_string_from_cstr("x");
    for (i = 0; i < 100000; i++) {
      tmp = sz_list_cons(s, xs);
      sz_release(xs);
      xs = tmp;
    }
    ys = sz_list_init(xs);
    assert(sz_list_len(ys) == 99999);
    sz_list_free(ys);
    ys = sz_list_takewhile(xs, keep_true, NULL);
    assert(sz_list_len(ys) == 100000);
    sz_list_free(ys);
    ys = sz_list_sliding(xs, 2);
    assert(sz_list_len(ys) == 99999);
    sz_list_free(ys);
    sz_list_free(xs);
    sz_string_free(s);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* List.flatten: concatenate inner lists. */
  {
    SzString *a = sz_string_from_cstr("a");
    SzString *b = sz_string_from_cstr("b");
    SzString *c = sz_string_from_cstr("c");
    SzList *inner1 = sz_list_cons(a, sz_list_nil());
    SzList *inner2 = sz_list_cons(b, sz_list_cons(c, sz_list_nil()));
    SzList *xss = sz_list_cons(inner1, sz_list_cons(inner2, sz_list_nil()));
    SzList *flat = sz_list_flatten(xss);
    SzList *empty;
    assert(sz_list_len(flat) == 3);
    assert(flat->head == a);
    assert(flat->tail && flat->tail->head == b);
    assert(flat->tail->tail && flat->tail->tail->head == c);
    sz_list_free(flat);
    empty = sz_list_flatten(NULL);
    assert(sz_list_is_empty(empty));
    sz_list_free(xss);
    sz_list_free(inner1);
    sz_list_free(inner2);
    sz_string_free(a);
    sz_string_free(b);
    sz_string_free(c);
  }
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *s;
    SzString *a;
    SzString *b;
    SzList *xs;
    SzList *ys;
    sz_alloc_stats(&base_bytes, &base_count);
    s = sz_string_from_cstr("rc");
    sz_retain(s);
    sz_release(s);
    sz_release(s);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
    a = sz_string_from_cstr("a");
    b = sz_string_from_cstr("b");
    xs = sz_list_cons(a, sz_list_nil());
    ys = sz_list_cons(b, xs);
    sz_list_free(ys);
    assert(xs->head == a);
    sz_list_free(xs);
    sz_string_free(a);
    sz_string_free(b);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Persistent map: shared subtrees; release returns to baseline. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *ka;
    SzString *kb;
    SzString *va;
    SzString *vb;
    SzMap *m0;
    SzMap *m1;
    SzMap *m2;
    sz_alloc_stats(&base_bytes, &base_count);
    ka = sz_string_from_cstr("a");
    kb = sz_string_from_cstr("b");
    va = sz_string_from_cstr("1");
    vb = sz_string_from_cstr("2");
    m0 = sz_map_empty();
    m1 = sz_map_set(m0, ka, va, 1);
    m2 = sz_map_set(m1, kb, vb, 1);
    assert(sz_map_contains(m2, ka) == 1);
    assert(sz_map_contains(m1, kb) == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_map_get_or(m2, ka, NULL)), "1") == 0);
    {
      SzList *hit = sz_map_get(m2, ka);
      SzList *miss = sz_map_get(m1, kb);
      SzString *kz = sz_string_from_cstr("z");
      SzList *absent = sz_map_get(m2, kz);
      assert(sz_list_len(hit) == 1);
      assert(strcmp(sz_string_cstr((SzString *)hit->head), "1") == 0);
      assert(sz_list_is_empty(miss));
      assert(sz_list_is_empty(absent));
      sz_list_free(hit);
      sz_string_free(kz);
    }
    sz_release(m2);
    sz_release(m1);
    sz_string_free(ka);
    sz_string_free(kb);
    sz_string_free(va);
    sz_string_free(vb);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Persistent map remove / keys / size. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *ka;
    SzString *kb;
    SzString *kc;
    SzString *kz;
    SzString *va;
    SzString *vb;
    SzString *vc;
    SzMap *m1;
    SzMap *m2;
    SzMap *m3;
    SzMap *gone;
    SzMap *miss;
    SzMap *two;
    SzList *ks;
    SzList *ks2;
    void *k1;
    void *k2;
    void *k3;
    SzMap *i1;
    SzMap *i2;
    SzMap *i3;
    SzMap *i4;
    sz_alloc_stats(&base_bytes, &base_count);
    ka = sz_string_from_cstr("a");
    kb = sz_string_from_cstr("b");
    kc = sz_string_from_cstr("c");
    kz = sz_string_from_cstr("z");
    va = sz_string_from_cstr("1");
    vb = sz_string_from_cstr("2");
    vc = sz_string_from_cstr("3");
    m1 = sz_map_set(NULL, ka, va, 1);
    m2 = sz_map_set(m1, kb, vb, 1);
    assert(sz_map_size(m2) == 2);
    ks = sz_map_keys(m2);
    assert(sz_list_len(ks) == 2);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(ks)), "a") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(sz_list_tail(ks))), "b") ==
           0);
    sz_list_free(ks);
    {
      SzList *vs = sz_map_values(m2);
      assert(sz_list_len(vs) == 2);
      assert(strcmp(sz_string_cstr((SzString *)sz_list_head(vs)), "1") == 0);
      assert(strcmp(sz_string_cstr((SzString *)sz_list_head(sz_list_tail(vs))), "2") ==
             0);
      sz_list_free(vs);
    }
    gone = sz_map_remove(m2, kb);
    assert(sz_map_contains(gone, kb) == 0);
    assert(sz_map_contains(m2, kb) == 1);
    assert(sz_map_size(gone) == 1);
    miss = sz_map_remove(m2, kz);
    assert(miss == m2);
    assert(sz_map_size(miss) == 2);
    sz_release(miss);
    m3 = sz_map_set(m2, kc, vc, 1);
    two = sz_map_remove(m3, kb);
    assert(sz_map_size(two) == 2);
    assert(sz_map_contains(two, ka) == 1);
    assert(sz_map_contains(two, kc) == 1);
    assert(sz_map_contains(two, kb) == 0);
    ks2 = sz_map_keys(two);
    assert(sz_list_len(ks2) == 2);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(ks2)), "a") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(sz_list_tail(ks2))), "c") ==
           0);
    sz_list_free(ks2);
    k1 = sz_box_i64(1);
    k2 = sz_box_i64(2);
    k3 = sz_box_i64(3);
    i1 = sz_map_set(NULL, k2, va, 0);
    i2 = sz_map_set(i1, k1, va, 0);
    i3 = sz_map_set(i2, k3, va, 0);
    i4 = sz_map_remove(i3, k2);
    assert(sz_map_size(i4) == 2);
    assert(sz_map_contains(i4, k1) == 1);
    assert(sz_map_contains(i4, k3) == 1);
    assert(sz_map_contains(i4, k2) == 0);
    sz_release(i4);
    sz_release(i3);
    sz_release(i2);
    sz_release(i1);
    sz_release(k1);
    sz_release(k2);
    sz_release(k3);
    sz_release(two);
    sz_release(m3);
    sz_release(gone);
    sz_release(m2);
    sz_release(m1);
    sz_string_free(ka);
    sz_string_free(kb);
    sz_string_free(kc);
    sz_string_free(kz);
    sz_string_free(va);
    sz_string_free(vb);
    sz_string_free(vc);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Persistent set union / intersect / diff. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *kx;
    SzString *ky;
    SzString *kz;
    SzMap *a0;
    SzMap *a;
    SzMap *b0;
    SzMap *b;
    SzMap *u;
    SzMap *isect;
    SzMap *d;
    SzMap *tmp;
    SzList *ks;
    sz_alloc_stats(&base_bytes, &base_count);
    kx = sz_string_from_cstr("x");
    ky = sz_string_from_cstr("y");
    kz = sz_string_from_cstr("z");
    a0 = sz_map_set(NULL, kx, NULL, 1);
    a = sz_map_set(a0, ky, NULL, 1);
    b0 = sz_map_set(NULL, ky, NULL, 1);
    b = sz_map_set(b0, kz, NULL, 1);
    u = sz_set_union(a, b);
    assert(sz_map_size(u) == 3);
    assert(sz_map_contains(u, kx) == 1);
    assert(sz_map_contains(u, ky) == 1);
    assert(sz_map_contains(u, kz) == 1);
    ks = sz_map_keys(u);
    assert(sz_list_len(ks) == 3);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(ks)), "x") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(sz_list_tail(ks))), "y") ==
           0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(
                      sz_list_tail(sz_list_tail(ks)))),
                  "z") == 0);
    sz_list_free(ks);
    isect = sz_set_intersect(a, b);
    assert(sz_map_size(isect) == 1);
    assert(sz_map_contains(isect, ky) == 1);
    d = sz_set_diff(a, b);
    assert(sz_map_size(d) == 1);
    assert(sz_map_contains(d, kx) == 1);
    tmp = sz_set_union(a, NULL);
    assert(tmp == a);
    sz_release(tmp);
    tmp = sz_set_union(NULL, b);
    assert(tmp == b);
    sz_release(tmp);
    assert(sz_set_intersect(NULL, b) == NULL);
    assert(sz_set_intersect(a, NULL) == NULL);
    assert(sz_set_diff(NULL, b) == NULL);
    tmp = sz_set_diff(a, NULL);
    assert(tmp == a);
    sz_release(tmp);
    tmp = sz_set_union(a, a);
    assert(sz_map_size(tmp) == 2);
    sz_release(tmp);
    sz_release(d);
    sz_release(isect);
    sz_release(u);
    sz_release(b);
    sz_release(b0);
    sz_release(a);
    sz_release(a0);
    sz_string_free(kx);
    sz_string_free(ky);
    sz_string_free(kz);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Persistent map union / intersect / diff keep values. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *ka;
    SzString *kb;
    SzString *kc;
    SzString *v1;
    SzString *v2;
    SzString *v9;
    SzString *v3;
    SzMap *a0;
    SzMap *a;
    SzMap *b0;
    SzMap *b;
    SzMap *u;
    SzMap *isect;
    SzMap *d;
    SzMap *tmp;
    SzList *vs;
    sz_alloc_stats(&base_bytes, &base_count);
    ka = sz_string_from_cstr("a");
    kb = sz_string_from_cstr("b");
    kc = sz_string_from_cstr("c");
    v1 = sz_string_from_cstr("1");
    v2 = sz_string_from_cstr("2");
    v9 = sz_string_from_cstr("9");
    v3 = sz_string_from_cstr("3");
    a0 = sz_map_set(NULL, ka, v1, 1);
    a = sz_map_set(a0, kb, v2, 1);
    b0 = sz_map_set(NULL, kb, v9, 1);
    b = sz_map_set(b0, kc, v3, 1);
    u = sz_map_union(a, b);
    assert(sz_map_size(u) == 3);
    assert(strcmp(sz_string_cstr((SzString *)sz_map_get_or(u, ka, NULL)), "1") ==
           0);
    assert(strcmp(sz_string_cstr((SzString *)sz_map_get_or(u, kb, NULL)), "9") ==
           0);
    assert(strcmp(sz_string_cstr((SzString *)sz_map_get_or(u, kc, NULL)), "3") ==
           0);
    vs = sz_map_values(u);
    assert(sz_list_len(vs) == 3);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(vs)), "1") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(sz_list_tail(vs))),
                  "9") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(
                      sz_list_tail(sz_list_tail(vs)))),
                  "3") == 0);
    sz_list_free(vs);
    isect = sz_map_intersect(a, b);
    assert(sz_map_size(isect) == 1);
    assert(strcmp(sz_string_cstr((SzString *)sz_map_get_or(isect, kb, NULL)),
                  "2") == 0);
    d = sz_map_diff(a, b);
    assert(sz_map_size(d) == 1);
    assert(strcmp(sz_string_cstr((SzString *)sz_map_get_or(d, ka, NULL)), "1") ==
           0);
    tmp = sz_map_union(a, NULL);
    assert(tmp == a);
    sz_release(tmp);
    tmp = sz_map_union(NULL, b);
    assert(tmp == b);
    sz_release(tmp);
    assert(sz_map_intersect(NULL, b) == NULL);
    assert(sz_map_intersect(a, NULL) == NULL);
    assert(sz_map_diff(NULL, b) == NULL);
    tmp = sz_map_diff(a, NULL);
    assert(tmp == a);
    sz_release(tmp);
    tmp = sz_map_union(a, a);
    assert(sz_map_size(tmp) == 2);
    assert(strcmp(sz_string_cstr((SzString *)sz_map_get_or(tmp, kb, NULL)),
                  "2") == 0);
    sz_release(tmp);
    sz_release(d);
    sz_release(isect);
    sz_release(u);
    sz_release(b);
    sz_release(b0);
    sz_release(a);
    sz_release(a0);
    sz_string_free(ka);
    sz_string_free(kb);
    sz_string_free(kc);
    sz_string_free(v1);
    sz_string_free(v2);
    sz_string_free(v9);
    sz_string_free(v3);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Persistent set subset / disjoint. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *kx;
    SzString *ky;
    SzString *kz;
    SzMap *a0;
    SzMap *a;
    SzMap *b0;
    SzMap *b;
    SzMap *z;
    sz_alloc_stats(&base_bytes, &base_count);
    kx = sz_string_from_cstr("x");
    ky = sz_string_from_cstr("y");
    kz = sz_string_from_cstr("z");
    a0 = sz_map_set(NULL, kx, NULL, 1);
    a = sz_map_set(a0, ky, NULL, 1);
    b0 = sz_map_set(NULL, ky, NULL, 1);
    b = sz_map_set(b0, kz, NULL, 1);
    z = sz_map_set(NULL, kz, NULL, 1);
    assert(sz_set_is_subset(a0, a) == 1);
    assert(sz_set_is_subset(a, a) == 1);
    assert(sz_set_is_subset(a, b) == 0);
    assert(sz_set_is_subset(NULL, a) == 1);
    assert(sz_set_is_subset(a, NULL) == 0);
    assert(sz_set_is_disjoint(a, z) == 1);
    assert(sz_set_is_disjoint(a, b) == 0);
    assert(sz_set_is_disjoint(NULL, a) == 1);
    assert(sz_set_is_disjoint(a, NULL) == 1);
    sz_release(z);
    sz_release(b);
    sz_release(b0);
    sz_release(a);
    sz_release(a0);
    sz_string_free(kx);
    sz_string_free(ky);
    sz_string_free(kz);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Map.filter / mapValues / exists / forall. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *ka;
    SzString *kb;
    SzString *kc;
    SzString *va;
    SzString *vb;
    SzString *vc;
    SzMap *m0;
    SzMap *m1;
    SzMap *m2;
    SzMap *kept;
    SzMap *mapped;
    SzList *vals;
    sz_alloc_stats(&base_bytes, &base_count);
    ka = sz_string_from_cstr("a");
    kb = sz_string_from_cstr("b");
    kc = sz_string_from_cstr("c");
    va = sz_string_from_cstr("1");
    vb = sz_string_from_cstr("b");
    vc = sz_string_from_cstr("3");
    m0 = sz_map_set(NULL, ka, va, 1);
    m1 = sz_map_set(m0, kb, vb, 1);
    m2 = sz_map_set(m1, kc, vc, 1);
    kept = sz_map_filter(m2, keep_not_b, NULL);
    assert(sz_map_size(kept) == 2);
    assert(sz_map_contains(kept, ka) == 1);
    assert(sz_map_contains(kept, kb) == 0);
    assert(sz_map_contains(kept, kc) == 1);
    assert(sz_map_exists(m2, keep_a, NULL) == 0);
    assert(sz_map_exists(m2, keep_not_b, NULL) == 1);
    assert(sz_map_forall(m2, keep_not_b, NULL) == 0);
    assert(sz_map_forall(kept, keep_not_b, NULL) == 1);
    assert(sz_map_filter(NULL, keep_not_b, NULL) == NULL);
    assert(sz_map_exists(NULL, keep_a, NULL) == 0);
    assert(sz_map_forall(NULL, keep_a, NULL) == 1);
    mapped = sz_map_map_values(kept, map_id, NULL);
    assert(sz_map_size(mapped) == 2);
    vals = sz_map_values(mapped);
    assert(sz_list_len(vals) == 2);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(vals)), "1") == 0);
    assert(sz_map_map_values(NULL, map_id, NULL) == NULL);
    sz_list_free(vals);
    sz_release(mapped);
    sz_release(kept);
    sz_release(m2);
    sz_release(m1);
    sz_release(m0);
    sz_string_free(ka);
    sz_string_free(kb);
    sz_string_free(kc);
    sz_string_free(va);
    sz_string_free(vb);
    sz_string_free(vc);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Set.filter / map / exists / forall. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzString *kx;
    SzString *ky;
    SzString *kz;
    SzMap *s0;
    SzMap *s1;
    SzMap *s2;
    SzMap *kept;
    SzMap *mapped;
    SzList *keys;
    sz_alloc_stats(&base_bytes, &base_count);
    kx = sz_string_from_cstr("a");
    ky = sz_string_from_cstr("b");
    kz = sz_string_from_cstr("c");
    s0 = sz_map_set(NULL, kx, NULL, 1);
    s1 = sz_map_set(s0, ky, NULL, 1);
    s2 = sz_map_set(s1, kz, NULL, 1);
    kept = sz_set_filter(s2, keep_not_b, NULL);
    assert(sz_map_size(kept) == 2);
    assert(sz_map_contains(kept, kx) == 1);
    assert(sz_map_contains(kept, ky) == 0);
    assert(sz_set_exists(s2, keep_a, NULL) == 1);
    assert(sz_set_exists(s2, keep_none, NULL) == 0);
    assert(sz_set_forall(s2, keep_not_b, NULL) == 0);
    assert(sz_set_forall(kept, keep_not_b, NULL) == 1);
    assert(sz_set_filter(NULL, keep_not_b, NULL) == NULL);
    assert(sz_set_exists(NULL, keep_a, NULL) == 0);
    assert(sz_set_forall(NULL, keep_a, NULL) == 1);
    mapped = sz_set_map(kept, str_take1, NULL, 1);
    assert(sz_map_size(mapped) == 2);
    keys = sz_map_keys(mapped);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_head(keys)), "a") == 0);
    assert(strcmp(sz_string_cstr((SzString *)sz_list_at(keys, 1)), "c") == 0);
    {
      SzString *aa = sz_string_from_cstr("aa");
      SzString *ab = sz_string_from_cstr("ab");
      SzMap *one = sz_map_set(NULL, aa, NULL, 1);
      SzMap *two = sz_map_set(one, ab, NULL, 1);
      SzMap *collapsed = sz_set_map(two, str_take1, NULL, 1);
      assert(sz_map_size(collapsed) == 1);
      sz_release(collapsed);
      sz_release(two);
      sz_release(one);
      sz_string_free(aa);
      sz_string_free(ab);
    }
    assert(sz_set_map(NULL, str_take1, NULL, 1) == NULL);
    sz_list_free(keys);
    sz_release(mapped);
    sz_release(kept);
    sz_release(s2);
    sz_release(s1);
    sz_release(s0);
    sz_string_free(kx);
    sz_string_free(ky);
    sz_string_free(kz);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* IO graph and fiber structs drop after unsafe_run. */
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    SzIoResult r;
    sz_alloc_stats(&base_bytes, &base_count);
    r = sz_io_unsafe_run(fm_drop(pure_drop(NULL), cont_pure_unit, NULL));
    assert(r.ok);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
    r = sz_io_unsafe_run(repeat_n_drop(1, pure_drop(NULL)));
    assert(r.ok);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }

  /* Fault injection: fail the Nth Fs op. */
  {
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_FAULT_KIND", "fs", 1);
    setenv("SCUZZ_FAULT_N", "1", 1);
    sz_testrt_install();
    r = sz_io_unsafe_run(
        sz_fs_write(sz_string_from_cstr("note.txt"), sz_string_from_cstr("hi")));
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "injected fault") != NULL);
    sz_error_free(r.error);
    sz_testrt_reset();
    unsetenv("SCUZZ_FAULT_KIND");
    unsetenv("SCUZZ_FAULT_N");
    unsetenv("SCUZZ_TESTRT");
  }

  /* Fault injection: drop a Net stub response. */
  {
    SzString *url;
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_FAULT_KIND", "net", 1);
    setenv("SCUZZ_FAULT_N", "1", 1);
    setenv("SCUZZ_FAULT_MODE", "drop", 1);
    sz_testrt_install();
    sz_testrt_net_stub("http://example.test/v1", "ok");
    url = sz_string_from_cstr("http://example.test/v1");
    r = sz_io_unsafe_run(sz_net_http_get(url));
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "dropped") != NULL);
    sz_error_free(r.error);
    sz_release(url);
    sz_testrt_reset();
    unsetenv("SCUZZ_FAULT_MODE");
    unsetenv("SCUZZ_FAULT_KIND");
    unsetenv("SCUZZ_FAULT_N");
    unsetenv("SCUZZ_TESTRT");
  }

  /* Fault injection: corrupt a Net stub body. */
  {
    SzString *url;
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_FAULT_KIND", "net", 1);
    setenv("SCUZZ_FAULT_N", "1", 1);
    setenv("SCUZZ_FAULT_MODE", "corrupt", 1);
    sz_testrt_install();
    sz_testrt_net_stub("http://example.test/v1", "ok");
    url = sz_string_from_cstr("http://example.test/v1");
    r = sz_io_unsafe_run(sz_net_http_get(url));
    assert(r.ok);
    assert(r.value);
    assert(strcmp(sz_string_cstr((SzString *)r.value), "ok!") == 0);
    sz_release(r.value);
    sz_release(url);
    sz_testrt_reset();
    unsetenv("SCUZZ_FAULT_MODE");
    unsetenv("SCUZZ_FAULT_KIND");
    unsetenv("SCUZZ_FAULT_N");
    unsetenv("SCUZZ_TESTRT");
  }

  /* Fault on Net fails before loopback mailbox offer. */
  {
    SzString *url;
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_FAULT_KIND", "net", 1);
    setenv("SCUZZ_FAULT_N", "1", 1);
    sz_testrt_install();
    url = sz_string_from_cstr("http://127.0.0.1:8080/ping");
    r = sz_io_unsafe_run(sz_net_http_get(url));
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "injected fault") != NULL);
    sz_error_free(r.error);
    sz_release(url);
    sz_testrt_reset();
    unsetenv("SCUZZ_FAULT_KIND");
    unsetenv("SCUZZ_FAULT_N");
    unsetenv("SCUZZ_TESTRT");
  }

  /* Fault injection: fail the Nth Queue op. */
  {
    SzQueue *q;
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_FAULT_KIND", "queue", 1);
    setenv("SCUZZ_FAULT_N", "1", 1);
    sz_testrt_install();
    q = sz_queue_make();
    r = sz_io_unsafe_run(sz_queue_offer_cstr(q, "x"));
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message), "Queue.offer") != NULL);
    sz_error_free(r.error);
    sz_queue_free(q);
    sz_testrt_reset();
    unsetenv("SCUZZ_FAULT_KIND");
    unsetenv("SCUZZ_FAULT_N");
    unsetenv("SCUZZ_TESTRT");
  }

  /* Deadlock oracle: all fibers parked with no timer pending. */
  {
    SzQueue *q = sz_queue_make();
    r = sz_io_unsafe_run(sz_queue_take(q));
    assert(!r.ok);
    assert(r.error &&
           strstr(sz_string_cstr(r.error->message),
                  "all fibers parked with no timer pending") != NULL);
    sz_error_free(r.error);
    sz_queue_free(q);
  }

  /* Leak oracle: heap growth across an idle snapshot fails. */
  {
    pid_t pid;
    setenv("SCUZZ_TESTRT", "1", 1);
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      void *p;
      sz_testrt_ui_idle_snapshot();
      p = sz_alloc(128);
      (void)p;
      sz_testrt_ui_idle_check();
      _exit(0);
    }
    assert(wait_aborted(pid));
    sz_testrt_ui_idle_snapshot();
    sz_testrt_ui_idle_check();
    unsetenv("SCUZZ_TESTRT");
  }

  /* Heap baseline: growth vs the session/process snapshot fails. */
  {
    pid_t pid;
    setenv("SCUZZ_TESTRT", "1", 1);
    sz_testrt_heap_baseline_snapshot();
    sz_testrt_heap_baseline_check();
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      void *p;
      sz_testrt_heap_baseline_snapshot();
      p = sz_alloc(128);
      (void)p;
      sz_testrt_heap_baseline_check();
      _exit(0);
    }
    assert(wait_aborted(pid));
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      void *p;
      sz_testrt_session_baseline_snapshot();
      p = sz_alloc(64);
      (void)p;
      sz_testrt_session_baseline_check();
      _exit(0);
    }
    assert(wait_aborted(pid));
    unsetenv("SCUZZ_TESTRT");
  }

  /* Session baseline: byte drift at equal block count is retained end-state,
     not a leak. Replacing a block with a larger one must not fail. */
  {
    SzString *a;
    setenv("SCUZZ_TESTRT", "1", 1);
    a = sz_string_from_cstr("a");
    sz_testrt_session_baseline_snapshot();
    sz_release(a);
    a = sz_string_from_cstr("abcdef");
    sz_testrt_session_baseline_check();
    sz_release(a);
    unsetenv("SCUZZ_TESTRT");
  }

  /* Acquire/release: leftover retain fails; double release fails. */
  {
    pid_t pid;
    SzString *s;
    setenv("SCUZZ_TESTRT", "1", 1);
    s = sz_string_from_cstr("pair");
    sz_testrt_heap_baseline_snapshot();
    sz_retain(s);
    sz_release(s);
    sz_testrt_heap_baseline_check();
    sz_release(s);
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      SzString *p = sz_string_from_cstr("retain");
      sz_testrt_heap_baseline_snapshot();
      sz_retain(p);
      sz_testrt_heap_baseline_check();
      _exit(0);
    }
    assert(wait_aborted(pid));
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      SzString *p = sz_string_from_cstr("twice");
      sz_release(p);
      sz_release(p);
      _exit(0);
    }
    assert(wait_aborted(pid));
    unsetenv("SCUZZ_TESTRT");
  }

  /* Finalizer-on-cancel: a skipped unstepped IO.ensure fails. */
  {
    pid_t pid;
    setenv("SCUZZ_TESTRT", "1", 1);
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      SzLangResource *lr;
      sz_testrt_plant_skip_unstepped_ensure();
      lr = lang_make_tok();
      (void)sz_io_unsafe_run(fm_drop(
          fork_drop(sz_lang_resource_use(lr, lang_use_step, NULL)),
          fiber_interrupt_direct, NULL));
      _exit(0);
    }
    assert(wait_aborted(pid));
    unsetenv("SCUZZ_TESTRT");
  }

  /* No parked fibers at quiescence: a leftover forked waiter fails. */
  {
    pid_t pid;
    setenv("SCUZZ_TESTRT", "1", 1);
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      SzQueue *q = sz_queue_make();
      sz_testrt_plant_skip_orphan_cancel();
      (void)sz_io_unsafe_run(
          fm_drop(fork_drop(sz_queue_take(q)), after_fork_ignore, NULL));
      _exit(0);
    }
    assert(wait_aborted(pid));
    unsetenv("SCUZZ_TESTRT");
  }

  /* Compact drive tokens: ctor / list / fields. */
  {
    char inner[192];
    char f0[192];
    char f1[192];
    assert(sz_drive_uncons("None", "None", inner, (int)sizeof inner));
    assert(inner[0] == 0);
    assert(sz_drive_uncons("Some(3)", "Some", inner, (int)sizeof inner));
    assert(strcmp(inner, "3") == 0);
    assert(sz_drive_uncons("Point(3,5)", "Point", inner, (int)sizeof inner));
    assert(strcmp(inner, "3,5") == 0);
    assert(sz_drive_nfields(inner) == 2);
    assert(sz_drive_field(inner, 0, f0, (int)sizeof f0));
    assert(sz_drive_field(inner, 1, f1, (int)sizeof f1));
    assert(strcmp(f0, "3") == 0);
    assert(strcmp(f1, "5") == 0);
    assert(sz_drive_uncons("Some(Point(1,2))", "Some", inner, (int)sizeof inner));
    assert(strcmp(inner, "Point(1,2)") == 0);
    assert(sz_drive_uncons_list("[1,2,3]", inner, (int)sizeof inner));
    assert(strcmp(inner, "1,2,3") == 0);
    assert(sz_drive_nfields(inner) == 3);
    assert(sz_drive_uncons_list("[]", inner, (int)sizeof inner));
    assert(inner[0] == 0);
    assert(sz_drive_nfields("") == 0);
    assert(sz_drive_parse_int("42") == 42);
    assert(sz_drive_parse_bool("true") == 1);
    assert(sz_drive_parse_bool("false") == 0);
    assert(!sz_drive_uncons("Some(3)", "None", inner, (int)sizeof inner));
  }

  /* Property.classify dump. */
  {
    char path[] = "/tmp/scuzz-classify-test.txt";
    FILE *f;
    char line[64];
    SzString *nm;
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_CLASSIFY_DUMP", path, 1);
    nm = sz_string_from_cstr("square");
    sz_property_classify(nm, 1);
    sz_property_classify(nm, 0);
    sz_release(nm);
    sz_property_classify_flush();
    f = fopen(path, "r");
    assert(f);
    assert(fgets(line, (int)sizeof line, f));
    fclose(f);
    assert(strstr(line, "square 1 1") != NULL);
    unsetenv("SCUZZ_CLASSIFY_DUMP");
    unsetenv("SCUZZ_TESTRT");
    unlink(path);
  }

  /* Unclaimed State field variation dump. */
  {
    char path[] = "/tmp/scuzz-state-varied-test.txt";
    FILE *f;
    char line[64];
    int saw_drive = 0;
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_STATE_VARIED_DUMP", path, 1);
    sz_property_session_reset();
    sz_timeline_set_drive("drive a");
    sz_property_session_step();
    sz_timeline_set_drive("drive b");
    sz_property_session_step();
    sz_property_session_end();
    f = fopen(path, "r");
    assert(f);
    while (fgets(line, (int)sizeof line, f)) {
      if (strstr(line, "drive"))
        saw_drive = 1;
    }
    fclose(f);
    assert(saw_drive);
    unsetenv("SCUZZ_STATE_VARIED_DUMP");
    unsetenv("SCUZZ_TESTRT");
    sz_property_session_reset();
    unlink(path);
  }

  /* Session always / eventually thunks. */
  {
    pid_t pid;
    SzString *nm;
    setenv("SCUZZ_TESTRT", "1", 1);
    sz_property_session_reset();
    nm = sz_string_from_cstr("visible");
    sz_property_always_register(nm, (void *)session_ok);
    sz_release(nm);
    assert(sz_property_session_armed());
    sz_property_session_step();
    sz_property_session_end();
    sz_property_session_reset();
    assert(!sz_property_session_armed());
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      setenv("SCUZZ_TESTRT", "1", 1);
      nm = sz_string_from_cstr("visible");
      sz_property_always_register(nm, (void *)session_fail);
      sz_release(nm);
      sz_property_session_step();
      sz_property_session_end();
      _exit(0);
    }
    assert(wait_aborted(pid));
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      setenv("SCUZZ_TESTRT", "1", 1);
      nm = sz_string_from_cstr("shown");
      sz_property_eventually_register(nm, (void *)session_fail);
      sz_release(nm);
      sz_property_session_step();
      sz_property_session_end();
      _exit(0);
    }
    assert(wait_aborted(pid));
    nm = sz_string_from_cstr("shown");
    sz_property_eventually_register(nm, (void *)session_ok);
    sz_release(nm);
    sz_property_session_step();
    sz_property_session_end();
    sz_property_session_reset();
    unsetenv("SCUZZ_TESTRT");
  }

  /* Response claims: trigger latch, per-run end check, campaign record. */
  {
    pid_t pid;
    SzString *nm;
    setenv("SCUZZ_TESTRT", "1", 1);
    /* Trigger never fires: vacuous pass. */
    sz_property_session_reset();
    nm = sz_string_from_cstr("response:button:ghost:text:x");
    sz_property_response_register(nm, (void *)session_fail,
                                  (void *)session_fail);
    sz_release(nm);
    assert(sz_property_session_armed());
    sz_property_session_step();
    sz_property_session_end();
    sz_property_session_reset();
    /* Trigger fires, response holds: clean end, name recorded. */
    {
      char path[] = "/tmp/scuzz-response-test.txt";
      FILE *f;
      char buf[256];
      int found = 0;
      setenv("SCUZZ_SOMETIMES_DUMP", path, 1);
      nm = sz_string_from_cstr("response:button:+1:text:count = 1");
      sz_property_response_register(nm, (void *)session_ok,
                                    (void *)session_ok);
      sz_release(nm);
      sz_property_session_step();
      sz_property_session_end();
      sz_property_sometimes_flush();
      sz_property_session_reset();
      f = fopen(path, "r");
      assert(f);
      while (fgets(buf, (int)sizeof buf, f)) {
        if (strstr(buf, "response:button:+1:text:count = 1"))
          found = 1;
      }
      fclose(f);
      assert(found);
      unsetenv("SCUZZ_SOMETIMES_DUMP");
      unlink(path);
    }
    /* Trigger fires, response never holds: end aborts. */
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      setenv("SCUZZ_TESTRT", "1", 1);
      sz_property_session_reset();
      nm = sz_string_from_cstr("response:button:+1:text:changed");
      sz_property_response_register(nm, (void *)session_ok,
                                    (void *)session_fail);
      sz_release(nm);
      sz_property_session_step();
      sz_property_session_end();
      _exit(0);
    }
    assert(wait_aborted(pid));
    /* Last-hit stash query. */
    nm = sz_string_from_cstr("+1");
    assert(sz_property_last_hit_has(nm) == 0);
    sz_property_stash_last_hit("button:+1");
    assert(sz_property_last_hit_has(nm) == 1);
    sz_release(nm);
    nm = sz_string_from_cstr("reset");
    assert(sz_property_last_hit_has(nm) == 0);
    sz_release(nm);
    sz_property_stash_last_hit(NULL);
    unsetenv("SCUZZ_TESTRT");
  }

  /* Timeline => Verdict session claims judge at session_end. */
  {
    pid_t pid;
    int fds[2];
    char child_err[4096];
    setenv("SCUZZ_TESTRT", "1", 1);
    sz_property_session_reset();
    sz_verify_register("countOk", verify_len_ok);
    sz_verify_register("kitCombo", verify_kit_combo);
    assert(sz_property_session_armed());
    sz_property_session_step();
    sz_property_session_end();
    sz_property_session_reset();
    /* Invalid verdict: report carries claim name, state index, and why. */
    assert(pipe(fds) == 0);
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      dup2(fds[1], STDERR_FILENO);
      close(fds[0]);
      close(fds[1]);
      setenv("SCUZZ_TESTRT", "1", 1);
      sz_property_session_reset();
      sz_verify_register("countOk", verify_never);
      sz_property_session_step();
      sz_property_session_end();
      _exit(0);
    }
    close(fds[1]);
    read_fd_all(fds[0], child_err, sizeof child_err);
    close(fds[0]);
    assert(wait_aborted(pid));
    assert(strstr(child_err,
                  "verify failed: countOk at state 0: never holds") != NULL);
    /* Verdict.every surfaces the first failing state index. */
    assert(pipe(fds) == 0);
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      dup2(fds[1], STDERR_FILENO);
      close(fds[0]);
      close(fds[1]);
      setenv("SCUZZ_TESTRT", "1", 1);
      sz_property_session_reset();
      sz_verify_register("secondFails", verify_every_second);
      sz_property_session_step();
      sz_property_session_step();
      sz_property_session_end();
      _exit(0);
    }
    close(fds[1]);
    read_fd_all(fds[0], child_err, sizeof child_err);
    close(fds[0]);
    assert(wait_aborted(pid));
    assert(strstr(child_err, "verify failed: secondFails at state 1: "
                             "predicate false at this state") != NULL);
    unsetenv("SCUZZ_TESTRT");
  }

  /* Timeline dump header carries the schema version. */
  {
    const char *path = "/tmp/scuzz_test_io_timeline.dump";
    char *dump;
    remove(path);
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_TIMELINE_DUMP", path, 1);
    sz_property_session_reset();
    sz_property_session_step();
    sz_property_session_step();
    sz_property_session_end();
    sz_property_session_reset();
    dump = slurp_path(path);
    assert(dump);
    assert(strncmp(dump, "# timeline v=2 n=2\n", 19) == 0);
    assert(strstr(dump, "effects:\n") != NULL);
    assert(strstr(dump, "fibers:\n") != NULL);
    assert(strstr(dump, "fault:\n") != NULL);
    free(dump);
    unsetenv("SCUZZ_TIMELINE_DUMP");
    unsetenv("SCUZZ_TESTRT");
    remove(path);
  }

  /* v1 timeline dump loader: round-trip through tl_dump_file. */
  {
    const char *path = "/tmp/scuzz_test_io_tl_load.dump";
    void *tl;
    SzString *needle;
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_TIMELINE_DUMP", path, 1);
    sz_property_session_reset();
    sz_property_stash_a11y("button:+1");
    sz_property_stash_last_hit("button:+1");
    sz_timeline_set_drive("drive plusN 0");
    sz_property_session_step();
    sz_property_stash_a11y("text:count = 1");
    sz_property_session_step();
    sz_property_session_end();
    sz_property_session_reset();
    unsetenv("SCUZZ_TIMELINE_DUMP");
    unsetenv("SCUZZ_TESTRT");
    tl = sz_timeline_load(path);
    assert(tl);
    assert(sz_timeline_len(tl) == 2);
    needle = sz_string_from_cstr("button:+1");
    assert(sz_timeline_a11y_has(tl, 0, needle) == 1);
    assert(sz_timeline_last_hit_has(tl, 0, needle) == 1);
    sz_release(needle);
    needle = sz_string_from_cstr("text:count = 1");
    assert(sz_timeline_a11y_has(tl, 1, needle) == 1);
    assert(sz_timeline_a11y_has(tl, 0, needle) == 0);
    sz_release(needle);
    sz_timeline_free(tl);
    remove(path);
  }

  /* Effect log, fiber census, and fault context round-trip on a v2 dump. */
  {
    const char *path = "/tmp/scuzz_test_io_tl_obs.dump";
    void *tl;
    SzString *needle;
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_TIMELINE_DUMP", path, 1);
    sz_testrt_install();
    sz_property_session_reset();
    sz_io_unsafe_run(sz_clock_monotonic());
    sz_timeline_set_drive("drive obs");
    sz_property_session_step();
    sz_property_session_end();
    sz_property_session_reset();
    unsetenv("SCUZZ_TIMELINE_DUMP");
    unsetenv("SCUZZ_TESTRT");
    sz_testrt_reset();
    tl = sz_timeline_load(path);
    assert(tl);
    assert(sz_timeline_len(tl) == 1);
    needle = sz_string_from_cstr("Clock.monotonic");
    assert(sz_timeline_effect_has(tl, 0, needle) == 1);
    sz_release(needle);
    needle = sz_string_from_cstr("drive obs");
    assert(sz_timeline_drive_has(tl, 0, needle) == 1);
    sz_release(needle);
    needle = sz_string_from_cstr("kind=none");
    assert(sz_timeline_fault_kind_has(tl, 0, needle) == 1);
    sz_release(needle);
    assert(sz_timeline_effect_count(tl, 0) >= 1);
    assert(sz_timeline_fiber_live(tl, 0) >= 0);
    assert(sz_timeline_fiber_ready(tl, 0) >= 0);
    assert(sz_timeline_fiber_parked(tl, 0) >= 0);
    assert(sz_timeline_fiber_done(tl, 0) >= 0);
    assert(sz_timeline_fault_n(tl, 0) == 0);
    assert(sz_timeline_checkpoint(tl, 0) == 1);
    sz_timeline_free(tl);
    remove(path);
  }

  /* Checkpoint flags persist. Compact drops non-checkpoint states. Replay
   * restores the nearest checkpoint. */
  {
    const char *path = "/tmp/scuzz_test_io_tl_ckpt.dump";
    void *tl;
    SzString *needle;
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_TIMELINE_DUMP", path, 1);
    setenv("SCUZZ_TIMELINE_CHECKPOINT", "2", 1);
    sz_property_session_reset();
    sz_property_stash_a11y("s0");
    sz_property_session_step();
    sz_property_stash_a11y("s1");
    sz_property_session_step();
    sz_property_stash_a11y("s2");
    sz_property_session_step();
    sz_property_stash_a11y("s3");
    sz_property_session_step();
    sz_timeline_replay_from(1);
    assert(sz_timeline_replaying());
    needle = sz_string_from_cstr("s0");
    assert(sz_property_a11y_has(needle) == 1);
    sz_release(needle);
    sz_property_session_end();
    sz_property_session_reset();
    unsetenv("SCUZZ_TIMELINE_DUMP");
    unsetenv("SCUZZ_TIMELINE_CHECKPOINT");
    unsetenv("SCUZZ_TESTRT");
    tl = sz_timeline_load(path);
    assert(tl);
    assert(sz_timeline_len(tl) == 4);
    assert(sz_timeline_checkpoint(tl, 0) == 1);
    assert(sz_timeline_checkpoint(tl, 1) == 0);
    assert(sz_timeline_checkpoint(tl, 2) == 1);
    assert(sz_timeline_checkpoint(tl, 3) == 0);
    assert(sz_timeline_nearest_checkpoint(tl, 1) == 0);
    assert(sz_timeline_nearest_checkpoint(tl, 2) == 2);
    needle = sz_string_from_cstr("s1");
    assert(sz_timeline_a11y_has(tl, 1, needle) == 1);
    sz_release(needle);
    sz_timeline_compact_loaded(tl);
    assert(sz_timeline_len(tl) == 2);
    assert(sz_timeline_checkpoint(tl, 0) == 1);
    assert(sz_timeline_checkpoint(tl, 1) == 1);
    needle = sz_string_from_cstr("s0");
    assert(sz_timeline_a11y_has(tl, 0, needle) == 1);
    sz_release(needle);
    needle = sz_string_from_cstr("s2");
    assert(sz_timeline_a11y_has(tl, 1, needle) == 1);
    sz_release(needle);
    sz_timeline_free(tl);
    remove(path);
  }

  /* Compact-before-dump keeps only checkpoint states. */
  {
    const char *path = "/tmp/scuzz_test_io_tl_compact.dump";
    void *tl;
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_TIMELINE_DUMP", path, 1);
    setenv("SCUZZ_TIMELINE_CHECKPOINT", "2", 1);
    setenv("SCUZZ_TIMELINE_COMPACT", "1", 1);
    sz_property_session_reset();
    sz_property_stash_a11y("c0");
    sz_property_session_step();
    sz_property_stash_a11y("c1");
    sz_property_session_step();
    sz_property_stash_a11y("c2");
    sz_property_session_step();
    sz_property_session_end();
    sz_property_session_reset();
    unsetenv("SCUZZ_TIMELINE_DUMP");
    unsetenv("SCUZZ_TIMELINE_CHECKPOINT");
    unsetenv("SCUZZ_TIMELINE_COMPACT");
    unsetenv("SCUZZ_TESTRT");
    tl = sz_timeline_load(path);
    assert(tl);
    assert(sz_timeline_len(tl) == 2);
    assert(sz_timeline_checkpoint(tl, 0) == 1);
    assert(sz_timeline_checkpoint(tl, 1) == 1);
    sz_timeline_free(tl);
    remove(path);
  }

  /* Loader parses signal lines; bad headers are rejected. */
  {
    const char *path = "/tmp/scuzz_test_io_tl_sig.dump";
    void *tl;
    SzString *needle;
    write_text(path, "# timeline v=1 n=1\n--- 0\nlast_hit:\n\ndrive:\ndrive "
                     "x\nsignals:\nint[0] = 7\nlist[1] = [\"a\", "
                     "\"b\"]\na11y:\nbutton:+1\n");
    tl = sz_timeline_load(path);
    assert(tl);
    assert(sz_timeline_len(tl) == 1);
    assert(sz_timeline_signal_int(tl, 0, 0) == 7);
    assert(sz_timeline_signal_list_len(tl, 0, 1) == 2);
    needle = sz_string_from_cstr("button:+1");
    assert(sz_timeline_a11y_has(tl, 0, needle) == 1);
    sz_release(needle);
    sz_timeline_free(tl);
    write_text(path, "# timeline v=3 n=0\n");
    assert(sz_timeline_load(path) == NULL);
    write_text(path, "nonsense\n");
    assert(sz_timeline_load(path) == NULL);
    assert(sz_timeline_load("/tmp/scuzz_no_such_timeline_dump.txt") == NULL);
    remove(path);
  }

  /* Relation claims judge a pair of dumps in judge mode. */
  {
    const char *a = "/tmp/scuzz_test_io_rel_a.dump";
    const char *b = "/tmp/scuzz_test_io_rel_b.dump";
    write_text(a, "# timeline v=1 n=1\n--- 0\nlast_hit:\n\ndrive:\ndrive "
                  "x\nsignals:\nint[0] = 1\na11y:\n");
    write_text(b, "# timeline v=1 n=1\n--- 0\nlast_hit:\n\ndrive:\ndrive "
                  "x\nsignals:\nint[0] = 1\na11y:\n");
    sz_property_session_reset();
    sz_verify_register_rel("sameFinal", rel_final_int_eq);
    assert(sz_judge_rel_main(
               "/tmp/scuzz_test_io_rel_a.dump,/tmp/scuzz_test_io_rel_b.dump") ==
           0);
    write_text(b, "# timeline v=1 n=1\n--- 0\nlast_hit:\n\ndrive:\ndrive "
                  "x\nsignals:\nint[0] = 2\na11y:\n");
    assert(sz_judge_rel_main(
               "/tmp/scuzz_test_io_rel_a.dump,/tmp/scuzz_test_io_rel_b.dump") ==
           1);
    /* No relation claims registered: note, clean exit. */
    sz_property_session_reset();
    assert(sz_judge_rel_main(
               "/tmp/scuzz_test_io_rel_a.dump,/tmp/scuzz_test_io_rel_b.dump") ==
           0);
    /* Missing dump is a judge error. */
    assert(sz_judge_rel_main("/tmp/scuzz_test_io_rel_a.dump,/tmp/"
                             "scuzz_no_such_b.dump") == 2);
    remove(a);
    remove(b);
  }

  /* Optional effects/fibers/fault sections. Handwritten v=1 without them still loads. */
  {
    const char *path = "/tmp/scuzz_test_io_tl_obs.dump";
    void *tl;
    SzString *needle;
    write_text(path, "# timeline v=1 n=1\n--- 0\nlast_hit:\n\ndrive:\ndrive "
                     "x\nsignals:\nint[0] = 1\na11y:\nbutton:+1\neffects:\n"
                     "fs.write n=7\nfibers:\nready=1 parked=2\nfault:\n"
                     "kind=fs n=1 mode=fail\n");
    tl = sz_timeline_load(path);
    assert(tl);
    assert(sz_timeline_len(tl) == 1);
    needle = sz_string_from_cstr("button:+1");
    assert(sz_timeline_a11y_has(tl, 0, needle) == 1);
    sz_release(needle);
    needle = sz_string_from_cstr("fs.write");
    assert(sz_timeline_effect_has(tl, 0, needle) == 1);
    assert(sz_timeline_a11y_has(tl, 0, needle) == 0);
    sz_release(needle);
    assert(sz_timeline_fiber_ready(tl, 0) == 1);
    assert(sz_timeline_fiber_parked(tl, 0) == 2);
    needle = sz_string_from_cstr("kind=fs");
    assert(sz_timeline_fault_kind_has(tl, 0, needle) == 1);
    sz_release(needle);
    sz_timeline_free(tl);
    write_text(path, "# timeline v=1 n=1\n--- 0\nlast_hit:\n\ndrive:\ndrive "
                     "x\nsignals:\nint[0] = 1\na11y:\nbutton:+1\n");
    tl = sz_timeline_load(path);
    assert(tl);
    needle = sz_string_from_cstr("fs.write");
    assert(sz_timeline_effect_has(tl, 0, needle) == 0);
    sz_release(needle);
    assert(sz_timeline_fiber_ready(tl, 0) == 0);
    assert(sz_timeline_fiber_parked(tl, 0) == 0);
    sz_timeline_free(tl);
    remove(path);
  }

  /* IO-only session_end pushes a terminal state when the effect log is pending. */
  {
    const char *path = "/tmp/scuzz_test_io_tl_pending.dump";
    void *tl;
    SzString *needle;
    setenv("SCUZZ_TESTRT", "1", 1);
    setenv("SCUZZ_TIMELINE_DUMP", path, 1);
    sz_property_session_reset();
    sz_effect_log("fs.write n=7");
    sz_property_session_end();
    sz_property_session_reset();
    unsetenv("SCUZZ_TIMELINE_DUMP");
    unsetenv("SCUZZ_TESTRT");
    tl = sz_timeline_load(path);
    assert(tl);
    assert(sz_timeline_len(tl) == 1);
    needle = sz_string_from_cstr("fs.write n=7");
    assert(sz_timeline_effect_has(tl, 0, needle) == 1);
    sz_release(needle);
    needle = sz_string_from_cstr("kind=none");
    assert(sz_timeline_fault_kind_has(tl, 0, needle) == 1);
    sz_release(needle);
    assert(sz_timeline_fiber_ready(tl, 0) == 0);
    assert(sz_timeline_fiber_parked(tl, 0) == 0);
    sz_timeline_free(tl);
    remove(path);
  }

  puts("runtime io tests ok");
  return 0;
}
