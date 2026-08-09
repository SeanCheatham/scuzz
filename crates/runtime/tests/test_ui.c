#define _POSIX_C_SOURCE 200112L
#include "scalui_ui.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void test_label_session(void) {
  SuUiConfig cfg;
  SuView *view;
  SuUiSession *session;
  SuInputEvent tap;
  const char *path_a = "/tmp/scalui_ui_a.png";
  const char *path_b = "/tmp/scalui_ui_b.png";
  const char *path_tap = "/tmp/scalui_ui_tap.png";
  uint8_t *bytes = NULL;
  size_t len = 0;

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SU_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;

  view = su_view_label("Hello", 0xFF142850u, 0xFFF0F0F0u);
  session = su_ui_mount(&cfg, view);
  assert(session);
  assert(su_ui_session_kind(session) == SU_UI_RUNTIME_HEADLESS);
  assert(su_ui_pump_sync(session));
  assert(su_ui_snapshot_png_sync(session, path_a));
  assert(su_ui_snapshot_png_bytes(session, &bytes, &len));
  assert(bytes && len > 8 && bytes[0] == 137);
  free(bytes);

  assert(su_ui_snapshot_png_sync(session, path_b));
  assert(files_equal(path_a, path_b));

  memset(&tap, 0, sizeof(tap));
  tap.kind = SU_INPUT_TAP;
  tap.x = 100;
  tap.y = 50;
  assert(su_ui_inject_sync(session, &tap));
  assert(su_ui_pump_sync(session));
  assert(su_ui_snapshot_png_sync(session, path_tap));
  assert(!files_equal(path_a, path_tap));

  su_ui_unmount(session);
  su_view_free(view);

  cfg.kind = SU_UI_RUNTIME_WINDOW;
  cfg.title = "test";
  view = su_view_label("Win", 0xFF142850u, 0xFFF0F0F0u);
  session = su_ui_mount(&cfg, view);
  assert(session);
  assert(su_ui_session_kind(session) == SU_UI_RUNTIME_WINDOW);
  assert(su_ui_pump_sync(session));
  assert(su_ui_snapshot_png_sync(session, path_b));
  su_ui_unmount(session);
  su_view_free(view);

  {
    SuIoResult r = su_io_unsafe_run(su_ui_run_headless_label("Demo", 160, 80));
    assert(r.ok);
  }

  remove(path_a);
  remove(path_b);
  remove(path_tap);
}

static void counter_tap(SuView *self, void *env) {
  SuSignalInt *count = (SuSignalInt *)env;
  (void)self;
  su_signal_int_set(count, su_signal_int_get(count) + 1);
}

static void test_signals_layout_hit(void) {
  SuUiConfig cfg;
  SuSignalInt *count;
  SuView *root, *btn;
  SuUiSession *session;
  SuView *hit;
  SuInputEvent tap;
  const SuTheme *theme = su_theme_default();

  count = su_signal_int(0);
  root = su_view_column();
  su_view_add_child(root, su_view_text_signal_int(count, "n="));
  btn = su_view_button("+", counter_tap, count);
  su_view_add_child(root, btn);

  su_view_layout(root, 200.f, 100.f, theme);
  assert(su_view_frame(root).w == 200.f);
  assert(su_view_frame(btn).h == theme->control_h);

  hit = su_view_hit_test(root, su_view_frame(btn).x + 4.f,
                         su_view_frame(btn).y + 4.f);
  assert(hit == btn);
  assert(su_view_hit_test(root, 199.f, 99.f) == NULL ||
         su_view_kind(su_view_hit_test(root, 199.f, 99.f)) != SU_VIEW_BUTTON);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SU_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;
  session = su_ui_mount(&cfg, root);
  assert(session);
  assert(su_ui_pump_sync(session));

  memset(&tap, 0, sizeof(tap));
  tap.kind = SU_INPUT_TAP;
  tap.x = su_view_frame(btn).x + 8.f;
  tap.y = su_view_frame(btn).y + 8.f;
  assert(su_ui_inject_sync(session, &tap));
  assert(su_signal_int_get(count) == 1);
  assert(su_ui_pump_sync(session));

  /* Bridge: post from "IO", apply on pump. */
  su_ui_bridge_post_int(session, count, 42);
  assert(su_signal_int_get(count) == 1);
  assert(su_ui_pump_sync(session));
  assert(su_signal_int_get(count) == 42);

  su_ui_unmount(session);
  su_view_free(root);
  su_signal_int_free(count);
}

static void test_widgets_and_demos(void) {
  SuView *col, *scroll, *list, *field, *img, *icon;
  SuSignalStr *draft;
  const SuTheme *theme = su_theme_default();
  SuIoResult r;
  const char *todo_path = "/tmp/scalui_todo_test.txt";

  draft = su_signal_str("hi");
  col = su_view_column();
  field = su_view_text_field(draft, "type");
  img = su_view_image(16, 16, 0xFF112233u, "i");
  icon = su_view_icon('X', 0xFF000000u);
  list = su_view_list();
  su_view_add_child(list, su_view_text("a"));
  scroll = su_view_scroll(list);
  su_view_add_child(col, field);
  su_view_add_child(col, img);
  su_view_add_child(col, icon);
  su_view_add_child(col, scroll);
  su_view_layout(col, 200.f, 160.f, theme);
  assert(su_view_kind(field) == SU_VIEW_TEXT_FIELD);
  assert(su_view_kind(scroll) == SU_VIEW_SCROLL);
  assert(su_view_frame(scroll).h > 0);
  su_view_free(col);
  su_signal_str_free(draft);

  r = su_io_unsafe_run(su_ui_run_counter(200, 120));
  assert(r.ok);

  remove(todo_path);
  setenv("SCALUI_TODO_PATH", todo_path, 1);
  unsetenv("SCALUI_UI_TAP");
  r = su_io_unsafe_run(su_ui_run_todo(240, 160));
  assert(r.ok);

  /* Seed + add + save via env scripting */
  setenv("SCALUI_UI_TAP", "1", 1);
  setenv("SCALUI_TODO_SEED", "eggs", 1);
  setenv("SCALUI_TODO_SAVE", "1", 1);
  r = su_io_unsafe_run(su_ui_run_todo(240, 160));
  assert(r.ok);
  {
    FILE *f = fopen(todo_path, "r");
    char buf[64] = {0};
    assert(f);
    assert(fgets(buf, sizeof buf, f));
    assert(strncmp(buf, "eggs", 4) == 0);
    fclose(f);
  }

  unsetenv("SCALUI_UI_TAP");
  unsetenv("SCALUI_TODO_SEED");
  unsetenv("SCALUI_TODO_SAVE");
  unsetenv("SCALUI_TODO_PATH");
  remove(todo_path);
}

int main(void) {
  test_label_session();
  test_signals_layout_hit();
  test_widgets_and_demos();
  puts("runtime ui tests ok");
  return 0;
}
