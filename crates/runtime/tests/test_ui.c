#define _POSIX_C_SOURCE 200112L
#include "scuzz_ui.h"

#include <assert.h>
#include <math.h>
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
  SzUiConfig cfg;
  SzView *view;
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

  view = sz_view_label("Hello", 0xFF142850u, 0xFFF0F0F0u);
  session = sz_ui_mount(&cfg, view);
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
  tap.x = 100;
  tap.y = 50;
  assert(sz_ui_inject_sync(session, &tap));
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_snapshot_png_sync(session, path_tap));
  assert(!files_equal(path_a, path_tap));

  sz_ui_unmount(session);
  sz_view_free(view);

  cfg.kind = SZ_UI_RUNTIME_WINDOW;
  cfg.title = "test";
  view = sz_view_label("Win", 0xFF142850u, 0xFFF0F0F0u);
  session = sz_ui_mount(&cfg, view);
  assert(session);
  assert(sz_ui_session_kind(session) == SZ_UI_RUNTIME_WINDOW);
  assert(sz_ui_pump_sync(session));
  assert(sz_ui_snapshot_png_sync(session, path_b));
  sz_ui_unmount(session);
  sz_view_free(view);

  cfg.kind = SZ_UI_RUNTIME_MOBILE;
  cfg.title = "mobile";
  view = sz_view_label("Mob", 0xFF142850u, 0xFFF0F0F0u);
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

static void counter_tap(SzView *self, void *env) {
  SzSignalInt *count = (SzSignalInt *)env;
  (void)self;
  sz_signal_int_set(count, sz_signal_int_get(count) + 1);
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

  /* Scroll gesture via POINTER move on the Scroll viewport. */
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

static void test_a11y_and_anim(void) {
  SzView *btn;
  SzView *col;
  SzString *dump;
  SzAnimFloat *anim;
  SzUiConfig cfg;
  SzUiSession *session;

  btn = sz_view_button("Go", NULL, NULL);
  assert(sz_view_a11y_role(btn) == SZ_A11Y_BUTTON);
  assert(strcmp(sz_view_a11y_label(btn), "Go") == 0);
  col = sz_view_column();
  sz_view_add_child(col, btn);
  sz_view_add_child(col, sz_view_text("hi"));
  dump = sz_view_a11y_dump(col);
  assert(strstr(sz_string_cstr(dump), "button:Go") != NULL);
  assert(strstr(sz_string_cstr(dump), "text:hi") != NULL);

  anim = sz_anim_float(0.f, 10.f, 100);
  assert(!sz_anim_done(anim));
  sz_anim_tick(anim, 50);
  assert(sz_anim_value(anim) > 4.f && sz_anim_value(anim) < 6.f);
  sz_anim_tick(anim, 50);
  assert(sz_anim_done(anim));
  assert(sz_anim_value(anim) == 10.f);

  /* Pump advances registered anims via Clock dt (fake clock). */
  sz_testrt_clock_install(1000);
  {
    SzAnimFloat *a2 = sz_anim_float(0.f, 1.f, 40);
    memset(&cfg, 0, sizeof(cfg));
    cfg.kind = SZ_UI_RUNTIME_HEADLESS;
    cfg.width = 80;
    cfg.height = 40;
    session = sz_ui_mount(&cfg, col);
    assert(session);
    assert(sz_ui_pump_sync(session));
    sz_testrt_clock_advance(40);
    assert(sz_ui_pump_sync(session));
    assert(sz_anim_done(a2));
    sz_ui_unmount(session);
    sz_anim_free(a2);
  }
  sz_testrt_reset();
  sz_anim_free(anim);
  sz_view_free(col);
}

static void test_clear_and_set_texts(void) {
  SzView *list;
  SzString *dump;
  SzList *lines;

  list = sz_view_list();
  sz_view_add_child(list, sz_view_text("old"));
  dump = sz_view_a11y_dump(list);
  assert(strstr(sz_string_cstr(dump), "text:old") != NULL);

  sz_view_clear_children(list);
  dump = sz_view_a11y_dump(list);
  assert(strstr(sz_string_cstr(dump), "text:old") == NULL);

  lines = sz_list_cons(sz_string_from_cstr("milk"),
                       sz_list_cons(sz_string_from_cstr("eggs"), sz_list_nil()));
  sz_lang_view_set_texts(list, lines);
  dump = sz_view_a11y_dump(list);
  assert(strstr(sz_string_cstr(dump), "text:- milk") != NULL);
  assert(strstr(sz_string_cstr(dump), "text:- eggs") != NULL);

  sz_lang_view_clear_children(list);
  dump = sz_view_a11y_dump(list);
  assert(strstr(sz_string_cstr(dump), "text:- milk") == NULL);
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

  view = sz_view_label("alloc", 0xFF142850u, 0xFFF0F0F0u);
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
  /* live_count flat = no per-pump leak; live_bytes may grow a few bytes when
   * the mapped label gains digits (e.g. "9" → "10"), same allocation count. */
  assert(live_count == base_count);
  assert(live_bytes <= base_bytes + 32);
  assert(max_count <= base_count + 16);

  sz_ui_unmount(session);
  sz_view_free(root);
  sz_signal_str_free(label);
  sz_signal_int_free(count);
}

int main(void) {
  test_label_session();
  test_signals_layout_hit();
  test_button_set_and_show_when();
  test_widgets();
  test_expanded_column();
  test_expanded_row();
  test_center();
  test_stack();
  test_mobile_pointer_scroll_lifecycle();
  test_a11y_and_anim();
  test_clear_and_set_texts();
  test_view_each();
  test_text_field_edit();
  test_alloc_pump_flat();
  test_alloc_counter_pump_flat();
  puts("runtime ui tests ok");
  return 0;
}
