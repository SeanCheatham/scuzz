#include "scuzz_ui.h"

#include "scuzz_embedder.h"
#include "scuzz_mobile.h"
#include "sk_capi.h"

#include "rt_util.h"
#include "ui_script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

static int want_gpu_presenter(void) {
  const char *e = getenv("SCUZZ_SKIA");
  return e && strcmp(e, "gpu") == 0;
}

static SkSurface *make_session_surface(int pw, int ph) {
  SkSurface *s;
  if (!want_gpu_presenter())
    return sk_surface_make_raster_n32_premul(pw, ph);
  s = sk_surface_make_gpu_n32_premul(pw, ph);
  if (!s) {
#if defined(__APPLE__)
    fprintf(stderr, "missing OpenGL — install Xcode, then retry\n");
#else
    fprintf(stderr,
            "missing OpenGL — install mesa (libegl1-mesa-dev libgles2-mesa-dev "
            "libgl1-mesa-dri)\n");
#endif
  }
  return s;
}

/* Weak stubs — strong defs from embedder-desktop override when linked. */
__attribute__((weak)) int sz_embedder_available(void) { return 0; }
__attribute__((weak)) double sz_embedder_display_scale(void) { return 1.0; }
__attribute__((weak)) int sz_embedder_alive(void) { return 0; }
__attribute__((weak)) int sz_embedder_present(const char *title, int point_w,
                                              int point_h, int pixel_w,
                                              int pixel_h, const uint8_t *rgba,
                                              size_t nbytes) {
  (void)title;
  (void)point_w;
  (void)point_h;
  (void)pixel_w;
  (void)pixel_h;
  (void)rgba;
  (void)nbytes;
  return 0;
}
__attribute__((weak)) void sz_embedder_shutdown(void) {}
__attribute__((weak)) int sz_embedder_poll_event(SzInputEvent *out) {
  (void)out;
  return 0;
}

/* Weak stubs — strong defs from embedder-mobile override when linked. */
__attribute__((weak)) int sz_mobile_available(void) { return 0; }
__attribute__((weak)) int sz_mobile_present(const char *title, int point_w,
                                            int point_h, int pixel_w,
                                            int pixel_h, const uint8_t *rgba,
                                            size_t nbytes) {
  (void)title;
  (void)point_w;
  (void)point_h;
  (void)pixel_w;
  (void)pixel_h;
  (void)rgba;
  (void)nbytes;
  return 0;
}
__attribute__((weak)) void sz_mobile_shutdown(void) {}
__attribute__((weak)) void sz_mobile_set_keyboard(int visible) {
  (void)visible;
}
__attribute__((weak)) int sz_mobile_poll_event(SzInputEvent *out) {
  (void)out;
  return 0;
}
__attribute__((weak)) int sz_mobile_alive(void) { return 0; }

/* Implemented in view.c */
int sz_view_paint(SzView *root, SkCanvas *canvas, int width, int height,
                  const SzTheme *theme);
int sz_view_handle_tap(SzView *root, float x, float y);
int sz_view_handle_text(SzView *root, const char *text);
int sz_view_handle_text_edit(SzView *root, const char *text, int backspace);
int sz_view_handle_key(SzView *root, const char *key, const char *text,
                       int mods);

typedef enum {
  BRIDGE_INT = 1,
  BRIDGE_STR = 2
} BridgeKind;

typedef struct BridgeItem {
  BridgeKind kind;
  SzSignalInt *sig_int;
  SzSignalStr *sig_str;
  int64_t int_value;
  char *str_value;
  struct BridgeItem *next;
} BridgeItem;

struct SzUiSession {
  SzUiConfig cfg;
  SzView *root;
  SkSurface *surface;
  SkCanvas *canvas;
  int dirty;
  int owns_view;
  const SzTheme *theme;
  BridgeItem *bridge_head;
  BridgeItem *bridge_tail;
  SzLifecyclePhase lifecycle;
  int keyboard_visible;
  int pointer_down;
  float pointer_x;
  float pointer_y;
  float pointer_down_x;
  float pointer_down_y;
  SzView *pointer_scroll;
  SzView *pointer_slider;
  SzUiRebuildFn rebuild;
  void *rebuild_env;
  char *watch_path;
  char *watch_fp;
  char *debug_dump_path;
  char *inject_path;
  char *inject_fp;
  int inject_playing;
  char *record_path;
  int last_hit_seen;
  float last_hit_x;
  float last_hit_y;
  char *last_hit_desc;
  void *code_handle;
  void *code_stale;
  int code_gen;
  unsigned pumps;
};

static int runtime_kind_ok(SzUiRuntimeKind kind) {
  return kind == SZ_UI_RUNTIME_HEADLESS || kind == SZ_UI_RUNTIME_DESKTOP ||
         kind == SZ_UI_RUNTIME_MOBILE;
}

static void sync_keyboard(SzUiSession *session) {
  int want;
  if (!session || !session->root)
    return;
  want = sz_view_has_focused_text_field(session->root) ? 1 : 0;
  if (want == session->keyboard_visible)
    return;
  session->keyboard_visible = want;
  if (session->cfg.kind == SZ_UI_RUNTIME_MOBILE)
    sz_mobile_set_keyboard(want);
}

SzUiSession *sz_ui_mount(const SzUiConfig *cfg, SzView *root) {
  SzUiSession *s;
  int w, h, pw, ph;
  if (!cfg || !root)
    return NULL;
  if (cfg->width <= 0 || cfg->height <= 0)
    return NULL;
  if (!runtime_kind_ok(cfg->kind))
    return NULL;

  w = cfg->width;
  h = cfg->height;
  s = (SzUiSession *)sz_alloc_zero(sizeof(SzUiSession));
  s->cfg = *cfg;
  if (s->cfg.scale <= 0.0)
    s->cfg.scale = 1.0;
  /* Desktop: prefer OS backing scale so Retina text stays sharp. */
  if (s->cfg.kind == SZ_UI_RUNTIME_DESKTOP && sz_embedder_available()) {
    double ds = sz_embedder_display_scale();
    if (ds > s->cfg.scale)
      s->cfg.scale = ds;
  }
  s->root = root;
  s->owns_view = 0;
  s->theme = sz_theme_default();
  s->lifecycle = SZ_LIFECYCLE_RESUME;
  pw = (int)(w * s->cfg.scale + 0.5);
  ph = (int)(h * s->cfg.scale + 0.5);
  if (pw < 1)
    pw = 1;
  if (ph < 1)
    ph = 1;
  s->surface = make_session_surface(pw, ph);
  if (!s->surface) {
    sz_free(s);
    return NULL;
  }
  s->canvas = sk_surface_get_canvas(s->surface);
  s->dirty = 1;
  if (cfg->kind == SZ_UI_RUNTIME_DESKTOP) {
    if (sz_embedder_available()) {
      fprintf(stderr,
              "scuzz: UiRuntime.Desktop mounted (desktop embedder, scale=%.2f)\n",
              s->cfg.scale);
    } else {
      fprintf(stderr,
              "scuzz: UiRuntime.Desktop mounted (offscreen; no desktop embedder)\n");
    }
  } else if (cfg->kind == SZ_UI_RUNTIME_MOBILE) {
    if (sz_mobile_available()) {
      fprintf(stderr, "scuzz: UiRuntime.Mobile mounted (mobile embedder)\n");
    } else {
      fprintf(stderr,
              "scuzz: UiRuntime.Mobile mounted (offscreen; no mobile shell)\n");
    }
  }
  return s;
}

void sz_ui_session_take_root(SzUiSession *session) {
  if (session)
    session->owns_view = 1;
}

int sz_ui_session_replace_root(SzUiSession *session, SzView *root) {
  if (!session || !root)
    return 0;
  if (session->owns_view)
    sz_view_free(session->root);
  session->root = root;
  session->dirty = 1;
  session->keyboard_visible = 0;
  return 1;
}

enum { SZ_UI_STAMP_CAP = 4096 };

static char *stamp_snapshot(const char *path) {
  FILE *f;
  char buf[SZ_UI_STAMP_CAP];
  size_t n;
  if (!path)
    return sz_strdup("");
  f = fopen(path, "rb");
  if (!f)
    return sz_strdup("");
  n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';
  return sz_strdup(buf);
}

static int stamp_changed(SzUiSession *session) {
  char *now;
  int changed;
  if (!session || !session->watch_path)
    return 0;
  now = stamp_snapshot(session->watch_path);
  changed = !session->watch_fp || strcmp(session->watch_fp, now) != 0;
  if (changed) {
    sz_free(session->watch_fp);
    session->watch_fp = now;
  } else {
    sz_free(now);
  }
  return changed;
}

void sz_ui_session_set_rebuild(SzUiSession *session, SzUiRebuildFn fn,
                               void *env) {
  if (!session)
    return;
  session->rebuild = fn;
  sz_release(session->rebuild_env);
  sz_retain(env);
  session->rebuild_env = env;
}

int sz_ui_session_watch(SzUiSession *session, const char *path) {
  if (!session || !path || !path[0])
    return 0;
  sz_free(session->watch_path);
  sz_free(session->watch_fp);
  session->watch_path = sz_strdup(path);
  session->watch_fp = stamp_snapshot(path);
  return 1;
}

int sz_ui_session_set_debug_dump(SzUiSession *session, const char *path) {
  char panic_path[1024];
  if (!session || !path || !path[0])
    return 0;
  sz_free(session->debug_dump_path);
  session->debug_dump_path = sz_strdup(path);
  snprintf(panic_path, sizeof panic_path, "%s.panic", path);
  sz_alloc_set_panic_dump(panic_path);
  sz_alloc_mark();
  return 1;
}

int sz_ui_session_set_inject(SzUiSession *session, const char *path) {
  if (!session || !path || !path[0])
    return 0;
  sz_free(session->inject_path);
  sz_free(session->inject_fp);
  session->inject_path = sz_strdup(path);
  session->inject_fp = stamp_snapshot(path);
  return 1;
}

int sz_ui_session_set_record(SzUiSession *session, const char *path) {
  FILE *f;
  if (!session || !path || !path[0])
    return 0;
  sz_free(session->record_path);
  session->record_path = sz_strdup(path);
  /* Truncate so each process is one session. */
  f = fopen(path, "w");
  if (!f)
    return 0;
  fclose(f);
  return 1;
}

static void fputs_dump_label(FILE *f, const char *label) {
  const char *p;
  if (!label)
    return;
  for (p = label; *p; p++)
    fputc((*p == '\n' || *p == '\r') ? ' ' : *p, f);
}

static void fputs_dump_quoted(FILE *f, const char *s) {
  const char *p;
  fputc('"', f);
  if (s) {
    for (p = s; *p; p++)
      fputc((*p == '\n' || *p == '\r' || *p == '"') ? ' ' : *p, f);
  }
  fputc('"', f);
}

static const char *runtime_kind_name(SzUiRuntimeKind kind) {
  switch (kind) {
  case SZ_UI_RUNTIME_HEADLESS:
    return "headless";
  case SZ_UI_RUNTIME_DESKTOP:
    return "desktop";
  case SZ_UI_RUNTIME_MOBILE:
    return "mobile";
  default:
    return "unknown";
  }
}

static const char *lifecycle_name(SzLifecyclePhase phase) {
  switch (phase) {
  case SZ_LIFECYCLE_RESUME:
    return "resume";
  case SZ_LIFECYCLE_PAUSE:
    return "pause";
  case SZ_LIFECYCLE_STOP:
    return "stop";
  default:
    return "unknown";
  }
}

int sz_ui_session_write_dump(SzUiSession *session, const char *path) {
  FILE *f;
  SzString *signals;
  SzString *views;
  SzView *buttons[64];
  SzView *fields[64];
  SzView *scrolls[64];
  SzView *field_target;
  int n_buttons, n_fields, n_scrolls, i;
  if (!path || !path[0])
    return 0;
  f = fopen(path, "w");
  if (!f)
    return 0;
  signals = sz_signal_dump();
  views = (session && session->root) ? sz_view_a11y_dump(session->root)
                                     : sz_string_from_cstr("");
  fprintf(f, "[signals]\n%s\n[views]\n%s\n[taps]\n", sz_string_cstr(signals),
          sz_string_cstr(views));
  n_buttons = sz_ui_collect_buttons(session, buttons, 64);
  for (i = 0; i < n_buttons; i++) {
    SzRect fr = sz_view_frame(buttons[i]);
    fprintf(f, "%d ", i);
    fputs_dump_label(f, sz_view_a11y_label(buttons[i]));
    fprintf(f, " %.0f,%.0f %.0fx%.0f\n", fr.x, fr.y, fr.w, fr.h);
  }
  fprintf(f, "\n[fields]\n");
  n_fields = (session && session->root)
                 ? sz_view_collect_text_fields(session->root, fields, 64)
                 : 0;
  field_target = (session && session->root)
                     ? sz_view_text_field_target(session->root)
                     : NULL;
  for (i = 0; i < n_fields; i++) {
    fprintf(f, "%d%s ", i, fields[i] == field_target ? "*" : "");
    fputs_dump_label(f, sz_view_a11y_label(fields[i]));
    fputc('=', f);
    fputs_dump_quoted(f, sz_view_text_field_value(fields[i]));
    fputc('\n', f);
  }
  fprintf(f, "\n[scrolls]\n");
  n_scrolls = sz_ui_collect_scrolls(session, scrolls, 64);
  for (i = 0; i < n_scrolls; i++) {
    fprintf(f, "%d ", i);
    fputs_dump_label(f, sz_view_a11y_label(scrolls[i]));
    fputc('\n', f);
  }
  if (session && session->last_hit_seen) {
    fprintf(f, "\n[last_hit]\nxy %.1f %.1f -> %s\n", session->last_hit_x,
            session->last_hit_y,
            session->last_hit_desc ? session->last_hit_desc : "NULL");
  }
  if (session && session->debug_dump_path && path &&
      strcmp(path, session->debug_dump_path) == 0) {
    fprintf(f, "\n[session]\nkind=%s\nwidth=%d\nheight=%d\nlifecycle=%s\n"
               "keyboard=%d\npumps=%u\n",
            runtime_kind_name(session->cfg.kind), session->cfg.width,
            session->cfg.height, lifecycle_name(session->lifecycle),
            session->keyboard_visible, session->pumps);
    {
      char heap[1536];
      char live[2048];
      sz_alloc_format_heap(heap, sizeof heap, 1);
      fprintf(f, "\n[heap]\n%s", heap);
      sz_alloc_format_live(live, sizeof live, 32);
      fprintf(f, "\n[live]\n%s", live);
    }
  }
  fclose(f);
  sz_string_free(signals);
  sz_string_free(views);
  return 1;
}

int sz_ui_session_dump_now(SzUiSession *session) {
  if (!session || !session->debug_dump_path)
    return 0;
  return sz_ui_session_write_dump(session, session->debug_dump_path);
}

int sz_ui_session_reload(SzUiSession *session) {
  SzView *root;
  int ok;
  if (!session || !session->rebuild)
    return 0;
  root = session->rebuild(session->rebuild_env);
  if (!root)
    return 0;
  if (root == session->root) {
    session->dirty = 1;
    ok = 1;
  } else
    ok = sz_ui_session_replace_root(session, root);
  if (ok && session->code_stale) {
    dlclose(session->code_stale);
    session->code_stale = NULL;
  }
  return ok;
}

static int copy_file(const char *src, const char *dst) {
  FILE *in, *out;
  char buf[4096];
  size_t n;
  in = fopen(src, "rb");
  if (!in)
    return 0;
  out = fopen(dst, "wb");
  if (!out) {
    fclose(in);
    return 0;
  }
  while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in);
      fclose(out);
      return 0;
    }
  }
  fclose(in);
  fclose(out);
  return 1;
}

int sz_ui_session_load_code(SzUiSession *session, const char *path) {
  char staged[1024];
  void *h;
  SzUiRebuildFn fn;
  if (!session || !path || !path[0])
    return 0;
  session->code_gen++;
  if (snprintf(staged, sizeof staged, "%s.load-%d", path, session->code_gen) >=
      (int)sizeof staged)
    return 0;
  if (!copy_file(path, staged))
    return 0;
  h = dlopen(staged, RTLD_NOW | RTLD_LOCAL);
  if (!h)
    return 0;
  fn = (SzUiRebuildFn)dlsym(h, "sz_ui_reload_rebuild");
  if (!fn) {
    dlclose(h);
    return 0;
  }
  session->code_stale = session->code_handle;
  session->code_handle = h;
  session->rebuild = fn;
  return 1;
}

void sz_ui_bridge_post_int(SzUiSession *session, SzSignalInt *sig, int64_t value) {
  BridgeItem *it;
  if (!session || !sig)
    return;
  it = (BridgeItem *)sz_alloc_zero(sizeof(BridgeItem));
  it->kind = BRIDGE_INT;
  it->sig_int = sig;
  it->int_value = value;
  if (session->bridge_tail)
    session->bridge_tail->next = it;
  else
    session->bridge_head = it;
  session->bridge_tail = it;
  session->dirty = 1;
}

void sz_ui_bridge_post_str(SzUiSession *session, SzSignalStr *sig, const char *value) {
  BridgeItem *it;
  if (!session || !sig)
    return;
  it = (BridgeItem *)sz_alloc_zero(sizeof(BridgeItem));
  it->kind = BRIDGE_STR;
  it->sig_str = sig;
  it->str_value = sz_strdup(value);
  if (session->bridge_tail)
    session->bridge_tail->next = it;
  else
    session->bridge_head = it;
  session->bridge_tail = it;
  session->dirty = 1;
}

void sz_ui_bridge_flush(SzUiSession *session) {
  BridgeItem *it, *next;
  if (!session)
    return;
  it = session->bridge_head;
  session->bridge_head = NULL;
  session->bridge_tail = NULL;
  while (it) {
    next = it->next;
    if (it->kind == BRIDGE_INT)
      sz_signal_int_set(it->sig_int, it->int_value);
    else if (it->kind == BRIDGE_STR) {
      sz_signal_str_set(it->sig_str, it->str_value);
      sz_free(it->str_value);
    }
    sz_free(it);
    it = next;
  }
}

void sz_ui_unmount(SzUiSession *session) {
  if (!session)
    return;
  sz_ui_bridge_flush(session);
  if (session->cfg.kind == SZ_UI_RUNTIME_DESKTOP)
    sz_embedder_shutdown();
  if (session->cfg.kind == SZ_UI_RUNTIME_MOBILE) {
    sz_mobile_set_keyboard(0);
    sz_mobile_shutdown();
  }
  if (session->surface)
    sk_surface_unref(session->surface);
  if (session->owns_view)
    sz_view_free(session->root);
  sz_release(session->rebuild_env);
  session->rebuild_env = NULL;
  if (session->code_stale)
    dlclose(session->code_stale);
  if (session->code_handle)
    dlclose(session->code_handle);
  sz_free(session->watch_path);
  sz_free(session->watch_fp);
  sz_free(session->debug_dump_path);
  session->debug_dump_path = NULL;
  sz_alloc_set_panic_dump(NULL);
  sz_free(session->inject_path);
  sz_free(session->inject_fp);
  sz_free(session->record_path);
  sz_free(session->last_hit_desc);
  sz_free(session);
}

static void format_last_hit_desc(SzView *hit, char *buf, size_t cap) {
  const char *role = "none";
  if (!hit) {
    snprintf(buf, cap, "NULL");
    return;
  }
  switch (sz_view_a11y_role(hit)) {
  case SZ_A11Y_BUTTON:
    role = "button";
    break;
  case SZ_A11Y_TEXT:
    role = "text";
    break;
  case SZ_A11Y_TEXT_FIELD:
    role = "textfield";
    break;
  case SZ_A11Y_IMAGE:
    role = "image";
    break;
  case SZ_A11Y_LIST:
    role = "list";
    break;
  case SZ_A11Y_SCROLL:
    role = "scroll";
    break;
  case SZ_A11Y_CHECKBOX:
    role = "checkbox";
    break;
  case SZ_A11Y_SLIDER:
    role = "slider";
    break;
  case SZ_A11Y_RADIO:
    role = "radio";
    break;
  case SZ_A11Y_PROGRESS:
    role = "progress";
    break;
  case SZ_A11Y_CIRCULAR:
    role = "circular";
    break;
  case SZ_A11Y_AVATAR:
    role = "avatar";
    break;
  case SZ_A11Y_CHECK_TILE:
    role = "checktile";
    break;
  case SZ_A11Y_SWITCH_TILE:
    role = "switchtile";
    break;
  case SZ_A11Y_RADIO_TILE:
    role = "radiotile";
    break;
  case SZ_A11Y_SEGMENTED:
    role = "segmented";
    break;
  case SZ_A11Y_FAB:
    role = "fab";
    break;
  case SZ_A11Y_TOOLTIP:
    role = "tooltip";
    break;
  case SZ_A11Y_OUTLINED:
    role = "outlined";
    break;
  case SZ_A11Y_TEXT_BUTTON:
    role = "textbutton";
    break;
  case SZ_A11Y_PLACEHOLDER:
    role = "placeholder";
    break;
  case SZ_A11Y_SEMANTICS:
    role = "semantics";
    break;
  case SZ_A11Y_MERGE:
    role = "merge";
    break;
  case SZ_A11Y_INK_WELL:
    role = "inkwell";
    break;
  case SZ_A11Y_VISIBILITY:
    role = "visibility";
    break;
  case SZ_A11Y_OFFSTAGE:
    role = "offstage";
    break;
  case SZ_A11Y_UNCONSTRAINED:
    role = "unconstrained";
    break;
  case SZ_A11Y_SWITCH:
    role = "switch";
    break;
  case SZ_A11Y_CHIP:
    role = "chip";
    break;
  case SZ_A11Y_FILTER_CHIP:
    role = "filterchip";
    break;
  case SZ_A11Y_CHOICE_CHIP:
    role = "choicechip";
    break;
  case SZ_A11Y_ACTION_CHIP:
    role = "actionchip";
    break;
  case SZ_A11Y_INPUT_CHIP:
    role = "inputchip";
    break;
  case SZ_A11Y_LIST_TILE:
    role = "listtile";
    break;
  case SZ_A11Y_BADGE:
    role = "badge";
    break;
  case SZ_A11Y_CARD:
    role = "card";
    break;
  case SZ_A11Y_DIVIDER:
    role = "divider";
    break;
  case SZ_A11Y_EXPANSION:
    role = "expansion";
    break;
  case SZ_A11Y_ICON_BUTTON:
    role = "iconbutton";
    break;
  case SZ_A11Y_VDIV:
    role = "vdiv";
    break;
  default:
    break;
  }
  snprintf(buf, cap, "%s:%s", role, sz_view_a11y_label(hit));
}

static void session_set_last_hit(SzUiSession *session, float x, float y,
                                 SzView *hit_if_fired) {
  char desc[256];
  if (!session)
    return;
  format_last_hit_desc(hit_if_fired, desc, sizeof desc);
  session->last_hit_seen = 1;
  session->last_hit_x = x;
  session->last_hit_y = y;
  sz_free(session->last_hit_desc);
  session->last_hit_desc = sz_strdup(desc);
}

static int find_tap_index_at(SzUiSession *session, float x, float y) {
  SzView *buttons[64];
  SzView *hit;
  int n, i;
  if (!session || !session->root)
    return -1;
  sz_view_layout(session->root, (float)session->cfg.width,
                 (float)session->cfg.height, session->theme);
  hit = sz_view_hit_test(session->root, x, y);
  if (!hit || !sz_view_is_tap_target(hit))
    return -1;
  if (sz_view_kind(hit) == SZ_VIEW_SLIDER)
    return -1;
  n = sz_ui_collect_buttons(session, buttons, 64);
  for (i = 0; i < n; i++) {
    if (buttons[i] == hit)
      return i;
  }
  return -1;
}

static void record_tap_or_xy(SzUiSession *session, FILE *f, float x, float y) {
  int idx = find_tap_index_at(session, x, y);
  if (idx >= 0)
    fprintf(f, "tap %d\n", idx);
  else
    fprintf(f, "xy %.1f %.1f\n", x, y);
}

/* Append one OS event to the record file. Script / inject playback must not
 * call this. */
static void record_live_event(SzUiSession *session, const SzInputEvent *ev) {
  FILE *f;
  if (!session || !session->record_path || !ev)
    return;
  f = fopen(session->record_path, "a");
  if (!f)
    return;
  if (ev->kind == SZ_INPUT_TAP) {
    record_tap_or_xy(session, f, ev->x, ev->y);
  } else if (ev->kind == SZ_INPUT_KEY && ev->key && ev->key[0]) {
    if (ev->text && ev->text[0])
      fprintf(f, "key %s %s\n", ev->key, ev->text);
    else
      fprintf(f, "key %s\n", ev->key);
  } else if (ev->kind == SZ_INPUT_TEXT_EDIT) {
    if (!ev->text || !ev->text[0])
      fputs("backspace\n", f);
    else
      fprintf(f, "type %s\n", ev->text);
  } else if (ev->kind == SZ_INPUT_POINTER &&
             ev->pointer_phase == SZ_POINTER_UP && session->pointer_down) {
    float dx = ev->x - session->pointer_down_x;
    float dy = ev->y - session->pointer_down_y;
    if (session->pointer_slider)
      fprintf(f, "xy %.1f %.1f\n", ev->x, ev->y);
    else if (dx * dx + dy * dy <= 64.f)
      record_tap_or_xy(session, f, ev->x, ev->y);
  } else if (ev->kind == SZ_INPUT_SCROLL && session->root) {
    SzView *scrolls[64];
    SzView *hit;
    int n, i, idx;
    sz_view_layout(session->root, (float)session->cfg.width,
                   (float)session->cfg.height, session->theme);
    hit = sz_view_scroll_at(session->root, ev->x, ev->y);
    if (hit) {
      n = sz_ui_collect_scrolls(session, scrolls, 64);
      idx = -1;
      for (i = 0; i < n; i++) {
        if (scrolls[i] == hit) {
          idx = i;
          break;
        }
      }
      if (idx >= 0)
        fprintf(f, "scroll %d %.0f\n", idx, ev->dy);
    }
  }
  fclose(f);
}

/* Live OS path: record then inject. Tests call this to simulate drain. */
int sz_ui_session_live_inject(SzUiSession *session, const SzInputEvent *event) {
  if (!session || !event)
    return 0;
  record_live_event(session, event);
  return sz_ui_inject_sync(session, event);
}

static void drain_mobile_events(SzUiSession *session) {
  SzInputEvent ev;
  if (!session || session->cfg.kind != SZ_UI_RUNTIME_MOBILE)
    return;
  if (!sz_mobile_available())
    return;
  while (sz_mobile_poll_event(&ev))
    (void)sz_ui_session_live_inject(session, &ev);
}

static void drain_desktop_events(SzUiSession *session) {
  SzInputEvent ev;
  if (!session || session->cfg.kind != SZ_UI_RUNTIME_DESKTOP)
    return;
  if (!sz_embedder_available())
    return;
  while (sz_embedder_poll_event(&ev))
    (void)sz_ui_session_live_inject(session, &ev);
}

/* Prefix-extend plays the suffix; rewrite plays the whole file. */
static int take_inject(SzUiSession *session, char **out) {
  char *now;
  size_t old_n, now_n;
  const char *play;
  if (!session || !session->inject_path || !out)
    return 0;
  *out = NULL;
  now = stamp_snapshot(session->inject_path);
  if (session->inject_fp && strcmp(session->inject_fp, now) == 0) {
    sz_free(now);
    return 0;
  }
  old_n = session->inject_fp ? strlen(session->inject_fp) : 0;
  now_n = strlen(now);
  if (old_n > 0 && now_n >= old_n && memcmp(session->inject_fp, now, old_n) == 0)
    play = now + old_n;
  else
    play = now;
  if (!play[0]) {
    sz_free(session->inject_fp);
    session->inject_fp = now;
    return 0;
  }
  *out = sz_strdup(play);
  sz_free(session->inject_fp);
  session->inject_fp = now;
  return 1;
}

int sz_ui_pump_sync(SzUiSession *session) {
  size_t nbytes = 0;
  const uint8_t *rgba;
  int pw, ph;
  float scale;
  SzTheme paint_theme;
  const SzTheme *theme;
  int need_dump;
  if (!session)
    return 0;
  if (session->lifecycle == SZ_LIFECYCLE_STOP)
    return 0;
  need_dump = session->dirty || session->bridge_head != NULL;
  /* Pull OS events before the frame (host-driven mobile / desktop shell). */
  drain_mobile_events(session);
  drain_desktop_events(session);
  if (session->dirty)
    need_dump = 1;
  if (stamp_changed(session)) {
    const char *code = getenv("SCUZZ_UI_RELOAD_CODE");
    if (code && code[0])
      sz_ui_session_load_code(session, code);
    if (!sz_ui_session_reload(session))
      return 0;
    need_dump = 1;
  }
  if (!session->inject_playing) {
    char *delta = NULL;
    if (take_inject(session, &delta)) {
      session->inject_playing = 1;
      sz_ui_script_play_text(session, delta);
      sz_free(delta);
      session->inject_playing = 0;
      need_dump = 1;
    }
  }
  /* UI-thread hop: apply signal writes posted from completed IO. */
  sz_ui_bridge_flush(session);

  scale = (float)session->cfg.scale;
  if (scale < 0.01f)
    scale = 1.f;
  pw = sk_surface_width(session->surface);
  ph = sk_surface_height(session->surface);
  theme = session->theme;
  if (scale != 1.f) {
    paint_theme = *session->theme;
    paint_theme.font_px *= scale;
    paint_theme.pad *= scale;
    paint_theme.gap *= scale;
    paint_theme.control_h *= scale;
    paint_theme.radius *= scale;
    paint_theme.px_scale = scale;
    theme = &paint_theme;
  }
  /* Paint in device pixels. Layout is restored to logical points afterward
   * so hit-testing / inject stay in the same space as embedder events. */
  if (!sz_view_paint(session->root, session->canvas, pw, ph, theme))
    return 0;
  if (scale != 1.f)
    sz_view_layout(session->root, (float)session->cfg.width,
                   (float)session->cfg.height, session->theme);
  session->dirty = 0;
  /* Desktop peer: present to OS surface when embedder is available. */
  if (session->cfg.kind == SZ_UI_RUNTIME_DESKTOP && sz_embedder_available()) {
    rgba = sk_surface_peek_pixels(session->surface, &nbytes);
    if (rgba) {
      sz_embedder_present(session->cfg.title, session->cfg.width,
                          session->cfg.height, pw, ph, rgba, nbytes);
    }
  }
  /* Mobile peer: present when resumed and shell is available. */
  if (session->cfg.kind == SZ_UI_RUNTIME_MOBILE &&
      session->lifecycle == SZ_LIFECYCLE_RESUME && sz_mobile_available()) {
    rgba = sk_surface_peek_pixels(session->surface, &nbytes);
    if (rgba) {
      sz_mobile_present(session->cfg.title, session->cfg.width,
                        session->cfg.height, pw, ph, rgba, nbytes);
    }
  }
  sz_alloc_trace_on_pump();
  session->pumps += 1;
  if (need_dump && session->debug_dump_path)
    sz_ui_session_write_dump(session, session->debug_dump_path);
  return 1;
}

static int inject_pointer(SzUiSession *session, const SzInputEvent *event) {
  float dx, dy;
  const float tap_slop2 = 64.f; /* 8px squared */

  if (!session->root)
    return 0;

  /* Make frames current for hit / scroll targeting. */
  sz_view_layout(session->root, (float)session->cfg.width,
                 (float)session->cfg.height, session->theme);

  switch (event->pointer_phase) {
  case SZ_POINTER_DOWN: {
    SzView *hit = sz_view_hit_test(session->root, event->x, event->y);
    session->pointer_down = 1;
    session->pointer_x = event->x;
    session->pointer_y = event->y;
    session->pointer_down_x = event->x;
    session->pointer_down_y = event->y;
    if (hit && sz_view_kind(hit) == SZ_VIEW_SLIDER) {
      session->pointer_slider = hit;
      session->pointer_scroll = NULL;
      sz_view_slider_set_at(hit, event->x);
    } else {
      session->pointer_slider = NULL;
      session->pointer_scroll =
          sz_view_scroll_at(session->root, event->x, event->y);
    }
    session->dirty = 1;
    return 1;
  }
  case SZ_POINTER_MOVE:
    if (!session->pointer_down)
      return 0;
    dx = event->x - session->pointer_x;
    dy = event->y - session->pointer_y;
    if (session->pointer_slider) {
      sz_view_slider_set_at(session->pointer_slider, event->x);
      session->dirty = 1;
    } else if (session->pointer_scroll) {
      /* Finger down → content follows (positive finger pans content up or left). */
      if (sz_view_scroll_is_h(session->pointer_scroll)) {
        if (dx > 0.5f || dx < -0.5f) {
          sz_view_scroll_by(session->pointer_scroll, -dx);
          session->dirty = 1;
        }
      } else if (dy > 0.5f || dy < -0.5f) {
        sz_view_scroll_by(session->pointer_scroll, -dy);
        session->dirty = 1;
      }
    }
    session->pointer_x = event->x;
    session->pointer_y = event->y;
    return 1;
  case SZ_POINTER_UP:
    if (!session->pointer_down)
      return 0;
    dx = event->x - session->pointer_down_x;
    dy = event->y - session->pointer_down_y;
    session->pointer_down = 0;
    session->pointer_scroll = NULL;
    if (session->pointer_slider) {
      SzView *sl = session->pointer_slider;
      sz_view_slider_set_at(sl, event->x);
      session_set_last_hit(session, event->x, event->y, sl);
      session->pointer_slider = NULL;
      session->dirty = 1;
      return 1;
    }
    if (dx * dx + dy * dy <= tap_slop2) {
      SzView *hit = sz_view_hit_test(session->root, event->x, event->y);
      int fired = sz_view_handle_tap(session->root, event->x, event->y);
      session_set_last_hit(session, event->x, event->y, fired ? hit : NULL);
      if (fired)
        sync_keyboard(session);
      session->dirty = 1;
    }
    return 1;
  default:
    return 0;
  }
}

int sz_ui_inject_sync(SzUiSession *session, const SzInputEvent *event) {
  SzView *scroll;
  if (!session || !event || !session->root)
    return 0;
  if (session->lifecycle == SZ_LIFECYCLE_STOP &&
      event->kind != SZ_INPUT_LIFECYCLE)
    return 0;

  switch (event->kind) {
  case SZ_INPUT_TAP: {
    SzView *hit;
    int fired;
    sz_view_layout(session->root, (float)session->cfg.width,
                   (float)session->cfg.height, session->theme);
    hit = sz_view_hit_test(session->root, event->x, event->y);
    fired = sz_view_handle_tap(session->root, event->x, event->y);
    session_set_last_hit(session, event->x, event->y, fired ? hit : NULL);
    if (fired)
      sync_keyboard(session);
    /* Miss is a successful inject; mark dirty so live dump rewrites. */
    session->dirty = 1;
    return 1;
  }
  case SZ_INPUT_TEXT:
    if (!sz_view_handle_text(session->root, event->text))
      return 0;
    sync_keyboard(session);
    session->dirty = 1;
    return 1;
  case SZ_INPUT_TEXT_EDIT: {
    int backspace = !event->text || !event->text[0];
    if (!sz_view_handle_text_edit(session->root, event->text, backspace))
      return 0;
    sync_keyboard(session);
    session->dirty = 1;
    return 1;
  }
  case SZ_INPUT_KEY:
    if (!sz_view_handle_key(session->root, event->key, event->text,
                            event->key_mods))
      return 0;
    sync_keyboard(session);
    session->dirty = 1;
    return 1;
  case SZ_INPUT_RESIZE:
    if (event->width <= 0 || event->height <= 0)
      return 0;
    sk_surface_unref(session->surface);
    session->cfg.width = event->width;
    session->cfg.height = event->height;
    {
      int pw = (int)(event->width * session->cfg.scale + 0.5);
      int ph = (int)(event->height * session->cfg.scale + 0.5);
      if (pw < 1)
        pw = 1;
      if (ph < 1)
        ph = 1;
      session->surface = make_session_surface(pw, ph);
    }
    if (!session->surface)
      return 0;
    session->canvas = sk_surface_get_canvas(session->surface);
    session->dirty = 1;
    return 1;
  case SZ_INPUT_POINTER:
    return inject_pointer(session, event);
  case SZ_INPUT_SCROLL:
    sz_view_layout(session->root, (float)session->cfg.width,
                   (float)session->cfg.height, session->theme);
    scroll = sz_view_scroll_at(session->root, event->x, event->y);
    if (!scroll)
      return 0;
    sz_view_scroll_by(scroll, event->dy);
    session->dirty = 1;
    return 1;
  case SZ_INPUT_LIFECYCLE:
    if (event->lifecycle != SZ_LIFECYCLE_RESUME &&
        event->lifecycle != SZ_LIFECYCLE_PAUSE &&
        event->lifecycle != SZ_LIFECYCLE_STOP)
      return 0;
    session->lifecycle = event->lifecycle;
    if (event->lifecycle == SZ_LIFECYCLE_PAUSE ||
        event->lifecycle == SZ_LIFECYCLE_STOP) {
      session->keyboard_visible = 0;
      if (session->cfg.kind == SZ_UI_RUNTIME_MOBILE)
        sz_mobile_set_keyboard(0);
    }
    session->dirty = 1;
    return 1;
  case SZ_INPUT_KEYBOARD:
    session->keyboard_visible = event->keyboard_visible ? 1 : 0;
    if (session->cfg.kind == SZ_UI_RUNTIME_MOBILE)
      sz_mobile_set_keyboard(session->keyboard_visible);
    session->dirty = 1;
    return 1;
  default:
    return 0;
  }
}

int sz_ui_session_activate_view(SzUiSession *session, SzView *target) {
  SzRect fr;
  float x, y;
  if (!session || !target || !session->root)
    return 0;
  sz_view_layout(session->root, (float)session->cfg.width,
                 (float)session->cfg.height, session->theme);
  fr = sz_view_frame(target);
  x = fr.x + fr.w * 0.5f;
  y = fr.y + fr.h * 0.5f;
  if (!sz_view_activate(session->root, target, x, y))
    return 0;
  session_set_last_hit(session, x, y, target);
  sync_keyboard(session);
  session->dirty = 1;
  return 1;
}

int sz_ui_snapshot_png_bytes(SzUiSession *session, uint8_t **out, size_t *out_len) {
  if (!session || !out || !out_len)
    return 0;
  if (session->dirty && !sz_ui_pump_sync(session))
    return 0;
  return sk_encode_png(session->surface, out, out_len);
}

int sz_ui_snapshot_png_sync(SzUiSession *session, const char *path) {
  if (!session || !path)
    return 0;
  if (session->dirty && !sz_ui_pump_sync(session))
    return 0;
  return sk_encode_png_to_file(session->surface, path);
}

SzUiRuntimeKind sz_ui_session_kind(const SzUiSession *session) {
  return session ? session->cfg.kind : (SzUiRuntimeKind)0;
}

int sz_ui_session_width(const SzUiSession *session) {
  return session ? session->cfg.width : 0;
}

int sz_ui_session_height(const SzUiSession *session) {
  return session ? session->cfg.height : 0;
}

SzView *sz_ui_session_root(SzUiSession *session) {
  return session ? session->root : NULL;
}

SzLifecyclePhase sz_ui_session_lifecycle(const SzUiSession *session) {
  return session ? session->lifecycle : SZ_LIFECYCLE_STOP;
}

int sz_ui_session_alive(const SzUiSession *session) {
  return session && session->lifecycle != SZ_LIFECYCLE_STOP;
}

void sz_ui_session_request_stop(SzUiSession *session) {
  if (!session)
    return;
  session->lifecycle = SZ_LIFECYCLE_STOP;
  session->keyboard_visible = 0;
}

int sz_ui_session_keyboard_visible(const SzUiSession *session) {
  return session ? session->keyboard_visible : 0;
}

unsigned sz_ui_session_pumps(const SzUiSession *session) {
  return session ? session->pumps : 0;
}

/* Resolve width, height, and scale from args or env. */
void sz_ui_resolve_headless_size(int *width, int *height, double *scale) {
  if (*width <= 0) {
    const char *w = getenv("SCUZZ_UI_WIDTH");
    *width = (w && atoi(w) > 0) ? atoi(w) : 200;
  }
  if (*height <= 0) {
    const char *h = getenv("SCUZZ_UI_HEIGHT");
    *height = (h && atoi(h) > 0) ? atoi(h) : 120;
  }
  if (scale) {
    const char *sc = getenv("SCUZZ_UI_SCALE");
    *scale = (sc && atof(sc) > 0.0) ? atof(sc) : 1.0;
  }
}

void sz_ui_session_finish(SzUiSession *session) {
  const char *snap = getenv("SCUZZ_SNAPSHOT_PATH");
  const char *dump = getenv("SCUZZ_FUZZ_DUMP");
  if (snap && snap[0]) {
    if (!sz_ui_snapshot_png_sync(session, snap))
      sz_panic("headless snapshot failed");
    fprintf(stderr, "scuzz: wrote snapshot %s\n", snap);
  }
  if (dump && dump[0]) {
    /* Structural oracle: signal store + a11y + tap indices (not pixels). */
    SzString *views;
    if (!sz_ui_session_write_dump(session, dump))
      sz_panic("fuzz dump open failed");
    views = session && session->root ? sz_view_a11y_dump(session->root)
                                     : sz_string_from_cstr("");
    sz_property_stash_a11y(sz_string_cstr(views));
    sz_string_free(views);
    fprintf(stderr, "scuzz: wrote fuzz dump %s\n", dump);
  } else if (session && session->root) {
    /* Still stash a11y for residual properties under TESTRT without a dump path. */
    SzString *views = sz_view_a11y_dump(session->root);
    sz_property_stash_a11y(sz_string_cstr(views));
    sz_string_free(views);
  }
}
