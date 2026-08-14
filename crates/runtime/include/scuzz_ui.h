#ifndef SCUZZ_UI_H
#define SCUZZ_UI_H

#include "scuzz_rt.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UiRuntime peer interpreters. Headless is first-class. */
typedef enum SzUiRuntimeKind {
  SZ_UI_RUNTIME_HEADLESS = 1,
  SZ_UI_RUNTIME_DESKTOP = 2,
  SZ_UI_RUNTIME_MOBILE = 3
} SzUiRuntimeKind;

typedef struct SzUiConfig {
  SzUiRuntimeKind kind;
  int width;
  int height;
  double scale; /* Desktop: display backing scale; Headless: from SCUZZ_UI_SCALE */
  const char *title; /* Desktop / Mobile; may be NULL */
} SzUiConfig;

typedef enum SzPointerPhase {
  SZ_POINTER_DOWN = 1,
  SZ_POINTER_MOVE = 2,
  SZ_POINTER_UP = 3
} SzPointerPhase;

typedef enum SzLifecyclePhase {
  SZ_LIFECYCLE_RESUME = 1,
  SZ_LIFECYCLE_PAUSE = 2,
  SZ_LIFECYCLE_STOP = 3
} SzLifecyclePhase;

typedef enum SzInputKind {
  SZ_INPUT_TAP = 1,
  SZ_INPUT_RESIZE = 2,
  SZ_INPUT_TEXT = 3,      /* full replace of focused TextField (Headless) */
  SZ_INPUT_POINTER = 4,   /* touch / pointer with phase */
  SZ_INPUT_SCROLL = 5,    /* vertical pan dy on Scroll under (x,y) */
  SZ_INPUT_LIFECYCLE = 6, /* pause / resume / stop */
  SZ_INPUT_KEYBOARD = 7,  /* soft keyboard show (1) / hide (0) */
  SZ_INPUT_TEXT_EDIT = 8  /* append text, or backspace if text NULL/empty */
} SzInputKind;

typedef struct SzInputEvent {
  SzInputKind kind;
  float x;
  float y;
  int width;  /* resize */
  int height; /* resize */
  const char *text; /* SZ_INPUT_TEXT / SZ_INPUT_TEXT_EDIT */
  SzPointerPhase pointer_phase; /* SZ_INPUT_POINTER */
  float dy;                     /* SZ_INPUT_SCROLL (positive = content up) */
  SzLifecyclePhase lifecycle;   /* SZ_INPUT_LIFECYCLE */
  int keyboard_visible;         /* SZ_INPUT_KEYBOARD: 1=show, 0=hide */
} SzInputEvent;

/* --- theme tokens -------------------------------------------------------- */

typedef struct SzTheme {
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
  float font_px; /* paint/measure size (default 8px) */
  float radius;  /* 0 = sharp */
} SzTheme;

const SzTheme *sz_theme_default(void);

/* Language-facing theme / color ints (ARGB). */
int64_t sz_theme_accent(void);
int64_t sz_theme_primary(void);
int64_t sz_theme_muted(void);
int64_t sz_theme_foreground(void);
int64_t sz_color_rgb(int64_t r, int64_t g, int64_t b);

/* --- signals ------------------------------------------------------------- */

typedef struct SzSignalInt SzSignalInt;
typedef struct SzSignalStr SzSignalStr;
typedef struct SzSignalList SzSignalList;

SzSignalInt *sz_signal_int(int64_t initial);
void sz_signal_int_set(SzSignalInt *s, int64_t v);
int64_t sz_signal_int_get(const SzSignalInt *s);
void sz_signal_int_free(SzSignalInt *s);

SzSignalStr *sz_signal_str(const char *initial);
void sz_signal_str_set(SzSignalStr *s, const char *v);
const char *sz_signal_str_get(const SzSignalStr *s);
void sz_signal_str_free(SzSignalStr *s);

SzSignalList *sz_signal_list(SzList *initial);
void sz_signal_list_set(SzSignalList *s, SzList *v);
SzList *sz_signal_list_get(const SzSignalList *s);
void sz_signal_list_free(SzSignalList *s);

/* Signal store dump: one "kind[id] = value" line per live signal, in creation
   order (fuzz oracle; caller frees SzString). */
SzString *sz_signal_dump(void);
/* Law observation: signal store by creation-order id (TestRuntime / fuzz). */
int64_t sz_law_signal_int(int64_t id);
SzString *sz_law_signal_str(int64_t id);
int64_t sz_law_signal_list_len(int64_t id);
SzString *sz_law_signal_list_at(int64_t id, int64_t index);

/* --- declarative View tree ----------------------------------------------- */

typedef enum SzViewKind {
  SZ_VIEW_TEXT = 1,
  SZ_VIEW_BUTTON,
  SZ_VIEW_TEXT_FIELD,
  SZ_VIEW_COLUMN,
  SZ_VIEW_ROW,
  SZ_VIEW_LIST,
  SZ_VIEW_SCROLL,
  SZ_VIEW_EXPANDED, /* Column/Row flex child: leftover height or width */
  SZ_VIEW_CENTER,   /* fill max constraints; center child */
  SZ_VIEW_ALIGN,    /* fill max; place child 0=start 1=center 2=end */
  SZ_VIEW_STACK,    /* overlay children; paint back-to-front */
  SZ_VIEW_POSITIONED, /* Stack child: offset (x, y) from stack origin */
  SZ_VIEW_PADDING,  /* uniform inset; deflates max constraints */
  SZ_VIEW_SIZED,    /* tight w×h slot (clamped to incoming max) */
  SZ_VIEW_MIN_SIZE, /* raise min w×h (clamped to incoming max) */
  SZ_VIEW_BACKGROUND, /* paint color; size to child */
  SZ_VIEW_ASPECT_RATIO, /* largest rw:rh box that fits max constraints */
  SZ_VIEW_FRACTION, /* percent of incoming max; 0 = size to child on that axis */
  SZ_VIEW_IMAGE,
  SZ_VIEW_ICON
} SzViewKind;

typedef struct SzRect {
  float x, y, w, h;
} SzRect;

typedef struct SzView SzView;
typedef struct SzUiSession SzUiSession;

typedef void (*SzViewTapFn)(SzView *self, void *env);

SzView *sz_view_text(const char *text);
SzView *sz_view_text_signal_int(SzSignalInt *sig, const char *prefix);
/* bindText: a11y dump uses the live signal string. */
SzView *sz_view_text_signal_str(SzSignalStr *sig);

SzView *sz_view_button(const char *label, SzViewTapFn on_tap, void *env);
SzView *sz_view_text_field(SzSignalStr *text, const char *placeholder);
SzView *sz_view_column(void);
SzView *sz_view_row(void);
SzView *sz_view_stack(void);
SzView *sz_view_list(void);
/* Reactive list: children rebuilt from Signal.list at layout (`- item` texts). */
SzView *sz_view_each(SzSignalList *sig);
SzView *sz_view_scroll(SzView *child);
/* Column leftover height or Row leftover width after non-Expanded siblings. */
SzView *sz_view_expanded(SzView *child);
SzView *sz_view_center(SzView *child);
/* ax/ay: 0=start (left/top), 1=center, 2=end (right/bottom). */
SzView *sz_view_align(int ax, int ay, SzView *child);
/* Offset (x, y) from the parent Stack origin; x/y clamped to >= 0. */
SzView *sz_view_positioned(int x, int y, SzView *child);
/* Uniform inset in px; deflates max width/height for the child. */
SzView *sz_view_padding(int pad, SzView *child);
/* Tight w×h slot; clamped to incoming max. Child laid out at origin. */
SzView *sz_view_sized(int w, int h, SzView *child);
/* Floor on child size; 0 on an axis means no min. Clamped to incoming max. */
SzView *sz_view_min_size(int w, int h, SzView *child);
/* Pass constraints through; size to child; paint `argb` behind the child. */
SzView *sz_view_background(uint32_t argb, SzView *child);
/* Largest rw:rh box that fits incoming max; child laid out in that tight slot. */
SzView *sz_view_aspect_ratio(int rw, int rh, SzView *child);
/* Percent of incoming max (1–100); 0 on an axis sizes to the child. */
SzView *sz_view_fraction(int wpct, int hpct, SzView *child);
SzView *sz_view_image(int w, int h, uint32_t argb, const char *caption);
SzView *sz_view_icon(char glyph, uint32_t argb);
/* Visible iff Signal.get(sig) == value; returns child. */
SzView *sz_view_show_when(SzSignalInt *sig, int64_t value, SzView *child);

void sz_view_add_child(SzView *parent, SzView *child);
void sz_view_clear_children(SzView *parent);
void sz_view_free(SzView *view);

SzViewKind sz_view_kind(const SzView *view);
SzRect sz_view_frame(const SzView *view);

/* Layout + hit-test (also run inside pump / inject). */
void sz_view_layout(SzView *root, float width, float height, const SzTheme *theme);
SzView *sz_view_hit_test(SzView *root, float x, float y);
/* Append to focused TextField, or chop one byte when backspace != 0. */
int sz_view_handle_text_edit(SzView *root, const char *text, int backspace);

/* Scroll offset + soft keyboard helpers. */
float sz_view_scroll_y(const SzView *scroll);
void sz_view_scroll_by(SzView *scroll, float dy);
SzView *sz_view_scroll_at(SzView *root, float x, float y);
int sz_view_has_focused_text_field(SzView *root);
/* Shown TextFields in a11y preorder (cap 64 for dump / inject). */
int sz_view_collect_text_fields(SzView *root, SzView **out, int cap);
/* Focused field, else the first collected field (`text`/`type`/`backspace`). */
SzView *sz_view_text_field_target(SzView *root);
/* Focus collected field `index`, or the starred target when index < 0. */
int sz_view_focus_text_field_at(SzView *root, int index);
/* Caret in the focused TextField from measured text advance (not a cell grid).
 * Empty rect if nothing is focused. `theme` supplies font_px (default theme OK). */
SzRect sz_view_caret_rect(SzView *root, const SzTheme *theme);

/* Accessibility hooks (Headless-dumpable; no OS AT bridge yet). */
typedef enum SzA11yRole {
  SZ_A11Y_NONE = 0,
  SZ_A11Y_BUTTON = 1,
  SZ_A11Y_TEXT = 2,
  SZ_A11Y_TEXT_FIELD = 3,
  SZ_A11Y_IMAGE = 4,
  SZ_A11Y_LIST = 5,
  SZ_A11Y_SCROLL = 6
} SzA11yRole;

SzA11yRole sz_view_a11y_role(const SzView *view);
const char *sz_view_a11y_label(const SzView *view);
/* Live TextField string, or "" if not a field. */
const char *sz_view_text_field_value(const SzView *view);
/* Depth-first "role:label" lines joined by newlines (caller frees SzString). */
SzString *sz_view_a11y_dump(SzView *root);

/* --- session protocol ---------------------------------------------------- */

SzUiSession *sz_ui_mount(const SzUiConfig *cfg, SzView *root);
/* Transfer View ownership to the session (freed on unmount). */
void sz_ui_session_take_root(SzUiSession *session);
/* Swap the View tree. Signals are not freed or reset. If the session owns the
 * view, the previous root is freed. Marks dirty so the next pump relayouts. */
int sz_ui_session_replace_root(SzUiSession *session, SzView *root);
/* Rebuild factory for stamp-watch / reload. Must return a new tree. Signals
 * stay with the caller. env is not owned. */
typedef SzView *(*SzUiRebuildFn)(void *env);
void sz_ui_session_set_rebuild(SzUiSession *session, SzUiRebuildFn fn, void *env);
/* Watch a stamp file. Next pump that sees different contents calls rebuild
 * then replace_root. Missing file snapshots as empty. Headless, Desktop, and
 * Mobile share this path. */
int sz_ui_session_watch(SzUiSession *session, const char *path);
/* Live structural dump (same format as SCUZZ_FUZZ_DUMP) rewritten on dirty
 * pumps, stamp reload, and IO-bridge flushes. Agents read the file.
 * [taps] lists inject indices for `tap N` (scan order, cap 64).
 * [fields] lists TextFields in a11y order. `N*` is the text/type/backspace
 * target (focused, else first). Lines are `N placeholder="live"` (star on
 * the target). `text N s` / `type N s` / `backspace N k` target dump index N.
 * One-token forms still use the starred field.
 * [scrolls] lists hittable Scrolls in scan order; `scroll N dy` pans index N
 * (`scroll 40` stays the first). */
int sz_ui_session_set_debug_dump(SzUiSession *session, const char *path);
int sz_ui_session_write_dump(SzUiSession *session, const char *path);
/* Watch an inject script (tap/text/type/pump/scroll/backspace). Next pump that
 * sees new contents plays the suffix (append) or the whole file (rewrite).
 * Missing = empty. */
int sz_ui_session_set_inject(SzUiSession *session, const char *path);
/* Invoke the rebuild factory now. Pump calls this when the stamp changes. */
int sz_ui_session_reload(SzUiSession *session);
/* dlopen `path` (copied to a unique sibling so the OS does not keep a stale
 * image) and set the rebuild factory from exported `sz_ui_reload_rebuild`.
 * Signals stay in `rebuild_env`. Does not rebuild until reload/stamp.
 * Stamp-watch loads `SCUZZ_UI_RELOAD_CODE` (if set) before rebuild. */
int sz_ui_session_load_code(SzUiSession *session, const char *path);
void sz_ui_unmount(SzUiSession *session);
/* Snapshot PNG / structural dump from SCUZZ_SNAPSHOT_PATH / SCUZZ_FUZZ_DUMP. */
void sz_ui_session_finish(SzUiSession *session);
void sz_ui_resolve_headless_size(int *width, int *height, double *scale);

int sz_ui_pump_sync(SzUiSession *session);
int sz_ui_inject_sync(SzUiSession *session, const SzInputEvent *event);
int sz_ui_snapshot_png_sync(SzUiSession *session, const char *path);
int sz_ui_snapshot_png_bytes(SzUiSession *session, uint8_t **out, size_t *out_len);

SzUiRuntimeKind sz_ui_session_kind(const SzUiSession *session);
int sz_ui_session_width(const SzUiSession *session);
int sz_ui_session_height(const SzUiSession *session);
SzView *sz_ui_session_root(SzUiSession *session);
SzLifecyclePhase sz_ui_session_lifecycle(const SzUiSession *session);
int sz_ui_session_keyboard_visible(const SzUiSession *session);

/* IO → UI bridge: post signal writes from completed IO; flushed at pump. */
void sz_ui_bridge_post_int(SzUiSession *session, SzSignalInt *sig, int64_t value);
void sz_ui_bridge_post_str(SzUiSession *session, SzSignalStr *sig, const char *value);
void sz_ui_bridge_flush(SzUiSession *session);

/* --- language-facing View / Signal (Scuzz Lang-authored UI) ----------- */

SzSignalInt *sz_lang_signal_int(int64_t initial);
int64_t sz_lang_signal_get(SzSignalInt *s);
void *sz_lang_signal_set(SzSignalInt *s, int64_t v);
SzSignalStr *sz_lang_signal_str(SzString *initial);
SzString *sz_lang_signal_str_get(SzSignalStr *s);
void *sz_lang_signal_str_set(SzSignalStr *s, SzString *v);
SzSignalList *sz_lang_signal_list(SzList *initial);
SzList *sz_lang_signal_list_get(SzSignalList *s);
void *sz_lang_signal_list_set(SzSignalList *s, SzList *v);

SzView *sz_lang_view_text(SzString *text);
/* First-class tap closure: `tap`/`env` come from a compiled `_ => ...` lambda. */
SzView *sz_lang_view_button(SzString *label, SzViewTapFn tap, void *env);
SzView *sz_lang_view_column(void);
SzView *sz_lang_view_row(void);
SzView *sz_lang_view_stack(void);
SzView *sz_lang_view_each(SzSignalList *sig);
SzView *sz_lang_view_scroll(SzView *child);
SzView *sz_lang_view_expanded(SzView *child);
SzView *sz_lang_view_center(SzView *child);
SzView *sz_lang_view_align(int64_t ax, int64_t ay, SzView *child);
SzView *sz_lang_view_positioned(int64_t x, int64_t y, SzView *child);
SzView *sz_lang_view_padding(int64_t pad, SzView *child);
SzView *sz_lang_view_sized(int64_t w, int64_t h, SzView *child);
SzView *sz_lang_view_min_size(int64_t w, int64_t h, SzView *child);
SzView *sz_lang_view_background(int64_t argb, SzView *child);
SzView *sz_lang_view_aspect_ratio(int64_t rw, int64_t rh, SzView *child);
SzView *sz_lang_view_fraction(int64_t wpct, int64_t hpct, SzView *child);
SzView *sz_lang_view_text_field(SzSignalStr *text, SzString *placeholder);
SzView *sz_lang_view_icon(int64_t glyph, int64_t argb);
SzView *sz_lang_view_image(int64_t w, int64_t h, int64_t argb, SzString *caption);
void *sz_lang_view_add_child(SzView *parent, SzView *child);
SzView *sz_lang_view_show_when(SzSignalInt *sig, int64_t value, SzView *child);

/* Derived Signal.str from Signal.int (recomputed on get / dump). */
typedef SzString *(*SzSignalMapIntFn)(int64_t v, void *env);
SzSignalStr *sz_lang_signal_map(SzSignalInt *src, SzSignalMapIntFn fn, void *env);
SzView *sz_lang_view_bind_text(SzSignalStr *sig);

/* Mount prebuilt root → pump → optional scripted tap → snapshot → unmount. */
SzIo *sz_ui_run_view(SzView *root);
/* Like sz_ui_run_view, but construction is a factory so stamp-watch can
 * re-run it. Watches SCUZZ_UI_RELOAD_STAMP when set. On stamp change,
 * loads SCUZZ_UI_RELOAD_CODE (dylib exporting sz_ui_reload_rebuild) if
 * that file exists, then rebuilds. Writes SCUZZ_UI_DEBUG_DUMP on dirty
 * pumps when set. Plays SCUZZ_UI_INJECT (tap/text/type/pump/scroll/
 * backspace) when that file changes. */
SzIo *sz_ui_run_rebuild(SzUiRebuildFn fn, void *env);

#ifdef __cplusplus
}
#endif

#endif /* SCUZZ_UI_H */
