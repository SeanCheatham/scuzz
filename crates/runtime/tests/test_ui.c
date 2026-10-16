#define _POSIX_C_SOURCE 200809L
#include "scuzz_ui.h"
#include "../src/text.h"
#include "sk_capi.h"
#include "../src/ui_script.h"

#include <assert.h>
#include <stdatomic.h>
#include <math.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const int32_t sz_eval_value_tag_base = 42;

int sz_view_paint(SzView *root, SkCanvas *canvas, int width, int height,
                  const SzTheme *theme);
int sz_ui_session_live_inject(SzUiSession *session, const SzInputEvent *event);

static SzView *image_fixture(int width, int height, uint32_t color, const char *label) {
  SkSurface *surface = sk_surface_make_raster_n32_premul(width, height);
  assert(surface);
  sk_canvas_clear(sk_surface_get_canvas(surface), sk_color_argb(color));
  uint8_t *bytes = NULL;
  size_t len = 0;
  assert(sk_encode_png(surface, &bytes, &len));
  const char *error = NULL;
  SzImage *image = sz_image_decode(bytes, len, &error);
  assert(image && !error);
  SzView *view = sz_view_image(image, label);
  sz_image_free(image);
  free(bytes);
  sk_surface_unref(surface);
  return view;
}

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

static void test_script_scroll_targets_outer_container(void) {
  SzUiConfig cfg = {0};
  SzView *content = sz_view_column();
  SzView *inner = sz_view_scroll_h(sz_view_sized(400, 40, sz_view_text("wide")));
  SzView *outer;
  SzView *root;
  SzUiSession *session;
  const char *outer_script =
      "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"scroll\",\"i\":0,\"dy\":30}]}";
  const char *inner_script =
      "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"scroll\",\"i\":1,\"dy\":15}]}";
  sz_view_add_child(content, inner);
  sz_view_add_child(content, sz_view_sized(80, 300, sz_view_text("tall")));
  outer = sz_view_scroll(content);
  root = sz_view_sized(160, 120, outer);
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 160;
  cfg.height = 120;
  cfg.scale = 1.f;
  session = sz_ui_mount(&cfg, root);
  assert(session && sz_ui_pump_sync(session));
  assert(sz_view_scroll_at(root, 80.f, 60.f) == inner);
  sz_ui_script_play_json(session, outer_script);
  assert(sz_view_scroll_y(outer) == 30.f);
  assert(sz_view_scroll_x(inner) == 0.f);
  sz_ui_script_play_json(session, inner_script);
  assert(sz_view_scroll_y(outer) == 30.f);
  assert(sz_view_scroll_x(inner) == 15.f);
  sz_ui_unmount(session);
  sz_view_free(root);
}

static void test_nested_scroll_wheel_bubbles(void) {
  SzUiConfig cfg = {0};
  SzView *content = sz_view_column();
  SzView *inner = sz_view_scroll(sz_view_text("short"));
  SzView *outer;
  SzView *root;
  SzUiSession *session;
  SzInputEvent ev;
  SzRect fr;

  sz_view_add_child(content, sz_view_sized(160, 80, inner));
  sz_view_add_child(content, sz_view_sized(80, 300, sz_view_text("tall")));
  outer = sz_view_scroll(content);
  root = sz_view_sized(160, 120, outer);
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 160;
  cfg.height = 120;
  cfg.scale = 1.f;
  session = sz_ui_mount(&cfg, root);
  assert(session && sz_ui_pump_sync(session));
  fr = sz_view_frame(inner);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_SCROLL;
  ev.x = fr.x + 8.f;
  ev.y = fr.y + 8.f;
  ev.dy = 30.f;
  assert(sz_view_scroll_at(root, ev.x, ev.y) == inner);
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_view_scroll_y(inner) == 0.f);
  assert(sz_view_scroll_y(outer) == 30.f);
  sz_ui_unmount(session);
  sz_view_free(root);
}

static void test_code_block_wheel_pans_x(void) {
  SzUiConfig cfg = {0};
  SzView *code = sz_view_code(
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
  SzView *root = sz_view_sized(140, 80, code);
  SzView *scrolls[8];
  SzUiSession *session;
  SzInputEvent ev;
  SzRect fr;
  int n;

  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 140;
  cfg.height = 80;
  cfg.scale = 1.f;
  session = sz_ui_mount(&cfg, root);
  assert(session && sz_ui_pump_sync(session));
  n = sz_view_collect_scrolls(root, scrolls, 8);
  assert(n == 1);
  assert(sz_view_scroll_is_h(scrolls[0]));
  fr = sz_view_frame(scrolls[0]);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_SCROLL;
  ev.x = fr.x + 8.f;
  ev.y = fr.y + 8.f;
  ev.dy = 24.f;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_view_scroll_x(scrolls[0]) == 24.f);
  sz_ui_unmount(session);
  sz_view_free(root);
}

static double color_luminance(uint32_t color) {
  double channels[3];
  for (int i = 0; i < 3; i++) {
    double c = ((color >> (16 - i * 8)) & 255) / 255.0;
    channels[i] = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
  }
  return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2];
}

static double color_contrast(uint32_t a, uint32_t b) {
  double first = color_luminance(a), second = color_luminance(b);
  return (fmax(first, second) + 0.05) / (fmin(first, second) + 0.05);
}

static void test_default_theme_contrast(void) {
  const SzTheme *theme = sz_theme_default();
  const uint32_t papers[] = {theme->background, theme->surface};
  const uint32_t texts[] = {theme->foreground, theme->muted, theme->accent};
  /* WCAG 2.2: normal text 4.5:1; essential control graphics 3:1. */
  assert(fabs(color_contrast(0xFF000000, 0xFFFFFFFF) - 21.0) < 0.0001);
  assert(color_contrast(theme->surface, theme->surface) == 1.0);
  for (size_t p = 0; p < sizeof papers / sizeof papers[0]; p++) {
    for (size_t t = 0; t < sizeof texts / sizeof texts[0]; t++)
      assert(color_contrast(texts[t], papers[p]) >= 4.5);
    assert(color_contrast(theme->border, papers[p]) >= 3.0);
    assert(color_contrast(theme->muted, papers[p]) >= 3.0);
  }
  assert(color_contrast(theme->on_primary, theme->primary) >= 4.5);
  assert(color_contrast(theme->foreground, theme->selection) >= 4.5);
  assert(color_contrast(theme->border, theme->primary) >= 3.0);
  assert(color_contrast(theme->foreground, theme->surface) >= 3.0);
}

static void test_button_press_feedback(void) {
  for (int scale = 1; scale <= 2; scale++) {
    SzUiConfig cfg = {0};
    SzInputEvent ev = {0};
    SzSignalInt *count = sz_signal_int(0);
    SzView *button = sz_view_button("Save", counter_tap, count);
    SzUiSession *session;
    SzRect before, after;
    const char *idle = "/tmp/scuzz_button_idle.png";
    const char *down = "/tmp/scuzz_button_down.png";
    const char *cancel = "/tmp/scuzz_button_cancel.png";
    cfg.kind = SZ_UI_RUNTIME_HEADLESS;
    cfg.width = 120;
    cfg.height = 60;
    cfg.scale = scale;
    session = sz_ui_mount(&cfg, button);
    assert(session && sz_ui_pump_sync(session));
    before = sz_view_frame(button);
    assert(sz_ui_snapshot_png_sync(session, idle));
    ev.kind = SZ_INPUT_POINTER;
    ev.pointer_phase = SZ_POINTER_DOWN;
    ev.x = before.x + 10.f;
    ev.y = before.y + 10.f;
    assert(sz_ui_inject_sync(session, &ev));
    assert(sz_ui_pump_sync(session));
    assert(sz_ui_snapshot_png_sync(session, down));
    assert(!files_equal(idle, down));
    assert(sz_signal_int_get(count) == 0);
    after = sz_view_frame(button);
    assert(before.x == after.x && before.y == after.y &&
           before.w == after.w && before.h == after.h);
    ev.pointer_phase = SZ_POINTER_MOVE;
    ev.x = 110.f;
    ev.y = 50.f;
    assert(sz_ui_inject_sync(session, &ev));
    assert(sz_ui_pump_sync(session));
    assert(sz_ui_snapshot_png_sync(session, cancel));
    assert(files_equal(idle, cancel));
    ev.pointer_phase = SZ_POINTER_UP;
    assert(sz_ui_inject_sync(session, &ev));
    assert(sz_signal_int_get(count) == 0);
    ev.pointer_phase = SZ_POINTER_DOWN;
    ev.x = before.x + 10.f;
    ev.y = before.y + 10.f;
    assert(sz_ui_inject_sync(session, &ev));
    ev.pointer_phase = SZ_POINTER_UP;
    assert(sz_ui_inject_sync(session, &ev));
    assert(sz_signal_int_get(count) == 1);
    sz_ui_unmount(session);
    sz_view_free(button);
    sz_signal_int_free(count);
    remove(idle);
    remove(down);
    remove(cancel);
  }
}

static void test_session_snapshot(void) {
  SzUiConfig cfg = {0};
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
  SzUiConfig cfg = {0};
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
  SzUiConfig cfg = {0};
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
  dump1 = sz_signal_dump_json_string();

  root2 = sz_view_column();
  sz_view_add_child(root2, sz_view_text_signal_int(count, "v="));
  btn2 = sz_view_button("+", counter_tap, count);
  sz_view_add_child(root2, btn2);
  assert(sz_ui_session_replace_root(session, root2));
  assert(sz_ui_session_root(session) == root2);
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 1);
  dump2 = sz_signal_dump_json_string();
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
  SzUiConfig cfg = {0};
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
  sz_ui_session_set_rebuild(session, watch_rebuild, env, NULL);
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
  dump1 = sz_signal_dump_json_string();

  write_stamp(stamp, "v=");
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_root(session) != same);
  assert(sz_signal_int_get(env->count) == 1);
  dump2 = sz_signal_dump_json_string();
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
  SzIoResult r = sz_io_unsafe_run(sz_ui_run_rebuild(run_rebuild_factory, count, NULL));
  assert(r.ok);
  assert(sz_signal_int_get(count) == 7);
  sz_signal_int_free(count);
  {
    size_t base_bytes = 0, base_count = 0;
    size_t live_bytes = 0, live_count = 0;
    sz_alloc_stats(&base_bytes, &base_count);
    count = sz_signal_int(1);
    {
      SzIo *io = sz_ui_run_rebuild(run_rebuild_factory, count, NULL);
      sz_release(io);
    }
    sz_signal_int_free(count);
    sz_alloc_stats(&live_bytes, &live_count);
    assert(live_count == base_count);
    assert(live_bytes == base_bytes);
  }
}

typedef struct { int started; int cancelled; int progressed; } AsyncUiEnv;
static void *async_ui_start(void *value) {
  ((AsyncUiEnv *)value)->started++;
  return NULL;
}
static void *async_ui_cancelled(void *value) {
  ((AsyncUiEnv *)value)->cancelled++;
  return NULL;
}
static void *async_ui_progress(void *value) {
  ((AsyncUiEnv *)value)->progressed++;
  return NULL;
}
static void async_ui_tap(SzView *view, void *value) {
  (void)view;
  SzIo *sleep = sz_io_sleep_ms(1000);
  SzIo *start = sz_io_delay(async_ui_start, value);
  SzIo *inner = sz_io_both(start, sleep);
  SzIo *finish = sz_io_delay(async_ui_cancelled, value);
  SzIo *io = sz_io_ensure(inner, finish);
  sz_io_submit_ui(io);
  sz_release(sleep); sz_release(start); sz_release(inner);
  sz_release(finish); sz_release(io);
}
static SzView *async_ui_factory(void *value) {
  return sz_view_button("async", async_ui_tap, value);
}
static void test_ui_async_handlers(void) {
  AsyncUiEnv *env = sz_rc_alloc(sizeof *env, SZ_RC_BOX);
  memset(env, 0, sizeof *env);
  setenv("SCUZZ_UI_TAP", "1", 1);
  setenv("SCUZZ_UI_RELOAD_STAMP", "/tmp/scuzz-no-async-reload.stamp", 1);
  setenv("SCUZZ_LIVE_FRAMES", "3", 1);
  SzIo *ui = sz_ui_run_rebuild(async_ui_factory, env, NULL);
  SzIo *progress = sz_io_delay(async_ui_progress, env);
  SzIo *both = sz_io_both(ui, progress);
  sz_release(ui); sz_release(progress);
  SzIoResult result = sz_io_unsafe_run(both);
  assert(result.ok);
  assert(env->started == 1 && env->progressed == 1 && env->cancelled == 1);
  sz_release(result.value);
  sz_release(env);
  unsetenv("SCUZZ_UI_TAP"); unsetenv("SCUZZ_UI_RELOAD_STAMP");
  unsetenv("SCUZZ_LIVE_FRAMES");
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
  r = sz_io_unsafe_run(sz_ui_run_rebuild(keep_factory, env, NULL));
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
  assert(strstr(buf, "\"signals\":[") != NULL);
  assert(strstr(buf, "\"views\":[") != NULL);
  assert(strstr(buf, "\"taps\":[") != NULL);
  assert(strstr(buf, "\"fields\":[") != NULL);
  assert(strstr(buf, "\"scrolls\":[") != NULL);
  assert(strstr(buf, "\"session\":{") != NULL);
  assert(strstr(buf, "\"heap\":{") != NULL);
  assert(strstr(buf, "\"live_bytes\":") != NULL);
  assert(strstr(buf, "\"live\":[") != NULL);
  sz_signal_int_free(env->count);
  sz_free(env);
  remove(stamp);
  remove(dump);
}
/* --- typed session schema v=2 (JSON dump) ---------------------------------- */

static SzAdt *json_doc_key(SzAdt *j, const char *key) {
  SzString *k = sz_string_from_cstr(key);
  SzList *got = sz_json_get(j, k);
  SzAdt *out = sz_list_is_empty(got) ? NULL : (SzAdt *)sz_list_head(got);
  sz_release(got);
  sz_release(k);
  return out;
}

static int64_t json_doc_int(SzAdt *j, const char *key, int64_t d) {
  SzString *k = sz_string_from_cstr(key);
  int64_t out = sz_json_get_int(j, k, d);
  sz_release(k);
  return out;
}

static int json_str_eq(SzAdt *j, const char *key, const char *want) {
  SzString *k = sz_string_from_cstr(key);
  SzString *d = sz_string_from_cstr("");
  SzString *got = sz_json_get_str(j, k, d);
  int out = strcmp(sz_string_cstr(got), want) == 0;
  sz_release(got);
  sz_release(d);
  sz_release(k);
  return out;
}

static SzAdt *json_arr_head(SzAdt *j, SzList **hold) {
  *hold = sz_json_arr(j);
  if (sz_list_is_empty(*hold))
    return NULL;
  return (SzAdt *)sz_list_head(*hold);
}

/* First array entry with `key` equal to `want`, else NULL. `hold` keeps the
 * array list alive for the returned borrow. */
static SzAdt *json_arr_find_int(SzAdt *j, const char *key, int64_t want,
                                SzList **hold) {
  SzList *xs;
  SzAdt *out = NULL;
  *hold = sz_json_arr(j);
  for (xs = *hold; xs && !sz_list_is_empty(xs); xs = sz_list_tail(xs)) {
    SzAdt *ent = (SzAdt *)sz_list_head(xs);
    if (json_doc_int(ent, key, want + 1) == want) {
      out = ent;
      break;
    }
  }
  return out;
}

/* First array entry with `key` equal to `want`, else NULL. `hold` keeps the
 * array list alive for the returned borrow. */
static SzAdt *json_arr_find_str(SzAdt *j, const char *key, const char *want,
                                SzList **hold) {
  SzList *xs;
  SzAdt *out = NULL;
  *hold = sz_json_arr(j);
  for (xs = *hold; xs && !sz_list_is_empty(xs); xs = sz_list_tail(xs)) {
    SzAdt *ent = (SzAdt *)sz_list_head(xs);
    if (json_str_eq(ent, key, want)) {
      out = ent;
      break;
    }
  }
  return out;
}

static void test_dump_json_schema(void) {
  SzUiConfig cfg = {0};
  SzSignalInt *count;
  SzSignalInt *flag;
  SzSignalStr *text;
  SzSignal *rec;
  SzSignal *ints;
  SzAdt *rec_adt;
  SzList *int_list;
  SzView *root;
  SzView *card;
  SzUiSession *session;
  const char *path = "/tmp/scuzz_ui_schema.json";
  const char *plain_path = "/tmp/scuzz_ui_schema_plain.dump";
  char buf[16384];
  size_t n;
  FILE *f;
  SzString *doc;
  SzAdt *parsed, *json, *signals, *sig0, *taps, *tap0, *fields, *field0,
      *views, *a11y, *card_j, *kids, *node, *session_j, *heap, *kinds, *live;
  SzList *arr, *hold;

  count = sz_signal_int(410001);
  text = sz_signal_str("abc");
  flag = sz_signal_int(1);
  /* Schema v=2: a value cell and a non-string list cell get typed payloads. */
  {
    void *box = sz_box_i64(3);
    rec_adt = sz_adt_new(7, box);
    sz_release(box);
  }
  rec = sz_signal_new(rec_adt, 4, NULL);
  sz_release(rec_adt);
  sz_signal_name(rec, "rec");
  {
    void *b1 = sz_box_i64(1);
    void *b2 = sz_box_i64(2);
    SzList *tail = sz_list_cons(b2, sz_list_nil());
    int_list = sz_list_cons(b1, tail);
    sz_release(b1);
    sz_release(b2);
    sz_release(tail);
  }
  ints = sz_signal_new(int_list, 5, NULL);
  sz_release(int_list);
  sz_signal_name(ints, "ints");
  root = sz_view_column();
  card = sz_view_card(sz_view_button("+", counter_tap, count));
  sz_view_add_child(card, sz_view_checkbox(flag, "done"));
  sz_view_add_child(root, card);
  sz_view_add_child(root, sz_view_text_field(text, "name"));

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_set_debug_dump(session, path));
  remove(path);
  assert(sz_ui_session_dump_now(session));

  f = fopen(path, "r");
  assert(f);
  n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';
  doc = sz_string_from_cstr(buf);
  parsed = sz_json_parse(doc);
  sz_release(doc);
  assert(sz_adt_tag(parsed) == 1);
  json = (SzAdt *)sz_adt_payload(parsed);

  assert(json_doc_int(json, "v", 0) == 2);
  assert(json_str_eq(json, "kind", "dump"));

  signals = json_doc_key(json, "signals");
  assert(signals && sz_json_is_arr(signals) == 1);
  arr = sz_json_arr(signals);
  assert(sz_list_len(arr) >= 2);
  sz_release(arr);
  sig0 = json_arr_find_int(signals, "value", 410001, &hold);
  assert(sig0);
  assert(json_doc_int(sig0, "id", -1) >= 0);
  assert(json_str_eq(sig0, "type", "int"));
  sz_release(hold);

  /* v=2 typed payloads: an ADT value cell and a non-string list cell. */
  sig0 = json_arr_find_str(signals, "name", "rec", &hold);
  assert(sig0);
  assert(json_str_eq(sig0, "type", "value"));
  node = json_doc_key(sig0, "value");
  assert(node && sz_json_is_obj(node) == 1);
  assert(json_doc_int(node, "tag", -1) == 7);
  assert(json_doc_int(node, "payload", -1) == 3);
  sz_release(hold);
  sig0 = json_arr_find_str(signals, "name", "ints", &hold);
  assert(sig0);
  assert(json_str_eq(sig0, "type", "list"));
  node = json_doc_key(sig0, "value");
  assert(node && sz_json_is_arr(node) == 1);
  arr = sz_json_arr(node);
  assert(sz_list_len(arr) == 2);
  assert(sz_json_int_or((SzAdt *)sz_list_head(arr), -1) == 1);
  sz_release(arr);
  sz_release(hold);

  views = json_doc_key(json, "views");
  assert(views && sz_json_is_arr(views) == 1);
  arr = sz_json_arr(views);
  assert(sz_list_len(arr) >= 1);
  sz_release(arr);

  /* v=2 a11y forest: the card nests the button and the checkbox. The
   * column root has no role, so its children hoist to the top level. */
  a11y = json_doc_key(json, "a11y");
  assert(a11y && sz_json_is_arr(a11y) == 1);
  arr = sz_json_arr(a11y);
  assert(sz_list_len(arr) == 2);
  sz_release(arr);
  card_j = json_arr_find_str(a11y, "role", "card", &hold);
  assert(card_j);
  assert(json_str_eq(card_j, "label", "card"));
  kids = json_doc_key(card_j, "children");
  assert(kids && sz_json_is_arr(kids) == 1);
  arr = sz_json_arr(kids);
  assert(sz_list_len(arr) == 2);
  sz_release(arr);
  node = json_arr_find_str(kids, "role", "button", &arr);
  assert(node);
  assert(json_str_eq(node, "label", "+"));
  {
    SzString *k = sz_string_from_cstr("children");
    assert(sz_json_has(node, k) == 0);
    sz_release(k);
  }
  sz_release(arr);
  node = json_arr_find_str(kids, "role", "checkbox", &arr);
  assert(node);
  assert(json_str_eq(node, "label", "done"));
  {
    SzString *k = sz_string_from_cstr("on");
    assert(sz_json_get_bool(node, k, 0) == 1);
    sz_release(k);
  }
  sz_release(arr);
  sz_release(hold);
  node = json_arr_find_str(a11y, "role", "textfield", &hold);
  assert(node);
  assert(json_str_eq(node, "label", "name"));
  sz_release(hold);

  taps = json_doc_key(json, "taps");
  assert(taps && sz_json_is_arr(taps) == 1);
  arr = sz_json_arr(taps);
  assert(sz_list_len(arr) == 2);
  sz_release(arr);
  tap0 = json_arr_head(taps, &hold);
  assert(tap0 && json_str_eq(tap0, "label", "+"));
  sz_release(hold);

  fields = json_doc_key(json, "fields");
  assert(fields && sz_json_is_arr(fields) == 1);
  arr = sz_json_arr(fields);
  assert(sz_list_len(arr) == 1);
  sz_release(arr);
  field0 = json_arr_head(fields, &hold);
  assert(field0 && json_str_eq(field0, "value", "abc"));
  assert(json_doc_int(field0, "caret", -1) == 3);
  sz_release(hold);

  session_j = json_doc_key(json, "session");
  assert(session_j);
  assert(json_str_eq(session_j, "runtime", "headless"));
  assert(json_doc_int(session_j, "width", 0) == 200);
  assert(json_doc_int(session_j, "pumps", 0) >= 1);

  heap = json_doc_key(json, "heap");
  assert(heap);
  assert(json_doc_int(heap, "live_bytes", 0) > 0);
  kinds = json_doc_key(heap, "kinds");
  assert(kinds && sz_json_is_arr(kinds) == 1);
  arr = sz_json_arr(kinds);
  assert(sz_list_len(arr) == SZ_RC_KIND_COUNT);
  sz_release(arr);

  live = json_doc_key(json, "live");
  assert(live && sz_json_is_arr(live) == 1);

  sz_release(parsed);

  /* A non-`.json` path writes the same schema: no suffix routing. */
  remove(plain_path);
  assert(sz_ui_session_write_dump(session, plain_path));
  f = fopen(plain_path, "r");
  assert(f);
  n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';
  assert(strstr(buf, "\"taps\":[") != NULL);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  sz_signal_int_free(flag);
  sz_signal_str_free(text);
  sz_signal_free(rec);
  sz_signal_free(ints);
  remove(path);
  remove(plain_path);
}

/* --- typed session schema v=1 (JSON inject + record) ---------------------- */

static char *slurp_cstr(const char *path);

static SzAdt *json_slurp_parse(const char *path) {
  char *body = slurp_cstr(path);
  SzString *doc = sz_string_from_cstr(body);
  SzAdt *parsed = sz_json_parse(doc);
  free(body);
  sz_release(doc);
  assert(parsed && sz_adt_tag(parsed) == 1);
  return parsed;
}

static void test_script_json_inject(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root;
  SzSignalInt *count;
  SzSignalStr *draft;
  const char *path = "/tmp/scuzz_ui_inject_schema.json";

  count = sz_signal_int(0);
  draft = sz_signal_str("");
  root = sz_view_column();
  sz_view_add_child(root, sz_view_button("+1", counter_tap, count));
  sz_view_add_child(root, sz_view_text_field(draft, "item"));

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));

  sz_ui_script_play_json(session,
                         "{\"v\":1,\"kind\":\"inject\",\"events\":["
                         "{\"op\":\"tap\",\"i\":0},"
                         "{\"op\":\"text\",\"value\":\"hi\"},"
                         "{\"op\":\"type\",\"value\":\"!\"},"
                         "{\"op\":\"key\",\"key\":\"Backspace\"}]}");
  assert(sz_signal_int_get(count) == 1);
  assert(strcmp(sz_signal_str_get(draft), "hi") == 0);

  /* A script path that ends in `.json` routes to the schema. */
  remove(path);
  write_stamp(
      path,
      "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"tap\",\"i\":0}]}");
  sz_ui_script_run_file(session, path);
  assert(sz_signal_int_get(count) == 2);

  /* A `.json` watch inject plays the whole document on change. */
  assert(sz_ui_session_set_inject(session, path));
  write_stamp(path,
              "{\"v\":1,\"kind\":\"inject\",\"events\":["
              "{\"op\":\"tap\",\"i\":0},{\"op\":\"tap\",\"i\":0}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 4);
  write_stamp(path,
              "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"tap\",\"i\":0}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 5);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  sz_signal_str_free(draft);
  remove(path);
}

static void test_script_tap_named_id(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root;
  SzSignalInt *count;
  SzSignalInt *decoy;
  char id[160];

  decoy = sz_signal_int(0);
  count = sz_signal_int(0);
  root = sz_view_column();
  for (int i = 0; i < 70; i++)
    sz_view_add_child(root, sz_view_button("skip", counter_tap, decoy));
  sz_view_add_child(root, sz_view_button("+1", counter_tap, count));

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  {
    SzView *taps[72];
    int n = sz_ui_collect_buttons(session, taps, 72);
    assert(n == 71);
    sz_view_format_hit_id(taps[0], id, sizeof id);
    assert(strcmp(id, "button:skip") == 0);
    sz_view_format_hit_id(taps[70], id, sizeof id);
    assert(strcmp(id, "button:+1") == 0);
  }

  sz_ui_script_play_json(session,
                         "{\"v\":1,\"kind\":\"inject\",\"events\":["
                         "{\"op\":\"tap\",\"id\":\"button:+1\"}]}");
  assert(sz_signal_int_get(count) == 1);
  assert(sz_signal_int_get(decoy) == 0);

  sz_ui_script_play_json(session,
                         "{\"v\":1,\"kind\":\"inject\",\"events\":["
                         "{\"op\":\"tap\",\"i\":0}]}");
  assert(sz_signal_int_get(decoy) == 1);
  assert(sz_signal_int_get(count) == 1);

  sz_ui_script_play_json(session,
                         "{\"v\":1,\"kind\":\"inject\",\"events\":["
                         "{\"op\":\"tap\",\"id\":\"button:missing\"}]}");
  assert(sz_signal_int_get(count) == 1);
  assert(sz_signal_int_get(decoy) == 1);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  sz_signal_int_free(decoy);
}

static void test_record_json(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *btn;
  SzSignalInt *count;
  SzInputEvent ev;
  SzRect fr;
  const char *record = "/tmp/scuzz_ui_record_schema.json";
  SzAdt *parsed, *json, *events, *ev0, *mods, *m0;
  SzList *arr, *marr;

  remove(record);
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

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TAP;
  ev.x = fr.x + fr.w * 0.5f;
  ev.y = fr.y + fr.h * 0.5f;
  assert(sz_ui_session_live_inject(session, &ev));
  assert(sz_signal_int_get(count) == 1);

  parsed = json_slurp_parse(record);
  json = (SzAdt *)sz_adt_payload(parsed);
  assert(json_doc_int(json, "v", 0) == 1);
  assert(json_str_eq(json, "kind", "inject"));
  events = json_doc_key(json, "events");
  assert(events && sz_json_is_arr(events) == 1);
  arr = sz_json_arr(events);
  assert(sz_list_len(arr) == 2);
  assert(sz_jev_bool(json, "manualPumps"));
  ev0 = (SzAdt *)sz_list_head(arr);
  assert(json_str_eq(ev0, "op", "pump") && json_doc_int(ev0, "k", 0) == 1);
  ev0 = (SzAdt *)sz_list_head(sz_list_tail(arr));
  assert(json_str_eq(ev0, "op", "tap"));
  assert(json_doc_int(ev0, "i", -1) == 0);
  sz_release(arr);
  sz_release(parsed);

  /* A tap miss records an `xy` point. */
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TAP;
  ev.x = 190.f;
  ev.y = 90.f;
  assert(sz_ui_session_live_inject(session, &ev));
  parsed = json_slurp_parse(record);
  json = (SzAdt *)sz_adt_payload(parsed);
  events = json_doc_key(json, "events");
  arr = sz_json_arr(events);
  assert(sz_list_len(arr) == 3);
  ev0 = (SzAdt *)sz_list_head(sz_list_tail(sz_list_tail(arr)));
  assert(json_str_eq(ev0, "op", "xy"));
  assert(sz_json_float_or(json_doc_key(ev0, "x"), 0.0) == 190.0);
  sz_release(arr);
  sz_release(parsed);

  /* A modified key records its mods. */
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "s";
  ev.key_mods = SZ_KEY_CTRL;
  assert(sz_ui_session_live_inject(session, &ev));
  parsed = json_slurp_parse(record);
  json = (SzAdt *)sz_adt_payload(parsed);
  events = json_doc_key(json, "events");
  arr = sz_json_arr(events);
  assert(sz_list_len(arr) == 4);
  ev0 = (SzAdt *)sz_list_head(sz_list_tail(sz_list_tail(sz_list_tail(arr))));
  assert(json_str_eq(ev0, "op", "key"));
  assert(json_str_eq(ev0, "key", "s"));
  mods = json_doc_key(ev0, "mods");
  assert(mods && sz_json_is_arr(mods) == 1);
  marr = sz_json_arr(mods);
  assert(sz_list_len(marr) == 1);
  m0 = (SzAdt *)sz_list_head(marr);
  assert(sz_json_is_str(m0) == 1);
  assert(strcmp(sz_string_cstr((SzString *)sz_adt_payload(m0)), "ctrl") == 0);
  sz_release(marr);
  sz_release(arr);
  sz_release(parsed);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  remove(record);
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

static int dump_has(SzUiSession *session, const char *needle) {
  SzView *root;
  SzString *dump;
  int ok;
  if (!session || !needle)
    return 0;
  root = sz_ui_session_root(session);
  if (!root)
    return 0;
  dump = sz_view_a11y_dump(root);
  ok = dump && strstr(sz_string_cstr(dump), needle) != NULL;
  sz_string_free(dump);
  return ok;
}

static SzUiSession *mount_kind(SzUiRuntimeKind kind, SzView *root) {
  SzUiConfig cfg = {0};
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = kind;
  cfg.width = 160;
  cfg.height = 120;
  cfg.scale = 1.f;
  cfg.title = "motion";
  return sz_ui_mount(&cfg, root);
}

/* Press, appear, tab change, and list insert share one pump clock. */
static void test_pump_motion(void) {
  SzSignalInt *count = sz_signal_int(0);
  SzView *button = sz_view_button("Go", counter_tap, count);
  SzUiSession *session = mount_kind(SZ_UI_RUNTIME_HEADLESS, button);
  SzInputEvent tap;
  unsigned paints;
  int step;
  char needle[64];
  assert(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_paints(session) == 1);
  assert(!dump_has(session, "motion:"));
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_paints(session) == 1);

  memset(&tap, 0, sizeof tap);
  tap.kind = SZ_INPUT_TAP;
  tap.x = sz_view_frame(button).x + 8.f;
  tap.y = sz_view_frame(button).y + 8.f;
  assert(sz_ui_session_set_debug_dump(session, "/tmp/scuzz_motion_hit.dump"));
  assert(sz_ui_inject_sync(session, &tap));
  for (step = 4; step >= 1; step--) {
    char *dump;
    assert(sz_ui_pump_sync(session));
    snprintf(needle, sizeof needle, "motion:press:Go=%d", step);
    assert(dump_has(session, needle));
    dump = slurp_cstr("/tmp/scuzz_motion_hit.dump");
    assert(strstr(dump, "\"desc\":\"button:Go\"") != NULL);
    free(dump);
  }
  remove("/tmp/scuzz_motion_hit.dump");
  assert(sz_ui_pump_sync(session));
  assert(!dump_has(session, "motion:press:"));
  paints = sz_ui_session_paints(session);
  assert(paints == 6);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_paints(session) == paints);
  assert(sz_signal_int_get(count) == 1);
  sz_ui_unmount(session);
  sz_view_free(button);
  sz_signal_int_free(count);

  /* A script tap finishes the four steps before it writes the dump. */
  {
    SzSignalInt *n = sz_signal_int(0);
    SzView *go = sz_view_button("Go", counter_tap, n);
    const char *path = "/tmp/scuzz_motion_script.dump";
    char *dump;
    session = mount_kind(SZ_UI_RUNTIME_HEADLESS, go);
    assert(session && sz_ui_pump_sync(session));
    assert(sz_ui_session_set_debug_dump(session, path));
    paints = sz_ui_session_paints(session);
    sz_ui_script_play_json(
        session,
        "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"tap\",\"i\":0}]}");
    assert(sz_signal_int_get(n) == 1);
    assert(sz_ui_session_paints(session) == paints + 5);
    assert(!dump_has(session, "motion:press:"));
    dump = slurp_cstr(path);
    assert(strstr(dump, "motion:press:") == NULL);
    assert(strstr(dump, "\"desc\":\"button:Go\"") != NULL);
    free(dump);
    remove(path);
    sz_ui_unmount(session);
    sz_view_free(go);
    sz_signal_int_free(n);
  }

  {
    SzSignalInt *gate = sz_signal_int(0);
    SzSignalInt *n = sz_signal_int(0);
    SzView *hidden = sz_view_show_when(gate, 1, sz_view_button("Show", counter_tap, n));
    session = mount_kind(SZ_UI_RUNTIME_HEADLESS, hidden);
    assert(session && sz_ui_pump_sync(session));
    assert(!dump_has(session, "motion:"));
    assert(!dump_has(session, "button:Show"));
    sz_signal_int_set(gate, 1);
    assert(sz_ui_pump_sync(session));
    assert(dump_has(session, "button:Show"));
    assert(dump_has(session, "motion:appear:Show=4"));
    sz_ui_unmount(session);
    sz_view_free(hidden);
    sz_signal_int_free(gate);
    sz_signal_int_free(n);
  }

  {
    SzSignalInt *selected = sz_signal_int(0);
    SzView *sections = sz_view_column();
    SzView *tabs;
    sz_view_add_child(sections, sz_view_section("counter", "Counter", sz_view_text("One")));
    sz_view_add_child(sections, sz_view_section("status", "Status", sz_view_text("Two")));
    tabs = sz_view_tabs(selected, sections);
    session = mount_kind(SZ_UI_RUNTIME_HEADLESS, tabs);
    assert(session && sz_ui_pump_sync(session));
    assert(!dump_has(session, "motion:"));
    assert(dump_has(session, "tabpanel:Counter"));
    sz_signal_int_set(selected, 1);
    assert(sz_ui_pump_sync(session));
    assert(dump_has(session, "motion:tab:Tabs=4"));
    assert(dump_has(session, "motion:appear:Status=4"));
    assert(dump_has(session, "tabpanel:Status"));
    sz_ui_unmount(session);
    sz_view_free(tabs);
    sz_signal_int_free(selected);
  }

  {
    SzSignalInt *selected = sz_signal_int(0);
    SzView *sections = sz_view_column();
    SzView *book;
    sz_view_add_child(sections, sz_view_section("home", "Home", sz_view_text("H")));
    sz_view_add_child(sections, sz_view_section("tasks", "Tasks", sz_view_text("T")));
    book = sz_view_index_book(selected, sections);
    session = mount_kind(SZ_UI_RUNTIME_HEADLESS, book);
    assert(session && sz_ui_pump_sync(session));
    sz_signal_int_set(selected, 1);
    assert(sz_ui_pump_sync(session));
    assert(!dump_has(session, "motion:tab:"));
    assert(!dump_has(session, "motion:appear:"));
    sz_ui_unmount(session);
    sz_view_free(book);
    sz_signal_int_free(selected);
  }

  {
    SzList *xs = sz_list_cons(sz_string_from_cstr("milk"), sz_list_nil());
    SzSignalList *items = sz_signal_list(xs);
    SzView *list = sz_view_each(items);
    SzList *more;
    session = mount_kind(SZ_UI_RUNTIME_HEADLESS, list);
    assert(session && sz_ui_pump_sync(session));
    assert(dump_has(session, "text:- milk"));
    assert(!dump_has(session, "motion:insert:"));
    more = sz_list_cons(sz_string_from_cstr("eggs"), xs);
    sz_signal_list_set(items, more);
    sz_release(more);
    assert(sz_ui_pump_sync(session));
    assert(dump_has(session, "motion:insert:- eggs=4"));
    assert(dump_has(session, "text:- milk"));
    assert(!dump_has(session, "motion:insert:- milk"));
    sz_ui_unmount(session);
    sz_view_free(list);
    sz_signal_list_free(items);
  }

  {
    SzSignalInt *a = sz_signal_int(0);
    SzSignalInt *b = sz_signal_int(0);
    SzView *left = sz_view_button("Go", counter_tap, a);
    SzView *right = sz_view_button("Go", counter_tap, b);
    SzUiSession *head = mount_kind(SZ_UI_RUNTIME_HEADLESS, left);
    SzUiSession *desk = mount_kind(SZ_UI_RUNTIME_DESKTOP, right);
    const char *hp = "/tmp/scuzz_motion_h.png";
    const char *dp = "/tmp/scuzz_motion_d.png";
    int i;
    assert(head && desk);
    assert(sz_ui_pump_sync(head) && sz_ui_pump_sync(desk));
    assert(sz_ui_snapshot_png_sync(head, hp));
    assert(sz_ui_snapshot_png_sync(desk, dp));
    assert(files_equal(hp, dp));
    memset(&tap, 0, sizeof tap);
    tap.kind = SZ_INPUT_TAP;
    tap.x = sz_view_frame(left).x + 8.f;
    tap.y = sz_view_frame(left).y + 8.f;
    assert(sz_ui_inject_sync(head, &tap));
    tap.x = sz_view_frame(right).x + 8.f;
    tap.y = sz_view_frame(right).y + 8.f;
    assert(sz_ui_inject_sync(desk, &tap));
    for (i = 0; i < 5; i++) {
      SzString *hd;
      SzString *dd;
      assert(sz_ui_pump_sync(head) && sz_ui_pump_sync(desk));
      hd = sz_view_a11y_dump(sz_ui_session_root(head));
      dd = sz_view_a11y_dump(sz_ui_session_root(desk));
      assert(strcmp(sz_string_cstr(hd), sz_string_cstr(dd)) == 0);
      sz_string_free(hd);
      sz_string_free(dd);
      assert(sz_ui_snapshot_png_sync(head, hp));
      assert(sz_ui_snapshot_png_sync(desk, dp));
      assert(files_equal(hp, dp));
    }
    remove(hp);
    remove(dp);
    sz_ui_unmount(head);
    sz_ui_unmount(desk);
    sz_view_free(left);
    sz_view_free(right);
    sz_signal_int_free(a);
    sz_signal_int_free(b);
  }
}

static void test_quiesce(void) {
  SzUiConfig cfg = {0};
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
   * thread runs at the same time as pump flushes. Wait until several posts
   * land so two idle samples cannot settle before the poster runs. */
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
  while (atomic_load_explicit(&env.posted, memory_order_relaxed) < 16)
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
  SzUiConfig cfg = {0};
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
  assert(strstr(a, "\"signals\":[") != NULL);
  assert(strstr(a, "\"views\":[") != NULL);
  assert(strstr(a, "text:Debug") != NULL);
  assert(strstr(a, "\"taps\":[") != NULL);
  assert(strstr(a, "\"i\":0,\"label\":\"+\"") != NULL);
  assert(strstr(a, "\"i\":1,\"label\":\"-\"") != NULL);
  {
    const char *taps = strstr(a, "\"taps\":[");
    assert(taps != NULL);
    assert(strstr(taps, ",") != NULL);
    assert(strstr(taps, "x") != NULL);
  }
  assert(strstr(a, "\"last_hit\":{") == NULL);
  assert(strstr(a, "\"fields\":[") != NULL);
  assert(strstr(a, "\"i\":0,\"target\":true,\"label\":\"item\"") != NULL);
  assert(strstr(a, "\"i\":1,\"target\":false,\"label\":\"search\"") != NULL);
  assert(strstr(a, "\"target\":true,\"label\":\"search\"") == NULL);
  assert(strstr(a, "\"scrolls\":[") != NULL);
  assert(strstr(a, "\"session\":{") != NULL);
  assert(strstr(a, "\"runtime\":\"headless\"") != NULL);
  assert(strstr(a, "\"width\":200") != NULL);
  assert(strstr(a, "\"height\":200") != NULL);
  assert(strstr(a, "\"lifecycle\":\"resume\"") != NULL);
  assert(strstr(a, "\"pumps\":1") != NULL);
  assert(strstr(a, "\"heap\":{") != NULL);
  assert(strstr(a, "\"live_bytes\":") != NULL);
  assert(strstr(a, "\"live\":[") != NULL);
  assert(strstr(a, "\"kind\":\"string\",\"rc\":") != NULL);
  assert(strstr(a, "\"live_count\":") != NULL);
  assert(strstr(a, "\"peak_bytes\":") != NULL);
  assert(strstr(a, "\"delta_bytes\":") != NULL);
  assert(strstr(a, "\"delta_count\":") != NULL);
  assert(strstr(a, "\"kind\":\"raw\"") != NULL);
  assert(strstr(a, "\"kind\":\"string\"") != NULL);
  assert(strstr(a, "\"kind\":\"list\"") != NULL);
  assert(strstr(a, "\"kind\":\"pair\"") != NULL);
  {
    char *second;
    int64_t dc;
    assert(sz_ui_session_dump_now(session));
    second = slurp_cstr(path);
    assert(strstr(second, "\"kind\":\"string\"") != NULL);
    dc = dump_i64(second, "\"delta_count\":");
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
    assert(strstr(a, "\"label\":\"item\",\"value\":\"hi\",\"caret\":2") != NULL);
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
  assert(strstr(c, "\"i\":1,\"target\":true,\"label\":\"search\"") != NULL);
  assert(strstr(c, "\"i\":0,\"target\":false,\"label\":\"item\"") != NULL);
  assert(strstr(c, "\"target\":true,\"label\":\"item\"") == NULL);
  assert(strstr(c, "\"last_hit\":{") != NULL);
  assert(strstr(c, "\"desc\":\"button:+\"") != NULL || strstr(c, "\"desc\":\"textfield:search\"") != NULL);
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
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *btn;
  SzSignalInt *count;
  SzInputEvent tap;
  const char *path = "/tmp/scuzz_ui_xy.dump";
  const char *script = "/tmp/scuzz_ui_xy.json";
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
  assert(strstr(dump, "\"last_hit\":{") != NULL);
  assert(strstr(dump, "\"desc\":\"button:+1\"") != NULL);
  free(dump);

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = 190.f;
  tap.y = 150.f;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_signal_int_get(count) == 1);
  assert(sz_ui_pump_sync(session));
  dump = slurp_cstr(path);
  assert(strstr(dump, "\"desc\":\"NULL\"") != NULL);
  free(dump);

  remove(script);
  assert(sz_ui_session_set_inject(session, script));
  {
    char line[256];
    snprintf(line, sizeof line,
             "{\"v\":1,\"kind\":\"inject\",\"events\":["
             "{\"op\":\"xy\",\"x\":%.1f,\"y\":%.1f},"
             "{\"op\":\"xy\",\"x\":190.0,\"y\":150.0}]}",
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
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *btn;
  SzSignalInt *count;
  SzInputEvent tap;
  const char *record = "/tmp/scuzz_ui_record.json";
  const char *inject = "/tmp/scuzz_ui_record_inject.json";
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
  assert(strstr(body, "{\"op\":\"tap\",\"i\":0}") != NULL);
  n0 = strlen(body);
  free(body);

  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = 190.f;
  tap.y = 90.f;
  assert(sz_ui_session_live_inject(session, &tap));
  body = slurp_cstr(record);
  assert(strstr(body, "\"op\":\"xy\"") != NULL);
  n0 = strlen(body);
  free(body);

  assert(sz_ui_session_set_inject(session, inject));
  write_stamp(inject, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"tap\",\"i\":0}]}");
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
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *list, *scroll;
  SzInputEvent ev;
  const char *record = "/tmp/scuzz_ui_record_scroll.json";
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
  assert(strstr(body, "\"op\":\"scroll\",\"i\":0,\"dy\":40") != NULL);
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
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *home, *banner, *inc_btn, *add_btn;
  SzSignalInt *count;
  SzSignalInt *page;
  SzInputEvent tap;
  const char *record = "/tmp/scuzz_ui_studio_xy.json";
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
  assert(strstr(body, "\"desc\":\"button:+1\"") != NULL);
  free(body);
  body = slurp_cstr(record);
  assert(strstr(body, "\"op\":\"tap\"") != NULL);
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
  assert(strstr(body, "\"desc\":\"NULL\"") != NULL);
  free(body);
  body = slurp_cstr(record);
  assert(strstr(body, "\"op\":\"xy\"") != NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  sz_signal_int_free(page);
  remove(record);
  remove(dump);
}

static void test_session_inject_script(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *btn;
  SzSignalInt *count;
  const char *path = "/tmp/scuzz_ui_inject.json";

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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"tap\",\"i\":0}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 1);

  /* The document plays whole on change. It is not appended. */
  write_stamp(path,
              "{\"v\":1,\"kind\":\"inject\",\"events\":["
              "{\"op\":\"tap\",\"i\":0},{\"op\":\"tap\",\"i\":0}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 3);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"tap\",\"i\":0}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 4);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  remove(path);
}

static void test_session_inject_grows_past_4k(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *btn;
  SzSignalInt *count;
  const char *path = "/tmp/scuzz_ui_inject_4k.json";
  FILE *f;
  int i;

  remove(path);  count = sz_signal_int(0);
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

  /* A document past 4k still parses whole. The long text event is a no-op
   * (no text field). */
  f = fopen(path, "w");
  assert(f);
  fputs("{\"v\":1,\"kind\":\"inject\",\"events\":["
        "{\"op\":\"text\",\"value\":\"", f);
  for (i = 0; i < 4200; i++)
    fputc('x', f);
  fputs("\"},{\"op\":\"tap\",\"i\":0}]}", f);
  fclose(f);
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 1);

  write_stamp(path,
              "{\"v\":1,\"kind\":\"inject\",\"events\":["
              "{\"op\":\"tap\",\"i\":0},{\"op\":\"tap\",\"i\":0}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 3);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  remove(path);
}

static void test_session_inject_control(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  WatchRebuildEnv *env;
  SzView *root, *first;
  SzString *a11y;
  const char *stamp = "/tmp/scuzz_ui_inject_control.stamp";
  const char *dump = "/tmp/scuzz_ui_inject_control.dump";
  const char *inject = "/tmp/scuzz_ui_inject_control.json";
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
  sz_ui_session_set_rebuild(session, watch_rebuild, env, NULL);
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
  assert(strstr(body, "\"session\":{") != NULL);
  assert(strstr(body, "\"runtime\":\"headless\"") != NULL);
  assert(strstr(body, "\"lifecycle\":\"resume\"") != NULL);
  assert(strstr(body, "\"pumps\":1") != NULL);
  assert(strstr(body, "\"heap\":{") != NULL);
  assert(strstr(body, "\"live_bytes\":") != NULL);
  assert(strstr(body, "\"live\":[") != NULL);
  free(body);

  assert(sz_ui_session_write_dump(session, fuzz));
  body = slurp_cstr(fuzz);
  assert(strstr(body, "\"signals\":[") != NULL);
  assert(strstr(body, "\"session\":{") == NULL);
  assert(strstr(body, "\"heap\":{") == NULL);
  assert(strstr(body, "\"live\":[") == NULL);
  free(body);

  remove(dump);
  assert(sz_ui_session_dump_now(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "\"session\":{") != NULL);
  assert(strstr(body, "\"heap\":{") != NULL);
  assert(strstr(body, "\"peak_bytes\":") != NULL);
  assert(strstr(body, "\"delta_bytes\":") != NULL);
  assert(strstr(body, "\"kind\":\"string\"") != NULL);
  assert(strstr(body, "\"live\":[") != NULL);
  free(body);

  {
    unsigned pumps0 = sz_ui_session_pumps(session);
    remove(dump);
    write_stamp(inject, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"dump\"}]}");
    assert(sz_ui_pump_sync(session));
    body = slurp_cstr(dump);
    assert(strstr(body, "\"heap\":{") != NULL);
    assert(strstr(body, "\"pumps\":") != NULL);
    free(body);
    assert(sz_ui_session_pumps(session) > pumps0);
  }

  {
    const char *png = "/tmp/scuzz_ui_inject_control.png";
    FILE *f;
    unsigned char magic[8];
    remove(png);
    write_stamp(inject,
                "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"snapshot\",\"path\":\"/tmp/scuzz_ui_inject_control.png\"}]}");
    assert(sz_ui_pump_sync(session));
    f = fopen(png, "rb");
    assert(f != NULL);
    assert(fread(magic, 1, 8, f) == 8);
    fclose(f);
    assert(magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' &&
           magic[3] == 'G');
    remove(png);
  }

  {
    void *hold;
    int64_t peak_hi, peak_lo;
    hold = sz_alloc(65536);
    assert(sz_ui_session_dump_now(session));
    body = slurp_cstr(dump);
    assert(strstr(body, "\"kind\":\"raw\"") != NULL);
    peak_hi = dump_i64(body, "\"peak_bytes\":");
    free(body);
    sz_free(hold);
    write_stamp(inject, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"resetpeak\"}]}");
    assert(sz_ui_pump_sync(session));
    assert(sz_ui_session_dump_now(session));
    body = slurp_cstr(dump);
    peak_lo = dump_i64(body, "\"peak_bytes\":");
    assert(strstr(body, "\"delta_count\":") != NULL);
    free(body);
    assert(peak_hi >= 65536);
    assert(peak_lo < peak_hi);
  }

  first = sz_ui_session_root(session);
  write_stamp(stamp, "v=");
  write_stamp(inject, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"reload\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_root(session) != first);
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:v=") != NULL);
  sz_string_free(a11y);

  write_stamp(inject, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"quit\"},{\"op\":\"tap\",\"i\":0}]}");
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

static void test_idle_pump_skips_paint(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root;
  SzInputEvent tap;

  root = sz_view_column();
  sz_view_add_child(root, sz_view_button("go", NULL, NULL));
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 80;
  cfg.height = 40;
  cfg.scale = 1.0;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_session_needs_paint(session));
  assert(sz_ui_session_paints(session) == 0);
  assert(sz_ui_session_pumps(session) == 0);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_pumps(session) == 1);
  assert(sz_ui_session_paints(session) == 1);
  assert(!sz_ui_session_needs_paint(session));
  {
    int i;
    for (i = 0; i < 32; i++)
      assert(sz_ui_pump_sync(session));
    assert(sz_ui_session_pumps(session) == 33);
    assert(sz_ui_session_paints(session) == 1);
    assert(!sz_ui_session_needs_paint(session));
  }
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_POINTER;
  tap.x = 40.f;
  tap.y = 20.f;
  tap.pointer_phase = SZ_POINTER_DOWN;
  tap.pointer_button = 1;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_ui_session_needs_paint(session));
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_paints(session) == 2);
  assert(sz_ui_session_pumps(session) == 34);
  assert(!sz_ui_session_needs_paint(session));
  sz_ui_unmount(session);
}

static void test_session_dump_now_needs_path(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root;

  SzSignalInt *count = sz_signal_int(0);
  root = sz_view_column();
  sz_view_add_child(root, sz_view_text_signal_int(count, "n="));
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
  uint8_t *before = NULL, *after = NULL;
  size_t before_len = 0, after_len = 0;
  assert(sz_ui_snapshot_png_bytes(session, &before, &before_len));
  assert(sz_ui_snapshot_png_sync(session, "/tmp/scuzz_stop_before.png"));
  sz_signal_int_set(count, 1);
  SzInputEvent miss = {0};
  miss.kind = SZ_INPUT_TAP;
  miss.x = -1; miss.y = -1;
  assert(sz_ui_inject_sync(session, &miss));
  assert(sz_ui_session_needs_paint(session));
  sz_ui_session_request_stop(session);
  assert(!sz_ui_session_alive(session));
  assert(sz_ui_session_lifecycle(session) == SZ_LIFECYCLE_STOP);
  assert(!sz_ui_pump_sync(session));
  assert(sz_ui_session_pumps(session) == 1);
  assert(sz_ui_snapshot_png_bytes(session, &after, &after_len));
  assert(before_len == after_len && memcmp(before, after, before_len) == 0);
  assert(sz_ui_snapshot_png_sync(session, "/tmp/scuzz_stop_after.png"));
  assert(files_equal("/tmp/scuzz_stop_before.png", "/tmp/scuzz_stop_after.png"));
  assert(sz_ui_session_pumps(session) == 1);
  free(before);
  free(after);
  sz_ui_unmount(session);
  sz_signal_int_free(count);
}

static void test_session_inject_scroll(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *row, *scroll, *scroll2, *list, *list2;
  const char *path = "/tmp/scuzz_ui_inject_scroll.json";
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
  sz_view_add_child(row, sz_view_expanded(1, scroll));
  sz_view_add_child(row, sz_view_expanded(1, scroll2));
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"scroll\",\"dy\":40}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_scroll_y(scroll) == y0 + 40.f);
  assert(sz_view_scroll_y(scroll2) == y1);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"scroll\",\"i\":1,\"dy\":40}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_scroll_y(scroll) == y0 + 40.f);
  assert(sz_view_scroll_y(scroll2) == y1 + 40.f);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"scroll\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_scroll_y(scroll) == y0 + 80.f);
  assert(sz_view_scroll_y(scroll2) == y1 + 40.f);

  sz_ui_unmount(session);
  remove(path);
}

static void test_script_run_file_long_line(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  const char *path = "/tmp/scuzz_ui_long_line.json";
  FILE *f;
  size_t i;
  char *want;

  /* One event with a value longer than 1024 bytes. It must run whole. */
  f = fopen(path, "w");
  assert(f);
  fputs("{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"", f);
  for (i = 0; i < 2000; i++)
    fputc(i % 2 ? 'x' : 'y', f);
  fputs("\"}]}", f);
  fclose(f);

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
  assert(sz_ui_pump_sync(session));
  sz_ui_script_run_file(session, path);
  want = (char *)malloc(2001);
  assert(want);
  for (i = 0; i < 2000; i++)
    want[i] = i % 2 ? 'x' : 'y';
  want[2000] = '\0';
  assert(strcmp(sz_signal_str_get(draft), want) == 0);
  free(want);
  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
}

static void test_session_inject_backspace(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  const char *path = "/tmp/scuzz_ui_inject_backspace.json";

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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"hi\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hi") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"backspace\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "h") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"backspace\",\"count\":2}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "a") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
}

static void test_session_inject_type(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  const char *path = "/tmp/scuzz_ui_inject_type.json";

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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"hi\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hi") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"type\",\"value\":\"!\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hi!") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"type\",\"value\":\"\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hi!") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"ab\"},{\"op\":\"type\",\"value\":\"c\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abc") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"x\"},{\"op\":\"type\",\"value\":\"a\\nb\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "xa\nb") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
}

static void test_session_inject_key(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const char *path = "/tmp/scuzz_ui_inject_key.json";

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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"hi\"},{\"op\":\"key\",\"key\":\"q\",\"text\":\"q\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hiq") == 0);
  assert(sz_ui_session_alive(session));

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"Enter\"},{\"op\":\"key\",\"key\":\"Tab\"},{\"op\":\"key\",\"key\":\"Backspace\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hi") == 0);
  assert(sz_ui_session_alive(session));

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"PageUp\"},{\"op\":\"key\",\"key\":\"PageDown\"}]}");
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
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  const char *path = "/tmp/scuzz_ui_inject_key_utf8.json";
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"caf\xC3\xA9\"},{\"op\":\"key\",\"key\":\"Backspace\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "caf") == 0);
  assert(strlen(cafe) == 5);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"caf\xC3\xA9\"},{\"op\":\"backspace\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "caf") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
}

static void test_record_live_key(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const char *record = "/tmp/scuzz_ui_record_key.json";
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
  assert(strstr(body, "{\"op\":\"key\",\"key\":\"q\",\"text\":\"q\"}") != NULL);
  assert(strstr(body, "\"op\":\"type\"") == NULL);
  free(body);

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "Backspace";
  ev.text = "";
  assert(sz_ui_session_live_inject(session, &ev));
  assert(strcmp(sz_signal_str_get(draft), "") == 0);
  body = slurp_cstr(record);
  assert(strstr(body, "\"op\":\"key\",\"key\":\"Backspace\"") != NULL);
  assert(strstr(body, "backspace") == NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(record);
}

static void test_record_type_escapes(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const char *record = "/tmp/scuzz_ui_record_type_esc.json";
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
  assert(strstr(body, "\"op\":\"type\",\"value\":\"a\\nb\"") != NULL);
  free(body);
  assert(strcmp(sz_signal_str_get(draft), "a\nb") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(record);
}

static void test_session_inject_key_repeat(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const char *path = "/tmp/scuzz_ui_inject_key_repeat.json";
  const char *dump = "/tmp/scuzz_ui_inject_key_repeat.dump";
  const char *record = "/tmp/scuzz_ui_record_key_repeat.json";
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"a\",\"text\":\"a\"},{\"op\":\"key\",\"key\":\"a\",\"text\":\"a\",\"repeat\":true}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "aa") == 0);
  assert(sz_view_text_field_caret(field) == 2);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"Backspace\"},{\"op\":\"key\",\"key\":\"Backspace\",\"repeat\":true}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "") == 0);
  assert(sz_view_text_field_caret(field) == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"b\",\"text\":\"b\"},{\"op\":\"key\",\"key\":\"c\",\"text\":\"c\"},{\"op\":\"key\",\"key\":\"ArrowLeft\"},{\"op\":\"key\",\"key\":\"ArrowLeft\",\"repeat\":true}]}");
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
  assert(strstr(body, "{\"op\":\"key\",\"key\":\"e\",\"text\":\"e\",\"repeat\":true}") != NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
  remove(dump);
  remove(record);
}

static void test_session_inject_compose(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *field, *ed;
  SzSignalStr *draft, *buf;
  SzInputEvent ev;
  const char *path = "/tmp/scuzz_ui_inject_compose.json";
  const char *dump = "/tmp/scuzz_ui_inject_compose.dump";
  const char *record = "/tmp/scuzz_ui_record_compose.json";
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"compose\",\"value\":\"n\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "ab") == 0);
  assert(strcmp(sz_view_text_field_preedit(field), "n") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"preedit\":\"n\"") != NULL);
  assert(strstr(body, "\"label\":\"item\",\"value\":\"ab\"") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"compose\",\"value\":\"ni\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "ab") == 0);
  assert(strcmp(sz_view_text_field_preedit(field), "ni") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"commit\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abni") == 0);
  assert(sz_view_text_field_preedit(field)[0] == '\0');
  body = slurp_cstr(dump);
  assert(strstr(body, "\"label\":\"item\",\"value\":\"abni\"") != NULL);
  assert(strstr(body, "\"preedit\":\"n\"") == NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"compose\",\"value\":\"ja\"},{\"op\":\"key\",\"key\":\"Escape\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abni") == 0);
  assert(sz_view_text_field_preedit(field)[0] == '\0');

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"hi\"},{\"op\":\"select\",\"start\":0,\"end\":2},{\"op\":\"compose\",\"value\":\"x\"},{\"op\":\"compose\",\"value\":\"\"}]}");
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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"compose\",\"value\":\"ka\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "xy") == 0);
  assert(strcmp(sz_view_editor_preedit(ed), "ka") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"editors\":[") != NULL);
  assert(strstr(body, "\"preedit\":\"ka\"") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"commit\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strstr(sz_signal_str_get(buf), "ka") != NULL);
  assert(sz_view_editor_preedit(ed)[0] == '\0');

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"z\",\"text\":\"z\"},{\"op\":\"key\",\"key\":\"z\",\"text\":\"z\",\"repeat\":true},{\"op\":\"key\",\"key\":\"Backspace\",\"repeat\":true}]}");
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
  assert(strstr(body, "\"op\":\"compose\",\"value\":\"ni\"") != NULL);
  free(body);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_COMPOSE;
  ev.text = "";
  assert(sz_ui_session_live_inject(session, &ev));
  body = slurp_cstr(record);
  assert(strstr(body, "\"op\":\"commit\"") != NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  sz_signal_str_free(buf);
  remove(path);
  remove(dump);
  remove(record);
}

static void test_record_live_hover_secondary(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *btn, *tip;
  SzSignalInt *count;
  SzInputEvent ev;
  const char *record = "/tmp/scuzz_ui_record_hover.json";
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
    char *first = strstr(body, "\"op\":\"hover\"");
    assert(first != NULL);
    assert(strstr(first + 12, "\"op\":\"hover\"") == NULL);
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
  assert(strstr(body, "\"op\":\"secondary\",\"i\":0") != NULL);
  assert(strstr(body, "\"op\":\"tap\"") == NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  remove(record);
}

static void test_session_inject_caret(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const SzTheme *theme = sz_theme_default();
  const char *path = "/tmp/scuzz_ui_inject_caret.json";
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abc") == 0);
  assert(sz_view_text_field_caret(field) == 3);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"target\":true,\"label\":\"item\",\"value\":\"abc\",\"caret\":3") != NULL);
  assert(strstr(body, "\"taps\":[") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"caret\",\"offset\":1},{\"op\":\"key\",\"key\":\"Delete\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "ac") == 0);
  assert(sz_view_text_field_caret(field) == 1);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"target\":true,\"label\":\"item\",\"value\":\"ac\",\"caret\":1") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"key\",\"key\":\"Home\"},{\"op\":\"key\",\"key\":\"ArrowRight\"},{\"op\":\"key\",\"key\":\"x\",\"text\":\"x\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "axbc") == 0);
  assert(sz_view_text_field_caret(field) == 2);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"key\",\"key\":\"End\"},{\"op\":\"key\",\"key\":\"ArrowLeft\"},{\"op\":\"key\",\"key\":\"Backspace\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "ac") == 0);
  assert(sz_view_text_field_caret(field) == 1);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"caf\xC3\xA9\"},{\"op\":\"key\",\"key\":\"ArrowLeft\"},{\"op\":\"key\",\"key\":\"Delete\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "caf") == 0);
  assert(sz_view_text_field_caret(field) == 3);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"caret\",\"i\":0,\"offset\":1}]}");
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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));
  assert(sz_view_text_field_caret(field) == 1);
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"Delete\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "ac") == 0);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
  remove(dump);
}

static void test_session_inject_hover_secondary(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *btn, *tip;
  SzSignalInt *count;
  SzInputEvent ev;
  const char *path = "/tmp/scuzz_ui_inject_hover.json";
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
  assert(strstr(body, "\"hover\":{") != NULL);
  assert(strstr(body, "tooltip:Sean") != NULL);
  free(body);

  {
    char line[256];
    snprintf(line, sizeof line,
             "{\"v\":1,\"kind\":\"inject\",\"events\":["
             "{\"op\":\"hover\",\"x\":%.1f,\"y\":%.1f}]}",
             fr.x + fr.w * 0.5f, fr.y + fr.h * 0.5f);
    write_stamp(path, line);
  }
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "\"hover\":{") != NULL);
  assert(strstr(body, "tooltip:Sean") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"hover\",\"x\":190,\"y\":70}]}");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "\"hover\":{") != NULL);
  assert(strstr(body, "\"desc\":\"NULL\"") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"secondary\",\"i\":0}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"last_secondary\":{") != NULL);
  assert(strstr(body, "button:Go") != NULL);
  free(body);

  {
    char line[256];
    snprintf(line, sizeof line,
             "{\"v\":1,\"kind\":\"inject\",\"events\":["
             "{\"op\":\"secondary\",\"x\":%.1f,\"y\":%.1f}]}",
             fr.x + fr.w * 0.5f, fr.y + fr.h * 0.5f);
    write_stamp(path, line);
  }
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"tap\",\"i\":0}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 1);

  sz_ui_unmount(session);
  sz_signal_int_free(count);
  remove(path);
  remove(dump);
}

static void test_on_secondary_script_fires(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *btn, *wrap;
  SzSignalInt *primary;
  SzSignalInt *secondary;
  const char *path = "/tmp/scuzz_ui_on_secondary.json";
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"secondary\",\"i\":0}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(primary) == 0);
  assert(sz_signal_int_get(secondary) == 1);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"last_secondary\":{") != NULL);
  assert(strstr(body, "button:Go") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"tap\",\"i\":0}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(primary) == 1);
  assert(sz_signal_int_get(secondary) == 1);

  sz_ui_unmount(session);
  sz_signal_int_free(primary);
  sz_signal_int_free(secondary);
  remove(path);
  remove(dump);
}

static void test_focus_revealed_after_resize(void) {
  SzSignalInt *page = sz_signal_int(0);
  SzSignalStr *draft = sz_signal_str("");
  SzView *field = sz_view_text_field(draft, "item");
  SzView *content = sz_view_column();
  sz_view_add_child(content, sz_view_sized(20, 900, sz_view_text("space")));
  sz_view_add_child(content, field);
  SzView *sections = sz_view_column();
  sz_view_add_child(sections, sz_view_section("stable", "Renamed title", content));
  SzView *root = sz_view_index_book(page, sections);
  SzUiConfig cfg = {0};
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 320; cfg.height = 720; cfg.scale = 1;
  SzUiSession *session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  sz_ui_session_focus_view(session, field);
  assert(sz_ui_pump_sync(session));
  SzRect frame = sz_view_frame(field);
  assert(frame.y >= 0 && frame.y + frame.h <= 720);
  SzInputEvent resize = {0};
  resize.kind = SZ_INPUT_RESIZE; resize.width = 320; resize.height = 240;
  assert(sz_ui_inject_sync(session, &resize));
  assert(sz_ui_pump_sync(session));
  frame = sz_view_frame(field);
  assert(frame.y >= 0 && frame.y + frame.h <= 240);
  uint8_t *pixels;
  size_t bytes;
  assert(sz_ui_snapshot_png_bytes(session, &pixels, &bytes));
  resize.scale = NAN;
  assert(!sz_ui_inject_sync(session, &resize));
  resize.scale = -1;
  assert(!sz_ui_inject_sync(session, &resize));
  resize.scale = 1e30;
  assert(!sz_ui_inject_sync(session, &resize));
  uint8_t *after;
  size_t unchanged;
  assert(sz_ui_snapshot_png_bytes(session, &after, &unchanged));
  assert(unchanged == bytes && memcmp(pixels, after, bytes) == 0);
  free(pixels);
  free(after);
  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  sz_signal_int_free(page);
}

static void test_viewport_record_replay(void) {
  const char *record = "/tmp/scuzz_ui_viewport_record.json";
  const char *dump = "/tmp/scuzz_ui_viewport_dump.json";
  for (int scale = 1; scale <= 2; scale++) {
    for (int replay = 0; replay <= 1; replay++) {
      SzSignalStr *draft = sz_signal_str("");
      SzView *field = sz_view_text_field(draft, "draft");
      SzView *content = sz_view_column();
      sz_view_add_child(content, sz_view_sized(20, 900, sz_view_text("space")));
      sz_view_add_child(content, field);
      SzView *root = sz_view_scroll(content);
      SzUiConfig cfg = {0};
      cfg.kind = SZ_UI_RUNTIME_HEADLESS;
      cfg.width = 320; cfg.height = 720; cfg.scale = scale;
      SzUiSession *session = sz_ui_mount(&cfg, root);
      assert(session);
      sz_ui_session_take_root(session);
      sz_ui_session_focus_view(session, field);
      if (replay) {
        sz_ui_script_run_file(session, record);
      } else {
        assert(sz_ui_session_set_record(session, record));
        SzInputEvent event = {0};
        event.kind = SZ_INPUT_TEXT_EDIT; event.text = "caf\xc3\xa9";
        assert(sz_ui_session_live_inject(session, &event));
        event.kind = SZ_INPUT_RESIZE;
        event.width = 720; event.height = 320; event.scale = scale;
        assert(sz_ui_session_live_inject(session, &event));
        event.kind = SZ_INPUT_LIFECYCLE; event.lifecycle = SZ_LIFECYCLE_PAUSE;
        assert(sz_ui_session_live_inject(session, &event));
        event.lifecycle = SZ_LIFECYCLE_RESUME;
        assert(sz_ui_session_live_inject(session, &event));
        event.kind = SZ_INPUT_KEYBOARD; event.keyboard_visible = 1;
        assert(sz_ui_session_live_inject(session, &event));
        event.kind = SZ_INPUT_RESIZE; event.width = 320; event.height = 240;
        assert(sz_ui_session_live_inject(session, &event));
      }
      assert(sz_ui_pump_sync(session));
      SzRect frame = sz_view_frame(field);
      assert(frame.x >= 0 && frame.x + frame.w <= 320);
      assert(frame.y >= 0 && frame.y + frame.h <= 240);
      assert(strcmp(sz_signal_str_get(draft), "caf\xc3\xa9") == 0);
      assert(sz_ui_session_keyboard_visible(session));
      assert(sz_ui_session_lifecycle(session) == SZ_LIFECYCLE_RESUME);
      assert(sz_ui_session_set_debug_dump(session, dump));
      assert(sz_ui_session_dump_now(session));
      char *body = slurp_cstr(dump);
      assert(strstr(body, "\"width\":320,\"height\":240") != NULL);
      free(body);
      sz_ui_unmount(session);
      sz_signal_str_free(draft);
    }
  }
  remove(record);
  remove(dump);
}

static void test_code_copy_and_heading(void) {
  char long_text[1024] = "";
  for (int i = 0; i < 60; i++) strcat(long_text, "source line\n");
  strcat(long_text, "last line");
  SzView *long_view = sz_view_text(long_text);
  sz_view_layout(long_view, 600, 0, sz_theme_default());
  assert(sz_view_frame(long_view).h >= 61 * 20);
  sz_view_free(long_view);
  SzView *long_code = sz_view_code(long_text);
  sz_view_layout(long_code, 300, 0, sz_theme_default());
  assert(sz_view_frame(long_code).h >= 61 * 20);
  sz_view_free(long_code);
  SzView *inline_code = sz_view_code("echo hello");
  sz_view_layout(inline_code, 600, 0, sz_theme_default());
  assert(sz_view_frame(inline_code).h <= 130);
  sz_view_free(inline_code);
  SzSignalStr *draft = sz_signal_str("");
  SzView *root = sz_view_column();
  SzView *targets[4];
  const char *source = "echo café\n  echo world";
  sz_view_add_child(root, sz_view_heading(1, sz_view_text("Example")));
  sz_view_add_child(root, sz_view_code(source));
  sz_view_add_child(root, sz_view_text_field(draft, "paste"));
  SzUiConfig cfg = {0};
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 600; cfg.height = 400; cfg.scale = 1;
  SzUiSession *session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_view_collect_tap_targets(root, targets, 4) >= 1);
  assert(sz_ui_session_activate_view(session, targets[0]));
  assert(sz_ui_session_paste(session, NULL));
  assert(strcmp(sz_signal_str_get(draft), source) == 0);
  SzString *dump = sz_view_a11y_dump(root);
  assert(strstr(sz_string_cstr(dump), "heading:1"));
  assert(strstr(sz_string_cstr(dump), "outlined:Copied"));
  sz_release(dump);
  sz_ui_unmount(session);
  sz_signal_str_free(draft);
}

static void test_session_inject_selection_clipboard(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *field;
  SzSignalStr *draft;
  SzInputEvent ev;
  const SzTheme *theme = sz_theme_default();
  const char *path = "/tmp/scuzz_ui_inject_sel.json";
  const char *dump = "/tmp/scuzz_ui_inject_sel.dump";
  const char *record = "/tmp/scuzz_ui_record_sel.json";
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"caret\",\"offset\":0},{\"op\":\"key\",\"key\":\"ArrowRight\",\"mods\":[\"shift\"]},{\"op\":\"key\",\"key\":\"ArrowRight\",\"mods\":[\"shift\"]}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abc") == 0);
  assert(sz_view_text_field_caret(field) == 2);
  assert(sz_view_text_field_sel_start(field) == 0);
  assert(sz_view_text_field_sel_end(field) == 2);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"target\":true,\"label\":\"item\",\"value\":\"abc\",\"caret\":2,\"sel_start\":0,\"sel_end\":2") != NULL);
  assert(strstr(body, "\"taps\":[") != NULL);
  assert(strstr(body, "\"fields\":[") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"copy\"},{\"op\":\"key\",\"key\":\"End\"},{\"op\":\"paste\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "abcab") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"select\",\"start\":0,\"end\":2},{\"op\":\"type\",\"value\":\"x\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "xc") == 0);
  assert(sz_view_text_field_caret(field) == 1);
  assert(sz_view_text_field_sel_start(field) == 1);
  assert(sz_view_text_field_sel_end(field) == 1);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"target\":true,\"label\":\"item\",\"value\":\"xc\",\"caret\":1,\"sel_start\":1,\"sel_end\":1") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"select\",\"start\":0,\"end\":2},{\"op\":\"key\",\"key\":\"Backspace\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "c") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"select\",\"start\":1,\"end\":3},{\"op\":\"key\",\"key\":\"Delete\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "a") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"select\",\"start\":0,\"end\":2},{\"op\":\"cut\"},{\"op\":\"key\",\"key\":\"End\"},{\"op\":\"paste\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "cab") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"hi\"},{\"op\":\"paste\",\"value\":\"xyz\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "hixyz") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"caf\xC3\xA9\"},{\"op\":\"select\",\"start\":3,\"end\":5},{\"op\":\"key\",\"key\":\"Backspace\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "caf") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"select\",\"start\":0,\"end\":3},{\"op\":\"key\",\"key\":\"ArrowLeft\"}]}");
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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"}]}");
  assert(sz_ui_pump_sync(session));
  {
    char line[256];
    snprintf(line, sizeof line,
             "{\"v\":1,\"kind\":\"inject\",\"events\":["
             "{\"op\":\"drag\",\"x1\":%.1f,\"y1\":%.1f,\"x2\":%.1f,\"y2\":%.1f}]}",
             x0, y, x2, y);
    write_stamp(path, line);
  }
  assert(sz_ui_pump_sync(session));
  assert(sz_view_text_field_sel_start(field) == 0);
  assert(sz_view_text_field_sel_end(field) >= 1);
  assert(strcmp(sz_signal_str_get(draft), "abc") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"select\",\"start\":0,\"end\":2}]}");
  assert(sz_ui_pump_sync(session));
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "c";
  ev.key_mods = SZ_KEY_CMD;
  assert(sz_ui_session_live_inject(session, &ev));
  ev.key = "v";
  ev.key_mods = SZ_KEY_CTRL;
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"End\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_live_inject(session, &ev));
  assert(strcmp(sz_signal_str_get(draft), "abcab") == 0);
  body = slurp_cstr(record);
  assert(strstr(body, "\"op\":\"copy\"") != NULL);
  assert(strstr(body, "\"op\":\"paste\",\"value\":\"ab\"") != NULL);
  assert(strstr(body, "\"key\":\"c\"") == NULL);
  free(body);

  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "ArrowLeft";
  ev.key_mods = SZ_KEY_SHIFT;
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"abc\"},{\"op\":\"key\",\"key\":\"End\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_live_inject(session, &ev));
  assert(sz_view_text_field_sel_end(field) == 3);
  assert(sz_view_text_field_sel_start(field) < 3);
  body = slurp_cstr(record);
  assert(strstr(body, "\"key\":\"ArrowLeft\",\"mods\":[\"shift\"]") != NULL);
  free(body);

  sz_ui_unmount(session);
  sz_signal_str_free(draft);
  remove(path);
  remove(dump);
  remove(record);
}

static void test_session_inject_field_index(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root;
  SzSignalStr *draft, *query;
  const char *path = "/tmp/scuzz_ui_inject_field.json";

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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"i\":1,\"value\":\"hello\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "") == 0);
  assert(strcmp(sz_signal_str_get(query), "hello") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"type\",\"i\":0,\"value\":\"x\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "x") == 0);
  assert(strcmp(sz_signal_str_get(query), "hello") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"i\":0,\"value\":\"ab\"},{\"op\":\"backspace\",\"i\":0,\"count\":1}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "a") == 0);
  assert(strcmp(sz_signal_str_get(query), "hello") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"0\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(draft), "0") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"i\":9,\"value\":\"no\"}]}");
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
  SzUiConfig cfg = {0};
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

static void test_bitmap_images(void) {
  size_t before_bytes, before_count, after_bytes, after_count;
  sz_alloc_stats(&before_bytes, &before_count);
  SkSurface *source = sk_surface_make_raster_n32_premul(2, 1);
  assert(source);
  sk_canvas_clear(sk_surface_get_canvas(source), sk_color_rgba(120, 60, 20, 255));
  uint8_t *bytes = NULL;
  size_t len = 0;
  assert(sk_encode_png(source, &bytes, &len));
  SzString *input = sz_string_from_bytes((const char *)bytes, len);
  SzAdt *result = sz_lang_image_decode(input);
  assert(sz_adt_tag(result) == 1);
  SzImage *image = (SzImage *)sz_adt_payload(result);
  assert(sz_image_width(image) == 2 && sz_image_height(image) == 1);
  assert(sz_rc_kind(image) == SZ_RC_IMAGE && sz_rc_count(image) == 1);
  SzView *a = sz_view_image(image, "A");
  SzView *b = sz_view_image(image, "B");
  assert(sz_rc_count(image) == 3);
  sz_release(result);
  sz_release(input);
  assert(sz_rc_count(image) == 2);
  SzView *root = sz_view_sized(80, 80, sz_view_opacity(50, a));
  SzTheme theme = *sz_theme_default();
  theme.background = 0;
  SkSurface *surface = sk_surface_make_raster_n32_premul(80, 80);
  assert(surface);
  assert(sz_view_paint(root, sk_surface_get_canvas(surface), 80, 80, &theme));
  const uint8_t *pixels = sk_surface_peek_pixels(surface, NULL);
  assert(pixels[3] == 0);
  const uint8_t *middle = pixels + (40 * 80 + 40) * 4;
  assert(abs(middle[0] - 60) <= 1 && abs(middle[1] - 30) <= 1 && abs(middle[2] - 10) <= 1 && abs(middle[3] - 128) <= 1);
  sz_view_layout(a, 120, 120, &theme);
  assert(fabsf(sz_view_frame(a).w - 2.f) < 0.01f && fabsf(sz_view_frame(a).h - 1.f) < 0.01f);
  theme.px_scale = 2.f;
  sz_view_layout(a, 120, 120, &theme);
  assert(fabsf(sz_view_frame(a).w - 4.f) < 0.01f && fabsf(sz_view_frame(a).h - 2.f) < 0.01f);
  sz_view_free(root);
  assert(sz_rc_count(image) == 1);
  theme.px_scale = 1.f;
  root = sz_view_sized(80, 40, sz_view_radius(12, b));
  assert(sz_view_paint(root, sk_surface_get_canvas(surface), 80, 80, &theme));
  pixels = sk_surface_peek_pixels(surface, NULL);
  assert(pixels[3] == 0 && pixels[(20 * 80 + 40) * 4] == 120);
  SzString *labels = sz_view_a11y_dump(root);
  assert(strstr(sz_string_cstr(labels), "image:B"));
  sz_release(labels);
  sz_view_free(root);
  sk_surface_unref(source);
  sk_surface_unref(surface);
  free(bytes);
  input = sz_string_from_cstr("invalid");
  result = sz_lang_image_decode(input);
  assert(sz_adt_tag(result) == 0 && sz_rc_kind(sz_adt_payload(result)) == SZ_RC_STRING);
  sz_release(result);
  sz_release(input);
  sz_alloc_stats(&after_bytes, &after_count);
  assert(before_bytes == after_bytes && before_count == after_count);
}

static void test_widgets(void) {
  SzView *col, *scroll, *list, *field, *img, *icon;
  SzSignalStr *draft;
  const SzTheme *theme = sz_theme_default();

  draft = sz_signal_str("hi");
  col = sz_view_column();
  field = sz_view_text_field(draft, "type");
  img = image_fixture(16, 16, 0xFF112233u, "i");
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
  exp = sz_view_expanded(1, scroll);
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
  exp = sz_view_expanded(1, mid);
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
  bot = image_fixture(40, 40, 0xFF112233u, "");
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
  bot = image_fixture(40, 40, 0xFF112233u, "");
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

/* Peek is row-major RGBA. */
static int px_rgb(const uint8_t *px, int w, int x, int y, uint8_t r, uint8_t g,
                  uint8_t b) {
  const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
  return p[0] == r && p[1] == g && p[2] == b;
}

static void test_control_labels_use_text(void) {
  const SzTheme *theme = sz_theme_default();
  const char *label = "Enable autosave";
  SzSignalInt *state = sz_signal_int(0);
  SzView *controls[] = {sz_view_checkbox(state, label), sz_view_switch(state, label),
                        sz_view_chip(state, label), sz_view_radio(state, 1, label)};
  for (int i = 0; i < 4; i++) {
    SzRect before, after;
    sz_view_layout(controls[i], 400.f, 80.f, theme);
    before = sz_view_frame(controls[i]);
    assert(before.w >= sk_font_measure_string(label, theme->font_px) + theme->gap);
    sz_signal_int_set(state, 1);
    sz_view_layout(controls[i], 400.f, 80.f, theme);
    after = sz_view_frame(controls[i]);
    assert(before.w == after.w && before.h == after.h);
    assert(strcmp(sz_view_a11y_label(controls[i]), label) == 0);
    sz_signal_int_set(state, 0);
    sz_view_free(controls[i]);
  }
  sz_signal_int_free(state);
}

static void test_narrow_button_labels_stay_inside(void) {
  const char *label = "A long document name with UTF-8: caf\xc3\xa9.scuzz";
  for (int kind = 0; kind < 3; kind++) {
    SzView *button = kind == 0 ? sz_view_button(label, NULL, NULL) :
                     kind == 1 ? sz_view_outlined_button(label, NULL, NULL) :
                                 sz_view_text_button(label, NULL, NULL);
    SzView *root = sz_view_sized(80, 40, button);
    SkSurface *surf = sk_surface_make_raster_n32_premul(240, 80);
    SkCanvas *canvas = sk_surface_get_canvas(surf);
    size_t bytes;
    const uint8_t *pixels;
    assert(sz_view_paint(root, canvas, 240, 80, sz_theme_default()));
    assert(strcmp(sz_view_a11y_label(button), label) == 0);
    pixels = sk_surface_peek_pixels(surf, &bytes);
    assert(pixels && bytes == 240 * 80 * 4);
    for (int y = 0; y < 80; y++)
      for (int x = 80; x < 240; x++)
        assert(px_rgb(pixels, 240, x, y, 0xF3, 0xEF, 0xE3));
    sk_surface_unref(surf);
    sz_view_free(root);
  }
}

static void test_edit_paint_scale_and_clip(void) {
  const float scales[] = {1.f, 1.5f, 2.f};
  for (int editor = 0; editor <= 1; editor++) {
    for (int i = 0; i < 3; i++) {
      float scale = scales[i];
      SzTheme theme = *sz_theme_default();
      SzSignalStr *text = sz_signal_str(editor ? "a\nb\nc\nd\ne\nf\ng\nh\ni" : "");
      SzView *edit = editor ? sz_view_editor(text) : sz_view_text_field(text, "");
      SzView *root = sz_view_sized(100, 80, edit);
      SkSurface *surf = sk_surface_make_raster_n32_premul(240, 180);
      SkCanvas *canvas = sk_surface_get_canvas(surf);
      SzRect caret;
      const uint8_t *pixels;
      size_t bytes;
      sz_view_layout(root, 120.f, 90.f, &theme);
      assert(sz_view_focus_edit_target(root));
      if (editor) {
        assert(sz_view_set_editor_caret(edit, 4));
        sz_view_scroll_by(edit, 20.f);
        assert(sz_view_editor_scroll_y(edit) > 0.f);
      }
      theme.font_px *= scale;
      theme.control_h *= scale;
      theme.pad *= scale;
      theme.gap *= scale;
      theme.px_scale = scale;
      assert(sz_view_paint(root, canvas, 240, 180, &theme));
      caret = sz_view_caret_rect(root, &theme);
      assert(fabsf(caret.w - scale) < 0.01f);
      assert(fabsf(caret.h - 14.f * scale) < 0.01f);
      if (editor) {
        assert(fabsf(caret.x - sk_font_mono_cell(theme.font_px) - 10.f * scale) < 0.01f);
        assert(fabsf(caret.y - 26.f * scale) < 0.01f);
      } else {
        assert(fabsf(caret.x - 6.f * scale) < 0.01f);
      }
      pixels = sk_surface_peek_pixels(surf, &bytes);
      assert(pixels && bytes == 240 * 180 * 4);
      assert(px_rgb(pixels, 240, (int)ceilf(caret.x), (int)ceilf(caret.y + 2.f * scale),
                    0x24, 0x23, 0x1F));
      if (!editor) {
        sz_signal_str_set(text, "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM");
        assert(sz_view_paint(root, canvas, 240, 180, &theme));
        pixels = sk_surface_peek_pixels(surf, &bytes);
        for (int y = 0; y < 180; y++)
          for (int x = (int)(100.f * scale); x < 240; x++)
            assert(px_rgb(pixels, 240, x, y, 0xF3, 0xEF, 0xE3));
      }
      sk_surface_unref(surf);
      sz_view_free(root);
      sz_signal_str_free(text);
    }
  }
}

static void test_clip_paint_contains_overflow(void) {
  SzView *root, *clip, *body;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();

  body = sz_view_background(0xFF00AA00u, sz_view_sized(20, 80, sz_view_text("")));
  clip = sz_view_clip(sz_view_scroll(body));
  root = sz_view_sized(40, 40, clip);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  /* Scroll child sits at pad 12 inside the 40x40 clip. */
  assert(px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  /* Same canvas, outside the clip frame: theme background. */
  assert(px_rgb(px, 80, 20, 50, 0xF3, 0xEF, 0xE3));
  assert(px_rgb(px, 80, 50, 20, 0xF3, 0xEF, 0xE3));
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
  assert(px_rgb(px, 80, 20, 20, 0xF3, 0xEF, 0xE3));
  sz_view_free(root);

  root = opacity_green_box(50);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(!px_rgb(px, 80, 20, 20, 0x00, 0xAA, 0x00));
  assert(!px_rgb(px, 80, 20, 20, 0xF3, 0xEF, 0xE3));
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
  exp = sz_view_expanded(1, body);
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
  exp = sz_view_expanded(1, box);
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

static void test_container_child_list_ownership(void) {
  for (int list_first = 0; list_first <= 1; list_first++) {
    size_t before_bytes, before_count, after_bytes, after_count;
    sz_alloc_stats(&before_bytes, &before_count);
    SzView *grid = sz_view_grid(3);
    SzList *children = NULL;
    SzView *last = NULL;
    for (int i = 11; i >= 0; i--) {
      char label[32];
      snprintf(label, sizeof label, "cell%d", i);
      SzView *child = sz_view_button(label, NULL, NULL);
      if (i == 11)
        last = child;
      SzList *next = sz_list_cons(child, children);
      sz_list_free(children);
      sz_view_free(child);
      children = next;
    }
    assert(sz_rc_count(last) == 1);
    assert(sz_lang_view_add_children(grid, children) == NULL);
    assert(sz_rc_count(last) == 2);
    if (list_first) {
      sz_list_free(children);
      assert(sz_rc_count(last) == 1);
    }
    sz_view_layout(grid, 960.f, 560.f, sz_theme_default());
    SzRect frame = sz_view_frame(last);
    assert(sz_view_hit_test(grid, frame.x + 2.f, frame.y + 2.f) == last);
    SzString *dump = sz_view_a11y_dump(grid);
    assert(strstr(sz_string_cstr(dump), "button:cell11"));
    sz_string_free(dump);
    sz_view_free(grid);
    if (!list_first) {
      assert(sz_rc_count(last) == 1);
      assert(strcmp(sz_view_a11y_label(last), "cell11") == 0);
      sz_list_free(children);
    }
    sz_alloc_stats(&after_bytes, &after_count);
    assert(before_bytes == after_bytes && before_count == after_count);
  }
  SzView *empty = sz_view_column();
  assert(sz_lang_view_add_children(empty, NULL) == NULL);
  sz_view_free(empty);
}

static void assert_missing_aborts(void (*fn)(void), const char *expect);

static int virtual_builds, virtual_keys;
static SzView *virtual_first_field;

static void *virtual_test_key(void *value, void *env) {
  (void)env;
  virtual_keys++;
  SzList *fields = sz_adt_payload(value);
  sz_retain(fields->head);
  return fields->head;
}

static void *virtual_test_caption(void *value, void *env) {
  (void)env;
  SzList *fields = sz_adt_payload(value);
  sz_retain(fields->tail->head);
  return fields->tail->head;
}

static SzView *virtual_test_row(SzSignal *item, void *env) {
  (void)env;
  virtual_builds++;
  SzSignal *label = sz_signal_derive(item, virtual_test_caption, NULL, 2, NULL);
  SzSignalStr *draft = sz_signal_str("draft");
  SzView *field = sz_view_text_field(draft, "edit");
  void *value = sz_signal_read(item);
  SzList *fields = sz_adt_payload(value);
  if (sz_unbox_i64(fields->head) == 0) virtual_first_field = field;
  sz_release(value);
  SzView *body = sz_view_column();
  sz_view_add_child(body, sz_lang_view_bind_text(label));
  sz_view_add_child(body, field);
  return body;
}

static SzAdt *virtual_test_item(int64_t key, const char *label) {
  void *id = sz_box_i64(key);
  SzString *text = sz_string_from_cstr(label);
  SzList *tail = sz_list_cons(text, NULL);
  SzList *fields = sz_list_cons(id, tail);
  SzAdt *item = sz_adt_new(700, fields);
  sz_release(id);
  sz_release(text);
  sz_release(tail);
  sz_release(fields);
  return item;
}

static void test_virtual_list_window_and_identity(void) {
  size_t before_bytes, before_count, base_bytes, base_count, bytes, count;
  sz_alloc_stats(&before_bytes, &before_count);
  const int length = 100000;
  SzList *items = NULL;
  for (int i = length - 1; i >= 0; i--) {
    SzAdt *item = virtual_test_item(i, "row");
    SzList *next = sz_list_cons(item, items);
    sz_release(item);
    sz_release(items);
    items = next;
  }
  SzSignalList *source = sz_signal_list(items);
  sz_release(items);
  virtual_builds = virtual_keys = 0;
  virtual_first_field = NULL;
  SzView *root = sz_view_virtual_list(120, source, virtual_test_key, NULL,
                                     virtual_test_row, NULL);
  sz_view_layout(root, 320.f, 300.f, sz_theme_default());
  assert(virtual_keys == length);
  assert(virtual_builds <= 5);
  SzView *first = virtual_first_field;
  sz_view_focus(root, first);
  assert(sz_view_handle_key(root, "", "!", 0));
  assert(sz_view_has_focused_text_field(root));
  int built = virtual_builds;
  for (int i = 0; i < 30; i++)
    sz_view_layout(root, 320.f, 300.f, sz_theme_default());
  assert(virtual_builds == built && virtual_keys == length);
  SzAdt *updated = virtual_test_item(0, "updated");
  SzList *new_items = sz_list_cons(updated, sz_signal_list_get(source)->tail);
  sz_release(updated);
  sz_signal_list_set(source, new_items);
  sz_release(new_items);
  sz_view_layout(root, 320.f, 300.f, sz_theme_default());
  assert(virtual_builds == built);
  assert(sz_view_text_field_target(root) == first);
  SzString *dump = sz_view_a11y_dump(root);
  assert(strstr(sz_string_cstr(dump), "updated"));
  assert(strstr(sz_view_text_field_value(first), "!"));
  sz_release(dump);
  SzList *reversed = sz_list_reverse(sz_signal_list_get(source));
  sz_signal_list_set(source, reversed);
  sz_release(reversed);
  sz_view_layout(root, 320.f, 300.f, sz_theme_default());
  assert(sz_view_text_field_target(root) == first);
  assert(sz_view_has_focused_text_field(root));
  assert(sz_view_scroll_y(root) > 100000.f);
  sz_alloc_stats(&base_bytes, &base_count);
  int key_calls = virtual_keys;
  for (int i = 0; i < 40; i++) {
    sz_view_scroll_by(root, i % 2 ? 3000000.f : -3000000.f);
    sz_view_layout(root, 320.f, 300.f, sz_theme_default());
    SzView *fields[16];
    int visible = sz_view_collect_text_fields(root, fields, 16);
    assert(visible <= 5);
    assert(virtual_keys == key_calls);
    sz_alloc_stats(&bytes, &count);
    assert(bytes < base_bytes + 16384 && count < base_count + 100);
  }
  sz_signal_list_set(source, NULL);
  sz_view_layout(root, 320.f, 300.f, sz_theme_default());
  assert(sz_view_scroll_y(root) == 0.f);
  SzView *fields[2];
  assert(sz_view_collect_text_fields(root, fields, 2) == 0);
  sz_view_free(root);
  sz_signal_list_free(source);
  sz_alloc_stats(&bytes, &count);
  assert(bytes == before_bytes && count == before_count);
}

static void test_measured_list_window_and_identity(void) {
  size_t before_bytes, before_count, base_bytes, base_count, bytes, count;
  sz_alloc_stats(&before_bytes, &before_count);
  const int length = 100000;
  SzList *items = NULL;
  for (int i = length - 1; i >= 0; i--) {
    SzAdt *item = virtual_test_item(i, "row");
    SzList *next = sz_list_cons(item, items);
    sz_release(item);
    sz_release(items);
    items = next;
  }
  SzSignalList *source = sz_signal_list(items);
  sz_release(items);
  virtual_builds = virtual_keys = 0;
  virtual_first_field = NULL;
  SzView *root = sz_view_measured_list(80, source, virtual_test_key, NULL,
                                     virtual_test_row, NULL);
  sz_view_layout(root, 320.f, 300.f, sz_theme_default());
  assert(virtual_keys == length);
  assert(virtual_builds <= 8);
  SzView *first = virtual_first_field;
  sz_view_focus(root, first);
  assert(sz_view_handle_key(root, "", "!", 0));
  assert(sz_view_has_focused_text_field(root));
  int built = virtual_builds;
  for (int i = 0; i < 30; i++)
    sz_view_layout(root, 320.f, 300.f, sz_theme_default());
  assert(virtual_builds == built && virtual_keys == length);
  SzAdt *updated = virtual_test_item(0, "updated");
  SzList *new_items = sz_list_cons(updated, sz_signal_list_get(source)->tail);
  sz_release(updated);
  sz_signal_list_set(source, new_items);
  sz_release(new_items);
  sz_view_layout(root, 320.f, 300.f, sz_theme_default());
  assert(virtual_builds == built);
  assert(sz_view_text_field_target(root) == first);
  SzString *dump = sz_view_a11y_dump(root);
  assert(strstr(sz_string_cstr(dump), "updated"));
  assert(strstr(sz_view_text_field_value(first), "!"));
  sz_release(dump);
  SzList *reversed = sz_list_reverse(sz_signal_list_get(source));
  sz_signal_list_set(source, reversed);
  sz_release(reversed);
  sz_view_layout(root, 320.f, 300.f, sz_theme_default());
  assert(sz_view_text_field_target(root) == first);
  assert(sz_view_has_focused_text_field(root));
  assert(sz_view_scroll_y(root) > 100000.f);
  sz_alloc_stats(&base_bytes, &base_count);
  int key_calls = virtual_keys;
  for (int i = 0; i < 40; i++) {
    sz_view_scroll_by(root, i % 2 ? 3000000.f : -3000000.f);
    sz_view_layout(root, 320.f, 300.f, sz_theme_default());
    SzView *fields[16];
    int visible = sz_view_collect_text_fields(root, fields, 16);
    assert(visible <= 8);
    assert(virtual_keys == key_calls);
    sz_alloc_stats(&bytes, &count);
    assert(bytes < base_bytes + 32768 && count < base_count + 100);
  }
  sz_signal_list_set(source, NULL);
  sz_view_layout(root, 320.f, 300.f, sz_theme_default());
  assert(sz_view_scroll_y(root) == 0.f);
  SzView *fields[2];
  assert(sz_view_collect_text_fields(root, fields, 2) == 0);
  sz_view_free(root);
  sz_signal_list_free(source);
  sz_alloc_stats(&bytes, &count);
  assert(bytes == before_bytes && count == before_count);
}

static SzView *measured_test_row(SzSignal *item, void *env) {
  (void)env;
  virtual_builds++;
  void *value = sz_signal_read(item);
  SzList *fields = sz_adt_payload(value);
  int64_t key = sz_unbox_i64(fields->head);
  char label[32];
  snprintf(label, sizeof label, "row%lld", (long long)key);
  sz_release(value);
  return sz_view_sized(0, (int)(key % 3 + 1) * 20, sz_view_button(label, NULL, NULL));
}

static void test_measured_list_height_index(void) {
  size_t before_bytes, before_count, bytes, count;
  sz_alloc_stats(&before_bytes, &before_count);
  const int length = 100000;
  SzList *items = NULL;
  for (int i = length - 1; i >= 0; i--) {
    SzAdt *item = virtual_test_item(i, "row");
    SzList *next = sz_list_cons(item, items);
    sz_release(item);
    sz_release(items);
    items = next;
  }
  SzSignalList *source = sz_signal_list(items);
  sz_release(items);
  for (int estimate = 1; estimate <= 10000; estimate *= 10000) {
    virtual_builds = virtual_keys = 0;
    SzView *root = sz_view_measured_list(estimate, source, virtual_test_key, NULL,
                                       measured_test_row, NULL);
    sz_view_layout(root, 320, 300, sz_theme_default());
    assert(virtual_keys == length && virtual_builds < 18);
    SzView *buttons[32];
    int n = sz_view_collect_tap_targets(root, buttons, 32);
    assert(n > 3 && n < 18);
    for (int i = 1; i < n; i++) {
      SzRect previous = sz_view_frame(buttons[i - 1]);
      SzRect current = sz_view_frame(buttons[i]);
      assert(fabsf(previous.y + previous.h - current.y) < 0.01f);
    }
    sz_view_scroll_by(root, 18000);
    sz_view_layout(root, 320, 300, sz_theme_default());
    n = sz_view_collect_tap_targets(root, buttons, 32);
    int first = 0;
    while (first < n && sz_view_frame(buttons[first]).y + sz_view_frame(buttons[first]).h <= 12) first++;
    assert(first < n);
    SzView *anchor = buttons[first];
    SzRect frame = sz_view_frame(anchor);
    int builds = virtual_builds;
    for (int i = 0; i < 20; i++) sz_view_layout(root, 320, 300, sz_theme_default());
    assert(virtual_builds == builds && virtual_keys == length);
    assert(fabsf(sz_view_frame(anchor).y - frame.y) < 0.01f);
    sz_view_layout(root, 400, 300, sz_theme_default());
    assert(fabsf(sz_view_frame(anchor).y - frame.y) < 0.01f);
    sz_view_scroll_by(root, 2000000000.f);
    builds = virtual_builds;
    sz_view_layout(root, 400, 300, sz_theme_default());
    n = sz_view_collect_tap_targets(root, buttons, 32);
    assert(n > 3 && n < 18 && virtual_builds - builds < 18);
    assert(!strcmp(sz_view_a11y_label(buttons[n - 1]), "row99999"));
    frame = sz_view_frame(buttons[n - 1]);
    assert(fabsf(frame.y + frame.h - 288.f) < 0.01f);
    assert(sz_view_hit_test(root, frame.x + 5, frame.y + 5) == buttons[n - 1]);
    for (int i = 0; i < 20; i++) sz_view_layout(root, 400, 300, sz_theme_default());
    assert(fabsf(sz_view_frame(buttons[n - 1]).y - frame.y) < 0.01f);
    assert(sz_view_scroll_pan(root, 0, -1));
    sz_view_layout(root, 400, 300, sz_theme_default());
    assert(fabsf(sz_view_frame(buttons[n - 1]).y - frame.y - 1.f) < 0.01f);
    assert(sz_view_scroll_pan(root, 0, 1));
    sz_view_layout(root, 400, 300, sz_theme_default());
    assert(fabsf(sz_view_frame(buttons[n - 1]).y - frame.y) < 0.01f);
    sz_view_layout(root, 400, 0, sz_theme_default());
    sz_view_scroll_by(root, 2000000000.f);
    sz_view_layout(root, 400, 0, sz_theme_default());
    assert(isfinite(sz_view_scroll_y(root)));
    assert(sz_view_collect_tap_targets(root, buttons, 32) == 0);
    sz_view_layout(root, 400, 300, sz_theme_default());
    n = sz_view_collect_tap_targets(root, buttons, 32);
    assert(n > 3 && n < 18);
    assert(!strcmp(sz_view_a11y_label(buttons[n - 1]), "row99999"));
    sz_view_free(root);
  }
  sz_signal_list_free(source);
  sz_alloc_stats(&bytes, &count);
  assert(bytes == before_bytes && count == before_count);
}

static SzView *measured_wrap_row(SzSignal *item, void *env) {
  (void)env;
  virtual_builds++;
  void *value = sz_signal_read(item);
  SzList *fields = sz_adt_payload(value);
  char title[32];
  snprintf(title, sizeof title, "Item %lld", (long long)sz_unbox_i64(fields->head));
  sz_release(value);
  SzSignal *caption = sz_signal_derive(item, virtual_test_caption, NULL, 2, NULL);
  SzSignalStr *draft = sz_signal_str("");
  SzView *body = sz_view_column();
  sz_view_add_child(body, sz_view_button(title, NULL, NULL));
  sz_view_add_child(body, sz_lang_view_bind_text(caption));
  sz_view_add_child(body, sz_view_text_field(draft, "Note"));
  return body;
}

static void test_measured_list_wrap_and_focus(void) {
  SzList *items = NULL;
  for (int i = 99; i >= 0; i--) {
    SzAdt *item = virtual_test_item(i,
      "This row has enough text to wrap onto several lines. The available width controls its height. "
      "A measured list keeps its visible item in place when the window width changes.");
    SzList *next = sz_list_cons(item, items);
    sz_release(item);
    sz_release(items);
    items = next;
  }
  SzSignalList *source = sz_signal_list(items);
  sz_release(items);
  SzView *root = sz_view_measured_list(160, source, virtual_test_key, NULL, measured_wrap_row, NULL);
  sz_view_layout(root, 640, 300, sz_theme_default());
  sz_view_scroll_by(root, 400);
  sz_view_layout(root, 640, 300, sz_theme_default());
  SzView *buttons[16], *fields[16];
  int n = sz_view_collect_tap_targets(root, buttons, 16);
  assert(n >= 3 && n < 16);
  SzView *anchor = buttons[1];
  SzRect frame = sz_view_frame(anchor);
  sz_view_layout(root, 240, 300, sz_theme_default());
  assert(fabsf(sz_view_frame(anchor).y - frame.y) < 0.01f);
  n = sz_view_collect_text_fields(root, fields, 16);
  assert(n >= 2 && n < 8);
  SzView *focused = fields[1];
  sz_view_focus(root, focused);
  sz_view_handle_key(root, "", "kept", 0);
  SzList *reversed = sz_list_reverse(sz_signal_list_get(source));
  sz_signal_list_set(source, reversed);
  sz_release(reversed);
  sz_view_layout(root, 240, 300, sz_theme_default());
  assert(sz_view_focused(root) == focused);
  assert(!strcmp(sz_view_text_field_value(focused), "kept"));
  frame = sz_view_frame(focused);
  assert(frame.y >= 12.f - 0.01f && frame.y + frame.h <= 288.f + 0.01f);
  SzTheme scaled = *sz_theme_default();
  scaled.font_px *= 2;
  scaled.pad *= 2;
  scaled.gap *= 2;
  scaled.control_h *= 2;
  scaled.px_scale = 2;
  sz_view_layout(root, 480, 600, &scaled);
  assert(sz_view_focused(root) == focused);
  frame = sz_view_frame(focused);
  assert(frame.y >= 24.f - 0.01f && frame.y + frame.h <= 576.f + 0.01f);
  SzTheme *large = sz_theme_metrics(NULL, 22, 10, 16, 12, 52);
  sz_view_layout(root, 360, 600, large);
  assert(sz_view_focused(root) == focused);
  assert(!strcmp(sz_view_text_field_value(focused), "kept"));
  frame = sz_view_frame(focused);
  assert(frame.h == 52.f && frame.y >= 16.f - .01f && frame.y + frame.h <= 584.f + .01f);
  SzTheme *spaced = sz_theme_metrics(large, 22, 20, 16, 12, 52);
  sz_view_layout(root, 360, 600, spaced);
  assert(sz_view_focused(root) == focused);
  frame = sz_view_frame(focused);
  assert(frame.y >= 16.f - .01f && frame.y + frame.h <= 584.f + .01f);
  sz_release(spaced);
  sz_release(large);
  SzAdt *short_item = virtual_test_item(999, "short");
  SzList *one = sz_list_cons(short_item, NULL);
  sz_signal_list_set(source, one);
  sz_release(short_item);
  sz_release(one);
  sz_view_layout(root, 320, 300, sz_theme_default());
  assert(sz_view_scroll_y(root) == 0.f);
  assert(sz_view_collect_text_fields(root, fields, 16) == 1);
  frame = sz_view_frame(fields[0]);
  assert(sz_view_hit_test(root, frame.x + 5, frame.y + 5) == fields[0]);
  sz_view_free(root);
  sz_signal_list_free(source);
}

static void *virtual_duplicate_key(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_box_i64(0);
}

static void virtual_list_duplicate_keys(void) {
  SzAdt *a = virtual_test_item(0, "first");
  SzAdt *b = virtual_test_item(1, "second");
  SzList *tail = sz_list_cons(b, NULL);
  SzList *items = sz_list_cons(a, tail);
  sz_release(a);
  sz_release(b);
  sz_release(tail);
  SzSignalList *source = sz_signal_list(items);
  sz_release(items);
  SzView *root = sz_view_virtual_list(120, source, virtual_duplicate_key, NULL,
                                     virtual_test_row, NULL);
  sz_view_layout(root, 320.f, 300.f, sz_theme_default());
}

static void test_virtual_list_duplicate_keys(void) {
  assert_missing_aborts(virtual_list_duplicate_keys, "requires unique keys");
}

static void test_virtual_list_scale_and_resize(void) {
  SzList *items = NULL;
  for (int i = 999; i >= 0; i--) {
    SzAdt *item = virtual_test_item(i, "row");
    SzList *next = sz_list_cons(item, items);
    sz_release(item);
    sz_release(items);
    items = next;
  }
  SzSignalList *source = sz_signal_list(items);
  sz_release(items);
  SzView *root = sz_view_virtual_list(120, source, virtual_test_key, NULL,
                                     virtual_test_row, NULL);
  sz_view_layout(root, 320.f, 300.f, sz_theme_default());
  SzView *before[16], *after[16];
  int initial = sz_view_collect_text_fields(root, before, 16);
  SzRect first = sz_view_frame(before[0]);
  int builds = virtual_builds;
  SzTheme theme = *sz_theme_default();
  theme.px_scale = 2.f;
  theme.pad *= 2.f;
  theme.gap *= 2.f;
  theme.font_px *= 2.f;
  theme.control_h *= 2.f;
  sz_view_layout(root, 640.f, 600.f, &theme);
  assert(sz_view_collect_text_fields(root, after, 16) == initial);
  assert(virtual_builds == builds);
  for (int i = 0; i < initial; i++) assert(before[i] == after[i]);
  SzRect scaled = sz_view_frame(after[0]);
  assert(fabsf(scaled.x - first.x * 2.f) < 0.01f);
  assert(fabsf(scaled.y - first.y * 2.f) < 0.01f);
  assert(fabsf(scaled.h - first.h * 2.f) < 0.01f);
  sz_view_layout(root, 640.f, 960.f, &theme);
  int enlarged = sz_view_collect_text_fields(root, after, 16);
  assert(enlarged > initial && enlarged <= 7);
  for (int i = 0; i < initial; i++) assert(before[i] == after[i]);
  sz_view_free(root);
  sz_signal_list_free(source);
}

static void test_virtual_list_pointer_removal(void) {
  SzAdt *item = virtual_test_item(0, "row");
  SzList *items = sz_list_cons(item, NULL);
  sz_release(item);
  SzSignalList *source = sz_signal_list(items);
  sz_release(items);
  SzView *root = sz_view_virtual_list(120, source, virtual_test_key, NULL,
                                     virtual_test_row, NULL);
  SzUiConfig cfg = {0};
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 320;
  cfg.height = 300;
  cfg.scale = 1;
  SzUiSession *session = sz_ui_mount(&cfg, root);
  assert(sz_ui_pump_sync(session));
  SzView *field = virtual_first_field;
  uintptr_t id = sz_view_id(field);
  SzRect frame = sz_view_frame(field);
  SzInputEvent event;
  memset(&event, 0, sizeof(event));
  event.kind = SZ_INPUT_POINTER;
  event.pointer_button = 1;
  event.pointer_phase = SZ_POINTER_DOWN;
  event.x = frame.x + 2.f;
  event.y = frame.y + 2.f;
  assert(sz_ui_inject_sync(session, &event));
  sz_signal_list_set(source, NULL);
  assert(sz_ui_pump_sync(session));
  assert(sz_view_find(root, id) == NULL);
  event.pointer_phase = SZ_POINTER_MOVE;
  event.x += 5.f;
  assert(sz_ui_inject_sync(session, &event));
  event.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_inject_sync(session, &event));
  assert(!sz_view_has_focused_text_field(root));
  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_list_free(source);
}

static void test_adaptive_width_and_input(void) {
  size_t before_bytes, before_count, after_bytes, after_count;
  sz_alloc_stats(&before_bytes, &before_count);
  for (int scale = 1; scale <= 2; scale++) {
    SzTheme theme = *sz_theme_default();
    theme.px_scale = (float)scale;
    SzView *compact = sz_view_button("compact", NULL, NULL);
    SzView *wide = sz_view_button("wide", NULL, NULL);
    SzView *adaptive = sz_view_adaptive(200, compact, wide);
    for (int width = 1; width <= 600; width++) {
      sz_view_layout(adaptive, (float)width, 100.f * scale, &theme);
      SzView *active = width < 200 * scale ? compact : wide;
      SzView *inactive = active == compact ? wide : compact;
      SzRect frame = sz_view_frame(active);
      assert(frame.w > 0.f && frame.w <= width);
      assert(sz_view_frame(inactive).w == 0.f);
      assert(sz_view_hit_test(adaptive, frame.x + frame.w / 2.f,
                              frame.y + frame.h / 2.f) == active);
      SzString *dump = sz_view_a11y_dump(adaptive);
      const char *text = sz_string_cstr(dump);
      assert(strstr(text, active == compact ? "button:compact" : "button:wide"));
      assert(!strstr(text, active == compact ? "button:wide" : "button:compact"));
      sz_string_free(dump);
    }
    sz_view_layout(adaptive, 0.f, 0.f, &theme);
    assert(sz_view_frame(wide).w > 0.f && sz_view_frame(compact).w == 0.f);
    SzView *nested = sz_view_max_size(199, 0, adaptive);
    sz_view_layout(nested, 1000.f, 300.f, &theme);
    assert(sz_view_frame(compact).w > 0.f && sz_view_frame(wide).w == 0.f);
    sz_view_free(nested);
  }
  for (int container = 0; container < 4; container++) {
    SzView *compact = sz_view_text("compact");
    SzView *wide = sz_view_text("wide");
    SzView *adaptive = sz_view_adaptive(200, compact, wide);
    SzView *parent;
    if (container == 0) parent = sz_view_padding(20, adaptive);
    else if (container == 1) parent = sz_view_card(adaptive);
    else {
      parent = container == 2 ? sz_view_column() : sz_view_wrap();
      sz_view_add_child(parent, adaptive);
    }
    for (int width = 1; width <= 40; width++) {
      sz_view_layout(parent, (float)width, 100.f, sz_theme_default());
      assert(sz_view_frame(compact).w > 0.f);
      assert(sz_view_frame(wide).w == 0.f);
    }
    sz_view_free(parent);
  }
  SzView *small = sz_view_adaptive(INT64_MIN, sz_view_text("compact"), sz_view_text("wide"));
  sz_view_layout(small, 1.f, 0.f, sz_theme_default());
  SzString *dump = sz_view_a11y_dump(small);
  assert(strstr(sz_string_cstr(dump), "wide"));
  sz_string_free(dump);
  sz_view_free(small);
  SzView *large = sz_view_adaptive(INT64_MAX, sz_view_text("compact"), sz_view_text("wide"));
  sz_view_layout(large, 1000.f, 0.f, sz_theme_default());
  dump = sz_view_a11y_dump(large);
  assert(strstr(sz_string_cstr(dump), "compact"));
  sz_string_free(dump);
  sz_view_free(large);
  sz_alloc_stats(&after_bytes, &after_count);
  assert(before_bytes == after_bytes && before_count == after_count);
}

static void test_adaptive_focus_and_shared_state(void) {
  SzSignalStr *draft = sz_signal_str("shared");
  SzView *compact = sz_view_text_field(draft, "compact");
  SzView *wide = sz_view_text_field(draft, "wide");
  SzView *root = sz_view_adaptive(200, compact, wide);
  SzView *fields[2];
  sz_view_layout(root, 400.f, 100.f, sz_theme_default());
  assert(sz_view_collect_text_fields(root, fields, 2) == 1 && fields[0] == wide);
  sz_view_focus(root, compact);
  assert(!sz_view_has_focused_text_field(root));
  assert(!sz_view_activate(root, compact, 1.f, 1.f));
  sz_view_focus(root, wide);
  assert(sz_view_has_focused_text_field(root));
  sz_view_layout(root, 100.f, 100.f, sz_theme_default());
  assert(!sz_view_has_focused_text_field(root));
  assert(sz_view_collect_text_fields(root, fields, 2) == 1 && fields[0] == compact);
  assert(sz_view_text_field_target(root) == compact);
  sz_view_focus(root, compact);
  assert(sz_view_handle_key(root, "", "!", 0));
  assert(strstr(sz_signal_str_get(draft), "!"));
  sz_view_layout(root, 400.f, 100.f, sz_theme_default());
  assert(!sz_view_has_focused_text_field(root));
  assert(sz_view_text_field_target(root) == wide);
  assert(strstr(sz_signal_str_get(draft), "!"));
  sz_view_free(root);
  sz_signal_str_free(draft);
  SzSignalStr *buffer = sz_signal_str("code");
  SzView *compact_editor = sz_view_editor(buffer);
  SzView *wide_editor = sz_view_editor(buffer);
  root = sz_view_adaptive(200, compact_editor, wide_editor);
  sz_view_layout(root, 100.f, 200.f, sz_theme_default());
  assert(sz_view_first_editor(root) == compact_editor);
  sz_view_layout(root, 400.f, 200.f, sz_theme_default());
  assert(sz_view_first_editor(root) == wide_editor);
  sz_view_free(root);
  sz_signal_str_free(buffer);
}

static void test_auto_grid_widths(void) {
  for (int scale = 1; scale <= 2; scale++) {
    for (int gap = 0; gap <= 13; gap += 13) {
      SzTheme theme = *sz_theme_default();
      theme.px_scale = (float)scale;
      SzView *g = sz_view_auto_grid(80);
      SzView *root = sz_view_gap(gap, g);
      SzView *cells[7];
      for (int i = 0; i < 7; i++) {
        cells[i] = sz_view_sized(10000, 10 + i, sz_view_text("cell"));
        sz_view_add_child(g, cells[i]);
      }
      /* Check the packing rules across every integer width and each resize. */
      for (int width = 1; width <= 900; width++) {
        sz_view_layout(root, (float)width, 0.f, &theme);
        SzRect first = sz_view_frame(cells[0]);
        int columns = 1;
        while (columns < 7 && fabsf(sz_view_frame(cells[columns]).y - first.y) < 0.01f)
          columns++;
        float spacing = (float)(gap * scale);
        float inner = (float)width - 2.f * first.x;
        assert(fabsf(sz_view_frame(g).w - (float)width) < 0.01f);
        assert(columns == 1 || first.w >= 80.f * scale - 0.01f);
        assert(columns == 7 || inner < (columns + 1) * 80.f * scale + columns * spacing);
        for (int i = 0; i < 7; i++) {
          SzRect cell = sz_view_frame(cells[i]);
          assert(cell.w > 0.f && isfinite(cell.h));
          assert(fabsf(cell.w - first.w) < 0.01f);
          assert(cell.x >= 0.f && cell.x + cell.w <= width + 0.01f);
          if (i % columns != 0) {
            SzRect previous = sz_view_frame(cells[i - 1]);
            assert(fabsf(cell.y - previous.y) < 0.01f);
            assert(fabsf(cell.x - previous.x - previous.w - spacing) < 0.01f);
          } else if (i > 0) {
            assert(cell.y > sz_view_frame(cells[i - 1]).y);
          }
        }
      }
      sz_view_free(root);
    }
  }
}

static void test_auto_grid_visibility_and_input(void) {
  SzSignalInt *shown = sz_signal_int(0);
  SzView *g = sz_view_auto_grid(80);
  SzView *a = sz_view_button("A", NULL, NULL);
  SzView *hidden = sz_view_show_when(shown, 1, sz_view_button("hidden", NULL, NULL));
  SzView *b = sz_view_button("B", NULL, NULL);
  sz_view_add_child(g, a);
  sz_view_add_child(g, hidden);
  sz_view_add_child(g, b);
  sz_view_layout(g, 220.f, 200.f, sz_theme_default());
  SzRect af = sz_view_frame(a), bf = sz_view_frame(b);
  assert(fabsf(af.y - bf.y) < 0.01f);
  assert(sz_view_frame(hidden).w == 0.f);
  assert(sz_view_hit_test(g, bf.x + 2.f, bf.y + 2.f) == b);
  sz_signal_int_set(shown, 1);
  sz_view_layout(g, 220.f, 200.f, sz_theme_default());
  assert(sz_view_frame(b).y > sz_view_frame(a).y);
  sz_view_layout(g, 500.f, 200.f, sz_theme_default());
  assert(fabsf(sz_view_frame(a).y - sz_view_frame(b).y) < 0.01f);
  SzString *dump = sz_view_a11y_dump(g);
  assert(strstr(sz_string_cstr(dump), "button:B"));
  sz_string_free(dump);
  sz_view_layout(g, 0.f, 0.f, sz_theme_default());
  assert(sz_view_frame(b).y > sz_view_frame(hidden).y);
  sz_view_free(g);
  sz_signal_int_free(shown);

  g = sz_lang_view_auto_grid(INT64_MAX);
  sz_view_add_child(g, sz_view_text("one"));
  sz_view_add_child(g, sz_view_text("two"));
  sz_view_layout(g, 500.f, 0.f, sz_theme_default());
  sz_view_free(g);
  g = sz_lang_view_auto_grid(INT64_MIN);
  sz_view_layout(g, 500.f, 0.f, sz_theme_default());
  sz_view_free(g);
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
  SzUiConfig cfg = {0};
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
  ev.x = f.x + 8.f - 20.f; /* finger left -> content left */
  assert(sz_ui_inject_sync(session, &ev));
  ev.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_view_scroll_x(scroll) > x0);
  assert(sz_view_scroll_y(scroll) == 0.f);
  sz_ui_unmount(session);
}

static void test_scroll_h_inject_script(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *scroll, *row, *left, *right;
  const char *path = "/tmp/scuzz_ui_inject_scroll_h.json";
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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"scroll\",\"dy\":40}]}");
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

static void test_expanded_weights(void) {
  const int64_t weights[][2] = {{1, 2}, {3, 7}, {0, -4},
                               {INT64_MIN, 1}, {INT64_MAX, INT64_MAX},
                               {INT64_MAX, INT64_MAX / 2}};
  for (int axis = 0; axis < 2; axis++) {
    for (int scale = 1; scale <= 2; scale++) {
      for (unsigned k = 0; k < sizeof weights / sizeof weights[0]; k++) {
        SzTheme theme = *sz_theme_default();
        theme.px_scale = (float)scale;
        SzSignalInt *shown = sz_signal_int(1);
        SzView *root = axis ? sz_view_column() : sz_view_row();
        SzView *fixed = sz_view_sized(20, 20, sz_view_text("fixed"));
        SzView *a = sz_view_button("A", NULL, NULL);
        SzView *b = sz_view_button("B", NULL, NULL);
        SzView *ea = sz_view_expanded(weights[k][0], a);
        SzView *eb = sz_view_stretch(sz_view_expanded(weights[k][1], b));
        sz_view_show_when(shown, 1, eb);
        sz_view_add_child(root, fixed);
        sz_view_add_child(root, ea);
        sz_view_add_child(root, eb);
        double wa = weights[k][0] < 1 ? 1.0 : (double)weights[k][0];
        double wb = weights[k][1] < 1 ? 1.0 : (double)weights[k][1];
        for (int size = 240; size <= 900; size += 11) {
          sz_view_layout(root, (float)size, (float)size, &theme);
          SzRect f = sz_view_frame(fixed), af = sz_view_frame(a), bf = sz_view_frame(b);
          float fixed_size = axis ? f.h : f.w;
          float free_space = size - 2.f * theme.pad - fixed_size - 2.f * theme.gap;
          assert(fabsf((axis ? af.h : af.w) - (float)(free_space * wa / (wa + wb))) < 0.01f);
          assert(fabsf((axis ? bf.h : bf.w) - (float)(free_space * wb / (wa + wb))) < 0.01f);
          assert(fabsf((axis ? bf.y - af.y - af.h : bf.x - af.x - af.w) - theme.gap) < 0.01f);
          assert(sz_view_hit_test(root, bf.x + bf.w / 2.f, bf.y + bf.h / 2.f) == b);
          sz_signal_int_set(shown, 0);
          sz_view_layout(root, (float)size, (float)size, &theme);
          af = sz_view_frame(a);
          free_space = size - 2.f * theme.pad - fixed_size - theme.gap;
          assert(fabsf((axis ? af.h : af.w) - free_space) < 0.01f);
          assert(sz_view_frame(eb).w == 0.f && sz_view_frame(eb).h == 0.f);
          sz_signal_int_set(shown, 1);
        }
        sz_view_free(root);
        sz_signal_int_free(shown);
      }
    }
  }
}

static void test_expanded_row_text_fills_width(void) {
  SzView *row, *left, *mid, *right, *exp;
  const SzTheme *theme = sz_theme_default();
  float max_w = 320.f;
  float leftover;

  row = sz_view_row();
  left = sz_view_button("L", NULL, NULL);
  mid = sz_view_text("mid");
  exp = sz_view_expanded(1, mid);
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
  exp = sz_view_expanded(1, sz_view_text("flex"));
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
  exp = sz_view_expanded(1, body);
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
  exp = sz_view_expanded(1, wrap);
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
  exp = sz_view_expanded(1, sz_view_text("flex"));
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
  SzUiConfig cfg = {0};
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

  /* Soft keyboard: tap TextField -> keyboard visible. */
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
  ev.y = sz_view_frame(scroll).y + 8.f - 20.f; /* finger up -> content up */
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

static void test_gap_paints_control_label_at_layout_gap(void) {
  SzSignalInt *on;
  SzView *root;
  SkSurface *surf;
  SkCanvas *canvas;
  const uint8_t *px;
  size_t n = 0;
  const SzTheme *theme = sz_theme_default();
  float box = theme->font_px + 4.f;
  int label_x;
  int theme_x;
  int y;
  int saw;

  on = sz_signal_int(1);
  /* Gap 32 vs theme gap 8: the label must paint where layout put it. */
  root = sz_view_sized(120, 40, sz_view_gap(32, sz_view_checkbox(on, "||||")));
  surf = sk_surface_make_raster_n32_premul(160, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 160, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 160 * 80 * 4);
  label_x = (int)(box + 32.f);
  theme_x = (int)(box + 8.f);
  saw = 0;
  for (y = 14; y < 22; y++) {
    if (row_has_dark(px, 160, y, label_x + 1, label_x + 9))
      saw = 1;
    /* Nothing may paint at the theme-gap offset. */
    assert(!row_has_dark(px, 160, y, theme_x + 1, theme_x + 9));
  }
  assert(saw);
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(on);
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

  wrap = sz_view_font_size(24, sz_view_text("Hi"));
  sz_view_layout(wrap, 1000.f, 200.f, theme);
  big_h = sz_view_frame(wrap).h;
  big_w = sz_view_frame(wrap).w;
  assert(fabsf(big_h - (24.f + 6.f)) < 0.5f);
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
  assert(px_rgb(px, 80, 50, 20, 0xF3, 0xEF, 0xE3));
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
  assert(px_rgb(px, 80, 0, 0, 0xF3, 0xEF, 0xE3));
  assert(px_rgb(px, 80, 39, 0, 0xF3, 0xEF, 0xE3));
  assert(px_rgb(px, 80, 0, 39, 0xF3, 0xEF, 0xE3));
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
  assert(px_rgb(px, 80, 0, 0, 0xF3, 0xEF, 0xE3));
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
  assert(px_rgb(px, 80, 0, 0, 0xF3, 0xEF, 0xE3));
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

/* Check the tap array separately from the accessibility tree. */
static void check_control_tap_dump_count(SzView *control, const char *desc,
                                         const char *label, int count) {
  SzUiConfig cfg = {0};
  SzView *root = sz_view_column();
  SzUiSession *session;
  SzAdt *parsed, *taps, *tap;
  SzList *hold;
  char path[] = "/tmp/scuzz_ui_control_tap.XXXXXX";
  int fd = mkstemp(path);
  char *dump;
  assert(fd >= 0);
  close(fd);
  sz_view_add_child(root, control);
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 80;
  cfg.scale = 1.f;
  session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_session_write_dump(session, path));
  dump = slurp_cstr(path);
  assert(strstr(dump, desc));
  free(dump);
  parsed = json_slurp_parse(path);
  taps = json_doc_key((SzAdt *)sz_adt_payload(parsed), "taps");
  assert(taps && sz_json_is_arr(taps));
  tap = json_arr_head(taps, &hold);
  assert(sz_list_len(hold) == count);
  assert(tap && json_str_eq(tap, "label", label));
  assert(json_doc_int(tap, "i", -1) == 0);
  sz_release(hold);
  sz_release(parsed);
  sz_ui_unmount(session);
  remove(path);
}

static void check_control_tap_dump(SzView *control, const char *desc, const char *label) {
  check_control_tap_dump_count(control, desc, label, 1);
}

static void test_control_tap_dumps(void) {
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_checkbox(sig, "Done"), "checkbox:Done=0", "Done");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_switch(sig, "On"), "switch:On=0", "On");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_chip(sig, "Pin"), "chip:Pin=0", "Pin");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_checkbox_list_tile(sig, "Star"), "checktile:Star=0", "Star");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_switch_list_tile(sig, "Quiet"), "switchtile:Quiet=0", "Quiet");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_radio_list_tile(sig, 1, "Night"), "radiotile:Night=0", "Night");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump_count(sz_view_segmented(sig, "Layout", (const char *[]){"List", "Grid"}, 2), "choicechip:List=1", "List", 2);
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_filter_chip(sig, "Tag"), "filterchip:Tag=0", "Tag");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(1);
    check_control_tap_dump(sz_view_choice_chip(sig, 0, "Day"), "choicechip:Day=0", "Day");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_input_chip(sig, "In"), "inputchip:In=0", "In");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_radio(sig, 1, "On"), "radio:On=0", "On");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(40);
    check_control_tap_dump(sz_view_slider(sig, "Level", 0, 100, 1), "slider:Level=40", "Level");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_icon_button("i", counter_tap, sig), "iconbutton:i", "i");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_fab("+", counter_tap, sig), "fab:+", "+");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_outlined_button("Edit", counter_tap, sig), "outlined:Edit", "Edit");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_text_button("Open", counter_tap, sig), "textbutton:Open", "Open");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_action_chip("Go", counter_tap, sig), "actionchip:Go", "Go");
    sz_signal_int_free(sig);
  }
  {
    SzSignalInt *sig = sz_signal_int(0);
    check_control_tap_dump(sz_view_ink_well("face", counter_tap, sig, sz_view_avatar("S")), "inkwell:face", "face");
    sz_signal_int_free(sig);
  }
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

static void test_a11y_dump_long_labels(void) {
  SzView *box;
  SzView *txt;
  SzSignalInt *sig;
  SzSignalStr *strsig;
  SzString *dump;
  char *long_label;
  size_t i;

  /* Labels past the old 256-byte dump buffer survive in full. */
  long_label = (char *)malloc(1200);
  assert(long_label);
  for (i = 0; i < 1100; i++)
    long_label[i] = (char)('a' + (int)(i % 26));
  long_label[1100] = '\0';

  sig = sz_signal_int(1);
  box = sz_view_checkbox(sig, long_label);
  dump = sz_view_a11y_dump(box);
  assert(strstr(sz_string_cstr(dump), long_label) != NULL);
  assert(strstr(sz_string_cstr(dump), "=1\n") != NULL);
  sz_string_free(dump);
  sz_view_free(box);
  sz_signal_int_free(sig);

  strsig = sz_signal_str(long_label);
  txt = sz_view_text_signal_str(strsig);
  dump = sz_view_a11y_dump(txt);
  assert(strstr(sz_string_cstr(dump), long_label) != NULL);
  sz_string_free(dump);
  sz_view_free(txt);
  sz_signal_str_free(strsig);
  free(long_label);
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

static SzView *bounded_label_control(int kind, SzSignalInt *sig, const char *label) {
  switch (kind) {
  case 0: return sz_view_checkbox(sig, label);
  case 1: return sz_view_radio(sig, 1, label);
  case 2: return sz_view_switch(sig, label);
  case 3: return sz_view_checkbox_list_tile(sig, label);
  case 4: return sz_view_radio_list_tile(sig, 1, label);
  case 5: return sz_view_switch_list_tile(sig, label);
  case 6: return sz_view_list_tile(label, sz_view_text("OK"));
  default: return sz_view_expansion_tile(sig, label, sz_view_text("Details"));
  }
}

static void test_control_labels_stay_in_bounds(void) {
  const char *label = "A long selection label with readable words and UTF-8 caf\xc3\xa9";
  for (int scale = 1; scale <= 2; scale++) {
    SzTheme theme = *sz_theme_default();
    theme.px_scale = (float)scale;
    theme.font_px *= scale;
    theme.control_h *= scale;
    theme.pad *= scale;
    theme.gap *= scale;
    for (int kind = 0; kind < 8; kind++) {
      for (int width = 64; width <= 256; width *= 2) {
        SzSignalInt *sig = sz_signal_int(0);
        SzView *control = bounded_label_control(kind, sig, label);
        SzView *root = sz_view_sized(width, 40, control);
        SzView *empty = sz_view_sized(width, 40, bounded_label_control(kind, sig, ""));
        int canvas_w = 360 * scale;
        SkSurface *surf = sk_surface_make_raster_n32_premul(canvas_w, 48 * scale);
        SkSurface *reference = sk_surface_make_raster_n32_premul(canvas_w, 48 * scale);
        size_t bytes, reference_bytes;
        assert(sz_view_paint(root, sk_surface_get_canvas(surf), canvas_w, 48 * scale, &theme));
        assert(sz_view_paint(empty, sk_surface_get_canvas(reference), canvas_w, 48 * scale, &theme));
        const uint8_t *pixels = sk_surface_peek_pixels(surf, &bytes);
        const uint8_t *base = sk_surface_peek_pixels(reference, &reference_bytes);
        assert(bytes == reference_bytes);
        int end = width * scale;
        if (kind == 5) end -= 48 * scale;
        if (kind == 6) end -= (int)(theme.pad + sk_font_measure_string("OK", theme.font_px));
        if (kind == 7) end -= (int)(theme.pad + sk_font_measure_string(">", theme.font_px));
        /* Labels do not cover trailing controls or paint outside the frame. */
        for (int y = 0; y < 48 * scale; y++)
          for (int x = end; x < canvas_w; x++)
            assert(memcmp(pixels + (y * canvas_w + x) * 4,
                          base + (y * canvas_w + x) * 4, 4) == 0);
        SzString *a11y = sz_view_a11y_dump(control);
        assert(strstr(sz_string_cstr(a11y), label) != NULL);
        sz_string_free(a11y);
        sk_surface_unref(surf);
        sk_surface_unref(reference);
        sz_view_free(root);
        sz_view_free(empty);
        sz_signal_int_free(sig);
      }
    }
  }
}

static void test_selection_marks_without_color(void) {
  for (int scale = 1; scale <= 2; scale++) {
    SzTheme theme = *sz_theme_default();
    theme.px_scale = (float)scale;
    theme.font_px *= scale;
    theme.control_h *= scale;
    theme.pad *= scale;
    theme.gap *= scale;
    theme.primary = theme.background = theme.surface;
    for (int kind = 0; kind < 4; kind++) {
      SzSignalInt *sig = sz_signal_int(0);
      SzView *view = kind == 0 ? sz_view_checkbox(sig, "") :
                     kind == 1 ? sz_view_radio(sig, 1, "") :
                     kind == 2 ? sz_view_checkbox_list_tile(sig, "") :
                                 sz_view_radio_list_tile(sig, 1, "");
      SkSurface *surf = sk_surface_make_raster_n32_premul(240, 100);
      SkCanvas *canvas = sk_surface_get_canvas(surf);
      size_t bytes;
      const uint8_t *pixels;
      int x = (9 + (kind >= 2 ? 12 : 0)) * scale;
      int y = 20 * scale;
      assert(sz_view_paint(view, canvas, 240, 100, &theme));
      pixels = sk_surface_peek_pixels(surf, &bytes);
      assert(px_rgb(pixels, 240, x, y, 0xFF, 0xFC, 0xF4));
      sz_signal_int_set(sig, 1);
      assert(sz_view_paint(view, canvas, 240, 100, &theme));
      pixels = sk_surface_peek_pixels(surf, &bytes);
      assert(px_rgb(pixels, 240, x, y, 0x24, 0x23, 0x1F));
      sk_surface_unref(surf);
      sz_view_free(view);
      sz_signal_int_free(sig);
    }
  }
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
  assert(px_rgb(px, 80, 6, 16, 0xF3, 0xEF, 0xE3));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 6, 16, 0xE8, 0xEF, 0x48));
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
  SzUiConfig cfg = {0};
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
  /* Left of track: off thumb is ink, on fill is primary. */
  assert(px_rgb(px, 80, 6, 16, 0x24, 0x23, 0x1F));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 6, 16, 0xE8, 0xEF, 0x48));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
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

static void test_selection_tabs_without_color(void) {
  for (int scale = 1; scale <= 2; scale++) {
    SzTheme theme = *sz_theme_default();
    theme.px_scale = (float)scale;
    theme.font_px *= scale;
    theme.control_h *= scale;
    theme.pad *= scale;
    theme.gap *= scale;
    theme.primary = theme.surface;
    for (int kind = 0; kind < 5; kind++) {
      SzSignalInt *sig = sz_signal_int(0);
      SzView *view = kind == 0 ? sz_view_chip(sig, "Label") :
                     kind == 1 ? sz_view_choice_chip(sig, 1, "Label") :
                     kind == 2 ? sz_view_filter_chip(sig, "Label") :
                     kind == 3 ? sz_view_input_chip(sig, "Label") :
                                 sz_view_segmented(sig, "Direction", (const char *[]){"Left", "Right"}, 2);
      SkSurface *surf = sk_surface_make_raster_n32_premul(400, 100);
      SkCanvas *canvas = sk_surface_get_canvas(surf);
      size_t bytes;
      const uint8_t *pixels;
      SzRect f;
      assert(sz_view_paint(view, canvas, 400, 100, &theme));
      f = sz_view_frame(view);
      int x = (int)(f.x + f.w * (kind == 4 ? 0.75f : 0.5f));
      int y = (int)(f.y + f.h - 3.f * scale);
      int cx = (int)(f.x + f.w - 3.f * scale);
      int cy = (int)(f.y + 3.f * scale);
      pixels = sk_surface_peek_pixels(surf, &bytes);
      assert(px_rgb(pixels, 400, x, y, 0xFF, 0xFC, 0xF4));
      assert(px_rgb(pixels, 400, cx, cy, 0xFF, 0xFC, 0xF4));
      sz_signal_int_set(sig, 1);
      assert(sz_view_paint(view, canvas, 400, 100, &theme));
      pixels = sk_surface_peek_pixels(surf, &bytes);
      assert(px_rgb(pixels, 400, x, y, 0x24, 0x23, 0x1F));
      assert(px_rgb(pixels, 400, cx, cy, 0x24, 0x23, 0x1F));
      sk_surface_unref(surf);
      sz_view_free(view);
      sz_signal_int_free(sig);
    }
  }
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFC, 0xF4));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xE8, 0xEF, 0x48));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFC, 0xF4));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_list_tile_trailing_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(fabsf(f.w - cf.w - 44.f) < 0.5f);
  assert(fabsf(f.h - cf.h) < 0.5f);
  assert(fabsf(f.h - theme->control_h) < 0.5f);
  sz_view_free(badge);
  sz_signal_int_free(sig);
  sz_signal_int_free(pin);
}

static void test_badge_ticket_counts_and_scale(void) {
  const int64_t counts[] = {-1, 0, 9, 99, 100, INT64_MAX};
  for (int scale = 1; scale <= 2; scale++) {
    SzTheme theme = *sz_theme_default();
    theme.px_scale = (float)scale;
    theme.font_px *= scale;
    theme.control_h *= scale;
    theme.pad *= scale;
    theme.gap *= scale;
    for (int width = 64; width <= 256; width *= 2) {
      SzSignalInt *count = sz_signal_int(0);
      SzSignalInt *pin = sz_signal_int(0);
      SzView *child = sz_view_chip(pin, "Pin");
      SzView *badge = sz_view_badge(count, child);
      SzView *root = sz_view_sized(width, 40, badge);
      SkSurface *surface = sk_surface_make_raster_n32_premul(300 * scale, 60 * scale);
      float previous_width = 0.f;
      for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        sz_signal_int_set(count, counts[i]);
        assert(sz_view_paint(root, sk_surface_get_canvas(surface), 300 * scale, 60 * scale, &theme));
        SzRect frame = sz_view_frame(badge);
        SzRect control = sz_view_frame(child);
        assert(frame.w > control.w);
        assert(i == 0 || frame.w == previous_width);
        previous_width = frame.w;
        size_t bytes;
        const uint8_t *pixels = sk_surface_peek_pixels(surface, &bytes);
        for (int y = 0; y < 60 * scale; y++)
          for (int x = width * scale; x < 300 * scale; x++)
            assert(px_rgb(pixels, 300 * scale, x, y, 0xF3, 0xEF, 0xE3));
        SzString *dump = sz_view_a11y_dump(root);
        char full[64];
        snprintf(full, sizeof full, "badge:%lld", (long long)counts[i]);
        assert(strstr(sz_string_cstr(dump), full));
        sz_string_free(dump);
      }
      sk_surface_unref(surface);
      sz_view_free(root);
      sz_signal_int_free(count);
      sz_signal_int_free(pin);
    }
  }
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
  /* The ticket stays beside the child. */
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFC, 0xF4));
  assert(px_rgb(px, 80, (int)(f.x + f.w - 4.f), (int)(f.y + 12.f), 0xE8, 0xEF, 0x48));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
  sz_signal_int_free(pin);
}

static void test_badge_child_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 120, (int)(f.x + 2.f), (int)(f.y + 2.f), 0xFF, 0xFC, 0xF4));
  assert(px_rgb(px, 120, (int)(cf.x + 8.f), (int)(cf.y + 4.f), 0xE8, 0xEF, 0x48));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_card_child_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, lx, ly, 0x65, 0x61, 0x57));
  assert(px_rgb(px, 80, lx, by, 0xF3, 0xEF, 0xE3));
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
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
  assert(taps != NULL);
  assert(strstr(taps, "divider") == NULL);
  assert(strstr(taps, "Go") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_expansion_body_and_focus(void) {
  SzSignalInt *open = sz_signal_int(1);
  SzSignalInt *show = sz_signal_int(0);
  SzSignalStr *name = sz_signal_str("kept");
  SzView *text = sz_view_text("Passive body");
  SzView *field = sz_view_text_field(name, "Details name");
  SzView *body = sz_view_column();
  sz_view_add_child(body, text);
  sz_view_add_child(body, field);
  SzView *tile = sz_view_expansion_tile(open, "Details", sz_view_padding(12, body));
  SzView *root = sz_view_column();
  sz_view_add_child(root, tile);
  SzView *conditional = sz_view_expansion_tile(open, "Conditional",
    sz_view_show_when(show, 1, sz_view_text("Conditional body")));
  sz_view_add_child(root, conditional);
  SzView *outside = sz_view_button("Outside", NULL, NULL);
  sz_view_add_child(root, outside);
  sz_view_layout(root, 400, 360, sz_theme_default());
  SzRect header = sz_view_control_frame(tile);
  assert(header.h == sz_theme_default()->control_h && header.h < sz_view_frame(tile).h);
  SzRect frame = sz_view_frame(text);
  assert(!sz_view_handle_tap(root, frame.x + 10, frame.y + frame.h / 2));
  assert(sz_signal_int_get(open) == 1);
  SzString *dump = sz_view_a11y_dump(root);
  assert(!strstr(sz_string_cstr(dump), "Conditional body"));
  sz_release(dump);
  sz_signal_int_set(show, 1);
  sz_view_layout(root, 400, 360, sz_theme_default());
  dump = sz_view_a11y_dump(root);
  assert(strstr(sz_string_cstr(dump), "Conditional body"));
  sz_release(dump);
  sz_signal_int_set(open, -3);
  sz_view_layout(root, 400, 360, sz_theme_default());
  dump = sz_view_a11y_dump(root);
  assert(strstr(sz_string_cstr(dump), "Passive body"));
  sz_release(dump);
  sz_view_focus(root, tile);
  sz_view_handle_key(root, "Enter", "", 0);
  sz_view_layout(root, 400, 360, sz_theme_default());
  assert(sz_signal_int_get(open) == 0 && sz_view_focused(root) == tile);
  sz_view_handle_key(root, "Space", "", 0);
  sz_view_layout(root, 400, 360, sz_theme_default());
  assert(sz_signal_int_get(open) == 1);
  assert(sz_view_focus_next(root, 0, 0) && sz_view_focused(root) == field);
  sz_view_focus(root, tile);
  SzUiConfig config = {.kind = SZ_UI_RUNTIME_HEADLESS, .width = 400, .height = 360, .scale = 1};
  SzUiSession *session = sz_ui_mount(&config, root);
  sz_ui_session_take_root(session);
  sz_ui_session_focus_view(session, field);
  assert(sz_ui_session_keyboard_visible(session));
  sz_signal_int_set(open, 0);
  assert(sz_ui_pump_sync(session));
  assert(sz_view_focused(root) == tile && !sz_ui_session_keyboard_visible(session));
  assert(!strcmp(sz_signal_str_get(name), "kept"));
  sz_signal_int_set(open, 1);
  assert(sz_ui_pump_sync(session));
  assert(sz_view_focused(root) == tile);
  sz_ui_session_focus_view(session, outside);
  sz_signal_int_set(open, 0);
  assert(sz_ui_pump_sync(session));
  assert(sz_view_focused(root) == outside);
  sz_signal_int_set(open, 1);
  assert(sz_ui_pump_sync(session));
  assert(sz_view_focused(root) == outside);
  sz_ui_unmount(session);
  sz_signal_int_free(open);
  sz_signal_int_free(show);
  sz_signal_str_free(name);
}

static void test_expansion_geometry_and_identity(void) {
  SzSignalInt *first_open = sz_signal_int(0);
  SzSignalInt *second_open = sz_signal_int(0);
  SzView *first = sz_view_expansion_tile(first_open, "More", sz_view_text("First"));
  SzView *second = sz_view_expansion_tile(second_open, "More", sz_view_text("Second"));
  SzView *root = sz_view_column();
  sz_view_add_child(root, first);
  sz_view_add_child(root, second);
  assert(sz_view_id(first) != sz_view_id(second));
  for (int scale = 1; scale <= 3; scale++) {
    SzTheme theme = *sz_theme_default();
    theme.px_scale = scale;
    theme.font_px *= scale;
    theme.control_h *= scale;
    theme.pad *= scale;
    theme.gap *= scale;
    sz_view_layout(root, 400 * scale, 360 * scale, &theme);
    sz_view_focus(root, first);
    sz_view_handle_key(root, "Enter", "", 0);
    sz_view_layout(root, 400 * scale, 360 * scale, &theme);
    assert(sz_signal_int_get(first_open) == 1 && sz_signal_int_get(second_open) == 0);
    assert(sz_view_control_frame(first).h == theme.control_h);
    assert(sz_view_control_frame(first).h < sz_view_frame(first).h);
    assert(sz_view_focus_next(root, 0, 0) && sz_view_focused(root) == second);
    sz_view_handle_key(root, "Space", "", 0);
    sz_view_layout(root, 400 * scale, 360 * scale, &theme);
    assert(sz_signal_int_get(second_open) == 1);
    SzRect header = sz_view_control_frame(first);
    assert(sz_view_handle_tap(root, header.x + header.w / 2, header.y + header.h / 2));
    assert(sz_signal_int_get(first_open) == 0 && sz_signal_int_get(second_open) == 1);
    sz_signal_int_set(second_open, 0);
  }
  sz_view_free(root);
  sz_signal_int_set(first_open, 1);
  SzView *tile = sz_view_expansion_tile(first_open, "Clipped",
    sz_view_sized(200, 200, sz_view_button("Inside", NULL, NULL)));
  root = sz_view_sized(200, 50, sz_view_clip(tile));
  SkSurface *surface = sk_surface_make_raster_n32_premul(200, 100);
  const SzTheme *theme = sz_theme_default();
  assert(sz_view_paint(root, sk_surface_get_canvas(surface), 200, 100, theme));
  size_t bytes;
  const uint8_t *pixels = sk_surface_peek_pixels(surface, &bytes);
  assert(pixels && bytes == 200 * 100 * 4);
  assert(px_rgb(pixels, 200, 20, 70, (theme->background >> 16) & 255,
    (theme->background >> 8) & 255, theme->background & 255));
  assert(!sz_view_hit_test(root, 20, 70));
  sk_surface_unref(surface);
  sz_view_free(root);
  sz_signal_int_free(first_open);
  sz_signal_int_free(second_open);
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
  assert(px_rgb(px, 120, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFC, 0xF4));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_expansion_tile_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFC, 0xF4));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
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
  assert(px_rgb(px, 80, lx, ly, 0x65, 0x61, 0x57));
  assert(px_rgb(px, 80, bx, ly, 0xF3, 0xEF, 0xE3));
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
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  /* The ink band shows completion. The center stays clear. */
  assert(px_rgb(px, 80, tx, ty, 0x24, 0x23, 0x1F));
  assert(px_rgb(px, 80, cx, cy, 0xF3, 0xEF, 0xE3));
  sz_signal_int_set(sig, 0);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, tx, ty, 0x65, 0x61, 0x57));
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
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, mx, my, 0xE8, 0xEF, 0x48));
  assert(px_rgb(px, 80, cx, cy, 0xF3, 0xEF, 0xE3));
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
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, bx, by, 0xFF, 0xFC, 0xF4));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, bx, by, 0xE8, 0xEF, 0x48));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
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
  /* Off thumb is ink. On track fill is primary. */
  assert(px_rgb(px, 80, sx, sy, 0x24, 0x23, 0x1F));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, sx, sy, 0xE8, 0xEF, 0x48));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
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
  /* Off inner is surface. On inner is ink. */
  assert(px_rgb(px, 80, bx, by, 0xFF, 0xFC, 0xF4));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, bx, by, 0x24, 0x23, 0x1F));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}


static void test_segmented_keyboard(void) {
  SzSignalInt *selected = sz_signal_int(1);
  const char *labels[] = {"List", "Grid", "Columns"};
  SzView *first = sz_view_segmented(selected, "Layout", labels, 3);
  SzView *second = sz_view_segmented(selected, "Preview", labels, 3);
  SzView *after = sz_view_button("After", NULL, NULL);
  SzSignalStr *name = sz_signal_str("kept");
  SzView *root = sz_view_column();
  sz_view_add_child(root, first);
  sz_view_add_child(root, second);
  sz_view_add_child(root, after);
  sz_view_add_child(root, sz_view_text_field(name, "Name"));
  sz_view_layout(root, 600, 300, sz_theme_default());
  SzView *targets[8];
  assert(sz_view_collect_tap_targets(root, targets, 8) == 7);
  for (int i = 0; i < 3; i++) {
    SzRect frame = sz_view_frame(targets[i]);
    assert(fabsf(frame.w - sz_view_frame(first).w / 3) < .01f);
    assert(sz_view_tab_stop(targets[i]) == (i == 1));
  }
  assert(sz_view_focus_next(root, 0, 0) && sz_view_focused(root) == targets[1]);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_signal_int_get(selected) == 2 && sz_view_focused(root) == targets[2]);
  sz_view_handle_key(root, "ArrowDown", "", 0);
  assert(sz_signal_int_get(selected) == 0 && sz_view_focused(root) == targets[0]);
  sz_view_handle_key(root, "ArrowUp", "", 0);
  assert(sz_signal_int_get(selected) == 2 && sz_view_focused(root) == targets[2]);
  sz_view_handle_key(root, "Home", "", 0);
  assert(sz_signal_int_get(selected) == 0);
  sz_view_handle_key(root, "End", "", 0);
  assert(sz_signal_int_get(selected) == 2);
  assert(sz_view_focus_next(root, 0, 0) && sz_view_focused(root) == targets[5]);
  assert(sz_view_focus_next(root, 0, 0) && sz_view_focused(root) == after);
  assert(sz_view_focus_next(root, 1, 0) && sz_view_focused(root) == targets[5]);
  assert(sz_view_focus_next(root, 1, 0) && sz_view_focused(root) == targets[2]);
  sz_view_handle_key(root, "ArrowLeft", "", 0);
  assert(sz_signal_int_get(selected) == 1 && sz_view_focused(root) == targets[1]);
  sz_view_handle_key(root, "q", "q", 0);
  sz_view_handle_key(root, "ArrowLeft", "", SZ_KEY_CTRL);
  assert(!strcmp(sz_signal_str_get(name), "kept") && sz_signal_int_get(selected) == 1);
  SzRect frame = sz_view_frame(targets[0]);
  assert(sz_view_handle_tap(root, frame.x + frame.w / 2, frame.y + frame.h / 2));
  assert(sz_signal_int_get(selected) == 0 && sz_view_focused(root) == targets[0]);
  sz_view_handle_key(root, "Enter", "", 0);
  assert(sz_signal_int_get(selected) == 0);
  sz_view_free(root);
  sz_signal_str_free(name);

  SzString *label = sz_string_from_cstr("Owned group");
  SzString *choice = sz_string_from_cstr("Owned choice");
  SzList *choices = sz_list_cons(choice, sz_list_nil());
  sz_signal_int_set(selected, -1);
  root = sz_lang_view_segmented(selected, label, choices);
  sz_release(label);
  sz_release(choice);
  sz_release(choices);
  sz_view_layout(root, 200, 100, sz_theme_default());
  SzString *dump = sz_view_a11y_dump(root);
  assert(strstr(sz_string_cstr(dump), "radiogroup:Owned group"));
  assert(strstr(sz_string_cstr(dump), "choicechip:Owned choice=0"));
  sz_release(dump);
  assert(sz_view_focus_next(root, 0, 0));
  sz_view_handle_key(root, "Space", "", 0);
  assert(sz_signal_int_get(selected) == 0);
  sz_view_free(root);

  const char *many[100];
  for (int i = 0; i < 100; i++) many[i] = "Same";
  sz_signal_int_set(selected, 99);
  root = sz_view_segmented(selected, "Many", many, 100);
  SzTheme scaled = *sz_theme_default();
  scaled.px_scale = 2;
  sz_view_layout(root, 2000, 200, &scaled);
  assert(sz_view_focus_next(root, 0, 0));
  SzView *last = sz_view_focused(root);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_signal_int_get(selected) == 0 && sz_view_focused(root) != last);
  sz_view_handle_key(root, "ArrowLeft", "", 0);
  assert(sz_signal_int_get(selected) == 99 && sz_view_focused(root) == last);
  sz_view_free(root);

  sz_signal_int_set(selected, INT64_MAX);
  root = sz_view_segmented(selected, "Empty", NULL, 0);
  sz_view_layout(root, 200, 100, sz_theme_default());
  assert(!sz_view_focus_next(root, 0, 1));
  assert(sz_signal_int_get(selected) == INT64_MAX && sz_view_frame(root).h == 0);
  sz_view_free(root);
  root = sz_view_segmented(selected, "One", labels, 1);
  sz_view_layout(root, 200, 100, sz_theme_default());
  assert(sz_signal_int_get(selected) == INT64_MAX);
  assert(sz_view_focus_next(root, 0, 0));
  sz_view_handle_key(root, "Space", "", 0);
  assert(sz_signal_int_get(selected) == 0);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_signal_int_get(selected) == 0);
  sz_view_free(root);
  sz_signal_int_free(selected);
}

static void test_segmented_sizes(void) {
  SzView *t;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  t = sz_view_segmented(sig, "Layout", (const char *[]){"List", "Grid"}, 2);
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
  t = sz_view_segmented(sig, "Layout", (const char *[]){"List", "Grid"}, 2);
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "choicechip:List=1") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 1);
  dump = sz_view_a11y_dump(t);
  assert(strstr(sz_string_cstr(dump), "choicechip:Grid=1") != NULL);
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
  t = sz_view_segmented(sig, "Layout", (const char *[]){"List", "Grid"}, 2);
  sz_view_layout(t, 200.f, 80.f, theme);
  f = sz_view_frame(t);
  assert(!sz_view_is_tap_target(t));
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
  t = sz_view_segmented(sig, "Layout", (const char *[]){"List", "Grid"}, 2);
  sz_view_layout(t, 200.f, 80.f, theme);
  f = sz_view_frame(t);
  hit = sz_view_hit_test(t, f.x + 8.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_CHOICE_CHIP);
  hit = sz_view_hit_test(t, f.x + f.w - 8.f, f.y + f.h * 0.5f);
  assert(hit && sz_view_kind(hit) == SZ_VIEW_CHOICE_CHIP);
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
  root = sz_view_segmented(sig, "Layout", (const char *[]){"List", "Grid"}, 2);
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
  assert(px_rgb(px, 80, lx, y, 0xE8, 0xEF, 0x48));
  assert(px_rgb(px, 80, rx, y, 0xFF, 0xFC, 0xF4));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, lx, y, 0xFF, 0xFC, 0xF4));
  assert(px_rgb(px, 80, rx, y, 0xE8, 0xEF, 0x48));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xE8, 0xEF, 0x48));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
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

static void test_tooltip_focus_and_hover(void) {
  SzSignalInt *count = sz_signal_int(0);
  SzView *button = sz_view_button("Save", counter_tap, count);
  SzView *tip = sz_view_tooltip("Save the current document", button);
  SzView *next = sz_view_button("Next", counter_tap, count);
  SzView *root = sz_view_column();
  sz_view_add_child(root, tip);
  sz_view_add_child(root, next);
  sz_view_layout(root, 360, 240, sz_theme_default());
  assert(!sz_view_tooltip_visible(tip));
  assert(!sz_view_is_tap_target(tip));
  assert(sz_view_tooltip_owner(button) == tip);
  assert(sz_view_focus_next(root, 0, 1));
  assert(sz_view_focused(root) == button);
  assert(sz_view_tooltip_visible(tip));
  assert(sz_signal_int_get(count) == 0);
  SzRect trigger = sz_view_frame(button);
  assert(sz_view_set_hover_at(root, trigger.x + 2, trigger.y + 2));
  sz_view_handle_key(root, "Escape", "", 0);
  assert(!sz_view_tooltip_visible(tip));
  assert(sz_view_focused(root) == button);
  for (int i = 0; i < 5; i++) {
    sz_view_set_hover_at(root, trigger.x + 3, trigger.y + 3);
    sz_view_layout(root, 360, 240, sz_theme_default());
    assert(!sz_view_tooltip_visible(tip));
  }
  sz_view_focus_next(root, 0, 1);
  assert(sz_view_focused(root) == next);
  assert(!sz_view_tooltip_visible(tip));
  sz_view_set_hover_at(root, -1, -1);
  assert(sz_view_set_hover_at(root, trigger.x + 2, trigger.y + 2));
  assert(sz_view_tooltip_visible(tip));
  SzRect popup = sz_view_tooltip_frame(tip);
  assert(popup.y == trigger.y + trigger.h);
  assert(sz_view_set_hover_at(root, popup.x + popup.w / 2, popup.y + popup.h / 2));
  assert(sz_view_tooltip_visible(tip));
  assert(sz_view_hit_test(root, popup.x + 2, popup.y + 2) == tip);
  assert(sz_view_handle_tap(root, popup.x + 2, popup.y + 2));
  assert(sz_signal_int_get(count) == 0);
  assert(sz_view_focused(root) == next);
  sz_view_set_hover_at(root, 359, 239);
  assert(!sz_view_tooltip_visible(tip));
  sz_view_focus(root, button);
  assert(sz_view_tooltip_visible(tip));
  sz_view_focus(root, NULL);
  assert(!sz_view_tooltip_visible(tip));
  sz_view_free(root);
  sz_signal_int_free(count);
}

static void test_tooltip_scope_and_geometry(void) {
  SzSignalInt *shown = sz_signal_int(1);
  SzView *button = sz_view_button("Help", NULL, NULL);
  SzView *inner = sz_view_tooltip("A long description wraps inside a narrow viewport. It keeps accented text: café.", button);
  SzView *outer = sz_view_tooltip("Outer help", inner);
  SzView *conditional = sz_view_show_when(shown, 1, outer);
  SzView *root = sz_view_stack();
  sz_view_add_child(root, sz_view_positioned(150, 100, conditional));
  sz_view_layout(root, 220, 150, sz_theme_default());
  sz_view_focus(root, button);
  assert(sz_view_tooltip_visible(inner));
  assert(!sz_view_tooltip_visible(outer));
  assert(sz_view_tooltip_owner(button) == inner);
  SzRect popup = sz_view_tooltip_frame(inner);
  assert(popup.x >= 0 && popup.y >= 0);
  assert(popup.x + popup.w <= 220 && popup.y + popup.h <= 150);
  assert(popup.h > 40);
  assert(popup.y < sz_view_frame(button).y);
  sz_signal_int_set(shown, 0);
  sz_view_layout(root, 220, 150, sz_theme_default());
  assert(!sz_view_tooltip_visible(inner));
  assert(!sz_view_tooltip_at(root, 155, 105));
  sz_signal_int_set(shown, 1);
  sz_view_layout(root, 220, 150, sz_theme_default());
  sz_view_focus(root, button);
  SzTheme scale = *sz_theme_default();
  scale.font_px *= 2; scale.pad *= 2; scale.gap *= 2; scale.control_h *= 2; scale.px_scale = 2;
  sz_view_layout(root, 440, 300, &scale);
  popup = sz_view_tooltip_frame(inner);
  assert(popup.x >= 0 && popup.y >= 0);
  assert(popup.x + popup.w <= 440 && popup.y + popup.h <= 300);
  sz_view_layout(root, 20, 10, sz_theme_default());
  popup = sz_view_tooltip_frame(inner);
  assert(popup.w <= 20 && popup.h <= 10);
  sz_view_free(root);
  sz_signal_int_free(shown);

  button = sz_view_button("Empty", NULL, NULL);
  SzView *empty = sz_view_tooltip("", button);
  sz_view_layout(empty, 200, 100, sz_theme_default());
  sz_view_focus_next(empty, 0, 1);
  assert(!sz_view_tooltip_visible(empty));
  assert(!sz_view_tooltip_owner(button));
  assert(!sz_view_set_hover_at(empty, 2, 2));
  sz_view_free(empty);
}

static void test_tooltip_clipping_and_pointer(void) {
  SzView *button = sz_view_button("Outside", NULL, NULL);
  SzView *tip = sz_view_tooltip("Clipped help", button);
  SzView *stack = sz_view_stack();
  sz_view_add_child(stack, sz_view_positioned(0, 120, tip));
  SzView *root = sz_view_sized(200, 80, sz_view_clip(stack));
  sz_view_layout(root, 360, 240, sz_theme_default());
  sz_view_focus(root, button);
  assert(sz_view_frame(button).y >= 120);
  assert(!sz_view_tooltip_visible(tip));
  assert(!sz_view_tooltip_at(root, 2, 122));
  sz_view_free(root);

  for (int absorb = 0; absorb <= 1; absorb++) {
    tip = sz_view_tooltip("Blocked help", sz_view_button("Blocked", NULL, NULL));
    root = absorb ? sz_view_absorb_pointer(tip) : sz_view_ignore_pointer(tip);
    sz_view_layout(root, 360, 240, sz_theme_default());
    assert(!sz_view_set_hover_at(root, 2, 2));
    assert(!sz_view_tooltip_visible(tip));
    sz_view_free(root);
  }

  SzSignalInt *count = sz_signal_int(0);
  button = sz_view_button("Save", counter_tap, count);
  tip = sz_view_tooltip("Save the current document", button);
  root = sz_view_column();
  sz_view_add_child(root, tip);
  sz_view_add_child(root, sz_view_button("Next", counter_tap, count));
  SzUiConfig cfg = {.kind = SZ_UI_RUNTIME_HEADLESS, .width = 360, .height = 240, .scale = 1};
  SzUiSession *session = sz_ui_mount(&cfg, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  sz_ui_session_focus_view(session, button);
  assert(sz_ui_pump_sync(session));
  SzRect popup = sz_view_tooltip_frame(tip);
  assert(popup.w > 0 && popup.h > 0);
  SzInputEvent event = {.kind = SZ_INPUT_POINTER, .pointer_button = 1,
    .pointer_phase = SZ_POINTER_DOWN, .x = popup.x + 4, .y = popup.y + popup.h / 2};
  assert(sz_ui_session_live_inject(session, &event));
  event.pointer_phase = SZ_POINTER_UP;
  (void)sz_ui_session_live_inject(session, &event);
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 0);
  assert(sz_view_focused(root) == button);
  assert(sz_view_tooltip_visible(tip));
  sz_ui_unmount(session);
  sz_signal_int_free(count);
}

static void test_tooltip_modal_escape(void) {
  SzSignalInt *open = sz_signal_int(1);
  SzSignalStr *value = sz_signal_str("");
  SzView *field = sz_view_text_field(value, "Name");
  SzView *help = sz_view_tooltip("Enter your name", field);
  SzView *background = sz_view_tooltip("Background help", sz_view_button("Background", NULL, NULL));
  SzView *dialog = sz_view_overlay(open, "Details", sz_view_card(help));
  SzView *root = sz_view_stack();
  sz_view_add_child(root, background);
  sz_view_add_child(root, dialog);
  sz_view_layout(root, 360, 240, sz_theme_default());
  assert(sz_view_focused(root) == field);
  assert(sz_view_tooltip_visible(help));
  assert(!sz_view_tooltip_visible(background));
  sz_view_handle_compose(root, "a");
  sz_view_handle_key(root, "Escape", "", 0);
  assert(sz_signal_int_get(open) == 1);
  assert(sz_view_tooltip_visible(help));
  sz_view_handle_key(root, "Escape", "", 0);
  assert(sz_signal_int_get(open) == 1);
  assert(!sz_view_tooltip_visible(help));
  assert(sz_view_focused(root) == field);
  sz_view_handle_key(root, "Escape", "", 0);
  assert(sz_signal_int_get(open) == 0);
  sz_view_layout(root, 360, 240, sz_theme_default());
  assert(!sz_view_tooltip_visible(help));
  assert(sz_view_tooltip_visible(background));
  sz_view_free(root);
  sz_signal_int_free(open);
  sz_signal_str_free(value);
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
  assert(px_rgb(px, 80, mx, my, 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, mx, my, 0xFF, 0xFC, 0xF4));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_tooltip_not_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
  assert(taps != NULL);
  assert(strstr(taps, "Sean") == NULL);
  assert(strstr(taps, "tooltip") == NULL);
  free(dump);
  sz_ui_unmount(session);
  remove(path);
}

static void test_tooltip_child_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xFF, 0xFC, 0xF4));
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xE8, 0xEF, 0x48));
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
  assert(!px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xE8, 0xEF, 0x48));
  sk_surface_unref(surf);
  sz_view_free(outlined);
  sz_signal_int_free(sig);
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xF3, 0xEF, 0xE3));
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xFF, 0xFC, 0xF4));
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
  assert(!px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xE8, 0xEF, 0x48));
  assert(!px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 8.f), 0xFF, 0xFC, 0xF4));
  sk_surface_unref(surf);
  sz_view_free(textb);
  sz_signal_int_free(sig);
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
  assert(px_rgb(px, 80, mx, my, 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)f.y, 0x65, 0x61, 0x57));
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
  assert(px_rgb(px, 80, 8, 8, 0xF3, 0xEF, 0xE3));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_placeholder_not_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
  assert(taps != NULL);
  assert(strstr(taps, "ph") == NULL);
  assert(strstr(taps, "placeholder") == NULL);
  free(dump);
  sz_ui_unmount(session);
  remove(path);
}

static void test_placeholder_child_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, mx, my, 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, 8, 8, 0xF3, 0xEF, 0xE3));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_semantics_not_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
  assert(taps != NULL);
  assert(strstr(taps, "mark") == NULL);
  assert(strstr(taps, "semantics") == NULL);
  free(dump);
  sz_ui_unmount(session);
  remove(path);
}

static void test_semantics_child_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, mx, my, 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, 8, 8, 0xF3, 0xEF, 0xE3));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_merge_semantics_not_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
  assert(taps != NULL);
  assert(strstr(taps, "logo") == NULL);
  assert(strstr(taps, "merge") == NULL);
  free(dump);
  sz_ui_unmount(session);
  remove(path);
}

static void test_merge_semantics_child_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, mx, my, 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, 8, 8, 0xF3, 0xEF, 0xE3));
  sk_surface_unref(surf);
  sz_view_free(root);
}


static void test_ink_well_child_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, mx, my, 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 20.f), 0xF3, 0xEF, 0xE3));
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
  assert(px_rgb(px, 80, 8, 8, 0xF3, 0xEF, 0xE3));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_visibility_not_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
  assert(taps != NULL);
  assert(strstr(taps, "visibility") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_visibility_child_in_taps_on(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
  assert(taps != NULL);
  assert(strstr(taps, "Go") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(vis_sig);
  sz_signal_int_free(tap_sig);
  remove(path);
}

static void test_visibility_child_not_in_taps_off(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, mx, my, 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 20.f), 0xF3, 0xEF, 0xE3));
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
  assert(px_rgb(px, 80, 8, 8, 0xF3, 0xEF, 0xE3));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_offstage_not_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
  assert(taps != NULL);
  assert(strstr(taps, "offstage") == NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(sig);
  remove(path);
}

static void test_offstage_child_in_taps_on(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
  assert(taps != NULL);
  assert(strstr(taps, "Go") != NULL);
  free(dump);
  sz_ui_unmount(session);
  sz_signal_int_free(off_sig);
  sz_signal_int_free(tap_sig);
  remove(path);
}

static void test_offstage_child_not_in_taps_off(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, mx, my, 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, 8, 8, 0xF3, 0xEF, 0xE3));
  sk_surface_unref(surf);
  sz_view_free(root);
}

static void test_unconstrained_box_not_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
  assert(taps != NULL);
  assert(strstr(taps, "box") == NULL);
  assert(strstr(taps, "unconstrained") == NULL);
  free(dump);
  sz_ui_unmount(session);
  remove(path);
}

static void test_unconstrained_box_child_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFC, 0xF4));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, mx, my, 0x24, 0x23, 0x1F));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFC, 0xF4));
  sz_signal_int_set(sig, 0);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFC, 0xF4));
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xE8, 0xEF, 0x48));
  sk_surface_unref(surf);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(chip, canvas, 80, 80, theme));
  f = sz_view_frame(chip);
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFC, 0xF4));
  sk_surface_unref(surf);
  sz_view_free(filled);
  sz_view_free(chip);
  sz_signal_int_free(sig);
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xFF, 0xFC, 0xF4));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + 4.f), 0xE8, 0xEF, 0x48));
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
  assert(px_rgb(px, 80, mx, my, 0x24, 0x23, 0x1F));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
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
  assert(px_rgb(px, 80, 6, 16, 0xF3, 0xEF, 0xE3));
  sz_signal_int_set(sig, 1);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, 6, 16, 0xE8, 0xEF, 0x48));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}


static void test_split_keyboard(void) {
  SzSignalInt *fraction = sz_signal_int(40);
  SzSignalStr *name = sz_signal_str("kept");
  SzView *field = sz_view_text_field(name, "Primary name");
  SzView *end = sz_view_button("Secondary", NULL, NULL);
  SzView *split = sz_view_split(fraction, "Tools", field, end);
  SzView *root = sz_view_column();
  SzView *after = sz_view_button("After", NULL, NULL);
  sz_view_add_child(root, split);
  sz_view_add_child(root, after);
  const SzTheme *theme = sz_theme_default();
  sz_view_layout(root, 400, 200, theme);
  assert(sz_view_focus_next(root, 0, 0) && sz_view_focused(root) == field);
  assert(sz_view_focus_next(root, 0, 0) && sz_view_focused(root) == split);
  assert(sz_view_focus_next(root, 0, 0) && sz_view_focused(root) == end);
  assert(sz_view_focus_next(root, 1, 0) && sz_view_focused(root) == split);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_signal_int_get(fraction) == 41);
  sz_view_handle_key(root, "ArrowLeft", "", 0);
  assert(sz_signal_int_get(fraction) == 40);
  sz_view_handle_key(root, "Enter", "", 0);
  sz_view_layout(root, 400, 200, theme);
  assert(sz_signal_int_get(fraction) == 0 && sz_view_focused(root) == split);
  SzString *dump = sz_view_a11y_dump(root);
  assert(strstr(sz_string_cstr(dump), "split:Tools=0") && !strstr(sz_string_cstr(dump), "Primary name"));
  sz_release(dump);
  assert(sz_view_focus_next(root, 1, 1) && sz_view_focused(root) == after);
  sz_view_focus(root, field);
  assert(sz_view_focused(root) == after);
  sz_view_focus(root, split);
  sz_view_handle_key(root, "Enter", "", 0);
  sz_view_layout(root, 400, 200, theme);
  assert(sz_signal_int_get(fraction) == 40 && !strcmp(sz_signal_str_get(name), "kept"));
  sz_view_handle_key(root, "End", "", 0);
  sz_view_layout(root, 400, 200, theme);
  assert(sz_signal_int_get(fraction) == 100);
  dump = sz_view_a11y_dump(root);
  assert(strstr(sz_string_cstr(dump), "Primary name") && !strstr(sz_string_cstr(dump), "button:Secondary"));
  sz_release(dump);
  assert(sz_view_focus_next(root, 0, 0) && sz_view_focused(root) == after);
  sz_view_focus(root, split);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_signal_int_get(fraction) == 100);
  sz_view_handle_key(root, "Home", "", 0);
  sz_view_handle_key(root, "ArrowLeft", "", 0);
  assert(sz_signal_int_get(fraction) == 0);
  sz_view_handle_key(root, "Enter", "", 0);
  assert(sz_signal_int_get(fraction) == 100);
  sz_view_handle_key(root, "q", "q", 0);
  sz_view_handle_key(root, "ArrowLeft", "", SZ_KEY_CTRL);
  assert(sz_signal_int_get(fraction) == 100 && !strcmp(sz_signal_str_get(name), "kept"));
  sz_signal_int_set(fraction, 40);
  sz_view_layout(root, 400, 200, theme);
  SzRect handle = sz_view_control_frame(split);
  SzRect frame = sz_view_frame(split);
  assert(sz_view_hit_test(root, handle.x + handle.w / 2, handle.y + 10) == split);
  assert(sz_view_hit_test(root, frame.x + 10, frame.y + 10) == field);
  assert(sz_view_split_set_at(split, handle.x + handle.w / 2));
  assert(sz_signal_int_get(fraction) == 40);
  assert(!sz_view_split_set_at(split, NAN));
  sz_view_split_set_at(split, frame.x - 10);
  assert(sz_signal_int_get(fraction) == 0);
  sz_view_split_set_at(split, frame.x + frame.w + 10);
  assert(sz_signal_int_get(fraction) == 100);
  SzTheme scaled = *theme;
  scaled.px_scale = 2;
  sz_signal_int_set(fraction, 25);
  sz_view_layout(root, 800, 400, &scaled);
  handle = sz_view_control_frame(split);
  assert(handle.w == 12);
  sz_view_split_set_at(split, handle.x + handle.w / 2);
  assert(sz_signal_int_get(fraction) == 25);
  for (int scale = 1; scale <= 4; scale++) {
    scaled.px_scale = scale / 2.f;
    for (int n = 0; n <= 100; n++) {
      sz_signal_int_set(fraction, n);
      sz_view_layout(root, 400 * scaled.px_scale, 200 * scaled.px_scale, &scaled);
      handle = sz_view_control_frame(split);
      sz_view_split_set_at(split, handle.x + handle.w / 2);
      assert(sz_signal_int_get(fraction) == n);
    }
  }
  sz_signal_int_set(fraction, 40);
  sz_view_layout(root, 400, 200, theme);
  sz_view_focus(root, field);
  sz_signal_int_set(fraction, 0);
  sz_view_layout(root, 400, 200, theme);
  assert(sz_view_focused(root) == NULL);
  SzUiConfig config = {.kind = SZ_UI_RUNTIME_HEADLESS, .width = 400, .height = 200, .scale = 1};
  sz_signal_int_set(fraction, 40);
  SzUiSession *session = sz_ui_mount(&config, root);
  sz_ui_session_take_root(session);
  sz_ui_session_focus_view(session, field);
  assert(sz_ui_session_keyboard_visible(session));
  handle = sz_view_control_frame(split);
  SzInputEvent input = {.kind = SZ_INPUT_POINTER, .pointer_phase = SZ_POINTER_DOWN,
    .pointer_button = 1, .x = handle.x + handle.w / 2, .y = handle.y + 10};
  assert(sz_ui_inject_sync(session, &input));
  assert(sz_view_focused(root) == split && !sz_ui_session_keyboard_visible(session));
  input.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_inject_sync(session, &input));
  sz_view_handle_key(root, "q", "q", 0);
  assert(!strcmp(sz_signal_str_get(name), "kept"));
  sz_ui_unmount(session);
  sz_signal_int_free(fraction);
  sz_signal_str_free(name);
}

static void test_slider_ranges(void) {
  SzSignalInt *value = sz_signal_int(-14);
  SzView *slider = sz_view_slider(value, "Temperature", -60, 120, 3);
  assert(sz_signal_int_get(value) == -14 && sz_view_slider_value(slider) == -15);
  assert(sz_view_slider_minimum(slider) == -60 && sz_view_slider_maximum(slider) == 120);
  sz_view_layout(slider, 200, 100, sz_theme_default());
  sz_view_focus(slider, slider);
  assert(sz_view_handle_key(slider, "ArrowUp", "", 0));
  assert(sz_signal_int_get(value) == -12);
  sz_view_handle_key(slider, "PageUp", "", 0);
  assert(sz_signal_int_get(value) == 18);
  sz_view_handle_key(slider, "PageDown", "", 0);
  assert(sz_signal_int_get(value) == -12);
  sz_view_handle_key(slider, "Home", "", 0);
  assert(sz_signal_int_get(value) == -60);
  sz_view_handle_key(slider, "End", "", 0);
  assert(sz_signal_int_get(value) == 120);
  sz_view_handle_key(slider, "ArrowRight", "", 0);
  assert(sz_signal_int_get(value) == 120);
  sz_view_slider_set_at(slider, 100);
  assert(sz_signal_int_get(value) == 30);
  sz_view_slider_set_at(slider, -100);
  assert(sz_signal_int_get(value) == -60);
  sz_view_slider_set_at(slider, 1000);
  assert(sz_signal_int_get(value) == 120);
  sz_view_free(slider);
  slider = sz_view_slider(value, "Uneven", -10, 10, 6);
  assert(sz_view_slider_maximum(slider) == 8);
  sz_signal_int_set(value, -7);
  assert(sz_view_slider_value(slider) == -4);
  sz_signal_int_set(value, INT64_MAX);
  assert(sz_view_slider_value(slider) == 8);
  sz_view_free(slider);
  slider = sz_view_slider(value, "Constant", 5, 5, 1);
  sz_view_focus(slider, slider);
  sz_view_handle_key(slider, "PageUp", "", 0);
  assert(sz_signal_int_get(value) == 5);
  sz_view_free(slider);
  slider = sz_view_slider(value, "Large step", INT64_MIN, INT64_MAX, INT64_MAX);
  assert(sz_view_slider_maximum(slider) == INT64_MAX - 1);
  sz_view_layout(slider, 200, 100, sz_theme_default());
  sz_view_slider_set_at(slider, 100);
  assert(sz_signal_int_get(value) == -1);
  sz_view_focus(slider, slider);
  sz_view_handle_key(slider, "PageDown", "", 0);
  assert(sz_signal_int_get(value) == INT64_MIN);
  sz_view_handle_key(slider, "PageUp", "", 0);
  assert(sz_signal_int_get(value) == INT64_MAX - 1);
  sz_view_free(slider);
  slider = sz_view_slider(value, "Full Int", INT64_MIN, INT64_MAX, 1);
  sz_view_layout(slider, 200, 100, sz_theme_default());
  assert(sz_view_slider_maximum(slider) == INT64_MAX);
  sz_view_slider_set_at(slider, 50);
  assert(sz_signal_int_get(value) == INT64_C(-4611686018427387904));
  sz_view_slider_set_at(slider, 100);
  assert(sz_signal_int_get(value) == 0);
  sz_view_slider_set_at(slider, 150);
  assert(sz_signal_int_get(value) == INT64_C(4611686018427387903));
  sz_view_slider_set_at(slider, 0);
  assert(sz_signal_int_get(value) == INT64_MIN);
  sz_view_slider_set_at(slider, 200);
  assert(sz_signal_int_get(value) == INT64_MAX);
  sz_view_focus(slider, slider);
  sz_view_handle_key(slider, "PageUp", "", 0);
  assert(sz_signal_int_get(value) == INT64_MAX);
  sz_view_handle_key(slider, "ArrowLeft", "", 0);
  assert(sz_signal_int_get(value) == INT64_MAX - 1);
  sz_view_handle_key(slider, "Home", "", 0);
  sz_view_handle_key(slider, "ArrowDown", "", 0);
  assert(sz_signal_int_get(value) == INT64_MIN);
  sz_view_free(slider);
  sz_signal_int_free(value);
}

static void test_slider_pointer_focus_and_removal(void) {
  SzSignalInt *value = sz_signal_int(0), *hits = sz_signal_int(0);
  SzSignalStr *text = sz_signal_str("");
  SzView *root = sz_view_column();
  SzView *field = sz_view_text_field(text, "Name");
  SzView *slider = sz_view_slider(value, "Level", 0, 100, 1);
  sz_view_add_child(root, field);
  SzView *holder = sz_view_column();
  sz_view_add_child(holder, slider);
  sz_view_add_child(root, holder);
  SzUiConfig cfg = {.kind = SZ_UI_RUNTIME_HEADLESS, .width = 320, .height = 200, .scale = 1};
  SzUiSession *session = sz_ui_mount(&cfg, root);
  assert(session && sz_ui_pump_sync(session));
  sz_ui_session_focus_view(session, field);
  assert(sz_ui_session_keyboard_visible(session));
  SzInputEvent keyboard = {.kind = SZ_INPUT_KEYBOARD, .keyboard_visible = 0};
  assert(sz_ui_inject_sync(session, &keyboard));
  sz_ui_session_focus_view(session, field);
  assert(!sz_ui_session_keyboard_visible(session));
  sz_ui_session_focus_view(session, slider);
  sz_ui_session_focus_view(session, field);
  assert(sz_ui_session_keyboard_visible(session));
  SzRect frame = sz_view_frame(slider);
  SzInputEvent pointer = {.kind = SZ_INPUT_POINTER, .pointer_phase = SZ_POINTER_DOWN,
    .pointer_button = 1, .x = frame.x + 10, .y = frame.y + 10};
  assert(sz_ui_inject_sync(session, &pointer));
  assert(sz_view_focused(root) == slider);
  assert(!sz_ui_session_keyboard_visible(session));
  sz_view_handle_key(root, "q", "q", 0);
  assert(!strcmp(sz_signal_str_get(text), ""));
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_signal_int_get(value) > 0);
  sz_view_clear_children(holder);
  sz_view_add_child(holder, sz_view_button("Replacement", counter_tap, hits));
  pointer.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_inject_sync(session, &pointer));
  assert(sz_signal_int_get(hits) == 0);
  sz_ui_unmount(session); sz_view_free(root);
  sz_signal_int_free(value); sz_signal_int_free(hits); sz_signal_str_free(text);
}

static void test_slider_sizes(void) {
  SzView *sl;
  SzSignalInt *sig;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(40);
  sl = sz_view_slider(sig, "Level", 0, 100, 1);
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
  sl = sz_view_slider(sig, "Level", 0, 100, 1);
  dump = sz_view_a11y_dump(sl);
  assert(strstr(sz_string_cstr(dump), "slider:Level=40") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, 7);
  dump = sz_view_a11y_dump(sl);
  assert(strstr(sz_string_cstr(dump), "slider:Level=7") != NULL);
  sz_string_free(dump);
  sz_view_free(sl);
  sz_signal_int_free(sig);
}

static void test_slider_clamps_a11y(void) {
  SzView *sl;
  SzSignalInt *sig;
  SzString *dump;

  sig = sz_signal_int(150);
  sl = sz_view_slider(sig, "Level", 0, 100, 1);
  dump = sz_view_a11y_dump(sl);
  assert(strstr(sz_string_cstr(dump), "slider:Level=100") != NULL);
  sz_string_free(dump);
  sz_signal_int_set(sig, -3);
  dump = sz_view_a11y_dump(sl);
  assert(strstr(sz_string_cstr(dump), "slider:Level=0") != NULL);
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
  sl = sz_view_slider(sig, "Level", 0, 100, 1);
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
  sl = sz_view_slider(sig, "Level", 0, 100, 1);
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
  sl = sz_view_slider(sig, "Level", 0, 100, 1);
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
  root = sz_view_slider(sig, "Level", 0, 100, 1);
  sz_view_layout(root, 80.f, 80.f, theme);
  f = sz_view_frame(root);
  surf = sk_surface_make_raster_n32_premul(80, 80);
  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + f.h * 0.5f), 0xE8, 0xEF, 0x48));
  sz_signal_int_set(sig, 0);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + f.w - 8.f), (int)(f.y + f.h * 0.5f), 0x65, 0x61, 0x57));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}


static void test_slider_pointer_drag(void) {
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *sl;
  SzSignalInt *sig;
  SzInputEvent ev;
  const SzTheme *theme = sz_theme_default();
  SzRect f;

  sig = sz_signal_int(0);
  sl = sz_view_slider(sig, "Level", 0, 100, 1);
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
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root1, *root2, *sl;
  SzSignalInt *sig;
  SzInputEvent ev;
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  int64_t v0;

  sig = sz_signal_int(0);
  sl = sz_view_slider(sig, "Level", 0, 100, 1);
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
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root, *sl;
  SzSignalInt *sig;
  SzInputEvent ev;
  const char *record = "/tmp/scuzz_ui_slider.json";
  const SzTheme *theme = sz_theme_default();
  SzRect f;
  char *body;

  remove(record);
  sig = sz_signal_int(0);
  sl = sz_view_slider(sig, "Level", 0, 100, 1);
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
  assert(strstr(body, "\"op\":\"xy\"") != NULL);
  assert(strstr(body, "\"op\":\"tap\"") == NULL);
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

static void test_progress_amount_without_accent(void) {
  for (int scale = 1; scale <= 2; scale++) {
    SzTheme theme = *sz_theme_default();
    theme.px_scale = (float)scale;
    theme.font_px *= scale;
    theme.control_h *= scale;
    theme.pad *= scale;
    theme.gap *= scale;
    theme.primary = theme.surface;
    for (int kind = 0; kind < 2; kind++) {
      SzSignalInt *sig = sz_signal_int(0);
      SzView *view = kind ? sz_view_circular_progress(sig) : sz_view_progress(sig);
      SkSurface *surf = sk_surface_make_raster_n32_premul(160 * scale, 60 * scale);
      SkCanvas *canvas = sk_surface_get_canvas(surf);
      int previous = -1;
      for (int value = 0; value <= 100; value += 25) {
        size_t bytes;
        int ink = 0;
        sz_signal_int_set(sig, value);
        assert(sz_view_paint(view, canvas, 160 * scale, 60 * scale, &theme));
        const uint8_t *pixels = sk_surface_peek_pixels(surf, &bytes);
        SzRect frame = sz_view_frame(view);
        for (int y = 0; y < 60 * scale; y++) {
          for (int x = 0; x < 160 * scale; x++) {
            if (px_rgb(pixels, 160 * scale, x, y, 0x24, 0x23, 0x1F))
              ink++;
            if (x >= frame.w || y >= frame.h)
              assert(px_rgb(pixels, 160 * scale, x, y, 0xF3, 0xEF, 0xE3));
          }
        }
        /* More completion adds ink even when the accent matches the paper. */
        assert(ink > previous);
        previous = ink;
      }
      sk_surface_unref(surf);
      sz_view_free(view);
      sz_signal_int_free(sig);
    }
  }
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
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + f.h * 0.5f), 0xE8, 0xEF, 0x48));
  sz_signal_int_set(sig, 0);
  assert(sz_view_paint(root, canvas, 80, 80, theme));
  px = sk_surface_peek_pixels(surf, &n);
  assert(px && n == 80 * 80 * 4);
  assert(px_rgb(px, 80, (int)(f.x + 8.f), (int)(f.y + f.h * 0.5f), 0xFF, 0xFC, 0xF4));
  sk_surface_unref(surf);
  sz_view_free(root);
  sz_signal_int_free(sig);
}

static void test_progress_not_in_taps_dump(void) {
  SzUiConfig cfg = {0};
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
  taps = strstr(dump, "\"taps\":[");
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
  SzUiConfig cfg = {0};
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
  assert(strstr(dump, "\"taps\":[") != NULL);
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
  sz_view_add_child(row, sz_view_expanded(1, sz_view_text(s)));
  sz_view_add_child(row, sz_view_button("Del", NULL, NULL));
  return row;
}

static void test_each_expanded_row_in_scroll(void) {
  SzSignalList *items;
  SzView *col, *list, *scroll, *exp;
  SzList *xs;
  SzUiConfig cfg = {0};
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
  exp = sz_view_expanded(1, scroll);
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
  taps = strstr(dump, "\"taps\":[");
  assert(taps != NULL);
  p0 = strstr(taps, "\"label\":\"Del\"");
  assert(p0 != NULL);
  p1 = strstr(p0 + 12, "\"label\":\"Del\"");
  assert(p1 != NULL);
  c0 = strstr(p0, "\"y\":");
  c1 = strstr(p1, "\"y\":");
  assert(c0 && c1);
  y0 = atoi(c0 + 4);
  y1 = atoi(c1 + 4);
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
  dump = sz_signal_dump_json_string();
  s = strstr(sz_string_cstr(dump), "\"type\":\"list\"");
  assert(s);
  assert(strstr(s, "\"name\":\"items\""));
  name = sz_string_from_cstr("items");
  assert(sz_property_signal_list_len(name) == 2);
  sz_signal_list_set(items, sz_list_nil());
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
  dump = sz_signal_dump_json_string();
  s = strstr(sz_string_cstr(dump), "\"type\":\"list\"");
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
  sz_string_free(dump);
  sz_signal_list_free(items);
}

static void test_property_signal_int(void) {
  SzSignalInt *count;
  SzString *dump;
  SzString *name;
  const char *s;

  count = sz_signal_int(7);
  dump = sz_signal_dump_json_string();
  s = strstr(sz_string_cstr(dump), "\"type\":\"int\"");
  assert(s);
  /* Unnamed signals dump with an empty name. A missing name panics. */
  assert(strstr(s, "\"value\":7"));
  name = sz_string_from_cstr("count");
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
  items = sz_signal_new(xs, 5, sz_string_from_cstr("rows"));
  dump = sz_signal_dump_json_string();
  s = strstr(sz_string_cstr(dump), "\"type\":\"list\"");
  assert(s);
  /* Non-String elements encode typed (v=2). */
  assert(strstr(s, "\"name\":\"rows\",\"value\":[1,2]"));
  name = sz_string_from_cstr("rows");
  assert(sz_property_signal_list_len(name) == 2);
  got = sz_property_signal_list_at(name, 0);
  assert(strcmp(sz_string_cstr(got), "") == 0);
  sz_string_free(got);
  sz_release(name);
  sz_string_free(dump);
  sz_signal_list_free(items);

  xs = sz_list_cons(sz_box_i64(1), sz_list_nil());
  items = sz_signal_new(xs, 3, sz_string_from_cstr("rows"));
  dump = sz_signal_dump_json_string();
  s = strstr(sz_string_cstr(dump), "\"type\":\"list\"");
  assert(s);
  /* A String flag on non-String heads still encodes typed. */
  assert(strstr(s, "\"name\":\"rows\",\"value\":[1]"));
  sz_string_free(dump);
  sz_signal_list_free(items);
}

static int wait_aborted(pid_t pid) {
  int st = 0;
  if (waitpid(pid, &st, 0) != pid)
    return 0;
  return WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT;
}

static void assert_missing_aborts(void (*fn)(void), const char *expect) {
  pid_t pid;
  int fds[2];
  char err[8192];
  ssize_t n;

  assert(pipe(fds) == 0);
  fflush(NULL);
  pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    dup2(fds[1], STDERR_FILENO);
    close(fds[0]);
    close(fds[1]);
    fn();
    _exit(1);
  }
  close(fds[1]);
  n = read(fds[0], err, sizeof err - 1);
  close(fds[0]);
  if (n < 0)
    n = 0;
  err[n] = '\0';
  assert(wait_aborted(pid));
  assert(strstr(err, expect) != NULL);
}

static void missing_property_int(void) {
  SzString *name = sz_string_from_cstr("missing");
  (void)sz_property_signal_int(name);
}

static void missing_property_str(void) {
  SzString *name = sz_string_from_cstr("missing");
  (void)sz_property_signal_str(name);
}

static void missing_property_list_len(void) {
  SzString *name = sz_string_from_cstr("missing");
  (void)sz_property_signal_list_len(name);
}

static void missing_timeline_int(void) {
  FILE *f = fopen("/tmp/scuzz_ui_tl_missing.dump", "w");
  void *tl;
  SzString *name;
  assert(f);
  fputs("# timeline v=3 n=1\n--- 0\nlast_hit:\n\ndrive:\n\nsignals:\n"
        "int[0] count = 7\na11y:\n\n",
        f);
  fclose(f);
  tl = sz_timeline_load("/tmp/scuzz_ui_tl_missing.dump");
  name = sz_string_from_cstr("missing");
  (void)sz_timeline_signal_int(tl, 0, name);
}

static char g_missing_long[601];

static void missing_property_long_name(void) {
  SzString *name = sz_string_from_cstr(g_missing_long);
  (void)sz_property_signal_int(name);
}

static void test_property_missing_signal_panics(void) {
  int i;
  assert_missing_aborts(missing_property_int, "missing signal missing");
  assert_missing_aborts(missing_property_str, "missing signal missing");
  assert_missing_aborts(missing_property_list_len, "missing signal missing");
  assert_missing_aborts(missing_timeline_int, "missing signal missing");
  /* A long name reaches stderr in full: no fixed-buffer truncation. */
  for (i = 0; i < 600; i++)
    g_missing_long[i] = (char)('a' + (i % 26));
  g_missing_long[600] = '\0';
  assert_missing_aborts(missing_property_long_name, g_missing_long);
}

static int g_map_calls;

static void *map_counting_int(void *value, void *env) {
  int64_t v = sz_unbox_i64(value);
  (void)env;
  g_map_calls++;
  return sz_string_from_int(v);
}

static void test_signal_mirror_revision(void) {
  SzString *name = sz_string_from_cstr("$mirror");
  void *value = sz_box_i64(7);
  SzSignal *mirror = sz_signal_new(value, 1, name);
  SzSignal *visible = sz_signal_new(value, 1, NULL);
  SzSignal *derived = sz_signal_derive(mirror, map_counting_int, NULL, 2, name);
  uint64_t revision = sz_signal_revision();
  sz_release(value);
  value = sz_box_i64(8);
  sz_signal_write(mirror, value);
  assert(sz_signal_revision() == revision);
  assert(sz_signal_int_get(mirror) == 8);
  assert(!strcmp(sz_signal_str_get(derived), "8"));
  sz_signal_write(visible, value);
  assert(sz_signal_revision() == revision + 1);
  sz_signal_write(visible, value);
  assert(sz_signal_revision() == revision + 1);
  sz_release(value);
  sz_release(name);
  sz_signal_free(derived);
  sz_signal_free(visible);
  sz_signal_free(mirror);
}

static void test_signal_map_caches_unchanged_source(void) {
  SzSignalInt *count;
  SzSignalStr *label;

  count = sz_signal_int(7);
  g_map_calls = 0;
  label = sz_signal_derive(count, map_counting_int, NULL, 2, NULL);
  /* Construction primes the cache once. */
  assert(g_map_calls == 1);
  /* An unchanged source serves the cached string: no recompute, no realloc. */
  (void)sz_signal_str_get(label);
  (void)sz_signal_str_get(label);
  assert(g_map_calls == 1);
  assert(strcmp(sz_signal_str_get(label), "7") == 0);
  assert(g_map_calls == 1);
  /* A changed source recomputes once, then caches again. */
  sz_signal_int_set(count, 8);
  assert(strcmp(sz_signal_str_get(label), "8") == 0);
  assert(g_map_calls == 2);
  (void)sz_signal_str_get(label);
  assert(g_map_calls == 2);
  sz_signal_str_free(label);
  sz_signal_int_free(count);
}

static void test_signal_list_elem_str_flag(void) {
  SzSignalList *strs;
  SzSignalList *rows;
  SzString *dump;

  /* No explicit flag: an empty list stays unknown and dumps as String. */
  strs = sz_signal_list(sz_list_nil());
  sz_signal_name(strs, "estr");
  assert(sz_signal_list_elem_str(strs) == 1);
  dump = sz_signal_dump_json_string();
  assert(strstr(sz_string_cstr(dump), "\"name\":\"estr\",\"value\":[]") != NULL);
  sz_string_free(dump);

  /* An explicit non-String flag decides an empty list: typed payload. */
  rows = sz_signal_new(sz_list_nil(), 5, sz_string_from_cstr("erow"));
  assert(sz_signal_list_elem_str(rows) == 0);
  dump = sz_signal_dump_json_string();
  assert(strstr(sz_string_cstr(dump), "\"name\":\"erow\",\"value\":[]") != NULL);
  sz_string_free(dump);

  assert(sz_signal_list_elem_str(NULL) == 0);
  sz_signal_list_free(strs);
  sz_signal_list_free(rows);
}

static SzView *runtime_env_factory(void *env) {
  (void)env;
  return sz_view_text("hi");
}

/* Run fn in a forked child with stderr captured. Report the exit status
   (0 = clean, 1 = SIGABRT) through status and the stderr text in err. */
static void run_child(void (*fn)(void), int *aborted, char *err, size_t cap) {
  int fds[2];
  ssize_t n;
  pid_t pid;
  int st = 0;

  assert(pipe(fds) == 0);
  fflush(NULL);
  pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    dup2(fds[1], STDERR_FILENO);
    close(fds[0]);
    close(fds[1]);
    fn();
    _exit(0);
  }
  close(fds[1]);
  n = read(fds[0], err, cap - 1);
  close(fds[0]);
  if (n < 0)
    n = 0;
  err[n] = '\0';
  assert(waitpid(pid, &st, 0) == pid);
  *aborted = WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT;
}

static void runtime_env_bad(void) {
  setenv("SCUZZ_UI_RUNTIME", "deskop", 1);
  (void)sz_io_unsafe_run(sz_ui_run_rebuild(runtime_env_factory, NULL, NULL));
}

static void runtime_env_upper_desktop(void) {
  setenv("SCUZZ_UI_RUNTIME", "DESKTOP", 1);
  (void)sz_io_unsafe_run(sz_ui_run_rebuild(runtime_env_factory, NULL, NULL));
}

static void test_runtime_env_parse(void) {
  char err[4096];
  int aborted;

  /* An unknown value must abort, not silently run Headless. */
  run_child(runtime_env_bad, &aborted, err, sizeof err);
  assert(aborted);
  assert(strstr(err, "SCUZZ_UI_RUNTIME must be headless, desktop, or mobile"));

  /* Case does not matter. DESKTOP mounts the Desktop runtime. */
  run_child(runtime_env_upper_desktop, &aborted, err, sizeof err);
  assert(!aborted);
  assert(strstr(err, "UiRuntime.Desktop mounted"));
  unsetenv("SCUZZ_UI_RUNTIME");
}

static void test_signal_list_get_survives_set(void) {
  SzSignalList *items;
  SzList *old;
  SzList *got;
  SzList *neu;
  SzString *a;
  SzString *b;

  a = sz_string_from_cstr("a");
  old = sz_list_cons(a, sz_list_nil());
  sz_string_free(a);
  items = sz_signal_list(old);
  sz_release(old);
  got = sz_signal_read(items);
  b = sz_string_from_cstr("b");
  neu = sz_list_cons(b, sz_list_nil());
  sz_string_free(b);
  sz_signal_list_set(items, neu);
  sz_release(neu);
  assert(sz_list_len(got) == 1);
  assert(got && got->head);
  assert(strcmp(sz_string_cstr((SzString *)got->head), "a") == 0);
  sz_release(got);
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
  dump = sz_signal_dump_json_string();
  s = strstr(sz_string_cstr(dump), "\"type\":\"str\"");
  assert(s);
  assert(strstr(s, "\"name\":\"draft\""));
  name = sz_string_from_cstr("draft");
  got = sz_property_signal_str(name);
  assert(strcmp(sz_string_cstr(got), "milk") == 0);
  sz_string_free(got);
  sz_signal_str_set(draft, "oat");
  got = sz_property_signal_str(name);
  assert(strcmp(sz_string_cstr(got), "oat") == 0);
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
  dump = sz_signal_dump_json_string();
  d = sz_string_cstr(dump);
  assert(strstr(d, "a\\\"b") != NULL);
  sz_string_free(dump);

  sz_signal_str_set(s, "a\nb");
  dump = sz_signal_dump_json_string();
  d = sz_string_cstr(dump);
  assert(strstr(d, "a\\nb") != NULL);
  sz_string_free(dump);

  for (i = 0; i < 2000; i++)
    big[i] = 'x';
  big[2000] = '\0';
  sz_signal_str_set(s, big);
  dump = sz_signal_dump_json_string();
  d = sz_string_cstr(dump);
  assert(strlen(d) > 2000);
  assert(strstr(d, big) != NULL);
  sz_string_free(dump);
  sz_signal_str_free(s);

  xs = sz_list_cons(sz_string_from_cstr("a\"b"),
                    sz_list_cons(sz_string_from_cstr("a\nb"), sz_list_nil()));
  items = sz_signal_list(xs);
  sz_signal_name(items, "xs");
  dump = sz_signal_dump_json_string();
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

static void test_signal_name_last_wins_per_kind(void) {
  SzSignalInt *count;
  SzSignalStr *label;
  SzString *name;
  SzString *got;

  /* Names are unique per kind: a str bind must not clear an int bind. */
  count = sz_signal_int(41);
  label = sz_signal_str("beans");
  sz_signal_name(count, "dup");
  sz_signal_name(label, "dup");
  name = sz_string_from_cstr("dup");
  assert(sz_property_signal_int(name) == 41);
  got = sz_property_signal_str(name);
  assert(strcmp(sz_string_cstr(got), "beans") == 0);
  sz_string_free(got);
  sz_release(name);
  sz_signal_int_free(count);
  sz_signal_str_free(label);
}

static void test_signal_name_unregistered_clears_nothing(void) {
  SzSignalInt *a;
  SzSignalInt *b;
  SzString *name;

  a = sz_signal_int(1);
  sz_signal_name(a, "keep");
  /* A wild pointer must not strip the name from a live signal. */
  b = sz_signal_int(2);
  sz_signal_int_free(b);
  sz_signal_name(b, "keep");
  name = sz_string_from_cstr("keep");
  assert(sz_property_signal_int(name) == 1);
  sz_release(name);
  sz_signal_int_free(a);
}

static void *map_label_int(void *value, void *env) {
  int64_t v = sz_unbox_i64(value);
  (void)env;
  return sz_string_from_int(v);
}

static int generic_map_calls;

static void *generic_record_value(void *value, void *env) {
  (void)env;
  generic_map_calls++;
  void *payload = sz_adt_payload(value);
  sz_retain(payload);
  return payload;
}

static void *generic_value_label(void *value, void *env) {
  (void)env;
  return sz_string_from_int(sz_unbox_i64(value));
}

static void test_generic_signal_chain(void) {
  size_t baseline, bytes, after;
  sz_alloc_stats(&bytes, &baseline);
  void *box = sz_box_i64(3);
  SzAdt *record = sz_adt_new(0, box);
  SzSignal *state = sz_signal_new(record, 4, NULL);
  SzSignal *count = sz_signal_derive(state, generic_record_value, NULL, 1, NULL);
  SzSignal *label = sz_signal_derive(count, generic_value_label, NULL, 2, NULL);
  sz_release(box);
  sz_release(record);
  generic_map_calls = 0;
  assert(strcmp(sz_signal_str_get(label), "3") == 0);
  assert(strcmp(sz_signal_str_get(label), "3") == 0);
  assert(generic_map_calls == 0);
  box = sz_box_i64(9);
  record = sz_adt_new(0, box);
  sz_signal_write(state, record);
  sz_release(box);
  sz_release(record);
  assert(strcmp(sz_signal_str_get(label), "9") == 0);
  assert(generic_map_calls == 1);
  sz_signal_free(state);
  assert(strcmp(sz_signal_str_get(label), "9") == 0);
  sz_signal_int_free(count);
  sz_signal_str_free(label);
  sz_alloc_stats(&bytes, &after);
  assert(after == baseline);
}

static void test_signal_map_source_free(void) {
  SzSignalInt *count;
  SzSignalStr *label;
  SzSignalInt *other;
  const char *s;

  count = sz_signal_int(7);
  label = sz_signal_derive(count, map_label_int, NULL, 2, NULL);
  s = sz_signal_str_get(label);
  assert(strcmp(s, "7") == 0);
  /* Freeing the source severs the map. A recycled address must not leak
     a foreign value into the derived signal. */
  sz_signal_int_free(count);
  other = sz_signal_int(99);
  s = sz_signal_str_get(label);
  assert(strcmp(s, "7") == 0);
  sz_signal_int_free(other);
  sz_signal_str_free(label);
}

static void test_signal_dump_long_name(void) {
  SzSignalInt *s;
  SzString *dump;
  char big[601];
  int i;

  for (i = 0; i < 600; i++)
    big[i] = (char)('a' + (i % 26));
  big[600] = '\0';
  s = sz_signal_int(3);
  sz_signal_name(s, big);
  dump = sz_signal_dump_json_string();
  /* The full name survives: no fixed-buffer truncation. */
  assert(strstr(sz_string_cstr(dump), big) != NULL);
  assert(strstr(sz_string_cstr(dump), "\"value\":3") != NULL);
  sz_string_free(dump);
  sz_signal_int_free(s);
}

static void test_signal_list_mixed_kinds(void) {
  SzSignalList *items;
  SzList *xs;
  SzString *dump;
  SzString *name;
  SzString *got;

  /* First head decides the String dump; a non-String head prints empty. */
  xs = sz_list_cons(sz_string_from_cstr("a"),
                    sz_list_cons(sz_box_i64(2), sz_list_nil()));
  items = sz_signal_list(xs);
  sz_signal_name(items, "mix");
  dump = sz_signal_dump_json_string();
  assert(strstr(sz_string_cstr(dump), "\"name\":\"mix\",\"value\":[\"a\",\"\"]") != NULL);
  sz_string_free(dump);
  name = sz_string_from_cstr("mix");
  got = sz_property_signal_list_at(name, 0);
  assert(strcmp(sz_string_cstr(got), "a") == 0);
  sz_string_free(got);
  got = sz_property_signal_list_at(name, 1);
  assert(strcmp(sz_string_cstr(got), "") == 0);
  sz_string_free(got);
  sz_release(name);
  sz_signal_list_free(items);
}

static void test_property_replay_str_list(void) {
  SzSignalStr *draft;
  SzSignalList *items;
  SzList *xs;
  SzString *name;
  SzString *got;

  setenv("SCUZZ_TESTRT", "1", 1);
  sz_testrt_oracles_refresh();
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
  sz_testrt_oracles_refresh();
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
  SzUiConfig cfg = {0};
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

static void test_hidden_editor_marks(void) {
  SzSignalStr *buffer = sz_signal_str("abc");
  SzSignalInt *shown = sz_signal_int(0);
  SzView *editor = sz_view_editor(buffer);
  SzView *root = sz_view_show_when(shown, 1, editor);
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *visible[1];
  SzList *data = NULL;
  int values[] = {0, 0, 1, 8, 0};
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 120;
  cfg.scale = 1;
  session = sz_ui_mount(&cfg, root);
  assert(session && sz_ui_pump_sync(session));
  assert(sz_view_collect_editors(root, visible, 1) == 0);
  for (int i = 4; i >= 0; i--) {
    void *value = sz_box_i64(values[i]);
    SzList *next = sz_list_cons(value, data);
    sz_release(value);
    sz_release(data);
    data = next;
  }
  assert(sz_io_unsafe_run(sz_lang_ui_set_editor_tokens(data)).ok);
  sz_release(data);
  assert(sz_view_editor_token_count(editor) == 1);
  sz_signal_int_set(shown, 1);
  assert(sz_ui_pump_sync(session));
  assert(sz_view_collect_editors(root, visible, 1) == 1);
  assert(visible[0] == editor && sz_view_editor_token_count(editor) == 1);
  sz_ui_unmount(session);
  sz_signal_int_free(shown);
  sz_signal_str_free(buffer);
}

static void test_view_editor(void) {
  SzSignalStr *buf, *field_sig;
  SzView *root, *ed, *field, *btn;
  SzUiConfig cfg = {0};
  SzUiSession *session;
  const SzTheme *theme = sz_theme_default();
  const char *path = "/tmp/scuzz_ui_inject_editor.json";
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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"a\",\"text\":\"a\"}]}");
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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"z\",\"text\":\"z\"},{\"op\":\"key\",\"key\":\"Enter\"},{\"op\":\"key\",\"key\":\"Tab\"},{\"op\":\"key\",\"key\":\"b\",\"text\":\"b\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "z\n  b") == 0);
  assert(strcmp(sz_signal_str_get(field_sig), "hia") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"fields\":[") != NULL);
  assert(strstr(body, "\"label\":\"item\",\"value\":\"hia\"") != NULL);
  assert(strstr(body, "\"editors\":[") != NULL);
  assert(strstr(body, "\"i\":0,\"target\":true,\"caret\":5,\"sel_start\":5,\"sel_end\":5,\"scroll_x\":0,\"scroll_y\":0,\"lines\":2") != NULL);
  assert(strstr(body, "\"value\":\"z\\n  b\"") != NULL);
  {
    const char *taps_sec = strstr(body, "\"taps\":[");
    assert(taps_sec != NULL);
    assert(strstr(taps_sec, "\"label\":\"Go\"") != NULL);
  }
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"caret\",\"offset\":2}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_editor_caret(ed) == 2);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"select\",\"start\":0,\"end\":2},{\"op\":\"key\",\"key\":\"x\",\"text\":\"x\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "x  b") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"select\",\"start\":0,\"end\":1},{\"op\":\"copy\"},{\"op\":\"key\",\"key\":\"End\"},{\"op\":\"paste\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "x  bx") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"text\",\"value\":\"ab\"},{\"op\":\"key\",\"key\":\"Enter\"},{\"op\":\"key\",\"key\":\"c\",\"text\":\"c\"},{\"op\":\"key\",\"key\":\"d\",\"text\":\"d\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "ab\ncd") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"ab\\ncd\"") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"caret\",\"offset\":3},{\"op\":\"key\",\"key\":\"Backspace\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "abcd") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"caret\",\"offset\":2},{\"op\":\"key\",\"key\":\"Enter\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "ab\ncd") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"select\",\"start\":0,\"end\":5},{\"op\":\"cut\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "") == 0);
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"paste\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "ab\ncd") == 0);

  /* No 256-byte cap on the editor buffer. */
  for (i = 0; i < 300; i++)
    long_s[i] = 'a';
  long_s[300] = '\0';
  sz_signal_str_set(buf, long_s);
  assert(sz_view_set_editor_caret(ed, 300));
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"dump\"}]}");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "\"editors\":[") != NULL);
  assert(strstr(body, long_s) != NULL);
  assert(strstr(body, "\"caret\":300") != NULL);
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
  SzUiConfig cfg = {0};
  SzUiSession *session;
  const SzTheme *theme = sz_theme_default();
  const char *path = "/tmp/scuzz_ui_editor_viewport.json";
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
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = "End";
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));
  assert(sz_view_editor_scroll_x(ed) > 0.f);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"editors\":[") != NULL);
  assert(strstr(body, "\"scroll_x\":0") == NULL);
  free(body);
  {
    float x0 = sz_view_editor_scroll_x(ed);
    fr = sz_view_frame(ed);
    memset(&ev, 0, sizeof(ev));
    ev.kind = SZ_INPUT_SCROLL;
    ev.x = fr.x + 8.f;
    ev.y = fr.y + 8.f;
    ev.dx = -20.f;
    assert(sz_ui_inject_sync(session, &ev));
    assert(sz_view_editor_scroll_x(ed) < x0);
  }

  /* Tall file: caret at end pans vertically. Paint visible lines only. */
  tall[0] = '\0';
  for (i = 0; i < 40; i++)
    strcat(tall, "x\n");
  strcat(tall, "z");
  sz_signal_str_set(buf, tall);
  sz_view_set_editor_caret(ed, (int)strlen(tall));
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"caret\",\"offset\":80},{\"op\":\"key\",\"key\":\"End\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_view_editor_scroll_y(ed) > 0.f);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"scroll_y\":0") == NULL);
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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"Home\"}]}");
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"PageUp\"},{\"op\":\"key\",\"key\":\"PageDown\"}]}");
  assert(sz_ui_pump_sync(session));

  sz_ui_unmount(session);
  sz_signal_str_free(buf);
  remove(path);
  remove(dump);
}

static void test_view_editor_undo_gutter(void) {
  SzSignalStr *buf;
  SzView *root, *ed;
  SzUiConfig cfg = {0};
  SzUiSession *session;
  const char *path = "/tmp/scuzz_ui_editor_undo.json";
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"a\",\"text\":\"a\"},{\"op\":\"key\",\"key\":\"b\",\"text\":\"b\"},{\"op\":\"key\",\"key\":\"c\",\"text\":\"c\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "abc") == 0);
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"z\",\"mods\":[\"ctrl\"]}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(buf), "ab") == 0);
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"y\",\"mods\":[\"ctrl\"]}]}");
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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"dump\"}]}");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "\"lines\":3") != NULL);
  assert(strstr(body, "\"diags\":[{\"line\":1,\"severity\":1},{\"line\":2,\"severity\":2}]") != NULL);
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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"dump\"}]}");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "{\"line\":1,\"severity\":1}") != NULL);
  free(body);
  {
    SzIoResult r = sz_io_unsafe_run(sz_lang_ui_set_editor_diagnostics(NULL));
    assert(r.ok);
  }
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"dump\"}]}");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "\"diags\":[]") != NULL);
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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"dump\"}]}");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "\"tokens\":1") != NULL);
  assert(strstr(body, "\"inlays\":1") != NULL);
  assert(strstr(body, "\"folds\":1") != NULL);
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
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"dump\"}]}");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "\"tokens\":0") != NULL);
  assert(strstr(body, "\"inlays\":0") != NULL);
  assert(strstr(body, "\"folds\":0") != NULL);
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
  SzUiConfig cfg = {0};
  SzUiSession *session;
  const char *path = "/tmp/scuzz_ui_a8.json";
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
    split = sz_view_split(frac, "Primary pane", sz_view_text("L"), sz_view_text("R"));
    sz_view_add_child(col, field);
    sz_view_add_child(col, btn);
    sz_view_add_child(col, split);
    sz_view_add_child(root, col);
  }
  pop_field = sz_view_text_field(pop, "pop");
  overlay = sz_view_overlay(open, "Dialog", pop_field);
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"p\",\"text\":\"p\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(strcmp(sz_signal_str_get(pop), "p") == 0);
  assert(strcmp(sz_signal_str_get(draft), "") == 0);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"Escape\"}]}");
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"a\",\"text\":\"a\"}]}");
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
  ev.x = fr.x + (fr.w - 6.f) * 0.25f + 3.f;
  assert(sz_ui_inject_sync(session, &ev));
  ev.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_inject_sync(session, &ev));
  assert(sz_ui_pump_sync(session));
  assert(sz_view_split_frac(split) == 25);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"dump\"}]}");
  assert(sz_ui_pump_sync(session));
  body = slurp_cstr(dump);
  assert(strstr(body, "\"title\":\"Hello\"") != NULL);
  assert(strstr(body, "\"splits\":[") != NULL);
  assert(strstr(body, "\"frac\":25") != NULL);
  assert(strstr(body, "\"overlays\":[") != NULL);
  assert(strstr(body, "\"open\":false") != NULL);
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

static void test_app_shell_and_tabs(void) {
  for (int scale = 1; scale <= 2; scale++) {
    SzSignalInt *selected = sz_signal_int(0);
    SzSignalInt *count = sz_signal_int(0);
    SzSignalStr *draft = sz_signal_str("keep this text");
    SzView *sections = sz_view_column(), *live = sz_view_column();
    SzView *field = sz_view_text_field(draft, "Draft");
    SzView *actions = sz_view_wrap();
    SzView *action = sz_view_button("Save draft", counter_tap, count);
    sz_view_add_child(actions, action);
    sz_view_add_child(live, field);
    for (int i = 0; i < 20; i++) sz_view_add_child(live, sz_view_text("Long panel content"));
    sz_view_add_child(sections, sz_view_section("live", "Live", live));
    sz_view_add_child(sections, sz_view_section("source", "Source", sz_view_button("Source action", counter_tap, count)));
    SzView *tabs = sz_view_tabs(selected, sections);
    SzView *bar = sz_view_app_bar(sz_view_text("A practical application"), actions);
    SzView *root = sz_view_app_shell(bar, tabs);
    SzTheme theme = *sz_theme_default();
    theme.px_scale = (float)scale;
    theme.font_px *= scale; theme.control_h *= scale;
    theme.pad *= scale; theme.gap *= scale;
    for (int width = 320; width <= 800; width += 480) {
      sz_view_layout(root, width * scale, 720.f * scale, &theme);
      SzRect b = sz_view_frame(bar), a = sz_view_frame(action), t = sz_view_frame(tabs);
      assert(b.y == 0.f && t.y == b.h);
      assert(fabsf(t.h + b.h - 720.f * scale) < 0.1f);
      assert(a.x >= 0.f && a.x + a.w <= width * scale);
      assert(a.y >= 0.f && a.y + a.h <= b.h);
      assert(sz_view_frame(field).x < 100.f * scale);
      assert(!sz_view_tap_label(root, "Source action"));
      SzView *scrolls[4];
      assert(sz_view_collect_scrolls(root, scrolls, 4) == 2);
      sz_view_scroll_by(scrolls[1], 60.f);
      sz_view_layout(root, width * scale, 240.f * scale, &theme);
      float offset = sz_view_scroll_y(scrolls[1]);
      assert(offset > 0.f);
      assert(sz_view_frame(bar).y == 0.f);
      assert(sz_view_tap_label(root, "Live"));
      assert(sz_view_handle_key(root, "ArrowRight", "", 0));
      assert(sz_signal_int_get(selected) == 0);
      assert(sz_view_handle_key(root, "Enter", "", 0));
      assert(sz_signal_int_get(selected) == 1);
      sz_view_layout(root, width * scale, 240.f * scale, &theme);
      SzView *fields[4];
      assert(sz_view_collect_text_fields(root, fields, 4) == 0);
      assert(sz_view_tap_label(root, "Source action"));
      assert(sz_view_tap_label(root, "Source"));
      assert(sz_view_handle_key(root, "ArrowRight", "", 0));
      assert(sz_view_handle_key(root, "Space", "", 0));
      assert(sz_signal_int_get(selected) == 0);
      sz_view_layout(root, width * scale, 240.f * scale, &theme);
      assert(fabsf(sz_view_scroll_y(scrolls[1]) - offset) < 0.1f);
      assert(strcmp(sz_signal_str_get(draft), "keep this text") == 0);
      assert(sz_view_handle_key(root, "End", "", 0));
      assert(sz_view_handle_key(root, "Home", "", 0));
      assert(sz_view_handle_key(root, "Enter", "", 0));
      assert(sz_signal_int_get(selected) == 0);
      SzString *dump = sz_view_a11y_dump(root);
      assert(strstr(sz_string_cstr(dump), "appbar:App bar"));
      assert(strstr(sz_string_cstr(dump), "tab:Live=1"));
      assert(strstr(sz_string_cstr(dump), "tabpanel:Live"));
      assert(!strstr(sz_string_cstr(dump), "tabpanel:Source"));
      sz_release(dump);
    }
    sz_view_free(root);
    sz_signal_str_free(draft); sz_signal_int_free(selected); sz_signal_int_free(count);
  }
}

static void test_tabs_navigate(void) {
  SzSignalInt *selected = sz_signal_int(0);
  SzView *sections = sz_view_column();
  SzView *root;
  sz_view_add_child(sections, sz_view_section("run", "Run", sz_view_text("Run")));
  sz_view_add_child(sections, sz_view_section("search", "Search", sz_view_text("Search")));
  root = sz_view_tabs(selected, sections);
  sz_view_layout(root, 640.f, 480.f, sz_theme_default());
  assert(sz_view_navigate(root, "search"));
  assert(sz_signal_int_get(selected) == 1);
  assert(!sz_view_navigate(root, "missing"));
  sz_view_free(root);
  sz_signal_int_free(selected);
}

static void test_docs_nav_widgets(void) {
  SzSignalInt *selected = sz_signal_int(0);
  SzView *sections = sz_view_column();
  SzView *page;
  SzView *root;
  SzView *taps[16];
  SzView *crumb;
  SzView *img;
  SzView *icon;
  SzString *dump;
  SzView *docs;
  SzView *cur;
  SzRect docs_r;
  SzRect cur_r;
  float sep_w;
  const SzTheme *theme = sz_theme_default();
  float image_h;

  page = sz_view_column();
  sz_view_add_child(page, sz_view_link("Open GUI", "gui"));
  sz_view_add_child(page, sz_view_nav_tile('W', "Ship to the web", "web"));
  crumb = sz_view_breadcrumb();
  docs = sz_view_link("Docs", "start");
  cur = sz_view_text("Start");
  sz_view_add_child(crumb, docs);
  sz_view_add_child(crumb, cur);
  sz_view_add_child(page, crumb);
  img = image_fixture(80, 32, 0xFFE8EF48u, "Scuzz Docs");
  icon = sz_view_icon('B', 0xFF923D24u);
  sz_view_add_child(page, img);
  sz_view_add_child(page, icon);
  sz_view_add_child(sections, sz_view_section("start", "Start", page));
  sz_view_add_child(sections, sz_view_section("gui", "GUI", sz_view_text("GUI")));
  sz_view_add_child(sections, sz_view_section("web", "Web", sz_view_text("Web")));
  root = sz_view_index_book(selected, sections);
  sz_view_layout(root, 640.f, 480.f, theme);
  docs_r = sz_view_frame(docs);
  cur_r = sz_view_frame(cur);
  sep_w = 8.f;
  assert(docs_r.h < theme->control_h);
  assert(cur_r.x >= docs_r.x + docs_r.w + sep_w);
  assert(fabsf((docs_r.y + docs_r.h * 0.5f) - (cur_r.y + cur_r.h * 0.5f)) < 2.f);
  dump = sz_view_a11y_dump(root);
  assert(strstr(sz_string_cstr(dump), "link:Open GUI") != NULL);
  assert(strstr(sz_string_cstr(dump), "navtile:Ship to the web") != NULL);
  assert(strstr(sz_string_cstr(dump), "breadcrumb:Breadcrumb") != NULL);
  assert(strstr(sz_string_cstr(dump), "image:Scuzz Docs") != NULL);
  assert(strstr(sz_string_cstr(dump), "icon:B") != NULL);
  sz_string_free(dump);
  image_h = sz_view_frame(img).h;
  assert(fabsf(image_h - 32.f) < 0.01f);
  assert(sz_view_collect_tap_targets(root, taps, 16) >= 5);
  assert(sz_view_is_tap_target(taps[0]));
  assert(sz_view_tap_label(root, "Open GUI"));
  sz_view_layout(root, 640.f, 480.f, theme);
  assert(sz_signal_int_get(selected) == 1);
  assert(sz_view_navigate(root, "web"));
  assert(sz_signal_int_get(selected) == 2);
  assert(!sz_view_navigate(root, "missing"));
  assert(!sz_view_navigate(root, "https://example.com"));
  sz_view_free(root);
  sz_signal_int_free(selected);
}

static void test_index_book_long_index(void) {
  SzSignalInt *selected = sz_signal_int(0);
  SzView *sections = sz_view_column();
  SzView *root, *tabs[20], *scrolls[4];
  const SzTheme *theme = sz_theme_default();
  char title[32];
  for (int i = 0; i < 16; i++) {
    snprintf(title, sizeof title, "Section %d", i + 1);
    sz_view_add_child(sections, sz_view_section(title, title, sz_view_text(title)));
  }
  root = sz_view_index_book(selected, sections);
  sz_view_layout(root, 320.f, 240.f, theme);
  assert(sz_view_collect_scrolls(root, scrolls, 4) == 2);
  assert(sz_view_frame(scrolls[0]).h <= 96.f);
  sz_view_scroll_by(scrolls[0], 1000.f);
  sz_view_layout(root, 320.f, 240.f, theme);
  assert(sz_view_collect_tap_targets(root, tabs, 20) == 16);
  SzRect last = sz_view_frame(tabs[15]);
  assert(last.y >= 0.f && last.y + last.h <= 96.f);
  assert(sz_view_handle_tap(root, last.x + last.w * 0.5f,
                           last.y + last.h * 0.5f));
  assert(sz_signal_int_get(selected) == 15);
  sz_view_free(root);
  sz_signal_int_free(selected);
}

static void test_index_book_navigation_and_resize(void) {
  for (int scale = 1; scale <= 2; scale++) {
    SzSignalInt *selected = sz_signal_int(0);
    SzSignalInt *count = sz_signal_int(0);
    SzView *sections = sz_view_column();
    SzView *page = sz_view_column();
    SzView *other = sz_view_button("Other action", counter_tap, count);
    SzView *root, *taps[32], *scrolls[8];
    SzTheme scaled = *sz_theme_default();
    scaled.px_scale = (float)scale;
    scaled.font_px *= scale;
    scaled.control_h *= scale;
    scaled.pad *= scale;
    scaled.gap *= scale;
    const SzTheme *theme = &scaled;
    for (int i = 0; i < 12; i++)
      sz_view_add_child(page, sz_view_button("Count", counter_tap, count));
    sz_view_add_child(sections, sz_view_section("a", "Page A", page));
    sz_view_add_child(sections, sz_view_section("b", "Page B", other));
    root = sz_view_index_book(selected, sections);
    sz_view_layout(root, 320.f * scale, 240.f * scale, theme);
    assert(sz_view_collect_tap_targets(root, taps, 32) == 14);
    assert(sz_view_frame(page).y > sz_view_frame(taps[0]).y);
    assert(!sz_view_tap_label(root, "Other action"));
    assert(sz_view_tap_label(root, "Count"));
    assert(sz_signal_int_get(count) == 1);
    assert(sz_view_collect_scrolls(root, scrolls, 8) == 2);
    SzView *page_scroll = scrolls[1];
    sz_view_scroll_by(page_scroll, 80.f * scale);
    sz_view_layout(root, 320.f * scale, 240.f * scale, theme);
    float offset = sz_view_scroll_y(page_scroll);
    assert(offset > 0.f);
    assert(sz_view_tap_label(root, "Page B"));
    sz_view_layout(root, 320.f * scale, 240.f * scale, theme);
    assert(sz_signal_int_get(selected) == 1);
    assert(sz_view_collect_tap_targets(root, taps, 32) == 3);
    assert(!sz_view_tap_label(root, "Count"));
    assert(sz_view_tap_label(root, "Other action"));
    assert(sz_signal_int_get(count) == 2);
    assert(sz_view_tap_label(root, "Page A"));
    sz_view_layout(root, 800.f * scale, 240.f * scale, theme);
    assert(sz_view_frame(page).x >= 200.f * scale);
    assert(fabsf(sz_view_scroll_y(page_scroll) - offset) < 0.1f);
    assert(sz_signal_int_get(count) == 2);
    sz_view_layout(root, 320.f * scale, 240.f * scale, theme);
    assert(fabsf(sz_view_scroll_y(page_scroll) - offset) < 0.1f);
    assert(sz_view_collect_tap_targets(root, taps, 32) == 14);
    for (int i = 0; i < 2; i++) {
      SzRect f = sz_view_frame(taps[i]);
      assert(f.x >= 0.f && f.x + f.w <= 320.f * scale);
    }
    assert(sz_view_handle_key(root, "ArrowDown", "", 0));
    assert(sz_signal_int_get(selected) == 0);
    assert(sz_view_handle_key(root, "Enter", "", 0));
    assert(sz_signal_int_get(selected) == 1);
    sz_view_free(root);
    sz_signal_int_free(selected);
    sz_signal_int_free(count);
  }
}

static void test_view_focus_group_keys(void) {
  SzSignalInt *a;
  SzSignalInt *b;
  SzSignalInt *go;
  SzSignalInt *open;
  SzSignalStr *draft;
  SzView *root, *group, *ba, *bb, *btn, *field, *overlay;
  SzUiConfig cfg = {0};
  SzUiSession *session;
  const char *path = "/tmp/scuzz_ui_focus_group.json";
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
    overlay = sz_view_overlay(open, "Dialog", sz_view_text("Palette"));
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

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"tap\",\"i\":0},{\"op\":\"dump\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(a) == 1);
  assert(!sz_view_has_focused_text_field(root));
  assert(strcmp(sz_view_focus_kind(root), "button:sample.txt") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"focus\":\"button:sample.txt\"") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"ArrowDown\"},{\"op\":\"dump\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(a) == 1);
  assert(sz_signal_int_get(b) == 0);
  assert(strcmp(sz_view_focus_kind(root), "button:scuzz.toml") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"focus\":\"button:scuzz.toml\"") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"Enter\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(b) == 1);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"tap\",\"i\":0}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(a) == 2);
  sz_signal_int_set(open, 1);
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"ArrowDown\"},{\"op\":\"dump\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(b) == 1);
  assert(strcmp(sz_view_focus_kind(root), "overlay:Dialog") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"focus\":\"overlay:Dialog\"") != NULL);
  free(body);

  sz_signal_int_set(open, 0);
  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"Space\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(a) == 3);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"tap\",\"i\":2},{\"op\":\"dump\"}]}");
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(go) == 1);
  assert(strcmp(sz_view_focus_kind(root), "button:Go") == 0);
  body = slurp_cstr(dump);
  assert(strstr(body, "\"focus\":\"button:Go\"") != NULL);
  free(body);

  write_stamp(path, "{\"v\":1,\"kind\":\"inject\",\"events\":[{\"op\":\"key\",\"key\":\"ArrowDown\"}]}");
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

  /* Inject path: TEXT_EDIT empty -> backspace. */
  {
    SzUiConfig cfg = {0};
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
  SzUiConfig cfg = {0};
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
  SzUiConfig cfg = {0};
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
  SzUiConfig cfg = {0};
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

static void *map_count_label(void *value, void *env) {
  int64_t v = sz_unbox_i64(value);
  (void)env;
  return sz_string_from_int(v);
}

/* Counter-shaped UI (Signal.map + bindText + taps): live_count flat over pumps. */
static void test_alloc_counter_pump_flat(void) {
  SzUiConfig cfg = {0};
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
  label = sz_signal_derive(count, map_count_label, NULL, 2, NULL);
  root = sz_view_column();
  sz_view_add_child(root, sz_lang_view_bind_text(label));
  btn = sz_view_button("+", counter_tap, count);
  sz_view_add_child(root, btn);

  session = sz_ui_mount(&cfg, root);
  assert(session);
  setenv("SCUZZ_TESTRT", "1", 1);
  sz_testrt_oracles_refresh();
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
   * the mapped label gains digits (for example "9" -> "10"). Same allocation count. */
  assert(live_count == base_count);
  assert(live_bytes <= base_bytes + 32);
  assert(max_count <= base_count + 16);

  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_str_free(label);
  sz_signal_int_free(count);
  unsetenv("SCUZZ_TESTRT");
  sz_testrt_oracles_refresh();
}

/* Headless button with a captured list env: tap still fires after the
 * caller drops the list, and sz_view_free returns live_count to baseline. */
static void list_env_tap(SzView *self, void *env) {
  SzSignalInt *count = (SzSignalInt *)sz_list_head((SzList *)env);
  (void)self;
  sz_signal_int_set(count, sz_signal_int_get(count) + 1);
}

static void test_tap_env_retain_release(void) {
  SzUiConfig cfg = {0};
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

static SzView *each_row_identity(SzString *item, void *env) {
  (void)env;
  sz_retain(item);
  return (SzView *)item;
}

/* One view list per write. The caller releases the button after cons. */
static SzList *orphan_button_list(void *env) {
  SzView *btn = sz_view_button("go", noop_tap, env);
  SzList *xs = sz_list_cons(btn, sz_list_nil());
  sz_release(btn);
  return xs;
}

static void test_each_orphan_views_freed(void) {
  SzSignalList *items;
  SzList *xs, *env, *held;
  SzString *cap;
  SzView *list;
  const SzTheme *theme = sz_theme_default();
  size_t base_count = 0, base_bytes = 0;
  size_t live_count = 0, live_bytes = 0;

  sz_alloc_stats(&base_bytes, &base_count);
  {
    SzView *start = sz_view_text("start");
    xs = sz_list_cons(start, sz_list_nil());
    sz_release(start);
  }
  items = sz_signal_list(xs);
  sz_release(xs);
  list = sz_view_each_map(items, each_row_identity, NULL);
  sz_view_layout(list, 200.f, 120.f, theme);
  assert(sz_rc_count(sz_list_head(sz_signal_list_get(items))) == 2);

  /* Two writes before the next layout. The first list is never mounted.
   * The signal alone holds it, so that write frees its button. */
  cap = sz_string_from_cstr("captured");
  env = sz_list_cons(cap, sz_list_nil());
  sz_release(cap);
  xs = orphan_button_list(env);
  sz_signal_list_set(items, xs);
  sz_release(xs);
  xs = orphan_button_list(env);
  sz_signal_list_set(items, xs);
  sz_release(xs);
  sz_release(env);
  sz_view_layout(list, 200.f, 120.f, theme);
  sz_view_free(list);
  sz_signal_list_free(items);
  sz_alloc_stats(&live_bytes, &live_count);
  assert(live_count == base_count);

  /* A list another holder still reads keeps its views. List.head does not
   * retain, so a view pulled out by hand stays a borrow. */
  {
    SzView *kept = sz_view_text("kept");
    SzView *next = sz_view_text("next");
    SzView *pulled;
    uint32_t rc;
    xs = sz_list_cons(kept, sz_list_nil());
    sz_release(kept);
    items = sz_signal_list(xs);
    held = xs;
    xs = sz_list_cons(next, sz_list_nil());
    sz_release(next);
    sz_signal_list_set(items, xs);
    sz_release(xs);
    assert(sz_alloc_kind_of(held->head) == SZ_RC_VIEW);
    assert(sz_rc_count(held->head) == 1);
    rc = sz_rc_count(held->head);
    pulled = (SzView *)sz_list_head(held);
    assert(pulled == (SzView *)held->head);
    assert(sz_rc_count(pulled) == rc);
    sz_release(held);
    sz_signal_list_free(items);
  }
  sz_alloc_stats(&live_bytes, &live_count);
  assert(live_count == base_count);
}

static void test_view_show(void) {
  SzView *v = sz_view_text("milk");
  SzString *s = sz_view_show(v);
  SzSignalInt *n = sz_signal_int(1);
  SzString *sig = sz_signal_show(n);
  assert(strcmp(sz_string_cstr(s), "text:milk") == 0);
  assert(strcmp(sz_string_cstr(sig), "<signal>") == 0);
  sz_string_free(s);
  sz_string_free(sig);
  sz_view_free(v);
  sz_signal_int_free(n);
}

#ifdef __APPLE__
#define RELOAD_A "build/reload_a.dylib"
#define RELOAD_B "build/reload_b.dylib"
#define RELOAD_BAD "build/reload_bad.dylib"
#else
#define RELOAD_A "build/reload_a.so"
#define RELOAD_B "build/reload_b.so"
#define RELOAD_BAD "build/reload_bad.so"
#endif

SzIo *scuzz_reload_later;

static void test_session_load_code(void) {
  static char source[] = "old";
  SzUiConfig cfg = {0};
  SzUiSession *session;
  SzView *root;
  SzSignalInt *count;
  SzString *a11y, *dump1, *dump2, *cached;

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
  sz_ui_session_set_rebuild(session, NULL, count, "counter:int");
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

  assert(!sz_ui_session_load_code(session, RELOAD_BAD));
  assert(sz_ui_session_reload(session));
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:A") != NULL);
  sz_string_free(a11y);

  sz_signal_int_set(count, 8);
  dump1 = sz_signal_dump_json_string();
  assert(sz_ui_session_load_code(session, RELOAD_B));
  assert(sz_ui_session_reload(session));
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:B") != NULL);
  assert(strstr(sz_string_cstr(a11y), "text:A") == NULL);
  sz_string_free(a11y);
  dump2 = sz_signal_dump_json_string();
  assert(strcmp(sz_string_cstr(dump1), sz_string_cstr(dump2)) == 0);
  assert(sz_signal_int_get(count) == 8);
  sz_string_free(dump1);
  sz_string_free(dump2);

  SzIoResult later = sz_io_unsafe_run(scuzz_reload_later);
  scuzz_reload_later = NULL;
  assert(later.ok);
  sz_release(later.value);
  assert(sz_signal_int_get(count) == 9);

  cached = sz_string_lit(source);
  assert(cached == sz_string_lit(source));
  sz_ui_unmount(session);
  /* A new code image can use the same source address. */
  memcpy(source, "new", sizeof source);
  assert(strcmp(sz_string_cstr(cached), "old") == 0);
  assert(strcmp(sz_string_cstr(sz_string_lit(source)), "new") == 0);
  sz_signal_int_free(count);
}

static void test_stamp_loads_reload_code(void) {
  SzUiConfig cfg = {0};
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
  sz_ui_session_set_rebuild(session, init_code_factory, count, "counter:int");
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
  dump1 = sz_signal_dump_json_string();
  copy_file_test(RELOAD_B, code);
  write_stamp(stamp, "3");
  assert(sz_ui_pump_sync(session));
  a11y = sz_view_a11y_dump(sz_ui_session_root(session));
  assert(strstr(sz_string_cstr(a11y), "text:B") != NULL);
  assert(strstr(sz_string_cstr(a11y), "text:A") == NULL);
  sz_string_free(a11y);
  dump2 = sz_signal_dump_json_string();
  assert(strcmp(sz_string_cstr(dump1), sz_string_cstr(dump2)) == 0);
  assert(sz_signal_int_get(count) == 8);
  sz_string_free(dump1);
  sz_string_free(dump2);

  sz_release(scuzz_reload_later);
  scuzz_reload_later = NULL;
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

static void test_mirror_signal_session(void) {
  SzString *name = sz_string_from_cstr("$mirror");
  void *initial = sz_box_i64(1);
  SzSignal *pool = sz_signal_new(initial, 4, name);
  size_t before_bytes, before_count, after_bytes, after_count;
  sz_release(initial);
  setenv("SCUZZ_TESTRT", "1", 1);
  sz_testrt_oracles_refresh();
  sz_alloc_stats(&before_bytes, &before_count);
  sz_signal_session_push();
  void *next = sz_box_i64(2);
  sz_signal_write(pool, next);
  sz_release(next);
  sz_signal_session_push();
  next = sz_box_i64(3);
  sz_signal_write(pool, next);
  sz_signal_new(next, 4, name);
  sz_release(next);
  SzString *dump = sz_signal_dump_json_string();
  assert(strcmp(sz_string_cstr(dump), "[]") == 0);
  sz_release(dump);
  sz_signal_session_pop();
  next = sz_signal_read(pool);
  assert(sz_unbox_i64(next) == 2);
  sz_release(next);
  sz_signal_session_pop();
  next = sz_signal_read(pool);
  assert(sz_unbox_i64(next) == 1);
  sz_release(next);
  sz_alloc_stats(&after_bytes, &after_count);
  assert(after_count == before_count && after_bytes == before_bytes);
  sz_signal_free(pool);
  sz_release(name);
  SzSignalInt *visible = sz_signal_int(7);
  sz_signal_name(visible, "visible");
  dump = sz_signal_dump_json_string();
  assert(strcmp(sz_string_cstr(dump),
                "[{\"id\":0,\"type\":\"int\",\"name\":\"visible\",\"value\":7}]") == 0);
  sz_release(dump);
  sz_signal_int_free(visible);
  void *view = getenv("SCUZZ_EVAL_MIRROR")
                   ? (void *)sz_adt_new(sz_eval_value_tag_base + 26, NULL)
                   : (void *)sz_view_text("value");
  SzString *view_name = sz_string_from_cstr("view");
  SzSignal *view_signal = sz_signal_new(view, 4, view_name);
  SzList *views = sz_list_cons(view, sz_list_nil());
  void *list = getenv("SCUZZ_EVAL_MIRROR")
                   ? (void *)sz_adt_new(sz_eval_value_tag_base + 4, views)
                   : (void *)views;
  if (list == views) sz_retain(list);
  SzString *list_name = sz_string_from_cstr("views");
  SzSignal *list_signal = sz_signal_new(list, 4, list_name);
  dump = sz_signal_dump_json_string();
  assert(strstr(sz_string_cstr(dump),
                "\"name\":\"view\",\"value\":\"<handle>\"") != NULL);
  assert(strstr(sz_string_cstr(dump),
                "\"name\":\"views\",\"value\":[\"<handle>\"]") != NULL);
  sz_release(dump);
  sz_signal_free(view_signal);
  sz_signal_free(list_signal);
  sz_release(view_name);
  sz_release(list_name);
  sz_release(list);
  sz_release(views);
  sz_release(view);
}

static void *scope_map_with_cell(void *value, void *env) {
  (void)env;
  SzSignalInt *late = sz_signal_int(sz_unbox_i64(value));
  sz_signal_name(late, "late row cell");
  return sz_string_from_int(sz_unbox_i64(value));
}

static void test_signal_name_index(void) {
  size_t before_bytes, before_count, bytes, count;
  sz_alloc_stats(&before_bytes, &before_count);
  const int length = 4096;
  SzSignal **ints = sz_alloc_zero((size_t)length * sizeof(*ints));
  SzSignal **texts = sz_alloc_zero((size_t)length * sizeof(*texts));
  for (int i = 0; i < length; i++) {
    char key[64];
    snprintf(key, sizeof(key), "index-cell-%d", i);
    SzString *name = sz_string_from_cstr(key);
    void *value = sz_box_i64(i);
    ints[i] = sz_signal_new(value, 1, name);
    SzString *text = sz_string_from_cstr("value");
    texts[i] = sz_signal_new(text, 2, name);
    sz_release(text);
    sz_release(value);
    sz_release(name);
  }
  for (int i = length - 1; i >= 0; i--) {
    char key[64];
    snprintf(key, sizeof(key), "index-cell-%d", i);
    SzString *name = sz_string_from_cstr(key);
    assert(sz_property_signal_int(name) == i);
    SzString *text = sz_property_signal_str(name);
    assert(strcmp(sz_string_cstr(text), "value") == 0);
    sz_release(text);
    sz_release(name);
  }
  sz_signal_name(ints[0], "index-cell-1");
  SzString *name = sz_string_from_cstr("index-cell-1");
  assert(sz_property_signal_int(name) == 0);
  SzString *text = sz_property_signal_str(name);
  assert(strcmp(sz_string_cstr(text), "value") == 0);
  sz_release(text);
  sz_signal_name(ints[0], "");
  sz_signal_name(ints[1], "index-cell-1");
  assert(sz_property_signal_int(name) == 1);
  sz_release(name);
  for (int i = 0; i < length; i++) {
    int index = i * 31 % length;
    sz_signal_free(ints[index]);
    sz_signal_free(texts[index]);
  }
  sz_free(ints);
  sz_free(texts);
  sz_alloc_stats(&bytes, &count);
  assert(bytes == before_bytes && count == before_count);
}

static void test_signal_scope_ownership(void) {
  size_t before_bytes, before_count, bytes, count;
  sz_alloc_stats(&before_bytes, &before_count);
  const int unrelated_count = 50000;
  SzSignal **unrelated = sz_alloc_zero((size_t)unrelated_count * sizeof(*unrelated));
  for (int i = 0; i < unrelated_count; i++) unrelated[i] = sz_signal_int(i);
  SzSignalScope *parent = sz_signal_scope_new();
  SzSignalScope *previous = sz_signal_scope_enter(parent);
  SzSignalInt *source = sz_signal_int(1);
  (void)sz_signal_derive(source, map_label_int, NULL, 2, NULL);
  sz_signal_scope_enter(previous);
  SzSignalStr *survivor = sz_signal_derive(source, map_label_int, NULL, 2, NULL);
  SzSignalScope *child = sz_signal_scope_new();
  previous = sz_signal_scope_enter(child);
  SzSignalStr *nested = sz_signal_derive(source, scope_map_with_cell, NULL, 2, NULL);
  SzSignalInt *local = sz_signal_int(2);
  SzSignalStr *mapped = sz_signal_derive(local, scope_map_with_cell, NULL, 2, NULL);
  sz_signal_scope_enter(previous);
  SzSignalInt *outside = sz_signal_int(99);
  sz_signal_int_set(source, 7);
  sz_signal_scope_free(parent);
  assert(strcmp(sz_signal_str_get(survivor), "7") == 0);
  assert(strcmp(sz_signal_str_get(nested), "7") == 0);
  sz_signal_int_set(local, 3);
  assert(strcmp(sz_signal_str_get(mapped), "3") == 0);
  sz_signal_scope_free(child);
  assert(sz_signal_int_get(outside) == 99);
  SzString *dump = sz_signal_dump_json_string();
  assert(!strstr(sz_string_cstr(dump), "late row cell"));
  sz_release(dump);
  sz_signal_str_free(survivor);
  sz_signal_int_free(outside);
  for (int i = 0; i < unrelated_count; i++) {
    assert(sz_signal_int_get(unrelated[i]) == i);
    sz_signal_free(unrelated[i]);
  }
  sz_free(unrelated);
  sz_alloc_stats(&bytes, &count);
  assert(bytes == before_bytes && count == before_count);
}

static void keyboard_count(SzView *view, void *env) {
  (void)view;
  SzSignalInt *count = env;
  sz_signal_int_set(count, sz_signal_int_get(count) + 1);
}

static void test_keyboard_controls(void) {
  SzSignalInt *count = sz_signal_int(0);
  SzSignalInt *value = sz_signal_int(50);
  SzSignalInt *checked = sz_signal_int(0);
  SzSignalInt *shown = sz_signal_int(0);
  SzSignalStr *text = sz_signal_str("");
  SzView *root = sz_view_column();
  SzView *first = sz_view_button("First", keyboard_count, count);
  SzView *field = sz_view_text_field(text, "Name");
  SzView *slider = sz_view_slider(value, "Level", 0, 100, 1);
  SzView *toggle = sz_view_checkbox(checked, "Check");
  SzView *last = sz_view_button("Last", keyboard_count, count);
  sz_view_add_child(root, first);
  sz_view_add_child(root, field);
  sz_view_add_child(root, slider);
  sz_view_add_child(root, toggle);
  sz_view_add_child(root, sz_view_show_when(shown, 1, sz_view_button("Hidden", NULL, NULL)));
  sz_view_add_child(root, last);
  sz_view_layout(root, 320, 600, sz_theme_default());
  assert(sz_view_handle_key(root, "Tab", "", 0));
  assert(sz_view_focused(root) == first);
  assert(!strcmp(sz_view_focus_kind(root), "button:First"));
  assert(sz_view_handle_key(root, "Enter", "", 0));
  assert(sz_signal_int_get(count) == 1 && sz_view_focused(root) == first);
  sz_view_handle_key(root, "x", "x", 0);
  assert(!strcmp(sz_signal_str_get(text), ""));
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == field);
  sz_view_handle_key(root, "x", "x", 0);
  assert(!strcmp(sz_signal_str_get(text), "x"));
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == slider);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_signal_int_get(value) == 51);
  sz_view_handle_key(root, "Home", "", 0);
  sz_view_handle_key(root, "ArrowLeft", "", 0);
  assert(sz_signal_int_get(value) == 0);
  sz_view_handle_key(root, "End", "", 0);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_signal_int_get(value) == 100);
  sz_signal_int_set(value, INT64_MIN);
  sz_view_handle_key(root, "ArrowLeft", "", 0);
  assert(sz_signal_int_get(value) == 0);
  sz_signal_int_set(value, INT64_MAX);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_signal_int_get(value) == 100);
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == toggle);
  sz_view_handle_key(root, "Space", "", 0);
  assert(sz_signal_int_get(checked) == 1 && sz_view_focused(root) == toggle);
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == last);
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == first);
  sz_view_handle_key(root, "Tab", "", SZ_KEY_SHIFT);
  assert(sz_view_focused(root) == last);
  sz_view_free(root);
  sz_signal_int_free(count);
  sz_signal_int_free(value);
  sz_signal_int_free(checked);
  sz_signal_int_free(shown);
  sz_signal_str_free(text);
}

static void test_keyboard_many_targets(void) {
  SzView *root = sz_view_column();
  SzView *buttons[100];
  for (int i = 0; i < 100; i++) {
    buttons[i] = sz_view_button("item", NULL, NULL);
    sz_view_add_child(root, buttons[i]);
  }
  sz_view_layout(root, 320, 100000, sz_theme_default());
  for (int i = 0; i < 110; i++) {
    sz_view_handle_key(root, "Tab", "", 0);
    assert(sz_view_focused(root) == buttons[i % 100]);
  }
  sz_view_handle_key(root, "Tab", "", SZ_KEY_SHIFT);
  assert(sz_view_focused(root) == buttons[8]);
  sz_view_free(root);
}

static void test_keyboard_radio_group(void) {
  SzSignalInt *selected = sz_signal_int(1);
  SzSignalInt *other = sz_signal_int(1);
  SzView *root = sz_view_column();
  SzView *a = sz_view_radio(selected, 1, "A");
  SzView *b = sz_view_radio(selected, 2, "B");
  SzView *c = sz_view_radio_list_tile(selected, 3, "C");
  sz_view_add_child(root, a);
  sz_view_add_child(root, sz_view_radio(other, 2, "Other"));
  sz_view_add_child(root, b);
  sz_view_add_child(root, c);
  sz_view_layout(root, 320, 600, sz_theme_default());
  sz_view_focus(root, a);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_view_focused(root) == b && sz_signal_int_get(selected) == 2);
  assert(sz_signal_int_get(other) == 1);
  sz_view_handle_key(root, "End", "", 0);
  assert(sz_view_focused(root) == c && sz_signal_int_get(selected) == 3);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_view_focused(root) == a && sz_signal_int_get(selected) == 1);
  sz_view_handle_key(root, "ArrowLeft", "", 0);
  assert(sz_view_focused(root) == c && sz_signal_int_get(selected) == 3);
  sz_view_handle_key(root, "Home", "", 0);
  assert(sz_view_focused(root) == a && sz_signal_int_get(selected) == 1);
  sz_view_free(root);
  sz_signal_int_free(selected);
  sz_signal_int_free(other);
}

static void test_keyboard_tab_list(void) {
  SzSignalInt *selected = sz_signal_int(1);
  SzView *sections = sz_view_column();
  SzView *first_body = sz_view_button("Body first", NULL, NULL);
  SzView *second_body = sz_view_button("Body second", NULL, NULL);
  sz_view_add_child(sections, sz_view_section("first", "First", first_body));
  sz_view_add_child(sections, sz_view_section("second", "Second", second_body));
  SzView *root = sz_view_tabs(selected, sections);
  sz_view_layout(root, 640, 480, sz_theme_default());
  sz_view_handle_key(root, "Tab", "", 0);
  SzView *tab = sz_view_focused(root);
  assert(tab && !strcmp(sz_view_a11y_label(tab), "Second"));
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == second_body);
  sz_view_handle_key(root, "Tab", "", SZ_KEY_SHIFT);
  assert(sz_view_focused(root) == tab);
  sz_view_free(root);
  sz_signal_int_free(selected);
}

static void test_keyboard_modal_text(void) {
  SzSignalInt *open = sz_signal_int(1);
  SzSignalStr *background = sz_signal_str("");
  SzSignalStr *foreground = sz_signal_str("");
  SzView *root = sz_view_stack();
  SzView *back = sz_view_text_field(background, "Background");
  SzView *front = sz_view_text_field(foreground, "Foreground");
  sz_view_add_child(root, back);
  sz_view_add_child(root, sz_view_overlay(open, "Dialog", front));
  sz_view_layout(root, 320, 300, sz_theme_default());
  sz_view_focus(root, back);
  assert(sz_view_focused(root) == front);
  sz_view_handle_key(root, "x", "x", 0);
  assert(!strcmp(sz_signal_str_get(background), ""));
  assert(!strcmp(sz_signal_str_get(foreground), "x"));
  assert(sz_view_focused(root) == front);
  sz_view_free(root);
  sz_signal_int_free(open);
  sz_signal_str_free(background);
  sz_signal_str_free(foreground);
}

static void test_keyboard_overlay_and_editor(void) {
  SzSignalInt *open = sz_signal_int(1);
  SzSignalStr *text = sz_signal_str("");
  SzView *root = sz_view_stack();
  SzView *base = sz_view_button("Base", NULL, NULL);
  SzView *modal = sz_view_column();
  SzView *a = sz_view_button("A", NULL, NULL);
  SzView *b = sz_view_button("B", NULL, NULL);
  sz_view_add_child(modal, a);
  sz_view_add_child(modal, b);
  sz_view_add_child(root, base);
  sz_view_add_child(root, sz_view_overlay(open, "Dialog", modal));
  sz_view_layout(root, 320, 300, sz_theme_default());
  assert(sz_view_focused(root) == a);
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == b);
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == a);
  sz_view_handle_key(root, "Escape", "", 0);
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == base);
  sz_view_free(root);
  root = sz_view_column();
  SzView *editor = sz_view_editor(text);
  SzView *after = sz_view_button("After", NULL, NULL);
  sz_view_add_child(root, editor);
  sz_view_add_child(root, after);
  sz_view_layout(root, 320, 300, sz_theme_default());
  sz_view_focus(root, editor);
  sz_view_handle_key(root, "Tab", "", 0);
  assert(!strcmp(sz_signal_str_get(text), "  "));
  sz_view_handle_key(root, "Tab", "", SZ_KEY_CTRL);
  assert(sz_view_focused(root) == after);
  sz_view_free(root);
  sz_signal_int_free(open);
  sz_signal_str_free(text);
}

static void test_modal_pointer_surface(void) {
  SzSignalInt *open = sz_signal_int(1), *background_hits = sz_signal_int(0);
  SzSignalInt *actions = sz_signal_int(0), *secondary = sz_signal_int(0);
  SzView *root = sz_view_stack();
  SzView *background = sz_view_scroll(sz_view_tooltip("Background tip",
    sz_view_on_secondary(sz_view_sized(640, 1600, sz_view_text("Background")), keyboard_count, background_hits)));
  sz_view_add_child(root, background);
  SzView *body = sz_view_column();
  SzView *button = sz_view_button("Action", keyboard_count, actions);
  sz_view_add_child(body, sz_view_text("Read the details."));
  sz_view_add_child(body, button);
  SzView *card = sz_view_card(body);
  SzView *tip = sz_view_tooltip("Dialog tip", sz_view_on_secondary(card, keyboard_count, secondary));
  SzView *modal = sz_view_overlay(open, "Details", sz_view_center(sz_view_sized(240, 160, tip)));
  sz_view_add_child(root, modal);
  sz_view_layout(root, 640, 480, sz_theme_default());
  assert(sz_view_input_scope(root) == modal);
  SzRect frame = sz_view_frame(card);
  assert(sz_view_hit_test(root, frame.x + 20, frame.y + 20) == card);
  assert(sz_view_handle_tap(root, frame.x + 20, frame.y + 20));
  assert(sz_signal_int_get(open) == 1 && sz_view_focused(root) == button);
  assert(sz_view_handle_tap(root, frame.x + 4, frame.y + frame.h - 4));
  assert(sz_signal_int_get(open) == 1);
  SzRect action = sz_view_frame(button);
  assert(sz_view_handle_tap(root, action.x + 8, action.y + 8));
  assert(sz_signal_int_get(actions) == 1);
  assert(sz_view_tooltip_at(root, 40, 40) == NULL);
  assert(!sz_view_set_hover_at(root, 40, 40));
  assert(!sz_view_handle_secondary(root, 40, 40));
  assert(sz_signal_int_get(background_hits) == 0);
  assert(sz_view_scroll_at(root, 40, 40) == NULL);
  assert(sz_view_scroll_wheel_target(root, 40, 40, 0, 40) == NULL);
  assert(sz_view_tooltip_at(root, frame.x + 20, frame.y + 20) == tip);
  assert(sz_view_handle_secondary(root, frame.x + 20, frame.y + 20));
  assert(sz_signal_int_get(secondary) == 1);
  assert(sz_view_handle_tap(root, 40, 40));
  assert(sz_signal_int_get(open) == 0);
  sz_view_layout(root, 640, 480, sz_theme_default());
  assert(sz_view_input_scope(root) == root);
  assert(sz_view_scroll_at(root, 40, 40) == background);
  assert(sz_view_scroll_wheel_target(root, 40, 40, 0, 40) == background);
  assert(sz_view_tooltip_at(root, 40, 40));
  assert(sz_view_handle_secondary(root, 40, 40));
  assert(sz_signal_int_get(background_hits) == 1);
  sz_view_free(root);
  sz_signal_int_free(open); sz_signal_int_free(background_hits);
  sz_signal_int_free(actions); sz_signal_int_free(secondary);
}

static void test_modal_scroll_boundary(void) {
  SzSignalInt *open = sz_signal_int(1), *hits = sz_signal_int(0);
  SzView *inner = sz_view_scroll(sz_view_sized(200, 1000, sz_view_text("Long details")));
  SzView *modal = sz_view_overlay(open, "Details", sz_view_center(sz_view_sized(240, 160, sz_view_card(inner))));
  SzView *content = sz_view_column();
  sz_view_add_child(content, sz_view_sized(640, 480, modal));
  sz_view_add_child(content, sz_view_sized(640, 1000, sz_view_text("Background")));
  SzView *background = sz_view_scroll(content);
  SzView *root = sz_view_sized(640, 480, background);
  sz_view_layout(root, 640, 480, sz_theme_default());
  SzRect frame = sz_view_frame(inner);
  float x = frame.x + 10, y = frame.y + 10;
  assert(sz_view_scroll_at(root, x, y) == inner);
  assert(sz_view_scroll_wheel_target(root, x, y, 0, 40) == inner);
  float background_y = sz_view_scroll_y(background);
  assert(sz_view_scroll_pan(inner, 0, 10000));
  sz_view_layout(root, 640, 480, sz_theme_default());
  assert(sz_view_scroll_wheel_target(root, x, y, 0, 40) == NULL);
  assert(sz_view_scroll_y(background) == background_y);
  sz_signal_int_set(open, 0);
  sz_view_layout(root, 640, 480, sz_theme_default());
  frame = sz_view_frame(background);
  assert(sz_view_scroll_at(root, frame.x + frame.w / 2, frame.y + frame.h / 2) == background);
  sz_view_free(root);
  root = sz_view_overlay(open, "Action", sz_view_center(sz_view_sized(240, 160,
    sz_view_ink_well("Card action", keyboard_count, hits, sz_view_card(sz_view_text("Tap this card"))))));
  sz_signal_int_set(open, 1);
  sz_view_layout(root, 640, 480, sz_theme_default());
  assert(sz_view_handle_tap(root, 320, 240));
  assert(sz_signal_int_get(hits) == 1 && sz_signal_int_get(open) == 1);
  sz_view_free(root);
  sz_signal_int_free(open); sz_signal_int_free(hits);
}

static void test_modal_pointer_capture(void) {
  for (int scale = 1; scale <= 2; scale++) {
    SzSignalInt *open = sz_signal_int(0);
    SzView *root = sz_view_stack();
    SzView *background = sz_view_scroll(sz_view_sized(640, 1600, sz_view_text("Background")));
    sz_view_add_child(root, sz_view_sized(640, 480, background));
    sz_view_add_child(root, sz_view_overlay(open, "Details", sz_view_center(sz_view_sized(240, 160, sz_view_card(sz_view_text("Details"))))));
    SzUiConfig cfg = {.kind = SZ_UI_RUNTIME_HEADLESS, .width = 640, .height = 480, .scale = scale};
    SzUiSession *session = sz_ui_mount(&cfg, root);
    assert(session && sz_ui_pump_sync(session));
    SzRect viewport = sz_view_frame(background);
    SzInputEvent input = {.kind = SZ_INPUT_POINTER, .pointer_button = 1,
      .pointer_phase = SZ_POINTER_DOWN, .x = viewport.x + 10, .y = viewport.y + 200};
    assert(sz_ui_inject_sync(session, &input));
    sz_signal_int_set(open, 1);
    assert(sz_ui_pump_sync(session));
    input.pointer_phase = SZ_POINTER_MOVE; input.y = viewport.y + 100;
    assert(sz_ui_inject_sync(session, &input));
    input.pointer_phase = SZ_POINTER_UP;
    assert(sz_ui_inject_sync(session, &input));
    assert(sz_signal_int_get(open) == 1 && sz_view_scroll_y(background) == 0);
    input.kind = SZ_INPUT_SCROLL; input.dy = 40;
    assert(!sz_ui_inject_sync(session, &input));
    assert(sz_view_scroll_y(background) == 0);
    sz_signal_int_set(open, 0);
    assert(sz_ui_pump_sync(session));
    input.kind = SZ_INPUT_POINTER; input.pointer_phase = SZ_POINTER_DOWN; input.y = viewport.y + 200;
    assert(sz_ui_inject_sync(session, &input));
    input.pointer_phase = SZ_POINTER_MOVE; input.y = viewport.y + 100;
    assert(sz_ui_inject_sync(session, &input));
    input.pointer_phase = SZ_POINTER_UP;
    assert(sz_ui_inject_sync(session, &input));
    assert(sz_view_scroll_y(background) > 0);
    sz_ui_unmount(session); sz_view_free(root); sz_signal_int_free(open);
  }
}

static void test_modal_keyboard_sync(void) {
  for (int kind = SZ_UI_RUNTIME_HEADLESS; kind <= SZ_UI_RUNTIME_MOBILE; kind++) {
    SzSignalInt *open = sz_signal_int(0);
    SzSignalStr *text = sz_signal_str("");
    SzView *root = sz_view_stack();
    SzView *opener = sz_view_button("Open", NULL, NULL);
    sz_view_add_child(root, opener);
    SzView *field = sz_view_text_field(text, "Name");
    sz_view_add_child(root, sz_view_overlay(open, "Edit", field));
    SzUiConfig cfg = {.kind = kind, .width = 320, .height = 240, .scale = 1};
    SzUiSession *session = sz_ui_mount(&cfg, root);
    assert(session && sz_ui_pump_sync(session));
    sz_view_focus(root, opener);
    sz_signal_int_set(open, 1);
    assert(sz_ui_pump_sync(session));
    assert(sz_view_focused(root) == field);
    assert(sz_ui_session_keyboard_visible(session));
    SzInputEvent input = {.kind = SZ_INPUT_KEYBOARD, .keyboard_visible = 0};
    assert(sz_ui_inject_sync(session, &input));
    assert(sz_ui_pump_sync(session));
    assert(!sz_ui_session_keyboard_visible(session));
    sz_signal_int_set(open, 0);
    assert(sz_ui_pump_sync(session));
    assert(sz_view_focused(root) == opener);
    assert(!sz_ui_session_keyboard_visible(session));
    sz_signal_int_set(open, 1);
    assert(sz_ui_pump_sync(session));
    assert(sz_ui_session_keyboard_visible(session));
    sz_ui_unmount(session);
    sz_view_free(root);
    sz_signal_int_free(open); sz_signal_str_free(text);
  }
}

static void test_modal_lifecycle(void) {
  uint64_t before_bytes, before_count, bytes, count;
  sz_alloc_stats(&before_bytes, &before_count);
  SzSignalInt *open = sz_signal_int(0), *nested = sz_signal_int(0);
  SzView *root = sz_view_stack();
  SzView *base = sz_view_column();
  SzView *opener = sz_view_button("Open", NULL, NULL);
  SzView *fallback = sz_view_button("Fallback", NULL, NULL);
  sz_view_add_child(base, opener);
  sz_view_add_child(root, base);
  sz_view_add_child(root, fallback);
  SzView *body = sz_view_stack();
  SzView *content = sz_view_column();
  SzView *first = sz_view_button("First", NULL, NULL);
  SzView *next = sz_view_button("Next", NULL, NULL);
  sz_view_add_child(content, first);
  sz_view_add_child(content, next);
  sz_view_add_child(body, content);
  SzView *inner = sz_view_overlay(nested, "Confirm", sz_view_text("Read this message"));
  sz_view_add_child(body, inner);
  SzView *outer = sz_view_overlay(open, "Edit details", body);
  sz_view_add_child(root, outer);
  sz_view_layout(root, 500, 400, sz_theme_default());
  sz_view_focus(root, opener);
  sz_signal_int_set(open, 1);
  sz_view_layout(root, 500, 400, sz_theme_default());
  assert(sz_view_focused(root) == first);
  assert(!strcmp(sz_view_a11y_label(outer), "Edit details"));
  sz_view_focus(root, fallback);
  assert(sz_view_focused(root) == first);
  assert(!sz_view_activate(root, fallback, 0, 0));
  SzView *targets[8];
  assert(sz_view_collect_tap_targets(root, targets, 8) == 2);
  assert(targets[0] == first && targets[1] == next);
  sz_view_handle_key(root, "Tab", "", SZ_KEY_SHIFT);
  assert(sz_view_focused(root) == next);
  sz_signal_int_set(nested, 1);
  sz_view_layout(root, 500, 400, sz_theme_default());
  assert(sz_view_focused(root) == inner);
  assert(sz_view_handle_key(root, "Tab", "", 0));
  assert(sz_view_focused(root) == inner);
  sz_view_handle_key(root, "Escape", "", 0);
  sz_view_layout(root, 500, 400, sz_theme_default());
  assert(sz_signal_int_get(open) == 1 && sz_signal_int_get(nested) == 0);
  assert(sz_view_focused(root) == next);
  sz_signal_int_set(open, 0);
  sz_view_layout(root, 500, 400, sz_theme_default());
  assert(sz_view_focused(root) == opener);
  sz_signal_int_set(open, 1);
  sz_view_layout(root, 500, 400, sz_theme_default());
  sz_view_clear_children(base);
  sz_signal_int_set(open, 0);
  sz_view_layout(root, 500, 400, sz_theme_default());
  assert(sz_view_focused(root) == fallback);
  sz_signal_int_set(open, 1);
  sz_view_layout(root, 500, 400, sz_theme_default());
  sz_view_focus(root, next);
  sz_signal_int_set(nested, 1);
  sz_view_layout(root, 500, 400, sz_theme_default());
  sz_signal_int_set(open, 0);
  sz_view_layout(root, 500, 400, sz_theme_default());
  assert(sz_view_focused(root) == fallback);
  sz_signal_int_set(nested, 0);
  sz_view_clear_children(root);
  sz_view_layout(root, 500, 400, sz_theme_default());
  assert(sz_view_focused(root) == NULL);
  sz_view_free(root);
  sz_signal_int_free(open); sz_signal_int_free(nested);
  sz_alloc_stats(&bytes, &count);
  assert(bytes == before_bytes && count == before_count);
}

static void test_color_theme_scope(void) {
  uint64_t before_bytes, before_count, bytes, count;
  sz_alloc_stats(&before_bytes, &before_count);
  for (int i = 0; i < 100; i++) {
    SzTheme *palette = sz_theme_dark();
    SzView *view = sz_view_theme(palette, sz_view_text("theme"));
    sz_release(palette);
    sz_view_free(view);
  }
  sz_alloc_stats(&bytes, &count);
  assert(bytes == before_bytes && count == before_count);
  SzTheme *dark = sz_theme_dark();
  SzTheme *light = sz_theme_light();
  SzView *body = sz_view_stack();
  SzView *inner = sz_view_theme(light, sz_view_sized(80, 60, sz_view_text("")));
  SzView *button = sz_view_button("Action", NULL, NULL);
  sz_view_add_child(body, sz_view_positioned(12, 12, inner));
  sz_view_add_child(body, sz_view_positioned(120, 20, sz_view_sized(100, 40, button)));
  SzView *root = sz_view_theme(dark, sz_view_sized(240, 160, body));
  sz_release(light);
  sz_release(dark);
  SkSurface *surface = sk_surface_make_raster_n32_premul(240, 160);
  assert(surface);
  assert(sz_view_paint(root, sk_surface_get_canvas(surface), 240, 160, sz_theme_default()));
  const uint8_t *pixels = sk_surface_peek_pixels(surface, NULL);
  assert(px_rgb(pixels, 240, 2, 2, 0x24, 0x23, 0x1F));
  SzRect nested_frame = sz_view_frame(inner);
  assert(px_rgb(pixels, 240, (int)nested_frame.x + 5, (int)nested_frame.y + 5, 0xF3, 0xEF, 0xE3));
  assert(px_rgb(pixels, 240, 100, 100, 0x24, 0x23, 0x1F));
  SzRect frame = sz_view_frame(button);
  assert(px_rgb(pixels, 240, (int)frame.x + 5, (int)frame.y + 5, 0xE8, 0xEF, 0x48));
  assert(frame.w == 96.f && frame.h == 40.f);
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == button);
  SzTheme scaled = *sz_theme_default();
  scaled.px_scale = 2.f;
  sz_view_layout(root, 480, 320, &scaled);
  frame = sz_view_frame(button);
  assert(frame.w == 192.f && frame.h == 80.f);
  sk_surface_unref(surface);
  sz_view_free(root);
}

static void test_composite_tab_capacity(void) {
  SzSignalInt *selected = sz_signal_int(99);
  SzView *sections = sz_view_column();
  for (int i = 0; i < 100; i++) {
    char id[24], title[24];
    snprintf(id, sizeof id, "panel%d", i);
    snprintf(title, sizeof title, "Tab %d", i);
    sz_view_add_child(sections, sz_view_section(id, title, sz_view_button("Body", NULL, NULL)));
  }
  SzView *root = sz_view_tabs(selected, sections);
  sz_view_layout(root, 640, 480, sz_theme_default());
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) && !strcmp(sz_view_a11y_label(sz_view_focused(root)), "Tab 99"));
  SzView *last = sz_view_focused(root);
  sz_view_layout(root, 640, 480, sz_theme_default());
  assert(sz_view_focused(root) == last);
  sz_view_handle_key(root, "Home", "", 0);
  assert(!strcmp(sz_view_a11y_label(sz_view_focused(root)), "Tab 0"));
  assert(sz_signal_int_get(selected) == 99);
  sz_view_layout(root, 640, 480, sz_theme_default());
  assert(!strcmp(sz_view_a11y_label(sz_view_focused(root)), "Tab 0"));
  sz_view_handle_key(root, "ArrowDown", "", 0);
  assert(!strcmp(sz_view_a11y_label(sz_view_focused(root)), "Tab 0"));
  sz_view_handle_key(root, "Tab", "", 0);
  assert(!strcmp(sz_view_a11y_label(sz_view_focused(root)), "Body"));
  sz_view_layout(root, 640, 480, sz_theme_default());
  assert(!strcmp(sz_view_a11y_label(sz_view_focused(root)), "Body"));
  sz_view_handle_key(root, "Tab", "", SZ_KEY_SHIFT);
  assert(!strcmp(sz_view_a11y_label(sz_view_focused(root)), "Tab 0"));
  sz_view_free(root);
  sz_signal_int_free(selected);
}

static void test_composite_radio_tab_stop(void) {
  SzSignalInt *selected = sz_signal_int(2);
  SzView *root = sz_view_column();
  SzView *before = sz_view_button("Before", NULL, NULL);
  SzView *a = sz_view_radio(selected, 1, "A");
  SzView *b = sz_view_radio(selected, 2, "B");
  SzView *after = sz_view_button("After", NULL, NULL);
  sz_view_add_child(root, before);
  sz_view_add_child(root, a);
  sz_view_add_child(root, b);
  sz_view_add_child(root, after);
  sz_view_layout(root, 320, 500, sz_theme_default());
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == before);
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == b);
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == after);
  sz_view_handle_key(root, "Tab", "", SZ_KEY_SHIFT);
  assert(sz_view_focused(root) == b);
  sz_view_free(root);
  sz_signal_int_free(selected);
}

static void test_composite_named_radios(void) {
  SzSignalInt *selected = sz_signal_int(2);
  SzView *root = sz_view_column();
  SzView *a = sz_view_radio(selected, 1, "Same");
  SzView *b = sz_view_radio(selected, 2, "Same");
  SzView *c = sz_view_radio(selected, 1, "Same");
  SzView *d = sz_view_radio(selected, 2, "Same");
  SzView *first = sz_view_column();
  SzView *second = sz_view_column();
  sz_view_add_child(first, a);
  sz_view_add_child(first, b);
  sz_view_add_child(second, c);
  sz_view_add_child(second, d);
  sz_view_add_child(root, sz_view_radio_group("First", first));
  sz_view_add_child(root, sz_view_radio_group("Second", second));
  sz_view_layout(root, 400, 500, sz_theme_default());
  assert(sz_view_tab_stop(b) && sz_view_tab_stop(d));
  assert(!sz_view_tab_stop(a) && !sz_view_tab_stop(c));
  assert(sz_view_focus_next(root, 0, 0));
  assert(sz_view_focused(root) == b);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_view_focused(root) == a && sz_signal_int_get(selected) == 1);
  assert(sz_view_focus_next(root, 0, 0));
  assert(sz_view_focused(root) == c);
  assert(!sz_view_focus_next(root, 0, 0));
  assert(sz_view_focused(root) == c);
  assert(sz_view_focus_next(root, 1, 0));
  assert(sz_view_focused(root) == a);
  sz_signal_int_set(selected, 0);
  sz_view_free(root);
  sz_signal_int_free(selected);
}

static void test_composite_hidden_radios(void) {
  SzSignalInt *selected = sz_signal_int(2);
  SzSignalInt *shown = sz_signal_int(0);
  SzView *root = sz_view_column();
  SzView *a = sz_view_radio(selected, 1, "A");
  SzView *b = sz_view_radio(selected, 2, "B");
  SzView *c = sz_view_radio(selected, 3, "C");
  sz_view_add_child(root, a);
  sz_view_add_child(root, sz_view_visibility(shown, b));
  sz_view_add_child(root, c);
  sz_view_layout(root, 400, 500, sz_theme_default());
  assert(sz_view_tab_stop(a) && !sz_view_tab_stop(b) && !sz_view_tab_stop(c));
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_focused(root) == a);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_view_focused(root) == c && sz_signal_int_get(selected) == 3);
  sz_view_focus(root, NULL);
  sz_signal_int_set(selected, 0);
  assert(sz_view_tab_stop(a) && !sz_view_tab_stop(c));
  sz_view_handle_key(root, "Tab", "", 0);
  sz_view_handle_key(root, "Space", "", 0);
  assert(sz_signal_int_get(selected) == 1);
  sz_view_free(root);
  sz_signal_int_free(selected);
  sz_signal_int_free(shown);
}

static void test_composite_modal_radios(void) {
  for (int named = 0; named < 2; named++) {
    SzSignalInt *selected = sz_signal_int(2), *open = sz_signal_int(1);
    SzView *root = sz_view_stack();
    SzView *background = sz_view_radio(selected, 2, "Background");
    sz_view_add_child(root, background);
    SzView *items = sz_view_column();
    SzView *a = sz_view_radio(selected, 1, "A");
    SzView *b = sz_view_radio(selected, 2, "B");
    sz_view_add_child(items, a);
    sz_view_add_child(items, b);
    sz_view_add_child(root, sz_view_overlay(open, "Dialog", items));
    if (named) root = sz_view_radio_group("Shared", root);
    sz_view_layout(root, 400, 500, sz_theme_default());
    sz_view_handle_key(root, "Tab", "", 0);
    assert(sz_view_focused(root) == b);
    sz_view_handle_key(root, "ArrowRight", "", 0);
    assert(sz_view_focused(root) == a && sz_signal_int_get(selected) == 1);
    sz_view_handle_key(root, "Tab", "", 0);
    assert(sz_view_focused(root) == a);
    assert(!sz_view_tab_stop(background));
    sz_view_free(root);
    sz_signal_int_free(selected);
    sz_signal_int_free(open);
  }
}

static void test_composite_duplicate_focus(void) {
  SzView *items = sz_view_column();
  SzView *last = NULL;
  for (int i = 0; i < 100; i++) {
    last = sz_view_button("Same", NULL, NULL);
    sz_view_add_child(items, last);
  }
  SzView *root = sz_view_focus_group(sz_view_scroll(items));
  sz_view_layout(root, 400, 300, sz_theme_default());
  sz_view_focus(root, last);
  sz_view_layout(root, 400, 300, sz_theme_default());
  assert(sz_view_focused(root) == last);
  sz_view_handle_key(root, "ArrowUp", "", 0);
  SzView *previous = sz_view_focused(root);
  assert(previous && previous != last);
  sz_view_layout(root, 400, 300, sz_theme_default());
  assert(sz_view_focused(root) == previous);
  sz_view_handle_key(root, "ArrowDown", "", 0);
  assert(sz_view_focused(root) == last);
  sz_view_free(root);
}

static void test_composite_panel_stop(void) {
  SzSignalInt *selected = sz_signal_int(0);
  SzView *sections = sz_view_column();
  sz_view_add_child(sections, sz_view_section("text", "Text", sz_view_text("Content")));
  sz_view_add_child(sections, sz_view_section("button", "Button", sz_view_button("Action", NULL, NULL)));
  SzView *root = sz_view_tabs(selected, sections);
  sz_view_layout(root, 640, 480, sz_theme_default());
  sz_view_handle_key(root, "Tab", "", 0);
  assert(!strcmp(sz_view_a11y_label(sz_view_focused(root)), "Text"));
  sz_view_handle_key(root, "Tab", "", 0);
  assert(sz_view_a11y_role(sz_view_focused(root)) == SZ_A11Y_TAB_PANEL);
  sz_view_handle_key(root, "Tab", "", SZ_KEY_SHIFT);
  sz_view_handle_key(root, "ArrowRight", "", 0);
  assert(sz_signal_int_get(selected) == 0);
  sz_view_handle_key(root, "Enter", "", 0);
  sz_view_layout(root, 640, 480, sz_theme_default());
  assert(sz_signal_int_get(selected) == 1);
  sz_view_handle_key(root, "Tab", "", 0);
  assert(!strcmp(sz_view_a11y_label(sz_view_focused(root)), "Action"));
  sz_view_free(root);
  sz_signal_int_free(selected);
}

static void theme_metrics_bad_font(void) {
  (void)sz_theme_metrics(NULL, 0, 6, 12, 8, 40);
}

static void theme_metrics_bad_height(void) {
  (void)sz_theme_metrics(NULL, 22, 10, 12, 8, 20);
}

static void test_theme_metrics_scope(void) {
  assert_missing_aborts(theme_metrics_bad_font, "Theme metrics need");
  assert_missing_aborts(theme_metrics_bad_height, "Theme metrics need");
  SzTheme *small = sz_theme_metrics(NULL, 1, 0, 0, 0, 1);
  SzSignalInt *selected = sz_signal_int(0);
  SzView *control = sz_view_checkbox(selected, "Small");
  SzView *small_root = sz_view_theme(small, control);
  sz_view_layout(small_root, 100, 100, sz_theme_default());
  assert(sz_view_frame(control).w >= 0.f && sz_view_frame(control).h == 1.f);
  sz_view_free(small_root);
  sz_signal_int_free(selected);
  sz_release(small);

  SzTheme *base = sz_theme_dark();
  SzTheme *large = sz_theme_metrics(base, 22, 9, 4, 3, 40);
  assert(base->font_px == 14.f && base->pad == 12.f && base->line_gap == 6.f);
  assert(large->background == base->background && large->font_px == 22.f);
  SzSignalStr *value = sz_signal_str("WWii");
  SzView *field = sz_view_text_field(value, "Name");
  SzView *paragraph = sz_view_text("One\nTwo");
  SzView *plain = sz_view_text("One\nTwo");
  SzView *body = sz_view_column();
  sz_view_add_child(body, paragraph);
  sz_view_add_child(body, field);
  sz_view_add_child(body, sz_view_theme(base, plain));
  SzView *root = sz_view_theme(large, body);
  sz_release(large);
  sz_release(base);
  sz_view_layout(root, 640, 480, sz_theme_default());
  assert(sz_view_frame(paragraph).h == 62.f);
  assert(sz_view_frame(field).h == 40.f);
  assert(sz_view_frame(plain).h == 40.f);
  sz_view_focus(root, field);
  sz_view_set_text_field_caret(field, 2);
  SzRect caret = sz_view_caret_rect(root, sz_theme_default());
  assert(caret.h == 22.f);
  SzRect frame = sz_view_frame(field);
  sz_view_activate(root, field, frame.x + 6.f + sk_font_measure_string("WW", 22.f), frame.y + 20.f);
  assert(sz_view_text_field_caret(field) == 2);
  SzTheme scaled = *sz_theme_default();
  scaled.font_px *= 2.f;
  scaled.pad *= 2.f;
  scaled.gap *= 2.f;
  scaled.control_h *= 2.f;
  scaled.px_scale = 2.f;
  sz_view_layout(root, 1280, 960, &scaled);
  assert(sz_view_frame(paragraph).h == 124.f);
  assert(sz_view_frame(plain).h == 80.f);
  frame = sz_view_frame(field);
  sz_view_activate(root, field, frame.x + 12.f + sk_font_measure_string("WW", 44.f), frame.y + 40.f);
  assert(sz_view_text_field_caret(field) == 2);
  caret = sz_view_caret_rect(root, &scaled);
  assert(caret.h == 44.f);

  sz_view_free(root);
  sz_signal_str_free(value);
}

static void test_theme_metrics_editor(void) {
  SzTheme *large = sz_theme_metrics(NULL, 22, 9, 4, 3, 40);
  SzSignalStr *value = sz_signal_str("abcd\nabcd\nabcd\nabcd\nabcd\nabcd");
  SzView *editor = sz_view_editor(value);
  SzView *root = sz_view_theme(large, sz_view_sized(240, 62, editor));
  sz_release(large);
  for (int scale = 1; scale <= 2; scale++) {
    SzTheme theme = *sz_theme_default();
    theme.font_px *= scale;
    theme.pad *= scale;
    theme.gap *= scale;
    theme.control_h *= scale;
    theme.px_scale = scale;
    sz_view_layout(root, 640 * scale, 480 * scale, &theme);
    sz_view_set_editor_caret(editor, 0);
    sz_view_focus(root, editor);
    SzRect frame = sz_view_frame(editor);
    float gutter = sz_view_editor_gutter_w(editor);
    sz_view_activate(root, editor, frame.x + gutter + 6.f * scale,
                     frame.y + (6.f + 31.f) * scale);
    assert(sz_view_editor_caret(editor) == 5);
    sz_view_handle_key(root, "PageDown", "", 0);
    assert(sz_view_editor_caret(editor) == 15);
    assert(sz_view_editor_scroll_y(editor) >= 31.f);
    sz_view_scroll_pan(editor, 0.f, 10000.f);
    assert(fabsf(sz_view_editor_scroll_y(editor) - 130.f) < .01f);
    sz_view_scroll_pan(editor, 0.f, -10000.f);
  }
  sz_view_free(root);
  sz_signal_str_free(value);
}

typedef struct TextProbe {
  int count, dots;
  int starts[32], ends[32], styles[32];
  float x[32], baseline[32], fonts[32];
} TextProbe;

static void text_probe(SkTextRange context, int start, int end,
  float x, float baseline, float width, float font, float line_height,
  int style, uint32_t color, int colored, int ellipsis, int action,
  float context_x, void *env) {
  const char *source = context.source;
  (void)context; (void)context_x;
  (void)action;
  (void)source; (void)width; (void)line_height; (void)color; (void)colored;
  TextProbe *probe = env;
  if (probe->count < 32) {
    int i = probe->count;
    probe->starts[i] = start; probe->ends[i] = end; probe->styles[i] = style;
    probe->x[i] = x; probe->baseline[i] = baseline; probe->fonts[i] = font;
  }
  probe->count++;
  probe->dots += ellipsis;
}

static SzText *test_text_span(const char *source) {
  SzString *value = sz_string_from_cstr(source);
  SzText *text = sz_text_span(value);
  sz_release(value);
  return text;
}

static SzText *test_text_pair(SzText *a, SzText *b) {
  SzList *tail = sz_list_cons(b, NULL);
  SzList *values = sz_list_cons(a, tail);
  sz_release(tail);
  SzText *text = sz_text_concat(values);
  sz_release(values);
  return text;
}

static void test_shaped_field_geometry(void) {
  if (sk_font_measure_string("i", 14) == sk_font_measure_string("W", 14)) return;
  SzSignalStr *value = sz_signal_str("אב");
  SzView *field = sz_view_text_field(value, "Name");
  sz_view_layout(field, 320, 100, sz_theme_default());
  sz_view_focus(field, field);
  sz_view_set_text_field_caret(field, 0);
  SzRect start = sz_view_caret_rect(field, sz_theme_default());
  sz_view_set_text_field_caret(field, 4);
  SzRect end = sz_view_caret_rect(field, sz_theme_default());
  assert(start.x > end.x + 5.f);
  sz_view_activate(field, field, start.x, start.y + 2);
  assert(sz_view_text_field_caret(field) == 0);
  sz_view_activate(field, field, end.x, end.y + 2);
  assert(sz_view_text_field_caret(field) == 4);
  sz_view_set_text_field_caret(field, 0);
  sz_view_handle_key(field, "ArrowLeft", "", 0);
  assert(sz_view_text_field_caret(field) == 2);
  sz_view_handle_key(field, "ArrowLeft", "", 0);
  assert(sz_view_text_field_caret(field) == 4);
  sz_view_handle_key(field, "ArrowRight", "", 0);
  assert(sz_view_text_field_caret(field) == 2);
  sz_view_handle_text_edit(field, "X", 0);
  assert(strcmp(sz_signal_str_get(value), "אXב") == 0);
  sz_signal_str_set(value, "ab");
  sz_view_set_text_field_sel(field, 0, 2);
  sz_view_handle_compose(field, "لا");
  SzRect preview = sz_view_caret_rect(field, sz_theme_default());
  assert(fabsf(preview.x - (sz_view_frame(field).x + 6.f)) < .01f);
  assert(strcmp(sz_signal_str_get(value), "ab") == 0);
  sz_view_handle_key(field, "Escape", "", 0);
  assert(strcmp(sz_signal_str_get(value), "ab") == 0);
  sz_view_set_text_field_sel(field, 0, 2);
  sz_view_handle_compose(field, "لا");
  sz_view_handle_compose(field, "");
  assert(strcmp(sz_signal_str_get(value), "لا") == 0);
  sz_signal_str_set(value, "ab אב 12");
  sz_view_set_text_field_sel(field, 3, 7);
  SkSurface *surface = sk_surface_make_raster_n32_premul(320, 100);
  assert(surface);
  assert(sz_view_paint(field, sk_surface_get_canvas(surface), 320, 100, sz_theme_default()));
  const uint8_t *pixels = sk_surface_peek_pixels(surface, NULL);
  const SzTheme *theme = sz_theme_default();
  float prefix = sk_font_measure_string("ab ", 14);
  int selected_x = (int)(6.f + prefix + sk_font_measure_string("12 ", 14) + sk_font_measure_string("אב", 14) * .5f);
  int number_x = (int)(6.f + prefix + sk_font_measure_string("12", 14) * .5f);
  assert(px_rgb(pixels, 320, selected_x, 14, (theme->selection >> 16) & 255, (theme->selection >> 8) & 255, theme->selection & 255));
  assert(px_rgb(pixels, 320, number_x, 14, (theme->surface >> 16) & 255, (theme->surface >> 8) & 255, theme->surface & 255));
  sk_surface_unref(surface);
  sz_view_free(field);
  sz_signal_str_free(value);
}

static void test_shaped_text_wrapping(void) {
  if (sk_font_measure_string("i", 14) == sk_font_measure_string("W", 14)) return;
  const char *source = "ééé";
  SzText *text = test_text_span(source);
  float width = sk_font_measure_span(sk_text_range(source, 3), 14, 0);
  TextProbe probe = {0};
  SzTextMetrics metrics = sz_text_layout(text, 14, 1, 6, width + .01f, 0, 0, text_probe, &probe);
  assert(metrics.lines == 3 && probe.count == 3);
  for (int i = 0; i < 3; i++) assert(probe.starts[i] == 3 * i && probe.ends[i] == 3 * (i + 1));
  sz_release(text);
  text = test_text_span("éééééééééé");
  probe = (TextProbe){0};
  metrics = sz_text_layout(text, 14, 1, 6, width + sk_font_measure_span(sk_text_range("...", 3), 14, 0) + .01f,
    1, 1, text_probe, &probe);
  assert(metrics.truncated && probe.dots == 1);
  for (int i = 0; i < probe.count; i++) assert(probe.starts[i] % 3 == 0 && probe.ends[i] % 3 == 0);
  sz_release(text);

  SzView *plain = sz_view_text(source);
  SzView *root = sz_view_max_size((int)ceilf(width + 4.f), 0, plain);
  sz_view_layout(root, 320, 240, sz_theme_default());
  assert(sz_view_frame(plain).h == 60.f);
  sz_view_free(root);

  char long_line[401];
  memset(long_line, 'M', 400); long_line[400] = 0;
  plain = sz_view_text(long_line);
  sz_view_layout(plain, 6400, 80, sz_theme_default());
  float full_width = sk_font_measure_span(sk_text_range(long_line, 400), 14, 0);
  assert(sz_view_frame(plain).w >= full_width);
  SkSurface *surface = sk_surface_make_raster_n32_premul(6400, 80);
  assert(surface);
  assert(sz_view_paint(plain, sk_surface_get_canvas(surface), 6400, 80, sz_theme_default()));
  const uint8_t *pixels = sk_surface_peek_pixels(surface, NULL);
  int ink = 0;
  for (int y = 0; y < 40; y++) for (int x = (int)(full_width * .9f); x < (int)full_width; x++) {
    const uint8_t *pixel = pixels + ((size_t)y * 6400 + x) * 4;
    if (pixel[0] < 100 && pixel[1] < 100 && pixel[2] < 100) ink++;
  }
  assert(ink > 0);
  sk_surface_unref(surface);
  sz_view_free(plain);
}

static void test_opacity_transition_frames(void) {
  uint64_t before_bytes, before_count, end_bytes, end_count;
  sz_alloc_stats(&before_bytes, &before_count);
  SzSignalInt *value = sz_signal_int(100), *reduced = sz_signal_int(0), *visible = sz_signal_int(1);
  SzView *content = sz_view_stack();
  sz_view_add_child(content, sz_view_background(0xffff0000, sz_view_sized(20, 12, sz_view_text(""))));
  sz_view_add_child(content, sz_view_background(0xff0000ff, sz_view_sized(20, 12, sz_view_text(""))));
  SzView *animation = sz_view_animated_opacity(value, 4, content);
  SzView *root = sz_view_visibility(visible, sz_view_reduced_motion(reduced, animation));
  SkSurface *surface = sk_surface_make_raster_n32_premul(20, 12);
  SkCanvas *canvas = sk_surface_get_canvas(surface);
  SzTheme theme = *sz_theme_default(); theme.background = 0xffffffff; theme.pad = 0; theme.gap = 0;
  assert(sz_view_paint(root, canvas, 20, 12, &theme));
  assert(sz_view_animated_opacity_value(animation) == 100 && !sz_view_motion_pending(root));
  assert(px_rgb(sk_surface_peek_pixels(surface, NULL), 20, 5, 5, 0, 0, 255));
  sz_signal_int_set(value, 0);
  for (int i = 0; i < 3; i++) sz_view_layout(root, 20, 12, &theme);
  assert(sz_view_animated_opacity_value(animation) == 100 && sz_view_motion_pending(root));
  assert(sz_view_paint(root, canvas, 20, 12, &theme));
  assert(sz_view_animated_opacity_value(animation) == 75);
  assert(px_rgb(sk_surface_peek_pixels(surface, NULL), 20, 5, 5, 64, 64, 255));
  assert(sz_signal_int_get(value) == 0);
  assert(sz_view_paint(root, canvas, 20, 12, &theme));
  assert(sz_view_animated_opacity_value(animation) == 50);
  sz_signal_int_set(value, 100);
  const int reversed[] = {63, 75, 88, 100};
  for (int i = 0; i < 4; i++) {
    assert(sz_view_paint(root, canvas, 20, 12, &theme));
    assert(sz_view_animated_opacity_value(animation) == reversed[i]);
  }
  assert(!sz_view_motion_pending(root));
  sz_signal_int_set(value, INT64_MIN);
  for (int i = 0; i < 4; i++) assert(sz_view_paint(root, canvas, 20, 12, &theme));
  assert(sz_view_animated_opacity_value(animation) == 0 && sz_signal_int_get(value) == INT64_MIN);
  assert(px_rgb(sk_surface_peek_pixels(surface, NULL), 20, 5, 5, 255, 255, 255));
  sz_signal_int_set(value, INT64_MAX);
  assert(sz_view_paint(root, canvas, 20, 12, &theme));
  assert(sz_view_animated_opacity_value(animation) == 25 && sz_signal_int_get(value) == INT64_MAX);
  sz_signal_int_set(reduced, 1);
  assert(sz_view_paint(root, canvas, 20, 12, &theme));
  assert(sz_view_animated_opacity_value(animation) == 100 && !sz_view_motion_pending(root));
  sz_signal_int_set(reduced, 0); sz_signal_int_set(value, 0);
  assert(sz_view_paint(root, canvas, 20, 12, &theme));
  assert(sz_view_animated_opacity_value(animation) == 75);
  sz_signal_int_set(visible, 0);
  sz_view_layout(root, 20, 12, &theme);
  assert(sz_view_animated_opacity_value(animation) == 0 && !sz_view_motion_pending(root));
  sz_signal_int_set(value, 100);
  sz_view_layout(root, 20, 12, &theme);
  sz_signal_int_set(visible, 1);
  assert(sz_view_paint(root, canvas, 20, 12, &theme));
  assert(sz_view_animated_opacity_value(animation) == 100 && !sz_view_motion_pending(root));
  sk_surface_unref(surface); sz_view_free(root);
  sz_signal_int_free(value); sz_signal_int_free(reduced); sz_signal_int_free(visible);
  SzSignalInt *presses = sz_signal_int(0), *preference = sz_signal_int(1);
  SzView *button = sz_view_button("Press", counter_tap, presses);
  root = sz_view_reduced_motion(preference, button);
  sz_view_layout(root, 100, 100, &theme);
  assert(sz_view_activate(root, button, 5, 5));
  assert(sz_signal_int_get(presses) == 1 && !sz_view_motion_pending(root));
  sz_signal_int_set(preference, 0);
  sz_view_layout(root, 100, 100, &theme);
  assert(sz_view_activate(root, button, 5, 5));
  assert(sz_signal_int_get(presses) == 2 && sz_view_motion_pending(root));
  sz_view_reduce_motion(root, 1);
  sz_view_layout(root, 100, 100, &theme);
  assert(!sz_view_motion_pending(root));
  sz_view_free(root); sz_signal_int_free(presses); sz_signal_int_free(preference);
  sz_alloc_stats(&end_bytes, &end_count);
  assert(end_bytes == before_bytes && end_count == before_count);
}

static void test_opacity_session_pause_and_reduction(void) {
  SzSignalInt *value = sz_signal_int(100);
  SzSignalStr *text = sz_signal_str("Keep this text");
  SzView *field = sz_view_text_field(text, "Name");
  SzView *root = sz_view_animated_opacity(value, 4, field);
  SzUiConfig cfg = {.kind = SZ_UI_RUNTIME_HEADLESS, .width = 240, .height = 100, .scale = 1};
  SzUiSession *session = sz_ui_mount(&cfg, root);
  assert(session && sz_ui_pump_sync(session));
  sz_view_focus(root, field);
  sz_signal_int_set(value, 0);
  assert(sz_ui_pump_sync(session) && sz_view_animated_opacity_value(root) == 75);
  SzInputEvent event = {.kind = SZ_INPUT_LIFECYCLE, .lifecycle = SZ_LIFECYCLE_PAUSE};
  assert(sz_ui_inject_sync(session, &event));
  for (int i = 0; i < 3; i++) assert(sz_ui_pump_sync(session));
  assert(sz_view_animated_opacity_value(root) == 75 && !sz_view_motion_pending(root));
  sz_signal_int_set(value, 100);
  assert(sz_ui_pump_sync(session) && sz_view_animated_opacity_value(root) == 75);
  event.lifecycle = SZ_LIFECYCLE_RESUME;
  assert(sz_ui_inject_sync(session, &event));
  assert(sz_ui_pump_sync(session) && sz_view_animated_opacity_value(root) == 81);
  for (int i = 0; i < 3; i++) assert(sz_ui_pump_sync(session));
  assert(sz_view_animated_opacity_value(root) == 100 && !sz_view_motion_pending(root));
  sz_signal_int_set(value, 0);
  event.kind = SZ_INPUT_REDUCED_MOTION; event.reduced_motion = 1;
  assert(sz_ui_inject_sync(session, &event) && sz_ui_pump_sync(session));
  assert(sz_view_animated_opacity_value(root) == 0 && !sz_view_motion_pending(root));
  assert(sz_view_focused(root) == field);
  SzView *fields[1];
  assert(sz_view_collect_text_fields(root, fields, 1) == 1 && fields[0] == field);
  event.kind = SZ_INPUT_TEXT; event.text = "Retained input";
  assert(sz_ui_inject_sync(session, &event));
  assert(!strcmp(sz_signal_str_get(text), "Retained input"));
  event.kind = SZ_INPUT_REDUCED_MOTION; event.reduced_motion = 0;
  assert(sz_ui_inject_sync(session, &event));
  sz_signal_int_set(value, 100);
  assert(sz_ui_pump_sync(session) && sz_view_animated_opacity_value(root) == 25);
  sz_ui_unmount(session); sz_view_free(root);
  root = sz_view_animated_opacity(value, INT_MAX, sz_view_text("long"));
  session = sz_ui_mount(&cfg, root);
  assert(session && sz_ui_pump_sync(session));
  sz_signal_int_set(value, 0);
  assert(sz_ui_quiesce(session) == SZ_QUIESCE_BUDGET_TRIPPED);
  assert(sz_view_motion_pending(root) && sz_view_animated_opacity_value(root) == 100);
  event.kind = SZ_INPUT_REDUCED_MOTION; event.reduced_motion = 1;
  assert(sz_ui_inject_sync(session, &event));
  assert(sz_ui_quiesce(session) == SZ_QUIESCE_SETTLED);
  assert(sz_view_animated_opacity_value(root) == 0);
  event.kind = SZ_INPUT_LIFECYCLE; event.lifecycle = SZ_LIFECYCLE_PAUSE;
  assert(sz_ui_inject_sync(session, &event));
  SzView *old = root;
  root = sz_view_animated_opacity(value, 8, sz_view_text("replacement"));
  assert(sz_ui_session_replace_root(session, root));
  sz_view_free(old);
  assert(sz_ui_pump_sync(session));
  sz_signal_int_set(value, 100);
  assert(sz_ui_pump_sync(session) && sz_view_animated_opacity_value(root) == 100);
  event.kind = SZ_INPUT_REDUCED_MOTION; event.reduced_motion = 0;
  assert(sz_ui_inject_sync(session, &event));
  sz_signal_int_set(value, 0);
  assert(sz_ui_pump_sync(session) && sz_view_animated_opacity_value(root) == 100);
  assert(!sz_view_motion_pending(root));
  sz_ui_unmount(session); sz_view_free(root);
  sz_signal_int_free(value); sz_signal_str_free(text);
}

static void test_opacity_utf8_labels(void) {
  const struct { int length; const char *glyph; int keep, expected; } cases[] = {
    {63, "é", 0, 63}, {62, "é", 1, 62},
    {62, "中", 0, 62}, {61, "中", 1, 61},
    {61, "😀", 0, 61}, {60, "😀", 1, 60},
    {65, "中", 0, 64}, {0, "中", 1, 0}
  };
  char ascii[80]; memset(ascii, 'a', 79); ascii[79] = 0;
  SzSignalInt *value = sz_signal_int(50);
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char label[100], expected[80], line[120];
    snprintf(label, sizeof label, "%.*s%s", cases[i].length, ascii, cases[i].glyph);
    snprintf(expected, sizeof expected, "%.*s%s", cases[i].expected, ascii,
      cases[i].keep ? cases[i].glyph : "");
    SzView *root = sz_view_animated_opacity(value, 8, sz_view_text(label));
    sz_view_layout(root, 800, 160, sz_theme_default());
    SzString *dump = sz_view_a11y_dump(root);
    snprintf(line, sizeof line, "opacity:%s=50\n", expected);
    assert(!strncmp(sz_string_cstr(dump), line, strlen(line)));
    snprintf(line, sizeof line, "target:opacity:%s=50\n", expected);
    assert(strstr(sz_string_cstr(dump), line));
    snprintf(line, sizeof line, "pumps:opacity:%s=0\n", expected);
    assert(strstr(sz_string_cstr(dump), line));
    sz_string_free(dump); sz_view_free(root);
  }
  sz_signal_int_free(value);
}

static void test_opacity_recorded_pumps(void) {
  const char *path = "/tmp/scuzz-opacity-journal.json";
  SzSignalInt *value = sz_signal_int(0);
  SzView *button = sz_view_button("Increment", counter_tap, value);
  SzView *root = sz_view_animated_opacity(value, 8, button);
  SzUiConfig cfg = {.kind = SZ_UI_RUNTIME_HEADLESS, .width = 240, .height = 100, .scale = 1};
  SzUiSession *session = sz_ui_mount(&cfg, root);
  assert(session && sz_ui_pump_sync(session));
  sz_view_focus(root, button);
  assert(sz_ui_session_set_record(session, path));
  SzInputEvent event = {.kind = SZ_INPUT_KEY, .key = "Space"};
  assert(sz_ui_session_live_inject(session, &event));
  assert(sz_ui_session_live_inject(session, &event));
  assert(sz_signal_int_get(value) == 2 && sz_view_animated_opacity_value(root) == 0);
  for (int i = 0; i < 2; i++) assert(sz_ui_pump_sync(session));
  assert(sz_view_animated_opacity_value(root) == 1);
  event.kind = SZ_INPUT_REDUCED_MOTION; event.reduced_motion = 1;
  assert(sz_ui_session_live_inject(session, &event) && sz_ui_pump_sync(session));
  assert(sz_view_animated_opacity_value(root) == 2 && !sz_view_motion_pending(root));
  char *record = slurp_cstr(path);
  assert(strstr(record, "\"manualPumps\":true") && strstr(record, "\"op\":\"pump\",\"k\":2"));
  assert(strstr(record, "\"op\":\"reduceMotion\",\"value\":true"));
  sz_ui_unmount(session); sz_view_free(root); sz_signal_int_free(value);
  value = sz_signal_int(0);
  button = sz_view_button("Increment", counter_tap, value);
  root = sz_view_animated_opacity(value, 8, button);
  session = sz_ui_mount(&cfg, root);
  assert(session && sz_ui_pump_sync(session));
  sz_view_focus(root, button);
  sz_ui_script_run_file(session, path);
  assert(sz_signal_int_get(value) == 2 && sz_view_animated_opacity_value(root) == 2);
  assert(!sz_view_motion_pending(root) && sz_view_focused(root) == button);
  sz_ui_unmount(session); sz_view_free(root); sz_signal_int_free(value);
  free(record); remove(path);
}

static SzSignalList *select_test_options(const char *const *labels, int count) {
  SzList *items = NULL;
  for (int i = count - 1; i >= 0; i--) {
    SzString *label = sz_string_from_cstr(labels[i]);
    SzList *next = sz_list_cons(label, items);
    sz_release(label); sz_release(items);
    items = next;
  }
  SzSignalList *options = sz_signal_list(items);
  sz_release(items);
  return options;
}

static void test_select_keyboard_window(void) {
  uint64_t before_bytes, before_count, end_bytes, end_count;
  sz_alloc_stats(&before_bytes, &before_count);
  SzSignalInt *selected = sz_signal_int(0);
  SzSignalList *options = sz_signal_list(NULL);
  SzView *empty = sz_view_select(selected, "Empty", options);
  sz_view_layout(empty, 280, 720, sz_theme_default());
  assert(!sz_view_is_tap_target(empty) && !sz_view_tab_stop(empty));
  assert(!sz_view_activate(empty, empty, 10, 10));
  assert(!sz_view_select_open(empty));
  sz_view_free(empty);
  sz_signal_list_free(options);
  char labels[1000][24];
  const char *choices[1000];
  for (int i = 0; i < 1000; i++) {
    snprintf(labels[i], sizeof labels[i], "Item%04d", i);
    choices[i] = i < 3 ? "Same" : labels[i];
  }
  options = select_test_options(choices, 1000);
  SzView *select = sz_view_select(selected, "Language", options);
  sz_alloc_stats(&end_bytes, &end_count);
  assert(end_bytes - before_bytes < 200000);
  sz_view_layout(select, 280, 720, sz_theme_default());
  assert(sz_view_select_count(select) == 1000 && !strcmp(sz_view_select_text(select), "Same"));
  sz_view_focus(select, select);
  assert(sz_view_handle_key(select, "ArrowDown", "", 0));
  assert(sz_view_select_open(select) && sz_view_select_active(select) == 0);
  assert(sz_signal_int_get(selected) == 0);
  sz_view_layout(select, 280, 720, sz_theme_default());
  uintptr_t first_id = sz_view_id(sz_view_select_active_option(select));
  assert(first_id);
  SzView *taps[32];
  int count = sz_view_collect_tap_targets(select, taps, 32);
  assert(count >= 2 && count <= 10);
  assert(sz_view_focused(select) == select);
  sz_view_handle_key(select, "s", "", 0);
  assert(sz_view_select_active(select) == 1 && sz_signal_int_get(selected) == 0);
  sz_view_handle_key(select, "S", "", 0);
  assert(sz_view_select_active(select) == 2);
  sz_view_handle_key(select, "Escape", "", 0);
  assert(!sz_view_select_open(select) && sz_signal_int_get(selected) == 0);
  sz_view_handle_key(select, "End", "", 0);
  sz_view_layout(select, 280, 720, sz_theme_default());
  assert(sz_view_select_active(select) == 999 && sz_signal_int_get(selected) == 0);
  SzView *last = sz_view_select_active_option(select);
  assert(last && sz_view_id(last) != first_id);
  SzRect option = sz_view_control_frame(last);
  SzView *popup = sz_view_select_popup(select);
  SzRect frame = sz_view_frame(popup);
  assert(option.y >= frame.y && option.y + option.h <= frame.y + frame.h + .01f);
  assert(sz_view_hit_test(select, option.x + 2, option.y + 2) == last);
  sz_view_set_hover_at(select, option.x + 2, option.y + 2);
  sz_view_handle_key(select, "PageUp", "", 0);
  sz_view_layout(select, 280, 720, sz_theme_default());
  assert(sz_view_select_active(select) == 989);
  sz_view_set_hover_at(select, option.x + 2, option.y + 2);
  assert(sz_view_select_active(select) == 989);
  sz_view_layout(select, 180, 160, sz_theme_default());
  assert(sz_view_select_active(select) == 989 && sz_view_select_active_option(select));
  frame = sz_view_frame(popup);
  assert(frame.x >= 0 && frame.y >= 0 && frame.x + frame.w <= 180 && frame.y + frame.h <= 160);
  sz_view_handle_key(select, "End", "", 0);
  sz_view_layout(select, 280, 720, sz_theme_default());
  sz_view_handle_key(select, "Enter", "", 0);
  assert(!sz_view_select_open(select) && sz_signal_int_get(selected) == 999);
  sz_view_handle_key(select, "Home", "", 0);
  sz_view_layout(select, 280, 720, sz_theme_default());
  assert(sz_view_id(sz_view_select_active_option(select)) == first_id);
  sz_view_handle_key(select, "PageDown", "", 0);
  sz_view_layout(select, 280, 720, sz_theme_default());
  assert(sz_view_select_active(select) == 10 && sz_signal_int_get(selected) == 999);
  sz_signal_int_set(selected, 42);
  sz_view_layout(select, 280, 720, sz_theme_default());
  assert(sz_view_select_active(select) == 42 && sz_view_select_active_option(select));
  sz_view_handle_key(select, "Escape", "", 0);
  assert(sz_signal_int_get(selected) == 42);
  sz_signal_int_set(selected, INT64_MIN);
  sz_view_handle_key(select, "ArrowDown", "", 0);
  sz_view_layout(select, 280, 720, sz_theme_default());
  assert(sz_view_select_active(select) == 0 && !strcmp(sz_view_select_text(select), ""));
  assert(sz_signal_int_get(selected) == INT64_MIN);
  sz_view_handle_key(select, "Enter", "", 0);
  assert(sz_signal_int_get(selected) == 0);
  sz_view_input_time(select, 0);
  sz_view_handle_key(select, "I", "", 0);
  sz_view_handle_key(select, "t", "", 0);
  assert(sz_view_select_active(select) == 3);
  sz_view_input_time(select, 1001);
  sz_view_handle_key(select, "s", "", 0);
  assert(sz_view_select_active(select) == 0);
  assert(sz_signal_int_get(selected) == 0);
  sz_view_handle_key(select, "End", "", 0);
  sz_view_layout(select, 280, 720, sz_theme_default());
  assert(sz_view_scroll_wheel_target(select, 10, 10, 0, 80) == NULL);
  assert(sz_view_scroll_pan(popup, 0, -10000));
  sz_view_layout(select, 280, 720, sz_theme_default());
  assert(sz_view_select_active(select) < 800 && sz_view_select_active_option(select));
  assert(sz_signal_int_get(selected) == 0);
  sz_view_handle_key(select, "Escape", "", 0);
  SkSurface *surface = sk_surface_make_raster_n32_premul(280, 720);
  sz_view_handle_key(select, "ArrowDown", "", 0);
  assert(sz_view_paint(select, sk_surface_get_canvas(surface), 280, 720, sz_theme_default()));
  const uint8_t *pixels = sk_surface_peek_pixels(surface, NULL);
  frame = sz_view_control_frame(sz_view_select_active_option(select));
  assert(px_rgb(pixels, 280, (int)(frame.x + frame.w - 3), (int)(frame.y + frame.h * .5f),
    (sz_theme_default()->selection >> 16) & 255, (sz_theme_default()->selection >> 8) & 255, sz_theme_default()->selection & 255));
  sk_surface_unref(surface);
  sz_view_free(select);
  sz_signal_list_free(options);
  sz_signal_int_free(selected);
  sz_alloc_stats(&end_bytes, &end_count);
  assert(end_bytes == before_bytes && end_count == before_count);
}

static void select_test_replace(SzSignalList *options, const char *const *labels, int count) {
  SzSignalList *next = select_test_options(labels, count);
  sz_signal_list_set(options, sz_signal_list_get(next));
  sz_signal_list_free(next);
}

static void test_select_reactive_options(void) {
  uint64_t bytes, count, end_bytes, end_count;
  sz_alloc_stats(&bytes, &count);
  const char *initial[] = {"Alpha", "Beta", "中文"};
  const char *replacement[] = {"New", "Same", "Same", "Last"};
  const char *shorter[] = {"Only"};
  SzSignalList *options = select_test_options(initial, 3);
  SzSignalInt *value = sz_signal_int(1);
  SzSignalStr *name = sz_signal_str("Retained");
  SzView *select = sz_view_select(value, "Language", options);
  SzView *field = sz_view_text_field(name, "Name");
  SzView *root = sz_view_column();
  sz_view_add_child(root, select); sz_view_add_child(root, field);
  SzUiConfig cfg = {.kind = SZ_UI_RUNTIME_HEADLESS, .width = 280, .height = 360, .scale = 1.f};
  SzUiSession *session = sz_ui_mount(&cfg, root);
  assert(session && sz_ui_pump_sync(session));
  uintptr_t control = sz_view_id(select), field_id = sz_view_id(field);
  sz_view_focus(root, select);
  sz_view_handle_key(root, "End", "", 0);
  sz_view_layout(root, 280, 360, sz_theme_default());
  SzView *old_option = sz_view_select_active_option(select);
  uintptr_t old_id = sz_view_id(old_option);
  select_test_replace(options, replacement, 4);
  assert(!sz_ui_session_activate_view(session, old_option));
  assert(!sz_view_find(root, old_id));
  assert(!sz_view_select_open(select) && sz_signal_int_get(value) == 1);
  assert(sz_view_focused(root) == select && !strcmp(sz_view_select_text(select), "Same"));
  assert(sz_view_id(select) == control && sz_view_id(field) == field_id);
  sz_view_handle_key(root, "End", "", 0);
  sz_view_layout(root, 280, 360, sz_theme_default());
  SzView *last = sz_view_select_active_option(select);
  SzRect frame = sz_view_control_frame(last);
  SzInputEvent event = {.kind = SZ_INPUT_POINTER, .pointer_phase = SZ_POINTER_DOWN,
    .x = frame.x + 3, .y = frame.y + frame.h * .5f};
  assert(sz_ui_inject_sync(session, &event));
  select_test_replace(options, initial, 3);
  event.pointer_phase = SZ_POINTER_UP;
  assert(sz_ui_inject_sync(session, &event));
  assert(sz_signal_int_get(value) == 1 && !sz_view_select_open(select));
  sz_view_handle_key(root, "End", "", 0);
  select_test_replace(options, shorter, 1);
  /* Focus transfer cannot commit an index from the old list. */
  sz_view_focus(root, field);
  assert(sz_signal_int_get(value) == 1 && !sz_view_select_open(select));
  assert(!strcmp(sz_view_select_text(select), ""));
  sz_view_focus(root, select);
  sz_view_handle_key(root, "Home", "", 0);
  sz_view_handle_key(root, "Enter", "", 0);
  assert(sz_signal_int_get(value) == 0 && !strcmp(sz_view_select_text(select), "Only"));
  select_test_replace(options, initial, 3);
  sz_view_handle_key(root, "B", "", 0);
  assert(sz_view_select_active(select) == 1);
  sz_signal_list_set(options, NULL);
  sz_view_handle_key(root, "Enter", "", 0);
  assert(sz_view_select_count(select) == 0 && !sz_view_select_open(select));
  assert(!sz_view_tab_stop(select) && !sz_view_is_tap_target(select));
  assert(sz_signal_int_get(value) == 0);
  for (int i = 0; i < 30; i++) {
    select_test_replace(options, replacement, 4);
    select_test_replace(options, initial, 3);
    assert(sz_ui_pump_sync(session));
    assert(sz_view_select_count(select) == 3 && !strcmp(sz_view_select_text(select), "Alpha"));
    sz_view_handle_key(root, "B", "", 0);
    assert(sz_view_select_active(select) == 1);
  }
  assert(sz_view_focused(root) == select);
  assert(!strcmp(sz_signal_str_get(name), "Retained"));
  sz_ui_unmount(session); sz_view_free(root);
  sz_signal_list_free(options); sz_signal_int_free(value); sz_signal_str_free(name);
  sz_alloc_stats(&end_bytes, &end_count);
  assert(bytes == end_bytes && count == end_count);
}

static void test_select_pointer_identity(void) {
  const char *labels[100];
  for (int i = 0; i < 100; i++) labels[i] = "Same";
  SzSignalInt *value = sz_signal_int(0);
  SzSignalList *options = select_test_options(labels, 100);
  SzView *root = sz_view_select(value, "Items", options);
  SzUiConfig cfg = {.kind = SZ_UI_RUNTIME_HEADLESS, .width = 280, .height = 360, .scale = 1.f};
  SzUiSession *session = sz_ui_mount(&cfg, root);
  assert(session && sz_ui_pump_sync(session));
  sz_view_focus(root, root);
  sz_view_handle_key(root, "Home", "", 0);
  sz_view_layout(root, 280, 360, sz_theme_default());
  SzView *option = sz_view_select_active_option(root);
  uintptr_t first = sz_view_id(option);
  sz_signal_int_set(value, 99);
  assert(!sz_ui_session_activate_view(session, option));
  assert(sz_signal_int_get(value) == 99 && !sz_view_find(root, first));
  SzRect fr = sz_view_control_frame(sz_view_select_active_option(root));
  SzInputEvent event = {.kind = SZ_INPUT_POINTER, .pointer_phase = SZ_POINTER_DOWN,
    .x = fr.x + 3, .y = fr.y + fr.h * .5f};
  assert(sz_ui_inject_sync(session, &event));
  sz_signal_int_set(value, 0);
  sz_view_layout(root, 280, 360, sz_theme_default());
  event.pointer_phase = SZ_POINTER_UP;
  sz_ui_inject_sync(session, &event);
  assert(sz_signal_int_get(value) == 0);
  sz_view_handle_key(root, "End", "", 0);
  sz_signal_int_set(value, 42);
  sz_view_focus(root, NULL);
  assert(sz_signal_int_get(value) == 42);
  sz_ui_unmount(session);
  sz_view_free(root); sz_signal_list_free(options); sz_signal_int_free(value);
}

static void test_select_scope_and_journal(void) {
  const char *choices[] = {"Alpha", "Alpine", "Beta", "Bravo", "中文"};
  SzSignalInt *value = sz_signal_int(0), *dialog = sz_signal_int(0);
  SzSignalStr *text = sz_signal_str("Keep this text");
  SzSignalList *options = select_test_options(choices, 5);
  SzView *select = sz_view_select(value, "Language", options);
  SzView *field = sz_view_text_field(text, "Name");
  SzView *column = sz_view_column();
  sz_view_add_child(column, select); sz_view_add_child(column, field);
  SzView *dialog_select = sz_view_select(value, "Dialog choice", options);
  SzView *card = sz_view_card(dialog_select);
  SzView *overlay = sz_view_overlay(dialog, "Settings", card);
  SzView *root = sz_view_stack();
  sz_view_add_child(root, column); sz_view_add_child(root, overlay);
  SzUiConfig cfg = {.kind = SZ_UI_RUNTIME_HEADLESS, .width = 280, .height = 360, .scale = 1.0};
  SzUiSession *session = sz_ui_mount(&cfg, root);
  assert(session); sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  sz_view_focus(root, select);
  sz_view_handle_key(root, "End", "", 0);
  sz_view_layout(root, 280, 360, sz_theme_default());
  assert(sz_view_input_scope(root) == select);
  SzView *fields[8];
  assert(sz_view_collect_text_fields(root, fields, 8) == 0);
  assert(sz_view_activate(root, field, 10, 60));
  assert(!sz_view_select_open(select) && sz_signal_int_get(value) == 0);
  assert(!strcmp(sz_signal_str_get(text), "Keep this text"));
  assert(sz_view_focused(root) == select);
  sz_view_handle_key(root, "End", "", 0);
  sz_view_focus(root, field);
  assert(sz_signal_int_get(value) == 4 && !sz_view_select_open(select));
  sz_signal_int_set(dialog, 1);
  sz_view_layout(root, 280, 360, sz_theme_default());
  assert(sz_view_focused(root) == dialog_select);
  sz_view_handle_key(root, "Home", "", 0);
  sz_view_layout(root, 280, 360, sz_theme_default());
  SzRect popup = sz_view_frame(sz_view_select_popup(dialog_select));
  assert(popup.y >= 0 && popup.y + popup.h <= 360);
  assert(sz_view_input_scope(root) == dialog_select);
  sz_view_handle_key(root, "Escape", "", 0);
  assert(sz_signal_int_get(dialog) == 1 && !sz_view_select_open(dialog_select));
  sz_view_handle_key(root, "Escape", "", 0);
  assert(sz_signal_int_get(dialog) == 0);
  sz_view_layout(root, 280, 360, sz_theme_default());
  sz_view_focus(root, select);
  const char *record = "/tmp/scuzz-select-time.json";
  remove(record);
  assert(sz_ui_session_set_record(session, record));
  SzInputEvent key = {.kind = SZ_INPUT_KEY, .key = "B"};
  assert(sz_ui_session_live_inject(session, &key));
  assert(sz_view_select_active(select) == 2 && sz_signal_int_get(value) == 4);
  sz_ui_session_input_time(session, 1001);
  key.key = "r";
  assert(sz_ui_session_live_inject(session, &key));
  assert(sz_view_select_active(select) == 2);
  key.key = "Enter";
  assert(sz_ui_session_live_inject(session, &key));
  assert(sz_signal_int_get(value) == 2);
  char *body = slurp_cstr(record);
  assert(strstr(body, "\"op\":\"inputTime\"") && strstr(body, "\"key\":\"r\""));
  sz_ui_unmount(session);
  sz_signal_int_free(dialog); sz_signal_str_free(text); sz_signal_int_free(value);
  value = sz_signal_int(4);
  root = sz_view_select(value, "Language", options);
  session = sz_ui_mount(&cfg, root);
  assert(session); sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  sz_view_focus(root, root);
  sz_ui_script_run_file(session, record);
  assert(sz_signal_int_get(value) == 2 && !sz_view_select_open(root));
  free(body); remove(record);
  sz_ui_unmount(session); sz_signal_list_free(options); sz_signal_int_free(value);
}

static void test_fallback_text_layout(void) {
  if (sk_font_measure_string("i", 14) == sk_font_measure_string("W", 14)) return;
  const char *source[] = {"क्षि", "ปู่", "中文漢字"};
  for (int i = 0; i < 3; i++) {
    SzText *span = test_text_span(source[i]);
    SzText *large = sz_text_size(32, span);
    SzText *styled = sz_text_italic(large);
    TextProbe probe = {0};
    SzTextMetrics metrics = sz_text_layout(styled, 14, 1, 6, 400, 0, 0, text_probe, &probe);
    assert(metrics.lines == 1 && probe.count == 1);
    float ascent, descent;
    sk_font_span_metrics(sk_text_range(source[i], strlen(source[i])), 32, SK_TEXT_ITALIC, &ascent, &descent);
    assert(probe.baseline[0] >= ascent && metrics.height >= probe.baseline[0] + descent);
    SzView *plain = sz_view_text(source[i]);
    SzView *root = sz_view_font_size(32, plain);
    sz_view_layout(root, 400, 100, sz_theme_default());
    SzRect frame = sz_view_frame(plain);
    SkSurface *surface = sk_surface_make_raster_n32_premul(400, 100);
    assert(sz_view_paint(root, sk_surface_get_canvas(surface), 400, 100, sz_theme_default()));
    const uint8_t *pixels = sk_surface_peek_pixels(surface, NULL);
    int ink = 0;
    for (int y = 0; y < 100; y++) for (int x = 0; x < 400; x++) {
      const uint8_t *pixel = pixels + (y * 400 + x) * 4;
      if (pixel[0] < 100 && pixel[1] < 100 && pixel[2] < 100) {
        ink++;
        assert(y >= frame.y && y < ceilf(frame.y + frame.h));
      }
    }
    assert(ink > 20);
    sk_surface_unref(surface);
    sz_view_free(root);
    sz_release(styled); sz_release(large); sz_release(span);
  }
  SzText *span = test_text_span("क्षिक्षिक्षि");
  TextProbe probe = {0};
  SzTextMetrics metrics = sz_text_layout(span, 14, 1, 6, 14.f, 0, 0, text_probe, &probe);
  assert(metrics.lines == 3 && probe.count == 3);
  for (int i = 0; i < 3; i++) assert(probe.starts[i] == i * 12 && probe.ends[i] == (i + 1) * 12);
  sz_release(span);
  SzSignalStr *value = sz_signal_str("中文");
  SzView *field = sz_view_text_field(value, "Name");
  sz_view_layout(field, 320, 100, sz_theme_default());
  sz_view_focus(field, field);
  sz_view_set_text_field_caret(field, 0);
  SzRect start = sz_view_caret_rect(field, sz_theme_default());
  sz_view_set_text_field_caret(field, 6);
  SzRect end = sz_view_caret_rect(field, sz_theme_default());
  assert(fabsf(end.x - start.x - 28.f) < .01f);
  sz_view_activate(field, field, start.x + 14.f, start.y + 2);
  assert(sz_view_text_field_caret(field) == 3);
  sz_view_set_text_field_sel(field, 0, 6);
  sz_view_handle_compose(field, "क्षि");
  assert(!strcmp(sz_signal_str_get(value), "中文"));
  sz_view_handle_compose(field, "");
  assert(!strcmp(sz_signal_str_get(value), "क्षि"));
  sz_view_free(field); sz_signal_str_free(value);
}

static void test_styled_text_values(void) {
  uint64_t before_bytes, before_count, bytes, count;
  sz_alloc_stats(&before_bytes, &before_count);
  SzText *a = test_text_span("Scu");
  SzText *b = test_text_span("zz λ🙂");
  SzText *bold = sz_text_bold(a);
  SzText *colored = sz_text_color(0xffbb2200, b);
  SzText *joined = test_text_pair(bold, colored);
  assert(a->runs[0].style == 0 && b->runs[0].colored == 0);
  assert(joined->runs[0].style == SK_TEXT_BOLD && joined->runs[1].colored);
  SzString *plain = sz_text_plain(joined);
  assert(!strcmp(sz_string_cstr(plain), "Scuzz λ🙂"));
  SzText *italic = sz_text_italic(joined);
  SzText *underlined = sz_text_underline(italic);
  SzText *mono = sz_text_monospace(underlined);
  assert(mono->runs[0].style == (SK_TEXT_BOLD | SK_TEXT_ITALIC | SK_TEXT_MONO | SK_TEXT_UNDERLINE));
  sz_release(plain); sz_release(a); sz_release(b); sz_release(bold); sz_release(colored);
  sz_release(italic); sz_release(underlined); sz_release(mono); sz_release(joined);
  SzText *piece = test_text_span("x");
  SzList *pieces = NULL;
  for (int i = 0; i < 512; i++) {
    SzList *next = sz_list_cons(piece, pieces);
    sz_release(pieces); pieces = next;
  }
  SzText *many = sz_text_concat(pieces);
  sz_release(pieces); sz_release(piece);
  TextProbe probe = {0};
  SzTextMetrics many_metrics = sz_text_layout(many, 14, 1, 6, 0, 0, 0, text_probe, &probe);
  assert(many->length == 512 && probe.count == 512 && many_metrics.lines == 1);
  sz_release(many);
  SzText *empty = sz_text_concat(NULL);
  SzTextMetrics metrics = sz_text_layout(empty, 14, 1, 6, 100, 0, 0, NULL, NULL);
  assert(metrics.lines == 1 && metrics.width == 0.f && metrics.height == 20.f);
  sz_release(empty);
  empty = test_text_span("");
  SzText *large_empty = sz_text_size(24, empty);
  TextProbe empty_probe = {0};
  metrics = sz_text_layout(large_empty, 14, 1, 6, 100, 0, 0, text_probe, &empty_probe);
  assert(metrics.lines == 1 && empty_probe.count == 1 && empty_probe.fonts[0] == 24.f);
  sz_release(empty); sz_release(large_empty);
  sz_alloc_stats(&bytes, &count);
  assert(bytes == before_bytes && count == before_count);
}

static void test_wrapped_paragraph_direction(void) {
  if (sk_font_measure_string("i", 24) == sk_font_measure_string("W", 24)) return;
  SzText *prefix = test_text_span("אבגדהוז ");
  SzText *latin = test_text_span("AB ");
  SzText *hebrew = test_text_span("אב");
  SzText *colored = sz_text_color(0xff165ca0, latin);
  SzText *tail = test_text_pair(colored, hebrew);
  SzText *text = test_text_pair(prefix, tail);
  float width = ceilf(sk_font_measure_span(sk_text_range("אבגדהוז", strlen("אבגדהוז")), 24, 0)) + 1.f;
  TextProbe probe = {0};
  SzTextMetrics metrics = sz_text_layout(text, 24, 1, 6, width, 0, 0, text_probe, &probe);
  assert(metrics.lines == 2 && probe.count == 3);
  assert(probe.baseline[1] == probe.baseline[2]);
  /* The Latin run follows the Hebrew run in the RTL paragraph. */
  assert(probe.x[1] > probe.x[2]);
  SzTheme theme = *sz_theme_default();
  theme.background = 0; theme.font_px = 24; theme.foreground = 0xff165ca0;
  SzView *view = sz_view_rich_text(text);
  width += 4.f;
  SkSurface *actual = sk_surface_make_raster_n32_premul((int)width, 96);
  SkSurface *expected = sk_surface_make_raster_n32_premul((int)width, 96);
  assert(actual && expected);
  assert(sz_view_paint(view, sk_surface_get_canvas(actual), width, 96, &theme));
  SkCanvas *canvas = sk_surface_get_canvas(expected);
  sk_canvas_clear(canvas, sk_color_argb(0));
  SkPaint *paint = sk_paint_new();
  sk_paint_set_text_size(paint, 24); sk_paint_set_color(paint, sk_color_argb(theme.foreground));
  /* The direction mark gives the independent line the paragraph direction. */
  const char *reference = "\xe2\x80\x8f" "AB אב";
  sk_canvas_draw_span(canvas, sk_text_range(reference, strlen(reference)), 2, probe.baseline[1], paint, 0);
  const uint8_t *a = sk_surface_peek_pixels(actual, NULL);
  const uint8_t *b = sk_surface_peek_pixels(expected, NULL);
  assert(!memcmp(a + (int)width * 30 * 4, b + (int)width * 30 * 4, (size_t)(int)width * 66 * 4));
  sz_view_free(view);
  view = sz_view_text(sz_text_source(text));
  assert(sz_view_paint(view, sk_surface_get_canvas(actual), width, 96, &theme));
  sk_canvas_clear(canvas, sk_color_argb(0));
  sk_canvas_draw_span(canvas, sk_text_range(reference, strlen(reference)), 2, 56, paint, 0);
  a = sk_surface_peek_pixels(actual, NULL);
  b = sk_surface_peek_pixels(expected, NULL);
  assert(!memcmp(a + (int)width * 30 * 4, b + (int)width * 30 * 4, (size_t)(int)width * 66 * 4));
  sk_paint_delete(paint); sk_surface_unref(actual); sk_surface_unref(expected);
  sz_view_free(view);
  sz_release(prefix); sz_release(latin); sz_release(hebrew);
  sz_release(colored); sz_release(tail); sz_release(text);
}

static void test_plain_paragraph_direction(void) {
  if (sk_font_measure_string("i", 24) == sk_font_measure_string("W", 24)) return;
  const char *prefix[] = {"אבגדהוזחטיכלמנסע ", "אבגדהוזחטיכלמנסע\n",
    ("\xe2\x80\xab" "AAAAAAAAAAAAAAAA "), "AAAAAAAAAAAAAAAA ",
    "AAAAAAAAAAAAAAAA \xe2\x81\xa7", "אבגדהוזחטיכלמנסע ", "AAAAAAAAAAAAAAAA ", "אבגדהוזחטיכלמנסע "};
  const char *line[] = {"AB אב", "AB אב", "AB אב", "אב AB", "AB אב", "12: AB אב!", "12 אב AB!", "AB אב"};
  const char *suffix[] = {"", "", "\xe2\x80\xac", "", "\xe2\x81\xa9", "", "", "\nmore"};
  const int rtl[] = {1, 0, 1, 0, 1, 1, 0, 1};
  SzTheme theme = *sz_theme_default();
  theme.background = 0; theme.font_px = 24; theme.foreground = 0xff165ca0;
  for (int i = 0; i < 8; i++) {
    char source[256], reference[128];
    snprintf(source, sizeof source, "%s%s%s", prefix[i], line[i], suffix[i]);
    snprintf(reference, sizeof reference, "%s%s%s", rtl[i] ? "\xe2\x80\x8f" : "\xe2\x80\x8e", line[i], i == 7 ? "..." : "");
    int width = (int)ceilf(sk_font_measure_span(sk_text_range(prefix[i], strlen(prefix[i])), 24, 0)) + 4;
    SzView *text = sz_view_text(source);
    SzView *root = i == 7 ? sz_view_ellipsis(sz_view_max_lines(2, text)) : text;
    SkSurface *actual = sk_surface_make_raster_n32_premul(width, 96);
    SkSurface *expected = sk_surface_make_raster_n32_premul(width, 96);
    assert(actual && expected);
    assert(sz_view_paint(root, sk_surface_get_canvas(actual), width, 96, &theme));
    assert(fabsf(sz_view_frame(root).h - 60.f) < .001f);
    assert(!strcmp(sz_view_a11y_label(text), source));
    SkCanvas *canvas = sk_surface_get_canvas(expected);
    sk_canvas_clear(canvas, sk_color_argb(0));
    SkPaint *paint = sk_paint_new();
    sk_paint_set_text_size(paint, 24); sk_paint_set_color(paint, sk_color_argb(theme.foreground));
    sk_canvas_draw_span(canvas, sk_text_range(reference, strlen(reference)), 2, 56, paint, 0);
    const uint8_t *a = sk_surface_peek_pixels(actual, NULL), *b = sk_surface_peek_pixels(expected, NULL);
    assert(!memcmp(a + width * 30 * 4, b + width * 30 * 4, (size_t)width * 66 * 4));
    sk_paint_delete(paint); sk_surface_unref(actual); sk_surface_unref(expected);
    sz_view_free(root);
  }
}

static void test_styled_shaping_context(void) {
  uint64_t before_bytes, before_count, end_bytes, end_count;
  sz_alloc_stats(&before_bytes, &before_count);
  const char *left[] = {"A", "f", "ل", "e", "क्"};
  const char *right[] = {"V", "i", "ا", "́", "षि"};
  SzText *(*styles[])(const SzText *) = {NULL, sz_text_underline, sz_text_bold, sz_text_italic, sz_text_monospace};
  for (int style = 0; style < 5; style++) for (int i = 0; i < 5; i++) {
    SzText *a = test_text_span(left[i]), *b = test_text_span(right[i]);
    SzText *colored = sz_text_color(0xff165ca0, b);
    SzText *split = test_text_pair(a, colored);
    SzText *plain = test_text_span(sz_text_source(split));
    if (styles[style]) {
      SzText *next_split = styles[style](split), *next_plain = styles[style](plain);
      sz_release(split); sz_release(plain);
      split = next_split; plain = next_plain;
    }
    SzTextMetrics whole = sz_text_layout(plain, 32, 1, 6, 0, 0, 0, NULL, NULL);
    SzTextMetrics parts = sz_text_layout(split, 32, 1, 6, 0, 0, 0, NULL, NULL);
    assert(fabsf(whole.width - parts.width) < .01f);
    assert(fabsf(whole.height - parts.height) < .01f);
    parts = sz_text_layout(split, 32, 1, 6, whole.width + .01f, 0, 0, NULL, NULL);
    assert(parts.lines == 1);
    SzTheme theme = *sz_theme_default();
    theme.background = 0; theme.font_px = 32; theme.foreground = 0xff165ca0;
    SzView *plain_view = sz_view_rich_text(plain), *split_view = sz_view_rich_text(split);
    int gpu = sk_gpu_available();
    SkSurface *whole_surface = gpu ? sk_surface_make_gpu_n32_premul(256, 96) : sk_surface_make_raster_n32_premul(256, 96);
    SkSurface *split_surface = gpu ? sk_surface_make_gpu_n32_premul(256, 96) : sk_surface_make_raster_n32_premul(256, 96);
    assert(whole_surface && split_surface);
    assert(sz_view_paint(plain_view, sk_surface_get_canvas(whole_surface), 256, 96, &theme));
    assert(sz_view_paint(split_view, sk_surface_get_canvas(split_surface), 256, 96, &theme));
    const uint8_t *whole_pixels = sk_surface_peek_pixels(whole_surface, NULL);
    const uint8_t *split_pixels = sk_surface_peek_pixels(split_surface, NULL);
    int ink = 0;
    for (int p = 0; p < 256 * 96; p++) {
      if (whole_pixels[p * 4 + 3] != split_pixels[p * 4 + 3]) fprintf(stderr, "style %d case %d pixel %d,%d alpha %u != %u\n", style, i, p % 256, p / 256, whole_pixels[p * 4 + 3], split_pixels[p * 4 + 3]);
      assert(whole_pixels[p * 4 + 3] == split_pixels[p * 4 + 3]);
      ink += whole_pixels[p * 4 + 3] > 0;
    }
    assert(ink > 0);
    assert(!memcmp(whole_pixels, split_pixels, 256 * 96 * 4));
    if (i != 3) {
      theme.foreground = 0xffbd3a2a;
      assert(sz_view_paint(split_view, sk_surface_get_canvas(split_surface), 256, 96, &theme));
      split_pixels = sk_surface_peek_pixels(split_surface, NULL);
      int red = 0, blue = 0;
      for (int p = 0; p < 256 * 96; p++) {
        const uint8_t *pixel = split_pixels + p * 4;
        red += pixel[0] > pixel[1] && pixel[0] > pixel[2];
        blue += pixel[2] > pixel[0] && pixel[2] > pixel[1];
      }
      assert(red > 0 && blue > 0);
    }
    sk_surface_unref(whole_surface); sk_surface_unref(split_surface);
    sz_view_free(plain_view); sz_view_free(split_view);
    sz_release(a); sz_release(b); sz_release(colored); sz_release(split); sz_release(plain);
  }
  sz_alloc_stats(&end_bytes, &end_count);
  assert(before_bytes == end_bytes && before_count == end_count);
}

static void test_styled_text_wrap(void) {
  SzText *a = test_text_span("hel");
  SzText *b = test_text_span("lo world");
  SzText *bold = sz_text_bold(a);
  SzText *text = test_text_pair(bold, b);
  float width = sk_font_measure_span(sk_text_range("hel", 3), 14, SK_TEXT_BOLD) +
                sk_font_measure_span(sk_text_range("lo", 2), 14, 0);
  width = fmaxf(width, sk_font_measure_span(sk_text_range("world", 5), 14, 0)) + .01f;
  TextProbe probe = {0};
  SzTextMetrics metrics = sz_text_layout(text, 14, 1, 6, width, 0, 0, text_probe, &probe);
  assert(metrics.lines == 2 && probe.count == 3);
  assert(probe.starts[0] == 0 && probe.ends[0] == 3);
  assert(probe.starts[1] == 3 && probe.ends[1] == 5);
  assert(probe.starts[2] == 6 && probe.ends[2] == 11);
  assert(probe.baseline[0] == probe.baseline[1]);
  assert(probe.baseline[2] == probe.baseline[0] + 20.f);
  assert(metrics.width <= width && metrics.height == 40.f);
  probe = (TextProbe){0};
  metrics = sz_text_layout(text, 14, 1, 6, width, 1, 1, text_probe, &probe);
  assert(metrics.lines == 1 && metrics.truncated && probe.dots == 1);
  assert(metrics.width <= width + .01f);
  sz_release(a); sz_release(b); sz_release(bold); sz_release(text);

  a = test_text_span("word ");
  bold = sz_text_bold(a);
  b = test_text_span("Z");
  SzText *big = sz_text_size(40, b);
  SzText *part = test_text_pair(bold, big);
  SzText *tail = test_text_span(" tail");
  text = test_text_pair(part, tail);
  width = sk_font_measure_span(sk_text_range("word ", 5), 14, SK_TEXT_BOLD) +
          sk_font_measure_span(sk_text_range("Z", 1), 40, 0) + sk_font_measure_span(sk_text_range(" ", 1), 14, 0) + .01f;
  probe = (TextProbe){0};
  metrics = sz_text_layout(text, 14, 1, 6, width, 1, 1, text_probe, &probe);
  assert(probe.count == 2 && probe.dots == 1);
  assert(probe.fonts[1] == probe.fonts[0] && probe.styles[1] == probe.styles[0]);
  sz_release(a); sz_release(bold); sz_release(b); sz_release(big);
  sz_release(part); sz_release(tail); sz_release(text);

  text = test_text_span("λ🙂");
  width = sk_font_measure_span(sk_text_range("λ", 2), 14, 0) + .01f;
  probe = (TextProbe){0};
  metrics = sz_text_layout(text, 14, 1, 6, width, 0, 0, text_probe, &probe);
  assert(metrics.lines == 2 && probe.count == 2);
  assert(probe.starts[0] == 0 && probe.ends[0] == 2);
  assert(probe.starts[1] == 2 && probe.ends[1] == 6);
  sz_release(text);
}

static void test_styled_text_sizes(void) {
  SzText *a = test_text_span("Large");
  SzText *large = sz_text_size(30, a);
  SzText *b = test_text_span(" small");
  SzText *text = test_text_pair(large, b);
  TextProbe probe = {0};
  SzTextMetrics metrics = sz_text_layout(text, 14, 1, 6, 1000, 0, 0, text_probe, &probe);
  assert(metrics.lines == 1 && metrics.height >= 36.f && probe.count == 2);
  assert(probe.fonts[0] == 30.f && probe.fonts[1] == 14.f);
  assert(probe.baseline[0] == 32.f && probe.baseline[1] == 32.f);
  float measured = sk_font_measure_span(sk_text_range("Large", 5), 30, 0) + sk_font_measure_span(sk_text_range(" small", 6), 14, 0);
  assert(fabsf(metrics.width - measured) < .01f);
  probe = (TextProbe){0};
  metrics = sz_text_layout(text, 28, 2, 12, 2000, 0, 0, text_probe, &probe);
  assert(metrics.height >= 72.f && probe.baseline[0] == 64.f);
  measured = sk_font_measure_span(sk_text_range("Large", 5), 60, 0) + sk_font_measure_span(sk_text_range(" small", 6), 28, 0);
  assert(fabsf(metrics.width - measured) < .01f);
  SzView *view = sz_view_rich_text(text);
  sz_release(text);
  sz_view_layout(view, 1000, 100, sz_theme_default());
  float ascent, descent;
  sk_font_metrics(30, 0, &ascent, &descent);
  assert(sz_view_frame(view).h >= 32.f + descent);
  assert(!strcmp(sz_view_a11y_label(view), "Large small"));
  sz_view_free(view);
  sz_release(a); sz_release(b); sz_release(large);
  a = test_text_span("One\n\nTwo\n");
  text = sz_text_size(24, a);
  metrics = sz_text_layout(text, 14, 1, 6, 1000, 0, 0, NULL, NULL);
  assert(metrics.lines == 4 && metrics.height >= 120.f);
  sz_release(a); sz_release(text);
  a = test_text_span("gy");
  large = sz_text_size(200, a);
  text = sz_text_underline(large);
  probe = (TextProbe){0};
  metrics = sz_text_layout(text, 14, 1, 6, 1000, 0, 0, text_probe, &probe);
  assert(probe.count == 1);
  assert(metrics.height >= probe.baseline[0] + 32.5f);
  sz_release(a); sz_release(large); sz_release(text);
}

static void record_text_action(SzView *view, void *env) {
  sz_signal_int_set(env, sz_view_text_action_id(view));
}

static void test_inline_text_actions(void) {
  SzText *prefix = test_text_span("Before ");
  SzText *first = test_text_span("wrapped ");
  SzText *second = test_text_span("large ");
  SzText *large = sz_text_size(22, second);
  SzText *joined = test_text_pair(first, large);
  SzText *action = sz_text_action(-7, joined);
  SzText *gap = test_text_span(" after ");
  SzText *end = test_text_span("End");
  SzText *last = sz_text_action(INT64_MAX, end);
  SzText *a = test_text_pair(prefix, action);
  SzText *b = test_text_pair(a, gap);
  SzText *c = test_text_pair(b, action);
  SzText *text = test_text_pair(c, last);
  sz_release(a); sz_release(b); sz_release(c);
  assert(text->actions == 3 && action->actions == 1);
  assert(action->runs[0].action == 1 && action->runs[1].action == 1);
  assert(text->runs[1].action == 1 && text->runs[4].action == 2);
  SzSignalInt *picked = sz_signal_int(0);
  SzView *root = sz_view_action_text(text, record_text_action, picked);
  sz_view_layout(root, 160, 800, sz_theme_default());
  SzView *targets[4];
  assert(sz_view_collect_tap_targets(root, targets, 4) == 3);
  assert(targets[0] != targets[1] && sz_view_text_action_id(targets[2]) == INT64_MAX);
  SzRect box = sz_view_control_frame(targets[0]);
  SzRect bounds = sz_view_frame(targets[0]);
  assert(box.h < bounds.h && box.x > bounds.x);
  assert(sz_view_hit_test(root, box.x + box.w / 2, box.y + box.h / 2) == targets[0]);
  assert(sz_view_handle_tap(root, box.x + box.w / 2, box.y + box.h / 2));
  assert(sz_signal_int_get(picked) == -7);
  assert(!sz_view_hit_test(root, bounds.x + 1, box.y + box.h / 2));
  assert(!sz_view_handle_tap(root, bounds.x + 1, box.y + box.h / 2));
  assert(sz_view_focus_next(root, 0, 0) && sz_view_focused(root) == targets[1]);
  assert(sz_view_focus_next(root, 0, 0) && sz_view_focused(root) == targets[2]);
  sz_view_handle_key(root, "Space", "", 0);
  assert(sz_signal_int_get(picked) == INT64_MAX);
  assert(sz_view_focus_next(root, 1, 0) && sz_view_focused(root) == targets[1]);
  sz_view_handle_key(root, "Enter", "", 0);
  assert(sz_signal_int_get(picked) == -7);
  sz_view_free(root);
  root = sz_view_rich_text(text);
  sz_view_layout(root, 160, 800, sz_theme_default());
  assert(sz_view_collect_tap_targets(root, targets, 4) == 0);
  sz_view_free(root);

  SzText *long_word = test_text_span("abcdefghijklmno");
  SzText *long_action = sz_text_action(INT64_MIN, long_word);
  root = sz_view_ellipsis(sz_view_max_lines(1, sz_view_action_text(long_action, record_text_action, picked)));
  sz_view_layout(root, 70, 200, sz_theme_default());
  assert(sz_view_collect_tap_targets(root, targets, 4) == 1);
  box = sz_view_control_frame(targets[0]);
  assert(sz_view_handle_tap(root, box.x + box.w / 2, box.y + box.h / 2));
  assert(sz_signal_int_get(picked) == INT64_MIN);
  assert(!sz_view_hit_test(root, box.x + box.w + 1, box.y + box.h / 2));
  sz_view_free(root);
  SzText *truncated = test_text_pair(long_word, last);
  root = sz_view_ellipsis(sz_view_max_lines(1, sz_view_action_text(truncated, record_text_action, picked)));
  sz_view_layout(root, 1000, 200, sz_theme_default());
  assert(sz_view_focus_next(root, 0, 0));
  sz_view_layout(root, 60, 200, sz_theme_default());
  assert(sz_view_collect_tap_targets(root, targets, 4) == 0 && !sz_view_focused(root));
  sz_view_free(root);
  sz_release(truncated);
  sz_release(long_action); sz_release(long_word);
  SzText *unicode = test_text_span("café 😀");
  SzText *marked = sz_text_action(42, unicode);
  SzText *overridden = sz_text_action(17, marked);
  assert(marked->actions == 1 && overridden->actions == 1);
  assert(marked->runs[0].key == 42 && overridden->runs[0].key == 17);
  SzString *plain = sz_text_plain(overridden);
  assert(!strcmp(sz_string_cstr(plain), "café 😀"));
  sz_release(plain);
  SzText *many = test_text_span("");
  for (int i = 0; i < 100; i++) {
    SzText *next = test_text_pair(many, marked);
    sz_release(many); many = next;
  }
  root = sz_view_action_text(many, record_text_action, picked);
  SzTheme scaled = *sz_theme_default();
  scaled.px_scale = 2; scaled.font_px *= 2; scaled.line_gap *= 2;
  sz_view_layout(root, 2000, 2000, &scaled);
  assert(sz_view_focus_next(root, 0, 1));
  uintptr_t first_id = sz_view_id(sz_view_focused(root));
  for (int i = 1; i < 100; i++) assert(sz_view_focus_next(root, 0, 0));
  assert(sz_view_id(sz_view_focused(root)) != first_id);
  sz_view_handle_key(root, "Enter", "", 0);
  assert(sz_signal_int_get(picked) == 42);
  assert(sz_view_focus_next(root, 0, 1) && sz_view_id(sz_view_focused(root)) == first_id);
  sz_view_free(root); sz_release(many);
  sz_release(overridden); sz_release(marked); sz_release(unicode);
  sz_signal_int_free(picked);
  sz_release(text); sz_release(last); sz_release(end);
  sz_release(gap); sz_release(action); sz_release(joined);
  sz_release(large); sz_release(second); sz_release(first); sz_release(prefix);
}

static void test_styled_text_long_paint(void) {
  char source[601];
  memset(source, 'M', sizeof(source) - 1); source[600] = 0;
  SzText *text = test_text_span(source);
  SzText *red = sz_text_color(0xffff0000, text);
  SzText *bold = sz_text_bold(red);
  SzView *view = sz_view_rich_text(bold);
  sz_release(text); sz_release(red); sz_release(bold);
  SkSurface *surface = sk_surface_make_raster_n32_premul(10000, 40);
  assert(sz_view_paint(view, sk_surface_get_canvas(surface), 10000, 40, sz_theme_default()));
  const uint8_t *pixels = sk_surface_peek_pixels(surface, NULL);
  int ink = 0;
  for (int y = 0; y < 30; y++) for (int x = 5000; x < 10000; x++) {
    const uint8_t *p = pixels + ((size_t)y * 10000 + x) * 4;
    ink += p[0] > 200 && p[1] < 80 && p[2] < 80;
  }
  assert(ink > 0);
  sk_surface_unref(surface);
  sz_view_free(view);
}

static void test_styled_text_clip_and_theme(void) {
  SzText *source = test_text_span("MMMMMM");
  SzText *sized = sz_text_size(24, source);
  SzText *red = sz_text_color(0xffff0000, sized);
  SzText *styled = sz_text_italic(red);
  SzView *view = sz_view_rich_text(styled);
  SzView *root = sz_view_opacity(50, sz_view_clip(sz_view_sized(64, 20, view)));
  SzTheme theme = *sz_theme_default();
  theme.background = 0;
  SkSurface *surface = sk_surface_make_raster_n32_premul(100, 50);
  assert(sz_view_paint(root, sk_surface_get_canvas(surface), 100, 50, &theme));
  const uint8_t *pixels = sk_surface_peek_pixels(surface, NULL);
  int ink = 0;
  for (int y = 0; y < 50; y++) for (int x = 0; x < 100; x++) {
    const uint8_t *p = pixels + (y * 100 + x) * 4;
    if (x >= 64 || y >= 20) assert(p[3] == 0);
    assert(p[3] <= 128);
    ink += p[3] > 100;
  }
  assert(ink > 0);
  sk_surface_unref(surface);
  sz_view_free(root);
  sz_release(source); sz_release(sized); sz_release(red); sz_release(styled);

  source = test_text_span("theme");
  view = sz_view_rich_text(source);
  SzTheme *large = sz_theme_metrics(NULL, 22, 10, 16, 12, 52);
  root = sz_view_theme(large, view);
  sz_release(large); sz_release(source);
  sz_view_layout(root, 640, 480, sz_theme_default());
  assert(sz_view_frame(view).h == 32.f);
  sz_view_free(root);
}

int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--paragraph-direction")) {
    test_wrapped_paragraph_direction();
    test_plain_paragraph_direction();
    puts("paragraph direction tests ok");
    return 0;
  }
  if (argc == 2 && (!strcmp(argv[1], "--styled-shaping") || !strcmp(argv[1], "--styled-shaping-gpu"))) {
    if (!strcmp(argv[1], "--styled-shaping-gpu")) assert(sk_gpu_available());
    test_styled_shaping_context();
    printf("styled shaping tests ok (%s)\n", sk_gpu_available() ? "OpenGL" : "raster");
    return 0;
  }

  if (argc == 2 && !strcmp(argv[1], "--opacity")) {
    test_opacity_transition_frames();
    test_opacity_session_pause_and_reduction();
    test_opacity_recorded_pumps();
    test_opacity_utf8_labels();
    puts("opacity tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--select")) {
    test_select_keyboard_window();
    test_select_reactive_options();
    test_select_pointer_identity();
    test_select_scope_and_journal();
    puts("select tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--shaping")) {
    test_shaped_field_geometry();
    test_shaped_text_wrapping();
  test_wrapped_paragraph_direction();
    test_fallback_text_layout();
    test_styled_shaping_context();
    test_styled_text_wrap();
    test_styled_text_sizes();
    test_styled_text_long_paint();
    test_inline_text_actions();
    test_tooltip_focus_and_hover();
    puts("text shaping tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--tooltips")) {
    test_tooltip_focus_and_hover();
    test_tooltip_scope_and_geometry();
    test_tooltip_clipping_and_pointer();
    test_tooltip_modal_escape();
    test_tooltip_paint_hover();
    puts("tooltip tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--inline-actions")) {
    test_inline_text_actions();
    test_styled_text_values();
    test_styled_shaping_context();
    test_styled_text_wrap();
    test_styled_text_sizes();
    puts("inline text action tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--disclosure")) {
    test_expansion_body_and_focus();
    test_expansion_geometry_and_identity();
    test_expansion_tile_sizes_collapsed();
    test_expansion_tile_sizes_expanded();
    test_expansion_tile_a11y();
    test_expansion_tile_tap_toggles();
    test_expansion_tile_child_tap();
    test_expansion_tile_paint();
    test_expansion_tile_in_taps_dump();
    puts("disclosure tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--segmented")) {
    test_segmented_keyboard();
    test_segmented_sizes();
    test_segmented_a11y();
    test_segmented_tap();
    test_segmented_hit_test();
    test_segmented_paint();
    test_control_tap_dumps();
    puts("segmented tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--split")) {
    test_split_keyboard();
    test_view_focus_split_overlay();
    puts("split tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--range-slider")) {
    test_slider_ranges();
    test_slider_pointer_focus_and_removal();
    test_slider_a11y();
    test_slider_tap_sets_from_x();
    puts("range slider tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--modal-pointer")) {
    test_modal_pointer_surface();
    test_modal_scroll_boundary();
    test_modal_pointer_capture();
    puts("modal pointer tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--modal")) {
    test_modal_keyboard_sync();
    test_modal_lifecycle();
    test_view_focus_group_keys();
    puts("modal tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--styled-text")) {
    test_styled_text_values();
    test_styled_shaping_context();
    test_styled_text_wrap();
    test_styled_text_sizes();
    test_styled_text_long_paint();
    test_styled_text_clip_and_theme();
    puts("styled text tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--metrics")) {
    test_theme_metrics_scope();
    test_theme_metrics_editor();
    puts("theme metrics tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--composite-tabs")) {
    test_composite_tab_capacity();
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--composite")) {
    test_composite_modal_radios();
    test_composite_hidden_radios();
    test_composite_duplicate_focus();
    test_composite_named_radios();
    test_composite_panel_stop();
    test_composite_radio_tab_stop();
    test_composite_tab_capacity();
    puts("composite tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--measured-list")) {
    test_measured_list_window_and_identity();
    test_measured_list_height_index();
    test_measured_list_wrap_and_focus();
    puts("measured list tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--theme")) {
    test_theme_metrics_scope();
    test_theme_metrics_editor();
    test_color_theme_scope();
    puts("theme tests ok");
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--keyboard")) {
    test_modal_keyboard_sync();
    test_modal_lifecycle();
    test_keyboard_controls();
    test_keyboard_radio_group();
    test_keyboard_many_targets();
    test_keyboard_tab_list();
    test_keyboard_modal_text();
    test_keyboard_overlay_and_editor();
    puts("keyboard tests ok");
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--virtual-list") == 0) {
    test_signal_name_index();
    test_signal_scope_ownership();
    test_virtual_list_window_and_identity();
    test_virtual_list_duplicate_keys();
    test_virtual_list_scale_and_resize();
    test_virtual_list_pointer_removal();
    puts("virtual list tests ok");
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--mirror-session") == 0) {
    test_mirror_signal_session();
    return 0;
  }
  for (int mode = 0; mode < 2; mode++) {
    pid_t mirror = fork();
    assert(mirror >= 0);
    if (mirror == 0) {
      if (mode) setenv("SCUZZ_EVAL_MIRROR", "1", 1);
      else unsetenv("SCUZZ_EVAL_MIRROR");
      execl(argv[0], argv[0], "--mirror-session", (char *)NULL);
      _exit(127);
    }
    int mirror_status;
    assert(waitpid(mirror, &mirror_status, 0) == mirror);
    assert(WIFEXITED(mirror_status) && WEXITSTATUS(mirror_status) == 0);
  }
  test_control_labels_use_text();
  test_script_scroll_targets_outer_container();
  test_nested_scroll_wheel_bubbles();
  test_code_block_wheel_pans_x();
  test_narrow_button_labels_stay_inside();
  test_edit_paint_scale_and_clip();
  test_button_press_feedback();
  test_default_theme_contrast();
  test_session_snapshot();
  test_signals_layout_hit();
  test_replace_root_keeps_signals();
  test_watch_rebuild_keeps_signals();
  test_session_load_code();
  test_stamp_loads_reload_code();
  test_ui_run_rebuild();
  test_ui_run_rebuild_keepalive();
  test_ui_async_handlers();
  test_dump_json_schema();
  test_script_json_inject();
  test_script_tap_named_id();
  test_record_json();
  test_session_debug_dump();
  test_xy_hit_and_miss();
  test_record_live_not_script();
  test_record_live_scroll();
  test_studio_shaped_xy();
  test_session_inject_script();
  test_session_inject_grows_past_4k();
  test_session_inject_control();
  test_idle_pump_skips_paint();
  test_session_dump_now_needs_path();
  test_session_inject_scroll();
  test_session_inject_backspace();
  test_script_run_file_long_line();
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
  test_code_copy_and_heading();
  test_focus_revealed_after_resize();
  test_viewport_record_replay();
  test_session_inject_field_index();
  test_button_set_and_show_when();
  test_bitmap_images();
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
  test_container_child_list_ownership();
  test_virtual_list_window_and_identity();
  test_measured_list_window_and_identity();
  test_measured_list_height_index();
  test_measured_list_wrap_and_focus();
  test_virtual_list_duplicate_keys();
  test_virtual_list_scale_and_resize();
  test_virtual_list_pointer_removal();
  test_adaptive_width_and_input();
  test_adaptive_focus_and_shared_state();
  test_auto_grid_widths();
  test_auto_grid_visibility_and_input();
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
  test_expanded_weights();
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
  test_gap_paints_control_label_at_layout_gap();
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
  test_control_tap_dumps();
  test_checkbox_sizes();
  test_checkbox_a11y_off_on();
  test_a11y_dump_long_labels();
  test_checkbox_nonzero_is_on();
  test_checkbox_tap_toggles();
  test_checkbox_hit_test();
  test_checkbox_paint_off_on();
  test_selection_marks_without_color();
  test_control_labels_stay_in_bounds();
  test_tap_collect_tree_order();
  test_activate_offscreen_button();
  test_switch_sizes();
  test_switch_a11y_off_on();
  test_switch_nonzero_is_on();
  test_switch_tap_toggles();
  test_switch_hit_test();
  test_switch_paint_off_on();
  test_chip_sizes();
  test_chip_a11y_off_on();
  test_chip_nonzero_is_on();
  test_chip_tap_toggles();
  test_chip_hit_test();
  test_chip_paint_off_on();
  test_selection_tabs_without_color();
  test_list_tile_sizes();
  test_list_tile_unbounded_width();
  test_list_tile_a11y();
  test_list_tile_not_tap_target();
  test_list_tile_trailing_tap();
  test_list_tile_paint();
  test_list_tile_trailing_in_taps_dump();
  test_badge_sizes();
  test_badge_ticket_counts_and_scale();
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
  test_expansion_body_and_focus();
  test_expansion_geometry_and_identity();
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
  test_switch_list_tile_sizes();
  test_switch_list_tile_a11y();
  test_switch_list_tile_tap();
  test_switch_list_tile_hit_test();
  test_switch_list_tile_paint();
  test_radio_list_tile_sizes();
  test_radio_list_tile_a11y();
  test_radio_list_tile_tap();
  test_radio_list_tile_group_exclusive();
  test_radio_list_tile_hit_test();
  test_radio_list_tile_paint();
  test_segmented_keyboard();
  test_segmented_sizes();
  test_segmented_a11y();
  test_segmented_tap();
  test_segmented_hit_test();
  test_segmented_paint();
  test_fab_sizes();
  test_fab_a11y();
  test_fab_tap();
  test_fab_hit_test();
  test_fab_paint();
  test_fab_tap_twice();
  test_fab_a11y_distinct();
  test_tooltip_focus_and_hover();
  test_tooltip_scope_and_geometry();
  test_tooltip_clipping_and_pointer();
  test_tooltip_modal_escape();
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
  test_radio_sizes();
  test_radio_a11y_off_on();
  test_radio_tap_writes_value();
  test_radio_group_exclusive();
  test_radio_hit_test();
  test_radio_paint_off_on();
  test_split_keyboard();
  test_slider_ranges();
  test_slider_pointer_focus_and_removal();
  test_slider_sizes();
  test_slider_a11y();
  test_slider_clamps_a11y();
  test_slider_tap_sets_from_x();
  test_slider_tap_clamps_edges();
  test_slider_hit_test();
  test_slider_paint_fill();
  test_slider_pointer_drag();
  test_replace_root_drops_pointer();
  test_slider_live_records_xy();
  test_progress_sizes();
  test_progress_unbounded_width();
  test_progress_a11y();
  test_progress_clamps_a11y();
  test_progress_not_tap_target();
  test_progress_paint_fill();
  test_progress_amount_without_accent();
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
  test_property_missing_signal_panics();
  test_runtime_env_parse();
  test_signal_list_get_survives_set();
  test_signal_list_record_dump();
  test_property_signal_str();
  test_signal_dump_escapes();
  test_signal_name_last_wins();
  test_signal_name_last_wins_per_kind();
  test_signal_name_unregistered_clears_nothing();
  test_generic_signal_chain();
  test_signal_map_source_free();
  test_signal_mirror_revision();
  test_signal_map_caches_unchanged_source();
  test_signal_list_elem_str_flag();
  test_signal_dump_long_name();
  test_signal_list_mixed_kinds();
  test_property_replay_str_list();
  test_text_field_edit();
  test_hidden_editor_marks();
  test_view_editor();
  test_view_editor_viewport();
  test_view_editor_undo_gutter();
  test_view_focus_split_overlay();
  test_view_focus_group_keys();
  test_index_book_navigation_and_resize();
  test_app_shell_and_tabs();
  test_tabs_navigate();
  test_index_book_long_index();
  test_docs_nav_widgets();
  test_app_chord_save();
  test_app_chord_palette();
  test_caret_metrics();
  test_alloc_pump_flat();
  test_alloc_counter_pump_flat();
  test_alloc_each_pump_flat();
  test_tap_env_retain_release();
  test_each_env_retain_release();
  test_each_orphan_views_freed();
  test_view_show();
  test_pump_motion();
  test_quiesce();
  test_signal_name_index();
  test_signal_scope_ownership();
  test_opacity_transition_frames();
  test_opacity_session_pause_and_reduction();
  test_opacity_recorded_pumps();
  test_opacity_utf8_labels();
  test_inline_text_actions();
  test_select_keyboard_window();
  test_select_reactive_options();
  test_select_pointer_identity();
  test_select_scope_and_journal();
  test_shaped_field_geometry();
  test_shaped_text_wrapping();
  test_wrapped_paragraph_direction();
  test_plain_paragraph_direction();
  test_fallback_text_layout();
  test_styled_text_values();
  test_styled_shaping_context();
  test_styled_text_wrap();
  test_styled_text_sizes();
  test_styled_text_long_paint();
  test_styled_text_clip_and_theme();
  test_theme_metrics_scope();
  test_theme_metrics_editor();
  test_color_theme_scope();
  test_composite_modal_radios();
  test_composite_hidden_radios();
  test_composite_duplicate_focus();
  test_composite_named_radios();
  test_composite_panel_stop();
  test_composite_radio_tab_stop();
  test_composite_tab_capacity();
  test_modal_pointer_surface();
  test_modal_scroll_boundary();
  test_modal_pointer_capture();
  test_modal_keyboard_sync();
  test_modal_lifecycle();
  test_keyboard_controls();
  test_keyboard_radio_group();
  test_keyboard_many_targets();
  test_keyboard_tab_list();
  test_keyboard_modal_text();
  test_keyboard_overlay_and_editor();
  puts("runtime ui tests ok");
  return 0;
}
