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

int sz_ui_pump_sync(SzUiSession *session) {
  size_t nbytes = 0;
  const uint8_t *rgba;
  int64_t now_ms;
  int pw, ph;
  float scale;
  SzTheme paint_theme;
  const SzTheme *theme;
  if (!session)
    return 0;
  if (session->lifecycle == SZ_LIFECYCLE_STOP)
    return 0;
  /* Pull OS events before the frame (host-driven mobile / desktop shell). */
  drain_mobile_events(session);
  drain_desktop_events(session);
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
    FILE *f = fopen(dump, "w");
    SzString *signals, *views;
    if (!f)
      sz_panic("fuzz dump open failed");
    signals = sz_signal_dump();
    views = session && session->root ? sz_view_a11y_dump(session->root)
                                     : sz_string_from_cstr("");
    fprintf(f, "[signals]\n%s\n[views]\n%s", sz_string_cstr(signals),
            sz_string_cstr(views));
    fclose(f);
    sz_law_stash_a11y(sz_string_cstr(views));
    sz_string_free(signals);
    sz_string_free(views);
    fprintf(stderr, "scuzz: wrote fuzz dump %s\n", dump);
  } else if (session && session->root) {
    /* Still stash a11y for residual laws under TESTRT without a dump path. */
    SzString *views = sz_view_a11y_dump(session->root);
    sz_law_stash_a11y(sz_string_cstr(views));
    sz_string_free(views);
  }
}
