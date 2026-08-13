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
#include <unistd.h>

static int delay_calls = 0;
static void *delay_inc(void *env) {
  (void)env;
  delay_calls++;
  return (void *)(intptr_t)42;
}

static SzIo *cont_println(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_io_println_cstr("after-flatmap");
}

static int released = 0;
static void *acquire_token(void *env) {
  (void)env;
  return (void *)(intptr_t)7;
}
static void release_token(void *value, void *env) {
  (void)env;
  assert((intptr_t)value == 7);
  released = 1;
}
static SzIo *use_token(void *acquired, void *env) {
  (void)env;
  assert((intptr_t)acquired == 7);
  return sz_io_println_cstr("resource-used");
}

static SzIo *use_token_fail(void *acquired, void *env) {
  (void)env;
  assert((intptr_t)acquired == 7);
  return sz_io_fail_cstr("use-failed");
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

static SzIo *stream_bang(void *v, void *env) {
  (void)env;
  SzString *s = (SzString *)v;
  return sz_io_pure(sz_string_concat(s, sz_string_from_cstr("!")));
}

static SzIo *serve_path_ok(void *path, void *env) {
  (void)env;
  return sz_io_pure(
      sz_string_concat(sz_string_from_cstr("ok:"), (SzString *)path));
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
    usleep(10000);
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

static int live_connect(int port) {
  int fd = -1;
  int i;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  for (i = 0; i < 50; i++) {
    usleep(10000);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      continue;
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0)
      return fd;
    close(fd);
    fd = -1;
  }
  return -1;
}

static int live_http_get(int fd, const char *path, char *buf, size_t cap) {
  char req[128];
  ssize_t n;
  size_t total = 0;
  snprintf(req, sizeof req,
           "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
           path);
  if (write(fd, req, strlen(req)) < 0)
    return 0;
  while (total + 1 < cap &&
         (n = read(fd, buf + total, cap - 1 - total)) > 0)
    total += (size_t)n;
  buf[total] = '\0';
  return 1;
}

static void *live_get_two(void *arg) {
  int port = *(int *)arg;
  int fd;
  size_t n1;
  static char buf[2048];
  memset(buf, 0, sizeof buf);
  fd = live_connect(port);
  if (fd < 0)
    return NULL;
  if (!live_http_get(fd, "/a", buf, sizeof buf)) {
    close(fd);
    return NULL;
  }
  close(fd);
  n1 = strlen(buf);
  if (n1 + 2 >= sizeof buf)
    return buf;
  buf[n1++] = '|';
  fd = live_connect(port);
  if (fd < 0)
    return buf;
  if (!live_http_get(fd, "/b", buf + n1, sizeof buf - n1)) {
    close(fd);
    return buf;
  }
  close(fd);
  return buf;
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
  usleep(40000);
  g_peer_flag = 1;
  if (write(fd, "x", 1) < 0)
    return NULL;
  return NULL;
}

static void *live_get_client_late(void *arg) {
  usleep(50000);
  g_peer_flag = 1;
  return live_get_client(arg);
}

static SzIo *after_sleep_http(void *value, void *env) {
  (void)value;
  return sz_net_http_get((SzString *)env);
}

static void *stdin_late_write(void *arg) {
  int fd = *(int *)arg;
  usleep(40000);
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

  /* Resource bracket */
  released = 0;
  SzResource *res = sz_resource_make(acquire_token, release_token, NULL);
  SzIo *rio = sz_resource_use(res, use_token, NULL);
  r = sz_io_unsafe_run(rio);
  assert(r.ok);
  assert(released == 1);
  sz_resource_free(res);

  /* Resource releases on use failure */
  released = 0;
  res = sz_resource_make(acquire_token, release_token, NULL);
  r = sz_io_unsafe_run(sz_resource_use(res, use_token_fail, NULL));
  assert(!r.ok);
  assert(released == 1);
  sz_error_free(r.error);
  sz_resource_free(res);

  /* Language Resource.make / use (IO acquire + IO release) */
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

  /* Stream — emit / eval / concat / evalMap / compileToList / drain */
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
      usleep(20000);
      r = sz_io_unsafe_run(sz_sys_alive(pid));
    }
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 0);
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

  /* Live Net.serve: same listen socket, two sequential GETs. */
  {
    pthread_t th;
    int port = 18474;
    void *ret = NULL;
    pthread_create(&th, NULL, live_get_two, &port);
    r = sz_io_unsafe_run(sz_net_serve_n(port, 2, serve_path_ok, NULL));
    pthread_join(th, &ret);
    assert(r.ok);
    assert(ret && strstr((char *)ret, "ok:/a") != NULL);
    assert(ret && strstr((char *)ret, "ok:/b") != NULL);
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
