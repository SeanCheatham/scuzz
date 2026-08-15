#define _POSIX_C_SOURCE 200809L

#include "scuzz_ui.h"
#include "scuzz_embedder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fill_cfg(SzUiConfig *cfg, int width, int height) {
  double scale = 1.0;
  const char *rt = getenv("SCUZZ_UI_RUNTIME");
  memset(cfg, 0, sizeof(*cfg));
  cfg->kind = SZ_UI_RUNTIME_HEADLESS;
  if (rt && (strcmp(rt, "desktop") == 0 || strcmp(rt, "Desktop") == 0))
    cfg->kind = SZ_UI_RUNTIME_DESKTOP;
  else if (rt && (strcmp(rt, "mobile") == 0 || strcmp(rt, "Mobile") == 0))
    cfg->kind = SZ_UI_RUNTIME_MOBILE;
  cfg->width = width;
  cfg->height = height;
  cfg->title = "Scuzz Lang";
  sz_ui_resolve_headless_size(&cfg->width, &cfg->height, &scale);
  cfg->scale = scale;
}

/* --- Signal -------------------------------------------------------------- */

SzSignalInt *sz_lang_signal_int(int64_t initial) { return sz_signal_int(initial); }

int64_t sz_lang_signal_get(SzSignalInt *s) { return sz_signal_int_get(s); }

void *sz_lang_signal_set(SzSignalInt *s, int64_t v) {
  sz_signal_int_set(s, v);
  return NULL;
}

SzSignalStr *sz_lang_signal_str(SzString *initial) {
  return sz_signal_str(initial ? sz_string_cstr(initial) : "");
}

SzString *sz_lang_signal_str_get(SzSignalStr *s) {
  return sz_string_from_cstr(sz_signal_str_get(s));
}

void *sz_lang_signal_str_set(SzSignalStr *s, SzString *v) {
  sz_signal_str_set(s, v ? sz_string_cstr(v) : "");
  return NULL;
}

SzSignalList *sz_lang_signal_list(SzList *initial) { return sz_signal_list(initial); }

SzList *sz_lang_signal_list_get(SzSignalList *s) { return sz_signal_list_get(s); }

void *sz_lang_signal_list_set(SzSignalList *s, SzList *v) {
  sz_signal_list_set(s, v);
  return NULL;
}

/* --- View builders ------------------------------------------------------- */

SzView *sz_lang_view_text(SzString *text) {
  return sz_view_text(text ? sz_string_cstr(text) : "");
}

SzView *sz_lang_view_button(SzString *label, SzViewTapFn tap, void *env) {
  return sz_view_button(label ? sz_string_cstr(label) : "", tap, env);
}

SzView *sz_lang_view_icon_button(SzString *label, SzViewTapFn tap, void *env) {
  return sz_view_icon_button(label ? sz_string_cstr(label) : "", tap, env);
}

SzView *sz_lang_view_fab(SzString *label, SzViewTapFn tap, void *env) {
  return sz_view_fab(label ? sz_string_cstr(label) : "", tap, env);
}

SzView *sz_lang_view_outlined_button(SzString *label, SzViewTapFn tap,
                                    void *env) {
  return sz_view_outlined_button(label ? sz_string_cstr(label) : "", tap, env);
}

SzView *sz_lang_view_text_button(SzString *label, SzViewTapFn tap, void *env) {
  return sz_view_text_button(label ? sz_string_cstr(label) : "", tap, env);
}

SzView *sz_lang_view_vertical_divider(void) { return sz_view_vertical_divider(); }

SzView *sz_lang_view_checkbox(SzSignalInt *sig, SzString *label) {
  return sz_view_checkbox(sig, label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_radio(SzSignalInt *sig, int64_t value, SzString *label) {
  return sz_view_radio(sig, value, label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_slider(SzSignalInt *sig) { return sz_view_slider(sig); }

SzView *sz_lang_view_progress(SzSignalInt *sig) { return sz_view_progress(sig); }

SzView *sz_lang_view_circular_progress(SzSignalInt *sig) {
  return sz_view_circular_progress(sig);
}

SzView *sz_lang_view_avatar(SzString *label) {
  return sz_view_avatar(label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_switch(SzSignalInt *sig, SzString *label) {
  return sz_view_switch(sig, label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_chip(SzSignalInt *sig, SzString *label) {
  return sz_view_chip(sig, label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_list_tile(SzString *title, SzView *trailing) {
  return sz_view_list_tile(title ? sz_string_cstr(title) : "", trailing);
}

SzView *sz_lang_view_checkbox_list_tile(SzSignalInt *sig, SzString *title) {
  return sz_view_checkbox_list_tile(sig, title ? sz_string_cstr(title) : "");
}

SzView *sz_lang_view_switch_list_tile(SzSignalInt *sig, SzString *title) {
  return sz_view_switch_list_tile(sig, title ? sz_string_cstr(title) : "");
}

SzView *sz_lang_view_radio_list_tile(SzSignalInt *sig, int64_t value,
                                    SzString *title) {
  return sz_view_radio_list_tile(sig, value, title ? sz_string_cstr(title) : "");
}

SzView *sz_lang_view_segmented(SzSignalInt *sig, SzString *left, SzString *right) {
  return sz_view_segmented(sig, left ? sz_string_cstr(left) : "",
                           right ? sz_string_cstr(right) : "");
}

SzView *sz_lang_view_badge(SzSignalInt *sig, SzView *child) {
  return sz_view_badge(sig, child);
}

SzView *sz_lang_view_card(SzView *child) { return sz_view_card(child); }

SzView *sz_lang_view_tooltip(SzString *message, SzView *child) {
  return sz_view_tooltip(message ? sz_string_cstr(message) : "", child);
}

SzView *sz_lang_view_divider(void) { return sz_view_divider(); }

SzView *sz_lang_view_expansion_tile(SzSignalInt *sig, SzString *title,
                                   SzView *child) {
  return sz_view_expansion_tile(sig, title ? sz_string_cstr(title) : "", child);
}

SzView *sz_lang_view_column(void) { return sz_view_column(); }

SzView *sz_lang_view_row(void) { return sz_view_row(); }

SzView *sz_lang_view_wrap(void) { return sz_view_wrap(); }
SzView *sz_lang_view_grid(int64_t cols) { return sz_view_grid((int)cols); }

SzView *sz_lang_view_stack(void) { return sz_view_stack(); }

SzView *sz_lang_view_each(SzSignalList *sig) { return sz_view_each(sig); }
SzView *sz_lang_view_each_map(SzSignalList *sig, SzViewEachFn fn, void *env) {
  return sz_view_each_map(sig, fn, env);
}

SzView *sz_lang_view_scroll(SzView *child) { return sz_view_scroll(child); }
SzView *sz_lang_view_scroll_h(SzView *child) { return sz_view_scroll_h(child); }
SzView *sz_lang_view_expanded(SzView *child) { return sz_view_expanded(child); }
SzView *sz_lang_view_stretch(SzView *child) { return sz_view_stretch(child); }
SzView *sz_lang_view_center(SzView *child) { return sz_view_center(child); }
SzView *sz_lang_view_align(int64_t ax, int64_t ay, SzView *child) {
  return sz_view_align((int)ax, (int)ay, child);
}
SzView *sz_lang_view_positioned(int64_t x, int64_t y, SzView *child) {
  return sz_view_positioned((int)x, (int)y, child);
}
SzView *sz_lang_view_padding(int64_t pad, SzView *child) {
  return sz_view_padding((int)pad, child);
}
SzView *sz_lang_view_sized(int64_t w, int64_t h, SzView *child) {
  return sz_view_sized((int)w, (int)h, child);
}
SzView *sz_lang_view_min_size(int64_t w, int64_t h, SzView *child) {
  return sz_view_min_size((int)w, (int)h, child);
}
SzView *sz_lang_view_max_size(int64_t w, int64_t h, SzView *child) {
  return sz_view_max_size((int)w, (int)h, child);
}
SzView *sz_lang_view_clip(SzView *child) { return sz_view_clip(child); }
SzView *sz_lang_view_opacity(int64_t pct, SzView *child) {
  return sz_view_opacity((int)pct, child);
}
SzView *sz_lang_view_max_lines(int64_t n, SzView *child) {
  return sz_view_max_lines((int)n, child);
}
SzView *sz_lang_view_ignore_pointer(SzView *child) {
  return sz_view_ignore_pointer(child);
}
SzView *sz_lang_view_absorb_pointer(SzView *child) {
  return sz_view_absorb_pointer(child);
}
SzView *sz_lang_view_exclude_semantics(SzView *child) {
  return sz_view_exclude_semantics(child);
}
SzView *sz_lang_view_ellipsis(SzView *child) { return sz_view_ellipsis(child); }
SzView *sz_lang_view_text_color(int64_t argb, SzView *child) {
  return sz_view_text_color((uint32_t)argb, child);
}
SzView *sz_lang_view_gap(int64_t n, SzView *child) {
  return sz_view_gap((int)n, child);
}
SzView *sz_lang_view_font_size(int64_t n, SzView *child) {
  return sz_view_font_size((int)n, child);
}
SzView *sz_lang_view_border(int64_t n, int64_t argb, SzView *child) {
  return sz_view_border((int)n, (uint32_t)argb, child);
}
SzView *sz_lang_view_radius(int64_t n, SzView *child) {
  return sz_view_radius((int)n, child);
}
SzView *sz_lang_view_background(int64_t argb, SzView *child) {
  return sz_view_background((uint32_t)argb, child);
}
SzView *sz_lang_view_aspect_ratio(int64_t rw, int64_t rh, SzView *child) {
  return sz_view_aspect_ratio((int)rw, (int)rh, child);
}
SzView *sz_lang_view_fraction(int64_t wpct, int64_t hpct, SzView *child) {
  return sz_view_fraction((int)wpct, (int)hpct, child);
}

SzView *sz_lang_view_text_field(SzSignalStr *text, SzString *placeholder) {
  return sz_view_text_field(text, placeholder ? sz_string_cstr(placeholder) : "");
}

SzView *sz_lang_view_icon(int64_t glyph, int64_t argb) {
  return sz_view_icon((char)glyph, (uint32_t)argb);
}

SzView *sz_lang_view_image(int64_t w, int64_t h, int64_t argb, SzString *caption) {
  return sz_view_image((int)w, (int)h, (uint32_t)argb,
                       caption ? sz_string_cstr(caption) : "");
}

void *sz_lang_view_add_child(SzView *parent, SzView *child) {
  sz_view_add_child(parent, child);
  return NULL;
}

SzView *sz_lang_view_show_when(SzSignalInt *sig, int64_t value, SzView *child) {
  return sz_view_show_when(sig, value, child);
}

SzView *sz_lang_view_bind_text(SzSignalStr *sig) { return sz_view_text_signal_str(sig); }

/* --- Ui.run -------------------------------------------------------------- */

typedef struct {
  SzUiRebuildFn rebuild;
  void *env;
} RunRebuildEnv;

/* Collect unique buttons in top-to-bottom, left-to-right scan order.
   Frames must be current (run after a pump). */
static int collect_buttons(SzUiSession *session, SzView **buttons, int cap) {
  SzView *r = sz_ui_session_root(session);
  int n_buttons = 0;
  int yi, xi;
  int w = sz_ui_session_width(session);
  int h = sz_ui_session_height(session);
  for (yi = 0; yi < h; yi += 4) {
    for (xi = 0; xi < w; xi += 4) {
      SzView *hit = sz_view_hit_test(r, (float)xi, (float)yi);
      if (hit && sz_view_is_tap_target(hit)) {
        int seen = 0;
        int bi;
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
  }
  return n_buttons;
}

static int collect_scrolls(SzUiSession *session, SzView **scrolls, int cap) {
  SzView *r = sz_ui_session_root(session);
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

static void script_parse_scroll(const char *rest, int *index, float *dy) {
  const char *p = rest ? rest : "";
  int a = 0;
  *index = -1;
  *dy = 40.f;
  while (*p == ' ')
    p++;
  if (!*p)
    return;
  if (*p == '-') {
    *dy = (float)atoi(p);
    return;
  }
  if (*p < '0' || *p > '9')
    return;
  while (*p >= '0' && *p <= '9') {
    a = a * 10 + (*p - '0');
    p++;
  }
  while (*p == ' ')
    p++;
  if (*p) {
    *index = a;
    *dy = (float)atoi(p);
    return;
  }
  *dy = (float)a;
}

static void script_scroll(SzUiSession *session, int index, float dy) {
  SzView *scrolls[64];
  int count = collect_scrolls(session, scrolls, 64);
  SzInputEvent ev;
  SzRect fr;
  int n = index < 0 ? 0 : index;
  if (count <= 0 || n >= count) {
    if (index < 0)
      fprintf(stderr, "scuzz: script scroll skipped (no scroll)\n");
    else
      fprintf(stderr, "scuzz: script scroll %d skipped (%d scrolls)\n", n, count);
    return;
  }
  fr = sz_view_frame(scrolls[n]);
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_SCROLL;
  ev.x = fr.x + fr.w * 0.5f;
  ev.y = fr.y + fr.h * 0.5f;
  ev.dy = dy;
  if (!sz_ui_inject_sync(session, &ev))
    fprintf(stderr, "scuzz: script scroll skipped (no scroll)\n");
}

/* `N payload` → index N and payload; otherwise index -1 and rest unchanged
 * so `text 0` still means replace-with-"0" on the starred field. */
static const char *script_field_payload(const char *rest, int *index) {
  const char *p = rest ? rest : "";
  int n = 0;
  *index = -1;
  if (*p < '0' || *p > '9')
    return p;
  while (*p >= '0' && *p <= '9') {
    n = n * 10 + (*p - '0');
    p++;
  }
  if (*p == ' ') {
    *index = n;
    return p + 1;
  }
  return rest ? rest : "";
}

static void script_parse_backspace(const char *rest, int *index, int *count) {
  const char *p = rest ? rest : "";
  int a = 0, b = 0;
  *index = -1;
  *count = 1;
  if (*p < '0' || *p > '9')
    return;
  while (*p >= '0' && *p <= '9') {
    a = a * 10 + (*p - '0');
    p++;
  }
  if (*p == ' ') {
    p++;
    if (*p >= '0' && *p <= '9') {
      while (*p >= '0' && *p <= '9') {
        b = b * 10 + (*p - '0');
        p++;
      }
      *index = a;
      *count = b < 1 ? 1 : b;
      return;
    }
  }
  *count = a < 1 ? 1 : a;
}

static int script_focus_field(SzUiSession *session, int index) {
  if (session && sz_view_focus_text_field_at(sz_ui_session_root(session), index))
    return 1;
  if (index < 0)
    fprintf(stderr, "scuzz: script skipped (no text field)\n");
  else
    fprintf(stderr, "scuzz: script field %d skipped\n", index);
  return 0;
}

static void script_backspace(SzUiSession *session, int index, int n) {
  SzInputEvent ev;
  if (n < 1)
    n = 1;
  if (!script_focus_field(session, index))
    return;
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

static void script_type(SzUiSession *session, int index, const char *text) {
  SzInputEvent ev;
  if (!text || !text[0])
    return;
  if (!script_focus_field(session, index))
    return;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = text;
  if (!sz_ui_inject_sync(session, &ev))
    fprintf(stderr, "scuzz: script type skipped (no text field)\n");
}

static void scripted_button_tap(SzUiSession *session, int prefer_upper) {
  SzInputEvent tap;
  SzView *hit_btn = NULL;
  SzView *buttons[64];
  int n_buttons;
  float tx = 40.f, ty = 60.f;
  const char *tap_n_env = getenv("SCUZZ_UI_TAP_N");
  int tap_n = (tap_n_env && tap_n_env[0]) ? atoi(tap_n_env) : -1;

  n_buttons = collect_buttons(session, buttons, 64);

  if (tap_n >= 0) {
    if (tap_n >= n_buttons)
      sz_panic("Ui.run: SCUZZ_UI_TAP_N out of range");
    hit_btn = buttons[tap_n];
  } else if (prefer_upper && n_buttons > 0) {
    int bi;
    hit_btn = buttons[0];
    for (bi = 1; bi < n_buttons; bi++) {
      if (sz_view_frame(buttons[bi]).y < sz_view_frame(hit_btn).y)
        hit_btn = buttons[bi];
    }
  } else if (n_buttons > 0) {
    hit_btn = buttons[0];
  }

  if (!hit_btn)
    sz_panic("Ui.run: button not found for SCUZZ_UI_TAP");
  {
    SzRect fr = sz_view_frame(hit_btn);
    tx = fr.x + fr.w * 0.5f;
    ty = fr.y + fr.h * 0.5f;
  }
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = tx;
  tap.y = ty;
  if (!sz_ui_inject_sync(session, &tap) || !sz_ui_pump_sync(session))
    sz_panic("Ui.run tap/pump failed");
}

/* --- SCUZZ_UI_SCRIPT playback (fuzz / replay) ---------------------------- */
/* Line protocol, one event per line, delivered across pump boundaries:
     tap <n>    tap the nth button, checkbox, radio, switch, chip, expansion, icon button, fab, outlined button, text button, checkbox list tile, switch list tile, radio list tile, or segmented (scan order; [taps] in the dump); missing target is a no-op
     xy <x> <y> inject TAP at logical point; miss does not panic
     text <s>   replace the [fields] starred TextField with <s>; no field is a no-op
     text <n> <s>  replace dump-index n (a11y order); `text 0` is still payload "0"
     type <s>   append <s> to the [fields] starred TextField; empty is a no-op; no field is a no-op
     type <n> <s>  append to dump-index n; `type 0` is still payload "0"
     pump <k>   pump k extra frames
     scroll <dy> pan the first Scroll on its axis (positive = content up or left); no scroll is a no-op
     scroll <n> <dy>  pan dump-index n ([scrolls] scan order); `scroll 40` stays dy 40
     backspace <n> chop n bytes from the [fields] starred TextField (default 1); no field is a no-op
     backspace <n> <k>  chop k bytes from dump-index n
     drive <name> [args]  run a verify-graph driver (Int/String/Bool args)
   Blank lines and #-comments are skipped. Pump runs after every event. */

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
  /* Clamp into the session viewport so partially-offscreen controls still tap. */
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

static void script_xy(SzUiSession *session, float x, float y) {
  SzInputEvent tap;
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = x;
  tap.y = y;
  if (!sz_ui_inject_sync(session, &tap))
    sz_panic("Ui.run: script xy inject failed");
}

static void run_ui_script(SzUiSession *session, const char *path) {
  FILE *f = fopen(path, "r");
  char line[1024];
  if (!f)
    sz_panic("Ui.run: SCUZZ_UI_SCRIPT open failed");
  while (fgets(line, sizeof line, f)) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';
    if (len == 0 || line[0] == '#')
      continue;
    if (strncmp(line, "tap ", 4) == 0 || strcmp(line, "tap") == 0) {
      script_tap(session, len > 3 ? atoi(line + 4) : 0);
    } else if (strncmp(line, "xy ", 3) == 0) {
      float x = 0.f, y = 0.f;
      if (sscanf(line + 3, "%f %f", &x, &y) == 2)
        script_xy(session, x, y);
      else {
        fclose(f);
        sz_panic("Ui.run: xy needs x y");
      }
    } else if (strncmp(line, "text ", 5) == 0 || strcmp(line, "text") == 0) {
      SzInputEvent ev;
      int idx;
      const char *payload = script_field_payload(len > 4 ? line + 5 : "", &idx);
      memset(&ev, 0, sizeof(ev));
      ev.kind = SZ_INPUT_TEXT;
      ev.text = payload;
      if (script_focus_field(session, idx)) {
        if (!sz_ui_inject_sync(session, &ev))
          fprintf(stderr, "scuzz: script text skipped (no text field)\n");
      }
    } else if (strncmp(line, "pump ", 5) == 0 || strcmp(line, "pump") == 0) {
      int k = len > 5 ? atoi(line + 5) : 1;
      while (k-- > 1) {
        if (!sz_ui_pump_sync(session))
          sz_panic("Ui.run: script pump failed");
      }
    } else if (strncmp(line, "scroll ", 7) == 0 || strcmp(line, "scroll") == 0) {
      int idx;
      float dy;
      script_parse_scroll(len > 6 ? line + 7 : "", &idx, &dy);
      script_scroll(session, idx, dy);
    } else if (strncmp(line, "backspace ", 10) == 0 ||
               strcmp(line, "backspace") == 0) {
      int idx, n;
      script_parse_backspace(len > 9 ? line + 10 : "", &idx, &n);
      script_backspace(session, idx, n);
    } else if (strncmp(line, "type ", 5) == 0 || strcmp(line, "type") == 0) {
      int idx;
      const char *payload = script_field_payload(len > 4 ? line + 5 : "", &idx);
      script_type(session, idx, payload);
    } else if (strncmp(line, "drive ", 6) == 0) {
      sz_driver_run_line(line + 6);
    } else {
      fclose(f);
      sz_panic("Ui.run: unknown SCUZZ_UI_SCRIPT directive");
    }
    if (!sz_ui_pump_sync(session))
      sz_panic("Ui.run: script pump failed");
  }
  fclose(f);
}

static SzView *constant_root(void *env) { return (SzView *)env; }

static void *thunk_run_rebuild(void *env) {
  RunRebuildEnv *e = (RunRebuildEnv *)env;
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  const char *stamp;
  int interactive;
  int inject_text = 0;

  fill_cfg(&cfg, 0, 0);

  if (!e->rebuild)
    sz_panic("Ui.run rebuild missing");
  root = e->rebuild(e->env);
  if (!root)
    sz_panic("Ui.run rebuild returned null");

  session = sz_ui_mount(&cfg, root);
  if (!session)
    sz_panic("Ui.run mount failed");
  sz_ui_session_take_root(session);
  sz_ui_session_set_rebuild(session, e->rebuild, e->env);
  stamp = getenv("SCUZZ_UI_RELOAD_STAMP");
  if (stamp && stamp[0])
    sz_ui_session_watch(session, stamp);
  {
    const char *dump = getenv("SCUZZ_UI_DEBUG_DUMP");
    if (dump && dump[0])
      sz_ui_session_set_debug_dump(session, dump);
  }
  {
    const char *inject = getenv("SCUZZ_UI_INJECT");
    if (inject && inject[0])
      sz_ui_session_set_inject(session, inject);
  }
  {
    const char *record = getenv("SCUZZ_UI_RECORD");
    if (record && record[0])
      sz_ui_session_set_record(session, record);
  }

  if (!sz_ui_pump_sync(session))
    sz_panic("Ui.run pump failed");

  {
    const char *script = getenv("SCUZZ_UI_SCRIPT");
    if (script && script[0])
      run_ui_script(session, script);
  }

  if (getenv("SCUZZ_UI_TAP")) {
    const char *seed = getenv("SCUZZ_UI_TEXT");
    if (seed && seed[0]) {
      SzInputEvent ev;
      memset(&ev, 0, sizeof(ev));
      ev.kind = SZ_INPUT_TEXT;
      ev.text = seed;
      if (!sz_ui_inject_sync(session, &ev))
        sz_panic("Ui.run text inject failed");
      inject_text = 1;
    }
    scripted_button_tap(session, inject_text);
  }

  interactive = cfg.kind == SZ_UI_RUNTIME_DESKTOP && sz_embedder_available();
  if (interactive) {
    const char *max_frames_env = getenv("SCUZZ_LIVE_FRAMES");
    int64_t max_frames =
        (max_frames_env && atoi(max_frames_env) > 0) ? atoi(max_frames_env) : 0;
    int64_t frame = 0;
    do {
      if (!sz_ui_pump_sync(session))
        sz_panic("Ui.run live pump failed");
      frame++;
      if (max_frames > 0 && frame >= max_frames)
        break;
      {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 16000000L; /* ~60fps cap */
        nanosleep(&ts, NULL);
      }
    } while (sz_embedder_alive());
    sz_ui_session_finish(session);
  } else if (stamp && stamp[0]) {
    const char *max_frames_env = getenv("SCUZZ_LIVE_FRAMES");
    int64_t max_frames =
        (max_frames_env && atoi(max_frames_env) > 0) ? atoi(max_frames_env) : 0;
    int64_t frame = 0;
    do {
      if (!sz_ui_pump_sync(session))
        sz_panic("Ui.run live pump failed");
      frame++;
      if (max_frames > 0 && frame >= max_frames)
        break;
      {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 16000000L;
        nanosleep(&ts, NULL);
      }
    } while (1);
    sz_ui_session_finish(session);
  } else {
    sz_ui_session_finish(session);
  }

  sz_ui_unmount(session);
  sz_free(e);
  return NULL;
}

SzIo *sz_ui_run_rebuild(SzUiRebuildFn fn, void *env) {
  RunRebuildEnv *e = (RunRebuildEnv *)sz_alloc(sizeof(RunRebuildEnv));
  e->rebuild = fn;
  e->env = env;
  return sz_io_delay(thunk_run_rebuild, e);
}

SzIo *sz_ui_run_view(SzView *root) {
  return sz_ui_run_rebuild(constant_root, root);
}
