#include "scalui_ui.h"

#include "sk_capi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *su_strdup(const char *s) {
  size_t n;
  char *out;
  if (!s)
    s = "";
  n = strlen(s);
  out = (char *)su_alloc(n + 1);
  memcpy(out, s, n + 1);
  return out;
}

struct SuView {
  char *text;
  uint32_t bg_argb;
  uint32_t fg_argb;
  int toggled; /* flipped by tap for Phase 1 interaction demo */
};

struct SuUiSession {
  SuUiConfig cfg;
  SuView *root;
  SkSurface *surface;
  SkCanvas *canvas;
  int dirty;
  int owns_view;
};

SuView *su_view_label(const char *text, uint32_t bg_argb, uint32_t fg_argb) {
  SuView *v = (SuView *)su_alloc_zero(sizeof(SuView));
  v->text = su_strdup(text);
  v->bg_argb = bg_argb;
  v->fg_argb = fg_argb;
  v->toggled = 0;
  return v;
}

void su_view_free(SuView *view) {
  if (!view)
    return;
  su_free(view->text);
  su_free(view);
}

static int paint_view(SuUiSession *s) {
  SkPaint *paint;
  SkColor bg, fg;
  uint32_t bg_argb, fg_argb;
  const char *label;
  float pad = 16.f;
  float bar_h = 40.f;
  if (!s || !s->canvas || !s->root)
    return 0;

  bg_argb = s->root->toggled ? s->root->fg_argb : s->root->bg_argb;
  fg_argb = s->root->toggled ? s->root->bg_argb : s->root->fg_argb;
  bg = sk_color_argb(bg_argb);
  fg = sk_color_argb(fg_argb);
  label = s->root->text ? s->root->text : "";

  sk_canvas_clear(s->canvas, bg);
  paint = sk_paint_new();
  if (!paint)
    return 0;
  sk_paint_set_color(paint, fg);
  sk_canvas_draw_rect(s->canvas, pad, pad, (float)s->cfg.width - pad * 2.f, bar_h,
                      paint);
  sk_paint_set_color(paint, bg);
  sk_canvas_draw_string(s->canvas, label, pad + 8.f, pad + 26.f, paint);
  sk_paint_delete(paint);
  s->dirty = 0;
  return 1;
}

SuUiSession *su_ui_mount(const SuUiConfig *cfg, SuView *root) {
  SuUiSession *s;
  int w, h;
  if (!cfg || !root)
    return NULL;
  if (cfg->width <= 0 || cfg->height <= 0)
    return NULL;
  if (cfg->kind != SU_UI_RUNTIME_HEADLESS && cfg->kind != SU_UI_RUNTIME_WINDOW)
    return NULL;

  w = cfg->width;
  h = cfg->height;
  s = (SuUiSession *)su_alloc_zero(sizeof(SuUiSession));
  s->cfg = *cfg;
  if (s->cfg.scale <= 0.0)
    s->cfg.scale = 1.0;
  s->root = root;
  s->owns_view = 0;
  s->surface = sk_surface_make_raster_n32_premul(w, h);
  if (!s->surface) {
    su_free(s);
    return NULL;
  }
  s->canvas = sk_surface_get_canvas(s->surface);
  s->dirty = 1;
  if (cfg->kind == SU_UI_RUNTIME_WINDOW) {
    /* Phase 1: Window is a peer interpreter on the same protocol. OS window
     * presentation lands in crates/embedder-desktop; we still paint offscreen
     * so mount/pump/inject/snapshot work without a display. */
    fprintf(stderr,
            "scalui: UiRuntime.Window mounted (offscreen peer; embedder later)\n");
  }
  return s;
}

void su_ui_unmount(SuUiSession *session) {
  if (!session)
    return;
  if (session->surface)
    sk_surface_unref(session->surface);
  if (session->owns_view)
    su_view_free(session->root);
  su_free(session);
}

int su_ui_pump_sync(SuUiSession *session) {
  if (!session)
    return 0;
  if (!paint_view(session))
    return 0;
  return 1;
}

int su_ui_inject_sync(SuUiSession *session, const SuInputEvent *event) {
  if (!session || !event || !session->root)
    return 0;
  switch (event->kind) {
  case SU_INPUT_TAP:
    session->root->toggled = !session->root->toggled;
    session->dirty = 1;
    return 1;
  case SU_INPUT_RESIZE:
    if (event->width <= 0 || event->height <= 0)
      return 0;
    sk_surface_unref(session->surface);
    session->cfg.width = event->width;
    session->cfg.height = event->height;
    session->surface =
        sk_surface_make_raster_n32_premul(event->width, event->height);
    if (!session->surface)
      return 0;
    session->canvas = sk_surface_get_canvas(session->surface);
    session->dirty = 1;
    return 1;
  default:
    return 0;
  }
}

int su_ui_snapshot_png_bytes(SuUiSession *session, uint8_t **out, size_t *out_len) {
  if (!session || !out || !out_len)
    return 0;
  if (session->dirty && !su_ui_pump_sync(session))
    return 0;
  return sk_encode_png(session->surface, out, out_len);
}

int su_ui_snapshot_png_sync(SuUiSession *session, const char *path) {
  if (!session || !path)
    return 0;
  if (session->dirty && !su_ui_pump_sync(session))
    return 0;
  return sk_encode_png_to_file(session->surface, path);
}

typedef struct {
  SuUiSession *session;
} UiEnv;

static void *thunk_pump(void *env) {
  UiEnv *e = (UiEnv *)env;
  if (!su_ui_pump_sync(e->session))
    su_panic("su_ui_pump failed");
  return NULL;
}

SuIo *su_ui_pump(SuUiSession *session) {
  UiEnv *e = (UiEnv *)su_alloc(sizeof(UiEnv));
  e->session = session;
  return su_io_delay(thunk_pump, e);
}

typedef struct {
  SuUiSession *session;
  SuInputEvent event;
} UiInjectEnv;

static void *thunk_inject(void *env) {
  UiInjectEnv *e = (UiInjectEnv *)env;
  if (!su_ui_inject_sync(e->session, &e->event))
    su_panic("su_ui_inject failed");
  return NULL;
}

SuIo *su_ui_inject(SuUiSession *session, SuInputEvent event) {
  UiInjectEnv *e = (UiInjectEnv *)su_alloc(sizeof(UiInjectEnv));
  e->session = session;
  e->event = event;
  return su_io_delay(thunk_inject, e);
}

typedef struct {
  SuUiSession *session;
  char *path;
} UiSnapEnv;

static void *thunk_snapshot(void *env) {
  UiSnapEnv *e = (UiSnapEnv *)env;
  if (!su_ui_snapshot_png_sync(e->session, e->path))
    su_panic("su_ui_snapshot_png failed");
  return NULL;
}

SuIo *su_ui_snapshot_png(SuUiSession *session, const char *path) {
  UiSnapEnv *e = (UiSnapEnv *)su_alloc(sizeof(UiSnapEnv));
  e->session = session;
  e->path = path ? su_strdup(path) : NULL;
  if (!e->path)
    su_panic("snapshot path required");
  return su_io_delay(thunk_snapshot, e);
}

SuUiRuntimeKind su_ui_session_kind(const SuUiSession *session) {
  return session ? session->cfg.kind : (SuUiRuntimeKind)0;
}

int su_ui_session_width(const SuUiSession *session) {
  return session ? session->cfg.width : 0;
}

int su_ui_session_height(const SuUiSession *session) {
  return session ? session->cfg.height : 0;
}

typedef struct {
  char *text;
  int width;
  int height;
} HeadlessDemoEnv;

static void *thunk_headless_demo(void *env) {
  HeadlessDemoEnv *e = (HeadlessDemoEnv *)env;
  SuUiConfig cfg;
  SuView *view;
  SuUiSession *session;
  const char *snap = getenv("SCALUI_SNAPSHOT_PATH");
  SuInputEvent tap;

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SU_UI_RUNTIME_HEADLESS;
  cfg.width = e->width;
  cfg.height = e->height;
  if (cfg.width <= 0) {
    const char *w = getenv("SCALUI_UI_WIDTH");
    cfg.width = (w && atoi(w) > 0) ? atoi(w) : 200;
  }
  if (cfg.height <= 0) {
    const char *h = getenv("SCALUI_UI_HEIGHT");
    cfg.height = (h && atoi(h) > 0) ? atoi(h) : 100;
  }
  {
    const char *sc = getenv("SCALUI_UI_SCALE");
    cfg.scale = (sc && atof(sc) > 0.0) ? atof(sc) : 1.0;
  }

  view = su_view_label(e->text, 0xFF142850u, 0xFFF0F0F0u);
  session = su_ui_mount(&cfg, view);
  if (!session)
    su_panic("headless mount failed");
  session->owns_view = 1;

  if (!su_ui_pump_sync(session))
    su_panic("headless pump failed");

  /* Optional scripted interaction for golden-after-tap tests. */
  if (getenv("SCALUI_UI_TAP")) {
    memset(&tap, 0, sizeof(tap));
    tap.kind = SU_INPUT_TAP;
    tap.x = (float)cfg.width / 2.f;
    tap.y = (float)cfg.height / 2.f;
    if (!su_ui_inject_sync(session, &tap) || !su_ui_pump_sync(session))
      su_panic("headless tap/pump failed");
  }

  if (snap && snap[0]) {
    if (!su_ui_snapshot_png_sync(session, snap))
      su_panic("headless snapshot failed");
    fprintf(stderr, "scalui: wrote snapshot %s\n", snap);
  }

  su_ui_unmount(session);
  su_free(e->text);
  su_free(e);
  return NULL;
}

SuIo *su_ui_run_headless_label(const char *text, int width, int height) {
  HeadlessDemoEnv *e = (HeadlessDemoEnv *)su_alloc(sizeof(HeadlessDemoEnv));
  e->text = su_strdup(text ? text : "ScalUI");
  e->width = width;
  e->height = height;
  return su_io_delay(thunk_headless_demo, e);
}
