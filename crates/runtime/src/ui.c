#include "scuzz_ui.h"

#include "scuzz_embedder.h"
#include "scuzz_mobile.h"
#include "sk_capi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
__attribute__((weak)) int sz_mobile_present(const char *title, int width,
                                            int height, const uint8_t *rgba,
                                            size_t nbytes) {
  (void)title;
  (void)width;
  (void)height;
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

/* Implemented in view.c */
int sz_view_paint(SzView *root, SkCanvas *canvas, int width, int height,
                  const SzTheme *theme);
int sz_view_handle_tap(SzView *root, float x, float y);
int sz_view_handle_text(SzView *root, const char *text);
int sz_view_handle_text_edit(SzView *root, const char *text, int backspace);

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
  int64_t last_pump_ms; /* monotonic ms for animation dt */
  int has_pump_clock;
  SzUiRebuildFn rebuild;
  void *rebuild_env;
  char *watch_path;
  char *watch_fp;
  char *debug_dump_path;
  char *inject_path;
  char *inject_fp;
  int inject_playing;
};

static char *sz_strdup(const char *s) {
  size_t n;
  char *out;
  if (!s)
    s = "";
  n = strlen(s);
  out = (char *)sz_alloc(n + 1);
  memcpy(out, s, n + 1);
  return out;
}

static int runtime_kind_ok(SzUiRuntimeKind kind) {
  return kind == SZ_UI_RUNTIME_HEADLESS || kind == SZ_UI_RUNTIME_WINDOW ||
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
  /* Window: prefer OS backing scale so Retina text stays sharp. */
  if (s->cfg.kind == SZ_UI_RUNTIME_WINDOW && sz_embedder_available()) {
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
  s->surface = sk_surface_make_raster_n32_premul(pw, ph);
  if (!s->surface) {
    sz_free(s);
    return NULL;
  }
  s->canvas = sk_surface_get_canvas(s->surface);
  s->dirty = 1;
  if (cfg->kind == SZ_UI_RUNTIME_WINDOW) {
    if (sz_embedder_available()) {
      fprintf(stderr,
              "scuzz: UiRuntime.Window mounted (desktop embedder, scale=%.2f)\n",
              s->cfg.scale);
    } else {
      fprintf(stderr,
              "scuzz: UiRuntime.Window mounted (offscreen; no desktop embedder)\n");
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
  if (!session || !path || !path[0])
    return 0;
  sz_free(session->debug_dump_path);
  session->debug_dump_path = sz_strdup(path);
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

int sz_ui_session_write_dump(SzUiSession *session, const char *path) {
  FILE *f;
  SzString *signals;
  SzString *views;
  if (!path || !path[0])
    return 0;
  f = fopen(path, "w");
  if (!f)
    return 0;
  signals = sz_signal_dump();
  views = (session && session->root) ? sz_view_a11y_dump(session->root)
                                     : sz_string_from_cstr("");
  fprintf(f, "[signals]\n%s\n[views]\n%s", sz_string_cstr(signals),
          sz_string_cstr(views));
  fclose(f);
  sz_string_free(signals);
  sz_string_free(views);
  return 1;
}

int sz_ui_session_reload(SzUiSession *session) {
  SzView *root;
  if (!session || !session->rebuild)
    return 0;
  root = session->rebuild(session->rebuild_env);
  if (!root)
    return 0;
  if (root == session->root) {
    session->dirty = 1;
    return 1;
  }
  return sz_ui_session_replace_root(session, root);
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
  if (session->cfg.kind == SZ_UI_RUNTIME_WINDOW)
    sz_embedder_shutdown();
  if (session->cfg.kind == SZ_UI_RUNTIME_MOBILE) {
    sz_mobile_set_keyboard(0);
    sz_mobile_shutdown();
  }
  if (session->surface)
    sk_surface_unref(session->surface);
  if (session->owns_view)
    sz_view_free(session->root);
  sz_free(session->watch_path);
  sz_free(session->watch_fp);
  sz_free(session->debug_dump_path);
  sz_free(session->inject_path);
  sz_free(session->inject_fp);
  sz_free(session);
}

static void drain_mobile_events(SzUiSession *session) {
  SzInputEvent ev;
  if (!session || session->cfg.kind != SZ_UI_RUNTIME_MOBILE)
    return;
  if (!sz_mobile_available())
    return;
  while (sz_mobile_poll_event(&ev)) {
    if (!sz_ui_inject_sync(session, &ev))
      break;
  }
}

static void drain_desktop_events(SzUiSession *session) {
  SzInputEvent ev;
  if (!session || session->cfg.kind != SZ_UI_RUNTIME_WINDOW)
    return;
  if (!sz_embedder_available())
    return;
  while (sz_embedder_poll_event(&ev)) {
    if (!sz_ui_inject_sync(session, &ev))
      break;
  }
}

static int collect_buttons(SzUiSession *session, SzView **buttons, int cap) {
  SzView *r = session ? session->root : NULL;
  int n_buttons = 0;
  int yi, xi;
  int w = sz_ui_session_width(session);
  int h = sz_ui_session_height(session);
  if (!r)
    return 0;
  for (yi = 0; yi < h; yi += 4) {
    for (xi = 0; xi < w; xi += 4) {
      SzView *hit = sz_view_hit_test(r, (float)xi, (float)yi);
      int seen = 0;
      int bi;
      if (!hit || sz_view_kind(hit) != SZ_VIEW_BUTTON)
        continue;
      for (bi = 0; bi < n_buttons; bi++) {
        if (buttons[bi] == hit) {
          seen = 1;
          break;
        }
      }
      if (!seen && n_buttons < cap)
        buttons[n_buttons++] = hit;
    }
  }
  return n_buttons;
}

static int collect_scrolls(SzUiSession *session, SzView **scrolls, int cap) {
  SzView *r = session ? session->root : NULL;
  int n = 0;
  int yi, xi;
  int w = sz_ui_session_width(session);
  int h = sz_ui_session_height(session);
  if (!r)
    return 0;
  for (yi = 0; yi < h; yi += 4) {
    for (xi = 0; xi < w; xi += 4) {
      SzView *hit = sz_view_scroll_at(r, (float)xi, (float)yi);
      int seen = 0;
      int si;
      if (!hit)
        continue;
      for (si = 0; si < n; si++) {
        if (scrolls[si] == hit) {
          seen = 1;
          break;
        }
      }
      if (!seen && n < cap)
        scrolls[n++] = hit;
    }
  }
  return n;
}

static void script_scroll(SzUiSession *session, float dy) {
  SzView *scrolls[16];
  int count = collect_scrolls(session, scrolls, 16);
  SzInputEvent ev;
  SzRect fr;
  if (count <= 0) {
    fprintf(stderr, "scuzz: script scroll skipped (no scroll)\n");
    return;
  }
  fr = sz_view_frame(scrolls[0]);
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_SCROLL;
  ev.x = fr.x + fr.w * 0.5f;
  ev.y = fr.y + fr.h * 0.5f;
  ev.dy = dy;
  if (!sz_ui_inject_sync(session, &ev))
    fprintf(stderr, "scuzz: script scroll skipped (no scroll)\n");
}

static void script_backspace(SzUiSession *session, int n) {
  SzInputEvent ev;
  if (n < 1)
    n = 1;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = "";
  while (n-- > 0) {
    if (!sz_ui_inject_sync(session, &ev)) {
      fprintf(stderr, "scuzz: script backspace skipped (no text field)\n");
      return;
    }
  }
}

static void script_type(SzUiSession *session, const char *text) {
  SzInputEvent ev;
  if (!text || !text[0])
    return;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = text;
  if (!sz_ui_inject_sync(session, &ev))
    fprintf(stderr, "scuzz: script type skipped (no text field)\n");
}

static void script_tap(SzUiSession *session, int n) {
  SzView *buttons[64];
  int count = collect_buttons(session, buttons, 64);
  SzInputEvent tap;
  SzRect fr;
  float x, y;
  int w = sz_ui_session_width(session);
  int h = sz_ui_session_height(session);
  if (n < 0 || n >= count) {
    fprintf(stderr, "scuzz: script tap %d skipped (%d buttons)\n", n, count);
    return;
  }
  fr = sz_view_frame(buttons[n]);
  x = fr.x + fr.w * 0.5f;
  y = fr.y + fr.h * 0.5f;
  if (x < 0.f)
    x = 0.f;
  if (y < 0.f)
    y = 0.f;
  if (w > 0 && x >= (float)w)
    x = (float)w - 1.f;
  if (h > 0 && y >= (float)h)
    y = (float)h - 1.f;
  if (x < fr.x)
    x = fr.x + 1.f;
  if (y < fr.y)
    y = fr.y + 1.f;
  if (x >= fr.x + fr.w)
    x = fr.x + fr.w - 1.f;
  if (y >= fr.y + fr.h)
    y = fr.y + fr.h - 1.f;
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = x;
  tap.y = y;
  if (!sz_ui_inject_sync(session, &tap))
    sz_panic("Ui.run: script tap inject failed");
}

static void play_script_line(SzUiSession *session, char *line) {
  size_t len = strlen(line);
  if (len == 0 || line[0] == '#')
    return;
  if (strncmp(line, "tap ", 4) == 0 || strcmp(line, "tap") == 0)
    script_tap(session, len > 3 ? atoi(line + 4) : 0);
  else if (strncmp(line, "text ", 5) == 0 || strcmp(line, "text") == 0) {
    SzInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = SZ_INPUT_TEXT;
    ev.text = len > 4 ? line + 5 : "";
    if (!sz_ui_inject_sync(session, &ev))
      fprintf(stderr, "scuzz: script text skipped (no text field)\n");
  } else if (strncmp(line, "pump ", 5) == 0 || strcmp(line, "pump") == 0) {
    int k = len > 5 ? atoi(line + 5) : 1;
    while (k-- > 1) {
      if (!sz_ui_pump_sync(session))
        sz_panic("Ui.run: script pump failed");
    }
  } else if (strncmp(line, "scroll ", 7) == 0 || strcmp(line, "scroll") == 0)
    script_scroll(session, len > 6 ? (float)atoi(line + 7) : 40.f);
  else if (strncmp(line, "backspace ", 10) == 0 || strcmp(line, "backspace") == 0)
    script_backspace(session, len > 9 ? atoi(line + 10) : 1);
  else if (strncmp(line, "type ", 5) == 0 || strcmp(line, "type") == 0)
    script_type(session, len > 4 ? line + 5 : "");
  else
    sz_panic("Ui.run: unknown SCUZZ_UI_SCRIPT directive");
  if (!sz_ui_pump_sync(session))
    sz_panic("Ui.run: script pump failed");
}

static void play_ui_script_text(SzUiSession *session, char *text) {
  char *p = text;
  while (p && *p) {
    char *nl = strchr(p, '\n');
    char *line = p;
    size_t len;
    if (nl) {
      *nl = '\0';
      p = nl + 1;
    } else
      p += strlen(p);
    len = strlen(line);
    while (len > 0 && line[len - 1] == '\r')
      line[--len] = '\0';
    play_script_line(session, line);
  }
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
  int64_t now_ms;
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
  if (stamp_changed(session)) {
    if (!sz_ui_session_reload(session))
      return 0;
    need_dump = 1;
  }
  if (!session->inject_playing) {
    char *delta = NULL;
    if (take_inject(session, &delta)) {
      session->inject_playing = 1;
      play_ui_script_text(session, delta);
      sz_free(delta);
      session->inject_playing = 0;
      need_dump = 1;
    }
  }
  /* Advance animations with monotonic Clock dt. */
  now_ms = sz_clock_monotonic_ms_sync();
  if (session->has_pump_clock) {
    int64_t dt = now_ms - session->last_pump_ms;
    if (dt > 0)
      sz_anim_tick_all(dt);
  } else {
    session->has_pump_clock = 1;
  }
  session->last_pump_ms = now_ms;
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
    theme = &paint_theme;
  }
  /* Paint in device pixels; layout is restored to logical points afterward
   * so hit-testing / inject stay in the same space as embedder events. */
  if (!sz_view_paint(session->root, session->canvas, pw, ph, theme))
    return 0;
  if (scale != 1.f)
    sz_view_layout(session->root, (float)session->cfg.width,
                   (float)session->cfg.height, session->theme);
  session->dirty = 0;
  /* Window peer: present to OS surface when embedder is available. */
  if (session->cfg.kind == SZ_UI_RUNTIME_WINDOW && sz_embedder_available()) {
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
                        session->cfg.height, rgba, nbytes);
    }
  }
  sz_alloc_trace_on_pump();
  if (need_dump && session->debug_dump_path)
    sz_ui_session_write_dump(session, session->debug_dump_path);
  return 1;
}

static int inject_pointer(SzUiSession *session, const SzInputEvent *event) {
  float dx, dy;
  const float tap_slop2 = 64.f; /* 8px squared */

  if (!session->root)
    return 0;

  /* Ensure frames are current for hit / scroll targeting. */
  sz_view_layout(session->root, (float)session->cfg.width,
                 (float)session->cfg.height, session->theme);

  switch (event->pointer_phase) {
  case SZ_POINTER_DOWN:
    session->pointer_down = 1;
    session->pointer_x = event->x;
    session->pointer_y = event->y;
    session->pointer_down_x = event->x;
    session->pointer_down_y = event->y;
    session->pointer_scroll = sz_view_scroll_at(session->root, event->x, event->y);
    session->dirty = 1;
    return 1;
  case SZ_POINTER_MOVE:
    if (!session->pointer_down)
      return 0;
    dy = event->y - session->pointer_y;
    if (session->pointer_scroll && (dy > 0.5f || dy < -0.5f)) {
      /* Finger down → content follows (positive finger dy scrolls content up). */
      sz_view_scroll_by(session->pointer_scroll, -dy);
      session->dirty = 1;
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
    if (dx * dx + dy * dy <= tap_slop2) {
      if (!sz_view_handle_tap(session->root, event->x, event->y)) {
        /* Miss is still a successful pointer up. */
      }
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
  case SZ_INPUT_TAP:
    if (!sz_view_handle_tap(session->root, event->x, event->y))
      return 0;
    sync_keyboard(session);
    session->dirty = 1;
    return 1;
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
      session->surface = sk_surface_make_raster_n32_premul(pw, ph);
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

int sz_ui_session_keyboard_visible(const SzUiSession *session) {
  return session ? session->keyboard_visible : 0;
}

/* Shared resolution of headless size from args / env. */
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

void sz_ui_demo_finish(SzUiSession *session) {
  const char *snap = getenv("SCUZZ_SNAPSHOT_PATH");
  const char *dump = getenv("SCUZZ_FUZZ_DUMP");
  if (snap && snap[0]) {
    if (!sz_ui_snapshot_png_sync(session, snap))
      sz_panic("headless snapshot failed");
    fprintf(stderr, "scuzz: wrote snapshot %s\n", snap);
  }
  if (dump && dump[0]) {
    /* Structural oracle: signal store + a11y view dump (not pixels). */
    SzString *views;
    if (!sz_ui_session_write_dump(session, dump))
      sz_panic("fuzz dump open failed");
    views = session && session->root ? sz_view_a11y_dump(session->root)
                                     : sz_string_from_cstr("");
    sz_law_stash_a11y(sz_string_cstr(views));
    sz_string_free(views);
    fprintf(stderr, "scuzz: wrote fuzz dump %s\n", dump);
  } else if (session && session->root) {
    /* Still stash a11y for residual laws under TESTRT without a dump path. */
    SzString *views = sz_view_a11y_dump(session->root);
    sz_law_stash_a11y(sz_string_cstr(views));
    sz_string_free(views);
  }
}
