#define _POSIX_C_SOURCE 200112L
#include "scuzz_ui.h"
#include "sk_capi.h"

#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int sz_view_paint(SzView *root, SkCanvas *canvas, int width, int height,
                  const SzTheme *theme);
int sz_view_handle_tap(SzView *root, float x, float y);

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
  WatchRebuildEnv env;
  SzView *root;
  SzUiSession *session;
  SzInputEvent tap;
  SzString *dump1, *dump2, *a11y;
  SzView *same;
  const char *stamp = "/tmp/scuzz_ui_reload.stamp";

  write_stamp(stamp, "n=");
  env.count = sz_signal_int(0);
  env.path = stamp;
  env.btn = NULL;
  root = watch_rebuild(&env);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  sz_ui_session_set_rebuild(session, watch_rebuild, &env);
  assert(sz_ui_session_watch(session, stamp));
  assert(sz_ui_pump_sync(session));
  same = sz_ui_session_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_root(session) == same);

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(env.btn).x + 8.f;
  tap.y = sz_view_frame(env.btn).y + 8.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(env.count) == 1);
  dump1 = sz_signal_dump();

  write_stamp(stamp, "v=");
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_root(session) != same);
  assert(sz_signal_int_get(env.count) == 1);
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
  tap.x = sz_view_frame(env.btn).x + 8.f;
  tap.y = sz_view_frame(env.btn).y + 8.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(env.count) == 2);

  sz_ui_unmount(session);
  sz_signal_int_free(env.count);
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
  KeepEnv env;
  pthread_t th;
  SzIoResult r;
  const char *stamp = "/tmp/scuzz_ui_keepalive.stamp";
  const char *dump = "/tmp/scuzz_ui_keepalive.dump";
  FILE *f;
  char buf[2048];
  size_t n;

  env.count = sz_signal_int(3);
  env.calls = 0;
  write_stamp(stamp, "0");
  remove(dump);
  setenv("SCUZZ_UI_RELOAD_STAMP", stamp, 1);
  setenv("SCUZZ_UI_DEBUG_DUMP", dump, 1);
  setenv("SCUZZ_LIVE_FRAMES", "8", 1);
  assert(pthread_create(&th, NULL, stamp_bump, (void *)stamp) == 0);
  r = sz_io_unsafe_run(sz_ui_run_rebuild(keep_factory, &env));
  pthread_join(th, NULL);
  unsetenv("SCUZZ_UI_RELOAD_STAMP");
  unsetenv("SCUZZ_UI_DEBUG_DUMP");
  unsetenv("SCUZZ_LIVE_FRAMES");
  assert(r.ok);
  assert(sz_signal_int_get(env.count) == 3);
  assert(env.calls >= 2);
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
  sz_signal_int_free(env.count);
  remove(stamp);
  remove(dump);
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
  assert(sz_ui_pump_sync(session));
  a = slurp_cstr(path);
  assert(strstr(a, "[signals]") != NULL);
  assert(strstr(a, "[views]") != NULL);
  assert(strstr(a, "text:Debug") != NULL);
  assert(strstr(a, "[taps]") != NULL);
  assert(strstr(a, "0 +") != NULL);
  assert(strstr(a, "1 -") != NULL);
  assert(strstr(a, "[fields]") != NULL);
  assert(strstr(a, "0* item=\"\"") != NULL);
  assert(strstr(a, "1 search=\"\"") != NULL);
  assert(strstr(a, "1* search") == NULL);
  assert(strstr(a, "[scrolls]") != NULL);

  {
    SzInputEvent text;
    memset(&text, 0, sizeof(text));
    text.kind = SZ_INPUT_TEXT;
    text.text = "hi";
    assert(sz_ui_inject_sync(session, &text));
    assert(sz_ui_pump_sync(session));
    free(a);
    a = slurp_cstr(path);
    assert(strstr(a, "0* item=\"hi\"") != NULL);
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
  free(b);
  free(c);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  sz_signal_str_free(draft);
  sz_signal_str_free(query);
  remove(path);
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
  sz_view_add_child(row, scroll);
  sz_view_add_child(row, scroll2);
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

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
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

static int px_rgb(const uint8_t *px, int w, int x, int y, uint8_t r, uint8_t g,
                  uint8_t b) {
  const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
  return p[0] == r && p[1] == g && p[2] == b;
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
  for (x = x0; x < x1; x++) {
    const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
    if (p[0] > 180 && p[1] < 80 && p[2] < 80)
      return 1;
  }
  return 0;
}

static int row_has_blue(const uint8_t *px, int w, int y, int x0, int x1) {
  int x;
  for (x = x0; x < x1; x++) {
    const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
    if (p[2] > 180 && p[0] < 80 && p[1] < 80)
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
  sz_view_layout(list, 200.f, 120.f, theme);
  assert(strstr(sz_string_cstr(sz_view_a11y_dump(list)), "text:- eggs") != NULL);
  assert(strstr(sz_string_cstr(sz_view_a11y_dump(list)), "text:- milk") != NULL);

  sz_view_free(list);
  sz_signal_list_free(items);
}

static void test_law_signal_list_len(void) {
  SzSignalList *items;
  SzList *xs;
  SzString *dump;
  const char *s;
  int id;

  xs = sz_list_cons(sz_string_from_cstr("a"),
                    sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
  items = sz_signal_list(xs);
  dump = sz_signal_dump();
  s = strstr(sz_string_cstr(dump), "list[");
  assert(s);
  id = atoi(s + 5);
  assert(sz_law_signal_list_len(id) == 2);
  sz_signal_list_set(items, sz_list_nil());
  assert(sz_law_signal_list_len(id) == 0);
  assert(sz_law_signal_list_len(99999) == 0);
  sz_string_free(dump);
  sz_signal_list_free(items);
}

static void test_law_signal_list_at(void) {
  SzSignalList *items;
  SzList *xs;
  SzString *got;
  SzString *dump;
  const char *s;
  int id;

  xs = sz_list_cons(sz_string_from_cstr("a"),
                    sz_list_cons(sz_string_from_cstr("b"), sz_list_nil()));
  items = sz_signal_list(xs);
  dump = sz_signal_dump();
  s = strstr(sz_string_cstr(dump), "list[");
  assert(s);
  id = atoi(s + 5);
  got = sz_law_signal_list_at(id, 0);
  assert(strcmp(sz_string_cstr(got), "a") == 0);
  sz_string_free(got);
  got = sz_law_signal_list_at(id, 1);
  assert(strcmp(sz_string_cstr(got), "b") == 0);
  sz_string_free(got);
  got = sz_law_signal_list_at(id, 2);
  assert(strcmp(sz_string_cstr(got), "") == 0);
  sz_string_free(got);
  got = sz_law_signal_list_at(id, -1);
  assert(strcmp(sz_string_cstr(got), "") == 0);
  sz_string_free(got);
  got = sz_law_signal_list_at(99999, 0);
  assert(strcmp(sz_string_cstr(got), "") == 0);
  sz_string_free(got);
  sz_string_free(dump);
  sz_signal_list_free(items);
}

static void test_law_signal_str(void) {
  SzSignalStr *draft;
  SzString *got;
  SzString *dump;
  const char *s;
  int id;

  draft = sz_signal_str("milk");
  dump = sz_signal_dump();
  s = strstr(sz_string_cstr(dump), "str[");
  assert(s);
  id = atoi(s + 4);
  got = sz_law_signal_str(id);
  assert(strcmp(sz_string_cstr(got), "milk") == 0);
  sz_string_free(got);
  sz_signal_str_set(draft, "oat");
  got = sz_law_signal_str(id);
  assert(strcmp(sz_string_cstr(got), "oat") == 0);
  sz_string_free(got);
  got = sz_law_signal_str(99999);
  assert(strcmp(sz_string_cstr(got), "") == 0);
  sz_string_free(got);
  sz_string_free(dump);
  sz_signal_str_free(draft);
}

static void test_signal_list_spine_collect(void) {
  SzSignalList *items;
  SzList *xs;
  SzString *first;
  size_t origin_count = 0, origin_bytes = 0;
  size_t base_count = 0, base_bytes = 0;
  size_t live_count = 0, live_bytes = 0;
  int i;

  sz_alloc_stats(&origin_bytes, &origin_count);
  first = sz_string_from_cstr("a");
  xs = sz_list_cons(first, sz_list_nil());
  items = sz_signal_list(xs);
  sz_alloc_stats(&base_bytes, &base_count);
  /* Append copies the spine; set frees the unshared previous spine. */
  for (i = 0; i < 30; i++) {
    xs = sz_list_append(sz_signal_list_get(items), sz_string_from_cstr("x"));
    sz_signal_list_set(items, xs);
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
  size_t base_count = 0, base_bytes = 0;
  size_t live_count = 0, live_bytes = 0;
  size_t max_count = 0;
  int i;

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 120;
  cfg.scale = 1.0;

  items = sz_signal_list(
      sz_list_cons(sz_string_from_cstr("milk"), sz_list_nil()));
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
  sz_signal_str_set(draft, "ii");
  sz_view_layout(root, 200.f, 80.f, theme);
  caret = sz_view_caret_rect(root, theme);
  want = fr.x + 6.f + sk_font_measure_string("ii", theme->font_px);
  assert(fabsf(caret.x - want) < 0.5f);
  sz_signal_str_set(draft, "WW");
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
  label = sz_lang_signal_map(count, map_count_label, NULL);
  root = sz_view_column();
  sz_view_add_child(root, sz_lang_view_bind_text(label));
  btn = sz_view_button("+", counter_tap, count);
  sz_view_add_child(root, btn);

  session = sz_ui_mount(&cfg, root);
  assert(session);
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
  test_session_inject_script();
  test_session_inject_scroll();
  test_session_inject_backspace();
  test_session_inject_type();
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
  test_clear_children();
  test_view_each();
  test_signal_list_spine_collect();
  test_law_signal_list_len();
  test_law_signal_list_at();
  test_law_signal_str();
  test_text_field_edit();
  test_caret_metrics();
  test_alloc_pump_flat();
  test_alloc_counter_pump_flat();
  test_alloc_each_pump_flat();
  puts("runtime ui tests ok");
  return 0;
}
