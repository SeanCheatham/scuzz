#ifndef SCALUI_UI_H
#define SCALUI_UI_H

#include "scalui_rt.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UiRuntime peer interpreters. Headless is first-class. */
typedef enum SuUiRuntimeKind {
  SU_UI_RUNTIME_HEADLESS = 1,
  SU_UI_RUNTIME_WINDOW = 2,
  SU_UI_RUNTIME_MOBILE = 3
} SuUiRuntimeKind;

typedef struct SuUiConfig {
  SuUiRuntimeKind kind;
  int width;
  int height;
  double scale;      /* recorded; raster uses logical pixels */
  const char *title; /* Window / Mobile; may be NULL */
} SuUiConfig;

typedef enum SuPointerPhase {
  SU_POINTER_DOWN = 1,
  SU_POINTER_MOVE = 2,
  SU_POINTER_UP = 3
} SuPointerPhase;

typedef enum SuLifecyclePhase {
  SU_LIFECYCLE_RESUME = 1,
  SU_LIFECYCLE_PAUSE = 2,
  SU_LIFECYCLE_STOP = 3
} SuLifecyclePhase;

typedef enum SuInputKind {
  SU_INPUT_TAP = 1,
  SU_INPUT_RESIZE = 2,
  SU_INPUT_TEXT = 3,      /* deliver string to focused TextField */
  SU_INPUT_POINTER = 4,   /* touch / pointer with phase */
  SU_INPUT_SCROLL = 5,    /* vertical pan dy on Scroll under (x,y) */
  SU_INPUT_LIFECYCLE = 6, /* pause / resume / stop */
  SU_INPUT_KEYBOARD = 7   /* soft keyboard show (1) / hide (0) */
} SuInputKind;

typedef struct SuInputEvent {
  SuInputKind kind;
  float x;
  float y;
  int width;  /* resize */
  int height; /* resize */
  const char *text; /* SU_INPUT_TEXT */
  SuPointerPhase pointer_phase; /* SU_INPUT_POINTER */
  float dy;                     /* SU_INPUT_SCROLL (positive = content up) */
  SuLifecyclePhase lifecycle;   /* SU_INPUT_LIFECYCLE */
  int keyboard_visible;         /* SU_INPUT_KEYBOARD: 1=show, 0=hide */
} SuInputEvent;

/* --- theme tokens -------------------------------------------------------- */

typedef struct SuTheme {
  uint32_t background;
  uint32_t surface;
  uint32_t foreground;
  uint32_t primary;
  uint32_t on_primary;
  uint32_t border;
  uint32_t muted;
  uint32_t accent;
  uint32_t disabled;
  float pad;
  float gap;
  float control_h;
  float font_px; /* bitmap font cell size (default 8px) */
  float radius;  /* corner radius (0 = sharp; preserves current goldens) */
} SuTheme;

const SuTheme *su_theme_default(void);

/* Language-facing theme / color ints (ARGB). */
int64_t su_theme_accent(void);
int64_t su_theme_primary(void);
int64_t su_theme_muted(void);
int64_t su_theme_foreground(void);
int64_t su_color_rgb(int64_t r, int64_t g, int64_t b);

/* --- signals ------------------------------------------------------------- */

typedef struct SuSignalInt SuSignalInt;
typedef struct SuSignalStr SuSignalStr;
typedef struct SuSignalList SuSignalList;

SuSignalInt *su_signal_int(int64_t initial);
void su_signal_int_set(SuSignalInt *s, int64_t v);
int64_t su_signal_int_get(const SuSignalInt *s);
void su_signal_int_free(SuSignalInt *s);

SuSignalStr *su_signal_str(const char *initial);
void su_signal_str_set(SuSignalStr *s, const char *v);
const char *su_signal_str_get(const SuSignalStr *s);
void su_signal_str_free(SuSignalStr *s);

SuSignalList *su_signal_list(SuList *initial);
void su_signal_list_set(SuSignalList *s, SuList *v);
SuList *su_signal_list_get(const SuSignalList *s);
void su_signal_list_free(SuSignalList *s);

/* Signal store dump: one "kind[id] = value" line per live signal, in creation
   order (fuzz oracle; caller frees SuString). */
SuString *su_signal_dump(void);

/* --- declarative View tree ----------------------------------------------- */

typedef enum SuViewKind {
  SU_VIEW_TEXT = 1,
  SU_VIEW_BUTTON,
  SU_VIEW_TEXT_FIELD,
  SU_VIEW_COLUMN,
  SU_VIEW_ROW,
  SU_VIEW_LIST,
  SU_VIEW_SCROLL,
  SU_VIEW_IMAGE,
  SU_VIEW_ICON,
  SU_VIEW_LABEL /* full-bleed bg + bar that toggles colors on tap */
} SuViewKind;

typedef struct SuRect {
  float x, y, w, h;
} SuRect;

typedef struct SuView SuView;
typedef struct SuUiSession SuUiSession;

typedef void (*SuViewTapFn)(SuView *self, void *env);

SuView *su_view_text(const char *text);
SuView *su_view_text_signal_int(SuSignalInt *sig, const char *prefix);
SuView *su_view_text_signal_str(SuSignalStr *sig);

SuView *su_view_button(const char *label, SuViewTapFn on_tap, void *env);
SuView *su_view_text_field(SuSignalStr *text, const char *placeholder);
SuView *su_view_column(void);
SuView *su_view_row(void);
SuView *su_view_list(void);
SuView *su_view_scroll(SuView *child);
SuView *su_view_image(int w, int h, uint32_t argb, const char *caption);
SuView *su_view_icon(char glyph, uint32_t argb);
/* Full-bleed bg + bar that toggles colors on tap (C unit-test helper). */
SuView *su_view_label(const char *text, uint32_t bg_argb, uint32_t fg_argb);
/* Visible iff Signal.get(sig) == value; returns child. */
void su_view_set_show_when(SuView *view, SuSignalInt *sig, int64_t value);
SuView *su_view_show_when(SuSignalInt *sig, int64_t value, SuView *child);

void su_view_add_child(SuView *parent, SuView *child);
void su_view_clear_children(SuView *parent);
void su_view_free(SuView *view);

SuViewKind su_view_kind(const SuView *view);
SuRect su_view_frame(const SuView *view);

/* Layout + hit-test (also run inside pump / inject). */
void su_view_layout(SuView *root, float width, float height, const SuTheme *theme);
SuView *su_view_hit_test(SuView *root, float x, float y);

/* Scroll offset + soft keyboard helpers. */
float su_view_scroll_y(const SuView *scroll);
void su_view_scroll_by(SuView *scroll, float dy);
SuView *su_view_scroll_at(SuView *root, float x, float y);
int su_view_has_focused_text_field(SuView *root);

/* Accessibility hooks (Headless-dumpable; no OS AT bridge yet). */
typedef enum SuA11yRole {
  SU_A11Y_NONE = 0,
  SU_A11Y_BUTTON = 1,
  SU_A11Y_TEXT = 2,
  SU_A11Y_TEXT_FIELD = 3,
  SU_A11Y_IMAGE = 4,
  SU_A11Y_LIST = 5,
  SU_A11Y_SCROLL = 6
} SuA11yRole;

void su_view_set_a11y(SuView *view, SuA11yRole role, const char *label);
SuA11yRole su_view_a11y_role(const SuView *view);
const char *su_view_a11y_label(const SuView *view);
/* Depth-first "role:label" lines joined by newlines (caller frees SuString). */
SuString *su_view_a11y_dump(SuView *root);

/* Animation — float lerp; session pump ticks all registered anims. */
typedef struct SuAnimFloat SuAnimFloat;
SuAnimFloat *su_anim_float(float from, float to, int64_t duration_ms);
void su_anim_free(SuAnimFloat *a);
float su_anim_value(const SuAnimFloat *a);
int su_anim_done(const SuAnimFloat *a);
void su_anim_tick(SuAnimFloat *a, int64_t dt_ms);
void su_anim_tick_all(int64_t dt_ms);

/* --- session protocol ---------------------------------------------------- */

SuUiSession *su_ui_mount(const SuUiConfig *cfg, SuView *root);
/* Transfer View ownership to the session (freed on unmount). */
void su_ui_session_take_root(SuUiSession *session);
void su_ui_unmount(SuUiSession *session);

int su_ui_pump_sync(SuUiSession *session);
int su_ui_inject_sync(SuUiSession *session, const SuInputEvent *event);
int su_ui_snapshot_png_sync(SuUiSession *session, const char *path);
int su_ui_snapshot_png_bytes(SuUiSession *session, uint8_t **out, size_t *out_len);

SuUiRuntimeKind su_ui_session_kind(const SuUiSession *session);
int su_ui_session_width(const SuUiSession *session);
int su_ui_session_height(const SuUiSession *session);
SuView *su_ui_session_root(SuUiSession *session);
const SuTheme *su_ui_session_theme(const SuUiSession *session);
SuLifecyclePhase su_ui_session_lifecycle(const SuUiSession *session);
int su_ui_session_keyboard_visible(const SuUiSession *session);

/* IO → UI bridge: post signal writes from completed IO; flushed at pump. */
void su_ui_bridge_post_int(SuUiSession *session, SuSignalInt *sig, int64_t value);
void su_ui_bridge_post_str(SuUiSession *session, SuSignalStr *sig, const char *value);
void su_ui_bridge_flush(SuUiSession *session);

/* --- language-facing View / Signal (ScalUI-authored UI) ----------- */

SuSignalInt *su_lang_signal_int(int64_t initial);
int64_t su_lang_signal_get(SuSignalInt *s);
void *su_lang_signal_set(SuSignalInt *s, int64_t v);
SuSignalStr *su_lang_signal_str(SuString *initial);
SuString *su_lang_signal_str_get(SuSignalStr *s);
void *su_lang_signal_str_set(SuSignalStr *s, SuString *v);
SuSignalList *su_lang_signal_list(SuList *initial);
SuList *su_lang_signal_list_get(SuSignalList *s);
void *su_lang_signal_list_set(SuSignalList *s, SuList *v);

SuView *su_lang_view_text(SuString *text);
SuView *su_lang_view_text_signal(SuSignalInt *sig, SuString *prefix);
/* First-class tap closure: `tap`/`env` come from a compiled `_ => ...` lambda. */
SuView *su_lang_view_button(SuString *label, SuViewTapFn tap, void *env);
SuView *su_lang_view_column(void);
SuView *su_lang_view_row(void);
SuView *su_lang_view_list(void);
SuView *su_lang_view_scroll(SuView *child);
SuView *su_lang_view_text_field(SuSignalStr *text, SuString *placeholder);
SuView *su_lang_view_icon(int64_t glyph, int64_t argb);
SuView *su_lang_view_image(int64_t w, int64_t h, int64_t argb, SuString *caption);
void *su_lang_view_add_child(SuView *parent, SuView *child);
void *su_lang_view_add_texts(SuView *parent, SuList *lines);
void *su_lang_view_clear_children(SuView *parent);
void *su_lang_view_set_texts(SuView *parent, SuList *lines);
SuView *su_lang_view_show_when(SuSignalInt *sig, int64_t value, SuView *child);

/* Mount prebuilt root → pump → optional scripted tap → snapshot → unmount. */
SuIo *su_ui_run_view(SuView *root);

#ifdef __cplusplus
}
#endif

#endif /* SCALUI_UI_H */
