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

  cfg.kind = SU_UI_RUNTIME_MOBILE;
  cfg.title = "mobile";
  view = su_view_label("Mob", 0xFF142850u, 0xFFF0F0F0u);
  session = su_ui_mount(&cfg, view);
  assert(session);
  assert(su_ui_session_kind(session) == SU_UI_RUNTIME_MOBILE);
  assert(su_ui_pump_sync(session));
  assert(su_ui_snapshot_png_sync(session, path_b));
  su_ui_unmount(session);
  su_view_free(view);

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

typedef struct {
  SuSignalInt *sig;
  int64_t value;
} SetEnv;

static void set_tap(SuView *self, void *env) {
  SetEnv *e = (SetEnv *)env;
  (void)self;
  su_signal_int_set(e->sig, e->value);
}

static void test_button_set_and_show_when(void) {
  SuSignalInt *page;
  SuSignalInt *count;
  SuView *root, *home, *other, *set_btn, *inc_btn;
  SetEnv *set_env;
  const SuTheme *theme = su_theme_default();
  SuUiConfig cfg;
  SuUiSession *session;
  SuInputEvent tap;
  float hx, hy;

  page = su_signal_int(0);
  count = su_signal_int(0);
  root = su_view_column();
  set_env = (SetEnv *)su_alloc(sizeof(SetEnv));
  set_env->sig = page;
  set_env->value = 1;
  set_btn = su_lang_view_button(su_string_from_cstr("Other"), set_tap, set_env);
  su_view_add_child(root, set_btn);

  home = su_view_column();
  su_view_add_child(home, su_view_text("Home"));
  inc_btn = su_lang_view_button(su_string_from_cstr("+1"), counter_tap, count);
  su_view_add_child(home, inc_btn);
  su_view_add_child(root, su_view_show_when(page, 0, home));

  other = su_view_column();
  su_view_add_child(other, su_view_text("Other page"));
  su_view_add_child(root, su_view_show_when(page, 1, other));

  su_view_layout(root, 200.f, 160.f, theme);
  assert(su_view_frame(home).h > 0.f);
  assert(su_view_frame(other).h == 0.f);
  assert(su_view_hit_test(root, su_view_frame(inc_btn).x + 4.f,
                          su_view_frame(inc_btn).y + 4.f) == inc_btn);

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SU_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 160;
  cfg.scale = 1.0;
  session = su_ui_mount(&cfg, root);
  assert(session);
  assert(su_ui_pump_sync(session));

  hx = su_view_frame(set_btn).x + 8.f;
  hy = su_view_frame(set_btn).y + 8.f;
  memset(&tap, 0, sizeof(tap));
  tap.kind = SU_INPUT_TAP;
  tap.x = hx;
  tap.y = hy;
  assert(su_ui_inject_sync(session, &tap));
  assert(su_signal_int_get(page) == 1);
  assert(su_ui_pump_sync(session));

  su_view_layout(root, 200.f, 160.f, theme);
  assert(su_view_frame(home).h == 0.f);
  assert(su_view_frame(other).h > 0.f);
  assert(su_view_hit_test(root, su_view_frame(inc_btn).x + 4.f,
                          su_view_frame(inc_btn).y + 4.f) != inc_btn);

  su_ui_unmount(session);
  su_view_free(root);
  su_signal_int_free(page);
  su_signal_int_free(count);
}

static void test_widgets(void) {
  SuView *col, *scroll, *list, *field, *img, *icon;
  SuSignalStr *draft;
  const SuTheme *theme = su_theme_default();

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
}

static void test_mobile_pointer_scroll_lifecycle(void) {
  SuUiConfig cfg;
  SuSignalStr *draft;
  SuView *root, *scroll, *list, *field;
  SuUiSession *session;
  SuInputEvent ev;
  const SuTheme *theme = su_theme_default();
  float y0;

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SU_UI_RUNTIME_MOBILE;
  cfg.width = 200;
  cfg.height = 160;
  cfg.scale = 1.0;
  cfg.title = "mobile-test";

  draft = su_signal_str("");
  root = su_view_column();
  field = su_view_text_field(draft, "type");
  su_view_add_child(root, field);
  list = su_view_list();
  su_view_add_child(list, su_view_text("one"));
  su_view_add_child(list, su_view_text("two"));
  su_view_add_child(list, su_view_text("three"));
  su_view_add_child(list, su_view_text("four"));
  scroll = su_view_scroll(list);
  su_view_add_child(root, scroll);

  session = su_ui_mount(&cfg, root);
  assert(session);
  assert(su_ui_session_kind(session) == SU_UI_RUNTIME_MOBILE);
  assert(su_ui_session_lifecycle(session) == SU_LIFECYCLE_RESUME);
  assert(su_ui_pump_sync(session));

  /* Soft keyboard: tap TextField → keyboard visible. */
  su_view_layout(root, 200.f, 160.f, theme);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SU_INPUT_POINTER;
  ev.pointer_phase = SU_POINTER_DOWN;
  ev.x = su_view_frame(field).x + 4.f;
  ev.y = su_view_frame(field).y + 4.f;
  assert(su_ui_inject_sync(session, &ev));
  ev.pointer_phase = SU_POINTER_UP;
  assert(su_ui_inject_sync(session, &ev));
  assert(su_ui_session_keyboard_visible(session) == 1);
  assert(su_view_has_focused_text_field(root));

  memset(&ev, 0, sizeof(ev));
  ev.kind = SU_INPUT_TEXT;
  ev.text = "milk";
  assert(su_ui_inject_sync(session, &ev));
  assert(strcmp(su_signal_str_get(draft), "milk") == 0);

  /* Scroll gesture via POINTER move on the Scroll viewport. */
  su_view_layout(root, 200.f, 160.f, theme);
  y0 = su_view_scroll_y(scroll);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SU_INPUT_POINTER;
  ev.pointer_phase = SU_POINTER_DOWN;
  ev.x = su_view_frame(scroll).x + 8.f;
  ev.y = su_view_frame(scroll).y + 8.f;
  assert(su_ui_inject_sync(session, &ev));
  ev.pointer_phase = SU_POINTER_MOVE;
  ev.y = su_view_frame(scroll).y + 8.f - 20.f; /* finger up → content up */
  assert(su_ui_inject_sync(session, &ev));
  ev.pointer_phase = SU_POINTER_UP;
  assert(su_ui_inject_sync(session, &ev));
  assert(su_view_scroll_y(scroll) > y0);

  /* Direct scroll inject also works (Headless-scriptable). */
  memset(&ev, 0, sizeof(ev));
  ev.kind = SU_INPUT_SCROLL;
  ev.x = su_view_frame(scroll).x + 8.f;
  ev.y = su_view_frame(scroll).y + 8.f;
  ev.dy = 12.f;
  y0 = su_view_scroll_y(scroll);
  assert(su_ui_inject_sync(session, &ev));
  assert(su_view_scroll_y(scroll) == y0 + 12.f);

  /* Lifecycle pause hides keyboard; stop blocks further taps. */
  memset(&ev, 0, sizeof(ev));
  ev.kind = SU_INPUT_LIFECYCLE;
  ev.lifecycle = SU_LIFECYCLE_PAUSE;
  assert(su_ui_inject_sync(session, &ev));
  assert(su_ui_session_lifecycle(session) == SU_LIFECYCLE_PAUSE);
  assert(su_ui_session_keyboard_visible(session) == 0);

  ev.lifecycle = SU_LIFECYCLE_RESUME;
  assert(su_ui_inject_sync(session, &ev));
  assert(su_ui_pump_sync(session));

  ev.lifecycle = SU_LIFECYCLE_STOP;
  assert(su_ui_inject_sync(session, &ev));
  assert(!su_ui_pump_sync(session));

  su_ui_unmount(session);
  su_view_free(root);
  su_signal_str_free(draft);
}

static void test_a11y_and_anim(void) {
  SuView *btn;
  SuView *col;
  SuString *dump;
  SuAnimFloat *anim;
  SuUiConfig cfg;
  SuUiSession *session;

  btn = su_view_button("Go", NULL, NULL);
  assert(su_view_a11y_role(btn) == SU_A11Y_BUTTON);
  assert(strcmp(su_view_a11y_label(btn), "Go") == 0);
  col = su_view_column();
  su_view_add_child(col, btn);
  su_view_add_child(col, su_view_text("hi"));
  dump = su_view_a11y_dump(col);
  assert(strstr(su_string_cstr(dump), "button:Go") != NULL);
  assert(strstr(su_string_cstr(dump), "text:hi") != NULL);

  anim = su_anim_float(0.f, 10.f, 100);
  assert(!su_anim_done(anim));
  su_anim_tick(anim, 50);
  assert(su_anim_value(anim) > 4.f && su_anim_value(anim) < 6.f);
  su_anim_tick(anim, 50);
  assert(su_anim_done(anim));
  assert(su_anim_value(anim) == 10.f);

  /* Pump advances registered anims via Clock dt (fake clock). */
  su_testrt_clock_install(1000);
  {
    SuAnimFloat *a2 = su_anim_float(0.f, 1.f, 40);
    memset(&cfg, 0, sizeof(cfg));
    cfg.kind = SU_UI_RUNTIME_HEADLESS;
    cfg.width = 80;
    cfg.height = 40;
    session = su_ui_mount(&cfg, col);
    assert(session);
    assert(su_ui_pump_sync(session));
    su_testrt_clock_advance(40);
    assert(su_ui_pump_sync(session));
    assert(su_anim_done(a2));
    su_ui_unmount(session);
    su_anim_free(a2);
  }
  su_testrt_reset();
  su_anim_free(anim);
  su_view_free(col);
}

static void test_clear_and_set_texts(void) {
  SuView *list;
  SuString *dump;
  SuList *lines;

  list = su_view_list();
  su_view_add_child(list, su_view_text("old"));
  dump = su_view_a11y_dump(list);
  assert(strstr(su_string_cstr(dump), "text:old") != NULL);

  su_view_clear_children(list);
  dump = su_view_a11y_dump(list);
  assert(strstr(su_string_cstr(dump), "text:old") == NULL);

  lines = su_list_cons(su_string_from_cstr("milk"),
                       su_list_cons(su_string_from_cstr("eggs"), su_list_nil()));
  su_lang_view_set_texts(list, lines);
  dump = su_view_a11y_dump(list);
  assert(strstr(su_string_cstr(dump), "text:- milk") != NULL);
  assert(strstr(su_string_cstr(dump), "text:- eggs") != NULL);

  su_lang_view_clear_children(list);
  dump = su_view_a11y_dump(list);
  assert(strstr(su_string_cstr(dump), "text:- milk") == NULL);
  su_view_free(list);
}

static void test_view_each(void) {
  SuSignalList *items;
  SuView *list;
  const SuTheme *theme = su_theme_default();
  SuList *xs;

  xs = su_list_cons(su_string_from_cstr("milk"), su_list_nil());
  items = su_signal_list(xs);
  list = su_view_each(items);
  su_view_layout(list, 200.f, 120.f, theme);
  assert(strstr(su_string_cstr(su_view_a11y_dump(list)), "text:- milk") != NULL);

  xs = su_list_cons(su_string_from_cstr("eggs"), xs);
  su_signal_list_set(items, xs);
  su_view_layout(list, 200.f, 120.f, theme);
  assert(strstr(su_string_cstr(su_view_a11y_dump(list)), "text:- eggs") != NULL);
  assert(strstr(su_string_cstr(su_view_a11y_dump(list)), "text:- milk") != NULL);

  su_view_free(list);
  su_signal_list_free(items);
}

int main(void) {
  test_label_session();
  test_signals_layout_hit();
  test_button_set_and_show_when();
  test_widgets();
  test_mobile_pointer_scroll_lifecycle();
  test_a11y_and_anim();
  test_clear_and_set_texts();
  test_view_each();
  puts("runtime ui tests ok");
  return 0;
}
