#ifndef SCUZZ_UI_H
#define SCUZZ_UI_H

#include "scuzz_rt.h"

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UiRuntime peer interpreters. Headless is first-class. */
typedef enum SzUiRuntimeKind {
  SZ_UI_RUNTIME_HEADLESS = 1,
  SZ_UI_RUNTIME_DESKTOP = 2,
  SZ_UI_RUNTIME_MOBILE = 3,
  SZ_UI_RUNTIME_WEB = 4
} SzUiRuntimeKind;

typedef struct SzUiConfig {
  SzUiRuntimeKind kind;
  int width;
  int height;
  double scale; /* Desktop/Mobile backing scale; Headless: SCUZZ_UI_SCALE */
  const char *title; /* Desktop / Mobile; may be NULL */
  int reduced_motion;
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
  SZ_INPUT_SCROLL = 5,    /* pan dx/dy on Scroll or editor under (x,y) */
  SZ_INPUT_LIFECYCLE = 6, /* pause / resume / stop */
  SZ_INPUT_KEYBOARD = 7,  /* soft keyboard show (1) / hide (0) */
  SZ_INPUT_TEXT_EDIT = 8, /* append text, or backspace if text NULL/empty */
  SZ_INPUT_KEY = 9,       /* named key + optional UTF-8 insert; not KEYBOARD */
  SZ_INPUT_COMPOSE = 10,  /* IME preedit; empty text commits into the buffer */
  SZ_INPUT_REDUCED_MOTION = 11
} SzInputKind;

/* SZ_INPUT_KEY modifier bits (combine with |). */
#define SZ_KEY_SHIFT 1
#define SZ_KEY_CTRL 2
#define SZ_KEY_CMD 4
#define SZ_KEY_ALT 8

typedef struct SzInputEvent {
  SzInputKind kind;
  float x;
  float y;
  int width;  /* resize */
  int height; /* resize */
  double scale; /* resize; 0 keeps the current display scale */
  const char *text; /* SZ_INPUT_TEXT / SZ_INPUT_TEXT_EDIT / SZ_INPUT_KEY insert */
  SzPointerPhase pointer_phase; /* SZ_INPUT_POINTER */
  float dy;                     /* SZ_INPUT_SCROLL (positive = content up) */
  float dx;                     /* SZ_INPUT_SCROLL (positive = content left) */
  SzLifecyclePhase lifecycle;   /* SZ_INPUT_LIFECYCLE */
  int keyboard_visible;         /* SZ_INPUT_KEYBOARD: 1=show, 0=hide */
  const char *key;              /* SZ_INPUT_KEY name: Enter, Backspace, ArrowLeft, a */
  int key_mods;                 /* SZ_INPUT_KEY: SZ_KEY_SHIFT / CTRL / CMD / ALT */
  int key_repeat;               /* SZ_INPUT_KEY: 1 = auto-repeat; same path as a discrete key */
  int pointer_button;           /* SZ_INPUT_POINTER: 0 hover, 1 primary, 3 secondary */
  int reduced_motion;           /* SZ_INPUT_REDUCED_MOTION */
} SzInputEvent;

/* Styled text. */
typedef struct SzText SzText;
SzText *sz_text_span(const SzString *value);
SzText *sz_text_concat(const SzList *values);
SzText *sz_text_color(int64_t color, const SzText *source);
SzText *sz_text_size(int64_t size, const SzText *source);
SzText *sz_text_action(int64_t key, const SzText *source);
SzText *sz_text_bold(const SzText *text);
SzText *sz_text_italic(const SzText *text);
SzText *sz_text_underline(const SzText *text);
SzText *sz_text_monospace(const SzText *text);
SzString *sz_text_plain(const SzText *text);

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
  uint32_t selection;
  float pad;
  float gap;
  float control_h;
  float font_px; /* paint/measure size (default 14px) */
  float line_gap; /* Logical pixels between text lines. */
  /* Backing scale for View.fontSize / padding / gap / sized and other
   * author px. 0 or 1 = logical points (hit-test). Paint sets this to
   * the device scale so taps match pixels. */
  float px_scale;
} SzTheme;

const SzTheme *sz_theme_default(void);
SzTheme *sz_theme_metrics(const SzTheme *source, int64_t font_size, int64_t line_gap,
                          int64_t padding, int64_t gap, int64_t control_height);
SzTheme *sz_theme_light(void);
SzTheme *sz_theme_dark(void);
SzTheme *sz_theme_colors(int64_t background, int64_t surface, int64_t foreground,
                        int64_t primary, int64_t on_primary, int64_t border,
                        int64_t muted, int64_t accent, int64_t selection);

/* Language-facing theme / color ints (ARGB). */
int64_t sz_theme_accent(void);
int64_t sz_icon_book(void);
int64_t sz_icon_code(void);
int64_t sz_icon_link(void);
int64_t sz_icon_web(void);
int64_t sz_icon_gui(void);
int64_t sz_icon_install(void);
int64_t sz_theme_primary(void);
int64_t sz_theme_muted(void);
int64_t sz_theme_foreground(void);
int64_t sz_color_rgb(int64_t r, int64_t g, int64_t b);
int64_t sz_color_rgba(int64_t r, int64_t g, int64_t b, int64_t a);

/* --- signals ------------------------------------------------------------- */

typedef struct SzSignal SzSignal;
typedef struct SzSignal SzSignalInt;
typedef struct SzSignal SzSignalStr;
typedef struct SzSignal SzSignalList;
typedef void *(*SzSignalMapFn)(void *value, void *env);
SzSignal *sz_signal_new(void *value, int64_t kind, SzString *name);
void *sz_signal_read(SzSignal *s);
void sz_signal_free(SzSignal *s);
void sz_signal_session_push(void);
void sz_signal_session_pop(void);
typedef struct SzSignalScope SzSignalScope;
SzSignalScope *sz_signal_scope_new(void);
SzSignalScope *sz_signal_scope_enter(SzSignalScope *scope);
void sz_signal_scope_free(SzSignalScope *scope);
void *sz_signal_write(SzSignal *s, void *value);
SzSignal *sz_signal_derive(SzSignal *src, SzSignalMapFn fn, void *env,
                           int64_t kind, SzString *name);

SzSignalInt *sz_signal_int(int64_t initial);
void sz_signal_int_set(SzSignalInt *s, int64_t v);
int64_t sz_signal_int_get(const SzSignalInt *s);
void sz_signal_int_free(SzSignalInt *s);

SzSignalStr *sz_signal_str(const char *initial);
void sz_signal_str_set(SzSignalStr *s, const char *v);
const char *sz_signal_str_get(const SzSignalStr *s);
void sz_signal_str_free(SzSignalStr *s);

SzSignalList *sz_signal_list(SzList *initial);
/* 1 when the list signal holds String elements, 0 for other element types. */
int sz_signal_list_elem_str(const SzSignalList *s);
void sz_signal_list_set(SzSignalList *s, SzList *v);
SzList *sz_signal_list_get(const SzSignalList *s);
void sz_signal_list_free(SzSignalList *s);
/* One-line label for string interpolation. Does not read the signal. */
SzString *sz_signal_show(const void *sig);

/* Typed session schema v=2 signal section: a JSON array written to `f`. */
void sz_signal_dump_json(FILE *f);
/* The same array as a string. Timeline states record it (caller frees). */
SzString *sz_signal_dump_json_string(void);
/* Publish the for-binder name. Property and Timeline kits read that name. */
void sz_signal_name(const void *sig, const char *name);

/* Named observation. A missing name panics. */
int64_t sz_property_signal_int(SzString *name);
SzString *sz_property_signal_str(SzString *name);
int64_t sz_property_signal_list_len(SzString *name);
SzString *sz_property_signal_list_at(SzString *name, int64_t index);

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
  SZ_VIEW_SIZED,    /* tight w x h slot (clamped to incoming max) */
  SZ_VIEW_MIN_SIZE, /* raise min w x h (clamped to incoming max) */
  SZ_VIEW_BACKGROUND, /* paint color; size to child */
  SZ_VIEW_ASPECT_RATIO, /* largest rw:rh box that fits max constraints */
  SZ_VIEW_FRACTION, /* percent of incoming max; 0 = size to child on that axis */
  SZ_VIEW_IMAGE,
  SZ_VIEW_ICON,
  SZ_VIEW_STRETCH, /* Column/Row: tight cross-axis; main axis stays intrinsic */
  SZ_VIEW_MAX_SIZE, /* lower max w x h; 0 = no cap on that axis */
  SZ_VIEW_CLIP,     /* pass constraints; clip paint to this frame */
  SZ_VIEW_OPACITY,  /* pass constraints; scale paint alpha (0-100) */
  SZ_VIEW_MAX_LINES, /* cap wrapped text lines; 0 = no cap */
  SZ_VIEW_IGNORE_POINTER, /* skip hit-test; taps pass through */
  SZ_VIEW_ABSORB_POINTER, /* skip child hit-test; block taps behind */
  SZ_VIEW_EXCLUDE_SEMANTICS, /* skip a11y dump and field collect for subtree */
  SZ_VIEW_ELLIPSIS, /* cap extra lines; paint ... on the last visible line */
  SZ_VIEW_TEXT_COLOR, /* pass constraints; paint View.text with this ARGB */
  SZ_VIEW_GAP, /* pass constraints; Column/Row/Wrap/Grid/List child spacing in px */
  SZ_VIEW_FONT_SIZE, /* pass constraints; View.text measure/paint size in px */
  SZ_VIEW_BORDER, /* pass constraints; paint n px stroke in color inside the frame */
  SZ_VIEW_RADIUS, /* pass constraints; clip paint to a rounded rect of n px */
  SZ_VIEW_CHECKBOX, /* Signal[Int] 0/1 box + label; tap flips */
  SZ_VIEW_WRAP,     /* flow children into runs; wrap when remaining width is short */
  SZ_VIEW_SLIDER,   /* Named integer range; input writes an allowed step */
  SZ_VIEW_GRID,     /* n equal columns; new row after n shown children */
  SZ_VIEW_RADIO,    /* Signal[Int] group; tap writes this value */
  SZ_VIEW_PROGRESS, /* Signal[Int] 0-100 bar; display only */
  SZ_VIEW_SWITCH,   /* Signal[Int] 0/1 track + label; tap flips */
  SZ_VIEW_CHIP,     /* Signal[Int] 0/1 labeled chip; tap flips */
  SZ_VIEW_LIST_TILE, /* full-width title row; optional trailing child */
  SZ_VIEW_BADGE,     /* Signal[Int] mark on a child; sizes to the child */
  SZ_VIEW_CARD,      /* surface + pad + border around one child */
  SZ_VIEW_DIVIDER,   /* full-width hairline; display only */
  SZ_VIEW_EXPANSION_TILE, /* Signal[Int] header; child shows when on */
  SZ_VIEW_ICON_BUTTON,    /* square label tap; same closure as button */
  SZ_VIEW_VERTICAL_DIVIDER, /* 8 px slot, control_h; muted hairline */
  SZ_VIEW_CIRCULAR_PROGRESS, /* Signal[Int] 0-100 square ring; display only */
  SZ_VIEW_AVATAR,            /* control_h disc + label; display only */
  SZ_VIEW_CHECKBOX_LIST_TILE, /* Signal[Int] 0/1 full-width title row; tap flips */
  SZ_VIEW_SWITCH_LIST_TILE,   /* Signal[Int] 0/1 full-width title + trailing switch */
  SZ_VIEW_RADIO_LIST_TILE,    /* Signal[Int] group; full-width title + leading radio */
  SZ_VIEW_SEGMENTED,          /* Named radio group with equal-width choices. */
  SZ_VIEW_FAB,                /* circular primary tap; same closure as button */
  SZ_VIEW_OUTLINED_BUTTON,    /* surface + border tap; same closure as button */
  SZ_VIEW_TEXT_BUTTON,        /* foreground label tap; same closure as button */
  SZ_VIEW_TOOLTIP,            /* message + child; sizes to the child; shows message on hover; not a tap */
  SZ_VIEW_PLACEHOLDER,        /* sizes to the child; muted box mark; not a tap */
  SZ_VIEW_FILTER_CHIP,        /* Signal[Int] 0/1 chip with a leading check; tap flips */
  SZ_VIEW_CHOICE_CHIP,        /* Signal[Int] group; chip tap writes this value */
  SZ_VIEW_ACTION_CHIP,        /* label tap; chip paint; same closure as button */
  SZ_VIEW_INPUT_CHIP,         /* Signal[Int] 0/1 chip with a trailing X; tap flips */
  SZ_VIEW_SEMANTICS,          /* a11y label + child; sizes to the child; not a tap */
  SZ_VIEW_MERGE_SEMANTICS,    /* a11y label + child; sizes to the child; omits child a11y */
  SZ_VIEW_INK_WELL,           /* label tap + child; sizes to the child; same closure as button */
  SZ_VIEW_VISIBILITY,         /* Signal[Int]; sizes to the child; off keeps size, skips paint */
  SZ_VIEW_OFFSTAGE,           /* Signal[Int]; lays out the child; off reports size 0, skips paint */
  SZ_VIEW_UNCONSTRAINED_BOX,  /* sizes to the child; child lays out with unbounded max */
  SZ_VIEW_EDITOR,             /* multiline SignalStr buffer; not a TextField */
  SZ_VIEW_SPLIT,              /* Signal[Int] 0-100; two children + drag handle */
  SZ_VIEW_OVERLAY,            /* Signal[Int]; fills parent when on; Escape dismisses */
  SZ_VIEW_ON_SECONDARY,       /* child + button-3 handler; sizes to the child; not a tap */
  SZ_VIEW_INDEX_BOOK,
  SZ_VIEW_SECTION,
  SZ_VIEW_APP_SHELL,
  SZ_VIEW_APP_BAR,
  SZ_VIEW_TABS,
  SZ_VIEW_FOCUS_GROUP,       /* child list of taps; sizes to the child; not a tap */
  SZ_VIEW_LINK,             /* label + route; Index Book section or http(s) URL */
  SZ_VIEW_BREADCRUMB,       /* wrap of trail children; paints > between items */
  SZ_VIEW_ADAPTIVE,
  SZ_VIEW_NAV_TILE,         /* icon + title + route; tap navigates like a link */
  SZ_VIEW_VIRTUAL_LIST,
  SZ_VIEW_THEME,
  SZ_VIEW_SELECT,
  SZ_VIEW_ANIMATED_OPACITY,
  SZ_VIEW_REDUCED_MOTION,
  SZ_VIEW_SELECT_OPTION
} SzViewKind;

typedef struct SzRect {
  float x, y, w, h;
} SzRect;

typedef struct SzView SzView;
SzView *sz_view_rich_text(const SzText *text);
int64_t sz_view_text_action_id(const SzView *view);
typedef struct SzUiSession SzUiSession;

typedef void (*SzViewTapFn)(SzView *self, void *env);
SzView *sz_view_action_text(const SzText *text, SzViewTapFn tap, void *env);
/* Mapper must not free `item`. The list owns the string. */
/* The item is the list element pointer: an SzString for String lists, the
 * element value (for example an SzAdt record) otherwise. */
typedef SzView *(*SzViewEachFn)(SzString *item, void *env);
typedef void *(*SzViewKeyFn)(void *item, void *env);
typedef SzView *(*SzViewRowFn)(SzSignal *item, void *env);

SzView *sz_view_theme(const SzTheme *theme, SzView *child);
SzView *sz_view_text(const char *text);
SzView *sz_view_text_signal_int(SzSignalInt *sig, const char *prefix);
/* bindText: a11y dump uses the live signal string. */
SzView *sz_view_text_signal_str(SzSignalStr *sig);

SzView *sz_view_button(const char *label, SzViewTapFn on_tap, void *env);
/* Tap flips `sig` between 0 and 1. `label` is a11y + painted text. */
SzView *sz_view_checkbox(SzSignalInt *sig, const char *label);
/* Tap writes `value` into `sig`. Radios that share `sig` form a group. */
SzView *sz_view_radio(SzSignalInt *sig, int64_t value, const char *label);
/* Input writes an allowed step from minimum through maximum. */
SzView *sz_view_slider(SzSignalInt *sig, const char *label, int64_t minimum, int64_t maximum, int64_t step);
int64_t sz_view_slider_minimum(const SzView *view);
int64_t sz_view_slider_maximum(const SzView *view);
int64_t sz_view_slider_value(const SzView *view);
/* Paint a named 0-100 bar from `sig`. Keep it outside pointer input. */
SzView *sz_view_progress(SzSignalInt *sig, const char *label);
/* Paint a named 0-100 square ring from `sig`. Keep it outside pointer input. */
SzView *sz_view_circular_progress(SzSignalInt *sig, const char *label);
/* Current percentage, clamped to 0-100. Other View kinds return 0. */
int sz_view_progress_value(const SzView *view);
/* `control_h` disc with `label`. Not a tap target. */
SzView *sz_view_avatar(const char *label);
/* Tap flips `sig` between 0 and 1. `label` is a11y + painted text. */
SzView *sz_view_switch(SzSignalInt *sig, const char *label);
/* Tap flips `sig` between 0 and 1. `label` fills the chip. */
SzView *sz_view_chip(SzSignalInt *sig, const char *label);
/* Tap flips `sig` between 0 and 1. Leading check when on. */
SzView *sz_view_filter_chip(SzSignalInt *sig, const char *label);
/* Tap writes `value` into `sig`. Chips that share `sig` form a group. */
SzView *sz_view_choice_chip(SzSignalInt *sig, int64_t value, const char *label);
/* Label tap with chip paint. Same closure as button. */
SzView *sz_view_action_chip(const char *label, SzViewTapFn on_tap, void *env);
/* Tap flips `sig` between 0 and 1. Trailing X mark. */
SzView *sz_view_input_chip(SzSignalInt *sig, const char *label);
/* Full-width title row. Optional `trailing` sits on the right. Not a tap target. */
SzView *sz_view_list_tile(const char *title, SzView *trailing);
/* Full-width title row. Tap flips `sig` between 0 and 1. */
SzView *sz_view_checkbox_list_tile(SzSignalInt *sig, const char *title);
/* Full-width title row with a trailing switch. Tap flips `sig` between 0 and 1. */
SzView *sz_view_switch_list_tile(SzSignalInt *sig, const char *title);
/* Full-width title row with a leading radio. Tap writes `value` into `sig`. */
SzView *sz_view_radio_list_tile(SzSignalInt *sig, int64_t value, const char *title);
/* Full-width two-segment row. Tap left writes 0; tap right writes 1. */
/* Named page for an Index Book. The book owns the section. */
SzView *sz_view_section(const char *id, const char *title, SzView *child);
/* Consume a column of sections. Selection is a zero-based section index. */
SzView *sz_view_index_book(SzSignalInt *selected, SzView *sections);
/* The shell reserves the bar height and gives the body the remaining space. */
SzView *sz_view_app_shell(SzView *bar, SzView *body);
SzView *sz_view_app_bar(SzView *title, SzView *actions);
/* Fixed local panels. The selection is a zero-based section index. */
SzView *sz_view_tabs(SzSignalInt *selected, SzView *sections);
SzView *sz_view_select(SzSignalInt *sig, const char *label, SzSignalList *choices);
void sz_view_prepare_input(SzView *root);
void sz_view_input_time(SzView *root, int64_t milliseconds);
void sz_view_selects_dump_json(SzView *root, FILE *file);
int sz_view_select_open(const SzView *view);
int sz_view_select_active(const SzView *view);
int sz_view_select_count(const SzView *view);
int64_t sz_view_select_value(const SzView *view);
const char *sz_view_select_text(const SzView *view);
SzView *sz_view_select_popup(SzView *view);
SzView *sz_view_select_active_option(SzView *view);
int sz_view_select_option_index(const SzView *view);
int sz_view_select_option_count(const SzView *view);
SzView *sz_view_segmented(SzSignalInt *sig, const char *label,
                           const char *const *choices, int count);
/* Overlay `sig` as a count on `child`. Sizes to the child. Not a tap target. */
SzView *sz_view_badge(SzSignalInt *sig, SzView *child);
/* Surface, theme pad, and a 1 px border around `child`. Not a tap target. */
SzView *sz_view_card(SzView *child);
/* Full-width 8 px slot with a muted hairline. Not a tap target. */
SzView *sz_view_divider(void);
/* Tap flips `sig` 0/1. Child shows when `sig` is not 0. */
SzView *sz_view_expansion_tile(SzSignalInt *sig, const char *title, SzView *child);
/* Square `control_h` tap. `label` is a11y + painted glyph. */
SzView *sz_view_icon_button(const char *label, SzViewTapFn on_tap, void *env);
/* Circular `control_h` tap. `label` is a11y + painted glyph. Same closure as button. */
SzView *sz_view_fab(const char *label, SzViewTapFn on_tap, void *env);
/* Label tap with surface fill and border. Same closure as button. */
SzView *sz_view_outlined_button(const char *label, SzViewTapFn on_tap, void *env);
/* Label tap with no fill. Same closure as button. */
SzView *sz_view_text_button(const char *label, SzViewTapFn on_tap, void *env);
/* Sizes to `child`. `message` is a11y. Shows `message` on hover. Not a tap. */
SzView *sz_view_tooltip(const char *message, SzView *child);
/* Sizes to `child`. Button-3 on the child or a descendant runs `on_tap`.
 * Not a tap target. Primary taps still hit the child. */
SzView *sz_view_on_secondary(SzView *child, SzViewTapFn on_tap, void *env);
/* Sizes to `child`. A tap on a descendant tap target focuses that list.
 * ArrowUp / ArrowDown move among sibling taps. Enter / Space activate.
 * Not a tap target. An open overlay still takes keys. */
SzView *sz_view_focus_group(SzView *child);
/* Sizes to `child`. Paints a muted box mark. Not a tap target. */
SzView *sz_view_placeholder(SzView *child);
/* Sizes to `child`. `label` is a11y. Not a tap target. */
SzView *sz_view_semantics(const char *label, SzView *child);
SzView *sz_view_radio_group(const char *label, SzView *child);
/* Sizes to `child`. `label` is a11y. Omits the child subtree from a11y. Not a tap. */
SzView *sz_view_merge_semantics(const char *label, SzView *child);
/* Sizes to `child`. Tap runs the same closure as button. A11y is `inkwell`. */
SzView *sz_view_ink_well(const char *label, SzViewTapFn on_tap, void *env,
                         SzView *child);
/* Sizes to `child`. Off keeps the size, skips paint and child a11y. Not a tap. */
SzView *sz_view_visibility(SzSignalInt *sig, SzView *child);
/* Lays out `child`. Off reports size 0, skips paint and child a11y. Not a tap. */
SzView *sz_view_offstage(SzSignalInt *sig, SzView *child);
/* Sizes to `child`. Child lays out with unbounded max. Not a tap. */
SzView *sz_view_unconstrained_box(SzView *child);
/* 8 px slot, `control_h` tall, muted vertical hairline. Not a tap target. */
SzView *sz_view_vertical_divider(void);
SzView *sz_view_text_field(SzSignalStr *text, const char *placeholder);
/* Multiline buffer on `text`. Insert includes newline and a two-space soft-tab.
 * Not a TextField: omitted from `[fields]` / field inject indices. */
SzView *sz_view_editor(SzSignalStr *text);
/* Two panes with a named vertical separator. Input writes a percent from 0 to 100. */
SzView *sz_view_split(SzSignalInt *frac, const char *label, SzView *start, SzView *end);
/* Input and focus frame. A split uses its handle. An expansion uses its header. */
SzRect sz_view_control_frame(const SzView *view);
/* A nonzero `open` shows a named modal dialog. Use it in View.stack.
 * Escape and a backdrop tap write 0. Focus returns to the opener on close. */
SzView *sz_view_overlay(SzSignalInt *open, const char *label, SzView *child);
/* Drag uses the handle center and clamps the percent from 0 to 100. */
int sz_view_split_set_at(SzView *view, float x);
int sz_view_collect_splits(SzView *root, SzView **out, int cap);
int sz_view_collect_overlays(SzView *root, SzView **out, int cap);
int sz_view_overlay_is_open(const SzView *view);
int sz_view_split_frac(const SzView *view);
/* Return editor, field, overlay, none, or role:label for the focused control. */
const char *sz_view_focus_kind(SzView *root);
SzView *sz_view_focused(SzView *root);
int sz_view_tab_stop(SzView *view);
int sz_view_focus_next(SzView *root, int backwards, int wrap);
int sz_view_has_open_overlay(SzView *root);
SzView *sz_view_column(void);
SzView *sz_view_row(void);
SzView *sz_view_wrap(void);
/* `cols` columns (min 1). Shown children fill row-major. */
SzView *sz_view_grid(int cols);
/* Select equal columns from the available width. Use one column without a width limit. */
SzView *sz_view_auto_grid(int min_width);
/* Select a branch from the available width in logical pixels. */
SzView *sz_view_adaptive(int64_t min_width, SzView *compact, SzView *wide);
SzView *sz_lang_view_adaptive(int64_t min_width, SzView *compact, SzView *wide);
SzView *sz_view_stack(void);
SzView *sz_view_list(void);
/* Reactive list: children rebuilt from Signal[List[T]] at layout (`- item` texts). */
SzView *sz_view_each(SzSignalList *sig);
SzView *sz_view_virtual_list(int64_t row_height, SzSignalList *items,
                             SzViewKeyFn key, void *key_env,
                             SzViewRowFn render, void *render_env);
/* Measure mounted rows. Use estimate for rows with no measurement. */
SzView *sz_view_measured_list(int64_t estimate, SzSignalList *items,
                             SzViewKeyFn key, void *key_env,
                             SzViewRowFn render, void *render_env);
/* Like `sz_view_each`, but `fn` builds each child from the element. */
SzView *sz_view_each_map(SzSignalList *sig, SzViewEachFn fn, void *env);
SzView *sz_view_scroll(SzView *child);
/* Like `sz_view_scroll`, but the pan axis is x (unbounded child width). */
SzView *sz_view_scroll_h(SzView *child);
/* Divide remaining row or column space by weight. Values below one use one. */
SzView *sz_view_expanded(int64_t weight, SzView *child);
/* Tight cross-axis in Column (width) or Row (height). Main axis stays intrinsic. */
SzView *sz_view_stretch(SzView *child);
SzView *sz_view_center(SzView *child);
/* ax/ay: 0=start (left/top), 1=center, 2=end (right/bottom). */
SzView *sz_view_align(int ax, int ay, SzView *child);
/* Offset (x, y) from the parent Stack origin; x/y clamped to >= 0. */
SzView *sz_view_positioned(int x, int y, SzView *child);
/* Uniform inset in px; deflates max width/height for the child. */
SzView *sz_view_padding(int pad, SzView *child);
/* Tight w x h slot; clamped to incoming max. Child laid out at origin. */
SzView *sz_view_sized(int w, int h, SzView *child);
/* Floor on child size; 0 on an axis means no min. Clamped to incoming max. */
SzView *sz_view_min_size(int w, int h, SzView *child);
/* Ceiling on child size; 0 on an axis means no cap. Incoming max still wins when tighter. */
SzView *sz_view_max_size(int w, int h, SzView *child);
/* Pass constraints through; size to child; clip paint to this frame. */
SzView *sz_view_clip(SzView *child);
/* Pass constraints through; size to child; scale paint alpha (`pct` 0-100). */
SzView *sz_view_opacity(int pct, SzView *child);
SzView *sz_view_animated_opacity(SzSignalInt *value, int64_t pumps, SzView *child);
SzView *sz_view_reduced_motion(SzSignalInt *reduced, SzView *child);
int sz_view_animated_opacity_value(const SzView *view);
int sz_view_motion_pending(const SzView *root);
int sz_view_transition_pending(const SzView *root);
int sz_view_paint_opacity(const SzView *view);
void sz_view_reduce_motion(SzView *root, int reduced);
void sz_view_pause_motion(SzView *root, int paused);
void sz_view_transitions_dump_json(SzView *root, FILE *file);
/* Cap wrapped text lines; `0` = no cap. Nested caps take the tighter value. */
SzView *sz_view_max_lines(int n, SzView *child);
/* Skip hit-test on this subtree; taps pass through to widgets behind. */
SzView *sz_view_ignore_pointer(SzView *child);
/* Skip child hit-test; this frame blocks taps behind. */
SzView *sz_view_absorb_pointer(SzView *child);
/* Pass constraints through; size to child; skip a11y dump and field collect. */
SzView *sz_view_exclude_semantics(SzView *child);
/* Pass constraints through; size to child. Cap extra wrapped lines and paint `...`. */
SzView *sz_view_ellipsis(SzView *child);
/* Pass constraints through; size to child; paint `View.text` with `argb`. */
SzView *sz_view_text_color(uint32_t argb, SzView *child);
/* Pass constraints through; size to child; Column/Row/Wrap/Grid/List spacing `n` px (`0` = none). */
SzView *sz_view_gap(int n, SzView *child);
/* Pass constraints through; size to child; `View.text` measure/paint size `n` px (min 1). */
SzView *sz_view_font_size(int n, SzView *child);
/* Pass constraints through; size to child; paint `n` px `argb` stroke inside the frame. */
SzView *sz_view_border(int n, uint32_t argb, SzView *child);
/* Pass constraints through; size to child; clip paint to a rounded rect of `n` px (`0` = square). */
SzView *sz_view_radius(int n, SzView *child);
/* Pass constraints through; size to child; paint `argb` behind the child. */
SzView *sz_view_background(uint32_t argb, SzView *child);
/* Largest rw:rh box that fits incoming max; child laid out in that tight slot. */
SzView *sz_view_aspect_ratio(int rw, int rh, SzView *child);
/* Percent of incoming max (1-100); 0 on an axis sizes to the child. */
SzView *sz_view_fraction(int wpct, int hpct, SzView *child);
typedef struct SzImage SzImage;
/* Decode immutable image data. The caller owns a successful result. */
SzImage *sz_image_decode(const uint8_t *bytes, size_t len, const char **error);
void sz_image_free(SzImage *image);
int64_t sz_image_width(const SzImage *image);
int64_t sz_image_height(const SzImage *image);
SzAdt *sz_lang_image_decode(const SzString *bytes);
/* Retain the image. Use the label for accessibility. */
SzView *sz_view_image(SzImage *image, const char *label);
SzView *sz_view_icon(char glyph, uint32_t argb);
SzView *sz_view_link(const char *label, const char *route);
SzView *sz_view_breadcrumb(void);
SzView *sz_view_nav_tile(char glyph, const char *title, const char *route);
/* Select the Index Book section whose ID equals `id`. 1 if a page changed. */
int sz_view_navigate(SzView *root, const char *id);
/* Visible iff Signal.get(sig) == value; returns child. */
SzView *sz_view_show_when(SzSignalInt *sig, int64_t value, SzView *child);

void sz_view_add_child(SzView *parent, SzView *child);
void sz_view_clear_children(SzView *parent);
/* Release one reference. The last release frees children and buffers. */
void sz_view_free(SzView *view);
/* One node, `role:label`. Does not walk children. */
SzString *sz_view_show(const SzView *view);

SzViewKind sz_view_kind(const SzView *view);
SzRect sz_view_frame(const SzView *view);
uintptr_t sz_view_id(const SzView *view);
SzView *sz_view_find(SzView *root, uintptr_t id);
/* Button, checkbox, slider, and other `tap N` targets (a11y preorder collect). */
int sz_view_is_tap_target(const SzView *view);
/* Write the nearest allowed step from x. Return 1 for a slider. */
int sz_view_slider_set_at(SzView *view, float x);

/* Layout + hit-test (also run inside pump / inject). */
void sz_view_layout(SzView *root, float width, float height, const SzTheme *theme);
/* The top open dialog, or root when no dialog is open. */
SzView *sz_view_input_scope(SzView *root);
SzView *sz_view_hit_test(SzView *root, float x, float y);
/* Return the nearest tooltip ancestor with a nonempty message. */
SzView *sz_view_tooltip_owner(SzView *view);
int sz_view_tooltip_visible(const SzView *view);
SzRect sz_view_tooltip_frame(const SzView *view);
/* Return the tooltip at its trigger or visible popup. */
SzView *sz_view_tooltip_at(SzView *root, float x, float y);
/* Run the innermost onSecondary handler at (x, y). 1 if a handler ran. */
int sz_view_handle_secondary(SzView *root, float x, float y);
/* Clear hover marks. Mark the tooltip at (x, y). 1 if a tooltip is hovered. */
int sz_view_set_hover_at(SzView *root, float x, float y);
void sz_view_clear_hover(SzView *root);
/* Mark the primary button under the pointer. Clear all marks when inactive. */
void sz_view_set_pressed_at(SzView *root, float x, float y, int active);
int sz_view_handle_tap(SzView *root, float x, float y);
/* Fire the tap handler on `target` (no hit-test). Sliders use x. */
int sz_view_activate(SzView *root, SzView *target, float x, float y);
/* Layout + activate `target` and mark the session dirty (inject `tap`). */
int sz_ui_session_activate_view(SzUiSession *session, SzView *target);
/* Focus dump-index `index` (starred field when index < 0), set caret, mark dirty.
 * Collapses the selection to that offset. Index < 0 uses the focused editor
 * when that is the edit target. */
int sz_ui_session_set_caret(SzUiSession *session, int index, int offset);
/* Focus dump-index `index` (starred field when index < 0), set selection
 * `[start, end)` (byte offsets; order does not matter), mark dirty.
 * Index < 0 uses the focused editor when that is the edit target. */
int sz_ui_session_set_sel(SzUiSession *session, int index, int start, int end);
/* Copy the starred-field or focused-editor selection into the session clipboard.
 * Syncs the OS pasteboard when a Desktop/Mobile embedder is present. Empty
 * selection is a no-op. */
int sz_ui_session_copy(SzUiSession *session);
/* Copy then delete the starred-field or focused-editor selection. */
int sz_ui_session_cut(SzUiSession *session);
/* Replace the starred-field or focused-editor selection (or insert at the
 * caret) with `text`. NULL `text` uses the session clipboard, after a
 * Desktop/Mobile OS pull. */
int sz_ui_session_paste(SzUiSession *session, const char *text);
/* Insert UTF-8 at the caret. If backspace is nonzero, delete the preceding code point.
 * Replace or delete an active selection.
 * Use the focused TextField or editor, then the first field, then the first editor. */
int sz_view_handle_text_edit(SzView *root, const char *text, int backspace);
/* Named key on the focused TextField or editor, or on a focused focus-group
 * row. Backspace / Delete / arrows / Home / End use the caret. Shift+arrows /
 * Shift+Home / Shift+End extend the selection. On an editor, Enter inserts a
 * newline, Tab inserts two spaces, and ArrowUp / ArrowDown move by line. With
 * a focus-group row focused and no overlay, ArrowUp / ArrowDown move among
 * sibling taps and Enter / Space activate. An open overlay takes keys.
 * Nonempty `text` inserts UTF-8 (replaces a selection). Unused names inject
 * and no-op. */
int sz_view_handle_key(SzView *root, const char *key, const char *text,
                       int mods);
/* IME preedit on the focused TextField or editor. Nonempty `text` sets the
 * preview and does not enter the committed buffer. Empty `text` commits the
 * preview at the caret (replaces a selection). Escape cancels. */
int sz_view_handle_compose(SzView *root, const char *text);

/* Scroll offset + soft keyboard helpers. */
float sz_view_scroll_x(const SzView *scroll);
float sz_view_scroll_y(const SzView *scroll);
/* Pan on the scroll axis (positive = content up or left). */
void sz_view_scroll_by(SzView *scroll, float d);
/* Pan dx/dy with a max clamp. Horizontal Scroll maps dy onto x when dx is 0.
 * 1 if the offset changed. */
int sz_view_scroll_pan(SzView *scroll, float dx, float dy);
/* Innermost Scroll or editor under (x,y) that can pan this wheel. NULL if
 * none. Does not pan. */
SzView *sz_view_scroll_wheel_target(SzView *root, float x, float y, float dx,
                                    float dy);
int sz_view_scroll_is_h(const SzView *scroll);
SzView *sz_view_scroll_at(SzView *root, float x, float y);
/* 1 if a TextField or editor is focused (soft-keyboard show). */
int sz_view_has_focused_text_field(SzView *root);
/* Shown TextFields in a11y preorder (cap 64 for dump / inject). Editors omit. */
int sz_view_collect_text_fields(SzView *root, SzView **out, int cap);
/* Shown editors in a11y preorder (cap 64 for `[editor]` dump). */
int sz_view_collect_editors(SzView *root, SzView **out, int cap);
/* Find the first editor, including hidden editors, for source marks. */
SzView *sz_view_first_editor(SzView *root);
/* Shown tap targets in a11y preorder (cap 64 for dump / `tap N` / `tap id`). */
int sz_view_collect_tap_targets(SzView *root, SzView **out, int cap);
/* Fire the first tap target whose a11y label equals `label`. 1 if it fired. */
int sz_view_tap_label(SzView *root, const char *label);
/* last_hit / afterHit / inject key: `role:label`. NULL writes `NULL`. */
void sz_view_format_hit_id(const SzView *hit, char *buf, size_t cap);
/* Shown Scroll views in a11y preorder (cap 64 for dump / `scroll N`). */
int sz_view_collect_scrolls(SzView *root, SzView **out, int cap);
/* Focused field, else the first collected field (`text`/`type`/`backspace`/`key`). */
SzView *sz_view_text_field_target(SzView *root);
/* Focused TextField or editor, else the first field, else the first editor. */
SzView *sz_view_edit_target(SzView *root);
/* Focus collected field `index`, or the starred target when index < 0. */
int sz_view_focus_text_field_at(SzView *root, int index);
/* Focus the edit target (focused field/editor, else first field, else first editor). */
int sz_view_focus_edit_target(SzView *root);
/* Caret in the focused TextField or editor from measured text advance
 * (not a cell grid). Empty rect if nothing is focused. `theme` supplies
 * font_px (default theme OK). */
SzRect sz_view_caret_rect(SzView *root, const SzTheme *theme);

/* Accessibility hooks (Headless-dumpable; no OS AT bridge yet). */
typedef enum SzA11yRole {
  SZ_A11Y_NONE = 0,
  SZ_A11Y_BUTTON = 1,
  SZ_A11Y_TEXT = 2,
  SZ_A11Y_TEXT_FIELD = 3,
  SZ_A11Y_IMAGE = 4,
  SZ_A11Y_LIST = 5,
  SZ_A11Y_SCROLL = 6,
  SZ_A11Y_CHECKBOX = 7,
  SZ_A11Y_SLIDER = 8,
  SZ_A11Y_RADIO = 9,
  SZ_A11Y_PROGRESS = 10,
  SZ_A11Y_SWITCH = 11,
  SZ_A11Y_CHIP = 12,
  SZ_A11Y_LIST_TILE = 13,
  SZ_A11Y_BADGE = 14,
  SZ_A11Y_CARD = 15,
  SZ_A11Y_DIVIDER = 16,
  SZ_A11Y_EXPANSION = 17,
  SZ_A11Y_ICON_BUTTON = 18,
  SZ_A11Y_VDIV = 19,
  SZ_A11Y_CIRCULAR = 20,
  SZ_A11Y_AVATAR = 21,
  SZ_A11Y_CHECK_TILE = 22,
  SZ_A11Y_SWITCH_TILE = 23,
  SZ_A11Y_RADIO_TILE = 24,
  SZ_A11Y_FAB = 26,
  SZ_A11Y_TOOLTIP = 27,
  SZ_A11Y_OUTLINED = 28,
  SZ_A11Y_TEXT_BUTTON = 29,
  SZ_A11Y_PLACEHOLDER = 30,
  SZ_A11Y_FILTER_CHIP = 31,
  SZ_A11Y_CHOICE_CHIP = 32,
  SZ_A11Y_ACTION_CHIP = 33,
  SZ_A11Y_INPUT_CHIP = 34,
  SZ_A11Y_SEMANTICS = 35,
  SZ_A11Y_MERGE = 36,
  SZ_A11Y_INK_WELL = 37,
  SZ_A11Y_VISIBILITY = 38,
  SZ_A11Y_OFFSTAGE = 39,
  SZ_A11Y_UNCONSTRAINED = 40,
  SZ_A11Y_EDITOR = 41,
  SZ_A11Y_SPLIT = 42,
  SZ_A11Y_OVERLAY = 43,
  SZ_A11Y_HEADING = 44,
  SZ_A11Y_TAB = 45,
  SZ_A11Y_TAB_LIST = 46,
  SZ_A11Y_TAB_PANEL = 47,
  SZ_A11Y_APP_BAR = 48,
  SZ_A11Y_ICON = 49,
  SZ_A11Y_LINK = 50,
  SZ_A11Y_BREADCRUMB = 51,
  SZ_A11Y_NAV_TILE = 52,
  SZ_A11Y_RADIO_GROUP = 53,
  SZ_A11Y_REGION = 54,
  SZ_A11Y_COMBOBOX = 55,
  SZ_A11Y_LISTBOX = 56,
  SZ_A11Y_OPTION = 57
} SzA11yRole;

SzA11yRole sz_view_a11y_role(const SzView *view);
const char *sz_view_a11y_label(const SzView *view);
/* Live TextField string, or "" if not a field. */
const char *sz_view_text_field_value(const SzView *view);
/* Live editor buffer, or "" if not an editor. */
const char *sz_view_editor_value(const SzView *view);
/* Caret byte offset on a TextField (clamped, UTF-8 snapped). 0 if not a field. */
int sz_view_text_field_caret(const SzView *view);
int sz_view_editor_caret(const SzView *view);
/* IME preedit string, or "" when empty / not a field or editor. */
const char *sz_view_text_field_preedit(const SzView *view);
const char *sz_view_editor_preedit(const SzView *view);
/* Set caret byte offset (clamped, UTF-8 snapped). Collapses the selection.
 * 1 if `view` is a TextField. */
int sz_view_set_text_field_caret(SzView *view, int offset);
int sz_view_set_editor_caret(SzView *view, int offset);
/* Byte offset for 1-based line/column (column counts bytes). 0 if not an editor. */
int sz_view_editor_offset_at_line_col(const SzView *view, int line, int col);
/* Selection range on a TextField (`[start, end)` byte offsets, start <= end). */
int sz_view_text_field_sel_start(const SzView *view);
int sz_view_text_field_sel_end(const SzView *view);
int sz_view_editor_sel_start(const SzView *view);
int sz_view_editor_sel_end(const SzView *view);
/* Viewport pan on a View.editor (content origin). 0 if not an editor. */
float sz_view_editor_scroll_x(const SzView *view);
float sz_view_editor_scroll_y(const SzView *view);
int sz_view_editor_undo(SzView *view);
int sz_view_editor_redo(SzView *view);
int sz_view_editor_line_count(const SzView *view);
float sz_view_editor_gutter_w(const SzView *view);
/* 1-based line numbers. Clears marks when n <= 0. */
int sz_view_editor_set_diagnostics(SzView *view, const int *lines,
                                  const int *severities, int n);
int sz_view_editor_diag_count(const SzView *view);
int sz_view_editor_diag_line(const SzView *view, int i);
int sz_view_editor_diag_severity(const SzView *view, int i);
/* Packed LSP `data` (delta line, startChar, length, type, modifiers). Empty
 * keeps the in-widget lexer. */
int sz_view_editor_set_tokens(SzView *view, const int *data, int n);
int sz_view_editor_token_count(const SzView *view);
/* 0-based line/col. Labels copy. Clears when n <= 0. */
int sz_view_editor_set_inlays(SzView *view, const int *lines, const int *cols,
                             const char *const *labels, int n);
int sz_view_editor_inlay_count(const SzView *view);
/* 0-based start/end lines. Clears when n <= 0. */
int sz_view_editor_set_folds(SzView *view, const int *starts, const int *ends,
                            int n);
int sz_view_editor_fold_count(const SzView *view);
/* Set selection: `start` is the anchor, `end` is the caret (both snapped). */
int sz_view_set_text_field_sel(SzView *view, int start, int end);
int sz_view_set_editor_sel(SzView *view, int start, int end);
/* Move the caret to (x, y) without collapsing the selection. TextField uses x. */
int sz_view_edit_extend_to_xy(SzView *view, float x, float y);
/* Depth-first "role:label" lines joined by newlines (caller frees SzString). */
SzString *sz_view_a11y_dump(SzView *root);
/* Typed session schema v=2: the same preorder as a JSON forest. Toggle kinds
 * carry "on", numeric kinds carry "value". Children nest under "children". */
void sz_view_a11y_dump_json(SzView *root, FILE *f);

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
void sz_ui_session_set_rebuild(SzUiSession *session, SzUiRebuildFn fn, void *env,
                               const char *schema);
/* Watch a stamp file. Next pump that sees different contents calls rebuild
 * then replace_root. Missing file snapshots as empty. Headless, Desktop, and
 * Mobile share this path. */
int sz_ui_session_watch(SzUiSession *session, const char *path);
/* Write schema v=2 at any path. Dirty pumps, reload, and IO bridge work
 * update the live dump. Only live dumps include session, heap, and live rows.
 * Live rows have a limit of 32. Fuzz dumps omit these sections.
 * Read scuzz docs commands for the input and dump schemas. */
int sz_ui_session_set_debug_dump(SzUiSession *session, const char *path);
int sz_ui_session_write_dump(SzUiSession *session, const char *path);
/* Write the live debug dump. Return 0 when no dump path is set. */
int sz_ui_session_dump_now(SzUiSession *session);
/* Watch an inject document (schema v=1, kind `inject`).
 * Next pump that sees new contents plays the whole document.
 * Missing = empty. */
int sz_ui_session_set_inject(SzUiSession *session, const char *path);
/* Live record path (Desktop / Mobile drain). Writes the inject schema v=1.
 * Appends only from OS drain, never from script / inject playback. */
int sz_ui_session_set_record(SzUiSession *session, const char *path);
/* Invoke the rebuild factory now. Pump calls this when the stamp changes. */
int sz_ui_session_reload(SzUiSession *session);
/* dlopen `path` (copied to a unique sibling so the OS does not keep a stale
 * image) and set the rebuild factory from exported `sz_ui_reload_rebuild`.
 * Unlink the copy after dlopen. Signals stay in `rebuild_env`. Does not
 * rebuild until reload/stamp. Stamp-watch loads `SCUZZ_UI_RELOAD_CODE` (if
 * set) before rebuild. */
int sz_ui_session_load_code(SzUiSession *session, const char *path);
void sz_ui_unmount(SzUiSession *session);
/* Snapshot PNG / structural dump from SCUZZ_SNAPSHOT_PATH / SCUZZ_FUZZ_DUMP. */
void sz_ui_session_finish(SzUiSession *session);
/* Width/height/scale from args, else SCUZZ_UI_WIDTH / HEIGHT / SCALE. */
void sz_ui_resolve_headless_size(int *width, int *height, double *scale);

/* Layout + paint + present. A pump with no dirty work skips paint. */
int sz_ui_pump_sync(SzUiSession *session);
typedef enum SzQuiesce {
  SZ_QUIESCE_SETTLED = 0,
  SZ_QUIESCE_BUDGET_TRIPPED = 1
} SzQuiesce;

/* Headless TestRuntime: stop new events and pump until idle or the budget
 * (64 pumps); reports which terminal state was reached. */
SzQuiesce sz_ui_quiesce(SzUiSession *session);
int sz_ui_inject_sync(SzUiSession *session, const SzInputEvent *event);
void sz_ui_session_input_time(SzUiSession *session, int64_t milliseconds);
int sz_ui_session_live_inject(SzUiSession *session, const SzInputEvent *event);
int sz_ui_snapshot_png_sync(SzUiSession *session, const char *path);
int sz_ui_snapshot_png_bytes(SzUiSession *session, uint8_t **out, size_t *out_len);

SzUiRuntimeKind sz_ui_session_kind(const SzUiSession *session);
SzView *sz_ui_session_root(SzUiSession *session);
/* Copy `title` into the session. The next present uses it. Headless dumps it. */
int sz_ui_session_set_title(SzUiSession *session, const char *title);
const char *sz_ui_session_title(const SzUiSession *session);
/* Title for a new mount when the app called Ui.setTitle before Ui.run. */
const char *sz_ui_default_title(void);
SzLifecyclePhase sz_ui_session_lifecycle(const SzUiSession *session);
/* 1 while the session is not STOP. `quit` / request_stop clear this. */
int sz_ui_session_alive(const SzUiSession *session);
/* Mark STOP so live pump loops exit. The current pump still finishes. */
void sz_ui_session_request_stop(SzUiSession *session);
int sz_ui_session_keyboard_visible(const SzUiSession *session);
/* Completed pump count. STOP returns 0 without incrementing. */
unsigned sz_ui_session_pumps(const SzUiSession *session);
/* Completed paint count. An idle pump does not increment this. */
unsigned sz_ui_session_paints(const SzUiSession *session);
/* 1 when the next pump must paint (dirty or IO bridge work). */
int sz_ui_session_needs_paint(SzUiSession *session);

/* IO -> UI bridge: post signal writes from completed IO; flushed at pump. */
void sz_ui_bridge_post_int(SzUiSession *session, SzSignalInt *sig, int64_t value);

/* --- language-facing View / Signal (Scuzz Lang-authored UI) ----------- */

SzView *sz_lang_view_text(SzString *text);
/* First-class tap closure: `tap`/`env` come from a compiled `_ => ...` lambda. */
SzView *sz_lang_view_button(SzString *label, SzViewTapFn tap, void *env);
SzView *sz_lang_view_checkbox(SzSignalInt *sig, SzString *label);
SzView *sz_lang_view_radio(SzSignalInt *sig, int64_t value, SzString *label);
SzView *sz_lang_view_slider(SzSignalInt *sig, SzString *label, int64_t minimum, int64_t maximum, int64_t step);
SzView *sz_lang_view_progress(SzSignalInt *sig, SzString *label);
SzView *sz_lang_view_circular_progress(SzSignalInt *sig, SzString *label);
SzView *sz_lang_view_avatar(SzString *label);
SzView *sz_lang_view_switch(SzSignalInt *sig, SzString *label);
SzView *sz_lang_view_chip(SzSignalInt *sig, SzString *label);
SzView *sz_lang_view_filter_chip(SzSignalInt *sig, SzString *label);
SzView *sz_lang_view_choice_chip(SzSignalInt *sig, int64_t value, SzString *label);
SzView *sz_lang_view_action_chip(SzString *label, SzViewTapFn tap, void *env);
SzView *sz_lang_view_input_chip(SzSignalInt *sig, SzString *label);
SzView *sz_lang_view_list_tile(SzString *title, SzView *trailing);
SzView *sz_lang_view_checkbox_list_tile(SzSignalInt *sig, SzString *title);
SzView *sz_lang_view_switch_list_tile(SzSignalInt *sig, SzString *title);
SzView *sz_lang_view_radio_list_tile(SzSignalInt *sig, int64_t value,
                                    SzString *title);
SzView *sz_lang_view_select(SzSignalInt *sig, SzString *label, SzSignalList *choices);
SzView *sz_lang_view_segmented(SzSignalInt *sig, SzString *label, SzList *choices);
SzView *sz_lang_view_badge(SzSignalInt *sig, SzView *child);
SzView *sz_lang_view_section(SzString *id, SzString *title, SzView *child);
SzView *sz_lang_view_index_book(SzSignalInt *selected, SzView *sections);
SzView *sz_lang_view_app_shell(SzView *bar, SzView *body);
SzView *sz_lang_view_app_bar(SzView *title, SzView *actions);
SzView *sz_lang_view_tabs(SzSignalInt *selected, SzView *sections);
SzView *sz_lang_view_card(SzView *child);
SzView *sz_lang_view_divider(void);
SzView *sz_lang_view_expansion_tile(SzSignalInt *sig, SzString *title,
                                   SzView *child);
SzView *sz_lang_view_icon_button(SzString *label, SzViewTapFn tap, void *env);
SzView *sz_lang_view_fab(SzString *label, SzViewTapFn tap, void *env);
SzView *sz_lang_view_outlined_button(SzString *label, SzViewTapFn tap, void *env);
SzView *sz_lang_view_text_button(SzString *label, SzViewTapFn tap, void *env);
SzView *sz_lang_view_action_text(const SzText *text, SzViewTapFn tap, void *env);
SzView *sz_lang_view_tooltip(SzString *message, SzView *child);
SzView *sz_lang_view_on_secondary(SzView *child, SzViewTapFn tap, void *env);
SzView *sz_lang_view_focus_group(SzView *child);
SzView *sz_lang_view_placeholder(SzView *child);
SzView *sz_lang_view_semantics(SzString *label, SzView *child);
SzView *sz_lang_view_radio_group(SzString *label, SzView *child);
SzView *sz_lang_view_merge_semantics(SzString *label, SzView *child);
SzView *sz_lang_view_ink_well(SzString *label, SzViewTapFn tap, void *env,
                             SzView *child);
SzView *sz_lang_view_visibility(SzSignalInt *sig, SzView *child);
SzView *sz_lang_view_offstage(SzSignalInt *sig, SzView *child);
SzView *sz_lang_view_unconstrained_box(SzView *child);
SzView *sz_lang_view_vertical_divider(void);
SzView *sz_lang_view_column(void);
SzView *sz_lang_view_row(void);
SzView *sz_lang_view_wrap(void);
SzView *sz_lang_view_grid(int64_t cols);
SzView *sz_lang_view_auto_grid(int64_t min_width);
SzView *sz_lang_view_stack(void);
SzView *sz_lang_view_each(SzSignalList *sig);
SzView *sz_lang_view_virtual_list(int64_t row_height, SzSignalList *items,
                                  SzViewKeyFn key, void *key_env,
                                  SzViewRowFn render, void *render_env);
SzView *sz_lang_view_measured_list(int64_t estimate, SzSignalList *items,
                                  SzViewKeyFn key, void *key_env,
                                  SzViewRowFn render, void *render_env);
SzView *sz_lang_view_each_map(SzSignalList *sig, SzViewEachFn fn, void *env);
SzView *sz_lang_view_scroll(SzView *child);
SzView *sz_lang_view_scroll_h(SzView *child);
SzView *sz_lang_view_expanded(int64_t weight, SzView *child);
SzView *sz_lang_view_center(SzView *child);
SzView *sz_lang_view_positioned(int64_t x, int64_t y, SzView *child);
SzView *sz_lang_view_padding(int64_t pad, SzView *child);
SzView *sz_lang_view_sized(int64_t w, int64_t h, SzView *child);
SzView *sz_lang_view_min_size(int64_t w, int64_t h, SzView *child);
SzView *sz_lang_view_max_size(int64_t w, int64_t h, SzView *child);
SzView *sz_lang_view_theme(const SzTheme *theme, SzView *child);
SzView *sz_lang_view_text_color(int64_t argb, SzView *child);
SzView *sz_lang_view_font_size(int64_t n, SzView *child);
SzView *sz_lang_view_background(int64_t argb, SzView *child);
SzView *sz_lang_view_aspect_ratio(int64_t rw, int64_t rh, SzView *child);
SzView *sz_lang_view_fraction(int64_t wpct, int64_t hpct, SzView *child);
SzView *sz_lang_view_text_field(SzSignalStr *text, SzString *placeholder);
SzView *sz_lang_view_editor(SzSignalStr *text);
SzView *sz_lang_view_split(SzSignalInt *frac, SzString *label, SzView *start, SzView *end);
SzView *sz_lang_view_overlay(SzSignalInt *open, SzString *label, SzView *child);
SzIo *sz_lang_ui_set_title(SzString *title);
SzIo *sz_lang_ui_set_editor_caret(int64_t line, int64_t col);
SzIo *sz_lang_ui_editor_caret(void);
SzIo *sz_lang_ui_set_editor_diagnostics(SzList *marks);
SzIo *sz_lang_ui_set_editor_tokens(SzList *data);
SzIo *sz_lang_ui_set_editor_inlays(SzList *hints);
SzIo *sz_lang_ui_set_editor_folds(SzList *ranges);
SzView *sz_lang_view_icon(int64_t glyph, int64_t argb);
SzView *sz_lang_view_image(SzImage *image, SzString *label);
SzView *sz_lang_view_link(SzString *label, SzString *route);
SzView *sz_lang_view_breadcrumb(void);
SzView *sz_lang_view_nav_tile(int64_t glyph, SzString *title, SzString *route);
void *sz_lang_view_add_children(SzView *parent, const SzList *children);
SzView *sz_lang_view_show_when(SzSignalInt *sig, int64_t value, SzView *child);

/* Derived Signal[String] from Signal[Int] (recomputed on get / dump). */
SzView *sz_lang_view_bind_text(SzSignalStr *sig);

/* Mount factory View -> pump -> optional scripted tap -> snapshot -> unmount.
 * Construction is a factory so stamp-watch can re-run it. Watches
 * SCUZZ_UI_RELOAD_STAMP when set. On stamp change, loads
 * SCUZZ_UI_RELOAD_CODE (dylib exporting sz_ui_reload_rebuild) if that
 * file exists, then rebuilds. Writes SCUZZ_UI_DEBUG_DUMP on dirty pumps
 * when set (includes [heap]). Plays SCUZZ_UI_INJECT
 * (tap/xy/text/type/key/caret/select/copy/cut/paste/drag/hover/secondary/pump/scroll/backspace/dump/snapshot/reload/quit/resetpeak) when
 * that file changes. `quit` stops the live pump loop. Desktop quit is window
 * close. `resetpeak` resets
 * peak bytes and the heap delta mark. */
SzIo *sz_ui_run_rebuild(SzUiRebuildFn fn, void *env, const char *schema);

/* Code blocks and heading semantics use the shared View tree. */
SzView *sz_view_code(const char *text);
SzView *sz_view_heading(int level, SzView *child);
SzView *sz_lang_view_code(SzString *text);
SzView *sz_lang_view_heading(int64_t level, SzView *child);
SzView *sz_view_take_copy(SzView *root);
const char *sz_view_copy_payload(SzView *view);
void sz_view_copy_result(SzView *view, int success);
void sz_view_focus(SzView *root, SzView *view);
void sz_view_reveal_focus(SzView *root);
void sz_ui_session_invalidate(SzUiSession *session);
void sz_ui_session_focus_view(SzUiSession *session, SzView *view);

#ifdef __cplusplus
}
#endif

#endif /* SCUZZ_UI_H */
