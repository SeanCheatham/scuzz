#define _POSIX_C_SOURCE 200112L
#include "scuzz_ui.h"
#include "sk_capi.h"

#include <assert.h>
#include <stdatomic.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int sz_view_paint(SzView *root, SkCanvas *canvas, int width, int height,
                  const SzTheme *theme);
int sz_ui_session_live_inject(SzUiSession *session, const SzInputEvent *event);

static int files_equal(const char *a, const char *b) {
  FILE *fa = fopen(a, "rb");
  FILE *fb = fopen(b, "rb");
  unsigned char ba[4096], bb[4096];
  size_t na, nb;
  if (!fa || !fb) {
    if (fa)
      fclose(fa);
    if (fb)
      fclose(fb);
    return 0;
  }
  for (;;) {
    na = fread(ba, 1, sizeof ba, fa);
    nb = fread(bb, 1, sizeof bb, fb);
    if (na != nb || memcmp(ba, bb, na) != 0) {
      fclose(fa);
      fclose(fb);
      return 0;
    }
    if (na == 0)
      break;
  }
  fclose(fa);
  fclose(fb);
  return 1;
}


static void counter_tap(SzView *self, void *env) {
  SzSignalInt *count = (SzSignalInt *)env;
  (void)self;
  sz_signal_int_set(count, sz_signal_int_get(count) + 1);
}

static void test_session_snapshot(void) {
  SzUiConfig cfg;
  SzSignalInt *count;
  SzView *root, *btn, *view;
  SzUiSession *session;
  SzInputEvent tap;
  const char *path_a = "/tmp/scuzz_ui_a.png";
  const char *path_b = "/tmp/scuzz_ui_b.png";
  const char *path_tap = "/tmp/scuzz_ui_tap.png";
  uint8_t *bytes = NULL;
  size_t len = 0;

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;

  count = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_text_signal_int(count, "n="));
  btn = sz_view_button("+", counter_tap, count);
  sz_view_add_child(root, btn);
  session = sz_ui_mount(&cfg, root);
  assert(session);
  assert(sz_ui_session_kind(session) == SZ_UI_RUNTIME_HEADLESS);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_snapshot_png_sync(session, path_a));
  assert(sz_ui_snapshot_png_bytes(session, &bytes, &len));
  assert(bytes && len > 8 && bytes[0] == 137);
  free(bytes);

  assert(sz_ui_snapshot_png_sync(session, path_b));
  assert(files_equal(path_a, path_b));

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(btn).x + 4.f;
  tap.y = sz_view_frame(btn).y + 4.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_snapshot_png_sync(session, path_tap));
  assert(!files_equal(path_a, path_tap));

  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_int_free(count);

  cfg.kind = SZ_UI_RUNTIME_DESKTOP;
  cfg.title = "test";
  view = sz_view_text("Win");
  session = sz_ui_mount(&cfg, view);
  assert(session);
  assert(sz_ui_session_kind(session) == SZ_UI_RUNTIME_DESKTOP);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_snapshot_png_sync(session, path_b));
  sz_ui_unmount(session);
  sz_view_free(view);

  cfg.kind = SZ_UI_RUNTIME_MOBILE;
  cfg.title = "mobile";
  view = sz_view_text("Mob");
  session = sz_ui_mount(&cfg, view);
  assert(session);
  assert(sz_ui_session_kind(session) == SZ_UI_RUNTIME_MOBILE);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_snapshot_png_sync(session, path_b));
  sz_ui_unmount(session);
  sz_view_free(view);

  remove(path_a);
  remove(path_b);
  remove(path_tap);
}

static void test_signals_layout_hit(void) {
  SzUiConfig cfg;
  SzSignalInt *count;
  SzView *root, *btn;
  SzUiSession *session;
  SzView *hit;
  SzInputEvent tap;
  const SzTheme *theme = sz_theme_default();

  count = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_text_signal_int(count, "n="));
  btn = sz_view_button("+", counter_tap, count);
  sz_view_add_child(root, btn);

  sz_view_layout(root, 200.f, 100.f, theme);
  assert(sz_view_frame(root).w == 200.f);
  assert(sz_view_frame(btn).h == theme->control_h);

  hit = sz_view_hit_test(root, sz_view_frame(btn).x + 4.f,
                         sz_view_frame(btn).y + 4.f);
  assert(hit == btn);
  assert(sz_view_hit_test(root, 199.f, 99.f) == NULL ||
         sz_view_kind(sz_view_hit_test(root, 199.f, 99.f)) != SZ_VIEW_BUTTON);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  assert(sz_ui_pump_sync(session));

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(btn).x + 8.f;
  tap.y = sz_view_frame(btn).y + 8.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(count) == 1);
  assert(sz_ui_pump_sync(session));

  /* Bridge: post from "IO", apply on pump. */
  sz_ui_bridge_post_int(session, count, 42);
  assert(sz_signal_int_get(count) == 1);
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 42);

  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_int_free(count);
}

static void test_replace_root_keeps_signals(void) {
  SzUiConfig cfg;
  SzSignalInt *count;
  SzView *root1, *root2, *btn1, *btn2;
  SzUiSession *session;
  SzInputEvent tap;
  SzString *dump1, *dump2, *a11y;
  const SzTheme *theme = sz_theme_default();

  count = sz_signal_int(0);
  root1 = sz_view_column();
  sz_view_add_child(root1, sz_view_text_signal_int(count, "n="));
  btn1 = sz_view_button("+", counter_tap, count);
  sz_view_add_child(root1, btn1);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root1);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(btn1).x + 8.f;
  tap.y = sz_view_frame(btn1).y + 8.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(count) == 1);
  dump1 = sz_signal_dump();

  root2 = sz_view_column();
  sz_view_add_child(root2, sz_view_text_signal_int(count, "v="));
  btn2 = sz_view_button("+", counter_tap, count);
  sz_view_add_child(root2, btn2);
  assert(sz_ui_session_replace_root(session, root2));
  assert(sz_ui_session_root(session) == root2);
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 1);
  dump2 = sz_signal_dump();
  assert(strcmp(sz_string_cstr(dump1), sz_string_cstr(dump2)) == 0);
  a11y = sz_view_a11y_dump(root2);
  assert(strstr(sz_string_cstr(a11y), "text:v=") != NULL);
  sz_string_free(a11y);
  sz_string_free(dump1);
  sz_string_free(dump2);

  sz_view_layout(root2, 200.f, 100.f, theme);
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(btn2).x + 8.f;
  tap.y = sz_view_frame(btn2).y + 8.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(count) == 2);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
}

typedef struct {
  SzSignalInt *count;
  const char *path;
  SzView *btn;
} WatchRebuildEnv;

static void write_stamp(const char *path, const char *contents) {
  FILE *f = fopen(path, "w");
  assert(f);
  fputs(contents, f);
  fclose(f);
}

static void copy_file_test(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  FILE *out = fopen(dst, "wb");
  char buf[4096];
  size_t n;
  assert(in && out);
  while ((n = fread(buf, 1, sizeof buf, in)) > 0)
    assert(fwrite(buf, 1, n, out) == n);
  fclose(in);
  fclose(out);
}

static SzView *init_code_factory(void *env) {
  SzSignalInt *count = (SzSignalInt *)env;
  SzView *root = sz_view_column();
  sz_view_add_child(root, sz_view_text("init"));
  sz_view_add_child(root, sz_view_text_signal_int(count, "n="));
  return root;
}

static SzView *watch_rebuild(void *env) {
  WatchRebuildEnv *e = (WatchRebuildEnv *)env;
  char prefix[64] = "n=";
  FILE *f;
  SzView *root, *btn;
  size_t n;
  f = fopen(e->path, "r");
  if (f) {
    if (fgets(prefix, (int)sizeof(prefix), f)) {
      n = strlen(prefix);
      while (n > 0 && (prefix[n - 1] == '\n' || prefix[n - 1] == '\r'))
        prefix[--n] = '\0';
    }
    fclose(f);
  }
  root = sz_view_column();
  sz_view_add_child(root, sz_view_text_signal_int(e->count, prefix));
  btn = sz_view_button("+", counter_tap, e->count);
  sz_view_add_child(root, btn);
  e->btn = btn;
  return root;
}

static void test_watch_rebuild_keeps_signals(void) {
  SzUiConfig cfg;
  WatchRebuildEnv *env;
  SzView *root;
  SzUiSession *session;
  SzInputEvent tap;
  SzString *dump1, *dump2, *a11y;
  SzView *same;
  const char *stamp = "/tmp/scuzz_ui_reload.stamp";

  write_stamp(stamp, "n=");
  env = (WatchRebuildEnv *)sz_alloc(sizeof(WatchRebuildEnv));
  env->count = sz_signal_int(0);
  env->path = stamp;
  env->btn = NULL;
  root = watch_rebuild(env);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  sz_ui_session_set_rebuild(session, watch_rebuild, env);
  assert(sz_ui_session_watch(session, stamp));
  assert(sz_ui_pump_sync(session));
  same = sz_ui_session_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_root(session) == same);

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(env->btn).x + 8.f;
  tap.y = sz_view_frame(env->btn).y + 8.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(env->count) == 1);
  dump1 = sz_signal_dump();

  write_stamp(stamp, "v=");
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_root(session) != same);
  assert(sz_signal_int_get(env->count) == 1);
  dump2 = sz_signal_dump();
  assert(strcmp(sz_string_cstr(dump1), sz_string_cstr(dump2)) == 0);
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:v=") != NULL);
  sz_string_free(a11y);
  sz_string_free(dump1);
  sz_string_free(dump2);

  sz_view_layout(sz_ui_session_root(session), 200.f, 100.f, sz_theme_default());
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(env->btn).x + 8.f;
  tap.y = sz_view_frame(env->btn).y + 8.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(env->count) == 2);

  sz_ui_unmount(session);
  sz_signal_int_free(env->count);
  sz_free(env);
  remove(stamp);
}

static SzView *run_rebuild_factory(void *env) {
  SzSignalInt *count = (SzSignalInt *)env;
  SzView *root = sz_view_column();
  sz_view_add_child(root, sz_view_text_signal_int(count, "n="));
  return root;
}

static void test_ui_run_rebuild(void) {
  SzSignalInt *count = sz_signal_int(7);
  SzIoResult r = sz_io_unsafe_run(sz_ui_run_rebuild(run_rebuild_factory, count));
  assert(r.ok);
  assert(sz_signal_int_get(count) == 7);
  sz_signal_int_free(count);
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    count = sz_signal_int(1);
    {
      SzIo *io = sz_ui_run_rebuild(run_rebuild_factory, count);
      sz_release(io);
    }
    sz_signal_int_free(count);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }
}

typedef struct {
  SzSignalInt *count;
  int calls;
} KeepEnv;

static SzView *keep_factory(void *env) {
  KeepEnv *e = (KeepEnv *)env;
  SzView *root;
  e->calls++;
  root = sz_view_column();
  sz_view_add_child(root, sz_view_text_signal_int(e->count, "n="));
  return root;
}

static void *stamp_bump(void *arg) {
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 50000000L;
  nanosleep(&ts, NULL);
  write_stamp((const char *)arg, "bump");
  return NULL;
}

static void test_ui_run_rebuild_keepalive(void) {
  KeepEnv *env;
  pthread_t th;
  SzIoResult r;
  const char *stamp = "/tmp/scuzz_ui_keepalive.stamp";
  const char *dump = "/tmp/scuzz_ui_keepalive.dump";
  FILE *f;
  char buf[8192];
  size_t n;

  env = (KeepEnv *)sz_alloc(sizeof(KeepEnv));
  env->count = sz_signal_int(3);
  env->calls = 0;
  write_stamp(stamp, "0");
  remove(dump);
  setenv("SCUZZ_UI_RELOAD_STAMP", stamp, 1);
  setenv("SCUZZ_UI_DEBUG_DUMP", dump, 1);
  setenv("SCUZZ_LIVE_FRAMES", "8", 1);
  assert(pthread_create(&th, NULL, stamp_bump, (void *)stamp) == 0);
  r = sz_io_unsafe_run(sz_ui_run_rebuild(keep_factory, env));
  pthread_join(th, NULL);
  unsetenv("SCUZZ_UI_RELOAD_STAMP");
  unsetenv("SCUZZ_UI_DEBUG_DUMP");
  unsetenv("SCUZZ_LIVE_FRAMES");
  assert(r.ok);
  assert(sz_signal_int_get(env->count) == 3);
  assert(env->calls >= 2);
  f = fopen(dump, "r");
  assert(f);
  n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';
  assert(strstr(buf, "[signals]") != NULL);
  assert(strstr(buf, "[views]") != NULL);
  assert(strstr(buf, "[taps]") != NULL);
  assert(strstr(buf, "[fields]") != NULL);
  assert(strstr(buf, "[scrolls]") != NULL);
  assert(strstr(buf, "[session]") != NULL);
  assert(strstr(buf, "[heap]") != NULL);
  assert(strstr(buf, "live_bytes=") != NULL);
  assert(strstr(buf, "[live]") != NULL);
  sz_signal_int_free(env->count);
  sz_free(env);
  remove(stamp);
  remove(dump);
}
/* --- quiesce terminal boundary ------------------------------------------ */

typedef struct {
  SzUiSession *session;
  SzSignalInt *sig;
  atomic_int stop;
  atomic_int posted;
} QuiescePostEnv;

/* IO -> UI bridge poster: keeps pending bridge work so quiesce never sees
 * two consecutive idle pumps. */
static void *quiesce_poster(void *arg) {
  QuiescePostEnv *env = (QuiescePostEnv *)arg;
  int64_t v = 0;
  /* Post continuously: the pump drains the bridge each frame, and a post is
   * far cheaper than a paint, so pending work is visible at every quiesce
   * sample. */
  while (!atomic_load_explicit(&env->stop, memory_order_relaxed)) {
    sz_ui_bridge_post_int(env->session, env->sig, v++);
    atomic_fetch_add_explicit(&env->posted, 1, memory_order_relaxed);
  }
  return NULL;
}

static void test_quiesce(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzSignalInt *sig;
  SzView *root;
  QuiescePostEnv env;
  pthread_t th;
  SzQuiesce q;
  int tries;

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;

  assert(sz_ui_quiesce(NULL) == SZ_QUIESCE_SETTLED);

  /* Idle session settles. */
  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_text_signal_int(sig, "n="));
  session = sz_ui_mount(&cfg, root);
  assert(session);
  assert(sz_ui_quiesce(session) == SZ_QUIESCE_SETTLED);
  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_int_free(sig);

  /* Pending bridge work every pump trips the 64-pump budget. The poster
   * thread runs at the same time as pump flushes. Wait for the first post
   * so two idle samples cannot settle before the poster runs. */
  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_text_signal_int(sig, "n="));
  session = sz_ui_mount(&cfg, root);
  assert(session);
  env.session = session;
  env.sig = sig;
  atomic_init(&env.stop, 0);
  atomic_init(&env.posted, 0);
  sz_ui_bridge_post_int(session, sig, 0);
  assert(pthread_create(&th, NULL, quiesce_poster, &env) == 0);
  while (atomic_load_explicit(&env.posted, memory_order_relaxed) == 0)
    sched_yield();
  q = SZ_QUIESCE_SETTLED;
  for (tries = 0; tries < 8 && q != SZ_QUIESCE_BUDGET_TRIPPED; tries++)
    q = sz_ui_quiesce(session);
  atomic_store_explicit(&env.stop, 1, memory_order_relaxed);
  pthread_join(th, NULL);
  assert(q == SZ_QUIESCE_BUDGET_TRIPPED);
  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static char *slurp_cstr(const char *path) {
  FILE *f = fopen(path, "r");
  char *buf;
  long n;
  assert(f);
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = (char *)malloc((size_t)n + 1);
  assert(buf);
  n = (long)fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[n] = '\0';
  return buf;
}
static int64_t dump_i64(const char *s, const char *key) {
  const char *p = strstr(s, key);
  assert(p);
  return (int64_t)atoll(p + strlen(key));
}

static void test_session_debug_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *btn, *search;
  SzSignalInt *count;
  SzSignalStr *draft, *query;
  SzInputEvent tap;
  const char *path = "/tmp/scuzz_ui_debug.dump";
  char *a, *b, *c;

  count = sz_signal_int(0);
  draft = sz_signal_str("");
  query = sz_signal_str("");
  root = sz_view_column();
  sz_view_add_child(root, sz_view_text("Debug"));
  btn = sz_view_button("+", counter_tap, count);
  sz_view_add_child(root, btn);
  sz_view_add_child(root, sz_view_button("-", counter_tap, count));
  sz_view_add_child(root, sz_view_text_field(draft, "item"));
  search = sz_view_text_field(query, "search");
  sz_view_add_child(root, search);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 200;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_debug_dump(session, path));
  assert(sz_alloc_panic_dump_path());
  assert(strstr(sz_alloc_panic_dump_path(), ".panic") != NULL);
  assert(sz_ui_pump_sync(session));
  a = slurp_cstr(path);
  assert(strstr(a, "[signals]") != NULL);
  assert(strstr(a, "[views]") != NULL);
  assert(strstr(a, "text:Debug") != NULL);
  assert(strstr(a, "[taps]") != NULL);
  assert(strstr(a, "0 +") != NULL);
  assert(strstr(a, "1 -") != NULL);
  {
    const char *taps = strstr(a, "[taps]\n");
    assert(taps != NULL);
    assert(strstr(taps, ",") != NULL);
    assert(strstr(taps, "x") != NULL);
  }
  assert(strstr(a, "[last_hit]") == NULL);
  assert(strstr(a, "[fields]") != NULL);
  assert(strstr(a, "0* item=\"\"") != NULL);
  assert(strstr(a, "1 search=\"\"") != NULL);
  assert(strstr(a, "1* search") == NULL);
  assert(strstr(a, "[scrolls]") != NULL);
  assert(strstr(a, "[session]") != NULL);
  assert(strstr(a, "kind=headless") != NULL);
  assert(strstr(a, "width=200") != NULL);
  assert(strstr(a, "height=200") != NULL);
  assert(strstr(a, "lifecycle=resume") != NULL);
  assert(strstr(a, "pumps=1") != NULL);
  assert(strstr(a, "[heap]") != NULL);
  assert(strstr(a, "live_bytes=") != NULL);
  assert(strstr(a, "[live]") != NULL);
  assert(strstr(a, "string rc=") != NULL);
  assert(strstr(a, "live_count=") != NULL);
  assert(strstr(a, "peak_bytes=") != NULL);
  assert(strstr(a, "delta_bytes=") != NULL);
  assert(strstr(a, "delta_count=") != NULL);
  assert(strstr(a, "raw=") != NULL);
  assert(strstr(a, "string=") != NULL);
  assert(strstr(a, "list=") != NULL);
  assert(strstr(a, "pair=") != NULL);
  {
    char *second;
    int64_t dc;
    assert(sz_ui_session_dump_now(session));
    second = slurp_cstr(path);
    assert(strstr(second, "string=") != NULL);
    dc = dump_i64(second, "delta_count=");
    assert(dc > -20 && dc < 20);
    free(second);
  }

  {
    SzInputEvent text;
    memset(&text, 0, sizeof(text));
    text.kind = SZ_INPUT_TEXT;
    text.text = "hi";
    assert(sz_ui_inject_sync(session, &text));
    assert(sz_ui_pump_sync(session));
    free(a);
    a = slurp_cstr(path);
    assert(strstr(a, "0* item=\"hi\" caret=2") != NULL);
  }

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(btn).x + 8.f;
  tap.y = sz_view_frame(btn).y + 8.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(count) == 1);
  assert(sz_ui_pump_sync(session));
  b = slurp_cstr(path);
  assert(strcmp(a, b) != 0);
  free(a);

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(search).x + 8.f;
  tap.y = sz_view_frame(search).y + 8.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_ui_pump_sync(session));
  c = slurp_cstr(path);
  assert(strstr(c, "1* search=\"\"") != NULL);
  assert(strstr(c, "0 item=\"hi\"") != NULL);
  assert(strstr(c, "0* item") == NULL);
  assert(strstr(c, "[last_hit]") != NULL);
  assert(strstr(c, "-> button:+") != NULL || strstr(c, "-> textfield:search") != NULL);
  free(b);
  free(c);

  sz_ui_unmount(session);
  assert(sz_alloc_panic_dump_path() == NULL);
  sz_signal_int_free(count);
  sz_signal_str_free(draft);
  sz_signal_str_free(query);
  remove(path);
}

static void test_xy_hit_and_miss(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *btn;
  SzSignalInt *count;
  SzInputEvent tap;
  const char *path = "/tmp/scuzz_ui_xy.dump";
  const char *script = "/tmp/scuzz_ui_xy.script";
  char *dump;
  SzRect fr;

  count = sz_signal_int(0);
  root = sz_view_column();
  btn = sz_view_button("+1", counter_tap, count);
  sz_view_add_child(root, btn);
  sz_view_add_child(root, sz_view_text("pad"));

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 160;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_debug_dump(session, path));
  assert(sz_ui_pump_sync(session));
  fr = sz_view_frame(btn);

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = fr.x + fr.w * 0.5f;
  tap.y = fr.y + fr.h * 0.5f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(count) == 1);
  assert(sz_ui_pump_sync(session));
  dump = slurp_cstr(path);
  assert(strstr(dump, "[last_hit]") != NULL);
  assert(strstr(dump, "-> button:+1") != NULL);
  free(dump);

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = 190.f;
  tap.y = 150.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(count) == 1);
  assert(sz_ui_pump_sync(session));
  dump = slurp_cstr(path);
  assert(strstr(dump, "-> NULL") != NULL);
  free(dump);

  remove(script);
  assert(sz_ui_session_set_inject(session, script));
  {
    char line[128];
    snprintf(line, sizeof line, "xy %.1f %.1f\nxy 190.0 150.0\n",
             fr.x + fr.w * 0.5f, fr.y + fr.h * 0.5f);
    write_stamp(script, line);
  }
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 2);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  remove(path);
  remove(script);
}

static void test_record_live_not_script(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *btn;
  SzSignalInt *count;
  SzInputEvent tap;
  const char *record = "/tmp/scuzz_ui_record.script";
  const char *inject = "/tmp/scuzz_ui_record_inject.script";
  char *body;
  SzRect fr;
  size_t n0;

  remove(record);
  remove(inject);
  count = sz_signal_int(0);
  root = sz_view_column();
  btn = sz_view_button("+1", counter_tap, count);
  sz_view_add_child(root, btn);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_record(session, record));
  assert(sz_ui_pump_sync(session));
  fr = sz_view_frame(btn);

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = fr.x + fr.w * 0.5f;
  tap.y = fr.y + fr.h * 0.5f;
  assert(sz_ui_session_live_inject(session, &tap));
  assert(sz_signal_int_get(count) == 1);
  body = slurp_cstr(record);
  assert(strstr(body, "tap 0") != NULL);
  n0 = strlen(body);
  free(body);

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = 190.f;
  tap.y = 90.f;
  assert(sz_ui_session_live_inject(session, &tap));
  body = slurp_cstr(record);
  assert(strstr(body, "xy ") != NULL);
  n0 = strlen(body);
  free(body);

  assert(sz_ui_session_set_inject(session, inject));
  write_stamp(inject, "tap 0\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 2);
  body = slurp_cstr(record);
  assert(strlen(body) == n0);
  free(body);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  remove(record);
  remove(inject);
}

static void test_record_live_scroll(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *list, *scroll;
  SzInputEvent ev;
  const char *record = "/tmp/scuzz_ui_record_scroll.script";
  char *body;
  SzRect fr;
  size_t n0;

  remove(record);
  root = sz_view_column();
  list = sz_view_list();
  sz_view_add_child(list, sz_view_text("one"));
  sz_view_add_child(list, sz_view_text("two"));
  sz_view_add_child(list, sz_view_text("three"));
  sz_view_add_child(list, sz_view_text("four"));
  scroll = sz_view_scroll(list);
  sz_view_add_child(root, scroll);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_record(session, record));
  assert(sz_ui_pump_sync(session));
  fr = sz_view_frame(scroll);

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_SCROLL;
  ev.x = fr.x + fr.w * 0.5f;
  ev.y = fr.y + fr.h * 0.5f;
  ev.dy = 40.f;
  assert(sz_ui_session_live_inject(session, &ev));
  body = slurp_cstr(record);
  assert(strstr(body, "scroll 0 40") != NULL);
  n0 = strlen(body);
  free(body);

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_SCROLL;
  ev.x = 1000.f;
  ev.y = 1000.f;
  ev.dy = 40.f;
  assert(!sz_ui_session_live_inject(session, &ev));
  body = slurp_cstr(record);
  assert(strlen(body) == n0);
  free(body);

  sz_ui_unmount(session);
  remove(record);
}

static void test_studio_shaped_xy(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *home, *banner, *inc_btn, *add_btn;
  SzSignalInt *count;
  SzSignalInt *page;
  SzInputEvent tap;
  const char *record = "/tmp/scuzz_ui_studio_xy.script";
  const char *dump = "/tmp/scuzz_ui_studio_xy.dump";
  char *body;
  SzRect fr, banner_fr;

  remove(record);
  count = sz_signal_int(0);
  page = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_button("Home", NULL, NULL));
  home = sz_view_column();
  banner = sz_view_background(0xFF3D7EA6u, sz_view_center(sz_view_text("Studio")));
  sz_view_add_child(home, sz_view_aspect_ratio(6, 1, banner));
  sz_view_add_child(home, sz_view_text("count = 0"));
  inc_btn = sz_view_button("+1", counter_tap, count);
  sz_view_add_child(home, inc_btn);
  add_btn = sz_view_button("Add", NULL, NULL);
  sz_view_add_child(home, add_btn);
  sz_view_add_child(root, sz_view_show_when(page, 0, home));

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 400;
  cfg.height = 560;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_record(session, record));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_pump_sync(session));

  fr = sz_view_frame(inc_btn);
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = fr.x + fr.w * 0.5f;
  tap.y = fr.y + fr.h * 0.5f;
  assert(sz_ui_session_live_inject(session, &tap));
  assert(sz_signal_int_get(count) == 1);
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "-> button:+1") != NULL);
  free(body);
  body = slurp_cstr(record);
  assert(strstr(body, "tap ") != NULL);
  free(body);

  banner_fr = sz_view_frame(banner);
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = banner_fr.x + banner_fr.w * 0.5f;
  tap.y = banner_fr.y + banner_fr.h * 0.5f;
  assert(sz_ui_session_live_inject(session, &tap));
  assert(sz_signal_int_get(count) == 1);
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "-> NULL") != NULL);
  free(body);
  body = slurp_cstr(record);
  assert(strstr(body, "xy ") != NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  sz_signal_int_free(page);
  remove(record);
  remove(dump);
}

static void test_session_inject_script(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *btn;
  SzSignalInt *count;
  const char *path = "/tmp/scuzz_ui_inject.script";
  FILE *f;

  remove(path);
  count = sz_signal_int(0);
  root = sz_view_column();
  btn = sz_view_button("+", counter_tap, count);
  sz_view_add_child(root, btn);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 0);

  write_stamp(path, "tap 0\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 1);

  f = fopen(path, "a");
  assert(f);
  fputs("tap 0\n", f);
  fclose(f);
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 2);

  write_stamp(path, "tap 0\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 3);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  remove(path);
}

static void test_session_inject_grows_past_4k(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *btn;
  SzSignalInt *count;
  const char *path = "/tmp/scuzz_ui_inject_4k.script";
  FILE *f;
  int i;

  remove(path);
  count = sz_signal_int(0);
  root = sz_view_column();
  btn = sz_view_button("+", counter_tap, count);
  sz_view_add_child(root, btn);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 0);

  f = fopen(path, "w");
  assert(f);
  fputc('#', f);
  for (i = 0; i < 4200; i++)
    fputc('x', f);
  fputc('\n', f);
  fputs("tap 0\n", f);
  fclose(f);
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 1);

  f = fopen(path, "a");
  assert(f);
  fputs("tap 0\n", f);
  fclose(f);
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 2);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  remove(path);
}

static void test_session_inject_control(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  WatchRebuildEnv *env;
  SzView *root, *first;
  SzString *a11y;
  const char *stamp = "/tmp/scuzz_ui_inject_control.stamp";
  const char *dump = "/tmp/scuzz_ui_inject_control.dump";
  const char *inject = "/tmp/scuzz_ui_inject_control.script";
  const char *fuzz = "/tmp/scuzz_ui_inject_control.fuzz.dump";
  char *body;

  write_stamp(stamp, "n=");
  remove(dump);
  remove(inject);
  remove(fuzz);
  env = (WatchRebuildEnv *)sz_alloc(sizeof(WatchRebuildEnv));
  env->count = sz_signal_int(0);
  env->path = stamp;
  env->btn = NULL;
  root = watch_rebuild(env);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  sz_ui_session_set_rebuild(session, watch_rebuild, env);
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_session_set_inject(session, inject));
  assert(sz_ui_session_alive(session));
  assert(sz_ui_session_pumps(session) == 0);
  assert(!sz_ui_session_dump_now(NULL));
  assert(!sz_ui_session_alive(NULL));
  assert(sz_ui_session_pumps(NULL) == 0);
  sz_ui_session_request_stop(NULL);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_pumps(session) == 1);
  body = slurp_cstr(dump);
  assert(strstr(body, "[session]") != NULL);
  assert(strstr(body, "kind=headless") != NULL);
  assert(strstr(body, "lifecycle=resume") != NULL);
  assert(strstr(body, "pumps=1") != NULL);
  assert(strstr(body, "[heap]") != NULL);
  assert(strstr(body, "live_bytes=") != NULL);
  assert(strstr(body, "[live]") != NULL);
  free(body);

  assert(sz_ui_session_write_dump(session, fuzz));
  body = slurp_cstr(fuzz);
  assert(strstr(body, "[signals]") != NULL);
  assert(strstr(body, "[session]") == NULL);
  assert(strstr(body, "[heap]") == NULL);
  assert(strstr(body, "[live]") == NULL);
  free(body);

  remove(dump);
  assert(sz_ui_session_dump_now(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "[session]") != NULL);
  assert(strstr(body, "[heap]") != NULL);
  assert(strstr(body, "peak_bytes=") != NULL);
  assert(strstr(body, "delta_bytes=") != NULL);
  assert(strstr(body, "string=") != NULL);
  assert(strstr(body, "[live]") != NULL);
  free(body);

  {
    unsigned pumps0 = sz_ui_session_pumps(session);
    remove(dump);
    write_stamp(inject, "dump\n");
    assert(sz_ui_pump_sync(session));
    body = slurp_cstr(dump);
    assert(strstr(body, "[heap]") != NULL);
    assert(strstr(body, "pumps=") != NULL);
    free(body);
    assert(sz_ui_session_pumps(session) > pumps0);
  }

  {
    void *hold;
    int64_t peak_hi, peak_lo;
    hold = sz_alloc(65536);
    assert(sz_ui_session_dump_now(session));
    body = slurp_cstr(dump);
    assert(strstr(body, "raw=") != NULL);
    peak_hi = dump_i64(body, "peak_bytes=");
    free(body);
    sz_free(hold);
    write_stamp(inject, "resetpeak\n");
    assert(sz_ui_pump_sync(session));
    assert(sz_ui_session_dump_now(session));
    body = slurp_cstr(dump);
    peak_lo = dump_i64(body, "peak_bytes=");
    assert(strstr(body, "delta_count=") != NULL);
    free(body);
    assert(peak_hi >= 65536);
    assert(peak_lo < peak_hi);
  }

  first = sz_ui_session_root(session);
  write_stamp(stamp, "v=");
  write_stamp(inject, "reload\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_root(session) != first);
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:v=") != NULL);
  sz_string_free(a11y);

  write_stamp(inject, "quit\ntap 0\n");
  assert(sz_ui_pump_sync(session));
  assert(!sz_ui_session_alive(session));
  assert(sz_signal_int_get(env->count) == 0);
  assert(!sz_ui_pump_sync(session));

  sz_ui_unmount(session);
  sz_signal_int_free(env->count);
  sz_free(env);
  remove(stamp);
  remove(dump);
  remove(inject);
  remove(fuzz);
}

static void test_session_dump_now_needs_path(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;

  root = sz_view_column();
  sz_view_add_child(root, sz_view_text("x"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 80;
  cfg.height = 40;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(!sz_ui_session_dump_now(session));
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_pumps(session) == 1);
  sz_ui_session_request_stop(session);
  assert(!sz_ui_session_alive(session));
  assert(sz_ui_session_lifecycle(session) == SZ_LIFECYCLE_STOP);
  assert(!sz_ui_pump_sync(session));
  assert(sz_ui_session_pumps(session) == 1);
  sz_ui_unmount(session);
}

static void test_session_inject_scroll(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *row, *scroll, *scroll2, *list, *list2;
  const char *path = "/tmp/scuzz_ui_inject_scroll.script";
  float y0, y1;

  remove(path);
  root = sz_view_column();
  list = sz_view_list();
  sz_view_add_child(list, sz_view_text("one"));
  sz_view_add_child(list, sz_view_text("two"));
  sz_view_add_child(list, sz_view_text("three"));
  sz_view_add_child(list, sz_view_text("four"));
  list2 = sz_view_list();
  sz_view_add_child(list2, sz_view_text("a"));
  sz_view_add_child(list2, sz_view_text("b"));
  sz_view_add_child(list2, sz_view_text("c"));
  sz_view_add_child(list2, sz_view_text("d"));
  scroll = sz_view_scroll(list);
  scroll2 = sz_view_scroll(list2);
  row = sz_view_row();
  sz_view_add_child(row, sz_view_expanded(scroll));
  sz_view_add_child(row, sz_view_expanded(scroll2));
  sz_view_add_child(root, row);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_pump_sync(session));
  y0 = sz_view_scroll_y(scroll);
  y1 = sz_view_scroll_y(scroll2);

  write_stamp(path, "scroll 40\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_scroll_y(scroll) == y0 + 40.f);
  assert(sz_view_scroll_y(scroll2) == y1);

  write_stamp(path, "scroll 1 40\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_scroll_y(scroll) == y0 + 40.f);
  assert(sz_view_scroll_y(scroll2) == y1 + 40.f);

  write_stamp(path, "scroll\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_scroll_y(scroll) == y0 + 80.f);
  assert(sz_view_scroll_y(scroll2) == y1 + 40.f);

  sz_ui_unmount(session);
  remove(path);
}

static void test_session_inject_backspace(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  const char *path = "/tmp/scuzz_ui_inject_backspace.script";

  remove(path);
  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "item");
  sz_view_add_child(root, field);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_pump_sync(session));

  write_stamp(path, "text hi\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hi") == 0);

  write_stamp(path, "backspace\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "h") == 0);

  write_stamp(path, "text abc\nbackspace 2\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "a") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
}

static void test_session_inject_type(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  const char *path = "/tmp/scuzz_ui_inject_type.script";

  remove(path);
  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "item");
  sz_view_add_child(root, field);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_pump_sync(session));

  write_stamp(path, "text hi\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hi") == 0);

  write_stamp(path, "type !\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hi!") == 0);

  write_stamp(path, "type\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hi!") == 0);

  write_stamp(path, "text ab\ntype c\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abc") == 0);

  write_stamp(path, "text x\ntype a\\nb\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "xa\nb") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
}

static void test_session_inject_key(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const char *path = "/tmp/scuzz_ui_inject_key.script";

  remove(path);
  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "item");
  sz_view_add_child(root, field);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_pump_sync(session));

  /* Focused field keeps q (quit is window close / Headless quit). */
  write_stamp(path, "text hi\nkey q q\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hiq") == 0);
  assert(sz_ui_session_alive(session));

  write_stamp(path, "key Enter\nkey Tab\nkey Backspace\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hi") == 0);
  assert(sz_ui_session_alive(session));

  write_stamp(path, "key PageUp\nkey PageDown\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hi") == 0);

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "q";
  ev.text = "q";
  assert(sz_ui_inject_sync(session, &ev));
  assert(strcmp(sz_signal_str_get(draft), "hiq") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
}

static void test_session_inject_key_utf8_backspace(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  const char *path = "/tmp/scuzz_ui_inject_key_utf8.script";
  const char *cafe = "caf\xC3\xA9";

  remove(path);
  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "item");
  sz_view_add_child(root, field);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_pump_sync(session));

  write_stamp(path, "text caf\xC3\xA9\nkey Backspace\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "caf") == 0);
  assert(strlen(cafe) == 5);

  write_stamp(path, "text caf\xC3\xA9\nbackspace\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "caf") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
}

static void test_record_live_key(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const char *record = "/tmp/scuzz_ui_record_key.script";
  char *body;

  remove(record);
  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "item");
  sz_view_add_child(root, field);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_record(session, record));
  assert(sz_ui_pump_sync(session));

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "q";
  ev.text = "q";
  assert(sz_ui_session_live_inject(session, &ev));
  assert(strcmp(sz_signal_str_get(draft), "q") == 0);
  body = slurp_cstr(record);
  assert(strstr(body, "key q q") != NULL);
  assert(strstr(body, "type ") == NULL);
  free(body);

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "Backspace";
  ev.text = "";
  assert(sz_ui_session_live_inject(session, &ev));
  assert(strcmp(sz_signal_str_get(draft), "") == 0);
  body = slurp_cstr(record);
  assert(strstr(body, "key Backspace") != NULL);
  assert(strstr(body, "backspace") == NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(record);
}

static void test_record_type_escapes(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const char *record = "/tmp/scuzz_ui_record_type_esc.script";
  char *body;

  remove(record);
  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "item");
  sz_view_add_child(root, field);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_record(session, record));
  assert(sz_ui_pump_sync(session));

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = "a\nb";
  assert(sz_ui_session_live_inject(session, &ev));
  body = slurp_cstr(record);
  assert(strstr(body, "type a\\nb") != NULL);
  free(body);
  assert(strcmp(sz_signal_str_get(draft), "a\nb") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(record);
}

static void test_session_inject_key_repeat(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const char *path = "/tmp/scuzz_ui_inject_key_repeat.script";
  const char *dump = "/tmp/scuzz_ui_inject_key_repeat.dump";
  const char *record = "/tmp/scuzz_ui_record_key_repeat.script";
  char *body;

  remove(path);
  remove(dump);
  remove(record);
  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "item");
  sz_view_add_child(root, field);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_pump_sync(session));

  write_stamp(path, "key a a\nkey a+repeat a\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "aa") == 0);
  assert(sz_view_text_field_caret(field) == 2);

  write_stamp(path, "key Backspace\nkey Backspace+repeat\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "") == 0);
  assert(sz_view_text_field_caret(field) == 0);

  write_stamp(path, "key b b\nkey c c\nkey ArrowLeft\nkey ArrowLeft+repeat\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "bc") == 0);
  assert(sz_view_text_field_caret(field) == 0);

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "d";
  ev.text = "d";
  ev.key_repeat = 1;
  assert(sz_ui_inject_sync(session, &ev));
  assert(strcmp(sz_signal_str_get(draft), "dbc") == 0);

  assert(sz_ui_session_set_record(session, record));
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "e";
  ev.text = "e";
  ev.key_repeat = 1;
  assert(sz_ui_session_live_inject(session, &ev));
  assert(strcmp(sz_signal_str_get(draft), "debc") == 0);
  body = slurp_cstr(record);
  assert(strstr(body, "key e+repeat e") != NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
  remove(dump);
  remove(record);
}

static void test_session_inject_compose(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *field, *ed;
  SzSignalStr *draft, *buf;
  SzInputEvent ev;
  const char *path = "/tmp/scuzz_ui_inject_compose.script";
  const char *dump = "/tmp/scuzz_ui_inject_compose.dump";
  const char *record = "/tmp/scuzz_ui_record_compose.script";
  char *body;

  remove(path);
  remove(dump);
  remove(record);
  draft = sz_signal_str("ab");
  buf = sz_signal_str("xy");
  root = sz_view_column();
  field = sz_view_text_field(draft, "item");
  ed = sz_view_editor(buf);
  sz_view_add_child(root, field);
  sz_view_add_child(root, ed);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 240;
  cfg.height = 160;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_pump_sync(session));

  write_stamp(path, "compose n\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "ab") == 0);
  assert(strcmp(sz_view_text_field_preedit(field), "n") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "preedit=\"n\"") != NULL);
  assert(strstr(body, "item=\"ab\"") != NULL);
  free(body);

  write_stamp(path, "compose ni\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "ab") == 0);
  assert(strcmp(sz_view_text_field_preedit(field), "ni") == 0);

  write_stamp(path, "commit\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abni") == 0);
  assert(sz_view_text_field_preedit(field)[0] == '\0');
  body = slurp_cstr(dump);
  assert(strstr(body, "item=\"abni\"") != NULL);
  assert(strstr(body, "preedit=") == NULL);
  free(body);

  write_stamp(path, "compose ja\nkey Escape\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abni") == 0);
  assert(sz_view_text_field_preedit(field)[0] == '\0');

  write_stamp(path, "text hi\nselect 0 2\ncompose x\ncompose\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "x") == 0);

  {
    SzRect fr = sz_view_frame(ed);
    memset(&ev, 0, sizeof(ev));
    ev.kind = SZ_INPUT_TAP;
    ev.x = fr.x + 8.f;
    ev.y = fr.y + 8.f;
    assert(sz_ui_inject_sync(session, &ev));
  }
  write_stamp(path, "compose ka\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "xy") == 0);
  assert(strcmp(sz_view_editor_preedit(ed), "ka") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "[editor]") != NULL);
  assert(strstr(body, "preedit=\"ka\"") != NULL);
  free(body);

  write_stamp(path, "commit\n");
  assert(sz_ui_pump_sync(session));
  assert(strstr(sz_signal_str_get(buf), "ka") != NULL);
  assert(sz_view_editor_preedit(ed)[0] == '\0');

  write_stamp(path, "key z z\nkey z+repeat z\nkey Backspace+repeat\n");
  assert(sz_ui_pump_sync(session));
  {
    const char *got = sz_signal_str_get(buf);
    assert(strchr(got, 'z') != NULL);
  }

  assert(sz_ui_session_set_record(session, record));
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_COMPOSE;
  ev.text = "ni";
  assert(sz_ui_session_live_inject(session, &ev));
  body = slurp_cstr(record);
  assert(strstr(body, "compose ni") != NULL);
  free(body);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_COMPOSE;
  ev.text = "";
  assert(sz_ui_session_live_inject(session, &ev));
  body = slurp_cstr(record);
  assert(strstr(body, "commit") != NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  sz_signal_str_free(buf);
  remove(path);
  remove(dump);
  remove(record);
}

static void test_record_live_hover_secondary(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *btn, *tip;
  SzSignalInt *count;
  SzInputEvent ev;
  const char *record = "/tmp/scuzz_ui_record_hover.script";
  char *body;
  SzRect fr;

  remove(record);
  count = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, count);
  tip = sz_view_tooltip("Sean", btn);
  root = sz_view_column();
  sz_view_add_child(root, tip);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_record(session, record));
  assert(sz_ui_pump_sync(session));
  fr = sz_view_frame(btn);

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_MOVE;
  ev.pointer_button = 0;
  ev.x = fr.x + fr.w * 0.5f;
  ev.y = fr.y + fr.h * 0.5f;
  assert(sz_ui_session_live_inject(session, &ev));
  assert(sz_ui_session_live_inject(session, &ev));
  body = slurp_cstr(record);
  {
    char *first = strstr(body, "hover ");
    assert(first != NULL);
    assert(strstr(first + 6, "hover ") == NULL);
  }
  free(body);

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_DOWN;
  ev.pointer_button = 3;
  ev.x = fr.x + fr.w * 0.5f;
  ev.y = fr.y + fr.h * 0.5f;
  assert(sz_ui_session_live_inject(session, &ev));
  ev.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_session_live_inject(session, &ev));
  assert(sz_signal_int_get(count) == 0);
  body = slurp_cstr(record);
  assert(strstr(body, "secondary 0") != NULL);
  assert(strstr(body, "tap ") == NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  remove(record);
}

static void test_session_inject_caret(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const SzTheme *theme = sz_theme_default();
  const char *path = "/tmp/scuzz_ui_inject_caret.script";
  const char *dump = "/tmp/scuzz_ui_inject_caret.dump";
  char *body;
  SzRect fr;
  float tap_x, tap_y;

  remove(path);
  remove(dump);
  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "item");
  sz_view_add_child(root, field);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_pump_sync(session));

  write_stamp(path, "text abc\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abc") == 0);
  assert(sz_view_text_field_caret(field) == 3);
  body = slurp_cstr(dump);
  assert(strstr(body, "0* item=\"abc\" caret=3") != NULL);
  assert(strstr(body, "[taps]") != NULL);
  free(body);

  write_stamp(path, "caret 1\nkey Delete\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "ac") == 0);
  assert(sz_view_text_field_caret(field) == 1);
  body = slurp_cstr(dump);
  assert(strstr(body, "0* item=\"ac\" caret=1") != NULL);
  free(body);

  write_stamp(path, "text abc\nkey Home\nkey ArrowRight\nkey x x\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "axbc") == 0);
  assert(sz_view_text_field_caret(field) == 2);

  write_stamp(path, "text abc\nkey End\nkey ArrowLeft\nkey Backspace\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "ac") == 0);
  assert(sz_view_text_field_caret(field) == 1);

  write_stamp(path, "text caf\xC3\xA9\nkey ArrowLeft\nkey Delete\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "caf") == 0);
  assert(sz_view_text_field_caret(field) == 3);

  write_stamp(path, "text abc\ncaret 0 1\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_text_field_caret(field) == 1);

  sz_view_layout(sz_ui_session_root(session), (float)cfg.width, (float)cfg.height,
                 theme);
  fr = sz_view_frame(field);
  tap_x = fr.x + 6.f + sk_font_measure_string("a", theme->font_px);
  tap_y = fr.y + fr.h * 0.5f;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TAP;
  ev.x = tap_x;
  ev.y = tap_y;
  write_stamp(path, "text abc\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));
  assert(sz_view_text_field_caret(field) == 1);
  write_stamp(path, "key Delete\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "ac") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
  remove(dump);
}

static void test_session_inject_hover_secondary(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *btn, *tip;
  SzSignalInt *count;
  SzInputEvent ev;
  const char *path = "/tmp/scuzz_ui_inject_hover.script";
  const char *dump = "/tmp/scuzz_ui_inject_hover.dump";
  char *body;
  SzRect fr;

  remove(path);
  remove(dump);
  count = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, count);
  tip = sz_view_tooltip("Sean", btn);
  root = sz_view_column();
  sz_view_add_child(root, tip);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_pump_sync(session));
  fr = sz_view_frame(btn);

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_MOVE;
  ev.pointer_button = 0;
  ev.x = fr.x + 4.f;
  ev.y = fr.y + fr.h * 0.5f;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "[hover]") != NULL);
  assert(strstr(body, "tooltip:Sean") != NULL);
  free(body);

  {
    char line[128];
    snprintf(line, sizeof line, "hover %.1f %.1f\n", fr.x + fr.w * 0.5f,
             fr.y + fr.h * 0.5f);
    write_stamp(path, line);
  }
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "[hover]") != NULL);
  assert(strstr(body, "tooltip:Sean") != NULL);
  free(body);

  write_stamp(path, "hover 190.0 70.0\n");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "[hover]") != NULL);
  assert(strstr(body, "-> NULL") != NULL);
  free(body);

  write_stamp(path, "secondary 0\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "[last_secondary]") != NULL);
  assert(strstr(body, "button:Go") != NULL);
  free(body);

  {
    char line[128];
    snprintf(line, sizeof line, "secondary %.1f %.1f\n", fr.x + fr.w * 0.5f,
             fr.y + fr.h * 0.5f);
    write_stamp(path, line);
  }
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 0);

  write_stamp(path, "tap 0\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 1);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  remove(path);
  remove(dump);
}

static void test_on_secondary_script_fires(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *btn, *wrap;
  SzSignalInt *primary;
  SzSignalInt *secondary;
  const char *path = "/tmp/scuzz_ui_on_secondary.script";
  const char *dump = "/tmp/scuzz_ui_on_secondary.dump";
  char *body;

  remove(path);
  remove(dump);
  primary = sz_signal_int(0);
  secondary = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, primary);
  wrap = sz_view_on_secondary(btn, counter_tap, secondary);
  root = sz_view_column();
  sz_view_add_child(root, wrap);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_pump_sync(session));
  assert(sz_view_kind(wrap) == SZ_VIEW_ON_SECONDARY);
  assert(!sz_view_is_tap_target(wrap));

  write_stamp(path, "secondary 0\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(primary) == 0);
  assert(sz_signal_int_get(secondary) == 1);
  body = slurp_cstr(dump);
  assert(strstr(body, "[last_secondary]") != NULL);
  assert(strstr(body, "button:Go") != NULL);
  free(body);

  write_stamp(path, "tap 0\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(primary) == 1);
  assert(sz_signal_int_get(secondary) == 1);

  sz_ui_unmount(session);
  sz_signal_int_free(primary);
  sz_signal_int_free(secondary);
  remove(path);
  remove(dump);
}

static void test_session_inject_selection_clipboard(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const SzTheme *theme = sz_theme_default();
  const char *path = "/tmp/scuzz_ui_inject_sel.script";
  const char *dump = "/tmp/scuzz_ui_inject_sel.dump";
  const char *record = "/tmp/scuzz_ui_record_sel.script";
  char *body;
  SzRect fr;
  float x0, x2, y;

  remove(path);
  remove(dump);
  remove(record);
  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "item");
  sz_view_add_child(root, field);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_session_set_record(session, record));
  assert(sz_ui_pump_sync(session));

  write_stamp(path, "text abc\ncaret 0\nkey ArrowRight+shift\nkey ArrowRight+shift\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abc") == 0);
  assert(sz_view_text_field_caret(field) == 2);
  assert(sz_view_text_field_sel_start(field) == 0);
  assert(sz_view_text_field_sel_end(field) == 2);
  body = slurp_cstr(dump);
  assert(strstr(body, "0* item=\"abc\" caret=2 sel=0:2") != NULL);
  assert(strstr(body, "[taps]") != NULL);
  assert(strstr(body, "[fields]") != NULL);
  free(body);

  write_stamp(path, "copy\nkey End\npaste\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abcab") == 0);

  write_stamp(path, "text abc\nselect 0 2\ntype x\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "xc") == 0);
  assert(sz_view_text_field_caret(field) == 1);
  assert(sz_view_text_field_sel_start(field) == 1);
  assert(sz_view_text_field_sel_end(field) == 1);
  body = slurp_cstr(dump);
  assert(strstr(body, "0* item=\"xc\" caret=1 sel=1:1") != NULL);
  free(body);

  write_stamp(path, "text abc\nselect 0 2\nkey Backspace\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "c") == 0);

  write_stamp(path, "text abc\nselect 1 3\nkey Delete\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "a") == 0);

  write_stamp(path, "text abc\nselect 0 2\ncut\nkey End\npaste\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "cab") == 0);

  write_stamp(path, "text hi\npaste xyz\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hixyz") == 0);

  write_stamp(path, "text caf\xC3\xA9\nselect 3 5\nkey Backspace\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "caf") == 0);

  write_stamp(path, "text abc\nselect 0 3\nkey ArrowLeft\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_text_field_caret(field) == 0);
  assert(sz_view_text_field_sel_start(field) == 0);
  assert(sz_view_text_field_sel_end(field) == 0);

  sz_view_layout(sz_ui_session_root(session), (float)cfg.width, (float)cfg.height,
                 theme);
  fr = sz_view_frame(field);
  x0 = fr.x + 6.f;
  x2 = fr.x + 6.f + sk_font_measure_string("ab", theme->font_px);
  y = fr.y + fr.h * 0.5f;
  write_stamp(path, "text abc\n");
  assert(sz_ui_pump_sync(session));
  {
    char line[128];
    snprintf(line, sizeof line, "drag %.1f %.1f %.1f %.1f\n", x0, y, x2, y);
    write_stamp(path, line);
  }
  assert(sz_ui_pump_sync(session));
  assert(sz_view_text_field_sel_start(field) == 0);
  assert(sz_view_text_field_sel_end(field) >= 1);
  assert(strcmp(sz_signal_str_get(draft), "abc") == 0);

  write_stamp(path, "text abc\nselect 0 2\n");
  assert(sz_ui_pump_sync(session));
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "c";
  ev.key_mods = SZ_KEY_CMD;
  assert(sz_ui_session_live_inject(session, &ev));
  ev.key = "v";
  ev.key_mods = SZ_KEY_CTRL;
  write_stamp(path, "key End\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_live_inject(session, &ev));
  assert(strcmp(sz_signal_str_get(draft), "abcab") == 0);
  body = slurp_cstr(record);
  assert(strstr(body, "copy") != NULL);
  assert(strstr(body, "paste ab") != NULL);
  assert(strstr(body, "key c") == NULL);
  free(body);

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "ArrowLeft";
  ev.key_mods = SZ_KEY_SHIFT;
  write_stamp(path, "text abc\nkey End\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_live_inject(session, &ev));
  assert(sz_view_text_field_sel_end(field) == 3);
  assert(sz_view_text_field_sel_start(field) < 3);
  body = slurp_cstr(record);
  assert(strstr(body, "key ArrowLeft+shift") != NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
  remove(dump);
  remove(record);
}

static void test_session_inject_field_index(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalStr *draft, *query;
  const char *path = "/tmp/scuzz_ui_inject_field.script";

  remove(path);
  draft = sz_signal_str("");
  query = sz_signal_str("");
  root = sz_view_column();
  sz_view_add_child(root, sz_view_text_field(draft, "item"));
  sz_view_add_child(root, sz_view_text_field(query, "search"));

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 120;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_pump_sync(session));

  write_stamp(path, "text 1 hello\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "") == 0);
  assert(strcmp(sz_signal_str_get(query), "hello") == 0);

  write_stamp(path, "type 0 x\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "x") == 0);
  assert(strcmp(sz_signal_str_get(query), "hello") == 0);

  write_stamp(path, "text 0 ab\nbackspace 0 1\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "a") == 0);
  assert(strcmp(sz_signal_str_get(query), "hello") == 0);

  write_stamp(path, "text 0\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "0") == 0);

  write_stamp(path, "text 9 no\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "0") == 0);
  assert(strcmp(sz_signal_str_get(query), "hello") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  sz_signal_str_free(query);
  remove(path);
}

typedef struct {
  SzSignalInt *sig;
  int64_t value;
} SetEnv;

static void set_tap(SzView *self, void *env) {
  SetEnv *e = (SetEnv *)env;
  (void)self;
  sz_signal_int_set(e->sig, e->value);
}

static void test_button_set_and_show_when(void) {
  SzSignalInt *page;
  SzSignalInt *count;
  SzView *root, *home, *other, *set_btn, *inc_btn;
  SetEnv *set_env;
  const SzTheme *theme = sz_theme_default();
  SzUiConfig cfg;
  SzUiSession *session;
  SzInputEvent tap;
  float hx, hy;

  page = sz_signal_int(0);
  count = sz_signal_int(0);
  root = sz_view_column();
  set_env = (SetEnv *)sz_alloc(sizeof(SetEnv));
  set_env->sig = page;
  set_env->value = 1;
  set_btn = sz_lang_view_button(sz_string_from_cstr("Other"), set_tap, set_env);
  sz_view_add_child(root, set_btn);

  home = sz_view_column();
  sz_view_add_child(home, sz_view_text("Home"));
  inc_btn = sz_lang_view_button(sz_string_from_cstr("+1"), counter_tap, count);
  sz_view_add_child(home, inc_btn);
  sz_view_add_child(root, sz_view_show_when(page, 0, home));

  other = sz_view_column();
  sz_view_add_child(other, sz_view_text("Other page"));
  sz_view_add_child(root, sz_view_show_when(page, 1, other));

  sz_view_layout(root, 200.f, 160.f, theme);
  assert(sz_view_frame(home).h > 0.f);
  assert(sz_view_frame(other).h == 0.f);
  assert(sz_view_hit_test(root, sz_view_frame(inc_btn).x + 4.f,
                          sz_view_frame(inc_btn).y + 4.f) == inc_btn);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 160;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  assert(sz_ui_pump_sync(session));

  hx = sz_view_frame(set_btn).x + 8.f;
  hy = sz_view_frame(set_btn).y + 8.f;
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = hx;
  tap.y = hy;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(page) == 1);
  assert(sz_ui_pump_sync(session));

  sz_view_layout(root, 200.f, 160.f, theme);
  assert(sz_view_frame(home).h == 0.f);
  assert(sz_view_frame(other).h > 0.f);
  assert(sz_view_hit_test(root, sz_view_frame(inc_btn).x + 4.f,
                          sz_view_frame(inc_btn).y + 4.f) != inc_btn);

  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_int_free(page);
  sz_signal_int_free(count);
}

static void test_widgets(void) {
  SzView *col, *scroll, *list, *field, *img, *icon;
  SzSignalStr *draft;
  const SzTheme *theme = sz_theme_default();

  draft = sz_signal_str("hi");
  col = sz_view_column();
  field = sz_view_text_field(draft, "type");
  img = sz_view_image(16, 16, 0xFF112233u, "i");
  icon = sz_view_icon('X', 0xFF000000u);
  list = sz_view_list();
  sz_view_add_child(list, sz_view_text("a"));
  scroll = sz_view_scroll(list);
  sz_view_add_child(col, field);
  sz_view_add_child(col, img);
  sz_view_add_child(col, icon);
  sz_view_add_child(col, scroll);
  sz_view_layout(col, 200.f, 160.f, theme);
  assert(sz_view_kind(field) == SZ_VIEW_TEXT_FIELD);
  assert(sz_view_kind(scroll) == SZ_VIEW_SCROLL);
  assert(sz_view_frame(scroll).h > 0);
  sz_view_free(col);
  sz_signal_str_free(draft);
}

static void test_expanded_column(void) {
  SzView *col, *title, *scroll, *list, *btn, *exp;
  const SzTheme *theme = sz_theme_default();
  float max_h = 280.f;
  float leftover;

  col = sz_view_column();
  title = sz_view_text("Title");
  list = sz_view_list();
  sz_view_add_child(list, sz_view_text("a"));
  scroll = sz_view_scroll(list);
  exp = sz_view_expanded(scroll);
  btn = sz_view_button("Go", NULL, NULL);
  sz_view_add_child(col, title);
  sz_view_add_child(col, exp);
  sz_view_add_child(col, btn);
  sz_view_layout(col, 200.f, max_h, theme);
  assert(sz_view_kind(exp) == SZ_VIEW_EXPANDED);
  assert(sz_view_frame(col).h == max_h);
  leftover = max_h - theme->pad * 2.f - sz_view_frame(title).h -
             sz_view_frame(btn).h - theme->gap * 2.f;
  assert(sz_view_frame(exp).h > 64.f);
  assert(sz_view_frame(exp).h >= leftover - 0.5f);
  assert(sz_view_frame(exp).h <= leftover + 0.5f);
  assert(sz_view_frame(scroll).h >= leftover - 0.5f);
  sz_view_free(col);
}

static void test_expanded_row(void) {
  SzView *row, *left, *mid, *right, *exp;
  const SzTheme *theme = sz_theme_default();
  float max_w = 320.f;
  float leftover;

  row = sz_view_row();
  left = sz_view_button("L", NULL, NULL);
  mid = sz_view_text("mid");
  exp = sz_view_expanded(mid);
  right = sz_view_button("R", NULL, NULL);
  sz_view_add_child(row, left);
  sz_view_add_child(row, exp);
  sz_view_add_child(row, right);
  sz_view_layout(row, max_w, 80.f, theme);
  assert(sz_view_kind(exp) == SZ_VIEW_EXPANDED);
  assert(sz_view_frame(row).w == max_w);
  leftover = max_w - theme->pad * 2.f - sz_view_frame(left).w -
             sz_view_frame(right).w - theme->gap * 2.f;
  assert(sz_view_frame(exp).w >= leftover - 0.5f);
  assert(sz_view_frame(exp).w <= leftover + 0.5f);
  assert(sz_view_frame(mid).w >= leftover - 0.5f);
  sz_view_free(row);
}

static void test_center(void) {
  SzView *center, *child;
  const SzTheme *theme = sz_theme_default();
  float max_w = 300.f;
  float max_h = 200.f;
  SzRect cf, chf;

  child = sz_view_text("Hi");
  center = sz_view_center(child);
  sz_view_layout(center, max_w, max_h, theme);
  assert(sz_view_kind(center) == SZ_VIEW_CENTER);
  cf = sz_view_frame(center);
  chf = sz_view_frame(child);
  assert(cf.w == max_w);
  assert(cf.h == max_h);
  assert(chf.w > 0.f && chf.h > 0.f);
  assert(chf.x >= cf.x - 0.5f);
  assert(chf.y >= cf.y - 0.5f);
  assert(fabsf(chf.x - (cf.x + (cf.w - chf.w) * 0.5f)) < 0.5f);
  assert(fabsf(chf.y - (cf.y + (cf.h - chf.h) * 0.5f)) < 0.5f);
  sz_view_free(center);
}

static void test_align(void) {
  SzView *align, *child;
  const SzTheme *theme = sz_theme_default();
  float max_w = 300.f;
  float max_h = 200.f;
  SzRect af, chf;

  child = sz_view_text("Hi");
  align = sz_view_align(2, 2, child);
  sz_view_layout(align, max_w, max_h, theme);
  assert(sz_view_kind(align) == SZ_VIEW_ALIGN);
  af = sz_view_frame(align);
  chf = sz_view_frame(child);
  assert(af.w == max_w);
  assert(af.h == max_h);
  assert(chf.w > 0.f && chf.h > 0.f);
  assert(fabsf(chf.x - (af.x + af.w - chf.w)) < 0.5f);
  assert(fabsf(chf.y - (af.y + af.h - chf.h)) < 0.5f);
  sz_view_free(align);

  child = sz_view_text("Hi");
  align = sz_view_align(0, 0, child);
  sz_view_layout(align, max_w, max_h, theme);
  chf = sz_view_frame(child);
  af = sz_view_frame(align);
  assert(fabsf(chf.x - af.x) < 0.5f);
  assert(fabsf(chf.y - af.y) < 0.5f);
  sz_view_free(align);
}

static void test_stack(void) {
  SzView *stack, *bot, *top;
  const SzTheme *theme = sz_theme_default();
  SzRect sf, bf, tf;

  stack = sz_view_stack();
  bot = sz_view_image(40, 40, 0xFF112233u, "");
  top = sz_view_icon('+', 0xFFFFFFFFu);
  sz_view_add_child(stack, bot);
  sz_view_add_child(stack, top);
  sz_view_layout(stack, 200.f, 200.f, theme);
  assert(sz_view_kind(stack) == SZ_VIEW_STACK);
  sf = sz_view_frame(stack);
  bf = sz_view_frame(bot);
  tf = sz_view_frame(top);
  assert(sf.w >= bf.w);
  assert(sf.h >= bf.h);
  assert(sf.w < 200.f); /* loose: not fill viewport */
  assert(fabsf(bf.x - tf.x) < 0.5f);
  assert(fabsf(bf.y - tf.y) < 0.5f);
  sz_view_free(stack);
}

static void test_positioned(void) {
  SzView *stack, *bot, *badge, *pos;
  const SzTheme *theme = sz_theme_default();
  SzRect bf, pf;

  stack = sz_view_stack();
  bot = sz_view_image(40, 40, 0xFF112233u, "");
  badge = sz_view_icon('+', 0xFFFFFFFFu);
  pos = sz_view_positioned(10, 6, badge);
  sz_view_add_child(stack, bot);
  sz_view_add_child(stack, pos);
  sz_view_layout(stack, 200.f, 200.f, theme);
  assert(sz_view_kind(pos) == SZ_VIEW_POSITIONED);
  bf = sz_view_frame(bot);
  pf = sz_view_frame(badge);
  assert(fabsf(pf.x - (bf.x + 10.f)) < 0.5f);
  assert(fabsf(pf.y - (bf.y + 6.f)) < 0.5f);
  sz_view_free(stack);
}

static void test_padding(void) {
  SzView *pad, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect pf, chf;
  float inset = 10.f;

  child = sz_view_text("Hi");
  pad = sz_view_padding(10, child);
  sz_view_layout(pad, 200.f, 100.f, theme);
  assert(sz_view_kind(pad) == SZ_VIEW_PADDING);
  pf = sz_view_frame(pad);
  chf = sz_view_frame(child);
  assert(fabsf(chf.x - (pf.x + inset)) < 0.5f);
  assert(fabsf(chf.y - (pf.y + inset)) < 0.5f);
  assert(fabsf(pf.w - (chf.w + inset * 2.f)) < 0.5f);
  assert(fabsf(pf.h - (chf.h + inset * 2.f)) < 0.5f);
  sz_view_free(pad);
}

static void test_sized(void) {
  SzView *box, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf, chf;
  float want_w = 80.f;
  float want_h = 50.f;

  child = sz_view_text("Hi");
  box = sz_view_sized(80, 50, child);
  sz_view_layout(box, 200.f, 200.f, theme);
  assert(sz_view_kind(box) == SZ_VIEW_SIZED);
  bf = sz_view_frame(box);
  chf = sz_view_frame(child);
  assert(fabsf(bf.w - want_w) < 0.5f);
  assert(fabsf(bf.h - want_h) < 0.5f);
  assert(fabsf(chf.w - want_w) < 0.5f);
  assert(fabsf(chf.h - want_h) < 0.5f);
  assert(fabsf(chf.x - bf.x) < 0.5f);
  assert(fabsf(chf.y - bf.y) < 0.5f);
  sz_view_free(box);
}

static void test_min_size(void) {
  SzView *box, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf, chf;

  child = sz_view_text("Hi");
  box = sz_view_min_size(80, 50, child);
  sz_view_layout(box, 200.f, 200.f, theme);
  assert(sz_view_kind(box) == SZ_VIEW_MIN_SIZE);
  bf = sz_view_frame(box);
  chf = sz_view_frame(child);
  assert(bf.w >= 80.f - 0.5f);
  assert(bf.h >= 50.f - 0.5f);
  assert(chf.w >= 80.f - 0.5f);
  assert(chf.h >= 50.f - 0.5f);
  sz_view_free(box);

  child = sz_view_text("Hi");
  box = sz_view_min_size(80, 50, child);
  sz_view_layout(box, 40.f, 30.f, theme);
  bf = sz_view_frame(box);
  assert(bf.w <= 40.f + 0.5f);
  assert(bf.h <= 30.f + 0.5f);
  sz_view_free(box);
}

static void test_max_size_caps_sized(void) {
  SzView *box, *inner, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf, inf, chf;

  child = sz_view_text("Hi");
  inner = sz_view_sized(80, 50, child);
  box = sz_view_max_size(40, 30, inner);
  sz_view_layout(box, 200.f, 200.f, theme);
  assert(sz_view_kind(box) == SZ_VIEW_MAX_SIZE);
  bf = sz_view_frame(box);
  inf = sz_view_frame(inner);
  chf = sz_view_frame(child);
  assert(fabsf(bf.w - 40.f) < 0.5f);
  assert(fabsf(bf.h - 30.f) < 0.5f);
  assert(fabsf(inf.w - 40.f) < 0.5f);
  assert(fabsf(inf.h - 30.f) < 0.5f);
  assert(fabsf(chf.w - 40.f) < 0.5f);
  assert(fabsf(chf.h - 30.f) < 0.5f);
  sz_view_free(box);
}

static void test_max_size_does_not_grow_child(void) {
  SzView *box, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf, chf, loose;

  child = sz_view_text("Hi");
  sz_view_layout(child, 200.f, 200.f, theme);
  loose = sz_view_frame(child);
  sz_view_free(child);

  child = sz_view_text("Hi");
  box = sz_view_max_size(80, 50, child);
  sz_view_layout(box, 200.f, 200.f, theme);
  bf = sz_view_frame(box);
  chf = sz_view_frame(child);
  assert(fabsf(bf.w - loose.w) < 0.5f);
  assert(fabsf(bf.h - loose.h) < 0.5f);
  assert(fabsf(chf.w - loose.w) < 0.5f);
  assert(fabsf(chf.h - loose.h) < 0.5f);
  assert(bf.w + 1.f < 80.f);
  sz_view_free(box);
}

static void test_max_size_zero_axis_is_uncapped(void) {
  SzView *box, *inner, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf;

  child = sz_view_text("Hi");
  inner = sz_view_sized(80, 50, child);
  box = sz_view_max_size(0, 30, inner);
  sz_view_layout(box, 200.f, 200.f, theme);
  bf = sz_view_frame(box);
  assert(fabsf(bf.w - 80.f) < 0.5f);
  assert(fabsf(bf.h - 30.f) < 0.5f);
  sz_view_free(box);

  child = sz_view_text("Hi");
  inner = sz_view_sized(80, 50, child);
  box = sz_view_max_size(40, 0, inner);
  sz_view_layout(box, 200.f, 200.f, theme);
  bf = sz_view_frame(box);
  assert(fabsf(bf.w - 40.f) < 0.5f);
  assert(fabsf(bf.h - 50.f) < 0.5f);
  sz_view_free(box);
}

static void test_incoming_max_wins_over_max_size(void) {
  SzView *box, *inner, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf;

  child = sz_view_text("Hi");
  inner = sz_view_sized(80, 50, child);
  box = sz_view_max_size(60, 40, inner);
  sz_view_layout(box, 20.f, 16.f, theme);
  bf = sz_view_frame(box);
  assert(fabsf(bf.w - 20.f) < 0.5f);
  assert(fabsf(bf.h - 16.f) < 0.5f);
  sz_view_free(box);
}

static void test_min_size_inside_max_size_clamps(void) {
  SzView *cap, *floor, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect cf, ff, chf;

  child = sz_view_text("Hi");
  floor = sz_view_min_size(80, 50, child);
  cap = sz_view_max_size(40, 30, floor);
  sz_view_layout(cap, 200.f, 200.f, theme);
  cf = sz_view_frame(cap);
  ff = sz_view_frame(floor);
  chf = sz_view_frame(child);
  assert(fabsf(cf.w - 40.f) < 0.5f);
  assert(fabsf(cf.h - 30.f) < 0.5f);
  assert(fabsf(ff.w - 40.f) < 0.5f);
  assert(fabsf(ff.h - 30.f) < 0.5f);
  assert(fabsf(chf.w - 40.f) < 0.5f);
  assert(fabsf(chf.h - 30.f) < 0.5f);
  sz_view_free(cap);
}

static void test_max_size_inside_column(void) {
  SzView *col, *box, *inner, *child, *sib;
  const SzTheme *theme = sz_theme_default();
  SzRect bf, sf;

  col = sz_view_column();
  child = sz_view_text("Hi");
  inner = sz_view_sized(80, 50, child);
  box = sz_view_max_size(40, 30, inner);
  sib = sz_view_button("Go", NULL, NULL);
  sz_view_add_child(col, box);
  sz_view_add_child(col, sib);
  sz_view_layout(col, 240.f, 200.f, theme);
  bf = sz_view_frame(box);
  sf = sz_view_frame(sib);
  assert(fabsf(bf.w - 40.f) < 0.5f);
  assert(fabsf(bf.h - 30.f) < 0.5f);
  assert(sf.w + 1.f < 240.f - theme->pad * 2.f);
  sz_view_free(col);
}

static void test_max_size_with_stretch(void) {
  SzView *col, *wrap, *cap, *inner, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect wf, cf;
  float inner_w;

  col = sz_view_column();
  child = sz_view_text("Hi");
  inner = sz_view_sized(10, 50, child);
  cap = sz_view_max_size(0, 20, inner);
  wrap = sz_view_stretch(cap);
  sz_view_add_child(col, wrap);
  sz_view_layout(col, 220.f, 160.f, theme);
  wf = sz_view_frame(wrap);
  cf = sz_view_frame(cap);
  inner_w = 220.f - theme->pad * 2.f;
  assert(fabsf(wf.w - inner_w) < 0.5f);
  assert(fabsf(cf.w - inner_w) < 0.5f);
  assert(fabsf(cf.h - 20.f) < 0.5f);
  sz_view_free(col);
}

static void test_nested_max_size_uses_tighter_cap(void) {
  SzView *outer, *inner, *box, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect of;

  child = sz_view_text("Hi");
  box = sz_view_sized(100, 80, child);
  inner = sz_view_max_size(40, 30, box);
  outer = sz_view_max_size(80, 60, inner);
  sz_view_layout(outer, 200.f, 200.f, theme);
  of = sz_view_frame(outer);
  assert(fabsf(of.w - 40.f) < 0.5f);
  assert(fabsf(of.h - 30.f) < 0.5f);
  assert(fabsf(sz_view_frame(inner).w - 40.f) < 0.5f);
  assert(fabsf(sz_view_frame(box).w - 40.f) < 0.5f);
  sz_view_free(outer);
}

static void test_max_size_inside_row(void) {
  SzView *row, *box, *inner, *child, *sib;
  const SzTheme *theme = sz_theme_default();
  SzRect bf;

  row = sz_view_row();
  child = sz_view_text("Hi");
  inner = sz_view_sized(80, 50, child);
  box = sz_view_max_size(40, 30, inner);
  sib = sz_view_button("Go", NULL, NULL);
  sz_view_add_child(row, box);
  sz_view_add_child(row, sib);
  sz_view_layout(row, 320.f, 80.f, theme);
  bf = sz_view_frame(box);
  assert(fabsf(bf.w - 40.f) < 0.5f);
  assert(fabsf(bf.h - 30.f) < 0.5f);
  sz_view_free(row);
}

static void test_clip_sizes_to_child(void) {
  SzView *clip, *inner, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect cf, inf;

  child = sz_view_text("Hi");
  inner = sz_view_sized(40, 30, child);
  clip = sz_view_clip(inner);
  sz_view_layout(clip, 200.f, 200.f, theme);
  assert(sz_view_kind(clip) == SZ_VIEW_CLIP);
  cf = sz_view_frame(clip);
  inf = sz_view_frame(inner);
  assert(fabsf(cf.w - 40.f) < 0.5f);
  assert(fabsf(cf.h - 30.f) < 0.5f);
  assert(fabsf(inf.w - 40.f) < 0.5f);
  assert(fabsf(inf.h - 30.f) < 0.5f);
  sz_view_free(clip);
}

/* Skia N32 peek is RGBA on Darwin and BGRA on Linux. Map R/B once. */
static int g_px_r = 0;
static int g_px_b = 2;
static int g_px_mapped;

static void px_map_channels(void) {
  SkSurface *surf;
  SkCanvas *canvas;
  SkPaint *paint;
  const uint8_t *px;
  size_t n = 0;
  if (g_px_mapped)
    return;
  g_px_mapped = 1;
  surf = sk_surface_make_raster_n32_premul(4, 4);
  if (!surf)
    return;
  canvas = sk_surface_get_canvas(surf);
  paint = sk_paint_new();
  if (!canvas || !paint) {
    if (paint)
      sk_paint_delete(paint);
    sk_surface_unref(surf);
    return;
  }
  sk_paint_set_color(paint, sk_color_rgba(255, 0, 0, 255));
  sk_canvas_draw_rect(canvas, 0, 0, 4, 4, paint);
  px = sk_surface_peek_pixels(surf, &n);
  if (px && n >= 4 && px[2] > 200 && px[0] < 50) {
    g_px_r = 2;
    g_px_b = 0;
  }
  sk_paint_delete(paint);
  sk_surface_unref(surf);
}

static int px_rgb(const uint8_t *px, int w, int x, int y, uint8_t r, uint8_t g,
                  uint8_t b) {
  const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
  px_map_channels();
  return p[g_px_r] == r && p[1] == g && p[g_px_b] == b;
}

static void test_clip_paint_contains_overflow(void) {
  SzView *root, *clip, *body;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  body = sz_view_background(0xFF00AA00u, sz_view_sized(20, 80, sz_view_text("x")));
  clip = sz_view_clip(sz_view_scroll(body));
  root = sz_view_sized(40, 40, clip);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Scroll child sits at pad 12 inside the 40×40 clip. */
  assert(px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  /* Same canvas, outside the clip frame: theme background. */
  assert(px_rgb(px, 80, 20, 50, 0xF5, 0xF5, 0xF5));
  assert(px_rgb(px, 80, 50, 20, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_opacity_sizes_to_child(void) {
  SzView *wrap, *inner, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect wf, inf;

  child = sz_view_text("Hi");
  inner = sz_view_sized(40, 30, child);
  wrap = sz_view_opacity(50, inner);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(sz_view_kind(wrap) == SZ_VIEW_OPACITY);
  wf = sz_view_frame(wrap);
  inf = sz_view_frame(inner);
  assert(fabsf(wf.w - 40.f) < 0.5f);
  assert(fabsf(wf.h - 30.f) < 0.5f);
  assert(fabsf(inf.w - 40.f) < 0.5f);
  assert(fabsf(inf.h - 30.f) < 0.5f);
  sz_view_free(wrap);
}

static SzView *opacity_green_box(int pct) {
  return sz_view_sized(
      40, 40,
      sz_view_opacity(pct, sz_view_background(0xFF00AA00u, sz_view_sized(
                                                               40, 40, sz_view_text("x")))));
}

static void test_opacity_paint_scales_alpha(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  uint8_t r50, g50, b50;

  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);

  root = opacity_green_box(100);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  sz_view_free(root);

  root = opacity_green_box(0);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px_rgb(px, 80, 20, 20, 0xF5, 0xF5, 0xF5));
  sz_view_free(root);

  root = opacity_green_box(50);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(!px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  assert(!px_rgb(px, 80, 20, 20, 0xF5, 0xF5, 0xF5));
  r50 = px[(20 * 80 + 20) * 4];
  g50 = px[(20 * 80 + 20) * 4 + 1];
  b50 = px[(20 * 80 + 20) * 4 + 2];
  assert(g50 > 0xAA && g50 < 0xF5);
  assert(r50 > 0x00 && r50 < 0xF5);
  (void)b50;
  sz_view_free(root);

  root = sz_view_sized(
      40, 40,
      sz_view_opacity(50, sz_view_opacity(50, sz_view_background(
                                                 0xFF00AA00u, sz_view_sized(40, 40, sz_view_text("x"))))));
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px[(20 * 80 + 20) * 4 + 1] > g50);
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_background(void) {
  SzView *bg, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf, chf;

  child = sz_view_text("Hi");
  bg = sz_view_background(0xFFE6F0F8u, child);
  sz_view_layout(bg, 200.f, 100.f, theme);
  assert(sz_view_kind(bg) == SZ_VIEW_BACKGROUND);
  bf = sz_view_frame(bg);
  chf = sz_view_frame(child);
  assert(fabsf(bf.w - chf.w) < 0.5f);
  assert(fabsf(bf.h - chf.h) < 0.5f);
  assert(fabsf(chf.x - bf.x) < 0.5f);
  assert(fabsf(chf.y - bf.y) < 0.5f);
  sz_view_free(bg);
}

static void test_aspect_ratio(void) {
  SzView *box, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf;

  child = sz_view_text("Hi");
  box = sz_view_aspect_ratio(16, 9, child);
  sz_view_layout(box, 160.f, 200.f, theme);
  assert(sz_view_kind(box) == SZ_VIEW_ASPECT_RATIO);
  bf = sz_view_frame(box);
  assert(fabsf(bf.w - 160.f) < 0.5f);
  assert(fabsf(bf.h - 90.f) < 0.5f);
  assert(fabsf(sz_view_frame(child).w - 160.f) < 0.5f);
  assert(fabsf(sz_view_frame(child).h - 90.f) < 0.5f);
  sz_view_free(box);

  child = sz_view_text("Hi");
  box = sz_view_aspect_ratio(16, 9, child);
  sz_view_layout(box, 320.f, 90.f, theme);
  bf = sz_view_frame(box);
  assert(fabsf(bf.w - 160.f) < 0.5f);
  assert(fabsf(bf.h - 90.f) < 0.5f);
  sz_view_free(box);
}

static void test_fraction(void) {
  SzView *box, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf, chf;

  child = sz_view_text("Hi");
  box = sz_view_fraction(50, 0, child);
  sz_view_layout(box, 200.f, 100.f, theme);
  assert(sz_view_kind(box) == SZ_VIEW_FRACTION);
  bf = sz_view_frame(box);
  chf = sz_view_frame(child);
  assert(fabsf(bf.w - 100.f) < 0.5f);
  assert(fabsf(bf.h - chf.h) < 0.5f);
  sz_view_free(box);

  child = sz_view_text("Hi");
  box = sz_view_fraction(50, 50, child);
  sz_view_layout(box, 200.f, 100.f, theme);
  bf = sz_view_frame(box);
  chf = sz_view_frame(child);
  assert(fabsf(bf.w - 100.f) < 0.5f);
  assert(fabsf(bf.h - 50.f) < 0.5f);
  assert(fabsf(chf.w - 100.f) < 0.5f);
  assert(fabsf(chf.h - 50.f) < 0.5f);
  sz_view_free(box);
}

static void test_expanded_text_fills_tight_slot(void) {
  SzView *col, *title, *body, *btn, *exp;
  const SzTheme *theme = sz_theme_default();
  float max_h = 280.f;
  float leftover;

  col = sz_view_column();
  title = sz_view_text("Title");
  body = sz_view_text("Hi");
  exp = sz_view_expanded(body);
  btn = sz_view_button("Go", NULL, NULL);
  sz_view_add_child(col, title);
  sz_view_add_child(col, exp);
  sz_view_add_child(col, btn);
  sz_view_layout(col, 200.f, max_h, theme);
  leftover = max_h - theme->pad * 2.f - sz_view_frame(title).h -
             sz_view_frame(btn).h - theme->gap * 2.f;
  assert(fabsf(sz_view_frame(exp).h - leftover) < 0.5f);
  assert(fabsf(sz_view_frame(body).h - leftover) < 0.5f);
  assert(fabsf(sz_view_frame(body).w - sz_view_frame(exp).w) < 0.5f);
  assert(fabsf(sz_view_frame(body).x - sz_view_frame(exp).x) < 0.5f);
  assert(fabsf(sz_view_frame(body).y - sz_view_frame(exp).y) < 0.5f);
  sz_view_free(col);
}

static void test_min_size_inside_expanded(void) {
  SzView *col, *title, *child, *box, *exp;
  const SzTheme *theme = sz_theme_default();
  float max_h = 240.f;

  col = sz_view_column();
  title = sz_view_text("T");
  child = sz_view_text("Hi");
  box = sz_view_min_size(0, 40, child);
  exp = sz_view_expanded(box);
  sz_view_add_child(col, title);
  sz_view_add_child(col, exp);
  sz_view_layout(col, 180.f, max_h, theme);
  assert(sz_view_frame(exp).h > 40.f);
  assert(fabsf(sz_view_frame(box).h - sz_view_frame(exp).h) < 0.5f);
  assert(fabsf(sz_view_frame(child).h - sz_view_frame(exp).h) < 0.5f);
  sz_view_free(col);
}

static void test_column_non_flex_stays_intrinsic(void) {
  SzView *col, *label, *btn;
  const SzTheme *theme = sz_theme_default();
  SzRect cf, lf, bf;
  float inner;

  col = sz_view_column();
  label = sz_view_text("Hi");
  btn = sz_view_button("Go", NULL, NULL);
  sz_view_add_child(col, label);
  sz_view_add_child(col, btn);
  sz_view_layout(col, 240.f, 200.f, theme);
  cf = sz_view_frame(col);
  lf = sz_view_frame(label);
  bf = sz_view_frame(btn);
  inner = cf.w - theme->pad * 2.f;
  assert(fabsf(cf.w - 240.f) < 0.5f);
  assert(lf.w + 1.f < inner);
  assert(bf.w + 1.f < inner);
  sz_view_free(col);
}

static void test_row_non_flex_stays_intrinsic(void) {
  SzView *row, *left, *mid, *right;
  const SzTheme *theme = sz_theme_default();
  SzRect rf, mf;
  float inner;

  row = sz_view_row();
  left = sz_view_button("L", NULL, NULL);
  mid = sz_view_text("mid");
  right = sz_view_button("R", NULL, NULL);
  sz_view_add_child(row, left);
  sz_view_add_child(row, mid);
  sz_view_add_child(row, right);
  sz_view_layout(row, 320.f, 80.f, theme);
  rf = sz_view_frame(row);
  mf = sz_view_frame(mid);
  inner = rf.w - theme->pad * 2.f;
  assert(fabsf(rf.w - 320.f) < 0.5f);
  assert(mf.w + 1.f < inner);
  sz_view_free(row);
}

static void test_row_overflow_keeps_button_width(void) {
  SzView *row, *solo, *first, *last;
  const SzTheme *theme = sz_theme_default();
  float want;
  int i;

  solo = sz_view_button("Complete", NULL, NULL);
  sz_view_layout(solo, 400.f, 80.f, theme);
  want = sz_view_frame(solo).w;
  sz_view_free(solo);

  row = sz_view_row();
  first = sz_view_button("Complete", NULL, NULL);
  sz_view_add_child(row, first);
  for (i = 0; i < 6; i++)
    sz_view_add_child(row, sz_view_button("Complete", NULL, NULL));
  last = sz_view_button("Complete", NULL, NULL);
  sz_view_add_child(row, last);
  sz_view_layout(row, 200.f, 80.f, theme);
  assert(fabsf(sz_view_frame(first).w - want) < 0.5f);
  assert(fabsf(sz_view_frame(last).w - want) < 0.5f);
  sz_view_free(row);
}

static SzView *wrap_two_sized(int w1, int h1, int w2, int h2) {
  SzView *flow = sz_view_wrap();
  sz_view_add_child(flow, sz_view_sized(w1, h1, sz_view_text("a")));
  sz_view_add_child(flow, sz_view_sized(w2, h2, sz_view_text("b")));
  return flow;
}

static void test_wrap_kind(void) {
  SzView *flow = sz_view_wrap();
  assert(sz_view_kind(flow) == SZ_VIEW_WRAP);
  sz_view_free(flow);
}

static void test_wrap_one_run_when_wide(void) {
  SzView *flow, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf, ff;

  flow = sz_view_wrap();
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(flow, a);
  sz_view_add_child(flow, b);
  sz_view_layout(flow, 200.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  ff = sz_view_frame(flow);
  assert(fabsf(bf.y - af.y) < 0.5f);
  assert(fabsf(bf.x - (af.x + af.w) - theme->gap) < 0.5f);
  assert(fabsf(ff.w - (theme->pad * 2.f + 20.f + theme->gap + 20.f)) < 0.5f);
  assert(fabsf(ff.h - (theme->pad * 2.f + 10.f)) < 0.5f);
  sz_view_free(flow);
}

static void test_wrap_sizes_to_runs_not_max(void) {
  SzView *flow = wrap_two_sized(20, 10, 20, 10);
  const SzTheme *theme = sz_theme_default();
  SzRect ff;

  sz_view_layout(flow, 200.f, 80.f, theme);
  ff = sz_view_frame(flow);
  assert(ff.w + 1.f < 200.f);
  sz_view_free(flow);
}

static void test_wrap_second_run_when_narrow(void) {
  SzView *flow, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf, ff;
  float inner;

  flow = sz_view_wrap();
  a = sz_view_sized(40, 10, sz_view_text("a"));
  b = sz_view_sized(40, 10, sz_view_text("b"));
  sz_view_add_child(flow, a);
  sz_view_add_child(flow, b);
  /* pad 12*2 + 40 + gap 8 + 40 = 112. Cap below that so b wraps. */
  sz_view_layout(flow, 100.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  ff = sz_view_frame(flow);
  inner = 100.f - theme->pad * 2.f;
  assert(af.w <= inner + 0.5f);
  assert(bf.y > af.y + af.h + 0.5f);
  assert(fabsf(bf.x - af.x) < 0.5f);
  assert(fabsf(bf.y - (af.y + af.h) - theme->gap) < 0.5f);
  assert(fabsf(ff.w - (theme->pad * 2.f + 40.f)) < 0.5f);
  assert(fabsf(ff.h - (theme->pad * 2.f + 10.f + theme->gap + 10.f)) < 0.5f);
  sz_view_free(flow);
}

static void test_wrap_unbounded_stays_one_run(void) {
  SzView *flow, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  flow = sz_view_wrap();
  a = sz_view_sized(40, 10, sz_view_text("a"));
  b = sz_view_sized(40, 10, sz_view_text("b"));
  sz_view_add_child(flow, a);
  sz_view_add_child(flow, b);
  sz_view_layout(flow, 0.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(bf.y - af.y) < 0.5f);
  sz_view_free(flow);
}

static void test_wrap_empty_is_pad(void) {
  SzView *flow = sz_view_wrap();
  const SzTheme *theme = sz_theme_default();
  SzRect ff;

  sz_view_layout(flow, 200.f, 80.f, theme);
  ff = sz_view_frame(flow);
  assert(fabsf(ff.w - theme->pad * 2.f) < 0.5f);
  assert(fabsf(ff.h - theme->pad * 2.f) < 0.5f);
  sz_view_free(flow);
}

static void test_wrap_a11y_dumps_children(void) {
  SzView *flow = sz_view_wrap();
  SzString *dump;

  sz_view_add_child(flow, sz_view_button("Home", NULL, NULL));
  sz_view_add_child(flow, sz_view_button("Tasks", NULL, NULL));
  dump = sz_view_a11y_dump(flow);
  assert(strstr(sz_string_cstr(dump), "button:Home") != NULL);
  assert(strstr(sz_string_cstr(dump), "button:Tasks") != NULL);
  sz_string_free(dump);
  sz_view_free(flow);
}

static void test_wrap_hit_test_second_run(void) {
  SzView *flow, *a, *b, *hit;
  const SzTheme *theme = sz_theme_default();
  SzRect bf;

  flow = sz_view_wrap();
  a = sz_view_button("A", NULL, NULL);
  b = sz_view_button("B", NULL, NULL);
  sz_view_add_child(flow, a);
  sz_view_add_child(flow, b);
  sz_view_layout(flow, 90.f, 120.f, theme);
  bf = sz_view_frame(b);
  assert(bf.y > sz_view_frame(a).y + 0.5f);
  hit = sz_view_hit_test(flow, bf.x + 2.f, bf.y + 2.f);
  assert(hit == b);
  sz_view_free(flow);
}

static void test_wrap_gap_zero_is_flush(void) {
  SzView *flow, *a, *b, *g;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  flow = sz_view_wrap();
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(flow, a);
  sz_view_add_child(flow, b);
  g = sz_view_gap(0, flow);
  sz_view_layout(g, 200.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(bf.x - (af.x + af.w)) < 0.5f);
  sz_view_free(g);
}

static void test_wrap_gap_spaces_runs(void) {
  SzView *flow, *a, *b, *g;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  flow = sz_view_wrap();
  a = sz_view_sized(40, 10, sz_view_text("a"));
  b = sz_view_sized(40, 10, sz_view_text("b"));
  sz_view_add_child(flow, a);
  sz_view_add_child(flow, b);
  g = sz_view_gap(16, flow);
  sz_view_layout(g, 100.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(bf.y > af.y + af.h + 0.5f);
  assert(fabsf(bf.y - (af.y + af.h) - 16.f) < 0.5f);
  sz_view_free(g);
}

static void test_wrap_show_when_skips(void) {
  SzView *flow, *a, *mid, *b;
  SzSignalInt *page;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  page = sz_signal_int(1);
  flow = sz_view_wrap();
  a = sz_view_sized(20, 10, sz_view_text("a"));
  mid = sz_view_sized(40, 10, sz_view_text("mid"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(flow, a);
  sz_view_add_child(flow, sz_view_show_when(page, 0, mid));
  sz_view_add_child(flow, b);
  sz_view_layout(flow, 200.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(bf.y - af.y) < 0.5f);
  assert(fabsf(bf.x - (af.x + af.w) - theme->gap) < 0.5f);
  sz_view_free(flow);
  sz_signal_int_free(page);
}

static void test_wrap_in_column_grows_height(void) {
  SzView *col, *flow, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect cf, ff;
  float one_run_h;

  flow = sz_view_wrap();
  a = sz_view_sized(40, 10, sz_view_text("a"));
  b = sz_view_sized(40, 10, sz_view_text("b"));
  sz_view_add_child(flow, a);
  sz_view_add_child(flow, b);
  col = sz_view_column();
  sz_view_add_child(col, flow);
  sz_view_layout(col, 200.f, 200.f, theme);
  one_run_h = sz_view_frame(flow).h;
  sz_view_layout(col, 100.f, 200.f, theme);
  ff = sz_view_frame(flow);
  cf = sz_view_frame(col);
  assert(ff.h > one_run_h + 0.5f);
  assert(cf.h > ff.h - 0.5f);
  sz_view_free(col);
}

static SzView *grid_two_sized(int cols, int w1, int h1, int w2, int h2) {
  SzView *g = sz_view_grid(cols);
  sz_view_add_child(g, sz_view_sized(w1, h1, sz_view_text("a")));
  sz_view_add_child(g, sz_view_sized(w2, h2, sz_view_text("b")));
  return g;
}

static void test_grid_kind(void) {
  SzView *g = sz_view_grid(2);
  assert(sz_view_kind(g) == SZ_VIEW_GRID);
  sz_view_free(g);
}

static void test_grid_two_cols_one_row(void) {
  SzView *g, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf, gf;
  float inner;
  float cell;

  g = sz_view_grid(2);
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(g, a);
  sz_view_add_child(g, b);
  sz_view_layout(g, 200.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  gf = sz_view_frame(g);
  inner = 200.f - theme->pad * 2.f;
  cell = (inner - theme->gap) / 2.f;
  assert(fabsf(bf.y - af.y) < 0.5f);
  assert(fabsf(bf.x - (af.x + cell) - theme->gap) < 0.5f);
  assert(fabsf(gf.w - 200.f) < 0.5f);
  assert(fabsf(gf.h - (theme->pad * 2.f + 10.f)) < 0.5f);
  sz_view_free(g);
}

static void test_grid_third_child_new_row(void) {
  SzView *g, *a, *b, *c;
  const SzTheme *theme = sz_theme_default();
  SzRect af, cf, gf;

  g = sz_view_grid(2);
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  c = sz_view_sized(20, 10, sz_view_text("c"));
  sz_view_add_child(g, a);
  sz_view_add_child(g, b);
  sz_view_add_child(g, c);
  sz_view_layout(g, 200.f, 80.f, theme);
  af = sz_view_frame(a);
  cf = sz_view_frame(c);
  gf = sz_view_frame(g);
  assert(fabsf(cf.x - af.x) < 0.5f);
  assert(fabsf(cf.y - (af.y + af.h) - theme->gap) < 0.5f);
  assert(fabsf(gf.h - (theme->pad * 2.f + 10.f + theme->gap + 10.f)) < 0.5f);
  sz_view_free(g);
}

static void test_grid_fills_max_width(void) {
  SzView *g = grid_two_sized(2, 20, 10, 20, 10);
  const SzTheme *theme = sz_theme_default();
  SzRect gf;

  sz_view_layout(g, 200.f, 80.f, theme);
  gf = sz_view_frame(g);
  assert(fabsf(gf.w - 200.f) < 0.5f);
  sz_view_free(g);
}

static void test_grid_unbounded_sizes_to_cells(void) {
  SzView *g, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf, gf;

  g = sz_view_grid(2);
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(g, a);
  sz_view_add_child(g, b);
  sz_view_layout(g, 0.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  gf = sz_view_frame(g);
  assert(fabsf(bf.y - af.y) < 0.5f);
  assert(fabsf(bf.x - (af.x + af.w) - theme->gap) < 0.5f);
  assert(fabsf(gf.w - (theme->pad * 2.f + 20.f + theme->gap + 20.f)) < 0.5f);
  sz_view_free(g);
}

static void test_grid_empty_is_pad(void) {
  SzView *g = sz_view_grid(3);
  const SzTheme *theme = sz_theme_default();
  SzRect gf;

  sz_view_layout(g, 200.f, 80.f, theme);
  gf = sz_view_frame(g);
  assert(fabsf(gf.w - 200.f) < 0.5f);
  assert(fabsf(gf.h - theme->pad * 2.f) < 0.5f);
  sz_view_free(g);
}

static void test_grid_a11y_dumps_children(void) {
  SzView *g = sz_view_grid(2);
  SzString *dump;

  sz_view_add_child(g, sz_view_button("Home", NULL, NULL));
  sz_view_add_child(g, sz_view_button("Tasks", NULL, NULL));
  dump = sz_view_a11y_dump(g);
  assert(strstr(sz_string_cstr(dump), "button:Home") != NULL);
  assert(strstr(sz_string_cstr(dump), "button:Tasks") != NULL);
  sz_string_free(dump);
  sz_view_free(g);
}

static void test_grid_hit_test_second_row(void) {
  SzView *g, *a, *b, *hit;
  const SzTheme *theme = sz_theme_default();
  SzRect bf;

  g = sz_view_grid(1);
  a = sz_view_button("A", NULL, NULL);
  b = sz_view_button("B", NULL, NULL);
  sz_view_add_child(g, a);
  sz_view_add_child(g, b);
  sz_view_layout(g, 200.f, 120.f, theme);
  bf = sz_view_frame(b);
  assert(bf.y > sz_view_frame(a).y + 0.5f);
  hit = sz_view_hit_test(g, bf.x + 2.f, bf.y + 2.f);
  assert(hit == b);
  sz_view_free(g);
}

static void test_grid_gap_zero_is_flush(void) {
  SzView *g, *a, *b, *wrap;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  g = sz_view_grid(2);
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(g, a);
  sz_view_add_child(g, b);
  wrap = sz_view_gap(0, g);
  sz_view_layout(wrap, 0.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(bf.x - (af.x + af.w)) < 0.5f);
  sz_view_free(wrap);
}

static void test_grid_gap_spaces_rows(void) {
  SzView *g, *a, *b, *wrap;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  g = sz_view_grid(1);
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(g, a);
  sz_view_add_child(g, b);
  wrap = sz_view_gap(16, g);
  sz_view_layout(wrap, 200.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(bf.y > af.y + af.h + 0.5f);
  assert(fabsf(bf.y - (af.y + af.h) - 16.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_grid_show_when_skips_cell(void) {
  SzView *g, *a, *mid, *b;
  SzSignalInt *page;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;
  float inner;
  float cell;

  page = sz_signal_int(1);
  g = sz_view_grid(2);
  a = sz_view_sized(20, 10, sz_view_text("a"));
  mid = sz_view_sized(40, 10, sz_view_text("mid"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(g, a);
  sz_view_add_child(g, sz_view_show_when(page, 0, mid));
  sz_view_add_child(g, b);
  sz_view_layout(g, 200.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  inner = 200.f - theme->pad * 2.f;
  cell = (inner - theme->gap) / 2.f;
  assert(fabsf(bf.y - af.y) < 0.5f);
  assert(fabsf(bf.x - (af.x + cell) - theme->gap) < 0.5f);
  sz_view_free(g);
  sz_signal_int_free(page);
}

static void test_grid_cols_less_than_one_is_one(void) {
  SzView *g, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  g = sz_view_grid(0);
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(g, a);
  sz_view_add_child(g, b);
  sz_view_layout(g, 200.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(bf.y > af.y + af.h + 0.5f);
  sz_view_free(g);
}

static void test_grid_in_column_grows_height(void) {
  SzView *col, *g, *a, *b, *c;
  const SzTheme *theme = sz_theme_default();
  SzRect cf, gf;
  float one_row_h;

  g = sz_view_grid(2);
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  c = sz_view_sized(20, 10, sz_view_text("c"));
  sz_view_add_child(g, a);
  sz_view_add_child(g, b);
  col = sz_view_column();
  sz_view_add_child(col, g);
  sz_view_layout(col, 200.f, 200.f, theme);
  one_row_h = sz_view_frame(g).h;
  sz_view_add_child(g, c);
  sz_view_layout(col, 200.f, 200.f, theme);
  gf = sz_view_frame(g);
  cf = sz_view_frame(col);
  assert(gf.h > one_row_h + 0.5f);
  assert(cf.h > gf.h - 0.5f);
  sz_view_free(col);
}

static SzView *scroll_h_wide_row(SzView **row, SzView **left, SzView **right) {
  *row = sz_view_row();
  *left = sz_view_sized(80, 16, sz_view_text("L"));
  *right = sz_view_sized(80, 16, sz_view_text("R"));
  sz_view_add_child(*row, *left);
  sz_view_add_child(*row, *right);
  return sz_view_scroll_h(*row);
}

static void test_scroll_h_kind(void) {
  SzView *scroll, *row, *left, *right, *vert;
  SzString *dump;

  scroll = scroll_h_wide_row(&row, &left, &right);
  vert = sz_view_scroll(sz_view_text("v"));
  (void)row;
  (void)left;
  (void)right;
  assert(sz_view_kind(scroll) == SZ_VIEW_SCROLL);
  assert(sz_view_scroll_is_h(scroll));
  assert(!sz_view_scroll_is_h(vert));
  dump = sz_view_a11y_dump(scroll);
  assert(strstr(sz_string_cstr(dump), "scroll:scrollh") != NULL);
  sz_string_free(dump);
  sz_view_free(scroll);
  sz_view_free(vert);
}

static void test_scroll_h_sizes_viewport(void) {
  SzView *scroll, *left, *right, *row;
  const SzTheme *theme = sz_theme_default();
  SzRect sf, rf;

  scroll = scroll_h_wide_row(&row, &left, &right);
  (void)left;
  (void)right;
  sz_view_layout(scroll, 80.f, 200.f, theme);
  sf = sz_view_frame(scroll);
  rf = sz_view_frame(row);
  assert(fabsf(sf.w - 80.f) < 0.5f);
  assert(rf.w > sf.w + 0.5f);
  assert(sf.h + 1.f < 200.f);
  assert(fabsf(sf.h - (rf.h + theme->pad * 2.f)) < 0.5f);
  sz_view_free(scroll);
}

static void test_scroll_h_unbounded_fits_child(void) {
  SzView *scroll, *left, *right, *row;
  const SzTheme *theme = sz_theme_default();
  SzRect sf, rf;

  scroll = scroll_h_wide_row(&row, &left, &right);
  (void)left;
  (void)right;
  sz_view_layout(scroll, 0.f, 80.f, theme);
  sf = sz_view_frame(scroll);
  rf = sz_view_frame(row);
  assert(fabsf(sf.w - (rf.w + theme->pad * 2.f)) < 0.5f);
  sz_view_free(scroll);
}

static void test_scroll_h_scroll_by_pans_x(void) {
  SzView *h, *v, *row, *left, *right;
  const SzTheme *theme = sz_theme_default();

  h = scroll_h_wide_row(&row, &left, &right);
  v = sz_view_scroll(sz_view_text("v"));
  (void)row;
  (void)left;
  (void)right;
  sz_view_layout(h, 80.f, 80.f, theme);
  sz_view_layout(v, 80.f, 80.f, theme);
  assert(sz_view_scroll_x(h) == 0.f);
  assert(sz_view_scroll_y(h) == 0.f);
  sz_view_scroll_by(h, 40.f);
  assert(sz_view_scroll_x(h) == 40.f);
  assert(sz_view_scroll_y(h) == 0.f);
  sz_view_scroll_by(h, -80.f);
  assert(sz_view_scroll_x(h) == 0.f);
  sz_view_scroll_by(v, 12.f);
  assert(sz_view_scroll_y(v) == 12.f);
  assert(sz_view_scroll_x(v) == 0.f);
  sz_view_free(h);
  sz_view_free(v);
}

static void test_scroll_h_hit_test(void) {
  SzView *row, *left, *right, *scroll;
  const SzTheme *theme = sz_theme_default();
  SzRect sf, lf, rf;

  row = sz_view_row();
  left = sz_view_button("L", NULL, NULL);
  right = sz_view_button("R", NULL, NULL);
  sz_view_add_child(row, left);
  sz_view_add_child(row, right);
  scroll = sz_view_scroll_h(row);
  /* Two 48px buttons + pads + gap exceed 60. Right starts past the viewport. */
  sz_view_layout(scroll, 60.f, 80.f, theme);
  sf = sz_view_frame(scroll);
  lf = sz_view_frame(left);
  rf = sz_view_frame(right);
  assert(sz_view_hit_test(scroll, lf.x + 4.f, lf.y + 4.f) == left);
  assert(rf.x >= sf.x + sf.w - 0.5f);
  assert(sz_view_hit_test(scroll, rf.x + 4.f, rf.y + 4.f) == NULL);
  sz_view_scroll_by(scroll, 50.f);
  sz_view_layout(scroll, 60.f, 80.f, theme);
  rf = sz_view_frame(right);
  assert(sz_view_hit_test(scroll, rf.x + 4.f, rf.y + 4.f) == right);
  sz_view_free(scroll);
}

static void test_scroll_h_pointer_drag(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *scroll, *row, *left, *right;
  SzInputEvent ev;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  float x0;

  scroll = scroll_h_wide_row(&row, &left, &right);
  (void)row;
  (void)left;
  (void)right;
  root = sz_view_column();
  sz_view_add_child(root, scroll);
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 80;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(scroll);
  x0 = sz_view_scroll_x(scroll);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_DOWN;
  ev.x = f.x + 8.f;
  ev.y = f.y + 8.f;
  assert(sz_ui_inject_sync(session, &ev));
  ev.pointer_phase = SZ_POINTER_MOVE;
  ev.x = f.x + 8.f - 20.f; /* finger left → content left */
  assert(sz_ui_inject_sync(session, &ev));
  ev.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_view_scroll_x(scroll) > x0);
  assert(sz_view_scroll_y(scroll) == 0.f);
  sz_ui_unmount(session);
}

static void test_scroll_h_inject_script(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *scroll, *row, *left, *right;
  const char *path = "/tmp/scuzz_ui_inject_scroll_h.script";
  float x0;

  remove(path);
  scroll = scroll_h_wide_row(&row, &left, &right);
  (void)row;
  (void)left;
  (void)right;
  root = sz_view_column();
  sz_view_add_child(root, scroll);
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 80;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_pump_sync(session));
  x0 = sz_view_scroll_x(scroll);
  write_stamp(path, "scroll 40\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_scroll_x(scroll) == x0 + 40.f);
  assert(sz_view_scroll_y(scroll) == 0.f);
  sz_ui_unmount(session);
  remove(path);
}

static void test_sized_inside_column_keeps_box(void) {
  SzView *col, *box, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf;

  col = sz_view_column();
  child = sz_view_text("Hi");
  box = sz_view_sized(80, 50, child);
  sz_view_add_child(col, box);
  sz_view_layout(col, 240.f, 200.f, theme);
  bf = sz_view_frame(box);
  assert(fabsf(bf.w - 80.f) < 0.5f);
  assert(fabsf(bf.h - 50.f) < 0.5f);
  assert(fabsf(sz_view_frame(child).w - 80.f) < 0.5f);
  assert(fabsf(sz_view_frame(child).h - 50.f) < 0.5f);
  sz_view_free(col);
}

static void test_padding_forwards_min_size(void) {
  SzView *box, *pad, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf, pf, chf;
  float inset = 10.f;

  child = sz_view_text("Hi");
  pad = sz_view_padding(10, child);
  box = sz_view_min_size(80, 50, pad);
  sz_view_layout(box, 200.f, 200.f, theme);
  bf = sz_view_frame(box);
  pf = sz_view_frame(pad);
  chf = sz_view_frame(child);
  assert(bf.w >= 80.f - 0.5f);
  assert(bf.h >= 50.f - 0.5f);
  assert(fabsf(pf.w - bf.w) < 0.5f);
  assert(fabsf(pf.h - bf.h) < 0.5f);
  assert(fabsf(chf.w - (pf.w - inset * 2.f)) < 0.5f);
  assert(fabsf(chf.h - (pf.h - inset * 2.f)) < 0.5f);
  sz_view_free(box);
}

static void test_fraction_width_only_tightens_width(void) {
  SzView *box, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf, chf;
  float intrinsic_h;

  child = sz_view_text("Hi");
  box = sz_view_fraction(50, 0, child);
  sz_view_layout(box, 200.f, 100.f, theme);
  bf = sz_view_frame(box);
  chf = sz_view_frame(child);
  intrinsic_h = chf.h;
  assert(fabsf(bf.w - 100.f) < 0.5f);
  assert(fabsf(chf.w - 100.f) < 0.5f);
  assert(fabsf(bf.h - intrinsic_h) < 0.5f);
  assert(chf.h < 100.f - 0.5f);
  sz_view_free(box);
}

static void test_background_forwards_tight_slot(void) {
  SzView *box, *bg, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect bf, gf, chf;

  child = sz_view_text("Hi");
  bg = sz_view_background(0xFFE6F0F8u, child);
  box = sz_view_sized(90, 60, bg);
  sz_view_layout(box, 200.f, 200.f, theme);
  bf = sz_view_frame(box);
  gf = sz_view_frame(bg);
  chf = sz_view_frame(child);
  assert(fabsf(bf.w - 90.f) < 0.5f);
  assert(fabsf(bf.h - 60.f) < 0.5f);
  assert(fabsf(gf.w - 90.f) < 0.5f);
  assert(fabsf(gf.h - 60.f) < 0.5f);
  assert(fabsf(chf.w - 90.f) < 0.5f);
  assert(fabsf(chf.h - 60.f) < 0.5f);
  sz_view_free(box);
}

static void test_center_second_pass_keeps_child_size(void) {
  SzView *wrap, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect wf, chf;

  child = sz_view_text("Hi");
  wrap = sz_view_center(child);
  sz_view_layout(wrap, 200.f, 160.f, theme);
  wf = sz_view_frame(wrap);
  chf = sz_view_frame(child);
  assert(fabsf(wf.w - 200.f) < 0.5f);
  assert(fabsf(wf.h - 160.f) < 0.5f);
  assert(chf.w + 1.f < wf.w);
  assert(chf.h + 1.f < wf.h);
  assert(chf.x > wf.x + 0.5f);
  assert(chf.y > wf.y + 0.5f);
  sz_view_free(wrap);
}

static void test_expanded_row_text_fills_width(void) {
  SzView *row, *left, *mid, *right, *exp;
  const SzTheme *theme = sz_theme_default();
  float max_w = 320.f;
  float leftover;

  row = sz_view_row();
  left = sz_view_button("L", NULL, NULL);
  mid = sz_view_text("mid");
  exp = sz_view_expanded(mid);
  right = sz_view_button("R", NULL, NULL);
  sz_view_add_child(row, left);
  sz_view_add_child(row, exp);
  sz_view_add_child(row, right);
  sz_view_layout(row, max_w, 80.f, theme);
  leftover = max_w - theme->pad * 2.f - sz_view_frame(left).w -
             sz_view_frame(right).w - theme->gap * 2.f;
  assert(fabsf(sz_view_frame(exp).w - leftover) < 0.5f);
  assert(fabsf(sz_view_frame(mid).w - leftover) < 0.5f);
  assert(fabsf(sz_view_frame(mid).h - sz_view_frame(exp).h) < 0.5f);
  sz_view_free(row);
}

static void test_stretch_column_fills_width(void) {
  SzView *col, *label, *wrap, *sib;
  const SzTheme *theme = sz_theme_default();
  SzRect cf, wf, lf, sf;
  float inner;

  col = sz_view_column();
  label = sz_view_text("Hi");
  wrap = sz_view_stretch(label);
  sib = sz_view_button("Go", NULL, NULL);
  sz_view_add_child(col, wrap);
  sz_view_add_child(col, sib);
  sz_view_layout(col, 240.f, 200.f, theme);
  cf = sz_view_frame(col);
  wf = sz_view_frame(wrap);
  lf = sz_view_frame(label);
  sf = sz_view_frame(sib);
  inner = cf.w - theme->pad * 2.f;
  assert(sz_view_kind(wrap) == SZ_VIEW_STRETCH);
  assert(fabsf(wf.w - inner) < 0.5f);
  assert(fabsf(lf.w - inner) < 0.5f);
  assert(sf.w + 1.f < inner);
  assert(wf.h + 0.5f < cf.h);
  sz_view_free(col);
}

static void test_stretch_row_fills_height(void) {
  SzView *row, *left, *mid, *wrap;
  const SzTheme *theme = sz_theme_default();
  SzRect rf, wf, mf;
  float inner_h;

  row = sz_view_row();
  left = sz_view_button("L", NULL, NULL);
  mid = sz_view_text("Hi");
  wrap = sz_view_stretch(mid);
  sz_view_add_child(row, left);
  sz_view_add_child(row, wrap);
  sz_view_layout(row, 320.f, 80.f, theme);
  rf = sz_view_frame(row);
  wf = sz_view_frame(wrap);
  mf = sz_view_frame(mid);
  inner_h = rf.h - theme->pad * 2.f;
  assert(fabsf(wf.h - inner_h) < 0.5f);
  assert(fabsf(mf.h - inner_h) < 0.5f);
  assert(wf.w + 1.f < rf.w - theme->pad * 2.f);
  sz_view_free(row);
}

static void test_stretch_does_not_take_flex_height(void) {
  SzView *col, *title, *body, *wrap, *btn, *exp;
  const SzTheme *theme = sz_theme_default();
  float max_h = 280.f;
  float leftover;
  float body_h;

  col = sz_view_column();
  title = sz_view_text("Title");
  body = sz_view_text("Hi");
  wrap = sz_view_stretch(body);
  btn = sz_view_button("Go", NULL, NULL);
  exp = sz_view_expanded(sz_view_text("flex"));
  sz_view_add_child(col, title);
  sz_view_add_child(col, wrap);
  sz_view_add_child(col, exp);
  sz_view_add_child(col, btn);
  sz_view_layout(col, 200.f, max_h, theme);
  leftover = max_h - theme->pad * 2.f - sz_view_frame(title).h -
             sz_view_frame(wrap).h - sz_view_frame(btn).h - theme->gap * 3.f;
  body_h = sz_view_frame(body).h;
  assert(sz_view_frame(exp).h >= leftover - 0.5f);
  assert(body_h + 8.f < leftover);
  assert(fabsf(sz_view_frame(body).w -
               (200.f - theme->pad * 2.f)) < 0.5f);
  sz_view_free(col);
}

static void test_stretch_background_fills_column(void) {
  SzView *col, *bg, *child, *wrap;
  const SzTheme *theme = sz_theme_default();
  SzRect cf, gf;
  float inner;

  col = sz_view_column();
  child = sz_view_text("Hi");
  bg = sz_view_background(0xFFE6F0F8u, child);
  wrap = sz_view_stretch(bg);
  sz_view_add_child(col, wrap);
  sz_view_layout(col, 220.f, 120.f, theme);
  cf = sz_view_frame(col);
  gf = sz_view_frame(bg);
  inner = cf.w - theme->pad * 2.f;
  assert(fabsf(gf.w - inner) < 0.5f);
  assert(fabsf(sz_view_frame(child).w - inner) < 0.5f);
  sz_view_free(col);
}

static void test_stretch_wrapping_expanded_still_flexes(void) {
  SzView *col, *title, *body, *exp, *wrap, *btn;
  const SzTheme *theme = sz_theme_default();
  float max_h = 260.f;
  float leftover;

  col = sz_view_column();
  title = sz_view_text("T");
  body = sz_view_text("Hi");
  exp = sz_view_expanded(body);
  wrap = sz_view_stretch(exp);
  btn = sz_view_button("Go", NULL, NULL);
  sz_view_add_child(col, title);
  sz_view_add_child(col, wrap);
  sz_view_add_child(col, btn);
  sz_view_layout(col, 200.f, max_h, theme);
  leftover = max_h - theme->pad * 2.f - sz_view_frame(title).h -
             sz_view_frame(btn).h - theme->gap * 2.f;
  assert(fabsf(sz_view_frame(wrap).h - leftover) < 0.5f);
  assert(fabsf(sz_view_frame(exp).h - leftover) < 0.5f);
  assert(fabsf(sz_view_frame(body).h - leftover) < 0.5f);
  assert(fabsf(sz_view_frame(body).w -
               (200.f - theme->pad * 2.f)) < 0.5f);
  sz_view_free(col);
}

static void test_expanded_wrapping_stretch_still_flexes(void) {
  SzView *col, *title, *body, *wrap, *exp, *btn;
  const SzTheme *theme = sz_theme_default();
  float max_h = 260.f;
  float leftover;

  col = sz_view_column();
  title = sz_view_text("T");
  body = sz_view_text("Hi");
  wrap = sz_view_stretch(body);
  exp = sz_view_expanded(wrap);
  btn = sz_view_button("Go", NULL, NULL);
  sz_view_add_child(col, title);
  sz_view_add_child(col, exp);
  sz_view_add_child(col, btn);
  sz_view_layout(col, 200.f, max_h, theme);
  leftover = max_h - theme->pad * 2.f - sz_view_frame(title).h -
             sz_view_frame(btn).h - theme->gap * 2.f;
  assert(fabsf(sz_view_frame(exp).h - leftover) < 0.5f);
  assert(fabsf(sz_view_frame(wrap).h - leftover) < 0.5f);
  assert(fabsf(sz_view_frame(body).h - leftover) < 0.5f);
  sz_view_free(col);
}

static void test_stretch_row_does_not_take_leftover_width(void) {
  SzView *row, *left, *mid, *wrap, *right, *exp;
  const SzTheme *theme = sz_theme_default();
  float max_w = 320.f;
  float leftover;

  row = sz_view_row();
  left = sz_view_button("L", NULL, NULL);
  mid = sz_view_text("Hi");
  wrap = sz_view_stretch(mid);
  exp = sz_view_expanded(sz_view_text("flex"));
  right = sz_view_button("R", NULL, NULL);
  sz_view_add_child(row, left);
  sz_view_add_child(row, wrap);
  sz_view_add_child(row, exp);
  sz_view_add_child(row, right);
  sz_view_layout(row, max_w, 80.f, theme);
  leftover = max_w - theme->pad * 2.f - sz_view_frame(left).w -
             sz_view_frame(wrap).w - sz_view_frame(right).w - theme->gap * 3.f;
  assert(sz_view_frame(exp).w >= leftover - 0.5f);
  assert(sz_view_frame(wrap).w + 8.f < leftover);
  assert(fabsf(sz_view_frame(mid).h -
               (80.f - theme->pad * 2.f)) < 0.5f);
  sz_view_free(row);
}

static void test_mobile_pointer_scroll_lifecycle(void) {
  SzUiConfig cfg;
  SzSignalStr *draft;
  SzView *root, *scroll, *list, *field;
  SzUiSession *session;
  SzInputEvent ev;
  const SzTheme *theme = sz_theme_default();
  float y0;

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_MOBILE;
  cfg.width = 200;
  cfg.height = 160;
  cfg.scale = 1.0;
  cfg.title = "mobile-test";

  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "type");
  sz_view_add_child(root, field);
  list = sz_view_list();
  sz_view_add_child(list, sz_view_text("one"));
  sz_view_add_child(list, sz_view_text("two"));
  sz_view_add_child(list, sz_view_text("three"));
  sz_view_add_child(list, sz_view_text("four"));
  scroll = sz_view_scroll(list);
  sz_view_add_child(root, scroll);

  session = sz_ui_mount(&cfg, root);
  assert(session);
  assert(sz_ui_session_kind(session) == SZ_UI_RUNTIME_MOBILE);
  assert(sz_ui_session_lifecycle(session) == SZ_LIFECYCLE_RESUME);
  assert(sz_ui_pump_sync(session));

  /* Soft keyboard: tap TextField → keyboard visible. */
  sz_view_layout(root, 200.f, 160.f, theme);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_DOWN;
  ev.x = sz_view_frame(field).x + 4.f;
  ev.y = sz_view_frame(field).y + 4.f;
  assert(sz_ui_inject_sync(session, &ev));
  ev.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_session_keyboard_visible(session) == 1);
  assert(sz_view_has_focused_text_field(root));

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TEXT;
  ev.text = "milk";
  assert(sz_ui_inject_sync(session, &ev));
  assert(strcmp(sz_signal_str_get(draft), "milk") == 0);

  /* Scroll gesture through POINTER move on the Scroll viewport. */
  sz_view_layout(root, 200.f, 160.f, theme);
  y0 = sz_view_scroll_y(scroll);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_DOWN;
  ev.x = sz_view_frame(scroll).x + 8.f;
  ev.y = sz_view_frame(scroll).y + 8.f;
  assert(sz_ui_inject_sync(session, &ev));
  ev.pointer_phase = SZ_POINTER_MOVE;
  ev.y = sz_view_frame(scroll).y + 8.f - 20.f; /* finger up → content up */
  assert(sz_ui_inject_sync(session, &ev));
  ev.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_view_scroll_y(scroll) > y0);

  /* Direct scroll inject also works (Headless-scriptable). */
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_SCROLL;
  ev.x = sz_view_frame(scroll).x + 8.f;
  ev.y = sz_view_frame(scroll).y + 8.f;
  ev.dy = 12.f;
  y0 = sz_view_scroll_y(scroll);
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_view_scroll_y(scroll) == y0 + 12.f);

  /* Lifecycle pause hides keyboard; stop blocks further taps. */
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_LIFECYCLE;
  ev.lifecycle = SZ_LIFECYCLE_PAUSE;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_session_lifecycle(session) == SZ_LIFECYCLE_PAUSE);
  assert(sz_ui_session_keyboard_visible(session) == 0);

  ev.lifecycle = SZ_LIFECYCLE_RESUME;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));

  ev.lifecycle = SZ_LIFECYCLE_STOP;
  assert(sz_ui_inject_sync(session, &ev));
  assert(!sz_ui_pump_sync(session));

  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_str_free(draft);
}

static void test_text_stays_one_line_when_unbounded(void) {
  SzView *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;

  t = sz_view_text("one two three");
  sz_view_layout(t, 1000.f, 100.f, theme);
  assert(fabsf(sz_view_frame(t).h - line_h) < 0.5f);
  sz_view_free(t);
}

static void test_text_wraps_at_newline(void) {
  SzView *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;
  SzString *dump;

  t = sz_view_text("one\ntwo");
  sz_view_layout(t, 1000.f, 200.f, theme);
  assert(fabsf(sz_view_frame(t).h - 2.f * line_h) < 0.5f);
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "text:one") != NULL);
  sz_string_free(dump);
  sz_view_free(t);
}

static void test_text_wraps_at_space(void) {
  SzView *one, *both;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;
  float one_w;

  one = sz_view_text("one");
  sz_view_layout(one, 1000.f, 100.f, theme);
  one_w = sz_view_frame(one).w;
  sz_view_free(one);

  both = sz_view_text("one two");
  sz_view_layout(both, one_w, 200.f, theme);
  assert(sz_view_frame(both).h >= 2.f * line_h - 0.5f);
  assert(sz_view_frame(both).w <= one_w + 0.5f);
  sz_view_free(both);
}

static void test_text_hard_wraps_long_word(void) {
  SzView *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;
  float one_w;

  t = sz_view_text("M");
  sz_view_layout(t, 1000.f, 100.f, theme);
  one_w = sz_view_frame(t).w;
  sz_view_free(t);

  t = sz_view_text("MMMM");
  sz_view_layout(t, one_w, 200.f, theme);
  assert(sz_view_frame(t).h >= 2.f * line_h - 0.5f);
  assert(sz_view_frame(t).w <= one_w + 0.5f);
  sz_view_free(t);
}

static void test_text_wrap_grows_column(void) {
  SzView *col, *t, *btn;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;
  float one_w;
  float col_h;

  t = sz_view_text("one");
  sz_view_layout(t, 1000.f, 100.f, theme);
  one_w = sz_view_frame(t).w;
  sz_view_free(t);

  col = sz_view_column();
  t = sz_view_text("one two");
  btn = sz_view_button("Go", NULL, NULL);
  sz_view_add_child(col, t);
  sz_view_add_child(col, btn);
  sz_view_layout(col, one_w + theme->pad * 2.f, 400.f, theme);
  col_h = sz_view_frame(col).h;
  assert(sz_view_frame(t).h >= 2.f * line_h - 0.5f);
  assert(col_h >= sz_view_frame(t).h + sz_view_frame(btn).h + theme->pad * 2.f -
                      0.5f);
  sz_view_free(col);
}

static void test_bind_text_wraps_at_newline(void) {
  SzSignalStr *s;
  SzView *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;

  s = sz_signal_str("one\ntwo");
  t = sz_view_text_signal_str(s);
  sz_view_layout(t, 1000.f, 200.f, theme);
  assert(fabsf(sz_view_frame(t).h - 2.f * line_h) < 0.5f);
  sz_view_free(t);
  sz_signal_str_free(s);
}

static void test_max_lines_caps_newlines(void) {
  SzView *wrap, *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;
  SzString *dump;

  t = sz_view_text("one\ntwo\nthree");
  wrap = sz_view_max_lines(2, t);
  sz_view_layout(wrap, 1000.f, 200.f, theme);
  assert(sz_view_kind(wrap) == SZ_VIEW_MAX_LINES);
  assert(fabsf(sz_view_frame(wrap).h - 2.f * line_h) < 0.5f);
  assert(fabsf(sz_view_frame(t).h - 2.f * line_h) < 0.5f);
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "three") != NULL);
  sz_string_free(dump);
  sz_view_free(wrap);
}

static void test_max_lines_zero_is_uncapped(void) {
  SzView *wrap, *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;

  t = sz_view_text("one\ntwo\nthree");
  wrap = sz_view_max_lines(0, t);
  sz_view_layout(wrap, 1000.f, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - 3.f * line_h) < 0.5f);
  sz_view_free(wrap);
}

static void test_max_lines_caps_soft_wrap(void) {
  SzView *one, *wrap, *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;
  float one_w;

  one = sz_view_text("one");
  sz_view_layout(one, 1000.f, 100.f, theme);
  one_w = sz_view_frame(one).w;
  sz_view_free(one);

  t = sz_view_text("one two");
  wrap = sz_view_max_lines(1, t);
  sz_view_layout(wrap, one_w, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - line_h) < 0.5f);
  assert(sz_view_frame(t).w <= one_w + 0.5f);
  sz_view_free(wrap);
}

static void test_nested_max_lines_uses_tighter_cap(void) {
  SzView *outer, *inner, *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;

  t = sz_view_text("a\nb\nc\nd");
  inner = sz_view_max_lines(3, t);
  outer = sz_view_max_lines(2, inner);
  sz_view_layout(outer, 1000.f, 200.f, theme);
  assert(fabsf(sz_view_frame(outer).h - 2.f * line_h) < 0.5f);
  assert(fabsf(sz_view_frame(t).h - 2.f * line_h) < 0.5f);
  sz_view_free(outer);
}

static void test_bind_text_respects_max_lines(void) {
  SzSignalStr *s;
  SzView *t, *wrap;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;

  s = sz_signal_str("one\ntwo\nthree");
  t = sz_view_text_signal_str(s);
  wrap = sz_view_max_lines(2, t);
  sz_view_layout(wrap, 1000.f, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - 2.f * line_h) < 0.5f);
  sz_view_free(wrap);
  sz_signal_str_free(s);
}

static void test_max_lines_does_not_wrap_button(void) {
  SzView *wrap, *b;
  const SzTheme *theme = sz_theme_default();
  float btn_h;

  b = sz_view_button("one\ntwo\nthree", NULL, NULL);
  sz_view_layout(b, 1000.f, 100.f, theme);
  btn_h = sz_view_frame(b).h;
  sz_view_free(b);

  b = sz_view_button("one\ntwo\nthree", NULL, NULL);
  wrap = sz_view_max_lines(1, b);
  sz_view_layout(wrap, 1000.f, 100.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - btn_h) < 0.5f);
  assert(fabsf(btn_h - theme->control_h) < 0.5f);
  sz_view_free(wrap);
}

static void test_ellipsis_sizes_to_child(void) {
  SzView *wrap, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect wf, chf;

  child = sz_view_sized(40, 30, sz_view_text("Hi"));
  wrap = sz_view_ellipsis(child);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(sz_view_kind(wrap) == SZ_VIEW_ELLIPSIS);
  wf = sz_view_frame(wrap);
  chf = sz_view_frame(child);
  assert(fabsf(wf.w - 40.f) < 0.5f);
  assert(fabsf(wf.h - 30.f) < 0.5f);
  assert(fabsf(chf.w - 40.f) < 0.5f);
  assert(fabsf(chf.h - 30.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_ellipsis_keeps_one_line(void) {
  SzView *wrap, *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;
  SzString *dump;

  t = sz_view_text("one\ntwo\nthree");
  wrap = sz_view_ellipsis(t);
  sz_view_layout(wrap, 1000.f, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - line_h) < 0.5f);
  assert(fabsf(sz_view_frame(t).h - line_h) < 0.5f);
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "three") != NULL);
  sz_string_free(dump);
  sz_view_free(wrap);
}

static void test_ellipsis_short_text_stays_one_line(void) {
  SzView *wrap, *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;

  t = sz_view_text("Hi");
  wrap = sz_view_ellipsis(t);
  sz_view_layout(wrap, 1000.f, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - line_h) < 0.5f);
  sz_view_free(wrap);
}

static void test_ellipsis_with_max_lines_keeps_cap(void) {
  SzView *wrap, *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;
  SzString *dump;

  t = sz_view_text("one\ntwo\nthree\nfour");
  wrap = sz_view_ellipsis(sz_view_max_lines(2, t));
  sz_view_layout(wrap, 1000.f, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - 2.f * line_h) < 0.5f);
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "four") != NULL);
  sz_string_free(dump);
  sz_view_free(wrap);
}

static void test_ellipsis_caps_soft_wrap(void) {
  SzView *one, *wrap, *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;
  float one_w;

  one = sz_view_text("one");
  sz_view_layout(one, 1000.f, 100.f, theme);
  one_w = sz_view_frame(one).w;
  sz_view_free(one);

  t = sz_view_text("one two");
  wrap = sz_view_ellipsis(t);
  sz_view_layout(wrap, one_w, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - line_h) < 0.5f);
  sz_view_free(wrap);
}

static void test_bind_text_respects_ellipsis(void) {
  SzSignalStr *s;
  SzView *t, *wrap;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;

  s = sz_signal_str("one\ntwo\nthree");
  t = sz_view_text_signal_str(s);
  wrap = sz_view_ellipsis(t);
  sz_view_layout(wrap, 1000.f, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - line_h) < 0.5f);
  sz_view_free(wrap);
  sz_signal_str_free(s);
}

static void test_ellipsis_does_not_wrap_button(void) {
  SzView *wrap, *b;
  const SzTheme *theme = sz_theme_default();
  float btn_h;

  b = sz_view_button("one\ntwo\nthree", NULL, NULL);
  sz_view_layout(b, 1000.f, 100.f, theme);
  btn_h = sz_view_frame(b).h;
  sz_view_free(b);

  b = sz_view_button("one\ntwo\nthree", NULL, NULL);
  wrap = sz_view_ellipsis(b);
  sz_view_layout(wrap, 1000.f, 100.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - btn_h) < 0.5f);
  assert(fabsf(btn_h - theme->control_h) < 0.5f);
  sz_view_free(wrap);
}

static int row_has_dark(const uint8_t *px, int w, int y, int x0, int x1) {
  int x;
  for (x = x0; x < x1; x++) {
    const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
    if (p[0] < 80 && p[1] < 80 && p[2] < 80)
      return 1;
  }
  return 0;
}

static void test_ellipsis_paint_hides_extra_lines(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;
  int y2;

  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);

  root = sz_view_text("one\ntwo\nthree");
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  y2 = (int)line_h + 2;
  {
    int y;
    int saw = 0;
    for (y = y2; y < y2 + (int)line_h && y < 80; y++) {
      if (row_has_dark(px, 80, y, 0, 40))
        saw = 1;
    }
    assert(saw);
  }
  sz_view_free(root);

  root = sz_view_ellipsis(sz_view_text("one\ntwo\nthree"));
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  {
    int y;
    for (y = y2; y < y2 + (int)line_h && y < 80; y++)
      assert(!row_has_dark(px, 80, y, 0, 40));
  }
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_color_rgb_packs_opaque(void) {
  assert((uint32_t)sz_color_rgb(230, 240, 248) == 0xFFE6F0F8u);
  assert((uint32_t)sz_color_rgb(1, 2, 3) == 0xFF010203u);
  assert((uint32_t)sz_color_rgb(-1, 256, 511) == 0xFFFF00FFu);
}

static void test_color_rgba_packs_alpha(void) {
  assert((uint32_t)sz_color_rgba(1, 2, 3, 4) == 0x04010203u);
  assert((uint32_t)sz_color_rgba(255, 0, 0, 255) == 0xFFFF0000u);
  assert((uint32_t)sz_color_rgba(0, 0, 0, 0) == 0x00000000u);
}

static void test_text_color_sizes_to_child(void) {
  SzView *wrap, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect wf, chf;

  child = sz_view_sized(40, 30, sz_view_text("Hi"));
  wrap = sz_view_text_color((uint32_t)sz_color_rgb(255, 0, 0), child);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(sz_view_kind(wrap) == SZ_VIEW_TEXT_COLOR);
  wf = sz_view_frame(wrap);
  chf = sz_view_frame(child);
  assert(fabsf(wf.w - 40.f) < 0.5f);
  assert(fabsf(wf.h - 30.f) < 0.5f);
  assert(fabsf(chf.w - 40.f) < 0.5f);
  assert(fabsf(chf.h - 30.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_text_color_keeps_a11y(void) {
  SzView *wrap;
  SzString *dump;

  wrap = sz_view_text_color((uint32_t)sz_color_rgb(255, 0, 0), sz_view_text("hi"));
  dump = sz_view_a11y_dump(wrap);
  assert(strstr(sz_string_cstr(dump), "text:hi") != NULL);
  sz_string_free(dump);
  sz_view_free(wrap);
}

static int row_has_red(const uint8_t *px, int w, int y, int x0, int x1) {
  int x;
  px_map_channels();
  for (x = x0; x < x1; x++) {
    const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
    if (p[g_px_r] > 180 && p[1] < 80 && p[g_px_b] < 80)
      return 1;
  }
  return 0;
}

static int row_has_blue(const uint8_t *px, int w, int y, int x0, int x1) {
  int x;
  px_map_channels();
  for (x = x0; x < x1; x++) {
    const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
    if (p[g_px_b] > 180 && p[g_px_r] < 80 && p[1] < 80)
      return 1;
  }
  return 0;
}

static int band_has(int (*fn)(const uint8_t *, int, int, int, int), const uint8_t *px,
                    int w, int y0, int y1, int x0, int x1) {
  int y;
  for (y = y0; y < y1 && y < 80; y++) {
    if (fn(px, w, y, x0, x1))
      return 1;
  }
  return 0;
}

static void test_text_color_paints_red(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  int y1;

  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  y1 = (int)(theme->font_px + 6.f);

  root = sz_view_text("MMMM");
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(!band_has(row_has_red, px, 80, 0, y1, 0, 40));
  sz_view_free(root);

  root = sz_view_text_color((uint32_t)sz_color_rgb(255, 0, 0), sz_view_text("MMMM"));
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(band_has(row_has_red, px, 80, 0, y1, 0, 40));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_nested_text_color_inner_wins(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  int y1;

  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  y1 = (int)(theme->font_px + 6.f);
  root = sz_view_text_color(
      (uint32_t)sz_color_rgb(255, 0, 0),
      sz_view_text_color((uint32_t)sz_color_rgb(0, 0, 255), sz_view_text("MMMM")));
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(band_has(row_has_blue, px, 80, 0, y1, 0, 40));
  assert(!band_has(row_has_red, px, 80, 0, y1, 0, 40));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_text_color_does_not_recolor_button(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int y;

  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  root = sz_view_text_color((uint32_t)sz_color_rgb(255, 0, 0),
                            sz_view_button("Go", NULL, NULL));
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  f = sz_view_frame(root);
  y = (int)(f.y + f.h * 0.5f);
  assert(!row_has_red(px, 80, y, (int)f.x, (int)(f.x + f.w)));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_bind_text_respects_text_color(void) {
  SzSignalStr *s;
  SzView *wrap;
  SzString *dump;
  const SzTheme *theme = sz_theme_default();

  s = sz_signal_str("live");
  wrap = sz_view_text_color((uint32_t)sz_color_rgb(0, 0, 255),
                            sz_view_text_signal_str(s));
  sz_view_layout(wrap, 200.f, 80.f, theme);
  dump = sz_view_a11y_dump(wrap);
  assert(strstr(sz_string_cstr(dump), "text:live") != NULL);
  sz_string_free(dump);
  sz_view_free(wrap);
  sz_signal_str_free(s);
}

static void test_color_rgba_background_paints_alpha(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  root = sz_view_background((uint32_t)sz_color_rgba(255, 0, 0, 255),
                            sz_view_sized(40, 40, sz_view_text("x")));
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(row_has_red(px, 80, 10, 4, 20));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_gap_sizes_to_child(void) {
  SzView *wrap, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect wf, chf;

  child = sz_view_sized(40, 30, sz_view_text("Hi"));
  wrap = sz_view_gap(12, child);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(sz_view_kind(wrap) == SZ_VIEW_GAP);
  wf = sz_view_frame(wrap);
  chf = sz_view_frame(child);
  assert(fabsf(wf.w - 40.f) < 0.5f);
  assert(fabsf(wf.h - 30.f) < 0.5f);
  assert(fabsf(chf.w - 40.f) < 0.5f);
  assert(fabsf(chf.h - 30.f) < 0.5f);
  sz_view_free(wrap);
}

static SzView *two_box_column(SzView **a, SzView **b) {
  SzView *col = sz_view_column();
  *a = sz_view_sized(20, 10, sz_view_text("a"));
  *b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(col, *a);
  sz_view_add_child(col, *b);
  return col;
}

static void test_default_column_uses_theme_gap(void) {
  SzView *col, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  col = two_box_column(&a, &b);
  sz_view_layout(col, 200.f, 200.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(bf.y - (af.y + af.h) - theme->gap) < 0.5f);
  sz_view_free(col);
}

static void test_gap_zero_stacks_column_flush(void) {
  SzView *wrap, *col, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  col = two_box_column(&a, &b);
  wrap = sz_view_gap(0, col);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(bf.y - (af.y + af.h)) < 0.5f);
  sz_view_free(wrap);
}

static void test_gap_n_spaces_column(void) {
  SzView *wrap, *col, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  col = two_box_column(&a, &b);
  wrap = sz_view_gap(20, col);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(bf.y - (af.y + af.h) - 20.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_negative_gap_is_zero(void) {
  SzView *wrap, *col, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  col = two_box_column(&a, &b);
  wrap = sz_view_gap(-4, col);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(bf.y - (af.y + af.h)) < 0.5f);
  sz_view_free(wrap);
}

static void test_nested_gap_inner_wins(void) {
  SzView *outer, *inner, *col, *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  col = two_box_column(&a, &b);
  inner = sz_view_gap(0, col);
  outer = sz_view_gap(20, inner);
  sz_view_layout(outer, 200.f, 200.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(bf.y - (af.y + af.h)) < 0.5f);
  sz_view_free(outer);
}

static void test_gap_spaces_row(void) {
  SzView *row, *a, *b, *wrap;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  row = sz_view_row();
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(row, a);
  sz_view_add_child(row, b);
  wrap = sz_view_gap(16, row);
  sz_view_layout(wrap, 200.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(bf.x - (af.x + af.w) - 16.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_gap_zero_row_is_flush(void) {
  SzView *row, *a, *b, *wrap;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  row = sz_view_row();
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(row, a);
  sz_view_add_child(row, b);
  wrap = sz_view_gap(0, row);
  sz_view_layout(wrap, 200.f, 80.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(bf.x - (af.x + af.w)) < 0.5f);
  sz_view_free(wrap);
}

static void test_gap_zero_shrinks_column_height(void) {
  SzView *wrap, *col, *a, *b;
  const SzTheme *theme = sz_theme_default();
  float plain_h, zero_h;

  col = two_box_column(&a, &b);
  sz_view_layout(col, 200.f, 200.f, theme);
  plain_h = sz_view_frame(col).h;
  sz_view_free(col);

  col = two_box_column(&a, &b);
  wrap = sz_view_gap(0, col);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  zero_h = sz_view_frame(wrap).h;
  assert(zero_h + 1.f < plain_h);
  assert(fabsf(plain_h - zero_h - theme->gap) < 0.5f);
  sz_view_free(wrap);
}

static void test_gap_does_not_change_stack(void) {
  SzView *stack, *a, *b, *wrap;
  const SzTheme *theme = sz_theme_default();
  SzRect af, bf;

  stack = sz_view_stack();
  a = sz_view_sized(20, 10, sz_view_text("a"));
  b = sz_view_sized(20, 10, sz_view_text("b"));
  sz_view_add_child(stack, a);
  sz_view_add_child(stack, b);
  wrap = sz_view_gap(20, stack);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  af = sz_view_frame(a);
  bf = sz_view_frame(b);
  assert(fabsf(af.x - bf.x) < 0.5f);
  assert(fabsf(af.y - bf.y) < 0.5f);
  sz_view_free(wrap);
}

static void test_font_size_sizes_to_child(void) {
  SzView *wrap, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect wf, chf;

  child = sz_view_sized(40, 30, sz_view_text("Hi"));
  wrap = sz_view_font_size(16, child);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(sz_view_kind(wrap) == SZ_VIEW_FONT_SIZE);
  wf = sz_view_frame(wrap);
  chf = sz_view_frame(child);
  assert(fabsf(wf.w - 40.f) < 0.5f);
  assert(fabsf(wf.h - 30.f) < 0.5f);
  assert(fabsf(chf.w - 40.f) < 0.5f);
  assert(fabsf(chf.h - 30.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_font_size_grows_text(void) {
  SzView *plain, *wrap;
  const SzTheme *theme = sz_theme_default();
  float plain_h, big_h, plain_w, big_w;

  plain = sz_view_text("Hi");
  sz_view_layout(plain, 1000.f, 200.f, theme);
  plain_h = sz_view_frame(plain).h;
  plain_w = sz_view_frame(plain).w;
  sz_view_free(plain);
  assert(fabsf(plain_h - (theme->font_px + 6.f)) < 0.5f);

  wrap = sz_view_font_size(16, sz_view_text("Hi"));
  sz_view_layout(wrap, 1000.f, 200.f, theme);
  big_h = sz_view_frame(wrap).h;
  big_w = sz_view_frame(wrap).w;
  assert(fabsf(big_h - (16.f + 6.f)) < 0.5f);
  assert(big_w > plain_w + 1.f);
  sz_view_free(wrap);
}

static void test_logical_px_match_device_scale(void) {
  SzView *col, *btn;
  SzTheme logical = *sz_theme_default();
  SzTheme device;
  float y1, y2;

  col = sz_view_column();
  sz_view_add_child(col, sz_view_padding(
                             8, sz_view_font_size(18, sz_view_text("Studio"))));
  btn = sz_view_button("+1", NULL, NULL);
  sz_view_add_child(col, btn);

  sz_view_layout(col, 200.f, 400.f, &logical);
  y1 = sz_view_frame(btn).y;

  device = logical;
  device.font_px *= 2.f;
  device.pad *= 2.f;
  device.gap *= 2.f;
  device.control_h *= 2.f;
  device.px_scale = 2.f;
  sz_view_layout(col, 400.f, 800.f, &device);
  y2 = sz_view_frame(btn).y;

  /* Author px (fontSize / padding) must scale with the backing factor so
   * device-pixel paint frames stay aligned with logical hit-test frames. */
  assert(fabsf(y2 - y1 * 2.f) < 1.5f);
  sz_view_free(col);
}

static void test_nested_font_size_inner_wins(void) {
  SzView *outer, *inner, *t;
  const SzTheme *theme = sz_theme_default();

  t = sz_view_text("Hi");
  inner = sz_view_font_size(8, t);
  outer = sz_view_font_size(24, inner);
  sz_view_layout(outer, 200.f, 200.f, theme);
  assert(fabsf(sz_view_frame(t).h - (8.f + 6.f)) < 0.5f);
  sz_view_free(outer);
}

static void test_font_size_zero_is_one(void) {
  SzView *wrap;
  const SzTheme *theme = sz_theme_default();

  wrap = sz_view_font_size(0, sz_view_text("Hi"));
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - 7.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_font_size_does_not_grow_button(void) {
  SzView *wrap, *b;
  const SzTheme *theme = sz_theme_default();
  float btn_h;

  b = sz_view_button("Go", NULL, NULL);
  sz_view_layout(b, 200.f, 100.f, theme);
  btn_h = sz_view_frame(b).h;
  sz_view_free(b);

  wrap = sz_view_font_size(24, sz_view_button("Go", NULL, NULL));
  sz_view_layout(wrap, 200.f, 100.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - btn_h) < 0.5f);
  assert(fabsf(btn_h - theme->control_h) < 0.5f);
  sz_view_free(wrap);
}

static void test_font_size_wraps_sooner(void) {
  SzView *plain, *wrap, *t;
  const SzTheme *theme = sz_theme_default();
  float one_w;
  float line_h = 16.f + 6.f;

  plain = sz_view_text("one");
  sz_view_layout(plain, 1000.f, 100.f, theme);
  one_w = sz_view_frame(plain).w;
  sz_view_free(plain);

  t = sz_view_text("one two");
  wrap = sz_view_font_size(16, t);
  sz_view_layout(wrap, one_w, 200.f, theme);
  assert(sz_view_frame(wrap).h > line_h + 1.f);
  sz_view_free(wrap);
}

static void test_border_sizes_to_child(void) {
  SzView *wrap, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect wf, chf;

  child = sz_view_sized(40, 30, sz_view_text("Hi"));
  wrap = sz_view_border(4, 0xFFFF0000u, child);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(sz_view_kind(wrap) == SZ_VIEW_BORDER);
  wf = sz_view_frame(wrap);
  chf = sz_view_frame(child);
  assert(fabsf(wf.w - 40.f) < 0.5f);
  assert(fabsf(wf.h - 30.f) < 0.5f);
  assert(fabsf(chf.w - 40.f) < 0.5f);
  assert(fabsf(chf.h - 30.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_border_keeps_a11y(void) {
  SzView *wrap;
  SzString *dump;

  wrap = sz_view_border(2, 0xFFFF0000u, sz_view_text("hi"));
  dump = sz_view_a11y_dump(wrap);
  assert(strstr(sz_string_cstr(dump), "text:hi") != NULL);
  sz_string_free(dump);
  sz_view_free(wrap);
}

static void test_border_zero_width_sizes_to_child(void) {
  SzView *wrap, *child;
  const SzTheme *theme = sz_theme_default();

  child = sz_view_sized(40, 30, sz_view_text("Hi"));
  wrap = sz_view_border(0, 0xFFFF0000u, child);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).w - 40.f) < 0.5f);
  assert(fabsf(sz_view_frame(wrap).h - 30.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_negative_border_is_zero(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_border(
      -3, 0xFFFF0000u,
      sz_view_background(0xFF00AA00u, sz_view_sized(40, 40, sz_view_text("x"))));
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 0, 0, 0x00, 0xAA, 0x00));
  assert(px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_border_paints_inside_frame(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_border(
      4, 0xFFFF0000u,
      sz_view_background(0xFF00AA00u, sz_view_sized(40, 40, sz_view_text("x"))));
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 0, 0, 0xFF, 0x00, 0x00));
  assert(px_rgb(px, 80, 39, 0, 0xFF, 0x00, 0x00));
  assert(px_rgb(px, 80, 0, 39, 0xFF, 0x00, 0x00));
  assert(px_rgb(px, 80, 39, 39, 0xFF, 0x00, 0x00));
  assert(px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  assert(px_rgb(px, 80, 50, 20, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_border_zero_does_not_paint(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_border(
      0, 0xFFFF0000u,
      sz_view_background(0xFF00AA00u, sz_view_sized(40, 40, sz_view_text("x"))));
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 0, 0, 0x00, 0xAA, 0x00));
  assert(px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_nested_border_outer_paints_on_top(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_border(
      2, 0xFF0000FFu,
      sz_view_border(8, 0xFFFF0000u,
                     sz_view_background(0xFF00AA00u,
                                        sz_view_sized(40, 40, sz_view_text("x")))));
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 0, 0, 0x00, 0x00, 0xFF));
  assert(px_rgb(px, 80, 4, 4, 0xFF, 0x00, 0x00));
  assert(px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_thick_border_fills_small_frame(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_border(
      100, 0xFFFF0000u,
      sz_view_background(0xFF00AA00u, sz_view_sized(10, 10, sz_view_text("x"))));
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 0, 0, 0xFF, 0x00, 0x00));
  assert(px_rgb(px, 80, 5, 5, 0xFF, 0x00, 0x00));
  assert(px_rgb(px, 80, 9, 9, 0xFF, 0x00, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_border_hit_test_reaches_child(void) {
  SzView *wrap, *hit;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  wrap = sz_view_border(4, 0xFFFF0000u, sz_view_button("Go", NULL, NULL));
  sz_view_layout(wrap, 200.f, 200.f, theme);
  f = sz_view_frame(wrap);
  hit = sz_view_hit_test(wrap, f.x + f.w * 0.5f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  sz_view_free(wrap);
}

static void test_border_does_not_grow_button(void) {
  SzView *wrap, *b;
  const SzTheme *theme = sz_theme_default();
  float btn_h;

  b = sz_view_button("Go", NULL, NULL);
  sz_view_layout(b, 200.f, 200.f, theme);
  btn_h = sz_view_frame(b).h;
  sz_view_free(b);

  wrap = sz_view_border(4, 0xFFFF0000u, sz_view_button("Go", NULL, NULL));
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - btn_h) < 0.5f);
  assert(fabsf(btn_h - theme->control_h) < 0.5f);
  sz_view_free(wrap);
}

static SzView *radius_green_box(int r) {
  return sz_view_radius(
      r, sz_view_background(0xFF00AA00u, sz_view_sized(40, 40, sz_view_text("x"))));
}

static void test_radius_sizes_to_child(void) {
  SzView *wrap, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect wf, chf;

  child = sz_view_sized(40, 30, sz_view_text("Hi"));
  wrap = sz_view_radius(8, child);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(sz_view_kind(wrap) == SZ_VIEW_RADIUS);
  wf = sz_view_frame(wrap);
  chf = sz_view_frame(child);
  assert(fabsf(wf.w - 40.f) < 0.5f);
  assert(fabsf(wf.h - 30.f) < 0.5f);
  assert(fabsf(chf.w - 40.f) < 0.5f);
  assert(fabsf(chf.h - 30.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_radius_keeps_a11y(void) {
  SzView *wrap;
  SzString *dump;

  wrap = sz_view_radius(8, sz_view_text("hi"));
  dump = sz_view_a11y_dump(wrap);
  assert(strstr(sz_string_cstr(dump), "text:hi") != NULL);
  sz_string_free(dump);
  sz_view_free(wrap);
}

static void test_radius_zero_fills_corners(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = radius_green_box(0);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 0, 0, 0x00, 0xAA, 0x00));
  assert(px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_negative_radius_is_zero(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = radius_green_box(-4);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 0, 0, 0x00, 0xAA, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_radius_clips_corners(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = radius_green_box(12);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 0, 0, 0xF5, 0xF5, 0xF5));
  assert(px_rgb(px, 80, 39, 0, 0xF5, 0xF5, 0xF5));
  assert(px_rgb(px, 80, 0, 39, 0xF5, 0xF5, 0xF5));
  assert(px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  assert(px_rgb(px, 80, 1, 20, 0x00, 0xAA, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_nested_radius_inner_wins(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_radius(20, radius_green_box(8));
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 0, 0, 0xF5, 0xF5, 0xF5));
  /* Inside r=8, outside r=20. Inner radius must win. */
  assert(px_rgb(px, 80, 12, 1, 0x00, 0xAA, 0x00));
  assert(px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_radius_clips_border_corners(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_radius(
      12, sz_view_border(4, 0xFFFF0000u,
                         sz_view_background(0xFF00AA00u,
                                            sz_view_sized(40, 40, sz_view_text("x")))));
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 0, 0, 0xF5, 0xF5, 0xF5));
  assert(px_rgb(px, 80, 0, 20, 0xFF, 0x00, 0x00));
  assert(px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_radius_hit_test_reaches_child(void) {
  SzView *wrap, *hit;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  wrap = sz_view_radius(8, sz_view_button("Go", NULL, NULL));
  sz_view_layout(wrap, 200.f, 200.f, theme);
  f = sz_view_frame(wrap);
  hit = sz_view_hit_test(wrap, f.x + f.w * 0.5f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  sz_view_free(wrap);
}

static void test_radius_does_not_grow_button(void) {
  SzView *wrap, *b;
  const SzTheme *theme = sz_theme_default();
  float btn_h;

  b = sz_view_button("Go", NULL, NULL);
  sz_view_layout(b, 200.f, 200.f, theme);
  btn_h = sz_view_frame(b).h;
  sz_view_free(b);

  wrap = sz_view_radius(8, sz_view_button("Go", NULL, NULL));
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(fabsf(sz_view_frame(wrap).h - btn_h) < 0.5f);
  assert(fabsf(btn_h - theme->control_h) < 0.5f);
  sz_view_free(wrap);
}

static void test_checkbox_sizes(void) {
  SzView *box;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  box = sz_view_checkbox(sig, "Done");
  sz_view_layout(box, 200.f, 200.f, theme);
  assert(sz_view_kind(box) == SZ_VIEW_CHECKBOX);
  f = sz_view_frame(box);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(f.w > 12.f);
  sz_view_free(box);
  sz_signal_int_free(sig);
}

static void test_checkbox_a11y_off_on(void) {
  SzView *box;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  box = sz_view_checkbox(sig, "Done");
  dump = sz_view_a11y_dump(box);
  assert(strstr(sz_string_cstr(dump), "checkbox:Done=0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(box);
  assert(strstr(sz_string_cstr(dump), "checkbox:Done=1") != NULL);
  sz_string_free(dump);
  sz_view_free(box);
  sz_signal_int_free(sig);
}

static void test_checkbox_nonzero_is_on(void) {
  SzView *box;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(7);
  box = sz_view_checkbox(sig, "X");
  dump = sz_view_a11y_dump(box);
  assert(strstr(sz_string_cstr(dump), "checkbox:X=1") != NULL);
  sz_string_free(dump);
  sz_view_free(box);
  sz_signal_int_free(sig);
}

static void test_checkbox_tap_toggles(void) {
  SzView *box;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  box = sz_view_checkbox(sig, "Done");
  sz_view_layout(box, 200.f, 200.f, theme);
  f = sz_view_frame(box);
  assert(sz_view_handle_tap(box, f.x + f.w * 0.5f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(box, f.x + 2.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(box);
  sz_signal_int_free(sig);
}

static void test_checkbox_hit_test(void) {
  SzView *box, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  box = sz_view_checkbox(sig, "Done");
  sz_view_layout(box, 200.f, 200.f, theme);
  f = sz_view_frame(box);
  hit = sz_view_hit_test(box, f.x + f.w * 0.5f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_CHECKBOX);
  assert(sz_view_is_tap_target(hit));
  sz_view_free(box);
  sz_signal_int_free(sig);
}

static void test_checkbox_paint_off_on(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  sig = sz_signal_int(0);
  root = sz_view_checkbox(sig, "Done");
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Box center: unchecked is theme background, not primary fill. */
  assert(px_rgb(px, 80, 6, 16, 0xF5, 0xF5, 0xF5));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 6, 16, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_tap_collect_tree_order(void) {
  SzView *root, *hits[8];
  SzSignalInt *a, *b;
  const SzTheme *theme = sz_theme_default();
  int n;

  a = sz_signal_int(0);
  b = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_button("A", NULL, NULL));
  sz_view_add_child(root, sz_view_checkbox(a, "B"));
  sz_view_add_child(root, sz_view_radio(b, 1, "C"));
  sz_view_layout(root, 200.f, 200.f, theme);
  n = sz_view_collect_tap_targets(root, hits, 8);
  assert(n == 3);
  assert(strcmp(sz_view_a11y_label(hits[0]), "A") == 0);
  assert(strcmp(sz_view_a11y_label(hits[1]), "B") == 0);
  assert(strcmp(sz_view_a11y_label(hits[2]), "C") == 0);
  sz_view_free(root);
  sz_signal_int_free(a);
  sz_signal_int_free(b);
}

static void test_activate_offscreen_button(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *hits[8];
  SzSignalInt *sig;
  int n;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_button("Top", counter_tap, sig));
  sz_view_add_child(root, sz_view_sized(40, 400, sz_view_text("pad")));
  sz_view_add_child(root, sz_view_button("Low", counter_tap, sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  n = sz_view_collect_tap_targets(sz_ui_session_root(session), hits, 8);
  assert(n == 2);
  assert(strcmp(sz_view_a11y_label(hits[1]), "Low") == 0);
  assert(sz_ui_session_activate_view(session, hits[1]));
  assert(sz_signal_int_get(sig) == 1);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
}

static void test_checkbox_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_checkbox.dump";
  char *dump;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_checkbox(sig, "Done"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "checkbox:Done=0") != NULL);
  assert(strstr(dump, "[taps]") != NULL);
  assert(strstr(dump, "Done") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_switch_sizes(void) {
  SzView *sw;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  sw = sz_view_switch(sig, "On");
  sz_view_layout(sw, 200.f, 200.f, theme);
  assert(sz_view_kind(sw) == SZ_VIEW_SWITCH);
  f = sz_view_frame(sw);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(f.w > 24.f);
  sz_view_free(sw);
  sz_signal_int_free(sig);
}

static void test_switch_a11y_off_on(void) {
  SzView *sw;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  sw = sz_view_switch(sig, "On");
  dump = sz_view_a11y_dump(sw);
  assert(strstr(sz_string_cstr(dump), "switch:On=0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(sw);
  assert(strstr(sz_string_cstr(dump), "switch:On=1") != NULL);
  sz_string_free(dump);
  sz_view_free(sw);
  sz_signal_int_free(sig);
}

static void test_switch_nonzero_is_on(void) {
  SzView *sw;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(7);
  sw = sz_view_switch(sig, "X");
  dump = sz_view_a11y_dump(sw);
  assert(strstr(sz_string_cstr(dump), "switch:X=1") != NULL);
  sz_string_free(dump);
  sz_view_free(sw);
  sz_signal_int_free(sig);
}

static void test_switch_tap_toggles(void) {
  SzView *sw;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  sw = sz_view_switch(sig, "On");
  sz_view_layout(sw, 200.f, 200.f, theme);
  f = sz_view_frame(sw);
  assert(sz_view_handle_tap(sw, f.x + f.w * 0.5f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(sw, f.x + 2.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(sw);
  sz_signal_int_free(sig);
}

static void test_switch_hit_test(void) {
  SzView *sw, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  sw = sz_view_switch(sig, "On");
  sz_view_layout(sw, 200.f, 200.f, theme);
  f = sz_view_frame(sw);
  hit = sz_view_hit_test(sw, f.x + f.w * 0.5f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_SWITCH);
  assert(sz_view_is_tap_target(hit));
  sz_view_free(sw);
  sz_signal_int_free(sig);
}

static void test_switch_paint_off_on(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  sig = sz_signal_int(0);
  root = sz_view_switch(sig, "On");
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Left of track: off thumb is surface, on fill is primary. */
  assert(px_rgb(px, 80, 6, 16, 0xFF, 0xFF, 0xFF));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 6, 16, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_switch_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_switch.dump";
  char *dump;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_switch(sig, "On"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "switch:On=0") != NULL);
  assert(strstr(dump, "[taps]") != NULL);
  assert(strstr(dump, "On") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_chip_sizes(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_chip(sig, "Pin");
  sz_view_layout(ch, 200.f, 200.f, theme);
  assert(sz_view_kind(ch) == SZ_VIEW_CHIP);
  f = sz_view_frame(ch);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(f.w >= 32.f);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_chip_a11y_off_on(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  ch = sz_view_chip(sig, "Pin");
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "chip:Pin=0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "chip:Pin=1") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_chip_nonzero_is_on(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(7);
  ch = sz_view_chip(sig, "X");
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "chip:X=1") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_chip_tap_toggles(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_chip(sig, "Pin");
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  assert(sz_view_handle_tap(ch, f.x + f.w * 0.5f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(ch, f.x + 2.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_chip_hit_test(void) {
  SzView *ch, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_chip(sig, "Pin");
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  hit = sz_view_hit_test(ch, f.x + f.w * 0.5f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_CHIP);
  assert(sz_view_is_tap_target(hit));
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_chip_paint_off_on(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  root = sz_view_chip(sig, "Pin");
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Top of chip fill, above the label. Off is surface; on is primary. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFF, 0xFF));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_chip_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_chip.dump";
  char *dump;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_chip(sig, "Pin"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "chip:Pin=0") != NULL);
  assert(strstr(dump, "[taps]") != NULL);
  assert(strstr(dump, "Pin") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_list_tile_sizes(void) {
  SzView *tile;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  tile = sz_view_list_tile("milk", NULL);
  sz_view_layout(tile, 200.f, 200.f, theme);
  assert(sz_view_kind(tile) == SZ_VIEW_LIST_TILE);
  f = sz_view_frame(tile);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - 200.f) < 0.5f);
  sz_view_free(tile);
}

static void test_list_tile_unbounded_width(void) {
  SzView *tile;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  tile = sz_view_list_tile("milk", NULL);
  sz_view_layout(tile, 0.f, 80.f, theme);
  f = sz_view_frame(tile);
  assert(fabsf(f.w - 120.f) < 0.5f);
  sz_view_free(tile);
}

static void test_list_tile_a11y(void) {
  SzView *tile;
  SzString *dump;

  tile = sz_view_list_tile("milk", NULL);
  dump = sz_view_a11y_dump(tile);
  assert(strstr(sz_string_cstr(dump), "listtile:milk") != NULL);
  sz_string_free(dump);
  sz_view_free(tile);
}

static void test_list_tile_not_tap_target(void) {
  SzView *tile;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  tile = sz_view_list_tile("milk", NULL);
  sz_view_layout(tile, 200.f, 80.f, theme);
  f = sz_view_frame(tile);
  assert(!sz_view_is_tap_target(tile));
  assert(sz_view_hit_test(tile, f.x + 8.f, f.y + f.h * 0.5f) == NULL);
  sz_view_free(tile);
}

static void test_list_tile_trailing_tap(void) {
  SzView *tile, *btn, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  btn = sz_view_button("Del", counter_tap, sig);
  tile = sz_view_list_tile("milk", btn);
  sz_view_layout(tile, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(tile, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_handle_tap(tile, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(fabsf(sz_view_frame(btn).h - theme->control_h) < 0.5f);
  sz_view_free(tile);
  sz_signal_int_free(sig);
}

static void test_list_tile_paint(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  root = sz_view_list_tile("milk", NULL);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFF, 0xFF));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_list_tile_trailing_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_listtile.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_list_tile("milk", sz_view_button("Del", counter_tap, sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "listtile:milk") != NULL);
  assert(strstr(dump, "button:Del") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "listtile") == NULL);
  assert(strstr(taps, "Del") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_badge_sizes(void) {
  SzView *badge, *ch;
  SzSignalInt *sig, *pin;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  pin = sz_signal_int(0);
  sig = sz_signal_int(3);
  ch = sz_view_chip(pin, "Pin");
  badge = sz_view_badge(sig, ch);
  sz_view_layout(badge, 200.f, 200.f, theme);
  assert(sz_view_kind(badge) == SZ_VIEW_BADGE);
  f = sz_view_frame(badge);
  cf = sz_view_frame(ch);
  assert(fabsf(f.w - cf.w) < 0.5f);
  assert(fabsf(f.h - cf.h) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(badge);
  sz_signal_int_free(sig);
  sz_signal_int_free(pin);
}

static void test_badge_a11y(void) {
  SzView *badge;
  SzSignalInt *sig, *pin;
  SzString *dump;

  pin = sz_signal_int(0);
  sig = sz_signal_int(0);
  badge = sz_view_badge(sig, sz_view_chip(pin, "Pin"));
  dump = sz_view_a11y_dump(badge);
  assert(strstr(sz_string_cstr(dump), "badge:0") != NULL);
  assert(strstr(sz_string_cstr(dump), "chip:Pin=0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 3);
  dump = sz_view_a11y_dump(badge);
  assert(strstr(sz_string_cstr(dump), "badge:3") != NULL);
  sz_string_free(dump);
  sz_view_free(badge);
  sz_signal_int_free(sig);
  sz_signal_int_free(pin);
}

static void test_badge_not_tap_target(void) {
  SzView *badge;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(3);
  badge = sz_view_badge(sig, sz_view_text("x"));
  sz_view_layout(badge, 200.f, 80.f, theme);
  f = sz_view_frame(badge);
  assert(!sz_view_is_tap_target(badge));
  assert(sz_view_hit_test(badge, f.x + 4.f, f.y + f.h * 0.5f) == NULL);
  assert(sz_view_handle_tap(badge, f.x + 4.f, f.y + f.h * 0.5f) == 0);
  assert(sz_signal_int_get(sig) == 3);
  sz_view_free(badge);
  sz_signal_int_free(sig);
}

static void test_badge_child_tap(void) {
  SzView *badge, *ch, *hit;
  SzSignalInt *sig, *pin;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  pin = sz_signal_int(0);
  sig = sz_signal_int(3);
  ch = sz_view_chip(pin, "Pin");
  badge = sz_view_badge(sig, ch);
  sz_view_layout(badge, 200.f, 80.f, theme);
  f = sz_view_frame(ch);
  hit = sz_view_hit_test(badge, f.x + 8.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_CHIP);
  assert(sz_view_handle_tap(badge, f.x + 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(pin) == 1);
  assert(sz_signal_int_get(sig) == 3);
  sz_view_free(badge);
  sz_signal_int_free(sig);
  sz_signal_int_free(pin);
}

static void test_badge_paint_mark(void) {
  SzView *root;
  SzSignalInt *sig, *pin;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  pin = sz_signal_int(0);
  sig = sz_signal_int(3);
  root = sz_view_badge(sig, sz_view_chip(pin, "Pin"));
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Off chip fill stays surface; badge mark is primary at top-right. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFF, 0xFF));
  assert(px_rgb(px, 80, (int)(f.x + f.w - 4.f), (int)(f.y + 4.f), 0x14, 0x28,
                0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
  sz_signal_int_free(pin);
}

static void test_badge_child_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig, *pin;
  const char *path = "/tmp/scuzz_ui_badge.dump";
  char *dump;
  const char *taps;

  pin = sz_signal_int(0);
  sig = sz_signal_int(3);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_badge(sig, sz_view_chip(pin, "Pin")));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "badge:3") != NULL);
  assert(strstr(dump, "chip:Pin=0") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "badge") == NULL);
  assert(strstr(taps, "Pin") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  sz_signal_int_free(pin);
  remove(path);
}

static void test_card_sizes(void) {
  SzView *card, *btn;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, sig);
  card = sz_view_card(btn);
  sz_view_layout(card, 200.f, 200.f, theme);
  assert(sz_view_kind(card) == SZ_VIEW_CARD);
  f = sz_view_frame(card);
  cf = sz_view_frame(btn);
  assert(fabsf(f.w - (cf.w + theme->pad * 2.f)) < 0.5f);
  assert(fabsf(f.h - (cf.h + theme->pad * 2.f)) < 0.5f);
  sz_view_free(card);
  sz_signal_int_free(sig);
}

static void test_card_empty_sizes(void) {
  SzView *card;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  card = sz_view_card(NULL);
  sz_view_layout(card, 200.f, 200.f, theme);
  f = sz_view_frame(card);
  assert(fabsf(f.w - theme->pad * 2.f) < 0.5f);
  assert(fabsf(f.h - theme->pad * 2.f) < 0.5f);
  sz_view_free(card);
}

static void test_card_a11y(void) {
  SzView *card;
  SzString *dump;

  card = sz_view_card(sz_view_text("Hi"));
  dump = sz_view_a11y_dump(card);
  assert(strstr(sz_string_cstr(dump), "card:card") != NULL);
  assert(strstr(sz_string_cstr(dump), "text:Hi") != NULL);
  sz_string_free(dump);
  sz_view_free(card);
}

static void test_card_not_tap_target(void) {
  SzView *card;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  card = sz_view_card(sz_view_text("Hi"));
  sz_view_layout(card, 200.f, 80.f, theme);
  f = sz_view_frame(card);
  assert(!sz_view_is_tap_target(card));
  assert(sz_view_hit_test(card, f.x + 2.f, f.y + 2.f) == NULL);
  sz_view_free(card);
}

static void test_card_child_tap(void) {
  SzView *card, *btn, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, sig);
  card = sz_view_card(btn);
  sz_view_layout(card, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(card, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_handle_tap(card, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(card);
  sz_signal_int_free(sig);
}

static void test_card_paint_pad(void) {
  SzView *root, *btn;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, sig);
  root = sz_view_card(btn);
  surf = sk_surface_make_raster_n32_premul(120, 120);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 120, 120, theme));
  f = sz_view_frame(root);
  cf = sz_view_frame(btn);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 120 * 120 * 4);
  /* Pad ring is surface. Child button fill is primary. */
  assert(px_rgb(px, 120, (int)(f.x + 2.f), (int)(f.y + 2.f), 0xFF, 0xFF, 0xFF));
  assert(px_rgb(px, 120, (int)(cf.x + 8.f), (int)(cf.y + 4.f), 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_card_child_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_card.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_card(sz_view_button("Go", counter_tap, sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "card:card") != NULL);
  assert(strstr(dump, "button:Go") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "card") == NULL);
  assert(strstr(taps, "Go") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_divider_sizes(void) {
  SzView *d;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  d = sz_view_divider();
  sz_view_layout(d, 200.f, 200.f, theme);
  assert(sz_view_kind(d) == SZ_VIEW_DIVIDER);
  f = sz_view_frame(d);
  assert(fabsf(f.h - 8.f) < 0.5f);
  assert(fabsf(f.w - 200.f) < 0.5f);
  sz_view_free(d);
}

static void test_divider_unbounded_width(void) {
  SzView *d;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  d = sz_view_divider();
  sz_view_layout(d, 0.f, 80.f, theme);
  f = sz_view_frame(d);
  assert(fabsf(f.w - 120.f) < 0.5f);
  assert(fabsf(f.h - 8.f) < 0.5f);
  sz_view_free(d);
}

static void test_divider_a11y(void) {
  SzView *d;
  SzString *dump;

  d = sz_view_divider();
  dump = sz_view_a11y_dump(d);
  assert(strstr(sz_string_cstr(dump), "divider:divider") != NULL);
  sz_string_free(dump);
  sz_view_free(d);
}

static void test_divider_not_tap_target(void) {
  SzView *d;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  d = sz_view_divider();
  sz_view_layout(d, 200.f, 80.f, theme);
  f = sz_view_frame(d);
  assert(!sz_view_is_tap_target(d));
  assert(sz_view_hit_test(d, f.x + f.w * 0.5f, f.y + f.h * 0.5f) == NULL);
  assert(sz_view_handle_tap(d, f.x + f.w * 0.5f, f.y + f.h * 0.5f) == 0);
  sz_view_free(d);
}

static void test_divider_paint_line(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int lx, ly, by;

  root = sz_view_divider();
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  lx = (int)(f.x + 8.f);
  ly = (int)(f.y + f.h * 0.5f);
  by = (int)(f.y + 1.f);
  /* Hairline is muted. Slot above the line stays the canvas background. */
  assert(px_rgb(px, 80, lx, ly, 0x6A, 0x6A, 0x6A));
  assert(px_rgb(px, 80, lx, by, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_divider_in_column(void) {
  SzView *col, *d;
  const SzTheme *theme = sz_theme_default();
  SzRect f, df;

  col = sz_view_column();
  d = sz_view_divider();
  sz_view_add_child(col, sz_view_text("Hi"));
  sz_view_add_child(col, d);
  sz_view_layout(col, 200.f, 200.f, theme);
  f = sz_view_frame(col);
  df = sz_view_frame(d);
  assert(fabsf(df.h - 8.f) < 0.5f);
  assert(f.h > df.h);
  sz_view_free(col);
}

static void test_divider_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_divider.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_divider());
  sz_view_add_child(root, sz_view_button("Go", counter_tap, sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "divider:divider") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "divider") == NULL);
  assert(strstr(taps, "Go") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_expansion_tile_sizes_collapsed(void) {
  SzView *tile;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  tile = sz_view_expansion_tile(sig, "More", sz_view_text("hint"));
  sz_view_layout(tile, 200.f, 200.f, theme);
  assert(sz_view_kind(tile) == SZ_VIEW_EXPANSION_TILE);
  f = sz_view_frame(tile);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - 200.f) < 0.5f);
  sz_view_free(tile);
  sz_signal_int_free(sig);
}

static void test_expansion_tile_sizes_expanded(void) {
  SzView *tile, *child;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  sig = sz_signal_int(1);
  child = sz_view_text("hint");
  tile = sz_view_expansion_tile(sig, "More", child);
  sz_view_layout(tile, 200.f, 200.f, theme);
  f = sz_view_frame(tile);
  cf = sz_view_frame(child);
  assert(f.h > theme->control_h);
  assert(fabsf(f.h - (theme->control_h + cf.h)) < 0.5f);
  sz_view_free(tile);
  sz_signal_int_free(sig);
}

static void test_expansion_tile_a11y(void) {
  SzView *tile;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  tile = sz_view_expansion_tile(sig, "More", sz_view_text("hint"));
  dump = sz_view_a11y_dump(tile);
  assert(strstr(sz_string_cstr(dump), "expansion:More=0") != NULL);
  assert(strstr(sz_string_cstr(dump), "hint") == NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(tile);
  assert(strstr(sz_string_cstr(dump), "expansion:More=1") != NULL);
  assert(strstr(sz_string_cstr(dump), "text:hint") != NULL);
  sz_string_free(dump);
  sz_view_free(tile);
  sz_signal_int_free(sig);
}

static void test_expansion_tile_tap_toggles(void) {
  SzView *tile;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  tile = sz_view_expansion_tile(sig, "More", sz_view_text("hint"));
  sz_view_layout(tile, 200.f, 80.f, theme);
  f = sz_view_frame(tile);
  assert(sz_view_handle_tap(tile, f.x + 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_layout(tile, 200.f, 80.f, theme);
  f = sz_view_frame(tile);
  assert(sz_view_handle_tap(tile, f.x + 8.f, f.y + theme->control_h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(tile);
  sz_signal_int_free(sig);
}

static void test_expansion_tile_child_tap(void) {
  SzView *tile, *btn, *hit;
  SzSignalInt *open, *count;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  open = sz_signal_int(1);
  count = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, count);
  tile = sz_view_expansion_tile(open, "More", btn);
  sz_view_layout(tile, 200.f, 120.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(tile, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_handle_tap(tile, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(count) == 1);
  assert(sz_signal_int_get(open) == 1);
  sz_view_free(tile);
  sz_signal_int_free(open);
  sz_signal_int_free(count);
}

static void test_expansion_tile_paint(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  root = sz_view_expansion_tile(sig, "More", sz_view_text("hint"));
  surf = sk_surface_make_raster_n32_premul(120, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 120, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 120 * 80 * 4);
  assert(px_rgb(px, 120, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFF, 0xFF));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_expansion_tile_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_expansion.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_expansion_tile(sig, "More", sz_view_text("hint")));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "expansion:More=0") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "More") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_icon_button_sizes(void) {
  SzView *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_icon_button("i", counter_tap, sig);
  sz_view_layout(b, 200.f, 200.f, theme);
  assert(sz_view_kind(b) == SZ_VIEW_ICON_BUTTON);
  f = sz_view_frame(b);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - theme->control_h) < 0.5f);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_icon_button_a11y(void) {
  SzView *b;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  b = sz_view_icon_button("i", counter_tap, sig);
  dump = sz_view_a11y_dump(b);
  assert(strstr(sz_string_cstr(dump), "iconbutton:i") != NULL);
  sz_string_free(dump);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_icon_button_tap(void) {
  SzView *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_icon_button("i", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(sz_view_is_tap_target(b));
  assert(sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_icon_button_hit_test(void) {
  SzView *b, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_icon_button("i", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  hit = sz_view_hit_test(b, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_ICON_BUTTON);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_icon_button_paint(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  root = sz_view_icon_button("i", counter_tap, sig);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Fill is surface, not primary. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFF, 0xFF));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_icon_button_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_iconbutton.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_icon_button("i", counter_tap, sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "iconbutton:i") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "i") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_vertical_divider_sizes(void) {
  SzView *d;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  d = sz_view_vertical_divider();
  sz_view_layout(d, 200.f, 200.f, theme);
  assert(sz_view_kind(d) == SZ_VIEW_VERTICAL_DIVIDER);
  f = sz_view_frame(d);
  assert(fabsf(f.w - 8.f) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(d);
}

static void test_vertical_divider_tight_height(void) {
  SzView *d;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  d = sz_view_vertical_divider();
  sz_view_layout(d, 80.f, 20.f, theme);
  f = sz_view_frame(d);
  assert(fabsf(f.h - 20.f) < 0.5f);
  assert(fabsf(f.w - 8.f) < 0.5f);
  sz_view_free(d);
}

static void test_vertical_divider_a11y(void) {
  SzView *d;
  SzString *dump;

  d = sz_view_vertical_divider();
  dump = sz_view_a11y_dump(d);
  assert(strstr(sz_string_cstr(dump), "vdiv:vdiv") != NULL);
  sz_string_free(dump);
  sz_view_free(d);
}

static void test_vertical_divider_not_tap_target(void) {
  SzView *d;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  d = sz_view_vertical_divider();
  sz_view_layout(d, 80.f, 80.f, theme);
  f = sz_view_frame(d);
  assert(!sz_view_is_tap_target(d));
  assert(sz_view_hit_test(d, f.x + f.w * 0.5f, f.y + f.h * 0.5f) == NULL);
  assert(sz_view_handle_tap(d, f.x + f.w * 0.5f, f.y + f.h * 0.5f) == 0);
  sz_view_free(d);
}

static void test_vertical_divider_paint_line(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int lx, ly, bx;

  root = sz_view_vertical_divider();
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  lx = (int)(f.x + f.w * 0.5f);
  ly = (int)(f.y + 8.f);
  bx = (int)(f.x + 1.f);
  /* Hairline is muted. Slot left of the line stays the canvas background. */
  assert(px_rgb(px, 80, lx, ly, 0x6A, 0x6A, 0x6A));
  assert(px_rgb(px, 80, bx, ly, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_vertical_divider_in_row(void) {
  SzView *row, *d;
  const SzTheme *theme = sz_theme_default();
  SzRect f, df;

  row = sz_view_row();
  d = sz_view_vertical_divider();
  sz_view_add_child(row, sz_view_button("Go", NULL, NULL));
  sz_view_add_child(row, d);
  sz_view_layout(row, 200.f, 200.f, theme);
  f = sz_view_frame(row);
  df = sz_view_frame(d);
  assert(fabsf(df.w - 8.f) < 0.5f);
  assert(fabsf(df.h - theme->control_h) < 0.5f);
  assert(fabsf(f.h - (theme->control_h + theme->pad * 2.f)) < 0.5f);
  sz_view_free(row);
}

static void test_vertical_divider_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_vdiv.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_row();
  sz_view_add_child(root, sz_view_vertical_divider());
  sz_view_add_child(root, sz_view_button("Go", counter_tap, sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "vdiv:vdiv") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "vdiv") == NULL);
  assert(strstr(taps, "Go") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_circular_progress_sizes(void) {
  SzView *p;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(40);
  p = sz_view_circular_progress(sig);
  sz_view_layout(p, 200.f, 200.f, theme);
  assert(sz_view_kind(p) == SZ_VIEW_CIRCULAR_PROGRESS);
  f = sz_view_frame(p);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - theme->control_h) < 0.5f);
  sz_view_free(p);
  sz_signal_int_free(sig);
}

static void test_circular_progress_a11y(void) {
  SzView *p;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(40);
  p = sz_view_circular_progress(sig);
  dump = sz_view_a11y_dump(p);
  assert(strstr(sz_string_cstr(dump), "circular:40") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 7);
  dump = sz_view_a11y_dump(p);
  assert(strstr(sz_string_cstr(dump), "circular:7") != NULL);
  sz_string_free(dump);
  sz_view_free(p);
  sz_signal_int_free(sig);
}

static void test_circular_progress_clamps_a11y(void) {
  SzView *p;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(150);
  p = sz_view_circular_progress(sig);
  dump = sz_view_a11y_dump(p);
  assert(strstr(sz_string_cstr(dump), "circular:100") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, -3);
  dump = sz_view_a11y_dump(p);
  assert(strstr(sz_string_cstr(dump), "circular:0") != NULL);
  sz_string_free(dump);
  sz_view_free(p);
  sz_signal_int_free(sig);
}

static void test_circular_progress_not_tap_target(void) {
  SzView *p;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(40);
  p = sz_view_circular_progress(sig);
  sz_view_layout(p, 80.f, 80.f, theme);
  f = sz_view_frame(p);
  assert(!sz_view_is_tap_target(p));
  assert(sz_view_hit_test(p, f.x + f.w * 0.5f, f.y + f.h * 0.5f) == NULL);
  assert(sz_view_handle_tap(p, f.x + f.w * 0.5f, f.y + f.h * 0.5f) == 0);
  assert(sz_signal_int_get(sig) == 40);
  sz_view_free(p);
  sz_signal_int_free(sig);
}

static void test_circular_progress_paint_ring(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int tx, ty, cx, cy;

  sig = sz_signal_int(100);
  root = sz_view_circular_progress(sig);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(root);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  tx = (int)(f.x + f.w * 0.5f);
  ty = (int)(f.y + 1.f);
  cx = (int)(f.x + f.w * 0.5f);
  cy = (int)(f.y + f.h * 0.5f);
  /* Full ring is primary. Hole stays the canvas background. */
  assert(px_rgb(px, 80, tx, ty, 0x14, 0x28, 0x50));
  assert(px_rgb(px, 80, cx, cy, 0xF5, 0xF5, 0xF5));
  sz_signal_int_set(sig, 0);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, tx, ty, 0x6A, 0x6A, 0x6A));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_circular_progress_in_row(void) {
  SzView *row, *p;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, pf;

  sig = sz_signal_int(40);
  row = sz_view_row();
  p = sz_view_circular_progress(sig);
  sz_view_add_child(row, sz_view_button("Go", NULL, NULL));
  sz_view_add_child(row, p);
  sz_view_layout(row, 200.f, 200.f, theme);
  f = sz_view_frame(row);
  pf = sz_view_frame(p);
  assert(fabsf(pf.w - theme->control_h) < 0.5f);
  assert(fabsf(pf.h - theme->control_h) < 0.5f);
  assert(fabsf(f.h - (theme->control_h + theme->pad * 2.f)) < 0.5f);
  sz_view_free(row);
  sz_signal_int_free(sig);
}

static void test_circular_progress_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_circular.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(40);
  root = sz_view_row();
  sz_view_add_child(root, sz_view_circular_progress(sig));
  sz_view_add_child(root, sz_view_button("Go", counter_tap, sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "circular:40") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "circular") == NULL);
  assert(strstr(taps, "Go") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_avatar_sizes(void) {
  SzView *a;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  a = sz_view_avatar("S");
  sz_view_layout(a, 200.f, 200.f, theme);
  assert(sz_view_kind(a) == SZ_VIEW_AVATAR);
  f = sz_view_frame(a);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - theme->control_h) < 0.5f);
  sz_view_free(a);
}

static void test_avatar_a11y(void) {
  SzView *a;
  SzString *dump;

  a = sz_view_avatar("S");
  dump = sz_view_a11y_dump(a);
  assert(strstr(sz_string_cstr(dump), "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(a);
}

static void test_avatar_not_tap_target(void) {
  SzView *a;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  a = sz_view_avatar("S");
  sz_view_layout(a, 80.f, 80.f, theme);
  f = sz_view_frame(a);
  assert(!sz_view_is_tap_target(a));
  assert(sz_view_hit_test(a, f.x + f.w * 0.5f, f.y + f.h * 0.5f) == NULL);
  assert(sz_view_handle_tap(a, f.x + f.w * 0.5f, f.y + f.h * 0.5f) == 0);
  sz_view_free(a);
}

static void test_avatar_paint_disc(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int mx, my, cx, cy;

  root = sz_view_avatar("S");
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(root);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  mx = (int)(f.x + f.w * 0.5f);
  my = (int)(f.y + 6.f);
  cx = (int)(f.x + 1.f);
  cy = (int)(f.y + 1.f);
  /* Disc fill is primary. Square corner stays the canvas background. */
  assert(px_rgb(px, 80, mx, my, 0x14, 0x28, 0x50));
  assert(px_rgb(px, 80, cx, cy, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_avatar_in_row(void) {
  SzView *row, *a;
  const SzTheme *theme = sz_theme_default();
  SzRect f, af;

  row = sz_view_row();
  a = sz_view_avatar("S");
  sz_view_add_child(row, sz_view_button("Go", NULL, NULL));
  sz_view_add_child(row, a);
  sz_view_layout(row, 200.f, 200.f, theme);
  f = sz_view_frame(row);
  af = sz_view_frame(a);
  assert(fabsf(af.w - theme->control_h) < 0.5f);
  assert(fabsf(af.h - theme->control_h) < 0.5f);
  assert(fabsf(f.h - (theme->control_h + theme->pad * 2.f)) < 0.5f);
  sz_view_free(row);
}

static void test_avatar_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_avatar.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_row();
  sz_view_add_child(root, sz_view_avatar("S"));
  sz_view_add_child(root, sz_view_button("Go", counter_tap, sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "avatar:S") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "avatar") == NULL);
  assert(strstr(taps, "Go") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_checkbox_list_tile_sizes(void) {
  SzView *t;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_checkbox_list_tile(sig, "Star");
  sz_view_layout(t, 200.f, 200.f, theme);
  assert(sz_view_kind(t) == SZ_VIEW_CHECKBOX_LIST_TILE);
  f = sz_view_frame(t);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - 200.f) < 0.5f);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_checkbox_list_tile_a11y(void) {
  SzView *t;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  t = sz_view_checkbox_list_tile(sig, "Star");
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "checktile:Star=0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "checktile:Star=1") != NULL);
  sz_string_free(dump);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_checkbox_list_tile_tap(void) {
  SzView *t;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_checkbox_list_tile(sig, "Star");
  sz_view_layout(t, 200.f, 80.f, theme);
  f = sz_view_frame(t);
  assert(sz_view_is_tap_target(t));
  assert(sz_view_handle_tap(t, f.x + 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(t, f.x + 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_checkbox_list_tile_hit_test(void) {
  SzView *t, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_checkbox_list_tile(sig, "Star");
  sz_view_layout(t, 200.f, 80.f, theme);
  f = sz_view_frame(t);
  hit = sz_view_hit_test(t, f.x + 8.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_CHECKBOX_LIST_TILE);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_checkbox_list_tile_paint(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int bx, by;

  sig = sz_signal_int(0);
  root = sz_view_checkbox_list_tile(sig, "Star");
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(root);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  bx = (int)(f.x + theme->pad + 6.f);
  by = (int)(f.y + f.h * 0.5f);
  /* Off box fill is surface. On box fill is primary. */
  assert(px_rgb(px, 80, bx, by, 0xFF, 0xFF, 0xFF));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, bx, by, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_checkbox_list_tile_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_checktile.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_checkbox_list_tile(sig, "Star"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "checktile:Star=0") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Star") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_switch_list_tile_sizes(void) {
  SzView *t;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_switch_list_tile(sig, "Quiet");
  sz_view_layout(t, 200.f, 200.f, theme);
  assert(sz_view_kind(t) == SZ_VIEW_SWITCH_LIST_TILE);
  f = sz_view_frame(t);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - 200.f) < 0.5f);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_switch_list_tile_a11y(void) {
  SzView *t;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  t = sz_view_switch_list_tile(sig, "Quiet");
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "switchtile:Quiet=0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "switchtile:Quiet=1") != NULL);
  sz_string_free(dump);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_switch_list_tile_tap(void) {
  SzView *t;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_switch_list_tile(sig, "Quiet");
  sz_view_layout(t, 200.f, 80.f, theme);
  f = sz_view_frame(t);
  assert(sz_view_is_tap_target(t));
  assert(sz_view_handle_tap(t, f.x + 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(t, f.x + 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_switch_list_tile_hit_test(void) {
  SzView *t, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_switch_list_tile(sig, "Quiet");
  sz_view_layout(t, 200.f, 80.f, theme);
  f = sz_view_frame(t);
  hit = sz_view_hit_test(t, f.x + 8.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_SWITCH_LIST_TILE);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_switch_list_tile_paint(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  float box, tw;
  int sx, sy;

  sig = sz_signal_int(0);
  root = sz_view_switch_list_tile(sig, "Quiet");
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(root);
  box = theme->font_px + 4.f;
  if (box < 12.f)
    box = 12.f;
  if (box > theme->control_h - 4.f)
    box = theme->control_h - 4.f;
  tw = box * 2.f;
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  sx = (int)(f.x + f.w - theme->pad - tw + 6.f);
  sy = (int)(f.y + f.h * 0.5f);
  /* Off thumb is surface. On track fill is primary. */
  assert(px_rgb(px, 80, sx, sy, 0xFF, 0xFF, 0xFF));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, sx, sy, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_switch_list_tile_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_switchtile.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_switch_list_tile(sig, "Quiet"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "switchtile:Quiet=0") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Quiet") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_radio_list_tile_sizes(void) {
  SzView *t;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_radio_list_tile(sig, 1, "Night");
  sz_view_layout(t, 200.f, 200.f, theme);
  assert(sz_view_kind(t) == SZ_VIEW_RADIO_LIST_TILE);
  f = sz_view_frame(t);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - 200.f) < 0.5f);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_radio_list_tile_a11y(void) {
  SzView *t;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  t = sz_view_radio_list_tile(sig, 1, "Night");
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "radiotile:Night=0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "radiotile:Night=1") != NULL);
  sz_string_free(dump);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_radio_list_tile_tap(void) {
  SzView *t;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_radio_list_tile(sig, 2, "Night");
  sz_view_layout(t, 200.f, 80.f, theme);
  f = sz_view_frame(t);
  assert(sz_view_is_tap_target(t));
  assert(sz_view_handle_tap(t, f.x + 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 2);
  assert(sz_view_handle_tap(t, f.x + 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 2);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_radio_list_tile_group_exclusive(void) {
  SzView *col, *a, *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzString *dump;
  SzRect f;

  sig = sz_signal_int(0);
  col = sz_view_column();
  a = sz_view_radio_list_tile(sig, 0, "Day");
  b = sz_view_radio_list_tile(sig, 1, "Night");
  sz_view_add_child(col, a);
  sz_view_add_child(col, b);
  sz_view_layout(col, 200.f, 200.f, theme);
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "radiotile:Day=1") != NULL);
  assert(strstr(sz_string_cstr(dump), "radiotile:Night=0") != NULL);
  sz_string_free(dump);
  f = sz_view_frame(b);
  assert(sz_view_handle_tap(col, f.x + 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "radiotile:Day=0") != NULL);
  assert(strstr(sz_string_cstr(dump), "radiotile:Night=1") != NULL);
  sz_string_free(dump);
  sz_view_free(col);
  sz_signal_int_free(sig);
}

static void test_radio_list_tile_hit_test(void) {
  SzView *t, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_radio_list_tile(sig, 1, "Night");
  sz_view_layout(t, 200.f, 80.f, theme);
  f = sz_view_frame(t);
  hit = sz_view_hit_test(t, f.x + 8.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_RADIO_LIST_TILE);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_radio_list_tile_paint(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int bx, by;

  sig = sz_signal_int(0);
  root = sz_view_radio_list_tile(sig, 1, "Night");
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(root);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  bx = (int)(f.x + theme->pad + 6.f);
  by = (int)(f.y + f.h * 0.5f);
  /* Off inner is surface. On inner is primary. */
  assert(px_rgb(px, 80, bx, by, 0xFF, 0xFF, 0xFF));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, bx, by, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_radio_list_tile_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_radiotile.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_radio_list_tile(sig, 1, "Night"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "radiotile:Night=0") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Night") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_segmented_sizes(void) {
  SzView *t;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_segmented(sig, "List", "Grid");
  sz_view_layout(t, 200.f, 200.f, theme);
  assert(sz_view_kind(t) == SZ_VIEW_SEGMENTED);
  f = sz_view_frame(t);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - 200.f) < 0.5f);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_segmented_a11y(void) {
  SzView *t;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  t = sz_view_segmented(sig, "List", "Grid");
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "segmented:0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "segmented:1") != NULL);
  sz_string_free(dump);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_segmented_tap(void) {
  SzView *t;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_segmented(sig, "List", "Grid");
  sz_view_layout(t, 200.f, 80.f, theme);
  f = sz_view_frame(t);
  assert(sz_view_is_tap_target(t));
  assert(sz_view_handle_tap(t, f.x + f.w - 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(t, f.x + 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  assert(sz_view_handle_tap(t, f.x + 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_segmented_hit_test(void) {
  SzView *t, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_segmented(sig, "List", "Grid");
  sz_view_layout(t, 200.f, 80.f, theme);
  f = sz_view_frame(t);
  hit = sz_view_hit_test(t, f.x + 8.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_SEGMENTED);
  hit = sz_view_hit_test(t, f.x + f.w - 8.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_SEGMENTED);
  sz_view_free(t);
  sz_signal_int_free(sig);
}

static void test_segmented_paint(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int lx, rx, y;

  sig = sz_signal_int(0);
  root = sz_view_segmented(sig, "List", "Grid");
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(root);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  lx = (int)(f.x + f.w * 0.25f);
  rx = (int)(f.x + f.w * 0.75f);
  y = (int)(f.y + 4.f);
  /* Left selected is primary. Right selected is primary. */
  assert(px_rgb(px, 80, lx, y, 0x14, 0x28, 0x50));
  assert(px_rgb(px, 80, rx, y, 0xFF, 0xFF, 0xFF));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, lx, y, 0xFF, 0xFF, 0xFF));
  assert(px_rgb(px, 80, rx, y, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_segmented_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_segmented.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_segmented(sig, "List", "Grid"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "segmented:0") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "segmented") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_fab_sizes(void) {
  SzView *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_fab("+", counter_tap, sig);
  sz_view_layout(b, 200.f, 200.f, theme);
  assert(sz_view_kind(b) == SZ_VIEW_FAB);
  f = sz_view_frame(b);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - theme->control_h) < 0.5f);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_fab_a11y(void) {
  SzView *b;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  b = sz_view_fab("+", counter_tap, sig);
  dump = sz_view_a11y_dump(b);
  assert(strstr(sz_string_cstr(dump), "fab:+") != NULL);
  sz_string_free(dump);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_fab_tap(void) {
  SzView *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_fab("+", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(sz_view_is_tap_target(b));
  assert(sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_fab_hit_test(void) {
  SzView *b, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_fab("+", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  hit = sz_view_hit_test(b, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_FAB);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_fab_paint(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  root = sz_view_fab("+", counter_tap, sig);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Disc fill is primary, not surface. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_fab_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_fab.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_fab("+", counter_tap, sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "fab:+") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "+") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_fab_tap_twice(void) {
  SzView *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_fab("+", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 2);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_fab_a11y_distinct(void) {
  SzView *b;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(0);
  b = sz_view_fab("+", counter_tap, sig);
  dump = sz_view_a11y_dump(b);
  s = sz_string_cstr(dump);
  assert(strstr(s, "fab:+") != NULL);
  assert(strstr(s, "iconbutton:") == NULL);
  assert(strstr(s, "button:") == NULL);
  sz_string_free(dump);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_tooltip_sizes(void) {
  SzView *tip, *av;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  av = sz_view_avatar("S");
  tip = sz_view_tooltip("Sean", av);
  sz_view_layout(tip, 200.f, 200.f, theme);
  assert(sz_view_kind(tip) == SZ_VIEW_TOOLTIP);
  f = sz_view_frame(tip);
  cf = sz_view_frame(av);
  assert(fabsf(f.w - cf.w) < 0.5f);
  assert(fabsf(f.h - cf.h) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(tip);
}

static void test_tooltip_empty_sizes(void) {
  SzView *tip;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  tip = sz_view_tooltip("Sean", NULL);
  sz_view_layout(tip, 200.f, 200.f, theme);
  f = sz_view_frame(tip);
  assert(fabsf(f.w) < 0.5f);
  assert(fabsf(f.h) < 0.5f);
  sz_view_free(tip);
}

static void test_tooltip_a11y(void) {
  SzView *tip;
  SzString *dump;
  const char *s;

  tip = sz_view_tooltip("Sean", sz_view_avatar("S"));
  dump = sz_view_a11y_dump(tip);
  s = sz_string_cstr(dump);
  assert(strstr(s, "tooltip:Sean") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(tip);
}

static void test_tooltip_not_tap_target(void) {
  SzView *tip;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  tip = sz_view_tooltip("Sean", sz_view_avatar("S"));
  sz_view_layout(tip, 200.f, 80.f, theme);
  f = sz_view_frame(tip);
  assert(!sz_view_is_tap_target(tip));
  assert(sz_view_hit_test(tip, f.x + 4.f, f.y + f.h * 0.5f) == NULL);
  sz_view_free(tip);
}

static void test_tooltip_child_tap(void) {
  SzView *tip, *btn, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, sig);
  tip = sz_view_tooltip("hint", btn);
  sz_view_layout(tip, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(tip, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_handle_tap(tip, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(tip);
  sz_signal_int_free(sig);
}

static void test_tooltip_paint_child(void) {
  SzView *root, *av;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int mx, my;

  av = sz_view_avatar("S");
  root = sz_view_tooltip("Sean", av);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(av);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  mx = (int)(f.x + f.w * 0.5f);
  my = (int)(f.y + 6.f);
  /* Child disc fill is primary. Tooltip adds no pad fill. */
  assert(px_rgb(px, 80, mx, my, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_tooltip_paint_hover(void) {
  SzView *root, *av;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int mx, my;

  av = sz_view_avatar("S");
  root = sz_view_tooltip("Sean", av);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(root);
  assert(sz_view_set_hover_at(root, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_view_tooltip_at(root, f.x + 4.f, f.y + f.h * 0.5f) == root);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  mx = (int)(f.x + 4.f);
  my = (int)(f.y + f.h + 6.f);
  if (my >= 80)
    my = 79;
  /* Hover bubble is surface white, not the page background. */
  assert(px_rgb(px, 80, mx, my, 0xFF, 0xFF, 0xFF));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_tooltip_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  const char *path = "/tmp/scuzz_ui_tooltip.dump";
  char *dump;
  const char *taps;

  root = sz_view_column();
  sz_view_add_child(root, sz_view_tooltip("Sean", sz_view_avatar("S")));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "tooltip:Sean") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Sean") == NULL);
  assert(strstr(taps, "tooltip") == NULL);
  free(dump);
  sz_ui_unmount(session);
  remove(path);
}

static void test_tooltip_child_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_tooltip_child.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_tooltip("hint", sz_view_button("Go", counter_tap, sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "tooltip:hint") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Go") != NULL);
  assert(strstr(taps, "hint") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_tooltip_same_origin(void) {
  SzView *tip, *av;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  av = sz_view_avatar("S");
  tip = sz_view_tooltip("Sean", av);
  sz_view_layout(tip, 200.f, 200.f, theme);
  f = sz_view_frame(tip);
  cf = sz_view_frame(av);
  assert(fabsf(f.x - cf.x) < 0.5f);
  assert(fabsf(f.y - cf.y) < 0.5f);
  sz_view_free(tip);
}

static void test_tooltip_empty_message_a11y(void) {
  SzView *tip;
  SzString *dump;

  tip = sz_view_tooltip("", sz_view_avatar("S"));
  dump = sz_view_a11y_dump(tip);
  assert(strstr(sz_string_cstr(dump), "tooltip:") != NULL);
  assert(strstr(sz_string_cstr(dump), "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(tip);
}

static void test_outlined_button_sizes(void) {
  SzView *b, *filled;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, ff;

  sig = sz_signal_int(0);
  b = sz_view_outlined_button("Edit", counter_tap, sig);
  filled = sz_view_button("Edit", counter_tap, sig);
  sz_view_layout(b, 200.f, 200.f, theme);
  sz_view_layout(filled, 200.f, 200.f, theme);
  assert(sz_view_kind(b) == SZ_VIEW_OUTLINED_BUTTON);
  f = sz_view_frame(b);
  ff = sz_view_frame(filled);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - ff.w) < 0.5f);
  assert(fabsf(f.h - ff.h) < 0.5f);
  assert(f.w >= 48.f);
  sz_view_free(b);
  sz_view_free(filled);
  sz_signal_int_free(sig);
}

static void test_outlined_button_empty_min_width(void) {
  SzView *b;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  b = sz_view_outlined_button("", NULL, NULL);
  sz_view_layout(b, 200.f, 200.f, theme);
  f = sz_view_frame(b);
  assert(fabsf(f.w - 48.f) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(b);
}

static void test_outlined_button_null_label(void) {
  SzView *b;
  SzString *dump;
  const SzTheme *theme = sz_theme_default();

  b = sz_view_outlined_button(NULL, NULL, NULL);
  sz_view_layout(b, 200.f, 80.f, theme);
  dump = sz_view_a11y_dump(b);
  assert(strstr(sz_string_cstr(dump), "outlined:") != NULL);
  sz_string_free(dump);
  sz_view_free(b);
}

static void test_outlined_button_clamps_max_w(void) {
  SzView *b;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  b = sz_view_outlined_button("Edit", NULL, NULL);
  sz_view_layout(b, 20.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(fabsf(f.w - 20.f) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(b);
}

static void test_outlined_button_does_not_wrap(void) {
  SzView *b;
  const SzTheme *theme = sz_theme_default();
  float full_h;

  b = sz_view_outlined_button("one two", NULL, NULL);
  sz_view_layout(b, 1000.f, 100.f, theme);
  full_h = sz_view_frame(b).h;
  sz_view_free(b);

  b = sz_view_outlined_button("one two", NULL, NULL);
  sz_view_layout(b, 20.f, 100.f, theme);
  assert(fabsf(sz_view_frame(b).h - full_h) < 0.5f);
  sz_view_free(b);
}

static void test_outlined_button_a11y(void) {
  SzView *b;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  b = sz_view_outlined_button("Edit", counter_tap, sig);
  dump = sz_view_a11y_dump(b);
  assert(strstr(sz_string_cstr(dump), "outlined:Edit") != NULL);
  sz_string_free(dump);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_outlined_button_a11y_distinct(void) {
  SzView *b;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(0);
  b = sz_view_outlined_button("Edit", counter_tap, sig);
  dump = sz_view_a11y_dump(b);
  s = sz_string_cstr(dump);
  assert(strstr(s, "outlined:Edit") != NULL);
  assert(strstr(s, "button:") == NULL);
  assert(strstr(s, "iconbutton:") == NULL);
  assert(strstr(s, "fab:") == NULL);
  sz_string_free(dump);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_outlined_button_tap(void) {
  SzView *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_outlined_button("Edit", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(sz_view_is_tap_target(b));
  assert(sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_outlined_button_tap_twice(void) {
  SzView *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_outlined_button("Edit", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 2);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_outlined_button_null_tap(void) {
  SzView *b;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  b = sz_view_outlined_button("Edit", NULL, NULL);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(sz_view_is_tap_target(b));
  assert(!sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  sz_view_free(b);
}

static void test_outlined_button_miss(void) {
  SzView *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_outlined_button("Edit", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(!sz_view_handle_tap(b, f.x - 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_outlined_button_hit_test(void) {
  SzView *b, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_outlined_button("Edit", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  hit = sz_view_hit_test(b, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_OUTLINED_BUTTON);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_outlined_button_paint(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  root = sz_view_outlined_button("Edit", counter_tap, sig);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Fill is surface, not primary. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xFF, 0xFF, 0xFF));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_outlined_button_paint_not_primary(void) {
  SzView *filled, *outlined;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  filled = sz_view_button("Edit", counter_tap, sig);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(filled, canvas, 80, 80, theme));
  f = sz_view_frame(filled);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(filled);

  outlined = sz_view_outlined_button("Edit", counter_tap, sig);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(outlined, canvas, 80, 80, theme));
  f = sz_view_frame(outlined);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(!px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(outlined);
  sz_signal_int_free(sig);
}

static void test_outlined_button_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_outlined.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_outlined_button("Edit", counter_tap, sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "outlined:Edit") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Edit") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_text_button_sizes(void) {
  SzView *b, *filled;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, ff;

  sig = sz_signal_int(0);
  b = sz_view_text_button("Open", counter_tap, sig);
  filled = sz_view_button("Open", counter_tap, sig);
  sz_view_layout(b, 200.f, 200.f, theme);
  sz_view_layout(filled, 200.f, 200.f, theme);
  assert(sz_view_kind(b) == SZ_VIEW_TEXT_BUTTON);
  f = sz_view_frame(b);
  ff = sz_view_frame(filled);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - ff.w) < 0.5f);
  assert(fabsf(f.h - ff.h) < 0.5f);
  assert(f.w >= 48.f);
  sz_view_free(b);
  sz_view_free(filled);
  sz_signal_int_free(sig);
}

static void test_text_button_empty_min_width(void) {
  SzView *b;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  b = sz_view_text_button("", NULL, NULL);
  sz_view_layout(b, 200.f, 200.f, theme);
  f = sz_view_frame(b);
  assert(fabsf(f.w - 48.f) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(b);
}

static void test_text_button_null_label(void) {
  SzView *b;
  SzString *dump;
  const SzTheme *theme = sz_theme_default();

  b = sz_view_text_button(NULL, NULL, NULL);
  sz_view_layout(b, 200.f, 80.f, theme);
  dump = sz_view_a11y_dump(b);
  assert(strstr(sz_string_cstr(dump), "textbutton:") != NULL);
  sz_string_free(dump);
  sz_view_free(b);
}

static void test_text_button_clamps_max_w(void) {
  SzView *b;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  b = sz_view_text_button("Open", NULL, NULL);
  sz_view_layout(b, 20.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(fabsf(f.w - 20.f) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(b);
}

static void test_text_button_does_not_wrap(void) {
  SzView *b;
  const SzTheme *theme = sz_theme_default();
  float full_h;

  b = sz_view_text_button("one two", NULL, NULL);
  sz_view_layout(b, 1000.f, 100.f, theme);
  full_h = sz_view_frame(b).h;
  sz_view_free(b);

  b = sz_view_text_button("one two", NULL, NULL);
  sz_view_layout(b, 20.f, 100.f, theme);
  assert(fabsf(sz_view_frame(b).h - full_h) < 0.5f);
  sz_view_free(b);
}

static void test_text_button_a11y(void) {
  SzView *b;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  b = sz_view_text_button("Open", counter_tap, sig);
  dump = sz_view_a11y_dump(b);
  assert(strstr(sz_string_cstr(dump), "textbutton:Open") != NULL);
  sz_string_free(dump);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_text_button_a11y_distinct(void) {
  SzView *b;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(0);
  b = sz_view_text_button("Open", counter_tap, sig);
  dump = sz_view_a11y_dump(b);
  s = sz_string_cstr(dump);
  assert(strstr(s, "textbutton:Open") != NULL);
  assert(strncmp(s, "textbutton:", 11) == 0);
  assert(strstr(s, "outlined:") == NULL);
  assert(strstr(s, "iconbutton:") == NULL);
  assert(strstr(s, "fab:") == NULL);
  sz_string_free(dump);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_text_button_tap(void) {
  SzView *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_text_button("Open", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(sz_view_is_tap_target(b));
  assert(sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_text_button_tap_twice(void) {
  SzView *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_text_button("Open", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 2);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_text_button_null_tap(void) {
  SzView *b;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  b = sz_view_text_button("Open", NULL, NULL);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(sz_view_is_tap_target(b));
  assert(!sz_view_handle_tap(b, f.x + 4.f, f.y + f.h * 0.5f));
  sz_view_free(b);
}

static void test_text_button_miss(void) {
  SzView *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_text_button("Open", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  assert(!sz_view_handle_tap(b, f.x - 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_text_button_hit_test(void) {
  SzView *b, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  b = sz_view_text_button("Open", counter_tap, sig);
  sz_view_layout(b, 200.f, 80.f, theme);
  f = sz_view_frame(b);
  hit = sz_view_hit_test(b, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_TEXT_BUTTON);
  sz_view_free(b);
  sz_signal_int_free(sig);
}

static void test_text_button_paint(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  root = sz_view_text_button("Open", counter_tap, sig);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* No fill: canvas stays theme background, not surface or primary. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_text_button_paint_not_filled(void) {
  SzView *filled, *outlined, *textb;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  filled = sz_view_button("Open", counter_tap, sig);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(filled, canvas, 80, 80, theme));
  f = sz_view_frame(filled);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(filled);

  outlined = sz_view_outlined_button("Open", counter_tap, sig);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(outlined, canvas, 80, 80, theme));
  f = sz_view_frame(outlined);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xFF, 0xFF, 0xFF));
  sk_surface_unref(surf);
  sz_view_free(outlined);

  textb = sz_view_text_button("Open", counter_tap, sig);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(textb, canvas, 80, 80, theme));
  f = sz_view_frame(textb);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(!px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0x14, 0x28, 0x50));
  assert(!px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xFF, 0xFF, 0xFF));
  sk_surface_unref(surf);
  sz_view_free(textb);
  sz_signal_int_free(sig);
}

static void test_text_button_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_textbutton.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_text_button("Open", counter_tap, sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "textbutton:Open") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Open") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_placeholder_sizes(void) {
  SzView *ph, *av;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  av = sz_view_avatar("S");
  ph = sz_view_placeholder(av);
  sz_view_layout(ph, 200.f, 200.f, theme);
  assert(sz_view_kind(ph) == SZ_VIEW_PLACEHOLDER);
  f = sz_view_frame(ph);
  cf = sz_view_frame(av);
  assert(fabsf(f.w - cf.w) < 0.5f);
  assert(fabsf(f.h - cf.h) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(ph);
}

static void test_placeholder_empty_sizes(void) {
  SzView *ph;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  ph = sz_view_placeholder(NULL);
  sz_view_layout(ph, 200.f, 200.f, theme);
  f = sz_view_frame(ph);
  assert(fabsf(f.w) < 0.5f);
  assert(fabsf(f.h) < 0.5f);
  sz_view_free(ph);
}

static void test_placeholder_same_origin(void) {
  SzView *ph, *av;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  av = sz_view_avatar("S");
  ph = sz_view_placeholder(av);
  sz_view_layout(ph, 200.f, 200.f, theme);
  f = sz_view_frame(ph);
  cf = sz_view_frame(av);
  assert(fabsf(f.x - cf.x) < 0.5f);
  assert(fabsf(f.y - cf.y) < 0.5f);
  sz_view_free(ph);
}

static void test_placeholder_a11y(void) {
  SzView *ph;
  SzString *dump;
  const char *s;

  ph = sz_view_placeholder(sz_view_avatar("S"));
  dump = sz_view_a11y_dump(ph);
  s = sz_string_cstr(dump);
  assert(strstr(s, "placeholder:ph") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(ph);
}

static void test_placeholder_a11y_nested_tooltip(void) {
  SzView *ph;
  SzString *dump;
  const char *s;

  ph = sz_view_placeholder(sz_view_tooltip("Sean", sz_view_avatar("S")));
  dump = sz_view_a11y_dump(ph);
  s = sz_string_cstr(dump);
  assert(strstr(s, "placeholder:ph") != NULL);
  assert(strstr(s, "tooltip:Sean") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(ph);
}

static void test_placeholder_not_tap_target(void) {
  SzView *ph;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  ph = sz_view_placeholder(sz_view_avatar("S"));
  sz_view_layout(ph, 200.f, 80.f, theme);
  f = sz_view_frame(ph);
  assert(!sz_view_is_tap_target(ph));
  assert(sz_view_hit_test(ph, f.x + 4.f, f.y + f.h * 0.5f) == NULL);
  sz_view_free(ph);
}

static void test_placeholder_child_tap(void) {
  SzView *ph, *btn, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, sig);
  ph = sz_view_placeholder(btn);
  sz_view_layout(ph, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(ph, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_handle_tap(ph, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(ph);
  sz_signal_int_free(sig);
}

static void test_placeholder_paint_child(void) {
  SzView *root, *av;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int mx, my;

  av = sz_view_avatar("S");
  root = sz_view_placeholder(av);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(av);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  mx = (int)(f.x + f.w * 0.5f);
  my = (int)(f.y + 6.f);
  /* Child disc fill is primary. Mark does not cover this sample. */
  assert(px_rgb(px, 80, mx, my, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_placeholder_paint_mark(void) {
  SzView *root, *child;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  child = sz_view_background(0xFF00AA00u, sz_view_sized(40, 40, sz_view_text("x")));
  root = sz_view_placeholder(child);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Interior stays the child fill. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 20.f), 0x00, 0xAA, 0x00));
  /* Top edge is the muted box. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)f.y, 0x6A, 0x6A, 0x6A));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_placeholder_paint_empty(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_placeholder(NULL);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 8, 8, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_placeholder_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  const char *path = "/tmp/scuzz_ui_placeholder.dump";
  char *dump;
  const char *taps;

  root = sz_view_column();
  sz_view_add_child(root, sz_view_placeholder(sz_view_avatar("S")));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "placeholder:ph") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "ph") == NULL);
  assert(strstr(taps, "placeholder") == NULL);
  free(dump);
  sz_ui_unmount(session);
  remove(path);
}

static void test_placeholder_child_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_placeholder_child.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_placeholder(sz_view_button("Go", counter_tap, sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "placeholder:ph") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Go") != NULL);
  assert(strstr(taps, "ph") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_semantics_sizes(void) {
  SzView *sem, *av;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  av = sz_view_avatar("S");
  sem = sz_view_semantics("mark", av);
  sz_view_layout(sem, 200.f, 200.f, theme);
  assert(sz_view_kind(sem) == SZ_VIEW_SEMANTICS);
  f = sz_view_frame(sem);
  cf = sz_view_frame(av);
  assert(fabsf(f.w - cf.w) < 0.5f);
  assert(fabsf(f.h - cf.h) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(sem);
}

static void test_semantics_empty_sizes(void) {
  SzView *sem;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sem = sz_view_semantics("mark", NULL);
  sz_view_layout(sem, 200.f, 200.f, theme);
  f = sz_view_frame(sem);
  assert(fabsf(f.w) < 0.5f);
  assert(fabsf(f.h) < 0.5f);
  sz_view_free(sem);
}

static void test_semantics_same_origin(void) {
  SzView *sem, *av;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  av = sz_view_avatar("S");
  sem = sz_view_semantics("mark", av);
  sz_view_layout(sem, 200.f, 200.f, theme);
  f = sz_view_frame(sem);
  cf = sz_view_frame(av);
  assert(fabsf(f.x - cf.x) < 0.5f);
  assert(fabsf(f.y - cf.y) < 0.5f);
  sz_view_free(sem);
}

static void test_semantics_a11y(void) {
  SzView *sem;
  SzString *dump;
  const char *s;

  sem = sz_view_semantics("mark", sz_view_avatar("S"));
  dump = sz_view_a11y_dump(sem);
  s = sz_string_cstr(dump);
  assert(strncmp(s, "semantics:", 10) == 0);
  assert(strstr(s, "semantics:mark") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(sem);
}

static void test_semantics_a11y_nested(void) {
  SzView *sem;
  SzString *dump;
  const char *s;

  sem = sz_view_semantics(
      "mark", sz_view_placeholder(sz_view_tooltip("Sean", sz_view_avatar("S"))));
  dump = sz_view_a11y_dump(sem);
  s = sz_string_cstr(dump);
  assert(strstr(s, "semantics:mark") != NULL);
  assert(strstr(s, "placeholder:ph") != NULL);
  assert(strstr(s, "tooltip:Sean") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(sem);
}

static void test_semantics_empty_label(void) {
  SzView *sem;
  SzString *dump;

  sem = sz_view_semantics("", sz_view_avatar("S"));
  dump = sz_view_a11y_dump(sem);
  assert(strstr(sz_string_cstr(dump), "semantics:") != NULL);
  assert(strstr(sz_string_cstr(dump), "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(sem);
}

static void test_semantics_null_label(void) {
  SzView *sem;
  SzString *dump;

  sem = sz_view_semantics(NULL, sz_view_avatar("S"));
  dump = sz_view_a11y_dump(sem);
  assert(strstr(sz_string_cstr(dump), "semantics:") != NULL);
  sz_string_free(dump);
  sz_view_free(sem);
}

static void test_semantics_not_tap_target(void) {
  SzView *sem;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sem = sz_view_semantics("mark", sz_view_avatar("S"));
  sz_view_layout(sem, 200.f, 80.f, theme);
  f = sz_view_frame(sem);
  assert(!sz_view_is_tap_target(sem));
  assert(sz_view_hit_test(sem, f.x + 4.f, f.y + f.h * 0.5f) == NULL);
  sz_view_free(sem);
}

static void test_semantics_child_tap(void) {
  SzView *sem, *btn, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, sig);
  sem = sz_view_semantics("mark", btn);
  sz_view_layout(sem, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(sem, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_handle_tap(sem, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(sem);
  sz_signal_int_free(sig);
}

static void test_semantics_paint_child(void) {
  SzView *root, *av;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int mx, my;

  av = sz_view_avatar("S");
  root = sz_view_semantics("mark", av);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(av);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  mx = (int)(f.x + f.w * 0.5f);
  my = (int)(f.y + 6.f);
  assert(px_rgb(px, 80, mx, my, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_semantics_paint_no_mark(void) {
  SzView *root, *child;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  child = sz_view_background(0xFF00AA00u, sz_view_sized(40, 40, sz_view_text("x")));
  root = sz_view_semantics("mark", child);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 20.f), 0x00, 0xAA, 0x00));
  /* Top edge stays the child fill. Semantics does not paint a box mark. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)f.y, 0x00, 0xAA, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_semantics_paint_empty(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_semantics("mark", NULL);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 8, 8, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_semantics_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  const char *path = "/tmp/scuzz_ui_semantics.dump";
  char *dump;
  const char *taps;

  root = sz_view_column();
  sz_view_add_child(root, sz_view_semantics("mark", sz_view_avatar("S")));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "semantics:mark") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "mark") == NULL);
  assert(strstr(taps, "semantics") == NULL);
  free(dump);
  sz_ui_unmount(session);
  remove(path);
}

static void test_semantics_child_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_semantics_child.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_semantics("mark", sz_view_button("Go", counter_tap, sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "semantics:mark") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Go") != NULL);
  assert(strstr(taps, "mark") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_merge_semantics_sizes(void) {
  SzView *mer, *av;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  av = sz_view_avatar("S");
  mer = sz_view_merge_semantics("logo", av);
  sz_view_layout(mer, 200.f, 200.f, theme);
  assert(sz_view_kind(mer) == SZ_VIEW_MERGE_SEMANTICS);
  f = sz_view_frame(mer);
  cf = sz_view_frame(av);
  assert(fabsf(f.w - cf.w) < 0.5f);
  assert(fabsf(f.h - cf.h) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(mer);
}

static void test_merge_semantics_empty_sizes(void) {
  SzView *mer;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  mer = sz_view_merge_semantics("logo", NULL);
  sz_view_layout(mer, 200.f, 200.f, theme);
  f = sz_view_frame(mer);
  assert(fabsf(f.w) < 0.5f);
  assert(fabsf(f.h) < 0.5f);
  sz_view_free(mer);
}

static void test_merge_semantics_same_origin(void) {
  SzView *mer, *av;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  av = sz_view_avatar("S");
  mer = sz_view_merge_semantics("logo", av);
  sz_view_layout(mer, 200.f, 200.f, theme);
  f = sz_view_frame(mer);
  cf = sz_view_frame(av);
  assert(fabsf(f.x - cf.x) < 0.5f);
  assert(fabsf(f.y - cf.y) < 0.5f);
  sz_view_free(mer);
}

static void test_merge_semantics_a11y(void) {
  SzView *mer;
  SzString *dump;
  const char *s;

  mer = sz_view_merge_semantics("logo", sz_view_avatar("S"));
  dump = sz_view_a11y_dump(mer);
  s = sz_string_cstr(dump);
  assert(strncmp(s, "merge:", 6) == 0);
  assert(strstr(s, "merge:logo") != NULL);
  assert(strstr(s, "avatar:S") == NULL);
  sz_string_free(dump);
  sz_view_free(mer);
}

static void test_merge_semantics_a11y_omits_nested(void) {
  SzView *mer;
  SzString *dump;
  const char *s;

  mer = sz_view_merge_semantics(
      "logo", sz_view_placeholder(sz_view_tooltip("Sean", sz_view_avatar("S"))));
  dump = sz_view_a11y_dump(mer);
  s = sz_string_cstr(dump);
  assert(strstr(s, "merge:logo") != NULL);
  assert(strstr(s, "placeholder:ph") == NULL);
  assert(strstr(s, "tooltip:Sean") == NULL);
  assert(strstr(s, "avatar:S") == NULL);
  sz_string_free(dump);
  sz_view_free(mer);
}

static void test_merge_semantics_a11y_distinct(void) {
  SzView *col;
  SzString *dump;
  const char *s;

  col = sz_view_column();
  sz_view_add_child(col, sz_view_semantics("mark", sz_view_avatar("S")));
  sz_view_add_child(col, sz_view_merge_semantics("logo", sz_view_avatar("T")));
  dump = sz_view_a11y_dump(col);
  s = sz_string_cstr(dump);
  assert(strstr(s, "semantics:mark") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  assert(strstr(s, "merge:logo") != NULL);
  assert(strstr(s, "avatar:T") == NULL);
  sz_string_free(dump);
  sz_view_free(col);
}

static void test_merge_semantics_empty_label(void) {
  SzView *mer;
  SzString *dump;

  mer = sz_view_merge_semantics("", sz_view_avatar("S"));
  dump = sz_view_a11y_dump(mer);
  assert(strstr(sz_string_cstr(dump), "merge:") != NULL);
  assert(strstr(sz_string_cstr(dump), "avatar:S") == NULL);
  sz_string_free(dump);
  sz_view_free(mer);
}

static void test_merge_semantics_null_label(void) {
  SzView *mer;
  SzString *dump;

  mer = sz_view_merge_semantics(NULL, sz_view_avatar("S"));
  dump = sz_view_a11y_dump(mer);
  assert(strstr(sz_string_cstr(dump), "merge:") != NULL);
  sz_string_free(dump);
  sz_view_free(mer);
}

static void test_merge_semantics_not_tap_target(void) {
  SzView *mer;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  mer = sz_view_merge_semantics("logo", sz_view_avatar("S"));
  sz_view_layout(mer, 200.f, 80.f, theme);
  f = sz_view_frame(mer);
  assert(!sz_view_is_tap_target(mer));
  assert(sz_view_hit_test(mer, f.x + 4.f, f.y + f.h * 0.5f) == NULL);
  sz_view_free(mer);
}

static void test_merge_semantics_child_tap(void) {
  SzView *mer, *btn, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, sig);
  mer = sz_view_merge_semantics("logo", btn);
  sz_view_layout(mer, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(mer, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_handle_tap(mer, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(mer);
  sz_signal_int_free(sig);
}

static void test_merge_semantics_paint_child(void) {
  SzView *root, *av;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int mx, my;

  av = sz_view_avatar("S");
  root = sz_view_merge_semantics("logo", av);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(av);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  mx = (int)(f.x + f.w * 0.5f);
  my = (int)(f.y + 6.f);
  assert(px_rgb(px, 80, mx, my, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_merge_semantics_paint_no_mark(void) {
  SzView *root, *child;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  child = sz_view_background(0xFF00AA00u, sz_view_sized(40, 40, sz_view_text("x")));
  root = sz_view_merge_semantics("logo", child);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 20.f), 0x00, 0xAA, 0x00));
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)f.y, 0x00, 0xAA, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_merge_semantics_paint_empty(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_merge_semantics("logo", NULL);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 8, 8, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_merge_semantics_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  const char *path = "/tmp/scuzz_ui_merge_semantics.dump";
  char *dump;
  const char *taps;

  root = sz_view_column();
  sz_view_add_child(root, sz_view_merge_semantics("logo", sz_view_avatar("S")));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "merge:logo") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "logo") == NULL);
  assert(strstr(taps, "merge") == NULL);
  free(dump);
  sz_ui_unmount(session);
  remove(path);
}

static void test_merge_semantics_child_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_merge_semantics_child.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_merge_semantics("logo", sz_view_button("Go", counter_tap, sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "merge:logo") != NULL);
  assert(strstr(dump, "button:Go") == NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Go") != NULL);
  assert(strstr(taps, "logo") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_merge_semantics_skips_field_collect(void) {
  SzView *col, *hidden, *shown;
  SzView *fields[8];
  SzSignalStr *a, *b;
  SzString *dump;
  int n;
  const SzTheme *theme = sz_theme_default();

  a = sz_signal_str("secret");
  b = sz_signal_str("ok");
  hidden = sz_view_text_field(a, "hidden");
  shown = sz_view_text_field(b, "shown");
  col = sz_view_column();
  sz_view_add_child(col, sz_view_merge_semantics("logo", hidden));
  sz_view_add_child(col, shown);
  sz_view_layout(col, 200.f, 120.f, theme);
  n = sz_view_collect_text_fields(col, fields, 8);
  assert(n == 1);
  assert(fields[0] == shown);
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "textfield:hidden") == NULL);
  assert(strstr(sz_string_cstr(dump), "merge:logo") != NULL);
  assert(strstr(sz_string_cstr(dump), "textfield:shown") != NULL);
  sz_string_free(dump);
  sz_view_free(col);
  sz_signal_str_free(a);
  sz_signal_str_free(b);
}

static void test_ink_well_sizes(void) {
  SzView *ink, *av;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  sig = sz_signal_int(0);
  av = sz_view_avatar("S");
  ink = sz_view_ink_well("face", counter_tap, sig, av);
  sz_view_layout(ink, 200.f, 200.f, theme);
  assert(sz_view_kind(ink) == SZ_VIEW_INK_WELL);
  f = sz_view_frame(ink);
  cf = sz_view_frame(av);
  assert(fabsf(f.w - cf.w) < 0.5f);
  assert(fabsf(f.h - cf.h) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(ink);
  sz_signal_int_free(sig);
}

static void test_ink_well_empty_sizes(void) {
  SzView *ink;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  ink = sz_view_ink_well("face", NULL, NULL, NULL);
  sz_view_layout(ink, 200.f, 200.f, theme);
  f = sz_view_frame(ink);
  assert(fabsf(f.w) < 0.5f);
  assert(fabsf(f.h) < 0.5f);
  sz_view_free(ink);
}

static void test_ink_well_same_origin(void) {
  SzView *ink, *av;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  av = sz_view_avatar("S");
  ink = sz_view_ink_well("face", NULL, NULL, av);
  sz_view_layout(ink, 200.f, 200.f, theme);
  f = sz_view_frame(ink);
  cf = sz_view_frame(av);
  assert(fabsf(f.x - cf.x) < 0.5f);
  assert(fabsf(f.y - cf.y) < 0.5f);
  sz_view_free(ink);
}

static void test_ink_well_a11y(void) {
  SzView *ink;
  SzString *dump;
  const char *s;

  ink = sz_view_ink_well("face", NULL, NULL, sz_view_avatar("S"));
  dump = sz_view_a11y_dump(ink);
  s = sz_string_cstr(dump);
  assert(strncmp(s, "inkwell:", 8) == 0);
  assert(strstr(s, "inkwell:face") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(ink);
}

static void test_ink_well_a11y_nested(void) {
  SzView *ink;
  SzString *dump;
  const char *s;

  ink = sz_view_ink_well(
      "face", NULL, NULL,
      sz_view_placeholder(sz_view_tooltip("Sean", sz_view_avatar("S"))));
  dump = sz_view_a11y_dump(ink);
  s = sz_string_cstr(dump);
  assert(strstr(s, "inkwell:face") != NULL);
  assert(strstr(s, "placeholder:ph") != NULL);
  assert(strstr(s, "tooltip:Sean") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(ink);
}

static void test_ink_well_a11y_distinct(void) {
  SzView *col;
  SzString *dump;
  const char *s;

  col = sz_view_column();
  sz_view_add_child(col, sz_view_button("Go", NULL, NULL));
  sz_view_add_child(col, sz_view_ink_well("face", NULL, NULL, sz_view_avatar("S")));
  dump = sz_view_a11y_dump(col);
  s = sz_string_cstr(dump);
  assert(strstr(s, "button:Go") != NULL);
  assert(strstr(s, "inkwell:face") != NULL);
  assert(strncmp(s, "inkwell:", 8) != 0);
  sz_string_free(dump);
  sz_view_free(col);
}

static void test_ink_well_empty_label(void) {
  SzView *ink;
  SzString *dump;

  ink = sz_view_ink_well("", NULL, NULL, sz_view_avatar("S"));
  dump = sz_view_a11y_dump(ink);
  assert(strstr(sz_string_cstr(dump), "inkwell:") != NULL);
  assert(strstr(sz_string_cstr(dump), "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(ink);
}

static void test_ink_well_null_label(void) {
  SzView *ink;
  SzString *dump;

  ink = sz_view_ink_well(NULL, NULL, NULL, sz_view_avatar("S"));
  dump = sz_view_a11y_dump(ink);
  assert(strstr(sz_string_cstr(dump), "inkwell:") != NULL);
  sz_string_free(dump);
  sz_view_free(ink);
}

static void test_ink_well_is_tap_target(void) {
  SzView *ink;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  ink = sz_view_ink_well("face", NULL, NULL, sz_view_avatar("S"));
  sz_view_layout(ink, 200.f, 80.f, theme);
  f = sz_view_frame(ink);
  assert(sz_view_is_tap_target(ink));
  assert(sz_view_hit_test(ink, f.x + 4.f, f.y + f.h * 0.5f) == ink);
  sz_view_free(ink);
}

static void test_ink_well_tap(void) {
  SzView *ink;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ink = sz_view_ink_well("face", counter_tap, sig, sz_view_avatar("S"));
  sz_view_layout(ink, 200.f, 80.f, theme);
  f = sz_view_frame(ink);
  assert(sz_view_handle_tap(ink, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(ink);
  sz_signal_int_free(sig);
}

static void test_ink_well_tap_twice(void) {
  SzView *ink;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ink = sz_view_ink_well("face", counter_tap, sig, sz_view_avatar("S"));
  sz_view_layout(ink, 200.f, 80.f, theme);
  f = sz_view_frame(ink);
  assert(sz_view_handle_tap(ink, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(ink, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 2);
  sz_view_free(ink);
  sz_signal_int_free(sig);
}

static void test_ink_well_null_tap(void) {
  SzView *ink;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  ink = sz_view_ink_well("face", NULL, NULL, sz_view_avatar("S"));
  sz_view_layout(ink, 200.f, 80.f, theme);
  f = sz_view_frame(ink);
  assert(sz_view_is_tap_target(ink));
  assert(!sz_view_handle_tap(ink, f.x + 4.f, f.y + f.h * 0.5f));
  sz_view_free(ink);
}

static void test_ink_well_miss(void) {
  SzView *ink;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ink = sz_view_ink_well("face", counter_tap, sig, sz_view_avatar("S"));
  sz_view_layout(ink, 200.f, 80.f, theme);
  f = sz_view_frame(ink);
  assert(!sz_view_handle_tap(ink, f.x - 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(ink);
  sz_signal_int_free(sig);
}

static void test_ink_well_child_button_wins(void) {
  SzView *ink, *btn, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, sig);
  ink = sz_view_ink_well("face", NULL, NULL, btn);
  sz_view_layout(ink, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(ink, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_handle_tap(ink, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(ink);
  sz_signal_int_free(sig);
}

static void test_ink_well_paint_child(void) {
  SzView *root, *av;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int mx, my;

  av = sz_view_avatar("S");
  root = sz_view_ink_well("face", NULL, NULL, av);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(av);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  mx = (int)(f.x + f.w * 0.5f);
  my = (int)(f.y + 6.f);
  assert(px_rgb(px, 80, mx, my, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_ink_well_paint_no_mark(void) {
  SzView *root, *child;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  child = sz_view_background(0xFF00AA00u, sz_view_sized(40, 40, sz_view_text("x")));
  root = sz_view_ink_well("face", NULL, NULL, child);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 20.f), 0x00, 0xAA, 0x00));
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)f.y, 0x00, 0xAA, 0x00));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_ink_well_paint_empty(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_ink_well("face", NULL, NULL, NULL);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 8, 8, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_ink_well_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_ink_well.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_ink_well("face", counter_tap, sig, sz_view_avatar("S")));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "inkwell:face") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "face") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_ink_well_child_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_ink_well_child.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_ink_well("face", NULL, NULL, sz_view_button("Go", counter_tap, sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "inkwell:face") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Go") != NULL);
  assert(strstr(taps, "face") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_visibility_sizes_on(void) {
  SzView *vis, *av;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  sig = sz_signal_int(1);
  av = sz_view_avatar("S");
  vis = sz_view_visibility(sig, av);
  sz_view_layout(vis, 200.f, 200.f, theme);
  assert(sz_view_kind(vis) == SZ_VIEW_VISIBILITY);
  f = sz_view_frame(vis);
  cf = sz_view_frame(av);
  assert(fabsf(f.w - cf.w) < 0.5f);
  assert(fabsf(f.h - cf.h) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(vis);
  sz_signal_int_free(sig);
}

static void test_visibility_sizes_off(void) {
  SzView *vis, *av, *hidden;
  SzSignalInt *off, *page;
  const SzTheme *theme = sz_theme_default();
  SzRect f, hf;

  off = sz_signal_int(0);
  av = sz_view_avatar("S");
  vis = sz_view_visibility(off, av);
  sz_view_layout(vis, 200.f, 200.f, theme);
  f = sz_view_frame(vis);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  page = sz_signal_int(0);
  hidden = sz_view_show_when(page, 1, sz_view_avatar("S"));
  sz_view_layout(hidden, 200.f, 200.f, theme);
  hf = sz_view_frame(hidden);
  assert(fabsf(hf.w) < 0.5f || !sz_view_is_tap_target(hidden));
  assert(f.h > hf.h + 1.f);
  sz_view_free(vis);
  sz_view_free(hidden);
  sz_signal_int_free(off);
  sz_signal_int_free(page);
}

static void test_visibility_empty_sizes(void) {
  SzView *vis;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(1);
  vis = sz_view_visibility(sig, NULL);
  sz_view_layout(vis, 200.f, 200.f, theme);
  f = sz_view_frame(vis);
  assert(fabsf(f.w) < 0.5f);
  assert(fabsf(f.h) < 0.5f);
  sz_view_free(vis);
  sz_signal_int_free(sig);
}

static void test_visibility_same_origin(void) {
  SzView *vis, *av;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  sig = sz_signal_int(0);
  av = sz_view_avatar("S");
  vis = sz_view_visibility(sig, av);
  sz_view_layout(vis, 200.f, 200.f, theme);
  f = sz_view_frame(vis);
  cf = sz_view_frame(av);
  assert(fabsf(f.x - cf.x) < 0.5f);
  assert(fabsf(f.y - cf.y) < 0.5f);
  sz_view_free(vis);
  sz_signal_int_free(sig);
}

static void test_visibility_a11y_on(void) {
  SzView *vis;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(1);
  vis = sz_view_visibility(sig, sz_view_avatar("S"));
  dump = sz_view_a11y_dump(vis);
  s = sz_string_cstr(dump);
  assert(strncmp(s, "visibility:", 11) == 0);
  assert(strstr(s, "visibility:1") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(vis);
  sz_signal_int_free(sig);
}

static void test_visibility_a11y_off(void) {
  SzView *vis;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(0);
  vis = sz_view_visibility(sig, sz_view_avatar("S"));
  dump = sz_view_a11y_dump(vis);
  s = sz_string_cstr(dump);
  assert(strstr(s, "visibility:0") != NULL);
  assert(strstr(s, "avatar:S") == NULL);
  sz_string_free(dump);
  sz_view_free(vis);
  sz_signal_int_free(sig);
}

static void test_visibility_nonzero_is_on(void) {
  SzView *vis;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(40);
  vis = sz_view_visibility(sig, sz_view_avatar("S"));
  dump = sz_view_a11y_dump(vis);
  assert(strstr(sz_string_cstr(dump), "visibility:1") != NULL);
  assert(strstr(sz_string_cstr(dump), "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(vis);
  sz_signal_int_free(sig);
}

static void test_visibility_not_tap_target(void) {
  SzView *vis;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(1);
  vis = sz_view_visibility(sig, sz_view_avatar("S"));
  sz_view_layout(vis, 200.f, 80.f, theme);
  f = sz_view_frame(vis);
  assert(!sz_view_is_tap_target(vis));
  assert(sz_view_hit_test(vis, f.x + 4.f, f.y + f.h * 0.5f) == NULL);
  sz_view_free(vis);
  sz_signal_int_free(sig);
}

static void test_visibility_child_tap_on(void) {
  SzView *vis, *btn, *hit;
  SzSignalInt *vis_sig, *tap_sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  vis_sig = sz_signal_int(1);
  tap_sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, tap_sig);
  vis = sz_view_visibility(vis_sig, btn);
  sz_view_layout(vis, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(vis, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_handle_tap(vis, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(tap_sig) == 1);
  sz_view_free(vis);
  sz_signal_int_free(vis_sig);
  sz_signal_int_free(tap_sig);
}

static void test_visibility_child_tap_off(void) {
  SzView *vis, *btn;
  SzSignalInt *vis_sig, *tap_sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  vis_sig = sz_signal_int(0);
  tap_sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, tap_sig);
  vis = sz_view_visibility(vis_sig, btn);
  sz_view_layout(vis, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  assert(sz_view_hit_test(vis, f.x + 4.f, f.y + f.h * 0.5f) == NULL);
  assert(!sz_view_handle_tap(vis, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(tap_sig) == 0);
  sz_view_free(vis);
  sz_signal_int_free(vis_sig);
  sz_signal_int_free(tap_sig);
}

static void test_visibility_paint_on(void) {
  SzView *root, *av;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int mx, my;

  sig = sz_signal_int(1);
  av = sz_view_avatar("S");
  root = sz_view_visibility(sig, av);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(av);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  mx = (int)(f.x + f.w * 0.5f);
  my = (int)(f.y + 6.f);
  assert(px_rgb(px, 80, mx, my, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_visibility_paint_off(void) {
  SzView *root, *child;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  child = sz_view_background(0xFF00AA00u, sz_view_sized(40, 40, sz_view_text("x")));
  root = sz_view_visibility(sig, child);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 20.f), 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_visibility_paint_empty(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  sig = sz_signal_int(1);
  root = sz_view_visibility(sig, NULL);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 8, 8, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_visibility_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_visibility.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(1);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_visibility(sig, sz_view_avatar("S")));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "visibility:1") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "visibility") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_visibility_child_in_taps_on(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *vis_sig, *tap_sig;
  const char *path = "/tmp/scuzz_ui_visibility_child.dump";
  char *dump;
  const char *taps;

  vis_sig = sz_signal_int(1);
  tap_sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_visibility(vis_sig, sz_view_button("Go", counter_tap, tap_sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "visibility:1") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Go") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(vis_sig);
  sz_signal_int_free(tap_sig);
  remove(path);
}

static void test_visibility_child_not_in_taps_off(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *vis_sig, *tap_sig;
  const char *path = "/tmp/scuzz_ui_visibility_off.dump";
  char *dump;
  const char *taps;

  vis_sig = sz_signal_int(0);
  tap_sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_visibility(vis_sig, sz_view_button("Go", counter_tap, tap_sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "visibility:0") != NULL);
  assert(strstr(dump, "button:Go") == NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Go") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(vis_sig);
  sz_signal_int_free(tap_sig);
  remove(path);
}

static void test_visibility_skips_field_collect_off(void) {
  SzView *col, *hidden, *shown;
  SzView *fields[8];
  SzSignalStr *a, *b;
  SzSignalInt *sig;
  SzString *dump;
  int n;
  const SzTheme *theme = sz_theme_default();

  sig = sz_signal_int(0);
  a = sz_signal_str("secret");
  b = sz_signal_str("ok");
  hidden = sz_view_text_field(a, "hidden");
  shown = sz_view_text_field(b, "shown");
  col = sz_view_column();
  sz_view_add_child(col, sz_view_visibility(sig, hidden));
  sz_view_add_child(col, shown);
  sz_view_layout(col, 200.f, 120.f, theme);
  n = sz_view_collect_text_fields(col, fields, 8);
  assert(n == 1);
  assert(fields[0] == shown);
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "textfield:hidden") == NULL);
  assert(strstr(sz_string_cstr(dump), "visibility:0") != NULL);
  assert(strstr(sz_string_cstr(dump), "textfield:shown") != NULL);
  sz_string_free(dump);
  sz_view_free(col);
  sz_signal_str_free(a);
  sz_signal_str_free(b);
  sz_signal_int_free(sig);
}

static void test_offstage_sizes_on(void) {
  SzView *off, *av;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  sig = sz_signal_int(1);
  av = sz_view_avatar("S");
  off = sz_view_offstage(sig, av);
  sz_view_layout(off, 200.f, 200.f, theme);
  assert(sz_view_kind(off) == SZ_VIEW_OFFSTAGE);
  f = sz_view_frame(off);
  cf = sz_view_frame(av);
  assert(fabsf(f.w - cf.w) < 0.5f);
  assert(fabsf(f.h - cf.h) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(off);
  sz_signal_int_free(sig);
}

static void test_offstage_sizes_off(void) {
  SzView *off, *av, *vis;
  SzSignalInt *sig, *vsig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf, vf;

  sig = sz_signal_int(0);
  av = sz_view_avatar("S");
  off = sz_view_offstage(sig, av);
  sz_view_layout(off, 200.f, 200.f, theme);
  f = sz_view_frame(off);
  cf = sz_view_frame(av);
  assert(fabsf(f.w) < 0.5f);
  assert(fabsf(f.h) < 0.5f);
  assert(fabsf(cf.h - theme->control_h) < 0.5f);
  vsig = sz_signal_int(0);
  vis = sz_view_visibility(vsig, sz_view_avatar("S"));
  sz_view_layout(vis, 200.f, 200.f, theme);
  vf = sz_view_frame(vis);
  assert(fabsf(vf.h - theme->control_h) < 0.5f);
  assert(vf.h > f.h + 1.f);
  sz_view_free(off);
  sz_view_free(vis);
  sz_signal_int_free(sig);
  sz_signal_int_free(vsig);
}

static void test_offstage_empty_sizes(void) {
  SzView *off;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(1);
  off = sz_view_offstage(sig, NULL);
  sz_view_layout(off, 200.f, 200.f, theme);
  f = sz_view_frame(off);
  assert(fabsf(f.w) < 0.5f);
  assert(fabsf(f.h) < 0.5f);
  sz_view_free(off);
  sz_signal_int_free(sig);
}

static void test_offstage_same_origin(void) {
  SzView *off, *av;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  sig = sz_signal_int(1);
  av = sz_view_avatar("S");
  off = sz_view_offstage(sig, av);
  sz_view_layout(off, 200.f, 200.f, theme);
  f = sz_view_frame(off);
  cf = sz_view_frame(av);
  assert(fabsf(f.x - cf.x) < 0.5f);
  assert(fabsf(f.y - cf.y) < 0.5f);
  sz_view_free(off);
  sz_signal_int_free(sig);
}

static void test_offstage_a11y_on(void) {
  SzView *off;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(1);
  off = sz_view_offstage(sig, sz_view_avatar("S"));
  dump = sz_view_a11y_dump(off);
  s = sz_string_cstr(dump);
  assert(strncmp(s, "offstage:", 9) == 0);
  assert(strstr(s, "offstage:1") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(off);
  sz_signal_int_free(sig);
}

static void test_offstage_a11y_off(void) {
  SzView *off;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(0);
  off = sz_view_offstage(sig, sz_view_avatar("S"));
  dump = sz_view_a11y_dump(off);
  s = sz_string_cstr(dump);
  assert(strstr(s, "offstage:0") != NULL);
  assert(strstr(s, "avatar:S") == NULL);
  sz_string_free(dump);
  sz_view_free(off);
  sz_signal_int_free(sig);
}

static void test_offstage_nonzero_is_on(void) {
  SzView *off;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(40);
  off = sz_view_offstage(sig, sz_view_avatar("S"));
  dump = sz_view_a11y_dump(off);
  assert(strstr(sz_string_cstr(dump), "offstage:1") != NULL);
  assert(strstr(sz_string_cstr(dump), "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(off);
  sz_signal_int_free(sig);
}

static void test_offstage_not_tap_target(void) {
  SzView *off;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(1);
  off = sz_view_offstage(sig, sz_view_avatar("S"));
  sz_view_layout(off, 200.f, 80.f, theme);
  f = sz_view_frame(off);
  assert(!sz_view_is_tap_target(off));
  assert(sz_view_hit_test(off, f.x + 4.f, f.y + f.h * 0.5f) == NULL);
  sz_view_free(off);
  sz_signal_int_free(sig);
}

static void test_offstage_child_tap_on(void) {
  SzView *off, *btn, *hit;
  SzSignalInt *off_sig, *tap_sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  off_sig = sz_signal_int(1);
  tap_sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, tap_sig);
  off = sz_view_offstage(off_sig, btn);
  sz_view_layout(off, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(off, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_handle_tap(off, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(tap_sig) == 1);
  sz_view_free(off);
  sz_signal_int_free(off_sig);
  sz_signal_int_free(tap_sig);
}

static void test_offstage_child_tap_off(void) {
  SzView *off, *btn;
  SzSignalInt *off_sig, *tap_sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  off_sig = sz_signal_int(0);
  tap_sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, tap_sig);
  off = sz_view_offstage(off_sig, btn);
  sz_view_layout(off, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  assert(sz_view_hit_test(off, f.x + 4.f, f.y + f.h * 0.5f) == NULL);
  assert(!sz_view_handle_tap(off, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(tap_sig) == 0);
  sz_view_free(off);
  sz_signal_int_free(off_sig);
  sz_signal_int_free(tap_sig);
}

static void test_offstage_paint_on(void) {
  SzView *root, *av;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int mx, my;

  sig = sz_signal_int(1);
  av = sz_view_avatar("S");
  root = sz_view_offstage(sig, av);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(av);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  mx = (int)(f.x + f.w * 0.5f);
  my = (int)(f.y + 6.f);
  assert(px_rgb(px, 80, mx, my, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_offstage_paint_off(void) {
  SzView *root, *child;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  child = sz_view_background(0xFF00AA00u, sz_view_sized(40, 40, sz_view_text("x")));
  root = sz_view_offstage(sig, child);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(child);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 20.f), 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_offstage_paint_empty(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  sig = sz_signal_int(1);
  root = sz_view_offstage(sig, NULL);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 8, 8, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_offstage_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_offstage.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(1);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_offstage(sig, sz_view_avatar("S")));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "offstage:1") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "offstage") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_offstage_child_in_taps_on(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *off_sig, *tap_sig;
  const char *path = "/tmp/scuzz_ui_offstage_child.dump";
  char *dump;
  const char *taps;

  off_sig = sz_signal_int(1);
  tap_sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_offstage(off_sig, sz_view_button("Go", counter_tap, tap_sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "offstage:1") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Go") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(off_sig);
  sz_signal_int_free(tap_sig);
  remove(path);
}

static void test_offstage_child_not_in_taps_off(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *off_sig, *tap_sig;
  const char *path = "/tmp/scuzz_ui_offstage_off.dump";
  char *dump;
  const char *taps;

  off_sig = sz_signal_int(0);
  tap_sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_offstage(off_sig, sz_view_button("Go", counter_tap, tap_sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "offstage:0") != NULL);
  assert(strstr(dump, "button:Go") == NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Go") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(off_sig);
  sz_signal_int_free(tap_sig);
  remove(path);
}

static void test_offstage_skips_field_collect_off(void) {
  SzView *col, *hidden, *shown;
  SzView *fields[8];
  SzSignalStr *a, *b;
  SzSignalInt *sig;
  SzString *dump;
  int n;
  const SzTheme *theme = sz_theme_default();

  sig = sz_signal_int(0);
  a = sz_signal_str("secret");
  b = sz_signal_str("ok");
  hidden = sz_view_text_field(a, "hidden");
  shown = sz_view_text_field(b, "shown");
  col = sz_view_column();
  sz_view_add_child(col, sz_view_offstage(sig, hidden));
  sz_view_add_child(col, shown);
  sz_view_layout(col, 200.f, 120.f, theme);
  n = sz_view_collect_text_fields(col, fields, 8);
  assert(n == 1);
  assert(fields[0] == shown);
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "textfield:hidden") == NULL);
  assert(strstr(sz_string_cstr(dump), "offstage:0") != NULL);
  assert(strstr(sz_string_cstr(dump), "textfield:shown") != NULL);
  sz_string_free(dump);
  sz_view_free(col);
  sz_signal_str_free(a);
  sz_signal_str_free(b);
  sz_signal_int_free(sig);
}

static void test_unconstrained_box_sizes(void) {
  SzView *box, *av;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  av = sz_view_avatar("S");
  box = sz_view_unconstrained_box(av);
  sz_view_layout(box, 200.f, 200.f, theme);
  assert(sz_view_kind(box) == SZ_VIEW_UNCONSTRAINED_BOX);
  f = sz_view_frame(box);
  cf = sz_view_frame(av);
  assert(fabsf(f.w - cf.w) < 0.5f);
  assert(fabsf(f.h - cf.h) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(box);
}

static void test_unconstrained_box_empty_sizes(void) {
  SzView *box;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  box = sz_view_unconstrained_box(NULL);
  sz_view_layout(box, 200.f, 200.f, theme);
  f = sz_view_frame(box);
  assert(fabsf(f.w) < 0.5f);
  assert(fabsf(f.h) < 0.5f);
  sz_view_free(box);
}

static void test_unconstrained_box_same_origin(void) {
  SzView *box, *av;
  const SzTheme *theme = sz_theme_default();
  SzRect f, cf;

  av = sz_view_avatar("S");
  box = sz_view_unconstrained_box(av);
  sz_view_layout(box, 200.f, 200.f, theme);
  f = sz_view_frame(box);
  cf = sz_view_frame(av);
  assert(fabsf(f.x - cf.x) < 0.5f);
  assert(fabsf(f.y - cf.y) < 0.5f);
  sz_view_free(box);
}

static void test_unconstrained_box_text_unbounded(void) {
  SzView *plain, *txt, *box;
  const SzTheme *theme = sz_theme_default();
  SzRect pf, cf, bf;
  const char *s = "one two three four five six seven";

  plain = sz_view_text(s);
  sz_view_layout(plain, 40.f, 200.f, theme);
  pf = sz_view_frame(plain);
  txt = sz_view_text(s);
  box = sz_view_unconstrained_box(txt);
  sz_view_layout(box, 40.f, 200.f, theme);
  cf = sz_view_frame(txt);
  bf = sz_view_frame(box);
  assert(cf.w > pf.w + 1.f);
  assert(cf.h + 1.f < pf.h);
  assert(bf.w <= 40.5f);
  sz_view_free(plain);
  sz_view_free(box);
}

static void test_unconstrained_box_a11y(void) {
  SzView *box;
  SzString *dump;
  const char *s;

  box = sz_view_unconstrained_box(sz_view_avatar("S"));
  dump = sz_view_a11y_dump(box);
  s = sz_string_cstr(dump);
  assert(strstr(s, "unconstrained:box") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(box);
}

static void test_unconstrained_box_a11y_nested(void) {
  SzView *box;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(1);
  box = sz_view_unconstrained_box(sz_view_offstage(sig, sz_view_avatar("S")));
  dump = sz_view_a11y_dump(box);
  s = sz_string_cstr(dump);
  assert(strstr(s, "unconstrained:box") != NULL);
  assert(strstr(s, "offstage:1") != NULL);
  assert(strstr(s, "avatar:S") != NULL);
  sz_string_free(dump);
  sz_view_free(box);
  sz_signal_int_free(sig);
}

static void test_unconstrained_box_not_tap_target(void) {
  SzView *box;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  box = sz_view_unconstrained_box(sz_view_avatar("S"));
  sz_view_layout(box, 200.f, 80.f, theme);
  f = sz_view_frame(box);
  assert(!sz_view_is_tap_target(box));
  assert(sz_view_hit_test(box, f.x + 4.f, f.y + f.h * 0.5f) == NULL);
  sz_view_free(box);
}

static void test_unconstrained_box_child_tap(void) {
  SzView *box, *btn, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, sig);
  box = sz_view_unconstrained_box(btn);
  sz_view_layout(box, 200.f, 80.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(box, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_handle_tap(box, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(box);
  sz_signal_int_free(sig);
}

static void test_unconstrained_box_paint_child(void) {
  SzView *root, *av;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int mx, my;

  av = sz_view_avatar("S");
  root = sz_view_unconstrained_box(av);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(av);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  mx = (int)(f.x + f.w * 0.5f);
  my = (int)(f.y + 6.f);
  assert(px_rgb(px, 80, mx, my, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_unconstrained_box_paint_empty(void) {
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  root = sz_view_unconstrained_box(NULL);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 8, 8, 0xF5, 0xF5, 0xF5));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_unconstrained_box_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  const char *path = "/tmp/scuzz_ui_unconstrained.dump";
  char *dump;
  const char *taps;

  root = sz_view_column();
  sz_view_add_child(root, sz_view_unconstrained_box(sz_view_avatar("S")));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "unconstrained:box") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "box") == NULL);
  assert(strstr(taps, "unconstrained") == NULL);
  free(dump);
  sz_ui_unmount(session);
  remove(path);
}

static void test_unconstrained_box_child_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_unconstrained_child.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root,
                    sz_view_unconstrained_box(sz_view_button("Go", counter_tap, sig)));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "unconstrained:box") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Go") != NULL);
  assert(strstr(taps, "unconstrained") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_unconstrained_box_collects_fields(void) {
  SzView *box;
  SzView *fields[8];
  SzSignalStr *a;
  SzString *dump;
  int n;
  const SzTheme *theme = sz_theme_default();

  a = sz_signal_str("ok");
  box = sz_view_unconstrained_box(sz_view_text_field(a, "item"));
  sz_view_layout(box, 200.f, 80.f, theme);
  n = sz_view_collect_text_fields(box, fields, 8);
  assert(n == 1);
  dump = sz_view_a11y_dump(box);
  assert(strstr(sz_string_cstr(dump), "textfield:item") != NULL);
  assert(strstr(sz_string_cstr(dump), "unconstrained:box") != NULL);
  sz_string_free(dump);
  sz_view_free(box);
  sz_signal_str_free(a);
}

static void test_filter_chip_sizes(void) {
  SzView *ch, *plain;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, pf;

  sig = sz_signal_int(0);
  ch = sz_view_filter_chip(sig, "Tag");
  plain = sz_view_chip(sig, "Tag");
  sz_view_layout(ch, 200.f, 200.f, theme);
  sz_view_layout(plain, 200.f, 200.f, theme);
  assert(sz_view_kind(ch) == SZ_VIEW_FILTER_CHIP);
  f = sz_view_frame(ch);
  pf = sz_view_frame(plain);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(f.w >= 32.f);
  assert(f.w > pf.w);
  sz_view_free(ch);
  sz_view_free(plain);
  sz_signal_int_free(sig);
}

static void test_filter_chip_empty_min_width(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_filter_chip(sig, "");
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  assert(f.w >= 32.f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_filter_chip_null_label(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  ch = sz_view_filter_chip(sig, NULL);
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "filterchip:") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_filter_chip_clamps_max_w(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_filter_chip(sig, "Tag");
  sz_view_layout(ch, 20.f, 80.f, theme);
  f = sz_view_frame(ch);
  assert(fabsf(f.w - 20.f) < 0.5f);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_filter_chip_a11y_off_on(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  ch = sz_view_filter_chip(sig, "Tag");
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "filterchip:Tag=0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "filterchip:Tag=1") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_filter_chip_a11y_distinct(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(0);
  ch = sz_view_filter_chip(sig, "Tag");
  dump = sz_view_a11y_dump(ch);
  s = sz_string_cstr(dump);
  assert(strncmp(s, "filterchip:", 11) == 0);
  assert(strstr(s, "filterchip:Tag=0") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_filter_chip_nonzero_is_on(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(7);
  ch = sz_view_filter_chip(sig, "X");
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "filterchip:X=1") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_filter_chip_tap_toggles(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_filter_chip(sig, "Tag");
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  assert(sz_view_is_tap_target(ch));
  assert(sz_view_handle_tap(ch, f.x + f.w * 0.5f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(ch, f.x + 2.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_filter_chip_miss(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_filter_chip(sig, "Tag");
  sz_view_layout(ch, 200.f, 80.f, theme);
  f = sz_view_frame(ch);
  assert(!sz_view_handle_tap(ch, f.x - 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_filter_chip_hit_test(void) {
  SzView *ch, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_filter_chip(sig, "Tag");
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  hit = sz_view_hit_test(ch, f.x + f.w * 0.5f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_FILTER_CHIP);
  assert(sz_view_is_tap_target(hit));
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_filter_chip_paint_off_on(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  root = sz_view_filter_chip(sig, "Tag");
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Top of chip fill, above the label. Off is surface; on is primary. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFF, 0xFF));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_filter_chip_paint_mark_on(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  float box;
  int mx, my;

  sig = sz_signal_int(1);
  root = sz_view_filter_chip(sig, "Tag");
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  box = theme->font_px + 4.f;
  if (box < 12.f)
    box = 12.f;
  mx = (int)(f.x + theme->pad + box * 0.5f);
  my = (int)(f.y + f.h * 0.5f);
  /* Leading check fill is on_primary. */
  assert(px_rgb(px, 80, mx, my, 0xF0, 0xF0, 0xF0));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_filter_chip_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_filterchip.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_filter_chip(sig, "Tag"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "filterchip:Tag=0") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Tag") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_choice_chip_sizes(void) {
  SzView *ch, *plain;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, pf;

  sig = sz_signal_int(0);
  ch = sz_view_choice_chip(sig, 0, "Day");
  plain = sz_view_chip(sig, "Day");
  sz_view_layout(ch, 200.f, 200.f, theme);
  sz_view_layout(plain, 200.f, 200.f, theme);
  assert(sz_view_kind(ch) == SZ_VIEW_CHOICE_CHIP);
  f = sz_view_frame(ch);
  pf = sz_view_frame(plain);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(f.w >= 32.f);
  assert(fabsf(f.w - pf.w) < 0.5f);
  sz_view_free(ch);
  sz_view_free(plain);
  sz_signal_int_free(sig);
}

static void test_choice_chip_empty_min_width(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_choice_chip(sig, 0, "");
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  assert(f.w >= 32.f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_choice_chip_null_label(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  ch = sz_view_choice_chip(sig, 0, NULL);
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "choicechip:") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_choice_chip_clamps_max_w(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_choice_chip(sig, 0, "Day");
  sz_view_layout(ch, 20.f, 80.f, theme);
  f = sz_view_frame(ch);
  assert(fabsf(f.w - 20.f) < 0.5f);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_choice_chip_a11y_off_on(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(1);
  ch = sz_view_choice_chip(sig, 0, "Day");
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "choicechip:Day=0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 0);
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "choicechip:Day=1") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_choice_chip_a11y_distinct(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(1);
  ch = sz_view_choice_chip(sig, 0, "Day");
  dump = sz_view_a11y_dump(ch);
  s = sz_string_cstr(dump);
  assert(strncmp(s, "choicechip:", 11) == 0);
  assert(strstr(s, "choicechip:Day=0") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_choice_chip_wrong_value_is_off(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(7);
  ch = sz_view_choice_chip(sig, 0, "Day");
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "choicechip:Day=0") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_choice_chip_tap_writes_value(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(1);
  ch = sz_view_choice_chip(sig, 0, "Day");
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  assert(sz_view_is_tap_target(ch));
  assert(sz_view_handle_tap(ch, f.x + f.w * 0.5f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  assert(sz_view_handle_tap(ch, f.x + 2.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_choice_chip_miss(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(1);
  ch = sz_view_choice_chip(sig, 0, "Day");
  sz_view_layout(ch, 200.f, 80.f, theme);
  f = sz_view_frame(ch);
  assert(!sz_view_handle_tap(ch, f.x - 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_choice_chip_hit_test(void) {
  SzView *ch, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_choice_chip(sig, 0, "Day");
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  hit = sz_view_hit_test(ch, f.x + f.w * 0.5f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_CHOICE_CHIP);
  assert(sz_view_is_tap_target(hit));
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_choice_chip_paint_off_on(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(1);
  root = sz_view_choice_chip(sig, 0, "Day");
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Top of chip fill, above the label. Off is surface; on is primary. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFF, 0xFF));
  sz_signal_int_set(sig, 0);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_choice_chip_group_exclusive(void) {
  SzView *col, *chip, *radio;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzString *dump;
  SzRect f;

  sig = sz_signal_int(0);
  col = sz_view_column();
  chip = sz_view_choice_chip(sig, 0, "Day");
  radio = sz_view_radio(sig, 1, "Night");
  sz_view_add_child(col, chip);
  sz_view_add_child(col, radio);
  sz_view_layout(col, 200.f, 200.f, theme);
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "choicechip:Day=1") != NULL);
  assert(strstr(sz_string_cstr(dump), "radio:Night=0") != NULL);
  sz_string_free(dump);
  f = sz_view_frame(radio);
  assert(sz_view_handle_tap(col, f.x + 2.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "choicechip:Day=0") != NULL);
  assert(strstr(sz_string_cstr(dump), "radio:Night=1") != NULL);
  sz_string_free(dump);
  f = sz_view_frame(chip);
  assert(sz_view_handle_tap(col, f.x + f.w * 0.5f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(col);
  sz_signal_int_free(sig);
}

static void test_choice_chip_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_choicechip.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(1);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_choice_chip(sig, 0, "Day"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "choicechip:Day=0") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Day") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_action_chip_sizes(void) {
  SzView *ch, *wide;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, wf;

  sig = sz_signal_int(0);
  ch = sz_view_action_chip("Go", counter_tap, sig);
  wide = sz_view_action_chip("Go now", counter_tap, sig);
  sz_view_layout(ch, 200.f, 200.f, theme);
  sz_view_layout(wide, 200.f, 200.f, theme);
  assert(sz_view_kind(ch) == SZ_VIEW_ACTION_CHIP);
  f = sz_view_frame(ch);
  wf = sz_view_frame(wide);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(f.w >= 32.f);
  assert(wf.w > f.w);
  sz_view_free(ch);
  sz_view_free(wide);
  sz_signal_int_free(sig);
}

static void test_action_chip_empty_min_width(void) {
  SzView *ch;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  ch = sz_view_action_chip("", NULL, NULL);
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  assert(f.w >= 32.f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(ch);
}

static void test_action_chip_null_label(void) {
  SzView *ch;
  SzString *dump;

  ch = sz_view_action_chip(NULL, NULL, NULL);
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "actionchip:") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
}

static void test_action_chip_clamps_max_w(void) {
  SzView *ch;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  ch = sz_view_action_chip("Go", NULL, NULL);
  sz_view_layout(ch, 20.f, 80.f, theme);
  f = sz_view_frame(ch);
  assert(fabsf(f.w - 20.f) < 0.5f);
  sz_view_free(ch);
}

static void test_action_chip_does_not_wrap(void) {
  SzView *ch;
  const SzTheme *theme = sz_theme_default();
  float full_h;

  ch = sz_view_action_chip("one two", NULL, NULL);
  sz_view_layout(ch, 1000.f, 100.f, theme);
  full_h = sz_view_frame(ch).h;
  sz_view_free(ch);

  ch = sz_view_action_chip("one two", NULL, NULL);
  sz_view_layout(ch, 20.f, 100.f, theme);
  assert(fabsf(sz_view_frame(ch).h - full_h) < 0.5f);
  sz_view_free(ch);
}

static void test_action_chip_a11y(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  ch = sz_view_action_chip("Go", counter_tap, sig);
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "actionchip:Go") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_action_chip_a11y_distinct(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(0);
  ch = sz_view_action_chip("Go", counter_tap, sig);
  dump = sz_view_a11y_dump(ch);
  s = sz_string_cstr(dump);
  assert(strncmp(s, "actionchip:", 11) == 0);
  assert(strstr(s, "actionchip:Go") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_action_chip_tap(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_action_chip("Go", counter_tap, sig);
  sz_view_layout(ch, 200.f, 80.f, theme);
  f = sz_view_frame(ch);
  assert(sz_view_is_tap_target(ch));
  assert(sz_view_handle_tap(ch, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_action_chip_tap_twice(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_action_chip("Go", counter_tap, sig);
  sz_view_layout(ch, 200.f, 80.f, theme);
  f = sz_view_frame(ch);
  assert(sz_view_handle_tap(ch, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(ch, f.x + 4.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 2);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_action_chip_null_tap(void) {
  SzView *ch;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  ch = sz_view_action_chip("Go", NULL, NULL);
  sz_view_layout(ch, 200.f, 80.f, theme);
  f = sz_view_frame(ch);
  assert(sz_view_is_tap_target(ch));
  assert(!sz_view_handle_tap(ch, f.x + 4.f, f.y + f.h * 0.5f));
  sz_view_free(ch);
}

static void test_action_chip_miss(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_action_chip("Go", counter_tap, sig);
  sz_view_layout(ch, 200.f, 80.f, theme);
  f = sz_view_frame(ch);
  assert(!sz_view_handle_tap(ch, f.x - 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_action_chip_hit_test(void) {
  SzView *ch, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_action_chip("Go", counter_tap, sig);
  sz_view_layout(ch, 200.f, 80.f, theme);
  f = sz_view_frame(ch);
  hit = sz_view_hit_test(ch, f.x + 4.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_ACTION_CHIP);
  assert(sz_view_is_tap_target(hit));
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_action_chip_paint(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  root = sz_view_action_chip("Go", counter_tap, sig);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Fill is surface, not primary. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFF, 0xFF));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_action_chip_paint_not_primary(void) {
  SzView *filled, *chip;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  filled = sz_view_button("Go", counter_tap, sig);
  chip = sz_view_action_chip("Go", counter_tap, sig);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(filled, canvas, 80, 80, theme));
  f = sz_view_frame(filled);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(chip, canvas, 80, 80, theme));
  f = sz_view_frame(chip);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFF, 0xFF));
  sk_surface_unref(surf);
  sz_view_free(filled);
  sz_view_free(chip);
  sz_signal_int_free(sig);
}

static void test_action_chip_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_actionchip.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_action_chip("Go", counter_tap, sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "actionchip:Go") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "Go") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_input_chip_sizes(void) {
  SzView *ch, *plain;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f, pf;

  sig = sz_signal_int(0);
  ch = sz_view_input_chip(sig, "In");
  plain = sz_view_chip(sig, "In");
  sz_view_layout(ch, 200.f, 200.f, theme);
  sz_view_layout(plain, 200.f, 200.f, theme);
  assert(sz_view_kind(ch) == SZ_VIEW_INPUT_CHIP);
  f = sz_view_frame(ch);
  pf = sz_view_frame(plain);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(f.w >= 32.f);
  assert(f.w > pf.w);
  sz_view_free(ch);
  sz_view_free(plain);
  sz_signal_int_free(sig);
}

static void test_input_chip_empty_min_width(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_input_chip(sig, "");
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  assert(f.w >= 32.f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_input_chip_null_label(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  ch = sz_view_input_chip(sig, NULL);
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "inputchip:") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_input_chip_clamps_max_w(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_input_chip(sig, "In");
  sz_view_layout(ch, 20.f, 80.f, theme);
  f = sz_view_frame(ch);
  assert(fabsf(f.w - 20.f) < 0.5f);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_input_chip_a11y_off_on(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  ch = sz_view_input_chip(sig, "In");
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "inputchip:In=0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "inputchip:In=1") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_input_chip_a11y_distinct(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;
  const char *s;

  sig = sz_signal_int(0);
  ch = sz_view_input_chip(sig, "In");
  dump = sz_view_a11y_dump(ch);
  s = sz_string_cstr(dump);
  assert(strncmp(s, "inputchip:", 10) == 0);
  assert(strstr(s, "inputchip:In=0") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_input_chip_nonzero_is_on(void) {
  SzView *ch;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(7);
  ch = sz_view_input_chip(sig, "X");
  dump = sz_view_a11y_dump(ch);
  assert(strstr(sz_string_cstr(dump), "inputchip:X=1") != NULL);
  sz_string_free(dump);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_input_chip_tap_toggles(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_input_chip(sig, "In");
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  assert(sz_view_is_tap_target(ch));
  assert(sz_view_handle_tap(ch, f.x + f.w * 0.5f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  assert(sz_view_handle_tap(ch, f.x + 2.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_input_chip_miss(void) {
  SzView *ch;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_input_chip(sig, "In");
  sz_view_layout(ch, 200.f, 80.f, theme);
  f = sz_view_frame(ch);
  assert(!sz_view_handle_tap(ch, f.x - 8.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 0);
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_input_chip_hit_test(void) {
  SzView *ch, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  ch = sz_view_input_chip(sig, "In");
  sz_view_layout(ch, 200.f, 200.f, theme);
  f = sz_view_frame(ch);
  hit = sz_view_hit_test(ch, f.x + f.w * 0.5f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_INPUT_CHIP);
  assert(sz_view_is_tap_target(hit));
  sz_view_free(ch);
  sz_signal_int_free(sig);
}

static void test_input_chip_paint_off_on(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  root = sz_view_input_chip(sig, "In");
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFF, 0xFF));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_input_chip_paint_mark_on(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  float box;
  int mx, my;

  sig = sz_signal_int(1);
  root = sz_view_input_chip(sig, "In");
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  f = sz_view_frame(root);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  box = theme->font_px + 4.f;
  if (box < 12.f)
    box = 12.f;
  mx = (int)(f.x + f.w - theme->pad - box * 0.5f);
  my = (int)(f.y + f.h * 0.5f);
  /* Trailing X is on_primary. */
  assert(px_rgb(px, 80, mx, my, 0xF0, 0xF0, 0xF0));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_input_chip_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_inputchip.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_input_chip(sig, "In"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "inputchip:In=0") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "In") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_radio_sizes(void) {
  SzView *r;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  r = sz_view_radio(sig, 1, "On");
  sz_view_layout(r, 200.f, 200.f, theme);
  assert(sz_view_kind(r) == SZ_VIEW_RADIO);
  f = sz_view_frame(r);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(f.w > 12.f);
  sz_view_free(r);
  sz_signal_int_free(sig);
}

static void test_radio_a11y_off_on(void) {
  SzView *r;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(0);
  r = sz_view_radio(sig, 1, "On");
  dump = sz_view_a11y_dump(r);
  assert(strstr(sz_string_cstr(dump), "radio:On=0") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(r);
  assert(strstr(sz_string_cstr(dump), "radio:On=1") != NULL);
  sz_string_free(dump);
  sz_view_free(r);
  sz_signal_int_free(sig);
}

static void test_radio_tap_writes_value(void) {
  SzView *r;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  r = sz_view_radio(sig, 2, "Two");
  sz_view_layout(r, 200.f, 200.f, theme);
  f = sz_view_frame(r);
  assert(sz_view_handle_tap(r, f.x + f.w * 0.5f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 2);
  assert(sz_view_handle_tap(r, f.x + 2.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 2);
  sz_view_free(r);
  sz_signal_int_free(sig);
}

static void test_radio_group_exclusive(void) {
  SzView *col, *a, *b;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzString *dump;
  SzRect f;

  sig = sz_signal_int(0);
  col = sz_view_column();
  a = sz_view_radio(sig, 0, "Home");
  b = sz_view_radio(sig, 1, "Tasks");
  sz_view_add_child(col, a);
  sz_view_add_child(col, b);
  sz_view_layout(col, 200.f, 200.f, theme);
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "radio:Home=1") != NULL);
  assert(strstr(sz_string_cstr(dump), "radio:Tasks=0") != NULL);
  sz_string_free(dump);
  f = sz_view_frame(b);
  assert(sz_view_handle_tap(col, f.x + 2.f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 1);
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "radio:Home=0") != NULL);
  assert(strstr(sz_string_cstr(dump), "radio:Tasks=1") != NULL);
  sz_string_free(dump);
  sz_view_free(col);
  sz_signal_int_free(sig);
}

static void test_radio_hit_test(void) {
  SzView *r, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  r = sz_view_radio(sig, 1, "On");
  sz_view_layout(r, 200.f, 200.f, theme);
  f = sz_view_frame(r);
  hit = sz_view_hit_test(r, f.x + 2.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_RADIO);
  assert(sz_view_is_tap_target(hit));
  sz_view_free(r);
  sz_signal_int_free(sig);
}

static void test_radio_paint_off_on(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  sig = sz_signal_int(0);
  root = sz_view_radio(sig, 1, "On");
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 6, 16, 0xF5, 0xF5, 0xF5));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 6, 16, 0x14, 0x28, 0x50));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_radio_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_radio.dump";
  char *dump;

  sig = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_radio(sig, 1, "On"));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "radio:On=0") != NULL);
  assert(strstr(dump, "[taps]") != NULL);
  assert(strstr(dump, "On") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_slider_sizes(void) {
  SzView *sl;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(40);
  sl = sz_view_slider(sig);
  sz_view_layout(sl, 200.f, 200.f, theme);
  assert(sz_view_kind(sl) == SZ_VIEW_SLIDER);
  f = sz_view_frame(sl);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  assert(fabsf(f.w - 200.f) < 0.5f);
  sz_view_free(sl);
  sz_signal_int_free(sig);
}

static void test_slider_a11y(void) {
  SzView *sl;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(40);
  sl = sz_view_slider(sig);
  dump = sz_view_a11y_dump(sl);
  assert(strstr(sz_string_cstr(dump), "slider:40") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 7);
  dump = sz_view_a11y_dump(sl);
  assert(strstr(sz_string_cstr(dump), "slider:7") != NULL);
  sz_string_free(dump);
  sz_view_free(sl);
  sz_signal_int_free(sig);
}

static void test_slider_clamps_a11y(void) {
  SzView *sl;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(150);
  sl = sz_view_slider(sig);
  dump = sz_view_a11y_dump(sl);
  assert(strstr(sz_string_cstr(dump), "slider:100") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, -3);
  dump = sz_view_a11y_dump(sl);
  assert(strstr(sz_string_cstr(dump), "slider:0") != NULL);
  sz_string_free(dump);
  sz_view_free(sl);
  sz_signal_int_free(sig);
}

static void test_slider_tap_sets_from_x(void) {
  SzView *sl;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  sl = sz_view_slider(sig);
  sz_view_layout(sl, 200.f, 80.f, theme);
  f = sz_view_frame(sl);
  assert(sz_view_handle_tap(sl, f.x + f.w * 0.5f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 50);
  assert(sz_view_handle_tap(sl, f.x + f.w * 0.25f, f.y + f.h * 0.5f));
  assert(sz_signal_int_get(sig) == 25);
  sz_view_free(sl);
  sz_signal_int_free(sig);
}

static void test_slider_tap_clamps_edges(void) {
  SzView *sl;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(40);
  sl = sz_view_slider(sig);
  sz_view_layout(sl, 200.f, 80.f, theme);
  f = sz_view_frame(sl);
  assert(sz_view_handle_tap(sl, f.x - 8.f, f.y + f.h * 0.5f) == 0);
  assert(sz_view_slider_set_at(sl, f.x - 8.f));
  assert(sz_signal_int_get(sig) == 0);
  assert(sz_view_slider_set_at(sl, f.x + f.w + 8.f));
  assert(sz_signal_int_get(sig) == 100);
  sz_view_free(sl);
  sz_signal_int_free(sig);
}

static void test_slider_hit_test(void) {
  SzView *sl, *hit;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(40);
  sl = sz_view_slider(sig);
  sz_view_layout(sl, 200.f, 80.f, theme);
  f = sz_view_frame(sl);
  hit = sz_view_hit_test(sl, f.x + f.w * 0.5f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_SLIDER);
  assert(sz_view_is_tap_target(hit));
  sz_view_free(sl);
  sz_signal_int_free(sig);
}

static void test_slider_paint_fill(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(100);
  root = sz_view_slider(sig);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(root);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + f.h * 0.5f), 0x14, 0x28,
                0x50));
  sz_signal_int_set(sig, 0);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + f.w - 8.f), (int)(f.y + f.h * 0.5f), 0x6A,
                0x6A, 0x6A));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_slider_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_slider.dump";
  char *dump;

  sig = sz_signal_int(40);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_slider(sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "slider:40") != NULL);
  assert(strstr(dump, "[taps]") != NULL);
  assert(strstr(dump, "slider") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_slider_pointer_drag(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *sl;
  SzSignalInt *sig;
  SzInputEvent ev;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  sl = sz_view_slider(sig);
  root = sz_view_column();
  sz_view_add_child(root, sl);
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  sz_view_layout(root, 200.f, 80.f, theme);
  f = sz_view_frame(sl);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_DOWN;
  ev.x = f.x + 4.f;
  ev.y = f.y + f.h * 0.5f;
  assert(sz_ui_inject_sync(session, &ev));
  ev.pointer_phase = SZ_POINTER_MOVE;
  ev.x = f.x + f.w * 0.8f;
  assert(sz_ui_inject_sync(session, &ev));
  ev.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_signal_int_get(sig) == 80);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
}

static void test_replace_root_drops_pointer(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root1, *root2, *sl;
  SzSignalInt *sig;
  SzInputEvent ev;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int64_t v0;

  sig = sz_signal_int(0);
  sl = sz_view_slider(sig);
  root1 = sz_view_column();
  sz_view_add_child(root1, sl);
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root1);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  sz_view_layout(root1, 200.f, 80.f, theme);
  f = sz_view_frame(sl);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_DOWN;
  ev.x = f.x + 4.f;
  ev.y = f.y + f.h * 0.5f;
  assert(sz_ui_inject_sync(session, &ev));
  v0 = sz_signal_int_get(sig);
  root2 = sz_view_column();
  sz_view_add_child(root2, sz_view_text("after"));
  assert(sz_ui_session_replace_root(session, root2));
  ev.pointer_phase = SZ_POINTER_MOVE;
  ev.x = f.x + f.w * 0.8f;
  (void)sz_ui_inject_sync(session, &ev);
  assert(sz_signal_int_get(sig) == v0);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
}

static void test_slider_live_records_xy(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root, *sl;
  SzSignalInt *sig;
  SzInputEvent ev;
  const char *record = "/tmp/scuzz_ui_slider.script";
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  char *body;

  remove(record);
  sig = sz_signal_int(0);
  sl = sz_view_slider(sig);
  root = sz_view_column();
  sz_view_add_child(root, sl);
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_DESKTOP;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_record(session, record));
  assert(sz_ui_pump_sync(session));
  sz_view_layout(root, 200.f, 80.f, theme);
  f = sz_view_frame(sl);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_DOWN;
  ev.x = f.x + f.w * 0.2f;
  ev.y = f.y + f.h * 0.5f;
  assert(sz_ui_session_live_inject(session, &ev));
  ev.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_session_live_inject(session, &ev));
  body = slurp_cstr(record);
  assert(strstr(body, "xy ") != NULL);
  assert(strstr(body, "tap ") == NULL);
  free(body);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(record);
}

static void test_progress_sizes(void) {
  SzView *p;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(40);
  p = sz_view_progress(sig);
  sz_view_layout(p, 200.f, 200.f, theme);
  assert(sz_view_kind(p) == SZ_VIEW_PROGRESS);
  f = sz_view_frame(p);
  assert(fabsf(f.h - 8.f) < 0.5f);
  assert(fabsf(f.w - 200.f) < 0.5f);
  sz_view_free(p);
  sz_signal_int_free(sig);
}

static void test_progress_unbounded_width(void) {
  SzView *p;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(40);
  p = sz_view_progress(sig);
  sz_view_layout(p, 0.f, 80.f, theme);
  f = sz_view_frame(p);
  assert(fabsf(f.w - 120.f) < 0.5f);
  sz_view_free(p);
  sz_signal_int_free(sig);
}

static void test_progress_a11y(void) {
  SzView *p;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(40);
  p = sz_view_progress(sig);
  dump = sz_view_a11y_dump(p);
  assert(strstr(sz_string_cstr(dump), "progress:40") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 7);
  dump = sz_view_a11y_dump(p);
  assert(strstr(sz_string_cstr(dump), "progress:7") != NULL);
  sz_string_free(dump);
  sz_view_free(p);
  sz_signal_int_free(sig);
}

static void test_progress_clamps_a11y(void) {
  SzView *p;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(150);
  p = sz_view_progress(sig);
  dump = sz_view_a11y_dump(p);
  assert(strstr(sz_string_cstr(dump), "progress:100") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, -3);
  dump = sz_view_a11y_dump(p);
  assert(strstr(sz_string_cstr(dump), "progress:0") != NULL);
  sz_string_free(dump);
  sz_view_free(p);
  sz_signal_int_free(sig);
}

static void test_progress_not_tap_target(void) {
  SzView *p;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(40);
  p = sz_view_progress(sig);
  sz_view_layout(p, 200.f, 80.f, theme);
  f = sz_view_frame(p);
  assert(!sz_view_is_tap_target(p));
  assert(sz_view_hit_test(p, f.x + f.w * 0.5f, f.y + f.h * 0.5f) == NULL);
  assert(sz_view_handle_tap(p, f.x + f.w * 0.5f, f.y + f.h * 0.5f) == 0);
  assert(sz_signal_int_get(sig) == 40);
  sz_view_free(p);
  sz_signal_int_free(sig);
}

static void test_progress_paint_fill(void) {
  SzView *root;
  SzSignalInt *sig;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(100);
  root = sz_view_progress(sig);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(root);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + f.h * 0.5f), 0x14, 0x28,
                0x50));
  sz_signal_int_set(sig, 0);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + f.h * 0.5f), 0x6A, 0x6A,
                0x6A));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_progress_not_in_taps_dump(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *sig;
  const char *path = "/tmp/scuzz_ui_progress.dump";
  char *dump;
  const char *taps;

  sig = sz_signal_int(40);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_progress(sig));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "progress:40") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  assert(strstr(taps, "progress") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_ignore_pointer_sizes_to_child(void) {
  SzView *wrap, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect wf, chf;

  child = sz_view_sized(40, 30, sz_view_text("Hi"));
  wrap = sz_view_ignore_pointer(child);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(sz_view_kind(wrap) == SZ_VIEW_IGNORE_POINTER);
  wf = sz_view_frame(wrap);
  chf = sz_view_frame(child);
  assert(fabsf(wf.w - 40.f) < 0.5f);
  assert(fabsf(wf.h - 30.f) < 0.5f);
  assert(fabsf(chf.w - 40.f) < 0.5f);
  assert(fabsf(chf.h - 30.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_ignore_pointer_passes_tap_through(void) {
  SzView *stack, *behind, *front, *wrap, *hit;
  SzSignalInt *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect ff;

  a = sz_signal_int(0);
  b = sz_signal_int(0);
  stack = sz_view_stack();
  behind = sz_view_button("Go", counter_tap, a);
  front = sz_view_button("Go", counter_tap, b);
  wrap = sz_view_ignore_pointer(front);
  sz_view_add_child(stack, behind);
  sz_view_add_child(stack, wrap);
  sz_view_layout(stack, 200.f, 100.f, theme);
  ff = sz_view_frame(front);
  hit = sz_view_hit_test(stack, ff.x + 4.f, ff.y + 4.f);
  assert(hit == behind);
  assert(sz_view_handle_tap(stack, ff.x + 4.f, ff.y + 4.f));
  assert(sz_signal_int_get(a) == 1);
  assert(sz_signal_int_get(b) == 0);
  sz_view_free(stack);
  sz_signal_int_free(a);
  sz_signal_int_free(b);
}

static void test_absorb_pointer_blocks_tap(void) {
  SzView *stack, *behind, *front, *wrap, *hit;
  SzSignalInt *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect ff;

  a = sz_signal_int(0);
  b = sz_signal_int(0);
  stack = sz_view_stack();
  behind = sz_view_button("Go", counter_tap, a);
  front = sz_view_button("Go", counter_tap, b);
  wrap = sz_view_absorb_pointer(front);
  sz_view_add_child(stack, behind);
  sz_view_add_child(stack, wrap);
  sz_view_layout(stack, 200.f, 100.f, theme);
  ff = sz_view_frame(front);
  hit = sz_view_hit_test(stack, ff.x + 4.f, ff.y + 4.f);
  assert(hit == wrap);
  assert(sz_view_kind(hit) == SZ_VIEW_ABSORB_POINTER);
  assert(!sz_view_handle_tap(stack, ff.x + 4.f, ff.y + 4.f));
  assert(sz_signal_int_get(a) == 0);
  assert(sz_signal_int_get(b) == 0);
  sz_view_free(stack);
  sz_signal_int_free(a);
  sz_signal_int_free(b);
}

static void test_stack_front_button_wins_without_ignore(void) {
  SzView *stack, *behind, *front, *hit;
  SzSignalInt *a, *b;
  const SzTheme *theme = sz_theme_default();
  SzRect ff;

  a = sz_signal_int(0);
  b = sz_signal_int(0);
  stack = sz_view_stack();
  behind = sz_view_button("Go", counter_tap, a);
  front = sz_view_button("Go", counter_tap, b);
  sz_view_add_child(stack, behind);
  sz_view_add_child(stack, front);
  sz_view_layout(stack, 200.f, 100.f, theme);
  ff = sz_view_frame(front);
  hit = sz_view_hit_test(stack, ff.x + 4.f, ff.y + 4.f);
  assert(hit == front);
  assert(sz_view_handle_tap(stack, ff.x + 4.f, ff.y + 4.f));
  assert(sz_signal_int_get(a) == 0);
  assert(sz_signal_int_get(b) == 1);
  sz_view_free(stack);
  sz_signal_int_free(a);
  sz_signal_int_free(b);
}

static void test_ignore_pointer_skips_scroll_at(void) {
  SzView *wrap, *scroll, *body;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  body = sz_view_sized(20, 80, sz_view_text("x"));
  scroll = sz_view_scroll(body);
  wrap = sz_view_ignore_pointer(sz_view_sized(80, 40, scroll));
  sz_view_layout(wrap, 80.f, 40.f, theme);
  f = sz_view_frame(wrap);
  assert(sz_view_scroll_at(wrap, f.x + 4.f, f.y + 4.f) == NULL);
  sz_view_free(wrap);
}

static void test_absorb_pointer_skips_scroll_at(void) {
  SzView *wrap, *scroll, *body;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  body = sz_view_sized(20, 80, sz_view_text("x"));
  scroll = sz_view_scroll(body);
  wrap = sz_view_absorb_pointer(sz_view_sized(80, 40, scroll));
  sz_view_layout(wrap, 80.f, 40.f, theme);
  f = sz_view_frame(wrap);
  assert(sz_view_scroll_at(wrap, f.x + 4.f, f.y + 4.f) == NULL);
  sz_view_free(wrap);
}

static void test_exclude_semantics_sizes_to_child(void) {
  SzView *wrap, *child;
  const SzTheme *theme = sz_theme_default();
  SzRect wf, chf;

  child = sz_view_sized(40, 30, sz_view_text("Hi"));
  wrap = sz_view_exclude_semantics(child);
  sz_view_layout(wrap, 200.f, 200.f, theme);
  assert(sz_view_kind(wrap) == SZ_VIEW_EXCLUDE_SEMANTICS);
  wf = sz_view_frame(wrap);
  chf = sz_view_frame(child);
  assert(fabsf(wf.w - 40.f) < 0.5f);
  assert(fabsf(wf.h - 30.f) < 0.5f);
  assert(fabsf(chf.w - 40.f) < 0.5f);
  assert(fabsf(chf.h - 30.f) < 0.5f);
  sz_view_free(wrap);
}

static void test_exclude_semantics_hides_from_dump(void) {
  SzView *col, *wrap;
  SzString *dump;

  col = sz_view_column();
  wrap = sz_view_exclude_semantics(sz_view_button("Secret", NULL, NULL));
  sz_view_add_child(col, wrap);
  sz_view_add_child(col, sz_view_text("hi"));
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "button:Secret") == NULL);
  assert(strstr(sz_string_cstr(dump), "text:hi") != NULL);
  sz_string_free(dump);
  sz_view_free(col);
}

static void test_exclude_semantics_hides_nested(void) {
  SzView *inner, *wrap;
  SzString *dump;

  inner = sz_view_column();
  sz_view_add_child(inner, sz_view_text("one"));
  sz_view_add_child(inner, sz_view_text("two"));
  wrap = sz_view_exclude_semantics(inner);
  dump = sz_view_a11y_dump(wrap);
  assert(strstr(sz_string_cstr(dump), "text:one") == NULL);
  assert(strstr(sz_string_cstr(dump), "text:two") == NULL);
  assert(sz_string_cstr(dump)[0] == '\0');
  sz_string_free(dump);
  sz_view_free(wrap);
}

static void test_exclude_semantics_hides_bind_text(void) {
  SzSignalStr *s;
  SzView *wrap;
  SzString *dump;

  s = sz_signal_str("live");
  wrap = sz_view_exclude_semantics(sz_view_text_signal_str(s));
  dump = sz_view_a11y_dump(wrap);
  assert(strstr(sz_string_cstr(dump), "text:live") == NULL);
  sz_string_free(dump);
  sz_signal_str_set(s, "later");
  dump = sz_view_a11y_dump(wrap);
  assert(strstr(sz_string_cstr(dump), "text:later") == NULL);
  sz_string_free(dump);
  sz_view_free(wrap);
  sz_signal_str_free(s);
}

static void test_exclude_semantics_still_tappable(void) {
  SzView *wrap, *btn, *hit;
  SzSignalInt *n;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  n = sz_signal_int(0);
  btn = sz_view_button("Go", counter_tap, n);
  wrap = sz_view_exclude_semantics(btn);
  sz_view_layout(wrap, 200.f, 100.f, theme);
  f = sz_view_frame(btn);
  hit = sz_view_hit_test(wrap, f.x + 4.f, f.y + 4.f);
  assert(hit == btn);
  assert(sz_view_handle_tap(wrap, f.x + 4.f, f.y + 4.f));
  assert(sz_signal_int_get(n) == 1);
  sz_view_free(wrap);
  sz_signal_int_free(n);
}

static void test_exclude_semantics_keeps_scroll_at(void) {
  SzView *wrap, *scroll, *body;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  body = sz_view_sized(20, 80, sz_view_text("x"));
  scroll = sz_view_scroll(body);
  wrap = sz_view_exclude_semantics(sz_view_sized(80, 40, scroll));
  sz_view_layout(wrap, 80.f, 40.f, theme);
  f = sz_view_frame(wrap);
  assert(sz_view_scroll_at(wrap, f.x + 4.f, f.y + 4.f) == scroll);
  sz_view_free(wrap);
}

static void test_exclude_semantics_skips_field_collect(void) {
  SzView *col, *hidden, *shown;
  SzView *fields[8];
  SzSignalStr *a, *b;
  SzString *dump;
  int n;
  const SzTheme *theme = sz_theme_default();

  a = sz_signal_str("secret");
  b = sz_signal_str("ok");
  hidden = sz_view_text_field(a, "hidden");
  shown = sz_view_text_field(b, "shown");
  col = sz_view_column();
  sz_view_add_child(col, sz_view_exclude_semantics(hidden));
  sz_view_add_child(col, shown);
  sz_view_layout(col, 200.f, 120.f, theme);
  n = sz_view_collect_text_fields(col, fields, 8);
  assert(n == 1);
  assert(fields[0] == shown);
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "textfield:hidden") == NULL);
  assert(strstr(sz_string_cstr(dump), "textfield:shown") != NULL);
  sz_string_free(dump);
  sz_view_free(col);
  sz_signal_str_free(a);
  sz_signal_str_free(b);
}

static void test_exclude_semantics_sibling_button_stays(void) {
  SzView *col;
  SzString *dump;

  col = sz_view_column();
  sz_view_add_child(col, sz_view_exclude_semantics(sz_view_button("Skip", NULL, NULL)));
  sz_view_add_child(col, sz_view_button("Keep", NULL, NULL));
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "button:Skip") == NULL);
  assert(strstr(sz_string_cstr(dump), "button:Keep") != NULL);
  sz_string_free(dump);
  sz_view_free(col);
}

static void test_button_does_not_wrap(void) {
  SzView *b;
  const SzTheme *theme = sz_theme_default();
  float full_h;

  b = sz_view_button("one two", NULL, NULL);
  sz_view_layout(b, 1000.f, 100.f, theme);
  full_h = sz_view_frame(b).h;
  sz_view_free(b);

  b = sz_view_button("one two", NULL, NULL);
  sz_view_layout(b, 20.f, 100.f, theme);
  assert(fabsf(sz_view_frame(b).h - full_h) < 0.5f);
  sz_view_free(b);
}

static void test_text_blank_line_from_newline(void) {
  SzView *t;
  const SzTheme *theme = sz_theme_default();
  float line_h = theme->font_px + 6.f;

  t = sz_view_text("one\n\ntwo");
  sz_view_layout(t, 1000.f, 200.f, theme);
  assert(fabsf(sz_view_frame(t).h - 3.f * line_h) < 0.5f);
  sz_view_free(t);
}

static void test_a11y(void) {
  SzView *btn;
  SzView *col;
  SzString *dump;

  btn = sz_view_button("Go", NULL, NULL);
  assert(sz_view_a11y_role(btn) == SZ_A11Y_BUTTON);
  assert(strcmp(sz_view_a11y_label(btn), "Go") == 0);
  col = sz_view_column();
  sz_view_add_child(col, btn);
  sz_view_add_child(col, sz_view_text("hi"));
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "button:Go") != NULL);
  assert(strstr(sz_string_cstr(dump), "text:hi") != NULL);
  sz_string_free(dump);
  sz_view_free(col);

  {
    SzSignalStr *s;
    SzView *bound;
    s = sz_signal_str("one");
    bound = sz_view_text_signal_str(s);
    assert(sz_view_a11y_role(bound) == SZ_A11Y_TEXT);
    dump = sz_view_a11y_dump(bound);
    assert(strstr(sz_string_cstr(dump), "text:one") != NULL);
    sz_string_free(dump);
    sz_signal_str_set(s, "two");
    dump = sz_view_a11y_dump(bound);
    assert(strstr(sz_string_cstr(dump), "text:two") != NULL);
    assert(strstr(sz_string_cstr(dump), "text:one") == NULL);
    sz_string_free(dump);
    sz_view_free(bound);
    sz_signal_str_free(s);
  }
}

static void test_a11y_dump_grows_past_4k(void) {
  SzView *col;
  SzString *dump;
  char lab[64];
  int i;
  const char *s;

  col = sz_view_column();
  for (i = 0; i < 80; i++) {
    snprintf(lab, sizeof lab, "L%02d-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
             i);
    sz_view_add_child(col, sz_view_button(lab, NULL, NULL));
  }
  sz_view_add_child(col, sz_view_button("TAIL", NULL, NULL));
  dump = sz_view_a11y_dump(col);
  s = sz_string_cstr(dump);
  assert(strlen(s) > 4096);
  assert(strstr(s, "button:TAIL") != NULL);
  sz_string_free(dump);
  sz_view_free(col);
}

static void test_clear_children(void) {
  SzView *list;
  SzString *dump;

  list = sz_view_list();
  sz_view_add_child(list, sz_view_text("old"));
  dump = sz_view_a11y_dump(list);
  assert(strstr(sz_string_cstr(dump), "text:old") != NULL);

  sz_view_clear_children(list);
  dump = sz_view_a11y_dump(list);
  assert(strstr(sz_string_cstr(dump), "text:old") == NULL);

  sz_view_add_child(list, sz_view_text("milk"));
  sz_view_add_child(list, sz_view_text("eggs"));
  dump = sz_view_a11y_dump(list);
  assert(strstr(sz_string_cstr(dump), "text:milk") != NULL);
  assert(strstr(sz_string_cstr(dump), "text:eggs") != NULL);

  sz_view_clear_children(list);
  dump = sz_view_a11y_dump(list);
  assert(strstr(sz_string_cstr(dump), "text:milk") == NULL);
  sz_view_free(list);
}

static void test_view_each(void) {
  SzSignalList *items;
  SzView *list;
  const SzTheme *theme = sz_theme_default();
  SzList *xs;

  xs = sz_list_cons(sz_string_from_cstr("milk"), sz_list_nil());
  items = sz_signal_list(xs);
  list = sz_view_each(items);
  sz_view_layout(list, 200.f, 120.f, theme);
  assert(strstr(sz_string_cstr(sz_view_a11y_dump(list)), "text:- milk") != NULL);

  xs = sz_list_cons(sz_string_from_cstr("eggs"), xs);
  sz_signal_list_set(items, xs);
  sz_release(xs);
  sz_view_layout(list, 200.f, 120.f, theme);
  assert(strstr(sz_string_cstr(sz_view_a11y_dump(list)), "text:- eggs") != NULL);
  assert(strstr(sz_string_cstr(sz_view_a11y_dump(list)), "text:- milk") != NULL);

  sz_view_free(list);
  sz_signal_list_free(items);
}

static void test_view_each_setlist_rebuilds(void) {
  SzSignalList *items;
  SzView *list;
  const SzTheme *theme = sz_theme_default();
  SzList *xs;
  SzString *dump;
  int i;

  xs = sz_list_cons(sz_string_from_cstr("old"), sz_list_nil());
  items = sz_signal_list(xs);
  sz_release(xs);
  list = sz_view_each(items);
  sz_view_layout(list, 200.f, 120.f, theme);
  dump = sz_view_a11y_dump(list);
  assert(strstr(sz_string_cstr(dump), "text:- old") != NULL);
  sz_string_free(dump);

  xs = sz_list_cons(sz_string_from_cstr("new"), sz_list_nil());
  sz_signal_list_set(items, xs);
  sz_release(xs);
  for (i = 0; i < 64; i++) {
    SzList *t = sz_list_cons(sz_string_from_cstr("churn"), sz_list_nil());
    sz_release(t);
  }
  sz_view_layout(list, 200.f, 120.f, theme);
  dump = sz_view_a11y_dump(list);
  assert(strstr(sz_string_cstr(dump), "text:- new") != NULL);
  assert(strstr(sz_string_cstr(dump), "text:- old") == NULL);
  sz_string_free(dump);
  sz_view_free(list);
  sz_signal_list_free(items);
}

static SzView *each_map_text(SzString *item, void *env) {
  (void)env;
  return sz_view_text(item ? sz_string_cstr(item) : "");
}

static SzView *each_map_button(SzString *item, void *env) {
  return sz_view_button(item ? sz_string_cstr(item) : "", counter_tap, env);
}

static void test_view_each_map_text(void) {
  SzSignalList *items;
  SzView *list;
  const SzTheme *theme = sz_theme_default();
  SzList *xs;
  const char *dump;

  xs = sz_list_cons(sz_string_from_cstr("milk"), sz_list_nil());
  items = sz_signal_list(xs);
  list = sz_view_each_map(items, each_map_text, NULL);
  sz_view_layout(list, 200.f, 120.f, theme);
  dump = sz_string_cstr(sz_view_a11y_dump(list));
  assert(strstr(dump, "text:milk") != NULL);
  assert(strstr(dump, "text:- milk") == NULL);

  xs = sz_list_cons(sz_string_from_cstr("eggs"), sz_list_nil());
  sz_signal_list_set(items, xs);
  sz_release(xs);
  sz_view_layout(list, 200.f, 120.f, theme);
  dump = sz_string_cstr(sz_view_a11y_dump(list));
  assert(strstr(dump, "text:eggs") != NULL);
  assert(strstr(dump, "text:milk") == NULL);

  sz_view_free(list);
  sz_signal_list_free(items);
}

static void test_view_each_map_button(void) {
  SzSignalList *items;
  SzSignalInt *count;
  SzView *list, *hit;
  const SzTheme *theme = sz_theme_default();
  SzList *xs;
  SzRect f;
  SzUiConfig cfg;
  SzUiSession *session;
  const char *path = "/tmp/scuzz_ui_each_map.dump";
  char *dump;

  xs = sz_list_cons(sz_string_from_cstr("milk"), sz_list_nil());
  items = sz_signal_list(xs);
  count = sz_signal_int(0);
  list = sz_view_each_map(items, each_map_button, count);
  sz_view_layout(list, 200.f, 120.f, theme);
  assert(strstr(sz_string_cstr(sz_view_a11y_dump(list)), "button:milk") != NULL);
  f = sz_view_frame(list);
  hit = sz_view_hit_test(list, f.x + theme->pad + 4.f, f.y + theme->pad + 4.f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_BUTTON);
  assert(sz_view_is_tap_target(hit));
  assert(sz_view_handle_tap(list, f.x + theme->pad + 4.f, f.y + theme->pad + 4.f));
  assert(sz_signal_int_get(count) == 1);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, list);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "[taps]") != NULL);
  assert(strstr(dump, "milk") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(count);
  sz_signal_list_free(items);
  remove(path);
}

static SzView *each_map_studio_row(SzString *item, void *env) {
  SzView *row = sz_view_row();
  const char *s = item ? sz_string_cstr(item) : "";
  (void)env;
  sz_view_add_child(row, sz_view_expanded(sz_view_text(s)));
  sz_view_add_child(row, sz_view_button("Del", NULL, NULL));
  return row;
}

static void test_each_expanded_row_in_scroll(void) {
  SzSignalList *items;
  SzView *col, *list, *scroll, *exp;
  SzList *xs;
  SzUiConfig cfg;
  SzUiSession *session;
  const char *path = "/tmp/scuzz_ui_each_scroll.dump";
  char *dump;
  const char *taps;
  const char *p0;
  const char *p1;
  const char *c0;
  const char *c1;
  int y0;
  int y1;

  xs = sz_list_cons(sz_string_from_cstr("eggs"),
                    sz_list_cons(sz_string_from_cstr("milk"), sz_list_nil()));
  items = sz_signal_list(xs);
  col = sz_view_column();
  list = sz_view_each_map(items, each_map_studio_row, NULL);
  scroll = sz_view_scroll(list);
  exp = sz_view_expanded(scroll);
  sz_view_add_child(col, sz_view_text("title"));
  sz_view_add_child(col, exp);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 400;
  cfg.height = 280;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, col);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, "text:milk") != NULL);
  assert(strstr(dump, "text:eggs") != NULL);
  taps = strstr(dump, "[taps]\n");
  assert(taps != NULL);
  p0 = strstr(taps, "Del ");
  assert(p0 != NULL);
  p1 = strstr(p0 + 4, "Del ");
  assert(p1 != NULL);
  c0 = strchr(p0, ',');
  c1 = strchr(p1, ',');
  assert(c0 && c1);
  y0 = atoi(c0 + 1);
  y1 = atoi(c1 + 1);
  /* Unbounded scroll height must not stretch each Expanded row. */
  assert(y1 > y0);
  assert(y1 - y0 < 80);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_list_free(items);
  remove(path);
}

static void test_property_signal_list_len(void) {
  SzSignalList *items;
  SzList *xs;
  SzString *dump;
  SzString *name;
  const char *s;

  xs = sz_list_cons(sz_string_from_cstr("a"),
                    sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
  items = sz_signal_list(xs);
  sz_signal_name(items, "items");
  dump = sz_signal_dump();
  s = strstr(sz_string_cstr(dump), "list[");
  assert(s);
  assert(strstr(s, "items = "));
  name = sz_string_from_cstr("items");
  assert(sz_property_signal_list_len(name) == 2);
  sz_signal_list_set(items, sz_list_nil());
  assert(sz_property_signal_list_len(name) == 0);
  sz_release(name);
  name = sz_string_from_cstr("missing");
  assert(sz_property_signal_list_len(name) == 0);
  sz_release(name);
  sz_string_free(dump);
  sz_signal_list_free(items);
}

static void test_property_signal_list_at(void) {
  SzSignalList *items;
  SzList *xs;
  SzString *got;
  SzString *dump;
  SzString *name;
  const char *s;

  xs = sz_list_cons(sz_string_from_cstr("a"),
                    sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
  items = sz_signal_list(xs);
  sz_signal_name(items, "items");
  dump = sz_signal_dump();
  s = strstr(sz_string_cstr(dump), "list[");
  assert(s);
  name = sz_string_from_cstr("items");
  got = sz_property_signal_list_at(name, 0);
  assert(strcmp(sz_string_cstr(got), "a") == 0);
  sz_string_free(got);
  got = sz_property_signal_list_at(name, 1);
  assert(strcmp(sz_string_cstr(got), "b") == 0);
  sz_string_free(got);
  got = sz_property_signal_list_at(name, 2);
  assert(strcmp(sz_string_cstr(got), "") == 0);
  sz_string_free(got);
  got = sz_property_signal_list_at(name, -1);
  assert(strcmp(sz_string_cstr(got), "") == 0);
  sz_string_free(got);
  sz_release(name);
  name = sz_string_from_cstr("missing");
  got = sz_property_signal_list_at(name, 0);
  assert(strcmp(sz_string_cstr(got), "") == 0);
  sz_string_free(got);
  sz_release(name);
  sz_string_free(dump);
  sz_signal_list_free(items);
}

static void test_property_signal_int(void) {
  SzSignalInt *count;
  SzString *dump;
  SzString *name;
  const char *s;

  count = sz_signal_int(7);
  dump = sz_signal_dump();
  s = strstr(sz_string_cstr(dump), "int[");
  assert(s);
  /* Unnamed signals dump with no name and read as 0 by name. */
  assert(strstr(s, " = 7"));
  name = sz_string_from_cstr("count");
  assert(sz_property_signal_int(name) == 0);
  sz_signal_name(count, "count");
  assert(sz_property_signal_int(name) == 7);
  sz_signal_int_set(count, 9);
  assert(sz_property_signal_int(name) == 9);
  sz_release(name);
  sz_string_free(dump);
  sz_signal_int_free(count);
}

static void test_signal_list_record_dump(void) {
  SzSignalList *items;
  SzList *xs;
  SzString *dump;
  SzString *name;
  SzString *got;
  const char *s;

  xs = sz_list_cons(sz_box_i64(1), sz_list_cons(sz_box_i64(2), sz_list_nil()));
  items = sz_lang_signal_list(xs, sz_string_from_cstr("rows"), 0);
  dump = sz_signal_dump();
  s = strstr(sz_string_cstr(dump), "list[");
  assert(s);
  /* Non-String elements dump the count only. */
  assert(strstr(s, "rows = <2>"));
  name = sz_string_from_cstr("rows");
  assert(sz_property_signal_list_len(name) == 2);
  got = sz_property_signal_list_at(name, 0);
  assert(strcmp(sz_string_cstr(got), "") == 0);
  sz_string_free(got);
  sz_release(name);
  sz_string_free(dump);
  sz_signal_list_free(items);
}

static void test_property_signal_str(void) {
  SzSignalStr *draft;
  SzString *got;
  SzString *dump;
  SzString *name;
  const char *s;

  draft = sz_signal_str("milk");
  sz_signal_name(draft, "draft");
  dump = sz_signal_dump();
  s = strstr(sz_string_cstr(dump), "str[");
  assert(s);
  assert(strstr(s, "draft = "));
  name = sz_string_from_cstr("draft");
  got = sz_property_signal_str(name);
  assert(strcmp(sz_string_cstr(got), "milk") == 0);
  sz_string_free(got);
  sz_signal_str_set(draft, "oat");
  got = sz_property_signal_str(name);
  assert(strcmp(sz_string_cstr(got), "oat") == 0);
  sz_string_free(got);
  sz_release(name);
  name = sz_string_from_cstr("missing");
  got = sz_property_signal_str(name);
  assert(strcmp(sz_string_cstr(got), "") == 0);
  sz_string_free(got);
  sz_release(name);
  sz_string_free(dump);
  sz_signal_str_free(draft);
}

static void test_signal_dump_escapes(void) {
  SzSignalStr *s;
  SzSignalList *items;
  SzList *xs;
  SzString *dump;
  SzString *name;
  SzString *got;
  const char *d;
  char big[2001];
  int i;

  s = sz_signal_str("a\"b");
  sz_signal_name(s, "q");
  dump = sz_signal_dump();
  d = sz_string_cstr(dump);
  assert(strstr(d, "a\\\"b") != NULL);
  sz_string_free(dump);

  sz_signal_str_set(s, "a\nb");
  dump = sz_signal_dump();
  d = sz_string_cstr(dump);
  assert(strstr(d, "a\\nb") != NULL);
  sz_string_free(dump);

  for (i = 0; i < 2000; i++)
    big[i] = 'x';
  big[2000] = '\0';
  sz_signal_str_set(s, big);
  dump = sz_signal_dump();
  d = sz_string_cstr(dump);
  assert(strlen(d) > 2000);
  assert(strstr(d, big) != NULL);
  sz_string_free(dump);
  sz_signal_str_free(s);

  xs = sz_list_cons(sz_string_from_cstr("a\"b"),
                    sz_list_cons(sz_string_from_cstr("a\nb"), sz_list_nil()));
  items = sz_signal_list(xs);
  sz_signal_name(items, "xs");
  dump = sz_signal_dump();
  d = sz_string_cstr(dump);
  assert(strstr(d, "a\\\"b") != NULL);
  assert(strstr(d, "a\\nb") != NULL);
  name = sz_string_from_cstr("xs");
  assert(sz_property_signal_list_len(name) == 2);
  got = sz_property_signal_list_at(name, 0);
  assert(strcmp(sz_string_cstr(got), "a\"b") == 0);
  sz_string_free(got);
  got = sz_property_signal_list_at(name, 1);
  assert(strcmp(sz_string_cstr(got), "a\nb") == 0);
  sz_string_free(got);
  sz_release(name);
  sz_string_free(dump);
  sz_signal_list_free(items);
}

static void test_signal_name_last_wins(void) {
  SzSignalInt *a;
  SzSignalInt *b;
  SzString *name;

  a = sz_signal_int(1);
  b = sz_signal_int(2);
  sz_signal_name(a, "n");
  sz_signal_name(b, "n");
  name = sz_string_from_cstr("n");
  assert(sz_property_signal_int(name) == 2);
  sz_release(name);
  sz_signal_int_free(a);
  sz_signal_int_free(b);
}

static void test_property_replay_str_list(void) {
  SzSignalStr *draft;
  SzSignalList *items;
  SzList *xs;
  SzString *name;
  SzString *got;

  setenv("SCUZZ_TESTRT", "1", 1);
  sz_property_session_reset();
  draft = sz_signal_str("old");
  sz_signal_name(draft, "draft");
  xs = sz_list_cons(sz_string_from_cstr("a"),
                    sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
  items = sz_signal_list(xs);
  sz_signal_name(items, "items");
  sz_property_session_step();
  sz_signal_str_set(draft, "new");
  sz_signal_list_set(items, sz_list_nil());
  name = sz_string_from_cstr("draft");
  got = sz_property_signal_str(name);
  assert(strcmp(sz_string_cstr(got), "new") == 0);
  sz_string_free(got);
  sz_timeline_replay_from(0);
  got = sz_property_signal_str(name);
  assert(strcmp(sz_string_cstr(got), "old") == 0);
  sz_string_free(got);
  sz_release(name);
  name = sz_string_from_cstr("items");
  assert(sz_property_signal_list_len(name) == 2);
  got = sz_property_signal_list_at(name, 1);
  assert(strcmp(sz_string_cstr(got), "b") == 0);
  sz_string_free(got);
  sz_timeline_replay_from(-1);
  assert(sz_property_signal_list_len(name) == 0);
  sz_release(name);
  sz_property_session_reset();
  unsetenv("SCUZZ_TESTRT");
  sz_signal_str_free(draft);
  sz_signal_list_free(items);
}

static void test_signal_list_spine_collect(void) {
  SzSignalList *items;
  SzList *xs;
  SzString *first;
  SzString *x;
  size_t origin_count = 0, origin_bytes = 0;
  size_t base_count = 0, base_bytes = 0;
  size_t live_count = 0, live_bytes = 0;
  int i;

  sz_alloc_stats(&origin_bytes, &origin_count);
    first = sz_string_from_cstr("a");
    xs = sz_list_cons(first, sz_list_nil());
    sz_string_free(first);
    items = sz_signal_list(xs);
    sz_release(xs);
  sz_alloc_stats(&base_bytes, &base_count);
  /* Append copies the spine; set frees the unshared previous spine. */
  for (i = 0; i < 30; i++) {
    x = sz_string_from_cstr("x");
    xs = sz_list_append(sz_signal_list_get(items), x);
    sz_release(x);
    sz_signal_list_set(items, xs);
    sz_release(xs);
  }
  sz_alloc_stats(&live_bytes, &live_count);
  /* Linear in current length, not quadratic leftover spines. */
  assert(live_count < base_count + 30 * 6);
  assert(sz_list_len(sz_signal_list_get(items)) == 31);
  sz_signal_list_free(items);
  sz_alloc_stats(&live_bytes, &live_count);
  assert(live_count == origin_count);
  assert(live_bytes == origin_bytes);
}

static void test_alloc_each_pump_flat(void) {
  SzUiConfig cfg;
  SzSignalList *items;
  SzView *root;
  SzUiSession *session;
  SzList *xs;
  size_t base_count = 0, base_bytes = 0;
  size_t live_count = 0, live_bytes = 0;
  size_t max_count = 0;
  int i;

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 120;
  cfg.scale = 1.0;

  xs = sz_list_cons(sz_string_from_cstr("milk"), sz_list_nil());
  items = sz_signal_list(xs);
  sz_release(xs);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_each(items));
  session = sz_ui_mount(&cfg, root);
  assert(session);
  assert(sz_ui_pump_sync(session));
  sz_alloc_stats(&base_bytes, &base_count);
  max_count = base_count;
  for (i = 0; i < 2000; i++) {
    assert(sz_ui_pump_sync(session));
    sz_alloc_stats(&live_bytes, &live_count);
    if (live_count > max_count)
      max_count = live_count;
  }
  assert(live_count == base_count);
  assert(live_bytes == base_bytes);
  assert(max_count <= base_count + 8);

  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_list_free(items);
}

static void test_view_editor(void) {
  SzSignalStr *buf, *field_sig;
  SzView *root, *ed, *field, *btn;
  SzUiConfig cfg;
  SzUiSession *session;
  const SzTheme *theme = sz_theme_default();
  const char *path = "/tmp/scuzz_ui_inject_editor.script";
  const char *dump = "/tmp/scuzz_ui_inject_editor.dump";
  char *body;
  SzString *a11y;
  char long_s[400];
  int i;
  SzView *fields[8];
  SzView *taps[8];
  SzInputEvent ev;
  SzRect fr;

  buf = sz_signal_str("");
  root = sz_view_column();
  btn = sz_view_button("Go", NULL, NULL);
  sz_view_add_child(root, btn);
  field_sig = sz_signal_str("hi");
  field = sz_view_text_field(field_sig, "item");
  sz_view_add_child(root, field);
  ed = sz_view_editor(buf);
  sz_view_add_child(root, ed);
  sz_view_layout(root, 240.f, 200.f, theme);
  assert(sz_view_kind(ed) == SZ_VIEW_EDITOR);
  assert(sz_view_collect_text_fields(root, fields, 8) == 1);
  assert(fields[0] == field);
  assert(sz_view_collect_editors(root, fields, 8) == 1);
  assert(fields[0] == ed);
  assert(sz_view_collect_tap_targets(root, taps, 8) == 1);
  assert(taps[0] == btn);
  a11y = sz_view_a11y_dump(root);
  assert(strstr(sz_string_cstr(a11y), "editor:editor") != NULL);
  assert(strstr(sz_string_cstr(a11y), "textfield:item") != NULL);
  sz_string_free(a11y);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 240;
  cfg.height = 200;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_pump_sync(session));

  /* Keys go to the starred field while the editor is unfocused. */
  write_stamp(path, "key a a\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(field_sig), "hia") == 0);
  assert(strcmp(sz_signal_str_get(buf), "") == 0);

  fr = sz_view_frame(ed);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TAP;
  ev.x = fr.x + 8.f;
  ev.y = fr.y + 8.f;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));

  /* Rewrite must not share a prefix with the prior `key a a` stamp (suffix play). */
  write_stamp(path, "key z z\nkey Enter\nkey Tab\nkey b b\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "z\n  b") == 0);
  assert(strcmp(sz_signal_str_get(field_sig), "hia") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "[fields]") != NULL);
  assert(strstr(body, "item=\"hia\"") != NULL);
  assert(strstr(body, "[editor]") != NULL);
  assert(strstr(body, "0* caret=5 sel=5:5 sx=0 sy=0 lines=2 \"z\\n  b\"") != NULL);
  {
    const char *taps_sec = strstr(body, "[taps]\n");
    assert(taps_sec != NULL);
    assert(strstr(taps_sec, "0 Go") != NULL);
  }
  free(body);

  write_stamp(path, "caret 2\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_editor_caret(ed) == 2);

  write_stamp(path, "select 0 2\nkey x x\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "x  b") == 0);

  write_stamp(path, "select 0 1\ncopy\nkey End\npaste\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "x  bx") == 0);

  write_stamp(path, "text ab\nkey Enter\nkey c c\nkey d d\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "ab\ncd") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"ab\\ncd\"") != NULL);
  free(body);

  write_stamp(path, "caret 3\nkey Backspace\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "abcd") == 0);

  write_stamp(path, "caret 2\nkey Enter\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "ab\ncd") == 0);

  write_stamp(path, "select 0 5\ncut\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "") == 0);
  write_stamp(path, "paste\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "ab\ncd") == 0);

  /* No 256-byte cap on the editor buffer. */
  for (i = 0; i < 300; i++)
    long_s[i] = 'a';
  long_s[300] = '\0';
  sz_signal_str_set(buf, long_s);
  assert(sz_view_set_editor_caret(ed, 300));
  write_stamp(path, "dump\n");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "[editor]") != NULL);
  assert(strstr(body, long_s) != NULL);
  assert(strstr(body, "caret=300") != NULL);
  free(body);
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "editor:editor") != NULL);
  assert(strstr(sz_string_cstr(a11y), long_s) == NULL);
  sz_string_free(a11y);
  {
    SkSurface *surf = sk_surface_make_raster_n32_premul(240, 200);
    SkCanvas *canvas;
    assert(surf);
    canvas = sk_surface_get_canvas(surf);
    assert(canvas);
    assert(sz_view_paint(sz_ui_session_root(session), canvas, 240, 200, theme));
    sk_surface_unref(surf);
  }

  sz_ui_unmount(session);
  sz_signal_str_free(buf);
  sz_signal_str_free(field_sig);
  remove(path);
  remove(dump);
}

static void test_view_editor_viewport(void) {
  SzSignalStr *buf;
  SzView *root, *ed;
  SzUiConfig cfg;
  SzUiSession *session;
  const SzTheme *theme = sz_theme_default();
  const char *path = "/tmp/scuzz_ui_editor_viewport.script";
  const char *dump = "/tmp/scuzz_ui_editor_viewport.dump";
  char *body;
  char long_line[97];
  char tall[512];
  int i;
  SzInputEvent ev;
  SzRect fr;
  float cell = sk_font_mono_cell(theme->font_px);
  float line_h = theme->font_px + 6.f;

  buf = sz_signal_str("");
  root = sz_view_column();
  ed = sz_view_editor(buf);
  sz_view_add_child(root, ed);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 80;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_pump_sync(session));

  fr = sz_view_frame(ed);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TAP;
  ev.x = fr.x + 8.f;
  ev.y = fr.y + 8.f;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));

  /* Long line: End pans horizontally. */
  for (i = 0; i < 96; i++)
    long_line[i] = 'a';
  long_line[96] = '\0';
  sz_signal_str_set(buf, long_line);
  write_stamp(path, "key End\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_editor_scroll_x(ed) > 0.f);
  body = slurp_cstr(dump);
  assert(strstr(body, "[editor]") != NULL);
  assert(strstr(body, "sx=0") == NULL);
  free(body);

  /* Tall file: caret at end pans vertically. Paint visible lines only. */
  tall[0] = '\0';
  for (i = 0; i < 40; i++)
    strcat(tall, "x\n");
  strcat(tall, "z");
  sz_signal_str_set(buf, tall);
  sz_view_set_editor_caret(ed, (int)strlen(tall));
  write_stamp(path, "caret 80\nkey End\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_editor_scroll_y(ed) > 0.f);
  body = slurp_cstr(dump);
  assert(strstr(body, "sy=0") == NULL);
  free(body);
  {
    SkSurface *surf = sk_surface_make_raster_n32_premul(80, 80);
    SkCanvas *canvas;
    assert(surf);
    canvas = sk_surface_get_canvas(surf);
    assert(canvas);
    assert(sz_view_paint(sz_ui_session_root(session), canvas, 80, 80, theme));
    sk_surface_unref(surf);
  }

  /* Wheel over the editor pans Y. Editors omit from [scrolls]. */
  {
    float y0 = sz_view_editor_scroll_y(ed);
    SzView *scrolls[8];
    assert(sz_view_collect_scrolls(sz_ui_session_root(session), scrolls, 8) == 0);
    fr = sz_view_frame(ed);
    memset(&ev, 0, sizeof(ev));
    ev.kind = SZ_INPUT_SCROLL;
    ev.x = fr.x + 8.f;
    ev.y = fr.y + 8.f;
    ev.dy = -y0;
    assert(sz_ui_inject_sync(session, &ev));
    assert(sz_view_editor_scroll_y(ed) < y0);
  }

  /* Mono columns: i and W share a click-to-caret column. */
  sz_signal_str_set(buf, "ii\nWW");
  sz_view_set_editor_caret(ed, 0);
  write_stamp(path, "key Home\n");
  assert(sz_ui_pump_sync(session));
  fr = sz_view_frame(ed);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TAP;
  ev.x = fr.x + sz_view_editor_gutter_w(ed) + 6.f + cell * 1.5f;
  ev.y = fr.y + 6.f + line_h * 0.4f;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_view_editor_caret(ed) == 2);
  ev.y = fr.y + 6.f + line_h * 1.4f;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_view_editor_caret(ed) == 5);

  write_stamp(path, "key PageUp\nkey PageDown\n");
  assert(sz_ui_pump_sync(session));

  sz_ui_unmount(session);
  sz_signal_str_free(buf);
  remove(path);
  remove(dump);
}

static void test_view_editor_undo_gutter(void) {
  SzSignalStr *buf;
  SzView *root, *ed;
  SzUiConfig cfg;
  SzUiSession *session;
  const char *path = "/tmp/scuzz_ui_editor_undo.script";
  const char *dump = "/tmp/scuzz_ui_editor_undo.dump";
  char *body;
  int lines[2];
  int sevs[2];
  SzInputEvent ev;
  SzRect fr;

  buf = sz_signal_str("");
  root = sz_view_column();
  ed = sz_view_editor(buf);
  sz_view_add_child(root, ed);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 120;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_pump_sync(session));

  fr = sz_view_frame(ed);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TAP;
  ev.x = fr.x + sz_view_editor_gutter_w(ed) + 8.f;
  ev.y = fr.y + 8.f;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));

  write_stamp(path, "key a a\nkey b b\nkey c c\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "abc") == 0);
  write_stamp(path, "key z+ctrl\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "ab") == 0);
  write_stamp(path, "key y+ctrl\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "abc") == 0);
  assert(sz_view_editor_undo(ed));
  assert(strcmp(sz_signal_str_get(buf), "ab") == 0);
  assert(sz_view_editor_undo(ed));
  assert(strcmp(sz_signal_str_get(buf), "a") == 0);
  assert(sz_view_editor_undo(ed));
  assert(strcmp(sz_signal_str_get(buf), "") == 0);
  assert(sz_view_editor_redo(ed));
  assert(strcmp(sz_signal_str_get(buf), "a") == 0);

  sz_signal_str_set(buf, "def main\n// x\n");
  lines[0] = 1;
  lines[1] = 2;
  sevs[0] = 1;
  sevs[1] = 2;
  assert(sz_view_editor_set_diagnostics(ed, lines, sevs, 2));
  assert(sz_view_editor_line_count(ed) == 3);
  assert(sz_view_editor_gutter_w(ed) > 0.f);
  write_stamp(path, "dump\n");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "lines=3") != NULL);
  assert(strstr(body, "diag=1:1,2:2") != NULL);
  free(body);

  sz_signal_str_set(buf, "ab\ncd\n");
  assert(sz_view_editor_offset_at_line_col(ed, 1, 1) == 0);
  assert(sz_view_editor_offset_at_line_col(ed, 1, 3) == 2);
  assert(sz_view_editor_offset_at_line_col(ed, 2, 1) == 3);
  {
    SzIoResult r = sz_io_unsafe_run(sz_lang_ui_set_editor_caret(2, 1));
    assert(r.ok);
    assert(sz_view_editor_caret(ed) == 3);
    r = sz_io_unsafe_run(sz_lang_ui_editor_caret());
    assert(r.ok);
    assert(sz_unbox_i64(r.value) == 3);
    sz_release(r.value);
  }
  {
    void *ln = sz_box_i64(1);
    void *sv = sz_box_i64(1);
    SzPair *cell = sz_pair_new(ln, sv);
    SzList *xs;
    SzIoResult r;
    sz_release(ln);
    sz_release(sv);
    xs = sz_list_cons(cell, sz_list_nil());
    sz_release(cell);
    r = sz_io_unsafe_run(sz_lang_ui_set_editor_diagnostics(xs));
    assert(r.ok);
    sz_release(xs);
  }
  write_stamp(path, "dump\n");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "diag=1:1") != NULL);
  free(body);
  {
    SzIoResult r = sz_io_unsafe_run(sz_lang_ui_set_editor_diagnostics(NULL));
    assert(r.ok);
  }
  write_stamp(path, "dump\n");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "diag=") == NULL);
  free(body);
  {
    int data[5];
    int ilines[1];
    int icols[1];
    const char *ilabs[1];
    int fstarts[1];
    int fends[1];
    data[0] = 0;
    data[1] = 0;
    data[2] = 5;
    data[3] = 8;
    data[4] = 0;
    assert(sz_view_editor_set_tokens(ed, data, 5));
    assert(sz_view_editor_token_count(ed) == 1);
    ilines[0] = 0;
    icols[0] = 5;
    ilabs[0] = "Int";
    assert(sz_view_editor_set_inlays(ed, ilines, icols, ilabs, 1));
    assert(sz_view_editor_inlay_count(ed) == 1);
    fstarts[0] = 0;
    fends[0] = 1;
    assert(sz_view_editor_set_folds(ed, fstarts, fends, 1));
    assert(sz_view_editor_fold_count(ed) == 1);
  }
  {
    void *a = sz_box_i64(0);
    void *b = sz_box_i64(0);
    void *c = sz_box_i64(3);
    void *d = sz_box_i64(8);
    void *e = sz_box_i64(0);
    SzList *xs;
    SzIoResult r;
    xs = sz_list_cons(e, sz_list_nil());
    sz_release(e);
    xs = sz_list_cons(d, xs);
    sz_release(d);
    xs = sz_list_cons(c, xs);
    sz_release(c);
    xs = sz_list_cons(b, xs);
    sz_release(b);
    xs = sz_list_cons(a, xs);
    sz_release(a);
    r = sz_io_unsafe_run(sz_lang_ui_set_editor_tokens(xs));
    assert(r.ok);
    sz_release(xs);
  }
  {
    void *ln = sz_box_i64(0);
    void *col = sz_box_i64(1);
    SzString *lab = sz_string_from_cstr("T");
    SzPair *inner = sz_pair_new(col, lab);
    SzPair *cell;
    SzList *xs;
    SzIoResult r;
    sz_release(col);
    sz_release(lab);
    cell = sz_pair_new(ln, inner);
    sz_release(ln);
    sz_release(inner);
    xs = sz_list_cons(cell, sz_list_nil());
    sz_release(cell);
    r = sz_io_unsafe_run(sz_lang_ui_set_editor_inlays(xs));
    assert(r.ok);
    sz_release(xs);
  }
  {
    void *a = sz_box_i64(0);
    void *b = sz_box_i64(1);
    SzPair *cell = sz_pair_new(a, b);
    SzList *xs;
    SzIoResult r;
    sz_release(a);
    sz_release(b);
    xs = sz_list_cons(cell, sz_list_nil());
    sz_release(cell);
    r = sz_io_unsafe_run(sz_lang_ui_set_editor_folds(xs));
    assert(r.ok);
    sz_release(xs);
  }
  write_stamp(path, "dump\n");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "tok=1") != NULL);
  assert(strstr(body, "inlay=1") != NULL);
  assert(strstr(body, "fold=1") != NULL);
  free(body);
  {
    SkSurface *surf = sk_surface_make_raster_n32_premul(200, 120);
    SkCanvas *canvas;
    assert(surf);
    canvas = sk_surface_get_canvas(surf);
    assert(sz_view_paint(sz_ui_session_root(session), canvas, 200, 120,
                         sz_theme_default()));
    sk_surface_unref(surf);
  }
  {
    SzIoResult r = sz_io_unsafe_run(sz_lang_ui_set_editor_tokens(NULL));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_lang_ui_set_editor_inlays(NULL));
    assert(r.ok);
    r = sz_io_unsafe_run(sz_lang_ui_set_editor_folds(NULL));
    assert(r.ok);
  }
  write_stamp(path, "dump\n");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "tok=") == NULL);
  assert(strstr(body, "inlay=") == NULL);
  assert(strstr(body, "fold=") == NULL);
  free(body);
  {
    SkSurface *surf = sk_surface_make_raster_n32_premul(200, 120);
    SkCanvas *canvas;
    assert(surf);
    canvas = sk_surface_get_canvas(surf);
    assert(sz_view_paint(sz_ui_session_root(session), canvas, 200, 120,
                         sz_theme_default()));
    sk_surface_unref(surf);
  }

  sz_ui_unmount(session);
  sz_signal_str_free(buf);
  remove(path);
  remove(dump);
}

static void noop_tap(SzView *self, void *env) {
  (void)self;
  (void)env;
}

static void test_view_focus_split_overlay(void) {
  SzSignalStr *draft;
  SzSignalStr *pop;
  SzSignalInt *frac;
  SzSignalInt *open;
  SzView *root, *field, *btn, *split, *overlay, *pop_field;
  SzUiConfig cfg;
  SzUiSession *session;
  const char *path = "/tmp/scuzz_ui_a8.script";
  const char *dump = "/tmp/scuzz_ui_a8.dump";
  char *body;
  SzInputEvent ev;
  SzRect fr;

  draft = sz_signal_str("");
  pop = sz_signal_str("");
  frac = sz_signal_int(50);
  open = sz_signal_int(1);
  root = sz_view_stack();
  {
    SzView *col = sz_view_column();
    field = sz_view_text_field(draft, "main");
    btn = sz_view_button("Go", noop_tap, NULL);
    split = sz_view_split(frac, sz_view_text("L"), sz_view_text("R"));
    sz_view_add_child(col, field);
    sz_view_add_child(col, btn);
    sz_view_add_child(col, split);
    sz_view_add_child(root, col);
  }
  pop_field = sz_view_text_field(pop, "pop");
  overlay = sz_view_overlay(open, pop_field);
  sz_view_add_child(root, overlay);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 240;
  cfg.height = 200;
  cfg.scale = 1.0;
  cfg.title = "Scuzz Lang";
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_ui_session_title(session), "Scuzz Lang") == 0);
  assert(sz_ui_session_set_title(session, "Hello"));
  assert(strcmp(sz_ui_session_title(session), "Hello") == 0);

  write_stamp(path, "key p p\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(pop), "p") == 0);
  assert(strcmp(sz_signal_str_get(draft), "") == 0);

  write_stamp(path, "key Escape\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_overlay_is_open(overlay) == 0);

  fr = sz_view_frame(field);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TAP;
  ev.x = fr.x + 8.f;
  ev.y = fr.y + 8.f;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));
  assert(sz_view_has_focused_text_field(root));

  write_stamp(path, "key a a\n");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "a") == 0);

  fr = sz_view_frame(btn);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TAP;
  ev.x = fr.x + 8.f;
  ev.y = fr.y + 8.f;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));
  assert(!sz_view_has_focused_text_field(root));

  sz_view_layout(root, 240.f, 200.f, sz_theme_default());
  fr = sz_view_frame(split);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_DOWN;
  ev.pointer_button = 1;
  ev.x = fr.x + (fr.w > 6.f ? (fr.w - 6.f) * 0.5f : 0.f) + 3.f;
  ev.y = fr.y + 8.f;
  assert(sz_ui_inject_sync(session, &ev));
  ev.pointer_phase = SZ_POINTER_MOVE;
  ev.x = fr.x + fr.w * 0.25f;
  assert(sz_ui_inject_sync(session, &ev));
  ev.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));
  assert(sz_view_split_frac(split) == 25);

  write_stamp(path, "dump\n");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "title=Hello") != NULL);
  assert(strstr(body, "[splits]") != NULL);
  assert(strstr(body, "frac=25") != NULL);
  assert(strstr(body, "[overlays]") != NULL);
  assert(strstr(body, "open=0") != NULL);
  assert(strstr(body, "split:") != NULL);
  assert(strstr(body, "overlay:") != NULL);
  free(body);
  {
    SkSurface *surf = sk_surface_make_raster_n32_premul(240, 200);
    SkCanvas *canvas;
    assert(surf);
    canvas = sk_surface_get_canvas(surf);
    assert(sz_view_paint(sz_ui_session_root(session), canvas, 240, 200,
                         sz_theme_default()));
    sk_surface_unref(surf);
  }

  remove(path);
  remove(dump);
}

static void test_view_focus_group_keys(void) {
  SzSignalInt *a;
  SzSignalInt *b;
  SzSignalInt *go;
  SzSignalInt *open;
  SzSignalStr *draft;
  SzView *root, *group, *ba, *bb, *btn, *field, *overlay;
  SzUiConfig cfg;
  SzUiSession *session;
  const char *path = "/tmp/scuzz_ui_focus_group.script";
  const char *dump = "/tmp/scuzz_ui_focus_group.dump";
  char *body;
  SzInputEvent ev;
  SzRect fr;

  a = sz_signal_int(0);
  b = sz_signal_int(0);
  go = sz_signal_int(0);
  open = sz_signal_int(0);
  draft = sz_signal_str("");
  root = sz_view_stack();
  {
    SzView *col = sz_view_column();
    SzView *list = sz_view_column();
    ba = sz_view_button("sample.txt", counter_tap, a);
    bb = sz_view_button("scuzz.toml", counter_tap, b);
    sz_view_add_child(list, ba);
    sz_view_add_child(list, bb);
    group = sz_view_focus_group(list);
    field = sz_view_text_field(draft, "main");
    btn = sz_view_button("Go", counter_tap, go);
    overlay = sz_view_overlay(open, sz_view_text("Palette"));
    sz_view_add_child(col, group);
    sz_view_add_child(col, field);
    sz_view_add_child(col, btn);
    sz_view_add_child(root, col);
    sz_view_add_child(root, overlay);
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 240;
  cfg.height = 200;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_set_inject(session, path));
  assert(sz_ui_session_set_debug_dump(session, dump));
  assert(sz_ui_pump_sync(session));
  assert(sz_view_kind(group) == SZ_VIEW_FOCUS_GROUP);
  assert(!sz_view_is_tap_target(group));

  fr = sz_view_frame(field);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TAP;
  ev.x = fr.x + 8.f;
  ev.y = fr.y + 8.f;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));
  assert(sz_view_has_focused_text_field(root));
  assert(strcmp(sz_view_focus_kind(root), "field") == 0);

  write_stamp(path, "tap 0\ndump\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(a) == 1);
  assert(!sz_view_has_focused_text_field(root));
  assert(strcmp(sz_view_focus_kind(root), "button:sample.txt") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "focus=button:sample.txt") != NULL);
  free(body);

  write_stamp(path, "key ArrowDown\ndump\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(a) == 1);
  assert(sz_signal_int_get(b) == 0);
  assert(strcmp(sz_view_focus_kind(root), "button:scuzz.toml") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "focus=button:scuzz.toml") != NULL);
  free(body);

  write_stamp(path, "key Enter\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(b) == 1);

  write_stamp(path, "tap 0\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(a) == 2);
  sz_signal_int_set(open, 1);
  write_stamp(path, "key ArrowDown\ndump\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(b) == 1);
  assert(strcmp(sz_view_focus_kind(root), "overlay") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "focus=overlay") != NULL);
  free(body);

  sz_signal_int_set(open, 0);
  write_stamp(path, "key Space\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(a) == 3);

  write_stamp(path, "tap 2\ndump\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(go) == 1);
  assert(strcmp(sz_view_focus_kind(root), "none") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "focus=none") != NULL);
  free(body);

  write_stamp(path, "key ArrowDown\n");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(b) == 1);

  {
    SkSurface *surf = sk_surface_make_raster_n32_premul(240, 200);
    SkCanvas *canvas;
    assert(surf);
    canvas = sk_surface_get_canvas(surf);
    assert(sz_view_paint(sz_ui_session_root(session), canvas, 240, 200,
                         sz_theme_default()));
    sk_surface_unref(surf);
  }

  sz_ui_unmount(session);
  sz_signal_int_free(a);
  sz_signal_int_free(b);
  sz_signal_int_free(go);
  sz_signal_int_free(open);
  sz_signal_str_free(draft);
  remove(path);
  remove(dump);
}

static void test_text_field_edit(void) {
  SzSignalStr *draft;
  SzView *root;
  SzView *field;
  const SzTheme *theme = sz_theme_default();

  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "type");
  sz_view_add_child(root, field);
  sz_view_layout(root, 200.f, 80.f, theme);
  (void)field;

  /* Append / backspace on the (first) TextField. */
  assert(sz_view_handle_text_edit(root, "hi", 0));
  assert(strcmp(sz_signal_str_get(draft), "hi") == 0);
  assert(sz_view_handle_text_edit(root, "!", 0));
  assert(strcmp(sz_signal_str_get(draft), "hi!") == 0);
  assert(sz_view_handle_text_edit(root, NULL, 1));
  assert(strcmp(sz_signal_str_get(draft), "hi") == 0);
  assert(sz_view_handle_text_edit(root, "", 1));
  assert(strcmp(sz_signal_str_get(draft), "h") == 0);

  /* Inject path: TEXT_EDIT empty → backspace. */
  {
    SzUiConfig cfg;
    SzUiSession *session;
    SzInputEvent ev;
    memset(&cfg, 0, sizeof(cfg));
    cfg.kind = SZ_UI_RUNTIME_HEADLESS;
    cfg.width = 200;
    cfg.height = 80;
    session = sz_ui_mount(&cfg, root);
    assert(session);
    memset(&ev, 0, sizeof(ev));
    ev.kind = SZ_INPUT_TEXT_EDIT;
    ev.text = "ello";
    assert(sz_ui_inject_sync(session, &ev));
    assert(strcmp(sz_signal_str_get(draft), "hello") == 0);
    ev.text = "";
    assert(sz_ui_inject_sync(session, &ev));
    assert(strcmp(sz_signal_str_get(draft), "hell") == 0);
    sz_ui_unmount(session);
  }

  sz_view_free(root);
  sz_signal_str_free(draft);
}

static void test_app_chord_save(void) {
  SzSignalInt *n;
  SzView *root, *btn;
  SzUiConfig cfg;
  SzUiSession *session;
  SzInputEvent ev;

  n = sz_signal_int(0);
  root = sz_view_column();
  btn = sz_view_button("Save", counter_tap, n);
  sz_view_add_child(root, btn);
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "s";
  ev.key_mods = SZ_KEY_CTRL;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_signal_int_get(n) == 1);
  ev.key = "f";
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_signal_int_get(n) == 1);
  sz_ui_unmount(session);
  sz_signal_int_free(n);
}

static void test_app_chord_palette(void) {
  SzSignalInt *complete;
  SzSignalInt *palette;
  SzView *root, *btn;
  SzUiConfig cfg;
  SzUiSession *session;
  SzInputEvent ev;

  complete = sz_signal_int(0);
  palette = sz_signal_int(0);
  root = sz_view_column();
  btn = sz_view_button("Complete", counter_tap, complete);
  sz_view_add_child(root, btn);
  btn = sz_view_button("Palette", counter_tap, palette);
  sz_view_add_child(root, btn);
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "p";
  ev.key_mods = SZ_KEY_CTRL;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_signal_int_get(complete) == 1);
  assert(sz_signal_int_get(palette) == 0);
  ev.key_mods = SZ_KEY_CTRL | SZ_KEY_SHIFT;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_signal_int_get(complete) == 1);
  assert(sz_signal_int_get(palette) == 1);
  ev.key_mods = SZ_KEY_CMD | SZ_KEY_SHIFT;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_signal_int_get(palette) == 2);
  sz_ui_unmount(session);
  sz_signal_int_free(complete);
  sz_signal_int_free(palette);
}

static void test_caret_metrics(void) {
  SzSignalStr *draft;
  SzView *root;
  SzView *field;
  const SzTheme *theme = sz_theme_default();
  SzRect caret, fr;
  float want;

  draft = sz_signal_str("");
  root = sz_view_column();
  field = sz_view_text_field(draft, "type");
  sz_view_add_child(root, field);
  sz_view_layout(root, 200.f, 80.f, theme);
  caret = sz_view_caret_rect(root, theme);
  assert(caret.w == 0.f);

  assert(sz_view_handle_text_edit(root, "i", 0));
  sz_view_layout(root, 200.f, 80.f, theme);
  fr = sz_view_frame(field);
  caret = sz_view_caret_rect(root, theme);
  want = fr.x + 6.f + sk_font_measure_string("i", theme->font_px);
  assert(fabsf(caret.x - want) < 0.5f);
  assert(caret.w >= 1.f - 0.5f);
  assert(caret.h >= 1.f - 0.5f);

  /* Same byte length, different glyphs: measured advance, not n * cell. */
  assert(sz_view_handle_text_edit(root, "i", 0));
  sz_view_layout(root, 200.f, 80.f, theme);
  caret = sz_view_caret_rect(root, theme);
  want = fr.x + 6.f + sk_font_measure_string("ii", theme->font_px);
  assert(fabsf(caret.x - want) < 0.5f);
  sz_signal_str_set(draft, "WW");
  assert(sz_view_handle_key(root, "End", "", 0));
  sz_view_layout(root, 200.f, 80.f, theme);
  caret = sz_view_caret_rect(root, theme);
  want = fr.x + 6.f + sk_font_measure_string("WW", theme->font_px);
  assert(fabsf(caret.x - want) < 0.5f);

  sz_view_free(root);
  sz_signal_str_free(draft);
}

/* Mount a static label and pump repeatedly: live_count stays flat. */
static void test_alloc_pump_flat(void) {
  SzUiConfig cfg;
  SzView *view;
  SzUiSession *session;
  size_t base_count = 0, base_bytes = 0;
  size_t live_count = 0, live_bytes = 0;
  size_t max_count = 0;
  int i;

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 120;
  cfg.height = 60;
  cfg.scale = 1.0;

  view = sz_view_text("alloc");
  session = sz_ui_mount(&cfg, view);
  assert(session);
  assert(sz_ui_pump_sync(session));
  sz_alloc_stats(&base_bytes, &base_count);
  max_count = base_count;

  for (i = 0; i < 2000; i++) {
    assert(sz_ui_pump_sync(session));
    sz_alloc_stats(&live_bytes, &live_count);
    if (live_count > max_count)
      max_count = live_count;
  }
  /* No unbounded growth across pumps (temps may allocate then free). */
  assert(live_count == base_count);
  assert(live_bytes == base_bytes);
  assert(max_count <= base_count + 8);

  sz_ui_unmount(session);
  sz_view_free(view);
}

static SzString *map_count_label(int64_t v, void *env) {
  (void)env;
  return sz_string_from_int(v);
}

/* Counter-shaped UI (Signal.map + bindText + taps): live_count flat over pumps. */
static void test_alloc_counter_pump_flat(void) {
  SzUiConfig cfg;
  SzSignalInt *count;
  SzSignalStr *label;
  SzView *root, *btn;
  SzUiSession *session;
  SzInputEvent tap;
  size_t base_count = 0, base_bytes = 0;
  size_t live_count = 0, live_bytes = 0;
  size_t max_count = 0;
  int i;

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;

  count = sz_signal_int(0);
  label = sz_lang_signal_map(count, map_count_label, NULL, NULL);
  root = sz_view_column();
  sz_view_add_child(root, sz_lang_view_bind_text(label));
  btn = sz_view_button("+", counter_tap, count);
  sz_view_add_child(root, btn);

  session = sz_ui_mount(&cfg, root);
  assert(session);
  setenv("SCUZZ_TESTRT", "1", 1);
  /* Warm-up: layout + a few taps so map cache / frames settle. */
  assert(sz_ui_pump_sync(session));
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(btn).x + 8.f;
  tap.y = sz_view_frame(btn).y + 8.f;
  for (i = 0; i < 5; i++) {
    assert(sz_ui_inject_sync(session, &tap));
    assert(sz_ui_pump_sync(session));
  }
  sz_alloc_stats(&base_bytes, &base_count);
  max_count = base_count;

  for (i = 0; i < 2000; i++) {
    if ((i % 50) == 0) {
      assert(sz_ui_inject_sync(session, &tap));
    }
    assert(sz_ui_pump_sync(session));
    sz_alloc_stats(&live_bytes, &live_count);
    if (live_count > max_count)
      max_count = live_count;
  }
  /* live_count flat = no per-pump leak. live_bytes may grow a few bytes when
   * the mapped label gains digits (for example "9" → "10"). Same allocation count. */
  assert(live_count == base_count);
  assert(live_bytes <= base_bytes + 32);
  assert(max_count <= base_count + 16);

  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_str_free(label);
  sz_signal_int_free(count);
  unsetenv("SCUZZ_TESTRT");
}

/* Headless button with a captured list env: tap still fires after the
 * caller drops the list, and sz_view_free returns live_count to baseline. */
static void list_env_tap(SzView *self, void *env) {
  SzSignalInt *count = (SzSignalInt *)sz_list_head((SzList *)env);
  (void)self;
  sz_signal_int_set(count, sz_signal_int_get(count) + 1);
}

static void test_tap_env_retain_release(void) {
  SzUiConfig cfg;
  SzSignalInt *count;
  SzList *env;
  SzView *root, *btn;
  SzUiSession *session;
  SzInputEvent tap;
  size_t base_count = 0, base_bytes = 0;
  size_t live_count = 0, live_bytes = 0;

  count = sz_signal_int(0);
  sz_alloc_stats(&base_bytes, &base_count);
  env = sz_list_cons(count, sz_list_nil());
  btn = sz_view_button("+", list_env_tap, env);
  sz_release(env);
  sz_view_free(btn);
  sz_alloc_stats(&live_bytes, &live_count);
  assert(live_count == base_count);
  sz_signal_int_free(count);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;

  count = sz_signal_int(0);
  env = sz_list_cons(count, sz_list_nil());
  root = sz_view_column();
  btn = sz_view_button("+", list_env_tap, env);
  sz_view_add_child(root, btn);
  sz_release(env);

  session = sz_ui_mount(&cfg, root);
  assert(session);
  assert(sz_ui_pump_sync(session));
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(btn).x + 8.f;
  tap.y = sz_view_frame(btn).y + 8.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 1);
  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_int_free(count);
}

static SzView *each_row_from_list_env(SzString *item, void *env) {
  SzSignalInt *n = (SzSignalInt *)sz_list_head((SzList *)env);
  char buf[64];
  snprintf(buf, sizeof buf, "%s-%lld", item ? sz_string_cstr(item) : "",
           (long long)sz_signal_int_get(n));
  return sz_view_text(buf);
}

static void test_each_env_retain_release(void) {
  SzSignalInt *n;
  SzSignalList *items;
  SzList *xs, *env;
  SzView *list;
  SzString *dump;
  const SzTheme *theme = sz_theme_default();
  size_t base_count = 0, base_bytes = 0;
  size_t live_count = 0, live_bytes = 0;

  n = sz_signal_int(7);
  xs = sz_list_cons(sz_string_from_cstr("milk"), sz_list_nil());
  items = sz_signal_list(xs);
  sz_release(xs);
  sz_alloc_stats(&base_bytes, &base_count);

  env = sz_list_cons(n, sz_list_nil());
  list = sz_view_each_map(items, each_row_from_list_env, env);
  sz_release(env);
  sz_view_layout(list, 200.f, 120.f, theme);
  dump = sz_view_a11y_dump(list);
  assert(strstr(sz_string_cstr(dump), "text:milk-7") != NULL);
  sz_string_free(dump);
  sz_view_free(list);

  sz_alloc_stats(&live_bytes, &live_count);
  assert(live_count == base_count);
  sz_signal_list_free(items);
  sz_signal_int_free(n);
}

#ifdef __APPLE__
#define RELOAD_A "build/reload_a.dylib"
#define RELOAD_B "build/reload_b.dylib"
#else
#define RELOAD_A "build/reload_a.so"
#define RELOAD_B "build/reload_b.so"
#endif

static void test_session_load_code(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *count;
  SzString *a11y, *dump1, *dump2;

  count = sz_signal_int(7);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_text("init"));

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  sz_ui_session_set_rebuild(session, NULL, count);
  assert(sz_ui_session_load_code(session, RELOAD_A));
  {
    char staged[128];
    FILE *st;
    snprintf(staged, sizeof staged, "%s.load-1", RELOAD_A);
    st = fopen(staged, "rb");
    assert(st == NULL);
  }
  assert(sz_ui_session_reload(session));
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:A") != NULL);
  assert(strstr(sz_string_cstr(a11y), "text:n=") != NULL);
  sz_string_free(a11y);
  assert(sz_signal_int_get(count) == 7);

  sz_signal_int_set(count, 8);
  dump1 = sz_signal_dump();
  assert(sz_ui_session_load_code(session, RELOAD_B));
  assert(sz_ui_session_reload(session));
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:B") != NULL);
  assert(strstr(sz_string_cstr(a11y), "text:A") == NULL);
  sz_string_free(a11y);
  dump2 = sz_signal_dump();
  assert(strcmp(sz_string_cstr(dump1), sz_string_cstr(dump2)) == 0);
  assert(sz_signal_int_get(count) == 8);
  sz_string_free(dump1);
  sz_string_free(dump2);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
}

static void test_stamp_loads_reload_code(void) {
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  SzSignalInt *count;
  SzString *a11y, *dump1, *dump2;
  const char *stamp = "/tmp/scuzz_ui_watch_code.stamp";
  const char *code = "/tmp/scuzz_ui_watch_code.dylib";
  char staged[128];
  int i;

  count = sz_signal_int(7);
  root = init_code_factory(count);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  sz_ui_session_set_rebuild(session, init_code_factory, count);
  write_stamp(stamp, "0");
  assert(sz_ui_session_watch(session, stamp));
  setenv("SCUZZ_UI_RELOAD_CODE", code, 1);
  remove(code);
  assert(sz_ui_pump_sync(session));
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:init") != NULL);
  sz_string_free(a11y);

  write_stamp(stamp, "1");
  assert(sz_ui_pump_sync(session));
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:init") != NULL);
  sz_string_free(a11y);

  copy_file_test(RELOAD_A, code);
  write_stamp(stamp, "2");
  assert(sz_ui_pump_sync(session));
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:A") != NULL);
  assert(strstr(sz_string_cstr(a11y), "text:init") == NULL);
  sz_string_free(a11y);
  assert(sz_signal_int_get(count) == 7);

  sz_signal_int_set(count, 8);
  dump1 = sz_signal_dump();
  copy_file_test(RELOAD_B, code);
  write_stamp(stamp, "3");
  assert(sz_ui_pump_sync(session));
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:B") != NULL);
  assert(strstr(sz_string_cstr(a11y), "text:A") == NULL);
  sz_string_free(a11y);
  dump2 = sz_signal_dump();
  assert(strcmp(sz_string_cstr(dump1), sz_string_cstr(dump2)) == 0);
  assert(sz_signal_int_get(count) == 8);
  sz_string_free(dump1);
  sz_string_free(dump2);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  unsetenv("SCUZZ_UI_RELOAD_CODE");
  remove(stamp);
  remove(code);
  for (i = 1; i <= 8; i++) {
    snprintf(staged, sizeof staged, "%s.load-%d", code, i);
    remove(staged);
  }
}

int main(void) {
  test_session_snapshot();
  test_signals_layout_hit();
  test_replace_root_keeps_signals();
  test_watch_rebuild_keeps_signals();
  test_session_load_code();
  test_stamp_loads_reload_code();
  test_ui_run_rebuild();
  test_ui_run_rebuild_keepalive();
  test_session_debug_dump();
  test_xy_hit_and_miss();
  test_record_live_not_script();
  test_record_live_scroll();
  test_studio_shaped_xy();
  test_session_inject_script();
  test_session_inject_grows_past_4k();
  test_session_inject_control();
  test_session_dump_now_needs_path();
  test_session_inject_scroll();
  test_session_inject_backspace();
  test_session_inject_type();
  test_session_inject_key();
  test_session_inject_key_utf8_backspace();
  test_record_live_key();
  test_record_type_escapes();
  test_session_inject_key_repeat();
  test_session_inject_compose();
  test_record_live_hover_secondary();
  test_session_inject_caret();
  test_session_inject_hover_secondary();
  test_on_secondary_script_fires();
  test_session_inject_selection_clipboard();
  test_session_inject_field_index();
  test_button_set_and_show_when();
  test_widgets();
  test_expanded_column();
  test_expanded_row();
  test_center();
  test_align();
  test_stack();
  test_positioned();
  test_padding();
  test_sized();
  test_min_size();
  test_max_size_caps_sized();
  test_max_size_does_not_grow_child();
  test_max_size_zero_axis_is_uncapped();
  test_incoming_max_wins_over_max_size();
  test_min_size_inside_max_size_clamps();
  test_max_size_inside_column();
  test_max_size_with_stretch();
  test_nested_max_size_uses_tighter_cap();
  test_max_size_inside_row();
  test_clip_sizes_to_child();
  test_clip_paint_contains_overflow();
  test_opacity_sizes_to_child();
  test_opacity_paint_scales_alpha();
  test_background();
  test_aspect_ratio();
  test_fraction();
  test_expanded_text_fills_tight_slot();
  test_min_size_inside_expanded();
  test_column_non_flex_stays_intrinsic();
  test_row_non_flex_stays_intrinsic();
  test_row_overflow_keeps_button_width();
  test_wrap_kind();
  test_wrap_one_run_when_wide();
  test_wrap_sizes_to_runs_not_max();
  test_wrap_second_run_when_narrow();
  test_wrap_unbounded_stays_one_run();
  test_wrap_empty_is_pad();
  test_wrap_a11y_dumps_children();
  test_wrap_hit_test_second_run();
  test_wrap_gap_zero_is_flush();
  test_wrap_gap_spaces_runs();
  test_wrap_show_when_skips();
  test_wrap_in_column_grows_height();
  test_grid_kind();
  test_grid_two_cols_one_row();
  test_grid_third_child_new_row();
  test_grid_fills_max_width();
  test_grid_unbounded_sizes_to_cells();
  test_grid_empty_is_pad();
  test_grid_a11y_dumps_children();
  test_grid_hit_test_second_row();
  test_grid_gap_zero_is_flush();
  test_grid_gap_spaces_rows();
  test_grid_show_when_skips_cell();
  test_grid_cols_less_than_one_is_one();
  test_grid_in_column_grows_height();
  test_scroll_h_kind();
  test_scroll_h_sizes_viewport();
  test_scroll_h_unbounded_fits_child();
  test_scroll_h_scroll_by_pans_x();
  test_scroll_h_hit_test();
  test_scroll_h_pointer_drag();
  test_scroll_h_inject_script();
  test_sized_inside_column_keeps_box();
  test_padding_forwards_min_size();
  test_fraction_width_only_tightens_width();
  test_background_forwards_tight_slot();
  test_center_second_pass_keeps_child_size();
  test_expanded_row_text_fills_width();
  test_stretch_column_fills_width();
  test_stretch_row_fills_height();
  test_stretch_does_not_take_flex_height();
  test_stretch_background_fills_column();
  test_stretch_wrapping_expanded_still_flexes();
  test_expanded_wrapping_stretch_still_flexes();
  test_stretch_row_does_not_take_leftover_width();
  test_mobile_pointer_scroll_lifecycle();
  test_text_stays_one_line_when_unbounded();
  test_text_wraps_at_newline();
  test_text_wraps_at_space();
  test_text_hard_wraps_long_word();
  test_text_wrap_grows_column();
  test_bind_text_wraps_at_newline();
  test_max_lines_caps_newlines();
  test_max_lines_zero_is_uncapped();
  test_max_lines_caps_soft_wrap();
  test_nested_max_lines_uses_tighter_cap();
  test_bind_text_respects_max_lines();
  test_max_lines_does_not_wrap_button();
  test_ellipsis_sizes_to_child();
  test_ellipsis_keeps_one_line();
  test_ellipsis_short_text_stays_one_line();
  test_ellipsis_with_max_lines_keeps_cap();
  test_ellipsis_caps_soft_wrap();
  test_bind_text_respects_ellipsis();
  test_ellipsis_does_not_wrap_button();
  test_ellipsis_paint_hides_extra_lines();
  test_color_rgb_packs_opaque();
  test_color_rgba_packs_alpha();
  test_text_color_sizes_to_child();
  test_text_color_keeps_a11y();
  test_text_color_paints_red();
  test_nested_text_color_inner_wins();
  test_text_color_does_not_recolor_button();
  test_bind_text_respects_text_color();
  test_color_rgba_background_paints_alpha();
  test_gap_sizes_to_child();
  test_default_column_uses_theme_gap();
  test_gap_zero_stacks_column_flush();
  test_gap_n_spaces_column();
  test_negative_gap_is_zero();
  test_nested_gap_inner_wins();
  test_gap_spaces_row();
  test_gap_zero_row_is_flush();
  test_gap_zero_shrinks_column_height();
  test_gap_does_not_change_stack();
  test_font_size_sizes_to_child();
  test_font_size_grows_text();
  test_logical_px_match_device_scale();
  test_nested_font_size_inner_wins();
  test_font_size_zero_is_one();
  test_font_size_does_not_grow_button();
  test_font_size_wraps_sooner();
  test_border_sizes_to_child();
  test_border_keeps_a11y();
  test_border_zero_width_sizes_to_child();
  test_negative_border_is_zero();
  test_border_paints_inside_frame();
  test_border_zero_does_not_paint();
  test_nested_border_outer_paints_on_top();
  test_thick_border_fills_small_frame();
  test_border_hit_test_reaches_child();
  test_border_does_not_grow_button();
  test_radius_sizes_to_child();
  test_radius_keeps_a11y();
  test_radius_zero_fills_corners();
  test_negative_radius_is_zero();
  test_radius_clips_corners();
  test_nested_radius_inner_wins();
  test_radius_clips_border_corners();
  test_radius_hit_test_reaches_child();
  test_radius_does_not_grow_button();
  test_checkbox_sizes();
  test_checkbox_a11y_off_on();
  test_checkbox_nonzero_is_on();
  test_checkbox_tap_toggles();
  test_checkbox_hit_test();
  test_checkbox_paint_off_on();
  test_tap_collect_tree_order();
  test_activate_offscreen_button();
  test_checkbox_in_taps_dump();
  test_switch_sizes();
  test_switch_a11y_off_on();
  test_switch_nonzero_is_on();
  test_switch_tap_toggles();
  test_switch_hit_test();
  test_switch_paint_off_on();
  test_switch_in_taps_dump();
  test_chip_sizes();
  test_chip_a11y_off_on();
  test_chip_nonzero_is_on();
  test_chip_tap_toggles();
  test_chip_hit_test();
  test_chip_paint_off_on();
  test_chip_in_taps_dump();
  test_list_tile_sizes();
  test_list_tile_unbounded_width();
  test_list_tile_a11y();
  test_list_tile_not_tap_target();
  test_list_tile_trailing_tap();
  test_list_tile_paint();
  test_list_tile_trailing_in_taps_dump();
  test_badge_sizes();
  test_badge_a11y();
  test_badge_not_tap_target();
  test_badge_child_tap();
  test_badge_paint_mark();
  test_badge_child_in_taps_dump();
  test_card_sizes();
  test_card_empty_sizes();
  test_card_a11y();
  test_card_not_tap_target();
  test_card_child_tap();
  test_card_paint_pad();
  test_card_child_in_taps_dump();
  test_divider_sizes();
  test_divider_unbounded_width();
  test_divider_a11y();
  test_divider_not_tap_target();
  test_divider_paint_line();
  test_divider_in_column();
  test_divider_not_in_taps_dump();
  test_expansion_tile_sizes_collapsed();
  test_expansion_tile_sizes_expanded();
  test_expansion_tile_a11y();
  test_expansion_tile_tap_toggles();
  test_expansion_tile_child_tap();
  test_expansion_tile_paint();
  test_expansion_tile_in_taps_dump();
  test_icon_button_sizes();
  test_icon_button_a11y();
  test_icon_button_tap();
  test_icon_button_hit_test();
  test_icon_button_paint();
  test_icon_button_in_taps_dump();
  test_vertical_divider_sizes();
  test_vertical_divider_tight_height();
  test_vertical_divider_a11y();
  test_vertical_divider_not_tap_target();
  test_vertical_divider_paint_line();
  test_vertical_divider_in_row();
  test_vertical_divider_not_in_taps_dump();
  test_circular_progress_sizes();
  test_circular_progress_a11y();
  test_circular_progress_clamps_a11y();
  test_circular_progress_not_tap_target();
  test_circular_progress_paint_ring();
  test_circular_progress_in_row();
  test_circular_progress_not_in_taps_dump();
  test_avatar_sizes();
  test_avatar_a11y();
  test_avatar_not_tap_target();
  test_avatar_paint_disc();
  test_avatar_in_row();
  test_avatar_not_in_taps_dump();
  test_checkbox_list_tile_sizes();
  test_checkbox_list_tile_a11y();
  test_checkbox_list_tile_tap();
  test_checkbox_list_tile_hit_test();
  test_checkbox_list_tile_paint();
  test_checkbox_list_tile_in_taps_dump();
  test_switch_list_tile_sizes();
  test_switch_list_tile_a11y();
  test_switch_list_tile_tap();
  test_switch_list_tile_hit_test();
  test_switch_list_tile_paint();
  test_switch_list_tile_in_taps_dump();
  test_radio_list_tile_sizes();
  test_radio_list_tile_a11y();
  test_radio_list_tile_tap();
  test_radio_list_tile_group_exclusive();
  test_radio_list_tile_hit_test();
  test_radio_list_tile_paint();
  test_radio_list_tile_in_taps_dump();
  test_segmented_sizes();
  test_segmented_a11y();
  test_segmented_tap();
  test_segmented_hit_test();
  test_segmented_paint();
  test_segmented_in_taps_dump();
  test_fab_sizes();
  test_fab_a11y();
  test_fab_tap();
  test_fab_hit_test();
  test_fab_paint();
  test_fab_in_taps_dump();
  test_fab_tap_twice();
  test_fab_a11y_distinct();
  test_tooltip_sizes();
  test_tooltip_empty_sizes();
  test_tooltip_a11y();
  test_tooltip_not_tap_target();
  test_tooltip_child_tap();
  test_tooltip_paint_child();
  test_tooltip_paint_hover();
  test_tooltip_not_in_taps_dump();
  test_tooltip_child_in_taps_dump();
  test_tooltip_same_origin();
  test_tooltip_empty_message_a11y();
  test_outlined_button_sizes();
  test_outlined_button_empty_min_width();
  test_outlined_button_null_label();
  test_outlined_button_clamps_max_w();
  test_outlined_button_does_not_wrap();
  test_outlined_button_a11y();
  test_outlined_button_a11y_distinct();
  test_outlined_button_tap();
  test_outlined_button_tap_twice();
  test_outlined_button_null_tap();
  test_outlined_button_miss();
  test_outlined_button_hit_test();
  test_outlined_button_paint();
  test_outlined_button_paint_not_primary();
  test_outlined_button_in_taps_dump();
  test_text_button_sizes();
  test_text_button_empty_min_width();
  test_text_button_null_label();
  test_text_button_clamps_max_w();
  test_text_button_does_not_wrap();
  test_text_button_a11y();
  test_text_button_a11y_distinct();
  test_text_button_tap();
  test_text_button_tap_twice();
  test_text_button_null_tap();
  test_text_button_miss();
  test_text_button_hit_test();
  test_text_button_paint();
  test_text_button_paint_not_filled();
  test_text_button_in_taps_dump();
  test_placeholder_sizes();
  test_placeholder_empty_sizes();
  test_placeholder_same_origin();
  test_placeholder_a11y();
  test_placeholder_a11y_nested_tooltip();
  test_placeholder_not_tap_target();
  test_placeholder_child_tap();
  test_placeholder_paint_child();
  test_placeholder_paint_mark();
  test_placeholder_paint_empty();
  test_placeholder_not_in_taps_dump();
  test_placeholder_child_in_taps_dump();
  test_semantics_sizes();
  test_semantics_empty_sizes();
  test_semantics_same_origin();
  test_semantics_a11y();
  test_semantics_a11y_nested();
  test_semantics_empty_label();
  test_semantics_null_label();
  test_semantics_not_tap_target();
  test_semantics_child_tap();
  test_semantics_paint_child();
  test_semantics_paint_no_mark();
  test_semantics_paint_empty();
  test_semantics_not_in_taps_dump();
  test_semantics_child_in_taps_dump();
  test_merge_semantics_sizes();
  test_merge_semantics_empty_sizes();
  test_merge_semantics_same_origin();
  test_merge_semantics_a11y();
  test_merge_semantics_a11y_omits_nested();
  test_merge_semantics_a11y_distinct();
  test_merge_semantics_empty_label();
  test_merge_semantics_null_label();
  test_merge_semantics_not_tap_target();
  test_merge_semantics_child_tap();
  test_merge_semantics_paint_child();
  test_merge_semantics_paint_no_mark();
  test_merge_semantics_paint_empty();
  test_merge_semantics_not_in_taps_dump();
  test_merge_semantics_child_in_taps_dump();
  test_merge_semantics_skips_field_collect();
  test_ink_well_sizes();
  test_ink_well_empty_sizes();
  test_ink_well_same_origin();
  test_ink_well_a11y();
  test_ink_well_a11y_nested();
  test_ink_well_a11y_distinct();
  test_ink_well_empty_label();
  test_ink_well_null_label();
  test_ink_well_is_tap_target();
  test_ink_well_tap();
  test_ink_well_tap_twice();
  test_ink_well_null_tap();
  test_ink_well_miss();
  test_ink_well_child_button_wins();
  test_ink_well_paint_child();
  test_ink_well_paint_no_mark();
  test_ink_well_paint_empty();
  test_ink_well_in_taps_dump();
  test_ink_well_child_in_taps_dump();
  test_visibility_sizes_on();
  test_visibility_sizes_off();
  test_visibility_empty_sizes();
  test_visibility_same_origin();
  test_visibility_a11y_on();
  test_visibility_a11y_off();
  test_visibility_nonzero_is_on();
  test_visibility_not_tap_target();
  test_visibility_child_tap_on();
  test_visibility_child_tap_off();
  test_visibility_paint_on();
  test_visibility_paint_off();
  test_visibility_paint_empty();
  test_visibility_not_in_taps_dump();
  test_visibility_child_in_taps_on();
  test_visibility_child_not_in_taps_off();
  test_visibility_skips_field_collect_off();
  test_offstage_sizes_on();
  test_offstage_sizes_off();
  test_offstage_empty_sizes();
  test_offstage_same_origin();
  test_offstage_a11y_on();
  test_offstage_a11y_off();
  test_offstage_nonzero_is_on();
  test_offstage_not_tap_target();
  test_offstage_child_tap_on();
  test_offstage_child_tap_off();
  test_offstage_paint_on();
  test_offstage_paint_off();
  test_offstage_paint_empty();
  test_offstage_not_in_taps_dump();
  test_offstage_child_in_taps_on();
  test_offstage_child_not_in_taps_off();
  test_offstage_skips_field_collect_off();
  test_unconstrained_box_sizes();
  test_unconstrained_box_empty_sizes();
  test_unconstrained_box_same_origin();
  test_unconstrained_box_text_unbounded();
  test_unconstrained_box_a11y();
  test_unconstrained_box_a11y_nested();
  test_unconstrained_box_not_tap_target();
  test_unconstrained_box_child_tap();
  test_unconstrained_box_paint_child();
  test_unconstrained_box_paint_empty();
  test_unconstrained_box_not_in_taps_dump();
  test_unconstrained_box_child_in_taps_dump();
  test_unconstrained_box_collects_fields();
  test_filter_chip_sizes();
  test_filter_chip_empty_min_width();
  test_filter_chip_null_label();
  test_filter_chip_clamps_max_w();
  test_filter_chip_a11y_off_on();
  test_filter_chip_a11y_distinct();
  test_filter_chip_nonzero_is_on();
  test_filter_chip_tap_toggles();
  test_filter_chip_miss();
  test_filter_chip_hit_test();
  test_filter_chip_paint_off_on();
  test_filter_chip_paint_mark_on();
  test_filter_chip_in_taps_dump();
  test_choice_chip_sizes();
  test_choice_chip_empty_min_width();
  test_choice_chip_null_label();
  test_choice_chip_clamps_max_w();
  test_choice_chip_a11y_off_on();
  test_choice_chip_a11y_distinct();
  test_choice_chip_wrong_value_is_off();
  test_choice_chip_tap_writes_value();
  test_choice_chip_miss();
  test_choice_chip_hit_test();
  test_choice_chip_paint_off_on();
  test_choice_chip_group_exclusive();
  test_choice_chip_in_taps_dump();
  test_action_chip_sizes();
  test_action_chip_empty_min_width();
  test_action_chip_null_label();
  test_action_chip_clamps_max_w();
  test_action_chip_does_not_wrap();
  test_action_chip_a11y();
  test_action_chip_a11y_distinct();
  test_action_chip_tap();
  test_action_chip_tap_twice();
  test_action_chip_null_tap();
  test_action_chip_miss();
  test_action_chip_hit_test();
  test_action_chip_paint();
  test_action_chip_paint_not_primary();
  test_action_chip_in_taps_dump();
  test_input_chip_sizes();
  test_input_chip_empty_min_width();
  test_input_chip_null_label();
  test_input_chip_clamps_max_w();
  test_input_chip_a11y_off_on();
  test_input_chip_a11y_distinct();
  test_input_chip_nonzero_is_on();
  test_input_chip_tap_toggles();
  test_input_chip_miss();
  test_input_chip_hit_test();
  test_input_chip_paint_off_on();
  test_input_chip_paint_mark_on();
  test_input_chip_in_taps_dump();
  test_radio_sizes();
  test_radio_a11y_off_on();
  test_radio_tap_writes_value();
  test_radio_group_exclusive();
  test_radio_hit_test();
  test_radio_paint_off_on();
  test_radio_in_taps_dump();
  test_slider_sizes();
  test_slider_a11y();
  test_slider_clamps_a11y();
  test_slider_tap_sets_from_x();
  test_slider_tap_clamps_edges();
  test_slider_hit_test();
  test_slider_paint_fill();
  test_slider_in_taps_dump();
  test_slider_pointer_drag();
  test_replace_root_drops_pointer();
  test_slider_live_records_xy();
  test_progress_sizes();
  test_progress_unbounded_width();
  test_progress_a11y();
  test_progress_clamps_a11y();
  test_progress_not_tap_target();
  test_progress_paint_fill();
  test_progress_not_in_taps_dump();
  test_ignore_pointer_sizes_to_child();
  test_ignore_pointer_passes_tap_through();
  test_absorb_pointer_blocks_tap();
  test_stack_front_button_wins_without_ignore();
  test_ignore_pointer_skips_scroll_at();
  test_absorb_pointer_skips_scroll_at();
  test_exclude_semantics_sizes_to_child();
  test_exclude_semantics_hides_from_dump();
  test_exclude_semantics_hides_nested();
  test_exclude_semantics_hides_bind_text();
  test_exclude_semantics_still_tappable();
  test_exclude_semantics_keeps_scroll_at();
  test_exclude_semantics_skips_field_collect();
  test_exclude_semantics_sibling_button_stays();
  test_button_does_not_wrap();
  test_text_blank_line_from_newline();
  test_a11y();
  test_a11y_dump_grows_past_4k();
  test_clear_children();
  test_view_each();
  test_view_each_setlist_rebuilds();
  test_view_each_map_text();
  test_view_each_map_button();
  test_each_expanded_row_in_scroll();
  test_signal_list_spine_collect();
  test_property_signal_list_len();
  test_property_signal_list_at();
  test_property_signal_int();
  test_signal_list_record_dump();
  test_property_signal_str();
  test_signal_dump_escapes();
  test_signal_name_last_wins();
  test_property_replay_str_list();
  test_text_field_edit();
  test_view_editor();
  test_view_editor_viewport();
  test_view_editor_undo_gutter();
  test_view_focus_split_overlay();
  test_view_focus_group_keys();
  test_app_chord_save();
  test_app_chord_palette();
  test_caret_metrics();
  test_alloc_pump_flat();
  test_alloc_counter_pump_flat();
  test_alloc_each_pump_flat();
  test_tap_env_retain_release();
  test_each_env_retain_release();
  test_quiesce();
  puts("runtime ui tests ok");
  return 0;
}
