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
__attribute__((weak)) int sz_embedder_clipboard_set(const char *text) {
  (void)text;
  return 0;
}
__attribute__((weak)) char *sz_embedder_clipboard_get(void) { return NULL; }

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
__attribute__((weak)) int sz_mobile_clipboard_set(const char *text) {
  (void)text;
  return 0;
}
__attribute__((weak)) char *sz_mobile_clipboard_get(void) { return NULL; }

/* Implemented in view.c */
int sz_view_paint(SzView *root, SkCanvas *canvas, int width, int height,
                  const SzTheme *theme);
int sz_view_handle_tap(SzView *root, float x, float y);
int sz_view_handle_text(SzView *root, const char *text);
int sz_view_handle_text_edit(SzView *root, const char *text, int backspace);
int sz_view_handle_key(SzView *root, const char *key, const char *text,
                       int mods);
int sz_view_handle_compose(SzView *root, const char *text);

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
  int pointer_button;
  float pointer_x;
  float pointer_y;
  float pointer_down_x;
  float pointer_down_y;
  SzView *pointer_scroll;
  SzView *pointer_slider;
  SzView *pointer_field;
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
  int hover_seen;
  float hover_x;
  float hover_y;
  char *hover_desc;
  char *record_hover_desc;
  int last_secondary_seen;
  float last_secondary_x;
  float last_secondary_y;
  char *last_secondary_desc;
  char *clipboard;
  char *title_owned;
  void *code_handle;
  void *code_stale;
  int code_gen;
  unsigned pumps;
};

static SzUiSession *g_live_session;
static char *g_pending_title;

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
  {
    const char *t = cfg->title;
    if (!t || !t[0])
      t = g_pending_title;
    if (!t || !t[0])
      t = "Scuzz Lang";
    s->title_owned = sz_strdup(t);
    s->cfg.title = s->title_owned;
  }
  g_live_session = s;
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

/* Editor dump: keep newlines as \\n so a file buffer stays one node. */
static void fputs_dump_escaped(FILE *f, const char *s) {
  const char *p;
  fputc('"', f);
  if (s) {
    for (p = s; *p; p++) {
      unsigned char c = (unsigned char)*p;
      if (c == '\\')
        fputs("\\\\", f);
      else if (c == '"')
        fputs("\\\"", f);
      else if (c == '\n')
        fputs("\\n", f);
      else if (c == '\r')
        fputs("\\r", f);
      else if (c == '\t')
        fputs("\\t", f);
      else
        fputc(*p, f);
    }
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
                     ? sz_view_edit_target(session->root)
                     : NULL;
  for (i = 0; i < n_fields; i++) {
    fprintf(f, "%d%s ", i, fields[i] == field_target ? "*" : "");
    fputs_dump_label(f, sz_view_a11y_label(fields[i]));
    fputc('=', f);
    fputs_dump_quoted(f, sz_view_text_field_value(fields[i]));
    fprintf(f, " caret=%d sel=%d:%d", sz_view_text_field_caret(fields[i]),
            sz_view_text_field_sel_start(fields[i]),
            sz_view_text_field_sel_end(fields[i]));
    {
      const char *pre = sz_view_text_field_preedit(fields[i]);
      if (pre && pre[0]) {
        fputs(" preedit=", f);
        fputs_dump_quoted(f, pre);
      }
    }
    fputc('\n', f);
  }
  {
    SzView *editors[64];
    SzView *ed_target;
    int n_editors = (session && session->root)
                        ? sz_view_collect_editors(session->root, editors, 64)
                        : 0;
    if (n_editors > 0) {
      ed_target = sz_view_edit_target(session->root);
      fprintf(f, "\n[editor]\n");
      for (i = 0; i < n_editors; i++) {
        fprintf(f, "%d%s caret=%d sel=%d:%d sx=%.0f sy=%.0f lines=%d", i,
                editors[i] == ed_target ? "*" : "",
                sz_view_editor_caret(editors[i]),
                sz_view_editor_sel_start(editors[i]),
                sz_view_editor_sel_end(editors[i]),
                sz_view_editor_scroll_x(editors[i]),
                sz_view_editor_scroll_y(editors[i]),
                sz_view_editor_line_count(editors[i]));
        {
          int d, nd = sz_view_editor_diag_count(editors[i]);
          int nt = sz_view_editor_token_count(editors[i]);
          int ni = sz_view_editor_inlay_count(editors[i]);
          int nf = sz_view_editor_fold_count(editors[i]);
          if (nd > 0) {
            fputs(" diag=", f);
            for (d = 0; d < nd; d++) {
              if (d)
                fputc(',', f);
              fprintf(f, "%d:%d", sz_view_editor_diag_line(editors[i], d),
                      sz_view_editor_diag_severity(editors[i], d));
            }
          }
          if (nt > 0)
            fprintf(f, " tok=%d", nt);
          if (ni > 0)
            fprintf(f, " inlay=%d", ni);
          if (nf > 0)
            fprintf(f, " fold=%d", nf);
        }
        {
          const char *pre = sz_view_editor_preedit(editors[i]);
          if (pre && pre[0]) {
            fputs(" preedit=", f);
            fputs_dump_quoted(f, pre);
          }
        }
        fputc(' ', f);
        fputs_dump_escaped(f, sz_view_editor_value(editors[i]));
        fputc('\n', f);
      }
    }
  }
  {
    SzView *splits[64];
    int n_splits = (session && session->root)
                       ? sz_view_collect_splits(session->root, splits, 64)
                       : 0;
    if (n_splits > 0) {
      fprintf(f, "\n[splits]\n");
      for (i = 0; i < n_splits; i++)
        fprintf(f, "%d frac=%d\n", i, sz_view_split_frac(splits[i]));
    }
  }
  {
    SzView *overlays[64];
    SzView *top = NULL;
    int n_ov = (session && session->root)
                   ? sz_view_collect_overlays(session->root, overlays, 64)
                   : 0;
    int j;
    if (n_ov > 0) {
      for (j = n_ov - 1; j >= 0; j--) {
        if (sz_view_overlay_is_open(overlays[j])) {
          top = overlays[j];
          break;
        }
      }
      fprintf(f, "\n[overlays]\n");
      for (i = 0; i < n_ov; i++)
        fprintf(f, "%d%s open=%d\n", i, overlays[i] == top ? "*" : "",
                sz_view_overlay_is_open(overlays[i]));
    }
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
  if (session && session->hover_seen) {
    fprintf(f, "\n[hover]\nxy %.1f %.1f -> %s\n", session->hover_x,
            session->hover_y,
            session->hover_desc ? session->hover_desc : "NULL");
  }
  if (session && session->last_secondary_seen) {
    fprintf(f, "\n[last_secondary]\nxy %.1f %.1f -> %s\n",
            session->last_secondary_x, session->last_secondary_y,
            session->last_secondary_desc ? session->last_secondary_desc
                                         : "NULL");
  }
  if (session && session->debug_dump_path && path &&
      strcmp(path, session->debug_dump_path) == 0) {
    fprintf(f, "\n[session]\nkind=%s\nwidth=%d\nheight=%d\ntitle=%s\n"
               "focus=%s\nlifecycle=%s\n"
               "keyboard=%d\npumps=%u\n",
            runtime_kind_name(session->cfg.kind), session->cfg.width,
            session->cfg.height, sz_ui_session_title(session),
            session->root ? sz_view_focus_kind(session->root) : "none",
            lifecycle_name(session->lifecycle),
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
  if (g_live_session == session)
    g_live_session = NULL;
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
  sz_free(session->hover_desc);
  sz_free(session->record_hover_desc);
  sz_free(session->last_secondary_desc);
  sz_free(session->clipboard);
  sz_free(session->title_owned);
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
  case SZ_A11Y_EDITOR:
    role = "editor";
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

static void session_set_hover(SzUiSession *session, float x, float y,
                              SzView *tip) {
  char desc[256];
  if (!session)
    return;
  format_last_hit_desc(tip, desc, sizeof desc);
  session->hover_seen = 1;
  session->hover_x = x;
  session->hover_y = y;
  sz_free(session->hover_desc);
  session->hover_desc = sz_strdup(desc);
}

static void session_clear_hover(SzUiSession *session) {
  if (!session)
    return;
  session->hover_seen = 0;
  sz_free(session->hover_desc);
  session->hover_desc = NULL;
  if (session->root)
    sz_view_clear_hover(session->root);
}

static void session_set_last_secondary(SzUiSession *session, float x, float y,
                                       SzView *hit_if_fired) {
  char desc[256];
  if (!session)
    return;
  format_last_hit_desc(hit_if_fired, desc, sizeof desc);
  session->last_secondary_seen = 1;
  session->last_secondary_x = x;
  session->last_secondary_y = y;
  sz_free(session->last_secondary_desc);
  session->last_secondary_desc = sz_strdup(desc);
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

static int event_pointer_button(const SzInputEvent *ev) {
  if (ev && ev->pointer_button == 3)
    return 3;
  return 1;
}

static void record_secondary_or_xy(SzUiSession *session, FILE *f, float x,
                                   float y) {
  int idx = find_tap_index_at(session, x, y);
  if (idx >= 0)
    fprintf(f, "secondary %d\n", idx);
  else
    fprintf(f, "secondary %.1f %.1f\n", x, y);
}

static void session_set_clipboard(SzUiSession *session, const char *text) {
  if (!session)
    return;
  sz_free(session->clipboard);
  session->clipboard = sz_strdup(text ? text : "");
}

static void clipboard_os_set(const char *text) {
  if (sz_embedder_available())
    (void)sz_embedder_clipboard_set(text ? text : "");
  if (sz_mobile_available())
    (void)sz_mobile_clipboard_set(text ? text : "");
}

static void clipboard_os_pull(SzUiSession *session) {
  char *os = NULL;
  if (!session)
    return;
  if (sz_embedder_available())
    os = sz_embedder_clipboard_get();
  if (!os && sz_mobile_available())
    os = sz_mobile_clipboard_get();
  if (os) {
    session_set_clipboard(session, os);
    free(os);
  }
}

static char *field_sel_dup(SzView *field) {
  const char *s;
  int a, b, n;
  char *out;
  if (!field)
    return sz_strdup("");
  if (sz_view_kind(field) == SZ_VIEW_EDITOR) {
    s = sz_view_editor_value(field);
    a = sz_view_editor_sel_start(field);
    b = sz_view_editor_sel_end(field);
  } else {
    s = sz_view_text_field_value(field);
    a = sz_view_text_field_sel_start(field);
    b = sz_view_text_field_sel_end(field);
  }
  if (!s)
    s = "";
  n = (int)strlen(s);
  if (a < 0)
    a = 0;
  if (b > n)
    b = n;
  if (b < a)
    b = a;
  out = (char *)sz_alloc((size_t)(b - a) + 1);
  memcpy(out, s + a, (size_t)(b - a));
  out[b - a] = '\0';
  return out;
}

/* 0 none, 1 copy, 2 cut, 3 paste. Ctrl/Cmd + c/x/v. */
static int clipboard_chord(const char *key, int mods) {
  unsigned char c;
  if (!key || !key[0] || key[1] != '\0')
    return 0;
  if ((mods & (SZ_KEY_CTRL | SZ_KEY_CMD)) == 0)
    return 0;
  c = (unsigned char)key[0];
  if (c >= 'A' && c <= 'Z')
    c = (unsigned char)(c - 'A' + 'a');
  if (c == 'c')
    return 1;
  if (c == 'x')
    return 2;
  if (c == 'v')
    return 3;
  return 0;
}

/* 0 none; else a toolbar a11y label. Ctrl/Cmd + s/f/k/p; Ctrl/Cmd+Shift+f/p. */
static const char *app_chord_label(const char *key, int mods) {
  unsigned char c;
  if (!key || !key[0] || key[1] != '\0')
    return NULL;
  if ((mods & (SZ_KEY_CTRL | SZ_KEY_CMD)) == 0)
    return NULL;
  c = (unsigned char)key[0];
  if (c >= 'A' && c <= 'Z')
    c = (unsigned char)(c - 'A' + 'a');
  if (c == 's')
    return "Save";
  if (c == 'f')
    return (mods & SZ_KEY_SHIFT) ? "Format" : "Find";
  if (c == 'k')
    return "Hover";
  if (c == 'p')
    return (mods & SZ_KEY_SHIFT) ? "Palette" : "Complete";
  if (c == 'd')
    return "Def";
  return NULL;
}

static void record_clipboard_verb(SzUiSession *session, int op) {
  FILE *f;
  if (!session || !session->record_path || op < 1 || op > 3)
    return;
  f = fopen(session->record_path, "a");
  if (!f)
    return;
  if (op == 1)
    fputs("copy\n", f);
  else if (op == 2)
    fputs("cut\n", f);
  else if (session->clipboard && session->clipboard[0])
    fprintf(f, "paste %s\n", session->clipboard);
  else
    fputs("paste\n", f);
  fclose(f);
}

static void record_key_line(FILE *f, const SzInputEvent *ev) {
  int mods = ev->key_mods;
  fputs("key ", f);
  fputs(ev->key, f);
  if (mods & SZ_KEY_SHIFT)
    fputs("+shift", f);
  if (mods & SZ_KEY_CTRL)
    fputs("+ctrl", f);
  if (mods & SZ_KEY_CMD)
    fputs("+cmd", f);
  if (mods & SZ_KEY_ALT)
    fputs("+alt", f);
  if (ev->key_repeat)
    fputs("+repeat", f);
  if (ev->text && ev->text[0]) {
    fputc(' ', f);
    fputs(ev->text, f);
  }
  fputc('\n', f);
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
    if (!clipboard_chord(ev->key, ev->key_mods))
      record_key_line(f, ev);
  } else if (ev->kind == SZ_INPUT_COMPOSE) {
    if (ev->text && ev->text[0])
      fprintf(f, "compose %s\n", ev->text);
    else
      fputs("commit\n", f);
  } else if (ev->kind == SZ_INPUT_TEXT_EDIT) {
    if (!ev->text || !ev->text[0])
      fputs("backspace\n", f);
    else
      fprintf(f, "type %s\n", ev->text);
  } else if (ev->kind == SZ_INPUT_POINTER &&
             ev->pointer_phase == SZ_POINTER_MOVE && !session->pointer_down) {
    SzView *tip;
    char desc[256];
    if (session->root) {
      sz_view_layout(session->root, (float)session->cfg.width,
                     (float)session->cfg.height, session->theme);
      tip = sz_view_tooltip_at(session->root, ev->x, ev->y);
    } else
      tip = NULL;
    format_last_hit_desc(tip, desc, sizeof desc);
    if (!session->record_hover_desc ||
        strcmp(session->record_hover_desc, desc) != 0) {
      fprintf(f, "hover %.1f %.1f\n", ev->x, ev->y);
      sz_free(session->record_hover_desc);
      session->record_hover_desc = sz_strdup(desc);
    }
  } else if (ev->kind == SZ_INPUT_POINTER &&
             ev->pointer_phase == SZ_POINTER_UP && session->pointer_down) {
    float dx = ev->x - session->pointer_down_x;
    float dy = ev->y - session->pointer_down_y;
    if (session->pointer_button == 3 || ev->pointer_button == 3) {
      if (dx * dx + dy * dy <= 64.f)
        record_secondary_or_xy(session, f, ev->x, ev->y);
    } else if (session->pointer_field && dx * dx + dy * dy > 64.f)
      fprintf(f, "drag %.1f %.1f %.1f %.1f\n", session->pointer_down_x,
              session->pointer_down_y, ev->x, ev->y);
    else if (session->pointer_slider)
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
  int chord = 0;
  int ok;
  if (!session || !event)
    return 0;
  if (event->kind == SZ_INPUT_KEY)
    chord = clipboard_chord(event->key, event->key_mods);
  if (chord) {
    ok = sz_ui_inject_sync(session, event);
    record_clipboard_verb(session, chord);
    return ok;
  }
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
  if (session->hover_seen)
    sz_view_set_hover_at(session->root, session->hover_x, session->hover_y);
  else
    sz_view_clear_hover(session->root);
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
  /* Leak oracle: heap must not grow across consecutive idle pumps. A dirty
   * frame resets the baseline. */
  if (sz_testrt_oracles_armed()) {
    if (need_dump)
      sz_testrt_ui_idle_reset();
    else {
      sz_testrt_ui_idle_check();
      sz_testrt_ui_idle_snapshot();
    }
  }
  if (need_dump && session->debug_dump_path)
    sz_ui_session_write_dump(session, session->debug_dump_path);
  return 1;
}

static int inject_pointer(SzUiSession *session, const SzInputEvent *event) {
  float dx, dy;
  const float tap_slop2 = 64.f; /* 8px squared */
  int button = event_pointer_button(event);

  if (!session->root)
    return 0;

  /* Make frames current for hit / scroll targeting. */
  sz_view_layout(session->root, (float)session->cfg.width,
                 (float)session->cfg.height, session->theme);

  switch (event->pointer_phase) {
  case SZ_POINTER_DOWN: {
    SzView *hit = sz_view_hit_test(session->root, event->x, event->y);
    session->pointer_down = 1;
    session->pointer_button = button;
    session->pointer_x = event->x;
    session->pointer_y = event->y;
    session->pointer_down_x = event->x;
    session->pointer_down_y = event->y;
    session_clear_hover(session);
    session->pointer_field = NULL;
    if (button == 3) {
      session->pointer_slider = NULL;
      session->pointer_scroll = NULL;
    } else if (hit && sz_view_kind(hit) == SZ_VIEW_SLIDER) {
      session->pointer_slider = hit;
      session->pointer_scroll = NULL;
      sz_view_slider_set_at(hit, event->x);
    } else if (hit && sz_view_kind(hit) == SZ_VIEW_SPLIT) {
      session->pointer_slider = hit;
      session->pointer_scroll = NULL;
      sz_view_split_set_at(hit, event->x);
    } else if (hit && (sz_view_kind(hit) == SZ_VIEW_TEXT_FIELD ||
                      sz_view_kind(hit) == SZ_VIEW_EDITOR)) {
      session->pointer_slider = NULL;
      session->pointer_scroll = NULL;
      session->pointer_field = hit;
      (void)sz_view_handle_tap(session->root, event->x, event->y);
      sync_keyboard(session);
    } else {
      session->pointer_slider = NULL;
      session->pointer_scroll =
          sz_view_scroll_at(session->root, event->x, event->y);
    }
    session->dirty = 1;
    return 1;
  }
  case SZ_POINTER_MOVE:
    if (!session->pointer_down) {
      SzView *tip = sz_view_tooltip_at(session->root, event->x, event->y);
      (void)sz_view_set_hover_at(session->root, event->x, event->y);
      session_set_hover(session, event->x, event->y, tip);
      session->pointer_x = event->x;
      session->pointer_y = event->y;
      session->dirty = 1;
      return 1;
    }
    dx = event->x - session->pointer_x;
    dy = event->y - session->pointer_y;
    if (session->pointer_button == 3) {
      session->pointer_x = event->x;
      session->pointer_y = event->y;
      return 1;
    }
    if (session->pointer_slider) {
      if (sz_view_kind(session->pointer_slider) == SZ_VIEW_SPLIT)
        sz_view_split_set_at(session->pointer_slider, event->x);
      else
        sz_view_slider_set_at(session->pointer_slider, event->x);
      session->dirty = 1;
    } else if (session->pointer_field) {
      (void)sz_view_edit_extend_to_xy(session->pointer_field, event->x,
                                      event->y);
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
    if (button != session->pointer_button)
      return 1;
    dx = event->x - session->pointer_down_x;
    dy = event->y - session->pointer_down_y;
    session->pointer_down = 0;
    session->pointer_scroll = NULL;
    if (session->pointer_button == 3) {
      SzView *hit = NULL;
      if (dx * dx + dy * dy <= tap_slop2) {
        hit = sz_view_hit_test(session->root, event->x, event->y);
        session_set_last_secondary(session, event->x, event->y, hit);
        (void)sz_view_handle_secondary(session->root, event->x, event->y);
        session->dirty = 1;
      }
      session->pointer_slider = NULL;
      session->pointer_field = NULL;
      session->pointer_button = 0;
      return 1;
    }
    if (session->pointer_slider) {
      SzView *sl = session->pointer_slider;
      sz_view_slider_set_at(sl, event->x);
      session_set_last_hit(session, event->x, event->y, sl);
      session->pointer_slider = NULL;
      session->pointer_field = NULL;
      session->dirty = 1;
      return 1;
    }
    if (session->pointer_field) {
      if (dx * dx + dy * dy > tap_slop2)
        (void)sz_view_edit_extend_to_xy(session->pointer_field, event->x,
                                        event->y);
      else
        (void)sz_view_handle_tap(session->root, event->x, event->y);
      sync_keyboard(session);
      session->pointer_field = NULL;
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
    session->pointer_field = NULL;
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
  case SZ_INPUT_KEY: {
    int chord = clipboard_chord(event->key, event->key_mods);
    if (chord == 1) {
      if (!sz_ui_session_copy(session))
        return 0;
      return 1;
    }
    if (chord == 2) {
      if (!sz_ui_session_cut(session))
        return 0;
      return 1;
    }
    if (chord == 3) {
      if (!sz_ui_session_paste(session, NULL))
        return 0;
      return 1;
    }
    {
      const char *lab = app_chord_label(event->key, event->key_mods);
      if (lab) {
        sz_view_layout(session->root, (float)session->cfg.width,
                       (float)session->cfg.height, session->theme);
        (void)sz_view_tap_label(session->root, lab);
        session->dirty = 1;
        return 1;
      }
    }
    if (!sz_view_handle_key(session->root, event->key, event->text,
                            event->key_mods))
      return 0;
    sync_keyboard(session);
    session->dirty = 1;
    return 1;
  }
  case SZ_INPUT_COMPOSE:
    if (!sz_view_handle_compose(session->root, event->text))
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

int sz_ui_session_set_caret(SzUiSession *session, int index, int offset) {
  SzView *target;
  if (!session || !session->root)
    return 0;
  if (index >= 0) {
    if (!sz_view_focus_text_field_at(session->root, index))
      return 0;
    target = sz_view_text_field_target(session->root);
    if (!sz_view_set_text_field_caret(target, offset))
      return 0;
  } else {
    if (!sz_view_focus_edit_target(session->root))
      return 0;
    target = sz_view_edit_target(session->root);
    if (sz_view_kind(target) == SZ_VIEW_EDITOR) {
      if (!sz_view_set_editor_caret(target, offset))
        return 0;
    } else if (!sz_view_set_text_field_caret(target, offset))
      return 0;
  }
  session->dirty = 1;
  return 1;
}

int sz_ui_session_set_sel(SzUiSession *session, int index, int start, int end) {
  SzView *target;
  if (!session || !session->root)
    return 0;
  if (index >= 0) {
    if (!sz_view_focus_text_field_at(session->root, index))
      return 0;
    target = sz_view_text_field_target(session->root);
    if (!sz_view_set_text_field_sel(target, start, end))
      return 0;
  } else {
    if (!sz_view_focus_edit_target(session->root))
      return 0;
    target = sz_view_edit_target(session->root);
    if (sz_view_kind(target) == SZ_VIEW_EDITOR) {
      if (!sz_view_set_editor_sel(target, start, end))
        return 0;
    } else if (!sz_view_set_text_field_sel(target, start, end))
      return 0;
  }
  session->dirty = 1;
  return 1;
}

int sz_ui_session_copy(SzUiSession *session) {
  SzView *target;
  char *sel;
  if (!session || !session->root)
    return 0;
  target = sz_view_edit_target(session->root);
  if (!target)
    return 0;
  sel = field_sel_dup(target);
  if (sel[0]) {
    session_set_clipboard(session, sel);
    clipboard_os_set(sel);
  }
  sz_free(sel);
  session->dirty = 1;
  return 1;
}

int sz_ui_session_cut(SzUiSession *session) {
  SzView *target;
  SzInputEvent ev;
  int a, b;
  if (!sz_ui_session_copy(session))
    return 0;
  target = sz_view_edit_target(session->root);
  if (!target)
    return 1;
  if (sz_view_kind(target) == SZ_VIEW_EDITOR) {
    a = sz_view_editor_sel_start(target);
    b = sz_view_editor_sel_end(target);
  } else {
    a = sz_view_text_field_sel_start(target);
    b = sz_view_text_field_sel_end(target);
  }
  if (b <= a)
    return 1;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_KEY;
  ev.key = "Backspace";
  if (!sz_view_handle_key(session->root, ev.key, "", 0))
    return 0;
  sync_keyboard(session);
  session->dirty = 1;
  return 1;
}

int sz_ui_session_paste(SzUiSession *session, const char *text) {
  SzView *target;
  const char *payload;
  if (!session || !session->root)
    return 0;
  target = sz_view_edit_target(session->root);
  if (!target)
    return 0;
  if (text && text[0]) {
    session_set_clipboard(session, text);
    clipboard_os_set(text);
  } else
    clipboard_os_pull(session);
  payload = session->clipboard ? session->clipboard : "";
  if (!payload[0]) {
    session->dirty = 1;
    return 1;
  }
  if (!sz_view_handle_text_edit(session->root, payload, 0))
    return 0;
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

int sz_ui_session_set_title(SzUiSession *session, const char *title) {
  char *n;
  if (!session)
    return 0;
  n = sz_strdup(title && title[0] ? title : "Scuzz Lang");
  sz_free(session->title_owned);
  session->title_owned = n;
  session->cfg.title = n;
  session->dirty = 1;
  return 1;
}

const char *sz_ui_session_title(const SzUiSession *session) {
  if (!session || !session->cfg.title || !session->cfg.title[0])
    return "Scuzz Lang";
  return session->cfg.title;
}

const char *sz_ui_default_title(void) {
  if (g_pending_title && g_pending_title[0])
    return g_pending_title;
  return "Scuzz Lang";
}

static void *thunk_set_title(void *env) {
  SzString *s = (SzString *)env;
  const char *t = s ? sz_string_cstr(s) : "";
  if (g_live_session)
    sz_ui_session_set_title(g_live_session, t);
  else {
    sz_free(g_pending_title);
    g_pending_title = sz_strdup(t && t[0] ? t : "Scuzz Lang");
  }
  return NULL;
}

SzIo *sz_lang_ui_set_title(SzString *title) {
  if (!title)
    sz_panic("Ui.setTitle(null)");
  return sz_io_delay(thunk_set_title, title);
}

static SzView *live_first_editor(void) {
  SzView *eds[64];
  int n;
  if (!g_live_session || !g_live_session->root)
    return NULL;
  n = sz_view_collect_editors(g_live_session->root, eds, 64);
  return n > 0 ? eds[0] : NULL;
}

static void *thunk_set_editor_caret(void *env) {
  SzPair *p = (SzPair *)env;
  int64_t line = p ? sz_unbox_i64(p->left) : 1;
  int64_t col = p && p->right ? sz_unbox_i64(p->right) : 1;
  SzView *ed = live_first_editor();
  int off;
  if (!ed)
    return NULL;
  off = sz_view_editor_offset_at_line_col(ed, (int)line, (int)col);
  sz_view_set_editor_caret(ed, off);
  if (g_live_session)
    g_live_session->dirty = 1;
  return NULL;
}

SzIo *sz_lang_ui_set_editor_caret(int64_t line, int64_t col) {
  void *lb = sz_box_i64(line);
  void *cb = sz_box_i64(col);
  SzPair *p = sz_pair_new(lb, cb);
  SzIo *io;
  sz_release(lb);
  sz_release(cb);
  io = sz_io_delay(thunk_set_editor_caret, p);
  sz_release(p);
  return io;
}

static void *thunk_set_editor_diagnostics(void *env) {
  SzList *marks = (SzList *)env;
  SzView *ed = live_first_editor();
  int n;
  int i;
  int *lines;
  int *sevs;
  SzList *p;
  if (!ed)
    return NULL;
  n = (int)sz_list_len(marks);
  if (n <= 0) {
    sz_view_editor_set_diagnostics(ed, NULL, NULL, 0);
    if (g_live_session)
      g_live_session->dirty = 1;
    return NULL;
  }
  lines = (int *)sz_alloc(sizeof(int) * (size_t)n);
  sevs = (int *)sz_alloc(sizeof(int) * (size_t)n);
  p = marks;
  for (i = 0; i < n && p && !sz_list_is_empty(p); i++) {
    SzPair *cell = (SzPair *)sz_list_head(p);
    lines[i] = cell ? (int)sz_unbox_i64(cell->left) : 1;
    sevs[i] = cell && cell->right ? (int)sz_unbox_i64(cell->right) : 1;
    if (lines[i] < 1)
      lines[i] = 1;
    if (sevs[i] < 1)
      sevs[i] = 1;
    p = sz_list_tail(p);
  }
  sz_view_editor_set_diagnostics(ed, lines, sevs, n);
  sz_free(lines);
  sz_free(sevs);
  if (g_live_session)
    g_live_session->dirty = 1;
  return NULL;
}

SzIo *sz_lang_ui_set_editor_diagnostics(SzList *marks) {
  /* Nil is NULL. Check JSON `[]` must clear marks, not panic. */
  return sz_io_delay(thunk_set_editor_diagnostics, marks);
}

static void *thunk_set_editor_tokens(void *env) {
  SzList *data = (SzList *)env;
  SzView *ed = live_first_editor();
  int n;
  int i;
  int *vals;
  SzList *p;
  if (!ed)
    return NULL;
  n = (int)sz_list_len(data);
  if (n <= 0) {
    sz_view_editor_set_tokens(ed, NULL, 0);
    if (g_live_session)
      g_live_session->dirty = 1;
    return NULL;
  }
  vals = (int *)sz_alloc(sizeof(int) * (size_t)n);
  p = data;
  for (i = 0; i < n && p && !sz_list_is_empty(p); i++) {
    vals[i] = (int)sz_unbox_i64(sz_list_head(p));
    p = sz_list_tail(p);
  }
  sz_view_editor_set_tokens(ed, vals, n);
  sz_free(vals);
  if (g_live_session)
    g_live_session->dirty = 1;
  return NULL;
}

SzIo *sz_lang_ui_set_editor_tokens(SzList *data) {
  return sz_io_delay(thunk_set_editor_tokens, data);
}

static void *thunk_set_editor_inlays(void *env) {
  SzList *hints = (SzList *)env;
  SzView *ed = live_first_editor();
  int n;
  int i;
  int *lines;
  int *cols;
  const char **labels;
  char **owned;
  SzList *p;
  if (!ed)
    return NULL;
  n = (int)sz_list_len(hints);
  if (n <= 0) {
    sz_view_editor_set_inlays(ed, NULL, NULL, NULL, 0);
    if (g_live_session)
      g_live_session->dirty = 1;
    return NULL;
  }
  lines = (int *)sz_alloc(sizeof(int) * (size_t)n);
  cols = (int *)sz_alloc(sizeof(int) * (size_t)n);
  labels = (const char **)sz_alloc(sizeof(char *) * (size_t)n);
  owned = (char **)sz_alloc(sizeof(char *) * (size_t)n);
  p = hints;
  for (i = 0; i < n && p && !sz_list_is_empty(p); i++) {
    SzPair *cell = (SzPair *)sz_list_head(p);
    SzPair *rest = cell ? (SzPair *)cell->right : NULL;
    SzString *lab = rest ? (SzString *)rest->right : NULL;
    lines[i] = cell ? (int)sz_unbox_i64(cell->left) : 0;
    cols[i] = rest ? (int)sz_unbox_i64(rest->left) : 0;
    owned[i] = sz_strdup(lab ? sz_string_cstr(lab) : "");
    labels[i] = owned[i];
    p = sz_list_tail(p);
  }
  sz_view_editor_set_inlays(ed, lines, cols, labels, n);
  for (i = 0; i < n; i++)
    sz_free(owned[i]);
  sz_free(owned);
  sz_free(lines);
  sz_free(cols);
  sz_free(labels);
  if (g_live_session)
    g_live_session->dirty = 1;
  return NULL;
}

SzIo *sz_lang_ui_set_editor_inlays(SzList *hints) {
  return sz_io_delay(thunk_set_editor_inlays, hints);
}

static void *thunk_set_editor_folds(void *env) {
  SzList *ranges = (SzList *)env;
  SzView *ed = live_first_editor();
  int n;
  int i;
  int *starts;
  int *ends;
  SzList *p;
  if (!ed)
    return NULL;
  n = (int)sz_list_len(ranges);
  if (n <= 0) {
    sz_view_editor_set_folds(ed, NULL, NULL, 0);
    if (g_live_session)
      g_live_session->dirty = 1;
    return NULL;
  }
  starts = (int *)sz_alloc(sizeof(int) * (size_t)n);
  ends = (int *)sz_alloc(sizeof(int) * (size_t)n);
  p = ranges;
  for (i = 0; i < n && p && !sz_list_is_empty(p); i++) {
    SzPair *cell = (SzPair *)sz_list_head(p);
    starts[i] = cell ? (int)sz_unbox_i64(cell->left) : 0;
    ends[i] = cell && cell->right ? (int)sz_unbox_i64(cell->right) : starts[i];
    p = sz_list_tail(p);
  }
  sz_view_editor_set_folds(ed, starts, ends, n);
  sz_free(starts);
  sz_free(ends);
  if (g_live_session)
    g_live_session->dirty = 1;
  return NULL;
}

SzIo *sz_lang_ui_set_editor_folds(SzList *ranges) {
  return sz_io_delay(thunk_set_editor_folds, ranges);
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
