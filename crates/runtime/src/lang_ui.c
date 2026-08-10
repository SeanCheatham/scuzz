#define _POSIX_C_SOURCE 200809L

#include "scalui_ui.h"
#include "scalui_embedder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* fill_cfg / demo_finish live in demos.c / ui.c */
void su_ui_resolve_headless_size(int *width, int *height, double *scale);
void su_ui_demo_finish(SuUiSession *session);

static void fill_cfg(SuUiConfig *cfg, int width, int height) {
  double scale = 1.0;
  const char *rt = getenv("SCALUI_UI_RUNTIME");
  memset(cfg, 0, sizeof(*cfg));
  cfg->kind = SU_UI_RUNTIME_HEADLESS;
  if (rt && (strcmp(rt, "window") == 0 || strcmp(rt, "Window") == 0))
    cfg->kind = SU_UI_RUNTIME_WINDOW;
  else if (rt && (strcmp(rt, "mobile") == 0 || strcmp(rt, "Mobile") == 0))
    cfg->kind = SU_UI_RUNTIME_MOBILE;
  cfg->width = width;
  cfg->height = height;
  cfg->title = "ScalUI";
  su_ui_resolve_headless_size(&cfg->width, &cfg->height, &scale);
  cfg->scale = scale;
}

/* --- Signal -------------------------------------------------------------- */

SuSignalInt *su_lang_signal_int(int64_t initial) { return su_signal_int(initial); }

int64_t su_lang_signal_get(SuSignalInt *s) { return su_signal_int_get(s); }

void *su_lang_signal_set(SuSignalInt *s, int64_t v) {
  su_signal_int_set(s, v);
  return NULL;
}

SuSignalStr *su_lang_signal_str(SuString *initial) {
  return su_signal_str(initial ? su_string_cstr(initial) : "");
}

SuString *su_lang_signal_str_get(SuSignalStr *s) {
  return su_string_from_cstr(su_signal_str_get(s));
}

void *su_lang_signal_str_set(SuSignalStr *s, SuString *v) {
  su_signal_str_set(s, v ? su_string_cstr(v) : "");
  return NULL;
}

SuSignalList *su_lang_signal_list(SuList *initial) { return su_signal_list(initial); }

SuList *su_lang_signal_list_get(SuSignalList *s) { return su_signal_list_get(s); }

void *su_lang_signal_list_set(SuSignalList *s, SuList *v) {
  su_signal_list_set(s, v);
  return NULL;
}

/* --- View builders ------------------------------------------------------- */

SuView *su_lang_view_text(SuString *text) {
  return su_view_text(text ? su_string_cstr(text) : "");
}

SuView *su_lang_view_text_signal(SuSignalInt *sig, SuString *prefix) {
  return su_view_text_signal_int(sig, prefix ? su_string_cstr(prefix) : "");
}

SuView *su_lang_view_button(SuString *label, SuViewTapFn tap, void *env) {
  return su_view_button(label ? su_string_cstr(label) : "", tap, env);
}

SuView *su_lang_view_column(void) { return su_view_column(); }

SuView *su_lang_view_row(void) { return su_view_row(); }

SuView *su_lang_view_list(void) { return su_view_list(); }

SuView *su_lang_view_scroll(SuView *child) { return su_view_scroll(child); }

SuView *su_lang_view_text_field(SuSignalStr *text, SuString *placeholder) {
  return su_view_text_field(text, placeholder ? su_string_cstr(placeholder) : "");
}

SuView *su_lang_view_icon(int64_t glyph, int64_t argb) {
  return su_view_icon((char)glyph, (uint32_t)argb);
}

SuView *su_lang_view_image(int64_t w, int64_t h, int64_t argb, SuString *caption) {
  return su_view_image((int)w, (int)h, (uint32_t)argb,
                       caption ? su_string_cstr(caption) : "");
}

void *su_lang_view_add_child(SuView *parent, SuView *child) {
  su_view_add_child(parent, child);
  return NULL;
}

void *su_lang_view_add_texts(SuView *parent, SuList *lines) {
  for (SuList *p = lines; p; p = p->tail) {
    SuString *s = (SuString *)p->head;
    char line[256];
    snprintf(line, sizeof line, "- %s", s ? su_string_cstr(s) : "");
    su_view_add_child(parent, su_view_text(line));
  }
  return NULL;
}

SuView *su_lang_view_show_when(SuSignalInt *sig, int64_t value, SuView *child) {
  return su_view_show_when(sig, value, child);
}

/* --- Ui.run -------------------------------------------------------------- */

typedef struct {
  SuView *root;
} RunViewEnv;

static void scripted_button_tap(SuUiSession *session, int prefer_upper) {
  SuInputEvent tap;
  SuView *r = su_ui_session_root(session);
  SuView *hit_btn = NULL;
  SuView *buttons[64];
  int n_buttons = 0;
  float tx = 40.f, ty = 60.f;
  int yi, xi;
  int w = su_ui_session_width(session);
  int h = su_ui_session_height(session);
  const char *tap_n_env = getenv("SCALUI_UI_TAP_N");
  int tap_n = (tap_n_env && tap_n_env[0]) ? atoi(tap_n_env) : -1;

  /* Collect unique buttons in top-to-bottom, left-to-right scan order. */
  for (yi = 0; yi < h; yi += 4) {
    for (xi = 0; xi < w; xi += 4) {
      SuView *hit = su_view_hit_test(r, (float)xi, (float)yi);
      if (hit && su_view_kind(hit) == SU_VIEW_BUTTON) {
        int seen = 0;
        int bi;
        for (bi = 0; bi < n_buttons; bi++) {
          if (buttons[bi] == hit) {
            seen = 1;
            break;
          }
        }
        if (!seen && n_buttons < 64)
          buttons[n_buttons++] = hit;
      }
    }
  }

  if (tap_n >= 0) {
    if (tap_n >= n_buttons)
      su_panic("Ui.run: SCALUI_UI_TAP_N out of range");
    hit_btn = buttons[tap_n];
  } else if (prefer_upper && n_buttons > 0) {
    int bi;
    hit_btn = buttons[0];
    for (bi = 1; bi < n_buttons; bi++) {
      if (su_view_frame(buttons[bi]).y < su_view_frame(hit_btn).y)
        hit_btn = buttons[bi];
    }
  } else if (n_buttons > 0) {
    hit_btn = buttons[0];
  }

  if (!hit_btn)
    su_panic("Ui.run: button not found for SCALUI_UI_TAP");
  {
    SuRect fr = su_view_frame(hit_btn);
    tx = fr.x + fr.w * 0.5f;
    ty = fr.y + fr.h * 0.5f;
  }
  memset(&tap, 0, sizeof(tap));
  tap.kind = SU_INPUT_TAP;
  tap.x = tx;
  tap.y = ty;
  {
    const char *sx = getenv("SCALUI_UI_TAP_X");
    const char *sy = getenv("SCALUI_UI_TAP_Y");
    if (sx)
      tap.x = (float)atof(sx);
    if (sy)
      tap.y = (float)atof(sy);
  }
  if (!su_ui_inject_sync(session, &tap) || !su_ui_pump_sync(session))
    su_panic("Ui.run tap/pump failed");
}

static void *thunk_run_view(void *env) {
  RunViewEnv *e = (RunViewEnv *)env;
  SuUiConfig cfg;
  SuUiSession *session;
  int interactive;
  int inject_text = 0;

  fill_cfg(&cfg, 0, 0);

  session = su_ui_mount(&cfg, e->root);
  if (!session)
    su_panic("Ui.run mount failed");
  su_ui_session_take_root(session);

  if (!su_ui_pump_sync(session))
    su_panic("Ui.run pump failed");

  if (getenv("SCALUI_UI_TAP")) {
    const char *seed = getenv("SCALUI_UI_TEXT");
    if (!seed || !seed[0])
      seed = getenv("SCALUI_TODO_SEED");
    if (seed && seed[0]) {
      SuInputEvent ev;
      memset(&ev, 0, sizeof(ev));
      ev.kind = SU_INPUT_TEXT;
      ev.text = seed;
      if (!su_ui_inject_sync(session, &ev))
        su_panic("Ui.run text inject failed");
      inject_text = 1;
    }
    scripted_button_tap(session, inject_text);
  }

  interactive = cfg.kind == SU_UI_RUNTIME_WINDOW && su_embedder_available();
  if (interactive) {
    const char *max_frames_env = getenv("SCALUI_LIVE_FRAMES");
    int64_t max_frames =
        (max_frames_env && atoi(max_frames_env) > 0) ? atoi(max_frames_env) : 0;
    int64_t frame = 0;
    do {
      if (!su_ui_pump_sync(session))
        su_panic("Ui.run live pump failed");
      frame++;
      if (max_frames > 0 && frame >= max_frames)
        break;
      {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 16000000L; /* ~60fps cap */
        nanosleep(&ts, NULL);
      }
    } while (su_embedder_alive());
  } else {
    su_ui_demo_finish(session);
  }

  su_ui_unmount(session);
  su_free(e);
  return NULL;
}

SuIo *su_ui_run_view(SuView *root) {
  RunViewEnv *e = (RunViewEnv *)su_alloc(sizeof(RunViewEnv));
  e->root = root;
  return su_io_delay(thunk_run_view, e);
}
