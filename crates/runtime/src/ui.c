#include "scalui_ui.h"

#include "scalui_embedder.h"
#include "sk_capi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Weak stubs — strong defs from embedder-desktop override when linked. */
__attribute__((weak)) int su_embedder_available(void) { return 0; }
__attribute__((weak)) int su_embedder_present(const char *title, int width,
                                              int height, const uint8_t *rgba,
                                              size_t nbytes) {
  (void)title;
  (void)width;
  (void)height;
  (void)rgba;
  (void)nbytes;
  return 0;
}
__attribute__((weak)) void su_embedder_shutdown(void) {}

/* Implemented in view.c */
int su_view_paint(SuView *root, SkCanvas *canvas, int width, int height,
                  const SuTheme *theme);
int su_view_handle_tap(SuView *root, float x, float y);
int su_view_handle_text(SuView *root, const char *text);

typedef enum {
  BRIDGE_INT = 1,
  BRIDGE_STR = 2
} BridgeKind;

typedef struct BridgeItem {
  BridgeKind kind;
  SuSignalInt *sig_int;
  SuSignalStr *sig_str;
  int64_t int_value;
  char *str_value;
  struct BridgeItem *next;
} BridgeItem;

struct SuUiSession {
  SuUiConfig cfg;
  SuView *root;
  SkSurface *surface;
  SkCanvas *canvas;
  int dirty;
  int owns_view;
  const SuTheme *theme;
  BridgeItem *bridge_head;
  BridgeItem *bridge_tail;
};

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
  s->theme = su_theme_default();
  s->surface = sk_surface_make_raster_n32_premul(w, h);
  if (!s->surface) {
    su_free(s);
    return NULL;
  }
  s->canvas = sk_surface_get_canvas(s->surface);
  s->dirty = 1;
  if (cfg->kind == SU_UI_RUNTIME_WINDOW) {
    if (su_embedder_available()) {
      fprintf(stderr, "scalui: UiRuntime.Window mounted (X11 embedder)\n");
    } else {
      fprintf(stderr,
              "scalui: UiRuntime.Window mounted (offscreen; no DISPLAY)\n");
    }
  }
  return s;
}

void su_ui_session_take_root(SuUiSession *session) {
  if (session)
    session->owns_view = 1;
}

void su_ui_bridge_post_int(SuUiSession *session, SuSignalInt *sig, int64_t value) {
  BridgeItem *it;
  if (!session || !sig)
    return;
  it = (BridgeItem *)su_alloc_zero(sizeof(BridgeItem));
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

void su_ui_bridge_post_str(SuUiSession *session, SuSignalStr *sig, const char *value) {
  BridgeItem *it;
  if (!session || !sig)
    return;
  it = (BridgeItem *)su_alloc_zero(sizeof(BridgeItem));
  it->kind = BRIDGE_STR;
  it->sig_str = sig;
  it->str_value = su_strdup(value);
  if (session->bridge_tail)
    session->bridge_tail->next = it;
  else
    session->bridge_head = it;
  session->bridge_tail = it;
  session->dirty = 1;
}

void su_ui_bridge_flush(SuUiSession *session) {
  BridgeItem *it, *next;
  if (!session)
    return;
  it = session->bridge_head;
  session->bridge_head = NULL;
  session->bridge_tail = NULL;
  while (it) {
    next = it->next;
    if (it->kind == BRIDGE_INT)
      su_signal_int_set(it->sig_int, it->int_value);
    else if (it->kind == BRIDGE_STR) {
      su_signal_str_set(it->sig_str, it->str_value);
      su_free(it->str_value);
    }
    su_free(it);
    it = next;
  }
}

void su_ui_unmount(SuUiSession *session) {
  if (!session)
    return;
  su_ui_bridge_flush(session);
  if (session->cfg.kind == SU_UI_RUNTIME_WINDOW)
    su_embedder_shutdown();
  if (session->surface)
    sk_surface_unref(session->surface);
  if (session->owns_view)
    su_view_free(session->root);
  su_free(session);
}

int su_ui_pump_sync(SuUiSession *session) {
  size_t nbytes = 0;
  const uint8_t *rgba;
  if (!session)
    return 0;
  /* UI-thread hop: apply signal writes posted from completed IO. */
  su_ui_bridge_flush(session);
  if (!su_view_paint(session->root, session->canvas, session->cfg.width,
                     session->cfg.height, session->theme))
    return 0;
  session->dirty = 0;
  /* Window peer: present to OS surface when embedder is available. */
  if (session->cfg.kind == SU_UI_RUNTIME_WINDOW && su_embedder_available()) {
    rgba = sk_surface_peek_pixels(session->surface, &nbytes);
    if (rgba) {
      su_embedder_present(session->cfg.title, session->cfg.width,
                          session->cfg.height, rgba, nbytes);
    }
  }
  return 1;
}

int su_ui_inject_sync(SuUiSession *session, const SuInputEvent *event) {
  if (!session || !event || !session->root)
    return 0;
  switch (event->kind) {
  case SU_INPUT_TAP:
    if (!su_view_handle_tap(session->root, event->x, event->y))
      return 0;
    session->dirty = 1;
    return 1;
  case SU_INPUT_TEXT:
    if (!su_view_handle_text(session->root, event->text))
      return 0;
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
  char *text_owned;
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
  e->text_owned = NULL;
  if (event.kind == SU_INPUT_TEXT && event.text) {
    e->text_owned = su_strdup(event.text);
    e->event.text = e->text_owned;
  }
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

SuView *su_ui_session_root(SuUiSession *session) {
  return session ? session->root : NULL;
}

const SuTheme *su_ui_session_theme(const SuUiSession *session) {
  return session ? session->theme : su_theme_default();
}

/* Shared resolution of headless size from args / env. */
void su_ui_resolve_headless_size(int *width, int *height, double *scale) {
  if (*width <= 0) {
    const char *w = getenv("SCALUI_UI_WIDTH");
    *width = (w && atoi(w) > 0) ? atoi(w) : 200;
  }
  if (*height <= 0) {
    const char *h = getenv("SCALUI_UI_HEIGHT");
    *height = (h && atoi(h) > 0) ? atoi(h) : 100;
  }
  if (scale) {
    const char *sc = getenv("SCALUI_UI_SCALE");
    *scale = (sc && atof(sc) > 0.0) ? atof(sc) : 1.0;
  }
}

void su_ui_demo_finish(SuUiSession *session) {
  const char *snap = getenv("SCALUI_SNAPSHOT_PATH");
  if (snap && snap[0]) {
    if (!su_ui_snapshot_png_sync(session, snap))
      su_panic("headless snapshot failed");
    fprintf(stderr, "scalui: wrote snapshot %s\n", snap);
  }
}
