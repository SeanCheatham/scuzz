#include "scuzz_ui.h"

#include "sk_capi.h"

#include "rt_util.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EDITOR_UNDO_MAX 64

typedef struct SzEditHist {
  char *text;
  int caret;
  int sel_anchor;
} SzEditHist;

struct SzView {
  SzViewKind kind;
  SzView *parent;
  SzView **children;
  int child_count;
  int child_cap;
  SzRect frame;
  int interactive; /* participates in hit-test */
  int focused;

  /* common / kind-specific */
  char *text;
  char *prefix;
  char *placeholder;
  SzSignalInt *sig_int;
  SzSignalStr *sig_str;
  SzViewTapFn on_tap;
  void *tap_env;
  uint32_t bg_argb;
  uint32_t fg_argb;
  int img_w;
  int img_h;
  char glyph;
  float scroll_y;
  float scroll_x;
  int scroll_h; /* 1 = pan x (View.scrollH); 0 = pan y */
  float pref_h; /* >0 overrides natural height when set */
  SzView *scroll_child; /* owned as sole child for SCROLL */

  /* a11y */
  SzA11yRole a11y_role;
  char *a11y_label;

  /* showWhen: when show_when_sig != NULL, visible iff signal == show_when_value */
  SzSignalInt *show_when_sig;
  int64_t show_when_value;

  /* View.each: rebuild children from Signal.list at layout (pull). */
  SzSignalList *each_sig;
  SzList *each_seen; /* last synced list pointer (not owned) */
  SzViewEachFn each_fn;
  void *each_env;

  /* View.align: 0=start, 1=center, 2=end on each axis. */
  int align_x;
  int align_y;
  /* View.positioned: offset from Stack origin. */
  int pos_x;
  int pos_y;
  /* View.padding: uniform inset. */
  int pad;
  /* View.radio: value written into sig_int on tap. */
  int64_t radio_value;
  /* TextField / editor caret: byte offset into the live string (UTF-8 snapped). */
  int caret;
  /* TextField / editor selection anchor (UTF-8 snapped). Collapsed when equal to caret. */
  int sel_anchor;
  SzEditHist *undo;
  int undo_n;
  int undo_cap;
  SzEditHist *redo;
  int redo_n;
  int redo_cap;
  int *diag_line;
  int *diag_sev;
  int diag_n;
  /* View.tooltip: 1 when the pointer hovers this node. */
  int hover;
};

static SzView *view_new(SzViewKind kind) {
  SzView *v = (SzView *)sz_alloc_zero(sizeof(SzView));
  v->kind = kind;
  return v;
}

static void view_set_tap(SzView *v, SzViewTapFn on_tap, void *env) {
  v->on_tap = on_tap;
  sz_retain(env);
  v->tap_env = env;
}

static int view_is_shown(const SzView *v);
static void resolve_text(const SzView *v, char *buf, size_t buflen);

static int view_is_shown(const SzView *v) {
  if (!v)
    return 0;
  if (v->show_when_sig &&
      sz_signal_int_get(v->show_when_sig) != v->show_when_value)
    return 0;
  return 1;
}

static int view_visibility_on(const SzView *v) {
  return v && v->kind == SZ_VIEW_VISIBILITY && v->sig_int &&
         sz_signal_int_get(v->sig_int) != 0;
}

static int view_offstage_shown(const SzView *v) {
  return v && v->kind == SZ_VIEW_OFFSTAGE && v->sig_int &&
         sz_signal_int_get(v->sig_int) != 0;
}

static int view_overlay_open(const SzView *v) {
  return v && v->kind == SZ_VIEW_OVERLAY && v->sig_int &&
         sz_signal_int_get(v->sig_int) != 0;
}

static int count_shown_children(const SzView *v) {
  int i, n = 0;
  if (!v)
    return 0;
  for (i = 0; i < v->child_count; i++) {
    if (view_is_shown(v->children[i]))
      n++;
  }
  return n;
}

static int view_accepts_children(SzViewKind kind) {
  return kind == SZ_VIEW_COLUMN || kind == SZ_VIEW_ROW ||
         kind == SZ_VIEW_WRAP || kind == SZ_VIEW_GRID || kind == SZ_VIEW_LIST ||
         kind == SZ_VIEW_SCROLL ||
         kind == SZ_VIEW_EXPANDED || kind == SZ_VIEW_CENTER ||
         kind == SZ_VIEW_ALIGN || kind == SZ_VIEW_STACK ||
         kind == SZ_VIEW_POSITIONED || kind == SZ_VIEW_PADDING ||
         kind == SZ_VIEW_SIZED || kind == SZ_VIEW_MIN_SIZE ||
         kind == SZ_VIEW_BACKGROUND || kind == SZ_VIEW_ASPECT_RATIO ||
         kind == SZ_VIEW_FRACTION || kind == SZ_VIEW_STRETCH ||
         kind == SZ_VIEW_MAX_SIZE || kind == SZ_VIEW_CLIP ||
         kind == SZ_VIEW_OPACITY || kind == SZ_VIEW_MAX_LINES ||
         kind == SZ_VIEW_IGNORE_POINTER || kind == SZ_VIEW_ABSORB_POINTER ||
         kind == SZ_VIEW_EXCLUDE_SEMANTICS || kind == SZ_VIEW_ELLIPSIS ||
         kind == SZ_VIEW_TEXT_COLOR || kind == SZ_VIEW_GAP ||
         kind == SZ_VIEW_FONT_SIZE || kind == SZ_VIEW_BORDER ||
         kind == SZ_VIEW_RADIUS || kind == SZ_VIEW_LIST_TILE ||
         kind == SZ_VIEW_BADGE || kind == SZ_VIEW_CARD ||
         kind == SZ_VIEW_EXPANSION_TILE || kind == SZ_VIEW_TOOLTIP ||
         kind == SZ_VIEW_PLACEHOLDER || kind == SZ_VIEW_SEMANTICS ||
         kind == SZ_VIEW_MERGE_SEMANTICS || kind == SZ_VIEW_INK_WELL ||
         kind == SZ_VIEW_VISIBILITY || kind == SZ_VIEW_OFFSTAGE ||
         kind == SZ_VIEW_UNCONSTRAINED_BOX || kind == SZ_VIEW_SPLIT ||
         kind == SZ_VIEW_OVERLAY;
}

/* Expanded, or Stretch wrapping Expanded. */
static int view_is_flex(const SzView *v) {
  while (v) {
    if (v->kind == SZ_VIEW_EXPANDED)
      return 1;
    if (v->kind == SZ_VIEW_STRETCH && v->child_count > 0) {
      v = v->children[0];
      continue;
    }
    return 0;
  }
  return 0;
}

/* Stretch, or Expanded wrapping Stretch. */
static int view_is_cross_stretch(const SzView *v) {
  while (v) {
    if (v->kind == SZ_VIEW_STRETCH)
      return 1;
    if (v->kind == SZ_VIEW_EXPANDED && v->child_count > 0) {
      v = v->children[0];
      continue;
    }
    return 0;
  }
  return 0;
}

SzViewKind sz_view_kind(const SzView *view) {
  return view ? view->kind : (SzViewKind)0;
}

int sz_view_is_tap_target(const SzView *view) {
  return view &&
         (view->kind == SZ_VIEW_BUTTON || view->kind == SZ_VIEW_CHECKBOX ||
          view->kind == SZ_VIEW_SLIDER || view->kind == SZ_VIEW_RADIO ||
          view->kind == SZ_VIEW_CHOICE_CHIP ||
          view->kind == SZ_VIEW_SWITCH || view->kind == SZ_VIEW_CHIP ||
          view->kind == SZ_VIEW_FILTER_CHIP ||
          view->kind == SZ_VIEW_INPUT_CHIP ||
          view->kind == SZ_VIEW_EXPANSION_TILE ||
          view->kind == SZ_VIEW_ICON_BUTTON ||
          view->kind == SZ_VIEW_FAB ||
          view->kind == SZ_VIEW_OUTLINED_BUTTON ||
          view->kind == SZ_VIEW_TEXT_BUTTON ||
          view->kind == SZ_VIEW_ACTION_CHIP ||
          view->kind == SZ_VIEW_INK_WELL ||
          view->kind == SZ_VIEW_CHECKBOX_LIST_TILE ||
          view->kind == SZ_VIEW_SWITCH_LIST_TILE ||
          view->kind == SZ_VIEW_RADIO_LIST_TILE ||
          view->kind == SZ_VIEW_SEGMENTED);
}

SzRect sz_view_frame(const SzView *view) {
  SzRect z = {0, 0, 0, 0};
  return view ? view->frame : z;
}

void sz_view_add_child(SzView *parent, SzView *child) {
  if (!parent || !child)
    return;
  if (!view_accepts_children(parent->kind))
    sz_panic("sz_view_add_child: parent cannot have children");
  if (parent->child_count >= parent->child_cap) {
    int ncap = parent->child_cap ? parent->child_cap * 2 : 4;
    SzView **n =
        (SzView **)sz_alloc((size_t)ncap * sizeof(SzView *));
    if (parent->children) {
      memcpy(n, parent->children, (size_t)parent->child_count * sizeof(SzView *));
      sz_free(parent->children);
    }
    parent->children = n;
    parent->child_cap = ncap;
  }
  parent->children[parent->child_count++] = child;
  child->parent = parent;
  if (parent->kind == SZ_VIEW_SCROLL)
    parent->scroll_child = child;
}

SzView *sz_view_text(const char *text) {
  SzView *v = view_new(SZ_VIEW_TEXT);
  v->text = sz_strdup(text);
  v->a11y_role = SZ_A11Y_TEXT;
  v->a11y_label = sz_strdup(text);
  return v;
}

SzView *sz_view_text_signal_int(SzSignalInt *sig, const char *prefix) {
  SzView *v = view_new(SZ_VIEW_TEXT);
  v->a11y_role = SZ_A11Y_TEXT;
  v->a11y_label = sz_strdup(prefix ? prefix : "value");
  v->sig_int = sig;
  v->prefix = sz_strdup(prefix ? prefix : "");
  return v;
}

SzView *sz_view_text_signal_str(SzSignalStr *sig) {
  SzView *v = view_new(SZ_VIEW_TEXT);
  v->sig_str = sig;
  v->a11y_role = SZ_A11Y_TEXT;
  return v;
}

SzView *sz_view_button(const char *label, SzViewTapFn on_tap, void *env) {
  SzView *v = view_new(SZ_VIEW_BUTTON);
  v->text = sz_strdup(label);
  view_set_tap(v, on_tap, env);
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_BUTTON;
  v->a11y_label = sz_strdup(label);
  return v;
}

SzView *sz_view_icon_button(const char *label, SzViewTapFn on_tap, void *env) {
  SzView *v = view_new(SZ_VIEW_ICON_BUTTON);
  v->text = sz_strdup(label ? label : "");
  view_set_tap(v, on_tap, env);
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_ICON_BUTTON;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_fab(const char *label, SzViewTapFn on_tap, void *env) {
  SzView *v = view_new(SZ_VIEW_FAB);
  v->text = sz_strdup(label ? label : "");
  view_set_tap(v, on_tap, env);
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_FAB;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_outlined_button(const char *label, SzViewTapFn on_tap,
                               void *env) {
  SzView *v = view_new(SZ_VIEW_OUTLINED_BUTTON);
  v->text = sz_strdup(label ? label : "");
  view_set_tap(v, on_tap, env);
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_OUTLINED;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_text_button(const char *label, SzViewTapFn on_tap, void *env) {
  SzView *v = view_new(SZ_VIEW_TEXT_BUTTON);
  v->text = sz_strdup(label ? label : "");
  view_set_tap(v, on_tap, env);
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_TEXT_BUTTON;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_vertical_divider(void) {
  SzView *v = view_new(SZ_VIEW_VERTICAL_DIVIDER);
  v->a11y_role = SZ_A11Y_VDIV;
  v->a11y_label = sz_strdup("vdiv");
  return v;
}

SzView *sz_view_checkbox(SzSignalInt *sig, const char *label) {
  SzView *v = view_new(SZ_VIEW_CHECKBOX);
  v->sig_int = sig;
  v->text = sz_strdup(label ? label : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_CHECKBOX;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_radio(SzSignalInt *sig, int64_t value, const char *label) {
  SzView *v = view_new(SZ_VIEW_RADIO);
  v->sig_int = sig;
  v->radio_value = value;
  v->text = sz_strdup(label ? label : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_RADIO;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_slider(SzSignalInt *sig) {
  SzView *v = view_new(SZ_VIEW_SLIDER);
  v->sig_int = sig;
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_SLIDER;
  v->a11y_label = sz_strdup("slider");
  return v;
}

SzView *sz_view_split(SzSignalInt *frac, SzView *start, SzView *end) {
  SzView *v = view_new(SZ_VIEW_SPLIT);
  v->sig_int = frac;
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_SPLIT;
  v->a11y_label = sz_strdup("split");
  if (start)
    sz_view_add_child(v, start);
  if (end)
    sz_view_add_child(v, end);
  return v;
}

SzView *sz_view_overlay(SzSignalInt *open, SzView *child) {
  SzView *v = view_new(SZ_VIEW_OVERLAY);
  v->sig_int = open;
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_OVERLAY;
  v->a11y_label = sz_strdup("overlay");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

int sz_view_overlay_is_open(const SzView *view) {
  return view_overlay_open(view) ? 1 : 0;
}

int sz_view_split_frac(const SzView *view) {
  int64_t n;
  if (!view || view->kind != SZ_VIEW_SPLIT)
    return 0;
  n = view->sig_int ? sz_signal_int_get(view->sig_int) : 50;
  if (n < 0)
    n = 0;
  if (n > 100)
    n = 100;
  return (int)n;
}

SzView *sz_view_progress(SzSignalInt *sig) {
  SzView *v = view_new(SZ_VIEW_PROGRESS);
  v->sig_int = sig;
  v->a11y_role = SZ_A11Y_PROGRESS;
  v->a11y_label = sz_strdup("progress");
  return v;
}

SzView *sz_view_circular_progress(SzSignalInt *sig) {
  SzView *v = view_new(SZ_VIEW_CIRCULAR_PROGRESS);
  v->sig_int = sig;
  v->a11y_role = SZ_A11Y_CIRCULAR;
  v->a11y_label = sz_strdup("circular");
  return v;
}

SzView *sz_view_avatar(const char *label) {
  SzView *v = view_new(SZ_VIEW_AVATAR);
  v->text = sz_strdup(label ? label : "");
  v->a11y_role = SZ_A11Y_AVATAR;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_switch(SzSignalInt *sig, const char *label) {
  SzView *v = view_new(SZ_VIEW_SWITCH);
  v->sig_int = sig;
  v->text = sz_strdup(label ? label : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_SWITCH;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_chip(SzSignalInt *sig, const char *label) {
  SzView *v = view_new(SZ_VIEW_CHIP);
  v->sig_int = sig;
  v->text = sz_strdup(label ? label : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_CHIP;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_filter_chip(SzSignalInt *sig, const char *label) {
  SzView *v = view_new(SZ_VIEW_FILTER_CHIP);
  v->sig_int = sig;
  v->text = sz_strdup(label ? label : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_FILTER_CHIP;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_choice_chip(SzSignalInt *sig, int64_t value, const char *label) {
  SzView *v = view_new(SZ_VIEW_CHOICE_CHIP);
  v->sig_int = sig;
  v->radio_value = value;
  v->text = sz_strdup(label ? label : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_CHOICE_CHIP;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_action_chip(const char *label, SzViewTapFn on_tap, void *env) {
  SzView *v = view_new(SZ_VIEW_ACTION_CHIP);
  v->text = sz_strdup(label ? label : "");
  view_set_tap(v, on_tap, env);
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_ACTION_CHIP;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_input_chip(SzSignalInt *sig, const char *label) {
  SzView *v = view_new(SZ_VIEW_INPUT_CHIP);
  v->sig_int = sig;
  v->text = sz_strdup(label ? label : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_INPUT_CHIP;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_list_tile(const char *title, SzView *trailing) {
  SzView *v = view_new(SZ_VIEW_LIST_TILE);
  v->text = sz_strdup(title ? title : "");
  v->a11y_role = SZ_A11Y_LIST_TILE;
  v->a11y_label = sz_strdup(title ? title : "");
  if (trailing)
    sz_view_add_child(v, trailing);
  return v;
}

SzView *sz_view_checkbox_list_tile(SzSignalInt *sig, const char *title) {
  SzView *v = view_new(SZ_VIEW_CHECKBOX_LIST_TILE);
  v->sig_int = sig;
  v->text = sz_strdup(title ? title : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_CHECK_TILE;
  v->a11y_label = sz_strdup(title ? title : "");
  return v;
}

SzView *sz_view_switch_list_tile(SzSignalInt *sig, const char *title) {
  SzView *v = view_new(SZ_VIEW_SWITCH_LIST_TILE);
  v->sig_int = sig;
  v->text = sz_strdup(title ? title : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_SWITCH_TILE;
  v->a11y_label = sz_strdup(title ? title : "");
  return v;
}

SzView *sz_view_radio_list_tile(SzSignalInt *sig, int64_t value,
                               const char *title) {
  SzView *v = view_new(SZ_VIEW_RADIO_LIST_TILE);
  v->sig_int = sig;
  v->radio_value = value;
  v->text = sz_strdup(title ? title : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_RADIO_TILE;
  v->a11y_label = sz_strdup(title ? title : "");
  return v;
}

SzView *sz_view_segmented(SzSignalInt *sig, const char *left, const char *right) {
  SzView *v = view_new(SZ_VIEW_SEGMENTED);
  v->sig_int = sig;
  v->text = sz_strdup(left ? left : "");
  v->prefix = sz_strdup(right ? right : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_SEGMENTED;
  v->a11y_label = sz_strdup("segmented");
  return v;
}

SzView *sz_view_badge(SzSignalInt *sig, SzView *child) {
  SzView *v = view_new(SZ_VIEW_BADGE);
  v->sig_int = sig;
  v->a11y_role = SZ_A11Y_BADGE;
  v->a11y_label = sz_strdup("badge");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_card(SzView *child) {
  SzView *v = view_new(SZ_VIEW_CARD);
  v->a11y_role = SZ_A11Y_CARD;
  v->a11y_label = sz_strdup("card");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_tooltip(const char *message, SzView *child) {
  SzView *v = view_new(SZ_VIEW_TOOLTIP);
  v->text = sz_strdup(message ? message : "");
  v->a11y_role = SZ_A11Y_TOOLTIP;
  v->a11y_label = sz_strdup(message ? message : "");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_placeholder(SzView *child) {
  SzView *v = view_new(SZ_VIEW_PLACEHOLDER);
  v->a11y_role = SZ_A11Y_PLACEHOLDER;
  v->a11y_label = sz_strdup("ph");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_semantics(const char *label, SzView *child) {
  SzView *v = view_new(SZ_VIEW_SEMANTICS);
  v->text = sz_strdup(label ? label : "");
  v->a11y_role = SZ_A11Y_SEMANTICS;
  v->a11y_label = sz_strdup(label ? label : "");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_merge_semantics(const char *label, SzView *child) {
  SzView *v = view_new(SZ_VIEW_MERGE_SEMANTICS);
  v->text = sz_strdup(label ? label : "");
  v->a11y_role = SZ_A11Y_MERGE;
  v->a11y_label = sz_strdup(label ? label : "");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_ink_well(const char *label, SzViewTapFn on_tap, void *env,
                         SzView *child) {
  SzView *v = view_new(SZ_VIEW_INK_WELL);
  v->text = sz_strdup(label ? label : "");
  view_set_tap(v, on_tap, env);
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_INK_WELL;
  v->a11y_label = sz_strdup(label ? label : "");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_visibility(SzSignalInt *sig, SzView *child) {
  SzView *v = view_new(SZ_VIEW_VISIBILITY);
  v->sig_int = sig;
  v->a11y_role = SZ_A11Y_VISIBILITY;
  v->a11y_label = sz_strdup("visibility");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_offstage(SzSignalInt *sig, SzView *child) {
  SzView *v = view_new(SZ_VIEW_OFFSTAGE);
  v->sig_int = sig;
  v->a11y_role = SZ_A11Y_OFFSTAGE;
  v->a11y_label = sz_strdup("offstage");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_unconstrained_box(SzView *child) {
  SzView *v = view_new(SZ_VIEW_UNCONSTRAINED_BOX);
  v->a11y_role = SZ_A11Y_UNCONSTRAINED;
  v->a11y_label = sz_strdup("box");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_divider(void) {
  SzView *v = view_new(SZ_VIEW_DIVIDER);
  v->a11y_role = SZ_A11Y_DIVIDER;
  v->a11y_label = sz_strdup("divider");
  return v;
}

SzView *sz_view_expansion_tile(SzSignalInt *sig, const char *title, SzView *child) {
  SzView *v = view_new(SZ_VIEW_EXPANSION_TILE);
  v->sig_int = sig;
  v->text = sz_strdup(title ? title : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_EXPANSION;
  v->a11y_label = sz_strdup(title ? title : "");
  if (child) {
    child->show_when_sig = sig;
    child->show_when_value = 1;
    sz_view_add_child(v, child);
  }
  return v;
}

static int64_t slider_clamp(int64_t n) {
  if (n < 0)
    return 0;
  if (n > 100)
    return 100;
  return n;
}

int sz_view_slider_set_at(SzView *view, float x) {
  float t;
  int64_t n;
  if (!view || view->kind != SZ_VIEW_SLIDER || !view->sig_int)
    return 0;
  if (view->frame.w <= 0.f)
    n = 0;
  else {
    t = (x - view->frame.x) / view->frame.w;
    if (t < 0.f)
      t = 0.f;
    if (t > 1.f)
      t = 1.f;
    n = (int64_t)(t * 100.f + 0.5f);
  }
  sz_signal_int_set(view->sig_int, slider_clamp(n));
  return 1;
}

int sz_view_split_set_at(SzView *view, float x) {
  float t;
  int64_t n;
  if (!view || view->kind != SZ_VIEW_SPLIT || !view->sig_int)
    return 0;
  if (view->frame.w <= 0.f)
    n = 0;
  else {
    t = (x - view->frame.x) / view->frame.w;
    if (t < 0.f)
      t = 0.f;
    if (t > 1.f)
      t = 1.f;
    n = (int64_t)(t * 100.f + 0.5f);
  }
  sz_signal_int_set(view->sig_int, slider_clamp(n));
  return 1;
}

SzView *sz_view_text_field(SzSignalStr *text, const char *placeholder) {
  SzView *v = view_new(SZ_VIEW_TEXT_FIELD);
  const char *s;
  v->sig_str = text;
  v->placeholder = sz_strdup(placeholder ? placeholder : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_TEXT_FIELD;
  v->a11y_label = sz_strdup(placeholder ? placeholder : "text field");
  s = text ? sz_signal_str_get(text) : "";
  v->caret = s ? (int)strlen(s) : 0;
  v->sel_anchor = v->caret;
  return v;
}

SzView *sz_view_editor(SzSignalStr *text) {
  SzView *v = view_new(SZ_VIEW_EDITOR);
  const char *s;
  v->sig_str = text;
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_EDITOR;
  v->a11y_label = sz_strdup("editor");
  s = text ? sz_signal_str_get(text) : "";
  v->caret = s ? (int)strlen(s) : 0;
  v->sel_anchor = v->caret;
  return v;
}

SzA11yRole sz_view_a11y_role(const SzView *view) {
  return view ? view->a11y_role : SZ_A11Y_NONE;
}

const char *sz_view_a11y_label(const SzView *view) {
  return view && view->a11y_label ? view->a11y_label : "";
}

const char *sz_view_text_field_value(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_TEXT_FIELD || !view->sig_str)
    return "";
  return sz_signal_str_get(view->sig_str);
}

const char *sz_view_editor_value(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_EDITOR || !view->sig_str)
    return "";
  return sz_signal_str_get(view->sig_str);
}

static int view_is_edit(const SzView *v) {
  return v && (v->kind == SZ_VIEW_TEXT_FIELD || v->kind == SZ_VIEW_EDITOR);
}

static const char *field_cstr(const SzView *v) {
  const char *s;
  if (!view_is_edit(v) || !v->sig_str)
    return "";
  s = sz_signal_str_get(v->sig_str);
  return s ? s : "";
}

static int utf8_snap(const char *s, int i) {
  int n;
  if (!s)
    return 0;
  n = (int)strlen(s);
  if (i <= 0)
    return 0;
  if (i >= n)
    return n;
  while (i > 0 && ((unsigned char)s[i] & 0xc0) == 0x80)
    i--;
  return i;
}

static int field_caret_clamped(const SzView *v) {
  const char *s = field_cstr(v);
  int n = (int)strlen(s);
  int c = v ? v->caret : 0;
  if (c < 0)
    c = 0;
  if (c > n)
    c = n;
  return utf8_snap(s, c);
}

int sz_view_text_field_caret(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_TEXT_FIELD)
    return 0;
  return field_caret_clamped(view);
}

int sz_view_editor_caret(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  return field_caret_clamped(view);
}

static int edit_set_caret(SzView *view, int offset) {
  if (!view_is_edit(view))
    return 0;
  view->caret = offset;
  view->caret = field_caret_clamped(view);
  view->sel_anchor = view->caret;
  return 1;
}

int sz_view_set_text_field_caret(SzView *view, int offset) {
  if (!view || view->kind != SZ_VIEW_TEXT_FIELD)
    return 0;
  return edit_set_caret(view, offset);
}

int sz_view_set_editor_caret(SzView *view, int offset) {
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  return edit_set_caret(view, offset);
}

int sz_view_editor_offset_at_line_col(const SzView *view, int line, int col) {
  const char *s;
  int cur_line = 1;
  int cur_col = 1;
  int i = 0;
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  if (line < 1)
    line = 1;
  if (col < 1)
    col = 1;
  s = field_cstr(view);
  if (!s)
    return 0;
  while (s[i]) {
    if (cur_line == line && cur_col == col)
      return i;
    if (s[i] == '\n') {
      if (cur_line == line)
        return i;
      cur_line++;
      cur_col = 1;
    } else
      cur_col++;
    i++;
  }
  return i;
}

static int field_anchor_clamped(const SzView *v) {
  const char *s = field_cstr(v);
  int n = (int)strlen(s);
  int a = v ? v->sel_anchor : 0;
  if (a < 0)
    a = 0;
  if (a > n)
    a = n;
  return utf8_snap(s, a);
}

static void field_sel_bounds(const SzView *v, int *start, int *end) {
  int c = field_caret_clamped(v);
  int a = field_anchor_clamped(v);
  if (c < a) {
    *start = c;
    *end = a;
  } else {
    *start = a;
    *end = c;
  }
}

int sz_view_text_field_sel_start(const SzView *view) {
  int a, b;
  if (!view || view->kind != SZ_VIEW_TEXT_FIELD)
    return 0;
  field_sel_bounds(view, &a, &b);
  return a;
}

int sz_view_text_field_sel_end(const SzView *view) {
  int a, b;
  if (!view || view->kind != SZ_VIEW_TEXT_FIELD)
    return 0;
  field_sel_bounds(view, &a, &b);
  return b;
}

int sz_view_editor_sel_start(const SzView *view) {
  int a, b;
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  field_sel_bounds(view, &a, &b);
  return a;
}

float sz_view_editor_scroll_x(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0.f;
  return view->scroll_x;
}

float sz_view_editor_scroll_y(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0.f;
  return view->scroll_y;
}

int sz_view_editor_sel_end(const SzView *view) {
  int a, b;
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  field_sel_bounds(view, &a, &b);
  return b;
}

static int edit_set_sel(SzView *view, int start, int end) {
  if (!view_is_edit(view))
    return 0;
  view->sel_anchor = start;
  view->caret = end;
  view->sel_anchor = field_anchor_clamped(view);
  view->caret = field_caret_clamped(view);
  return 1;
}

int sz_view_set_text_field_sel(SzView *view, int start, int end) {
  if (!view || view->kind != SZ_VIEW_TEXT_FIELD)
    return 0;
  return edit_set_sel(view, start, end);
}

int sz_view_set_editor_sel(SzView *view, int start, int end) {
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  return edit_set_sel(view, start, end);
}

static int field_has_sel(const SzView *v) {
  int a, b;
  field_sel_bounds(v, &a, &b);
  return b > a;
}

static const char *a11y_role_name(SzA11yRole role) {
  switch (role) {
  case SZ_A11Y_BUTTON:
    return "button";
  case SZ_A11Y_TEXT:
    return "text";
  case SZ_A11Y_TEXT_FIELD:
    return "textfield";
  case SZ_A11Y_IMAGE:
    return "image";
  case SZ_A11Y_LIST:
    return "list";
  case SZ_A11Y_SCROLL:
    return "scroll";
  case SZ_A11Y_CHECKBOX:
    return "checkbox";
  case SZ_A11Y_SLIDER:
    return "slider";
  case SZ_A11Y_RADIO:
    return "radio";
  case SZ_A11Y_PROGRESS:
    return "progress";
  case SZ_A11Y_SWITCH:
    return "switch";
  case SZ_A11Y_CHIP:
    return "chip";
  case SZ_A11Y_FILTER_CHIP:
    return "filterchip";
  case SZ_A11Y_CHOICE_CHIP:
    return "choicechip";
  case SZ_A11Y_ACTION_CHIP:
    return "actionchip";
  case SZ_A11Y_INPUT_CHIP:
    return "inputchip";
  case SZ_A11Y_LIST_TILE:
    return "listtile";
  case SZ_A11Y_BADGE:
    return "badge";
  case SZ_A11Y_CARD:
    return "card";
  case SZ_A11Y_DIVIDER:
    return "divider";
  case SZ_A11Y_EXPANSION:
    return "expansion";
  case SZ_A11Y_ICON_BUTTON:
    return "iconbutton";
  case SZ_A11Y_VDIV:
    return "vdiv";
  case SZ_A11Y_CIRCULAR:
    return "circular";
  case SZ_A11Y_AVATAR:
    return "avatar";
  case SZ_A11Y_CHECK_TILE:
    return "checktile";
  case SZ_A11Y_SWITCH_TILE:
    return "switchtile";
  case SZ_A11Y_RADIO_TILE:
    return "radiotile";
  case SZ_A11Y_SEGMENTED:
    return "segmented";
  case SZ_A11Y_FAB:
    return "fab";
  case SZ_A11Y_TOOLTIP:
    return "tooltip";
  case SZ_A11Y_OUTLINED:
    return "outlined";
  case SZ_A11Y_TEXT_BUTTON:
    return "textbutton";
  case SZ_A11Y_PLACEHOLDER:
    return "placeholder";
  case SZ_A11Y_SEMANTICS:
    return "semantics";
  case SZ_A11Y_MERGE:
    return "merge";
  case SZ_A11Y_INK_WELL:
    return "inkwell";
  case SZ_A11Y_VISIBILITY:
    return "visibility";
  case SZ_A11Y_OFFSTAGE:
    return "offstage";
  case SZ_A11Y_UNCONSTRAINED:
    return "unconstrained";
  case SZ_A11Y_EDITOR:
    return "editor";
  case SZ_A11Y_SPLIT:
    return "split";
  case SZ_A11Y_OVERLAY:
    return "overlay";
  default:
    return "none";
  }
}

static void a11y_dump_node(SzView *v, char *buf, size_t cap, size_t *len) {
  int i;
  if (!v || !buf || !len || !view_is_shown(v))
    return;
  if (v->kind == SZ_VIEW_EXCLUDE_SEMANTICS)
    return;
  if (v->a11y_role != SZ_A11Y_NONE) {
    char line[256];
    char live[256];
    const char *label = v->a11y_label ? v->a11y_label : "";
    int n;
    if (v->kind == SZ_VIEW_TEXT && (v->sig_int || v->sig_str)) {
      resolve_text(v, live, sizeof live);
      label = live;
    }
    if (v->kind == SZ_VIEW_CHECKBOX || v->kind == SZ_VIEW_SWITCH ||
        v->kind == SZ_VIEW_CHIP || v->kind == SZ_VIEW_FILTER_CHIP ||
        v->kind == SZ_VIEW_INPUT_CHIP ||
        v->kind == SZ_VIEW_EXPANSION_TILE ||
        v->kind == SZ_VIEW_CHECKBOX_LIST_TILE ||
        v->kind == SZ_VIEW_SWITCH_LIST_TILE) {
      int on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
      snprintf(live, sizeof live, "%s=%d", v->a11y_label ? v->a11y_label : "",
               on ? 1 : 0);
      label = live;
    }
    if (v->kind == SZ_VIEW_RADIO || v->kind == SZ_VIEW_RADIO_LIST_TILE ||
        v->kind == SZ_VIEW_CHOICE_CHIP) {
      int on = v->sig_int && sz_signal_int_get(v->sig_int) == v->radio_value;
      snprintf(live, sizeof live, "%s=%d", v->a11y_label ? v->a11y_label : "",
               on ? 1 : 0);
      label = live;
    }
    if (v->kind == SZ_VIEW_SLIDER || v->kind == SZ_VIEW_PROGRESS ||
        v->kind == SZ_VIEW_CIRCULAR_PROGRESS) {
      int64_t n = v->sig_int ? slider_clamp(sz_signal_int_get(v->sig_int)) : 0;
      snprintf(live, sizeof live, "%lld", (long long)n);
      label = live;
    }
    if (v->kind == SZ_VIEW_BADGE) {
      int64_t n = v->sig_int ? sz_signal_int_get(v->sig_int) : 0;
      snprintf(live, sizeof live, "%lld", (long long)n);
      label = live;
    }
    if (v->kind == SZ_VIEW_SEGMENTED) {
      int64_t n = v->sig_int && sz_signal_int_get(v->sig_int) != 0 ? 1 : 0;
      snprintf(live, sizeof live, "%lld", (long long)n);
      label = live;
    }
    if (v->kind == SZ_VIEW_VISIBILITY) {
      snprintf(live, sizeof live, "%d", view_visibility_on(v) ? 1 : 0);
      label = live;
    }
    if (v->kind == SZ_VIEW_OFFSTAGE) {
      snprintf(live, sizeof live, "%d", view_offstage_shown(v) ? 1 : 0);
      label = live;
    }
    if (v->kind == SZ_VIEW_SPLIT) {
      int64_t n = v->sig_int ? slider_clamp(sz_signal_int_get(v->sig_int)) : 50;
      snprintf(live, sizeof live, "%lld", (long long)n);
      label = live;
    }
    if (v->kind == SZ_VIEW_OVERLAY) {
      snprintf(live, sizeof live, "%d", view_overlay_open(v) ? 1 : 0);
      label = live;
    }
    n = snprintf(line, sizeof line, "%s:%s\n", a11y_role_name(v->a11y_role),
                 label);
    if (n > 0 && *len + (size_t)n < cap) {
      memcpy(buf + *len, line, (size_t)n);
      *len += (size_t)n;
      buf[*len] = '\0';
    }
  }
  if (v->kind == SZ_VIEW_MERGE_SEMANTICS)
    return;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return;
  for (i = 0; i < v->child_count; i++)
    a11y_dump_node(v->children[i], buf, cap, len);
}

SzString *sz_view_a11y_dump(SzView *root) {
  char buf[4096];
  size_t len = 0;
  buf[0] = '\0';
  a11y_dump_node(root, buf, sizeof buf, &len);
  return sz_string_from_cstr(buf);
}

SzView *sz_view_column(void) { return view_new(SZ_VIEW_COLUMN); }
SzView *sz_view_row(void) { return view_new(SZ_VIEW_ROW); }
SzView *sz_view_wrap(void) { return view_new(SZ_VIEW_WRAP); }

SzView *sz_view_grid(int cols) {
  SzView *v = view_new(SZ_VIEW_GRID);
  v->img_w = cols < 1 ? 1 : cols;
  return v;
}
SzView *sz_view_stack(void) { return view_new(SZ_VIEW_STACK); }
SzView *sz_view_list(void) {
  SzView *v = view_new(SZ_VIEW_LIST);
  v->a11y_role = SZ_A11Y_LIST;
  v->a11y_label = sz_strdup("list");
  return v;
}

SzView *sz_view_each(SzSignalList *sig) {
  SzView *v = sz_view_list();
  v->each_sig = sig;
  /* Sentinel: force first sync even when the list is empty (NULL). */
  v->each_seen = (SzList *)(uintptr_t)1;
  return v;
}

SzView *sz_view_each_map(SzSignalList *sig, SzViewEachFn fn, void *env) {
  SzView *v = sz_view_each(sig);
  v->each_fn = fn;
  sz_retain(env);
  v->each_env = env;
  return v;
}

static void sync_each(SzView *v) {
  SzList *xs;
  SzList *p;
  if (!v || !v->each_sig)
    return;
  xs = sz_signal_list_get(v->each_sig);
  if (xs == v->each_seen)
    return;
  sz_view_clear_children(v);
  for (p = xs; p; p = p->tail) {
    SzString *s = (SzString *)p->head;
    if (v->each_fn) {
      SzView *row = v->each_fn(s, v->each_env);
      if (row)
        sz_view_add_child(v, row);
    } else {
      char line[256];
      snprintf(line, sizeof line, "- %s", s ? sz_string_cstr(s) : "");
      sz_view_add_child(v, sz_view_text(line));
    }
  }
  v->each_seen = xs;
}

SzView *sz_view_scroll(SzView *child) {
  SzView *v = view_new(SZ_VIEW_SCROLL);
  v->pref_h = 64.f;
  v->a11y_role = SZ_A11Y_SCROLL;
  v->a11y_label = sz_strdup("scroll");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_scroll_h(SzView *child) {
  SzView *v = view_new(SZ_VIEW_SCROLL);
  v->scroll_h = 1;
  v->a11y_role = SZ_A11Y_SCROLL;
  v->a11y_label = sz_strdup("scrollh");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_expanded(SzView *child) {
  SzView *v = view_new(SZ_VIEW_EXPANDED);
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_stretch(SzView *child) {
  SzView *v = view_new(SZ_VIEW_STRETCH);
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_center(SzView *child) {
  SzView *v = view_new(SZ_VIEW_CENTER);
  if (child)
    sz_view_add_child(v, child);
  return v;
}

static int clamp_align(int a) {
  if (a < 0)
    return 0;
  if (a > 2)
    return 2;
  return a;
}

SzView *sz_view_align(int ax, int ay, SzView *child) {
  SzView *v = view_new(SZ_VIEW_ALIGN);
  v->align_x = clamp_align(ax);
  v->align_y = clamp_align(ay);
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_positioned(int x, int y, SzView *child) {
  SzView *v = view_new(SZ_VIEW_POSITIONED);
  v->pos_x = x > 0 ? x : 0;
  v->pos_y = y > 0 ? y : 0;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_padding(int pad, SzView *child) {
  SzView *v = view_new(SZ_VIEW_PADDING);
  v->pad = pad > 0 ? pad : 0;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_sized(int w, int h, SzView *child) {
  SzView *v = view_new(SZ_VIEW_SIZED);
  v->img_w = w > 0 ? w : 0;
  v->img_h = h > 0 ? h : 0;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_min_size(int w, int h, SzView *child) {
  SzView *v = view_new(SZ_VIEW_MIN_SIZE);
  v->img_w = w > 0 ? w : 0;
  v->img_h = h > 0 ? h : 0;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_max_size(int w, int h, SzView *child) {
  SzView *v = view_new(SZ_VIEW_MAX_SIZE);
  v->img_w = w > 0 ? w : 0;
  v->img_h = h > 0 ? h : 0;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_clip(SzView *child) {
  SzView *v = view_new(SZ_VIEW_CLIP);
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_opacity(int pct, SzView *child) {
  SzView *v = view_new(SZ_VIEW_OPACITY);
  if (pct < 0)
    pct = 0;
  if (pct > 100)
    pct = 100;
  v->img_w = pct;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_max_lines(int n, SzView *child) {
  SzView *v = view_new(SZ_VIEW_MAX_LINES);
  v->img_w = n > 0 ? n : 0;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_ignore_pointer(SzView *child) {
  SzView *v = view_new(SZ_VIEW_IGNORE_POINTER);
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_absorb_pointer(SzView *child) {
  SzView *v = view_new(SZ_VIEW_ABSORB_POINTER);
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_exclude_semantics(SzView *child) {
  SzView *v = view_new(SZ_VIEW_EXCLUDE_SEMANTICS);
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_ellipsis(SzView *child) {
  SzView *v = view_new(SZ_VIEW_ELLIPSIS);
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_text_color(uint32_t argb, SzView *child) {
  SzView *v = view_new(SZ_VIEW_TEXT_COLOR);
  v->bg_argb = argb;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_gap(int n, SzView *child) {
  SzView *v = view_new(SZ_VIEW_GAP);
  v->img_w = n > 0 ? n : 0;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_font_size(int n, SzView *child) {
  SzView *v = view_new(SZ_VIEW_FONT_SIZE);
  v->img_w = n > 0 ? n : 1;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_border(int n, uint32_t argb, SzView *child) {
  SzView *v = view_new(SZ_VIEW_BORDER);
  v->img_w = n > 0 ? n : 0;
  v->bg_argb = argb;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_radius(int n, SzView *child) {
  SzView *v = view_new(SZ_VIEW_RADIUS);
  v->img_w = n > 0 ? n : 0;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_background(uint32_t argb, SzView *child) {
  SzView *v = view_new(SZ_VIEW_BACKGROUND);
  v->bg_argb = argb;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_aspect_ratio(int rw, int rh, SzView *child) {
  SzView *v = view_new(SZ_VIEW_ASPECT_RATIO);
  v->img_w = rw > 0 ? rw : 1;
  v->img_h = rh > 0 ? rh : 1;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_fraction(int wpct, int hpct, SzView *child) {
  SzView *v = view_new(SZ_VIEW_FRACTION);
  if (wpct < 0)
    wpct = 0;
  if (wpct > 100)
    wpct = 100;
  if (hpct < 0)
    hpct = 0;
  if (hpct > 100)
    hpct = 100;
  v->img_w = wpct;
  v->img_h = hpct;
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_image(int w, int h, uint32_t argb, const char *caption) {
  SzView *v = view_new(SZ_VIEW_IMAGE);
  v->img_w = w > 0 ? w : 32;
  v->img_h = h > 0 ? h : 32;
  v->bg_argb = argb ? argb : 0xFF888888u;
  v->text = sz_strdup(caption ? caption : "");
  v->a11y_role = SZ_A11Y_IMAGE;
  v->a11y_label = sz_strdup(caption && caption[0] ? caption : "image");
  return v;
}

SzView *sz_view_icon(char glyph, uint32_t argb) {
  SzView *v = view_new(SZ_VIEW_ICON);
  v->glyph = glyph ? glyph : '*';
  v->fg_argb = argb ? argb : 0xFF1A1A1Au;
  return v;
}

static void sz_view_set_show_when(SzView *view, SzSignalInt *sig, int64_t value) {
  if (!view)
    return;
  view->show_when_sig = sig;
  view->show_when_value = value;
}

SzView *sz_view_show_when(SzSignalInt *sig, int64_t value, SzView *child) {
  if (child)
    sz_view_set_show_when(child, sig, value);
  return child;
}

void sz_view_free(SzView *view) {
  int i;
  if (!view)
    return;
  for (i = 0; i < view->child_count; i++)
    sz_view_free(view->children[i]);
  sz_free(view->children);
  sz_free(view->text);
  sz_free(view->prefix);
  sz_free(view->placeholder);
  sz_free(view->a11y_label);
  /* Signals are owned by the demo/session, not the view. */
  sz_release(view->tap_env);
  view->tap_env = NULL;
  sz_release(view->each_env);
  view->each_env = NULL;
  {
    int u;
    for (u = 0; u < view->undo_n; u++)
      sz_free(view->undo[u].text);
    sz_free(view->undo);
    for (u = 0; u < view->redo_n; u++)
      sz_free(view->redo[u].text);
    sz_free(view->redo);
    sz_free(view->diag_line);
    sz_free(view->diag_sev);
  }
  sz_free(view);
}

void sz_view_clear_children(SzView *parent) {
  int i;
  if (!parent)
    return;
  if (!view_accepts_children(parent->kind))
    sz_panic("sz_view_clear_children: parent cannot have children");
  for (i = 0; i < parent->child_count; i++) {
    parent->children[i]->parent = NULL;
    sz_view_free(parent->children[i]);
  }
  parent->child_count = 0;
  parent->scroll_child = NULL;
}

static float text_width(const char *s, float font_px) {
  return sk_font_measure_string(s ? s : "", font_px);
}

static int utf8_clen(const char *s, int i) {
  unsigned char c;
  if (!s || s[i] == '\0')
    return 0;
  c = (unsigned char)s[i];
  if (c < 0x80)
    return 1;
  if ((c & 0xe0) == 0xc0)
    return 2;
  if ((c & 0xf0) == 0xe0)
    return 3;
  if ((c & 0xf8) == 0xf0)
    return 4;
  return 1;
}

static int utf8_prev(const char *s, int n) {
  if (!s || n <= 0)
    return 0;
  n--;
  while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80)
    n--;
  return n;
}

static void ellipsize_to_width(char *line, size_t cap, float max_w, float font_px) {
  char out[256];
  int n;
  if (!line || cap < 4) {
    if (line)
      line[0] = '\0';
    return;
  }
  n = (int)strlen(line);
  while (n >= 0) {
    int keep = n;
    if (keep > (int)cap - 4)
      keep = (int)cap - 4;
    if (keep < 0)
      keep = 0;
    memcpy(out, line, (size_t)keep);
    memcpy(out + keep, "...", 4);
    if (text_width(out, font_px) <= max_w) {
      memcpy(line, out, (size_t)keep + 4);
      return;
    }
    if (keep == 0)
      break;
    n = utf8_prev(line, keep);
  }
  if (text_width("...", font_px) <= max_w)
    memcpy(line, "...", 4);
  else
    line[0] = '\0';
}

static float span_width(const char *s, int start, int end, float font_px) {
  char tmp[256];
  int n;
  if (!s || end <= start)
    return 0.f;
  n = end - start;
  if (n >= (int)sizeof tmp)
    n = (int)sizeof tmp - 1;
  memcpy(tmp, s + start, (size_t)n);
  tmp[n] = '\0';
  return text_width(tmp, font_px);
}

/* Unbounded span measure. Editor paint/caret must not use the 256-byte cap. */
static float editor_span_width(const char *s, int start, int end, float font_px) {
  float cell;
  int n = 0;
  int i;
  if (!s || end <= start)
    return 0.f;
  cell = sk_font_mono_cell(font_px);
  i = start;
  while (i < end) {
    int clen = utf8_clen(s, i);
    if (clen < 1)
      clen = 1;
    if (i + clen > end)
      clen = end - i;
    i += clen;
    n++;
  }
  return (float)n * cell;
}

static int editor_line_count(const char *s) {
  int n = 1;
  int i;
  if (!s || !s[0])
    return 1;
  for (i = 0; s[i]; i++) {
    if (s[i] == '\n')
      n++;
  }
  return n;
}

static float editor_gutter_w(const SzView *v, float font_px) {
  int lines;
  int digits = 1;
  int n;
  float cell;
  if (!v || v->kind != SZ_VIEW_EDITOR)
    return 0.f;
  lines = editor_line_count(field_cstr(v));
  n = lines;
  while (n >= 10) {
    n /= 10;
    digits++;
  }
  cell = sk_font_mono_cell(font_px);
  return (float)digits * cell + 4.f;
}

static void hist_clear(SzEditHist **hist, int *n, int *cap) {
  int i;
  if (!hist || !*hist)
    return;
  for (i = 0; i < *n; i++)
    sz_free((*hist)[i].text);
  sz_free(*hist);
  *hist = NULL;
  *n = 0;
  *cap = 0;
}

static void hist_push(SzEditHist **hist, int *n, int *cap, const char *text,
                      int caret, int sel_anchor) {
  SzEditHist *next;
  if (*n >= EDITOR_UNDO_MAX) {
    sz_free((*hist)[0].text);
    memmove(*hist, *hist + 1, sizeof(SzEditHist) * (size_t)(*n - 1));
    (*n)--;
  }
  if (*n + 1 > *cap) {
    int cap2 = *cap < 8 ? 8 : *cap * 2;
    if (cap2 > EDITOR_UNDO_MAX)
      cap2 = EDITOR_UNDO_MAX;
    next = (SzEditHist *)sz_alloc(sizeof(SzEditHist) * (size_t)cap2);
    if (*hist && *n > 0)
      memcpy(next, *hist, sizeof(SzEditHist) * (size_t)*n);
    sz_free(*hist);
    *hist = next;
    *cap = cap2;
  }
  (*hist)[*n].text = sz_strdup(text ? text : "");
  (*hist)[*n].caret = caret;
  (*hist)[*n].sel_anchor = sel_anchor;
  (*n)++;
}

static void editor_push_undo(SzView *t) {
  if (!t || t->kind != SZ_VIEW_EDITOR)
    return;
  hist_push(&t->undo, &t->undo_n, &t->undo_cap, field_cstr(t),
            field_caret_clamped(t), field_anchor_clamped(t));
  hist_clear(&t->redo, &t->redo_n, &t->redo_cap);
}

static int editor_restore(SzView *t, SzEditHist snap) {
  if (!t || t->kind != SZ_VIEW_EDITOR || !t->sig_str)
    return 0;
  sz_signal_str_set(t->sig_str, snap.text ? snap.text : "");
  t->caret = snap.caret;
  t->sel_anchor = snap.sel_anchor;
  t->caret = field_caret_clamped(t);
  t->sel_anchor = field_anchor_clamped(t);
  return 1;
}

int sz_view_editor_undo(SzView *view) {
  SzEditHist snap;
  if (!view || view->kind != SZ_VIEW_EDITOR || view->undo_n <= 0)
    return 0;
  hist_push(&view->redo, &view->redo_n, &view->redo_cap, field_cstr(view),
            field_caret_clamped(view), field_anchor_clamped(view));
  snap = view->undo[view->undo_n - 1];
  view->undo_n--;
  editor_restore(view, snap);
  sz_free(snap.text);
  return 1;
}

int sz_view_editor_redo(SzView *view) {
  SzEditHist snap;
  if (!view || view->kind != SZ_VIEW_EDITOR || view->redo_n <= 0)
    return 0;
  hist_push(&view->undo, &view->undo_n, &view->undo_cap, field_cstr(view),
            field_caret_clamped(view), field_anchor_clamped(view));
  snap = view->redo[view->redo_n - 1];
  view->redo_n--;
  editor_restore(view, snap);
  sz_free(snap.text);
  return 1;
}

int sz_view_editor_line_count(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  return editor_line_count(field_cstr(view));
}

float sz_view_editor_gutter_w(const SzView *view) {
  const SzTheme *theme = sz_theme_default();
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0.f;
  return editor_gutter_w(view, theme->font_px);
}

int sz_view_editor_set_diagnostics(SzView *view, const int *lines,
                                  const int *severities, int n) {
  int i;
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  sz_free(view->diag_line);
  sz_free(view->diag_sev);
  view->diag_line = NULL;
  view->diag_sev = NULL;
  view->diag_n = 0;
  if (n <= 0 || !lines || !severities)
    return 1;
  view->diag_line = (int *)sz_alloc(sizeof(int) * (size_t)n);
  view->diag_sev = (int *)sz_alloc(sizeof(int) * (size_t)n);
  for (i = 0; i < n; i++) {
    view->diag_line[i] = lines[i];
    view->diag_sev[i] = severities[i];
  }
  view->diag_n = n;
  return 1;
}

int sz_view_editor_diag_count(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  return view->diag_n;
}

int sz_view_editor_diag_line(const SzView *view, int i) {
  if (!view || view->kind != SZ_VIEW_EDITOR || i < 0 || i >= view->diag_n)
    return 0;
  return view->diag_line[i];
}

int sz_view_editor_diag_severity(const SzView *view, int i) {
  if (!view || view->kind != SZ_VIEW_EDITOR || i < 0 || i >= view->diag_n)
    return 0;
  return view->diag_sev[i];
}

static int editor_diag_at(const SzView *v, int line) {
  int i;
  if (!v)
    return 0;
  for (i = 0; i < v->diag_n; i++) {
    if (v->diag_line[i] == line)
      return v->diag_sev[i] != 0 ? v->diag_sev[i] : 1;
  }
  return 0;
}

static void line_bounds_at_off(const char *s, int off, int *start, int *end) {
  int n;
  int i;
  if (!s)
    s = "";
  n = (int)strlen(s);
  if (off < 0)
    off = 0;
  if (off > n)
    off = n;
  i = off;
  while (i > 0 && s[i - 1] != '\n')
    i--;
  *start = i;
  i = off;
  while (i < n && s[i] != '\n')
    i++;
  *end = i;
}

static int caret_on_line_at_width(const char *s, int start, int end, float want,
                                  float font_px) {
  float cell = sk_font_mono_cell(font_px);
  int col, i, n;
  if (want <= 0.f || start >= end)
    return start;
  if (cell < 1.f)
    cell = 1.f;
  col = (int)((want / cell) + 0.5f);
  i = start;
  n = 0;
  while (i < end && n < col) {
    int clen = utf8_clen(s, i);
    if (clen < 1)
      clen = 1;
    if (i + clen > end)
      break;
    i += clen;
    n++;
  }
  return i;
}

typedef void (*SzTextLineFn)(const char *s, int start, int end, float width,
                             void *env);

static void emit_text_line(const char *s, int start, int end, float font_px,
                           SzTextLineFn fn, void *env) {
  while (end > start && s[end - 1] == ' ')
    end--;
  if (fn)
    fn(s, start, end, span_width(s, start, end, font_px), env);
}

/* Wrap one paragraph. max_inner <= 0 means no width wrap. */
static void wrap_paragraph(const char *s, int start, int end, float font_px,
                           float max_inner, SzTextLineFn fn, void *env) {
  int i = start;
  int line = start;
  int last_space = -1;
  if (start >= end) {
    emit_text_line(s, start, end, font_px, fn, env);
    return;
  }
  while (i < end) {
    int clen = utf8_clen(s, i);
    int next;
    float w;
    if (clen < 1)
      clen = 1;
    if (i + clen > end)
      clen = end - i;
    next = i + clen;
    if (s[i] == ' ')
      last_space = i;
    w = span_width(s, line, next, font_px);
    if (max_inner > 0.f && w > max_inner && next > line) {
      if (last_space >= line) {
        emit_text_line(s, line, last_space, font_px, fn, env);
        line = last_space + 1;
        while (line < end && s[line] == ' ')
          line++;
        last_space = -1;
        i = line;
        continue;
      }
      if (i == line) {
        emit_text_line(s, line, next, font_px, fn, env);
        line = next;
        last_space = -1;
        i = next;
        continue;
      }
      emit_text_line(s, line, i, font_px, fn, env);
      line = i;
      last_space = -1;
      continue;
    }
    i = next;
  }
  if (line < end || line == start)
    emit_text_line(s, line, end, font_px, fn, env);
}

static void each_text_line(const char *s, float font_px, float max_inner,
                           SzTextLineFn fn, void *env) {
  int i = 0;
  int para = 0;
  if (!s)
    s = "";
  for (;;) {
    if (s[i] == '\0' || s[i] == '\n') {
      wrap_paragraph(s, para, i, font_px, max_inner, fn, env);
      if (s[i] == '\0')
        break;
      i++;
      para = i;
      continue;
    }
    i++;
  }
}

typedef struct SzWrapMetrics {
  float max_line_w;
  int n;
  int cap; /* 0 = unlimited */
  int truncated;
} SzWrapMetrics;

static void accum_wrap_line(const char *s, int start, int end, float width,
                            void *env) {
  SzWrapMetrics *m = (SzWrapMetrics *)env;
  (void)s;
  (void)start;
  (void)end;
  if (m->cap > 0 && m->n >= m->cap) {
    m->truncated = 1;
    return;
  }
  if (width > m->max_line_w)
    m->max_line_w = width;
  m->n++;
}

static void resolve_text(const SzView *v, char *buf, size_t buflen) {
  if (!buf || buflen == 0)
    return;
  buf[0] = '\0';
  if (v->sig_int) {
    snprintf(buf, buflen, "%s%lld", v->prefix ? v->prefix : "",
             (long long)sz_signal_int_get(v->sig_int));
  } else if (v->sig_str) {
    snprintf(buf, buflen, "%s", sz_signal_str_get(v->sig_str));
  } else if (v->text) {
    snprintf(buf, buflen, "%s", v->text);
  }
}

/* Constraints down, sizes up. min==max is a tight slot (Sized, Expanded flex). */
typedef struct SzBoxConstraints {
  float min_w;
  float min_h;
  float max_w;
  float max_h;
} SzBoxConstraints;

static SzBoxConstraints box_loose(float max_w, float max_h) {
  SzBoxConstraints c;
  c.min_w = 0.f;
  c.min_h = 0.f;
  c.max_w = max_w;
  c.max_h = max_h;
  return c;
}

static SzBoxConstraints box_tight(float w, float h) {
  SzBoxConstraints c;
  c.min_w = w;
  c.min_h = h;
  c.max_w = w;
  c.max_h = h;
  return c;
}

static SzBoxConstraints box_tight_width(float w, float max_h) {
  SzBoxConstraints c;
  c.min_w = w;
  c.min_h = 0.f;
  c.max_w = w;
  c.max_h = max_h;
  return c;
}

static SzBoxConstraints box_tight_height(float max_w, float h) {
  SzBoxConstraints c;
  c.min_w = 0.f;
  c.min_h = h;
  c.max_w = max_w;
  c.max_h = h;
  return c;
}

/* Row cross size. max_h 0 is unbounded (Scroll content) and stays intrinsic.
 * A positive max, even loose, is the cross slot for stretch / expanded. */
static float row_cross_inner(float min_h, float max_h, const SzTheme *theme) {
  float cap;
  (void)min_h;
  if (max_h > 0.f) {
    cap = max_h - theme->pad * 2.f;
    return cap > 0.f ? cap : 0.f;
  }
  return theme->control_h;
}

static SzBoxConstraints column_child_box(SzView *ch, float inner_w, float max_h,
                                         float flex_h, int flexing) {
  if (flexing && view_is_flex(ch))
    return box_tight(inner_w, flex_h);
  if (view_is_cross_stretch(ch))
    return box_tight_width(inner_w, max_h);
  return box_loose(inner_w, max_h);
}

static SzBoxConstraints row_child_box(SzView *ch, float max_w, float inner_h,
                                      float flex_w, int flexing) {
  if (flexing && view_is_flex(ch))
    return box_tight(flex_w, inner_h);
  if (view_is_cross_stretch(ch))
    return box_tight_height(max_w, inner_h);
  return box_loose(max_w, inner_h);
}

/* 0 cap means no extra ceiling. Incoming max wins when it is tighter. */
static float cap_max_axis(float incoming, float cap) {
  if (cap <= 0.f)
    return incoming;
  if (incoming <= 0.f || cap < incoming)
    return cap;
  return incoming;
}

static void layout_node_ex(SzView *v, float x, float y, float min_w, float min_h,
                           float max_w, float max_h, const SzTheme *theme);

static int g_max_lines;
static int g_ellipsis;
static int g_gap_on;
static float g_gap;
static float g_font_px; /* 0 = theme font */

static float layout_gap(const SzTheme *theme) {
  if (g_gap_on)
    return g_gap;
  return theme->gap;
}

static float layout_font_px(const SzTheme *theme) {
  return g_font_px > 0.f ? g_font_px : theme->font_px;
}

static float theme_px_scale(const SzTheme *theme) {
  float s = theme ? theme->px_scale : 1.f;
  return s > 0.01f ? s : 1.f;
}

static float scale_px(const SzTheme *theme, float n) {
  return n * theme_px_scale(theme);
}

static float text_line_h(const SzTheme *theme, float font_px) {
  return font_px + scale_px(theme, 6.f);
}

static int tighten_max_lines(int n) {
  if (n <= 0)
    return g_max_lines;
  if (g_max_lines <= 0 || n < g_max_lines)
    return n;
  return g_max_lines;
}

/* Positive maxLines wins. Ellipsis without a cap keeps one line. */
static int text_line_cap(void) {
  if (g_max_lines > 0)
    return g_max_lines;
  if (g_ellipsis)
    return 1;
  return 0;
}

static void layout_constrained(SzView *v, float x, float y, SzBoxConstraints c,
                               const SzTheme *theme) {
  layout_node_ex(v, x, y, c.min_w, c.min_h, c.max_w, c.max_h, theme);
}

static void layout_pass_child(SzView *v, float x, float y, float min_w, float min_h,
                              float max_w, float max_h, const SzTheme *theme) {
  SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
  if (ch) {
    SzBoxConstraints cc;
    cc.min_w = min_w;
    cc.min_h = min_h;
    cc.max_w = max_w;
    cc.max_h = max_h;
    layout_constrained(ch, x, y, cc, theme);
  }
  v->frame.w = ch ? ch->frame.w : 0.f;
  v->frame.h = ch ? ch->frame.h : 0.f;
}

static void layout_node(SzView *v, float x, float y, float max_w, float max_h,
                        const SzTheme *theme) {
  layout_constrained(v, x, y, box_loose(max_w, max_h), theme);
}

static void layout_node_ex(SzView *v, float x, float y, float min_w, float min_h,
                           float max_w, float max_h, const SzTheme *theme) {
  float font = theme->font_px;
  char buf[256];
  int i;

  v->frame.x = x;
  v->frame.y = y;

  if (!view_is_shown(v)) {
    v->frame.w = 0.f;
    v->frame.h = 0.f;
    return;
  }

  switch (v->kind) {
  case SZ_VIEW_TEXT: {
    SzWrapMetrics m;
    float inner;
    float font_px = layout_font_px(theme);
    float line_h = text_line_h(theme, font_px);
    float inset = scale_px(theme, 4.f);
    resolve_text(v, buf, sizeof buf);
    inner = 0.f;
    if (max_w > inset)
      inner = max_w - inset;
    m.max_line_w = 0.f;
    m.n = 0;
    m.cap = text_line_cap();
    m.truncated = 0;
    each_text_line(buf, font_px, inner, accum_wrap_line, &m);
    if (m.n < 1)
      m.n = 1;
    v->frame.w = m.max_line_w + inset;
    if (g_ellipsis && m.truncated) {
      float need = m.max_line_w + text_width("...", font_px) + inset;
      if (v->frame.w < need)
        v->frame.w = need;
    }
    v->frame.h = (float)m.n * line_h;
    if (max_w > 0.f && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  }
  case SZ_VIEW_BUTTON:
  case SZ_VIEW_OUTLINED_BUTTON:
  case SZ_VIEW_TEXT_BUTTON:
    resolve_text(v, buf, sizeof buf);
    v->frame.w = text_width(buf, font) + theme->pad * 2.f;
    v->frame.h = theme->control_h;
    if (v->frame.w < 48.f)
      v->frame.w = 48.f;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  case SZ_VIEW_ICON_BUTTON:
  case SZ_VIEW_FAB:
    v->frame.w = theme->control_h;
    v->frame.h = theme->control_h;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  case SZ_VIEW_VERTICAL_DIVIDER:
    v->frame.w = 8.f;
    v->frame.h = theme->control_h;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    if (max_h > 0 && v->frame.h > max_h)
      v->frame.h = max_h;
    break;
  case SZ_VIEW_CHECKBOX:
  case SZ_VIEW_RADIO: {
    float box = font + 4.f;
    float gap = layout_gap(theme);
    resolve_text(v, buf, sizeof buf);
    if (box < 12.f)
      box = 12.f;
    if (box > theme->control_h - 4.f)
      box = theme->control_h - 4.f;
    v->frame.h = theme->control_h;
    v->frame.w = box + gap + text_width(buf, font);
    if (v->frame.w < box)
      v->frame.w = box;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  }
  case SZ_VIEW_SWITCH: {
    float box = font + 4.f;
    float gap = layout_gap(theme);
    float track;
    resolve_text(v, buf, sizeof buf);
    if (box < 12.f)
      box = 12.f;
    if (box > theme->control_h - 4.f)
      box = theme->control_h - 4.f;
    track = box * 2.f;
    v->frame.h = theme->control_h;
    v->frame.w = track + gap + text_width(buf, font);
    if (v->frame.w < track)
      v->frame.w = track;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  }
  case SZ_VIEW_CHIP:
  case SZ_VIEW_CHOICE_CHIP:
  case SZ_VIEW_ACTION_CHIP:
    resolve_text(v, buf, sizeof buf);
    v->frame.w = text_width(buf, font) + theme->pad * 2.f;
    v->frame.h = theme->control_h;
    if (v->frame.w < 32.f)
      v->frame.w = 32.f;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  case SZ_VIEW_FILTER_CHIP:
  case SZ_VIEW_INPUT_CHIP: {
    float box = font + 4.f;
    float gap = layout_gap(theme);
    resolve_text(v, buf, sizeof buf);
    if (box < 12.f)
      box = 12.f;
    if (box > theme->control_h - 4.f)
      box = theme->control_h - 4.f;
    v->frame.w = theme->pad * 2.f + box + gap + text_width(buf, font);
    v->frame.h = theme->control_h;
    if (v->frame.w < 32.f)
      v->frame.w = 32.f;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  }
  case SZ_VIEW_LIST_TILE: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float pad = theme->pad;
    float cw = 0.f;
    float chh = 0.f;
    float tx;
    float ty;
    v->frame.w = max_w > 0 ? max_w : 120.f;
    v->frame.h = theme->control_h;
    if (ch) {
      layout_constrained(ch, x, y, box_loose(0.f, 0.f), theme);
      cw = ch->frame.w;
      chh = ch->frame.h;
      if (cw > v->frame.w - pad * 2.f)
        cw = v->frame.w - pad * 2.f;
      if (cw < 0.f)
        cw = 0.f;
      if (chh > v->frame.h)
        v->frame.h = chh;
      tx = x + v->frame.w - pad - cw;
      ty = y + (v->frame.h - chh) * 0.5f;
      layout_constrained(ch, tx, ty, box_tight(cw, chh), theme);
    }
    break;
  }
  case SZ_VIEW_CHECKBOX_LIST_TILE:
  case SZ_VIEW_SWITCH_LIST_TILE:
  case SZ_VIEW_RADIO_LIST_TILE:
  case SZ_VIEW_SEGMENTED:
    v->frame.w = max_w > 0 ? max_w : 120.f;
    v->frame.h = theme->control_h;
    break;
  case SZ_VIEW_SLIDER:
    v->frame.w = max_w > 0 ? max_w : 120.f;
    if (v->frame.w < 48.f)
      v->frame.w = 48.f;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    v->frame.h = theme->control_h;
    break;
  case SZ_VIEW_PROGRESS:
    v->frame.w = max_w > 0 ? max_w : 120.f;
    if (v->frame.w < 48.f)
      v->frame.w = 48.f;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    v->frame.h = 8.f;
    break;
  case SZ_VIEW_CIRCULAR_PROGRESS:
    v->frame.w = theme->control_h;
    v->frame.h = theme->control_h;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    if (max_h > 0 && v->frame.h > max_h)
      v->frame.h = max_h;
    break;
  case SZ_VIEW_AVATAR:
    v->frame.w = theme->control_h;
    v->frame.h = theme->control_h;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    if (max_h > 0 && v->frame.h > max_h)
      v->frame.h = max_h;
    break;
  case SZ_VIEW_DIVIDER:
    v->frame.w = max_w > 0 ? max_w : 120.f;
    v->frame.h = 8.f;
    break;
  case SZ_VIEW_EXPANSION_TILE: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float hh = theme->control_h;
    float child_max_h;
    v->frame.w = max_w > 0 ? max_w : 120.f;
    v->frame.h = hh;
    if (ch && view_is_shown(ch)) {
      child_max_h = max_h > hh ? max_h - hh : 0.f;
      layout_constrained(ch, x, y + hh, box_loose(v->frame.w, child_max_h),
                         theme);
      v->frame.h = hh + ch->frame.h;
    }
    break;
  }
  case SZ_VIEW_TEXT_FIELD:
    v->frame.w = max_w > 0 ? max_w : 120.f;
    v->frame.h = theme->control_h;
    break;
  case SZ_VIEW_EDITOR: {
    float font_px = theme->font_px;
    float line_h = text_line_h(theme, font_px);
    float h = 8.f * line_h;
    v->frame.w = max_w > 0 ? max_w : 120.f;
    if (max_h > 0.f && h > max_h)
      h = max_h;
    if (min_h > 0.f && h < min_h)
      h = min_h;
    v->frame.h = h;
    break;
  }
  case SZ_VIEW_ICON:
    v->frame.w = font + scale_px(theme, 4.f);
    v->frame.h = font + scale_px(theme, 4.f);
    break;
  case SZ_VIEW_IMAGE:
    v->frame.w = scale_px(theme, (float)v->img_w);
    v->frame.h = scale_px(theme, (float)v->img_h);
    break;
  case SZ_VIEW_COLUMN:
  case SZ_VIEW_LIST: {
    float cy = y + theme->pad;
    float inner_w = max_w - theme->pad * 2.f;
    float h = theme->pad;
    int shown = 0;
    int n_flex = 0;
    float fixed_h = 0.f;
    float flex_h = 0.f;
    float gaps = 0.f;
    if (v->kind == SZ_VIEW_LIST)
      sync_each(v);
    if (inner_w < 0)
      inner_w = 0;
    if (v->kind == SZ_VIEW_COLUMN) {
      int col_shown = 0;
      float h_budget = max_h > 0.f ? max_h : min_h;
      for (i = 0; i < v->child_count; i++) {
        if (!view_is_shown(v->children[i]))
          continue;
        col_shown++;
        if (view_is_flex(v->children[i]))
          n_flex++;
      }
      if (n_flex > 0 && h_budget > 0.f) {
        /* Measure non-flex children for leftover height. */
        for (i = 0; i < v->child_count; i++) {
          SzView *ch = v->children[i];
          if (!view_is_shown(ch) || view_is_flex(ch))
            continue;
          layout_constrained(ch, x + theme->pad, y + theme->pad,
                             column_child_box(ch, inner_w, max_h, 0.f, 0),
                             theme);
          fixed_h += ch->frame.h;
        }
        if (col_shown > 1)
          gaps = layout_gap(theme) * (float)(col_shown - 1);
        /* Not enough room for flex: fall back to intrinsic column layout. */
        if (fixed_h + gaps + theme->pad * 2.f > h_budget + 0.5f)
          n_flex = 0;
        else {
          flex_h = h_budget - theme->pad * 2.f - fixed_h - gaps;
          if (flex_h < 0.f)
            flex_h = 0.f;
          if (n_flex > 1)
            flex_h = flex_h / (float)n_flex;
          cy = y + theme->pad;
          for (i = 0; i < v->child_count; i++) {
            SzView *ch = v->children[i];
            if (!view_is_shown(ch)) {
              layout_constrained(ch, x + theme->pad, cy, box_loose(inner_w, max_h),
                                 theme);
              continue;
            }
            layout_constrained(ch, x + theme->pad, cy,
                               column_child_box(ch, inner_w, max_h, flex_h, 1),
                               theme);
            cy += ch->frame.h + layout_gap(theme);
          }
          v->frame.w = max_w;
          v->frame.h = h_budget;
          break;
        }
      }
    }
    for (i = 0; i < v->child_count; i++) {
      if (!view_is_shown(v->children[i])) {
        layout_constrained(v->children[i], x + theme->pad, cy,
                           box_loose(inner_w, max_h), theme);
        continue;
      }
      layout_constrained(v->children[i], x + theme->pad, cy,
                         column_child_box(v->children[i], inner_w, max_h, 0.f, 0),
                         theme);
      cy += v->children[i]->frame.h + layout_gap(theme);
      h += v->children[i]->frame.h + layout_gap(theme);
      shown++;
    }
    if (shown > 0)
      h -= layout_gap(theme);
    h += theme->pad;
    v->frame.w = max_w;
    v->frame.h = h;
    if (max_h > 0 && v->frame.h > max_h && v->kind == SZ_VIEW_LIST)
      v->frame.h = max_h;
    break;
  }
  case SZ_VIEW_EXPANDED: {
    float old_pref = 0.f;
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float slot_w = max_w > 0.f ? max_w : min_w;
    float slot_h = max_h > 0.f ? max_h : min_h;
    v->frame.w = slot_w;
    v->frame.h = slot_h;
    if (ch) {
      /* Scroll defaults to pref_h=64; inside Expanded, fill the flex slot. */
      if (ch->kind == SZ_VIEW_SCROLL) {
        old_pref = ch->pref_h;
        ch->pref_h = 0.f;
      }
      /* Tight slot: child sizes up to the flex box. No post-layout overwrite. */
      layout_constrained(ch, x, y, box_tight(slot_w, slot_h), theme);
      if (ch->kind == SZ_VIEW_SCROLL)
        ch->pref_h = old_pref;
    }
    break;
  }
  case SZ_VIEW_STRETCH: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    SzBoxConstraints cc;
    cc.min_w = min_w;
    cc.min_h = min_h;
    cc.max_w = max_w;
    cc.max_h = max_h;
    if (ch)
      layout_constrained(ch, x, y, cc, theme);
    v->frame.w = ch ? ch->frame.w : min_w;
    v->frame.h = ch ? ch->frame.h : min_h;
    break;
  }
  case SZ_VIEW_CENTER:
  case SZ_VIEW_ALIGN: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float cw = 0.f;
    float chh = 0.f;
    int ax = v->kind == SZ_VIEW_ALIGN ? v->align_x : 1;
    int ay = v->kind == SZ_VIEW_ALIGN ? v->align_y : 1;
    float ox;
    float oy;
    if (ch)
      layout_constrained(ch, x, y, box_loose(max_w, max_h), theme);
    if (ch) {
      cw = ch->frame.w;
      chh = ch->frame.h;
    }
    v->frame.w = max_w > 0.f ? max_w : cw;
    v->frame.h = max_h > 0.f ? max_h : chh;
    if (ax <= 0)
      ox = 0.f;
    else if (ax >= 2)
      ox = v->frame.w > cw ? v->frame.w - cw : 0.f;
    else
      ox = (v->frame.w - cw) * 0.5f;
    if (ay <= 0)
      oy = 0.f;
    else if (ay >= 2)
      oy = v->frame.h > chh ? v->frame.h - chh : 0.f;
    else
      oy = (v->frame.h - chh) * 0.5f;
    if (ch) {
      /* Re-layout at the aligned origin with a tight measured slot. */
      layout_constrained(ch, x + ox, y + oy, box_tight(cw, chh), theme);
    }
    break;
  }
  case SZ_VIEW_ROW: {
    float cx = x + theme->pad;
    float inner_h = theme->control_h;
    float w = theme->pad;
    int shown = count_shown_children(v);
    int n_flex = 0;
    float fixed_w = 0.f;
    float flex_w = 0.f;
    float gaps = 0.f;
    float child_max;
    float row_inner_h = row_cross_inner(min_h, max_h, theme);
    float w_budget = max_w > 0.f ? max_w : min_w;
    for (i = 0; i < v->child_count; i++) {
      if (view_is_shown(v->children[i]) && view_is_flex(v->children[i]))
        n_flex++;
    }
    if (n_flex > 0 && w_budget > 0.f) {
      /* Measure non-flex at intrinsic width (large max_w). */
      for (i = 0; i < v->child_count; i++) {
        SzView *ch = v->children[i];
        if (!view_is_shown(ch) || view_is_flex(ch))
          continue;
        layout_constrained(ch, x + theme->pad, y + theme->pad,
                           row_child_box(ch, max_w, row_inner_h, 0.f, 0), theme);
        fixed_w += ch->frame.w;
        if (max_h <= 0.f && ch->frame.h > row_inner_h)
          row_inner_h = ch->frame.h;
      }
      if (shown > 1)
        gaps = layout_gap(theme) * (float)(shown - 1);
      if (fixed_w + gaps + theme->pad * 2.f > w_budget + 0.5f)
        n_flex = 0;
      else {
        flex_w = w_budget - theme->pad * 2.f - fixed_w - gaps;
        if (flex_w < 0.f)
          flex_w = 0.f;
        if (n_flex > 1)
          flex_w = flex_w / (float)n_flex;
        cx = x + theme->pad;
        for (i = 0; i < v->child_count; i++) {
          SzView *ch = v->children[i];
          if (!view_is_shown(ch)) {
            layout_constrained(ch, cx, y + theme->pad,
                               box_loose(flex_w, row_inner_h), theme);
            continue;
          }
          layout_constrained(ch, cx, y + theme->pad,
                             row_child_box(ch, max_w, row_inner_h, flex_w, 1),
                             theme);
          cx += ch->frame.w + layout_gap(theme);
          if (ch->frame.h > inner_h)
            inner_h = ch->frame.h;
        }
        v->frame.w = w_budget;
        v->frame.h = inner_h + theme->pad * 2.f;
        break;
      }
    }
    child_max =
        shown > 0
            ? (max_w - theme->pad * 2.f - layout_gap(theme) * (float)(shown - 1)) /
                  (float)shown
            : max_w;
    if (child_max < 0)
      child_max = 0;
    for (i = 0; i < v->child_count; i++) {
      if (!view_is_shown(v->children[i])) {
        layout_constrained(v->children[i], cx, y + theme->pad,
                           box_loose(child_max, max_h), theme);
        continue;
      }
      layout_constrained(v->children[i], cx, y + theme->pad,
                         row_child_box(v->children[i], child_max, row_inner_h, 0.f,
                                       0),
                         theme);
      cx += v->children[i]->frame.w + layout_gap(theme);
      w += v->children[i]->frame.w + layout_gap(theme);
      if (v->children[i]->frame.h > inner_h)
        inner_h = v->children[i]->frame.h;
    }
    if (shown > 0)
      w -= layout_gap(theme);
    w += theme->pad;
    v->frame.w = max_w > 0 ? max_w : w;
    v->frame.h = inner_h + theme->pad * 2.f;
    break;
  }
  case SZ_VIEW_WRAP: {
    float pad = theme->pad;
    float gap = layout_gap(theme);
    float inner_w = max_w > pad * 2.f ? max_w - pad * 2.f : 0.f;
    int unbounded = !(max_w > pad * 2.f);
    float cx = x + pad;
    float cy = y + pad;
    float run_h = 0.f;
    float content_w = 0.f;
    int n_in_run = 0;
    for (i = 0; i < v->child_count; i++) {
      SzView *ch = v->children[i];
      float child_max = unbounded ? 0.f : inner_w;
      if (!view_is_shown(ch)) {
        layout_constrained(ch, cx, cy, box_loose(child_max, 0.f), theme);
        continue;
      }
      layout_constrained(ch, cx, cy, box_loose(child_max, 0.f), theme);
      if (n_in_run > 0 && !unbounded &&
          cx + ch->frame.w > x + pad + inner_w + 0.5f) {
        float run_w = cx - (x + pad) - gap;
        if (run_w > content_w)
          content_w = run_w;
        cx = x + pad;
        cy += run_h + gap;
        run_h = 0.f;
        n_in_run = 0;
        layout_constrained(ch, cx, cy, box_loose(child_max, 0.f), theme);
      }
      cx += ch->frame.w + gap;
      if (ch->frame.h > run_h)
        run_h = ch->frame.h;
      n_in_run++;
    }
    if (n_in_run > 0) {
      float run_w = cx - (x + pad) - gap;
      if (run_w > content_w)
        content_w = run_w;
      v->frame.h = (cy - y) + run_h + pad;
    } else {
      v->frame.h = pad * 2.f;
    }
    v->frame.w = content_w + pad * 2.f;
    break;
  }
  case SZ_VIEW_GRID: {
    float pad = theme->pad;
    float gap = layout_gap(theme);
    int cols = v->img_w < 1 ? 1 : v->img_w;
    int unbounded = !(max_w > pad * 2.f);
    float inner_w = unbounded ? 0.f : max_w - pad * 2.f;
    float cell_w = 0.f;
    float cx = x + pad;
    float cy = y + pad;
    float row_h = 0.f;
    float content_w = 0.f;
    int n_in_row = 0;
    if (!unbounded) {
      cell_w = (inner_w - gap * (float)(cols - 1)) / (float)cols;
      if (cell_w < 0.f)
        cell_w = 0.f;
    }
    for (i = 0; i < v->child_count; i++) {
      SzView *ch = v->children[i];
      float child_max = unbounded ? 0.f : cell_w;
      float used;
      if (!view_is_shown(ch)) {
        layout_constrained(ch, cx, cy, box_loose(child_max, 0.f), theme);
        continue;
      }
      if (n_in_row >= cols) {
        cx = x + pad;
        cy += row_h + gap;
        row_h = 0.f;
        n_in_row = 0;
      }
      layout_constrained(ch, cx, cy, box_loose(child_max, 0.f), theme);
      if (ch->frame.h > row_h)
        row_h = ch->frame.h;
      if (unbounded)
        cx += ch->frame.w + gap;
      else
        cx += cell_w + gap;
      n_in_row++;
      used = cx - (x + pad) - gap;
      if (used > content_w)
        content_w = used;
    }
    if (n_in_row > 0)
      v->frame.h = (cy - y) + row_h + pad;
    else
      v->frame.h = pad * 2.f;
    v->frame.w = unbounded ? content_w + pad * 2.f : max_w;
    break;
  }
  case SZ_VIEW_STACK: {
    float ix = x + theme->pad;
    float iy = y + theme->pad;
    float inner_w = max_w > 0.f ? max_w - theme->pad * 2.f : 0.f;
    float inner_h = max_h > 0.f ? max_h - theme->pad * 2.f : 0.f;
    float max_cw = 0.f;
    float max_ch = 0.f;
    if (inner_w < 0.f)
      inner_w = 0.f;
    if (inner_h < 0.f)
      inner_h = 0.f;
    for (i = 0; i < v->child_count; i++) {
      SzView *ch = v->children[i];
      if (!view_is_shown(ch)) {
        layout_constrained(ch, ix, iy, box_loose(inner_w, inner_h), theme);
        continue;
      }
      layout_constrained(ch, ix, iy,
                         box_loose(inner_w > 0.f ? inner_w : max_w,
                                   inner_h > 0.f ? inner_h : max_h),
                         theme);
      if (ch->frame.w > max_cw)
        max_cw = ch->frame.w;
      if (ch->frame.h > max_ch)
        max_ch = ch->frame.h;
    }
    /* StackFit.loose: size to largest child; clamp to max. */
    v->frame.w = max_cw + theme->pad * 2.f;
    v->frame.h = max_ch + theme->pad * 2.f;
    if (max_w > 0.f && v->frame.w > max_w)
      v->frame.w = max_w;
    if (max_h > 0.f && v->frame.h > max_h)
      v->frame.h = max_h;
    break;
  }
  case SZ_VIEW_POSITIONED: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float px = scale_px(theme, (float)v->pos_x);
    float py = scale_px(theme, (float)v->pos_y);
    float cw = 0.f;
    float chh = 0.f;
    float child_max_w = max_w > px ? max_w - px : 0.f;
    float child_max_h = max_h > py ? max_h - py : 0.f;
    if (ch)
      layout_constrained(ch, x + px, y + py, box_loose(child_max_w, child_max_h),
                         theme);
    if (ch) {
      cw = ch->frame.w;
      chh = ch->frame.h;
    }
    v->frame.w = px + cw;
    v->frame.h = py + chh;
    break;
  }
  case SZ_VIEW_PADDING: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float p = scale_px(theme, (float)v->pad);
    float inner_w = max_w > p * 2.f ? max_w - p * 2.f : 0.f;
    float inner_h = max_h > p * 2.f ? max_h - p * 2.f : 0.f;
    float inner_min_w = min_w > p * 2.f ? min_w - p * 2.f : 0.f;
    float inner_min_h = min_h > p * 2.f ? min_h - p * 2.f : 0.f;
    float cw = 0.f;
    float chh = 0.f;
    if (ch) {
      SzBoxConstraints inner;
      inner.min_w = inner_min_w;
      inner.min_h = inner_min_h;
      inner.max_w = inner_w;
      inner.max_h = inner_h;
      layout_constrained(ch, x + p, y + p, inner, theme);
    }
    if (ch) {
      cw = ch->frame.w;
      chh = ch->frame.h;
    }
    v->frame.w = cw + p * 2.f;
    v->frame.h = chh + p * 2.f;
    if (max_w > 0.f && v->frame.w > max_w)
      v->frame.w = max_w;
    if (max_h > 0.f && v->frame.h > max_h)
      v->frame.h = max_h;
    break;
  }
  case SZ_VIEW_CARD: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float p = theme->pad;
    float inner_w = max_w > p * 2.f ? max_w - p * 2.f : 0.f;
    float inner_h = max_h > p * 2.f ? max_h - p * 2.f : 0.f;
    float inner_min_w = min_w > p * 2.f ? min_w - p * 2.f : 0.f;
    float inner_min_h = min_h > p * 2.f ? min_h - p * 2.f : 0.f;
    float cw = 0.f;
    float chh = 0.f;
    if (ch) {
      SzBoxConstraints inner;
      inner.min_w = inner_min_w;
      inner.min_h = inner_min_h;
      inner.max_w = inner_w;
      inner.max_h = inner_h;
      layout_constrained(ch, x + p, y + p, inner, theme);
    }
    if (ch) {
      cw = ch->frame.w;
      chh = ch->frame.h;
    }
    v->frame.w = cw + p * 2.f;
    v->frame.h = chh + p * 2.f;
    if (max_w > 0.f && v->frame.w > max_w)
      v->frame.w = max_w;
    if (max_h > 0.f && v->frame.h > max_h)
      v->frame.h = max_h;
    break;
  }
  case SZ_VIEW_SIZED: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float tw = scale_px(theme, (float)v->img_w);
    float th = scale_px(theme, (float)v->img_h);
    if (max_w > 0.f && tw > max_w)
      tw = max_w;
    if (max_h > 0.f && th > max_h)
      th = max_h;
    v->frame.w = tw;
    v->frame.h = th;
    if (ch)
      layout_constrained(ch, x, y, box_tight(tw, th), theme);
    break;
  }
  case SZ_VIEW_MIN_SIZE: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float child_min_w = scale_px(theme, (float)v->img_w);
    float child_min_h = scale_px(theme, (float)v->img_h);
    if (child_min_w < min_w)
      child_min_w = min_w;
    if (child_min_h < min_h)
      child_min_h = min_h;
    if (max_w > 0.f && child_min_w > max_w)
      child_min_w = max_w;
    if (max_h > 0.f && child_min_h > max_h)
      child_min_h = max_h;
    if (ch) {
      SzBoxConstraints cc;
      cc.min_w = child_min_w;
      cc.min_h = child_min_h;
      cc.max_w = max_w;
      cc.max_h = max_h;
      layout_constrained(ch, x, y, cc, theme);
    }
    v->frame.w = ch ? ch->frame.w : child_min_w;
    v->frame.h = ch ? ch->frame.h : child_min_h;
    break;
  }
  case SZ_VIEW_MAX_SIZE: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float child_max_w = cap_max_axis(max_w, scale_px(theme, (float)v->img_w));
    float child_max_h = cap_max_axis(max_h, scale_px(theme, (float)v->img_h));
    float child_min_w = min_w;
    float child_min_h = min_h;
    if (child_max_w > 0.f && child_min_w > child_max_w)
      child_min_w = child_max_w;
    if (child_max_h > 0.f && child_min_h > child_max_h)
      child_min_h = child_max_h;
    if (ch) {
      SzBoxConstraints cc;
      cc.min_w = child_min_w;
      cc.min_h = child_min_h;
      cc.max_w = child_max_w;
      cc.max_h = child_max_h;
      layout_constrained(ch, x, y, cc, theme);
    }
    v->frame.w = ch ? ch->frame.w : child_min_w;
    v->frame.h = ch ? ch->frame.h : child_min_h;
    break;
  }
  case SZ_VIEW_BACKGROUND:
  case SZ_VIEW_CLIP:
  case SZ_VIEW_OPACITY:
  case SZ_VIEW_IGNORE_POINTER:
  case SZ_VIEW_ABSORB_POINTER:
  case SZ_VIEW_EXCLUDE_SEMANTICS:
  case SZ_VIEW_TEXT_COLOR:
  case SZ_VIEW_BORDER:
  case SZ_VIEW_RADIUS:
  case SZ_VIEW_BADGE:
  case SZ_VIEW_TOOLTIP:
  case SZ_VIEW_PLACEHOLDER:
  case SZ_VIEW_SEMANTICS:
  case SZ_VIEW_MERGE_SEMANTICS:
  case SZ_VIEW_INK_WELL:
  case SZ_VIEW_VISIBILITY:
    layout_pass_child(v, x, y, min_w, min_h, max_w, max_h, theme);
    break;
  case SZ_VIEW_OFFSTAGE:
    layout_pass_child(v, x, y, min_w, min_h, max_w, max_h, theme);
    if (!view_offstage_shown(v)) {
      v->frame.w = 0.f;
      v->frame.h = 0.f;
    }
    break;
  case SZ_VIEW_UNCONSTRAINED_BOX:
    /* Child gets unbounded max (0). Incoming max still clamps this frame. */
    layout_pass_child(v, x, y, 0.f, 0.f, 0.f, 0.f, theme);
    break;
  case SZ_VIEW_SPLIT: {
    SzView *left = v->child_count > 0 ? v->children[0] : NULL;
    SzView *right = v->child_count > 1 ? v->children[1] : NULL;
    float handle = 6.f;
    float avail = max_w > handle ? max_w - handle : 0.f;
    float n = (float)slider_clamp(v->sig_int ? sz_signal_int_get(v->sig_int) : 50);
    float lw = avail * (n / 100.f);
    float rw = avail - lw;
    float inner_h = max_h > 0.f ? max_h : theme->control_h;
    if (lw < 0.f)
      lw = 0.f;
    if (rw < 0.f)
      rw = 0.f;
    if (left)
      layout_constrained(left, x, y, box_tight_width(lw, inner_h), theme);
    if (right)
      layout_constrained(right, x + lw + handle, y, box_tight_width(rw, inner_h),
                         theme);
    v->frame.w = max_w > 0.f ? max_w : lw + handle + rw;
    v->frame.h = inner_h;
    break;
  }
  case SZ_VIEW_OVERLAY:
    if (!view_overlay_open(v)) {
      if (v->child_count > 0)
        layout_pass_child(v, x, y, min_w, min_h, max_w, max_h, theme);
      v->frame.w = 0.f;
      v->frame.h = 0.f;
      break;
    }
    v->frame.w = max_w > 0.f ? max_w : min_w;
    v->frame.h = max_h > 0.f ? max_h : min_h;
    if (v->child_count > 0)
      layout_constrained(v->children[0], x, y,
                         box_loose(v->frame.w, v->frame.h), theme);
    break;
  case SZ_VIEW_MAX_LINES: {
    int prev = g_max_lines;
    g_max_lines = tighten_max_lines(v->img_w);
    layout_pass_child(v, x, y, min_w, min_h, max_w, max_h, theme);
    g_max_lines = prev;
    break;
  }
  case SZ_VIEW_ELLIPSIS: {
    int prev = g_ellipsis;
    g_ellipsis = 1;
    layout_pass_child(v, x, y, min_w, min_h, max_w, max_h, theme);
    g_ellipsis = prev;
    break;
  }
  case SZ_VIEW_GAP: {
    int prev_on = g_gap_on;
    float prev = g_gap;
    g_gap_on = 1;
    g_gap = scale_px(theme, (float)v->img_w);
    layout_pass_child(v, x, y, min_w, min_h, max_w, max_h, theme);
    g_gap_on = prev_on;
    g_gap = prev;
    break;
  }
  case SZ_VIEW_FONT_SIZE: {
    float prev = g_font_px;
    g_font_px = scale_px(theme, (float)v->img_w);
    layout_pass_child(v, x, y, min_w, min_h, max_w, max_h, theme);
    g_font_px = prev;
    break;
  }
  case SZ_VIEW_ASPECT_RATIO: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float rw = (float)(v->img_w > 0 ? v->img_w : 1);
    float rh = (float)(v->img_h > 0 ? v->img_h : 1);
    float tw = 0.f;
    float th = 0.f;
    if (max_w > 0.f && max_h > 0.f) {
      th = max_w * rh / rw;
      if (th <= max_h) {
        tw = max_w;
      } else {
        tw = max_h * rw / rh;
        th = max_h;
      }
    } else if (max_w > 0.f) {
      tw = max_w;
      th = max_w * rh / rw;
    } else if (max_h > 0.f) {
      th = max_h;
      tw = max_h * rw / rh;
    }
    v->frame.w = tw;
    v->frame.h = th;
    if (ch)
      layout_constrained(ch, x, y, box_tight(tw, th), theme);
    break;
  }
  case SZ_VIEW_FRACTION: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    int wp = v->img_w;
    int hp = v->img_h;
    float child_max_w = max_w;
    float child_max_h = max_h;
    if (wp > 0 && max_w > 0.f)
      child_max_w = max_w * (float)wp / 100.f;
    if (hp > 0 && max_h > 0.f)
      child_max_h = max_h * (float)hp / 100.f;
    if (ch) {
      SzBoxConstraints cc;
      if (wp > 0 && hp > 0)
        cc = box_tight(child_max_w, child_max_h);
      else if (wp > 0)
        cc = box_tight_width(child_max_w, child_max_h);
      else if (hp > 0)
        cc = box_tight_height(child_max_w, child_max_h);
      else
        cc = box_loose(child_max_w, child_max_h);
      layout_constrained(ch, x, y, cc, theme);
    }
    v->frame.w = wp > 0 ? child_max_w : (ch ? ch->frame.w : 0.f);
    v->frame.h = hp > 0 ? child_max_h : (ch ? ch->frame.h : 0.f);
    break;
  }
  case SZ_VIEW_SCROLL: {
    float pad = theme->pad;
    if (v->scroll_h) {
      float ch_h = 0.f;
      float ch_w = 0.f;
      if (v->scroll_child) {
        /* Width 0 = unbounded. Height stays intrinsic unless the slot is tight. */
        float child_max_h = 0.f;
        if (min_h > 0.f && max_h > 0.f && min_h >= max_h - 0.5f &&
            max_h > pad * 2.f)
          child_max_h = max_h - pad * 2.f;
        layout_constrained(v->scroll_child, x + pad - v->scroll_x, y + pad,
                           box_loose(0.f, child_max_h), theme);
        ch_w = v->scroll_child->frame.w;
        ch_h = v->scroll_child->frame.h;
      }
      v->frame.w = max_w > 0.f ? max_w : ch_w + pad * 2.f;
      v->frame.h = ch_h + pad * 2.f;
      break;
    }
    {
      float inner_w = max_w - pad * 2.f;
      float vh;
      if (v->pref_h > 0)
        vh = v->pref_h;
      else if (max_h > 0)
        vh = max_h;
      else if (min_h > 0)
        vh = min_h;
      else
        vh = 0.f; /* Expanded flex slot may be empty; do not invent height */
      if (max_h > 0 && vh > max_h)
        vh = max_h;
      v->frame.w = max_w;
      v->frame.h = vh;
      if (v->scroll_child) {
        /* Height 0 = unbounded. A fake large max makes Expanded rows fill it. */
        layout_constrained(v->scroll_child, x + pad, y + pad - v->scroll_y,
                           box_loose(inner_w > 0 ? inner_w : max_w, 0.f),
                           theme);
      }
    }
    break;
  }
  default:
    v->frame.w = 0;
    v->frame.h = 0;
    break;
  }
  if (v->frame.w < min_w)
    v->frame.w = min_w;
  if (v->frame.h < min_h)
    v->frame.h = min_h;
  if (max_w > 0.f && v->frame.w > max_w)
    v->frame.w = max_w;
  if (max_h > 0.f && v->frame.h > max_h)
    v->frame.h = max_h;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v)) {
    v->frame.w = 0.f;
    v->frame.h = 0.f;
  }
}

void sz_view_layout(SzView *root, float width, float height, const SzTheme *theme) {
  if (!root || !theme)
    return;
  g_max_lines = 0;
  g_ellipsis = 0;
  g_gap_on = 0;
  g_font_px = 0.f;
  layout_node(root, 0.f, 0.f, width, height, theme);
}

static int point_in(const SzRect *r, float x, float y) {
  return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static SzView *hit_node(SzView *v, float x, float y) {
  int i;
  SzView *hit;
  if (!v || !view_is_shown(v) || !point_in(&v->frame, x, y))
    return NULL;
  if (v->kind == SZ_VIEW_IGNORE_POINTER)
    return NULL;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return NULL;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return NULL;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return NULL;
  if (v->kind == SZ_VIEW_ABSORB_POINTER)
    return v;
  /* Front-to-back: last child wins. */
  for (i = v->child_count - 1; i >= 0; i--) {
    hit = hit_node(v->children[i], x, y);
    if (hit)
      return hit;
  }
  return v->interactive ? v : NULL;
}

SzView *sz_view_hit_test(SzView *root, float x, float y) {
  return hit_node(root, x, y);
}

static SzView *tooltip_at_node(SzView *v, float x, float y) {
  int i;
  SzView *found;
  if (!v || !view_is_shown(v) || !point_in(&v->frame, x, y))
    return NULL;
  if (v->kind == SZ_VIEW_IGNORE_POINTER)
    return NULL;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return NULL;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return NULL;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return NULL;
  for (i = v->child_count - 1; i >= 0; i--) {
    found = tooltip_at_node(v->children[i], x, y);
    if (found)
      return found;
  }
  return v->kind == SZ_VIEW_TOOLTIP ? v : NULL;
}

SzView *sz_view_tooltip_at(SzView *root, float x, float y) {
  return tooltip_at_node(root, x, y);
}

void sz_view_clear_hover(SzView *root) {
  int i;
  if (!root)
    return;
  root->hover = 0;
  for (i = 0; i < root->child_count; i++)
    sz_view_clear_hover(root->children[i]);
}

int sz_view_set_hover_at(SzView *root, float x, float y) {
  SzView *tip;
  sz_view_clear_hover(root);
  tip = tooltip_at_node(root, x, y);
  if (!tip)
    return 0;
  tip->hover = 1;
  return 1;
}

static const float k_text_field_inset = 6.f;

static int g_clip_on;
static SzRect g_clip;
static int g_opacity = 100;
static int g_text_color_on;
static uint32_t g_text_argb;
static int g_radius_on;
static float g_radius;
static SzRect g_radius_rect;

static uint32_t apply_paint_alpha(uint32_t argb) {
  uint32_t a;
  if (g_opacity >= 100)
    return argb;
  if (g_opacity <= 0)
    return argb & 0x00ffffffu;
  a = (argb >> 24) & 0xffu;
  a = (a * (uint32_t)g_opacity) / 100u;
  return (a << 24) | (argb & 0x00ffffffu);
}

static int rects_intersect(SzRect a, SzRect b, SzRect *out) {
  float x0 = a.x > b.x ? a.x : b.x;
  float y0 = a.y > b.y ? a.y : b.y;
  float x1 = a.x + a.w;
  float y1 = a.y + a.h;
  float bx1 = b.x + b.w;
  float by1 = b.y + b.h;
  if (bx1 < x1)
    x1 = bx1;
  if (by1 < y1)
    y1 = by1;
  if (x1 <= x0 || y1 <= y0)
    return 0;
  out->x = x0;
  out->y = y0;
  out->w = x1 - x0;
  out->h = y1 - y0;
  return 1;
}

static float clamp_radius(float r, float w, float h) {
  if (r < 0.f)
    r = 0.f;
  if (r * 2.f > w)
    r = w * 0.5f;
  if (r * 2.f > h)
    r = h * 0.5f;
  return r;
}

/* Horizontal span of the radius clip at row `y`. */
static int rrect_x_span(SzRect r, float radius, float y, float *x0, float *x1) {
  float rr, dy, dx, inside, cy;
  if (y < r.y || y >= r.y + r.h)
    return 0;
  rr = clamp_radius(radius, r.w, r.h);
  *x0 = r.x;
  *x1 = r.x + r.w;
  if (rr < 0.5f)
    return 1;
  if (y + 1.f <= r.y + rr) {
    cy = r.y + rr;
    dy = cy - (y + 0.5f);
    inside = rr * rr - dy * dy;
    if (inside < 0.f)
      inside = 0.f;
    dx = sqrtf(inside);
    *x0 = r.x + rr - dx;
    *x1 = r.x + r.w - rr + dx;
  } else if (y >= r.y + r.h - rr) {
    cy = r.y + r.h - rr;
    dy = (y + 0.5f) - cy;
    inside = rr * rr - dy * dy;
    if (inside < 0.f)
      inside = 0.f;
    dx = sqrtf(inside);
    *x0 = r.x + rr - dx;
    *x1 = r.x + r.w - rr + dx;
  }
  return 1;
}

static void paint_rect(SkCanvas *c, float x, float y, float w, float h,
                       uint32_t argb) {
  SzRect req, cut;
  SkPaint *p;
  if (w <= 0.f || h <= 0.f)
    return;
  req.x = x;
  req.y = y;
  req.w = w;
  req.h = h;
  if (g_clip_on) {
    if (!rects_intersect(req, g_clip, &cut))
      return;
    x = cut.x;
    y = cut.y;
    w = cut.w;
    h = cut.h;
  } else {
    cut = req;
  }
  p = sk_paint_new();
  if (!p)
    return;
  sk_paint_set_color(p, sk_color_argb(apply_paint_alpha(argb)));
  if (g_radius_on && g_radius >= 0.5f) {
    float row = cut.y;
    float y1 = cut.y + cut.h;
    while (row < y1) {
      float row_h = 1.f;
      float rx0, rx1, sx, sw;
      if (row + row_h > y1)
        row_h = y1 - row;
      if (rrect_x_span(g_radius_rect, g_radius, row, &rx0, &rx1)) {
        sx = cut.x > rx0 ? cut.x : rx0;
        sw = (cut.x + cut.w < rx1 ? cut.x + cut.w : rx1) - sx;
        if (sw > 0.f)
          sk_canvas_draw_rect(c, sx, row, sw, row_h, p);
      }
      row += row_h;
    }
  } else {
    sk_canvas_draw_rect(c, x, y, w, h, p);
  }
  sk_paint_delete(p);
}

/* Stroke stays inside the frame so Headless pixels do not overflow. */
static void paint_border(SkCanvas *c, SzRect f, int width, uint32_t argb) {
  float t;
  if (width <= 0 || f.w <= 0.f || f.h <= 0.f)
    return;
  t = (float)width;
  if (t > f.w)
    t = f.w;
  if (t > f.h)
    t = f.h;
  if (t * 2.f >= f.w || t * 2.f >= f.h) {
    paint_rect(c, f.x, f.y, f.w, f.h, argb);
    return;
  }
  paint_rect(c, f.x, f.y, f.w, t, argb);
  paint_rect(c, f.x, f.y + f.h - t, f.w, t, argb);
  paint_rect(c, f.x, f.y + t, t, f.h - 2.f * t, argb);
  paint_rect(c, f.x + f.w - t, f.y + t, t, f.h - 2.f * t, argb);
}

static void paint_placeholder_mark(SkCanvas *c, SzRect f, uint32_t argb) {
  int i, steps;
  float n;
  paint_border(c, f, 1, argb);
  n = f.w < f.h ? f.w : f.h;
  steps = (int)(n + 0.5f);
  if (steps < 2)
    return;
  for (i = 0; i < steps; i++) {
    float t = (float)i / (float)(steps - 1);
    float x = f.x + t * (f.w - 1.f);
    float y0 = f.y + t * (f.h - 1.f);
    float y1 = f.y + (1.f - t) * (f.h - 1.f);
    paint_rect(c, x, y0, 1.f, 1.f, argb);
    paint_rect(c, x, y1, 1.f, 1.f, argb);
  }
}

/* Clockwise from the top edge. `frac` is 0–1 of the perimeter. */
static void paint_ring_frac(SkCanvas *c, SzRect f, float t, float frac,
                            uint32_t argb) {
  float w = f.w;
  float h = f.h;
  if (t < 1.f)
    t = 1.f;
  if (frac <= 0.f || w <= 0.f || h <= 0.f)
    return;
  if (frac > 1.f)
    frac = 1.f;
  if (frac > 0.f) {
    float p = frac < 0.25f ? frac / 0.25f : 1.f;
    paint_rect(c, f.x, f.y, w * p, t, argb);
  }
  if (frac > 0.25f) {
    float p = frac < 0.5f ? (frac - 0.25f) / 0.25f : 1.f;
    paint_rect(c, f.x + w - t, f.y, t, h * p, argb);
  }
  if (frac > 0.5f) {
    float p = frac < 0.75f ? (frac - 0.5f) / 0.25f : 1.f;
    paint_rect(c, f.x + w * (1.f - p), f.y + h - t, w * p, t, argb);
  }
  if (frac > 0.75f) {
    float p = (frac - 0.75f) / 0.25f;
    paint_rect(c, f.x, f.y + h * (1.f - p), t, h * p, argb);
  }
}

static void paint_string(SkCanvas *c, const char *s, float x, float y,
                         uint32_t argb, float font_px) {
  SkPaint *p;
  if (g_clip_on) {
    if (x >= g_clip.x + g_clip.w || y < g_clip.y ||
        y - font_px > g_clip.y + g_clip.h)
      return;
  }
  p = sk_paint_new();
  if (!p)
    return;
  sk_paint_set_color(p, sk_color_argb(apply_paint_alpha(argb)));
  sk_paint_set_text_size(p, font_px);
  sk_canvas_draw_string(c, s ? s : "", x, y, p);
  sk_paint_delete(p);
}

static void paint_mono_string(SkCanvas *c, const char *s, float x, float y,
                              uint32_t argb, float font_px) {
  SkPaint *p;
  if (g_clip_on) {
    if (x >= g_clip.x + g_clip.w || y < g_clip.y ||
        y - font_px > g_clip.y + g_clip.h)
      return;
  }
  p = sk_paint_new();
  if (!p)
    return;
  sk_paint_set_color(p, sk_color_argb(apply_paint_alpha(argb)));
  sk_paint_set_text_size(p, font_px);
  sk_canvas_draw_mono_string(c, s ? s : "", x, y, p);
  sk_paint_delete(p);
}

static int editor_cols(const char *s, int start, int end) {
  int n = 0;
  int i = start;
  if (!s || end <= start)
    return 0;
  while (i < end) {
    int clen = utf8_clen(s, i);
    if (clen < 1)
      clen = 1;
    if (i + clen > end)
      clen = end - i;
    i += clen;
    n++;
  }
  return n;
}

static int editor_ident_end(const char *s, int i, int end) {
  while (i < end) {
    unsigned char ch = (unsigned char)s[i];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '_'))
      break;
    i++;
  }
  return i;
}

static int editor_is_kw(const char *s, int a, int b) {
  static const char *kws[] = {
      "def",     "for",     "if",      "else",  "match",   "case",
      "yield",   "import",  "enum",    "record","trait",   "impl",
      "type",    "private", "package", "true",  "false",   NULL};
  int n = b - a;
  int i;
  if (n <= 0)
    return 0;
  for (i = 0; kws[i]; i++) {
    if ((int)strlen(kws[i]) == n && memcmp(s + a, kws[i], (size_t)n) == 0)
      return 1;
  }
  return 0;
}

static void paint_editor_token(SkCanvas *c, const char *s, int line_start,
                               int tok_start, int tok_end, float base_x,
                               float baseline, float font_px, float scroll_x,
                               float text_w, uint32_t argb) {
  float cell = sk_font_mono_cell(font_px);
  int first_col, vis_cols, i, n, vis0, vis1, col0, tok_col, out_n;
  char tmp[256];
  if (!s || tok_end <= tok_start)
    return;
  if (cell < 1.f)
    cell = 1.f;
  first_col = (int)(scroll_x / cell);
  if (first_col < 0)
    first_col = 0;
  vis_cols = (int)(text_w / cell) + 2;
  if (vis_cols < 1)
    vis_cols = 1;
  vis0 = first_col;
  vis1 = first_col + vis_cols;
  col0 = editor_cols(s, line_start, tok_start);
  if (col0 + editor_cols(s, tok_start, tok_end) <= vis0 || col0 >= vis1)
    return;
  i = tok_start;
  n = col0;
  while (i < tok_end && n < vis0) {
    int clen = utf8_clen(s, i);
    if (clen < 1)
      clen = 1;
    if (i + clen > tok_end)
      break;
    i += clen;
    n++;
  }
  tok_col = n;
  out_n = 0;
  while (i < tok_end && n < vis1 && out_n < (int)sizeof(tmp) - 5) {
    int clen = utf8_clen(s, i);
    if (clen < 1)
      clen = 1;
    if (i + clen > tok_end)
      clen = tok_end - i;
    memcpy(tmp + out_n, s + i, (size_t)clen);
    out_n += clen;
    i += clen;
    n++;
  }
  tmp[out_n] = '\0';
  if (out_n > 0)
    paint_mono_string(c, tmp, base_x + (float)tok_col * cell, baseline, argb,
                      font_px);
}

static void paint_editor_line_hl(SkCanvas *c, const char *s, int start, int end,
                                 float base_x, float baseline, float font_px,
                                 float scroll_x, float text_w,
                                 const SzTheme *theme) {
  int i = start;
  if (!theme)
    return;
  while (i < end) {
    int tok_s = i;
    int tok_e;
    uint32_t col = theme->foreground;
    unsigned char ch = (unsigned char)s[i];
    if (ch == '/' && i + 1 < end && s[i + 1] == '/') {
      tok_e = end;
      col = theme->muted;
    } else if (ch == '"') {
      i++;
      while (i < end && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < end)
          i++;
        i++;
      }
      if (i < end)
        i++;
      tok_e = i;
      col = theme->accent;
    } else if (ch >= '0' && ch <= '9') {
      while (i < end && s[i] >= '0' && s[i] <= '9')
        i++;
      tok_e = i;
      col = theme->primary;
    } else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
               ch == '_') {
      tok_e = editor_ident_end(s, i, end);
      if (editor_is_kw(s, tok_s, tok_e))
        col = theme->primary;
      i = tok_e;
    } else {
      i++;
      tok_e = i;
    }
    paint_editor_token(c, s, start, tok_s, tok_e, base_x, baseline, font_px,
                       scroll_x, text_w, col);
    if (tok_e <= tok_s)
      break;
    i = tok_e;
  }
}

typedef struct SzWrapPaint {
  SkCanvas *c;
  float x;
  float y;
  float font_px;
  float line_h;
  float inner;
  uint32_t argb;
  int cap;
  int drawn;
  int ellipsis;
} SzWrapPaint;

static void paint_wrap_line(const char *s, int start, int end, float width,
                            void *env) {
  SzWrapPaint *wp = (SzWrapPaint *)env;
  char tmp[256];
  int n;
  (void)width;
  if (wp->cap > 0 && wp->drawn >= wp->cap)
    return;
  n = end - start;
  if (n < 0)
    n = 0;
  if (n >= (int)sizeof tmp)
    n = (int)sizeof tmp - 1;
  if (n > 0)
    memcpy(tmp, s + start, (size_t)n);
  tmp[n] = '\0';
  if (wp->ellipsis && wp->cap > 0 && wp->drawn == wp->cap - 1)
    ellipsize_to_width(tmp, sizeof tmp, wp->inner, wp->font_px);
  paint_string(wp->c, tmp, wp->x, wp->y, wp->argb, wp->font_px);
  wp->y += wp->line_h;
  wp->drawn++;
}

static void paint_node(SzView *v, SkCanvas *c, const SzTheme *theme);

static void paint_children_clipped(SzView *v, SkCanvas *c, const SzTheme *theme) {
  int i;
  SzRect prev = g_clip;
  int prev_on = g_clip_on;
  SzRect next;
  if (g_clip_on) {
    if (!rects_intersect(prev, v->frame, &next)) {
      next.x = v->frame.x;
      next.y = v->frame.y;
      next.w = 0.f;
      next.h = 0.f;
    }
    g_clip = next;
  } else {
    g_clip = v->frame;
    g_clip_on = 1;
  }
  for (i = 0; i < v->child_count; i++)
    paint_node(v->children[i], c, theme);
  g_clip = prev;
  g_clip_on = prev_on;
}

static void paint_node(SzView *v, SkCanvas *c, const SzTheme *theme) {
  char buf[256];
  int i;
  float tx, ty;

  if (!v || !c || !view_is_shown(v))
    return;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return;

  switch (v->kind) {
  case SZ_VIEW_TEXT: {
    SzWrapPaint wp;
    float inner = 0.f;
    float font_px = layout_font_px(theme);
    resolve_text(v, buf, sizeof buf);
    if (v->frame.w > scale_px(theme, 4.f))
      inner = v->frame.w - scale_px(theme, 4.f);
    wp.c = c;
    wp.x = v->frame.x + scale_px(theme, 2.f);
    wp.y = v->frame.y + font_px + scale_px(theme, 2.f);
    wp.font_px = font_px;
    wp.line_h = text_line_h(theme, font_px);
    wp.inner = inner;
    wp.argb = g_text_color_on ? g_text_argb : theme->foreground;
    wp.cap = text_line_cap();
    wp.drawn = 0;
    wp.ellipsis = 0;
    if (g_ellipsis && wp.cap > 0) {
      SzWrapMetrics m;
      m.max_line_w = 0.f;
      m.n = 0;
      m.cap = wp.cap;
      m.truncated = 0;
      each_text_line(buf, font_px, inner, accum_wrap_line, &m);
      wp.ellipsis = m.truncated;
    }
    each_text_line(buf, font_px, inner, paint_wrap_line, &wp);
    break;
  }
  case SZ_VIEW_BUTTON:
    resolve_text(v, buf, sizeof buf);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->primary);
    tx = v->frame.x + theme->pad;
    ty = v->frame.y + (v->frame.h + theme->font_px) * 0.5f;
    paint_string(c, buf, tx, ty, theme->on_primary, theme->font_px);
    break;
  case SZ_VIEW_OUTLINED_BUTTON: {
    SzRect br = v->frame;
    resolve_text(v, buf, sizeof buf);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    paint_border(c, br, (int)(scale_px(theme, 1.f) + 0.5f), theme->border);
    tx = v->frame.x + theme->pad;
    ty = v->frame.y + (v->frame.h + theme->font_px) * 0.5f;
    paint_string(c, buf, tx, ty, theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_TEXT_BUTTON:
    resolve_text(v, buf, sizeof buf);
    tx = v->frame.x + theme->pad;
    ty = v->frame.y + (v->frame.h + theme->font_px) * 0.5f;
    paint_string(c, buf, tx, ty, theme->foreground, theme->font_px);
    break;
  case SZ_VIEW_ICON_BUTTON: {
    SzRect br = v->frame;
    resolve_text(v, buf, sizeof buf);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    paint_border(c, br, (int)(scale_px(theme, 1.f) + 0.5f), theme->border);
    paint_string(c, buf, v->frame.x + (v->frame.w - text_width(buf, theme->font_px)) * 0.5f,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_FAB: {
    int prev_on = g_radius_on;
    float prev_r = g_radius;
    SzRect prev_rect = g_radius_rect;
    float r = v->frame.w < v->frame.h ? v->frame.w * 0.5f : v->frame.h * 0.5f;
    resolve_text(v, buf, sizeof buf);
    g_radius_on = 1;
    g_radius = r;
    g_radius_rect = v->frame;
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->primary);
    g_radius_on = prev_on;
    g_radius = prev_r;
    g_radius_rect = prev_rect;
    paint_string(c, buf,
                 v->frame.x + (v->frame.w - text_width(buf, theme->font_px)) * 0.5f,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->on_primary, theme->font_px);
    break;
  }
  case SZ_VIEW_VERTICAL_DIVIDER: {
    float t = scale_px(theme, 2.f);
    float lx;
    if (t < 2.f)
      t = 2.f;
    if (t > v->frame.w)
      t = v->frame.w;
    lx = v->frame.x + (v->frame.w - t) * 0.5f;
    paint_rect(c, lx, v->frame.y, t, v->frame.h, theme->muted);
    break;
  }
  case SZ_VIEW_CHECKBOX: {
    float box = theme->font_px + 4.f;
    float gap;
    float bx, by;
    int on;
    SzRect br;
    if (box < 12.f)
      box = 12.f;
    if (box > theme->control_h - 4.f)
      box = theme->control_h - 4.f;
    gap = layout_gap(theme);
    bx = v->frame.x;
    by = v->frame.y + (v->frame.h - box) * 0.5f;
    br.x = bx;
    br.y = by;
    br.w = box;
    br.h = box;
    on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    if (on)
      paint_rect(c, bx, by, box, box, theme->primary);
    else
      paint_border(c, br, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    resolve_text(v, buf, sizeof buf);
    paint_string(c, buf, bx + box + gap,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_SWITCH: {
    float box = theme->font_px + 4.f;
    float gap;
    float bx, by;
    float tw, th, thumb, tx;
    int on;
    if (box < 12.f)
      box = 12.f;
    if (box > theme->control_h - 4.f)
      box = theme->control_h - 4.f;
    gap = layout_gap(theme);
    tw = box * 2.f;
    th = box;
    bx = v->frame.x;
    by = v->frame.y + (v->frame.h - th) * 0.5f;
    on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    paint_rect(c, bx, by, tw, th, on ? theme->primary : theme->muted);
    thumb = th > 4.f ? th - 4.f : th;
    tx = on ? bx + tw - thumb - 2.f : bx + 2.f;
    paint_rect(c, tx, by + (th - thumb) * 0.5f, thumb, thumb, theme->surface);
    resolve_text(v, buf, sizeof buf);
    paint_string(c, buf, bx + tw + gap,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_CHIP: {
    int on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    SzRect br = v->frame;
    resolve_text(v, buf, sizeof buf);
    if (on)
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h,
                 theme->primary);
    else {
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h,
                 theme->surface);
      paint_border(c, br, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    }
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 on ? theme->on_primary : theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_CHOICE_CHIP: {
    int on = v->sig_int && sz_signal_int_get(v->sig_int) == v->radio_value;
    SzRect br = v->frame;
    resolve_text(v, buf, sizeof buf);
    if (on)
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h,
                 theme->primary);
    else {
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h,
                 theme->surface);
      paint_border(c, br, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    }
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 on ? theme->on_primary : theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_ACTION_CHIP: {
    SzRect br = v->frame;
    resolve_text(v, buf, sizeof buf);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    paint_border(c, br, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_FILTER_CHIP: {
    int on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    SzRect br = v->frame;
    SzRect mark;
    float box = theme->font_px + 4.f;
    float gap;
    float bx, by;
    if (box < 12.f)
      box = 12.f;
    if (box > theme->control_h - 4.f)
      box = theme->control_h - 4.f;
    gap = layout_gap(theme);
    resolve_text(v, buf, sizeof buf);
    if (on)
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h,
                 theme->primary);
    else {
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h,
                 theme->surface);
      paint_border(c, br, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    }
    bx = v->frame.x + theme->pad;
    by = v->frame.y + (v->frame.h - box) * 0.5f;
    mark.x = bx;
    mark.y = by;
    mark.w = box;
    mark.h = box;
    if (on)
      paint_rect(c, bx, by, box, box, theme->on_primary);
    else
      paint_border(c, mark, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    paint_string(c, buf, bx + box + gap,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 on ? theme->on_primary : theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_INPUT_CHIP: {
    int on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    SzRect br = v->frame;
    SzRect mark;
    float box = theme->font_px + 4.f;
    float bx, by;
    if (box < 12.f)
      box = 12.f;
    if (box > theme->control_h - 4.f)
      box = theme->control_h - 4.f;
    resolve_text(v, buf, sizeof buf);
    if (on)
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h,
                 theme->primary);
    else {
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h,
                 theme->surface);
      paint_border(c, br, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    }
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 on ? theme->on_primary : theme->foreground, theme->font_px);
    bx = v->frame.x + v->frame.w - theme->pad - box;
    by = v->frame.y + (v->frame.h - box) * 0.5f;
    mark.x = bx;
    mark.y = by;
    mark.w = box;
    mark.h = box;
    paint_placeholder_mark(c, mark, on ? theme->on_primary : theme->muted);
    break;
  }
  case SZ_VIEW_LIST_TILE: {
    resolve_text(v, buf, sizeof buf);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    break;
  }
  case SZ_VIEW_CHECKBOX_LIST_TILE: {
    float box = theme->font_px + 4.f;
    float gap;
    float bx, by;
    int on;
    SzRect br;
    if (box < 12.f)
      box = 12.f;
    if (box > theme->control_h - 4.f)
      box = theme->control_h - 4.f;
    gap = layout_gap(theme);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    bx = v->frame.x + theme->pad;
    by = v->frame.y + (v->frame.h - box) * 0.5f;
    br.x = bx;
    br.y = by;
    br.w = box;
    br.h = box;
    on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    if (on)
      paint_rect(c, bx, by, box, box, theme->primary);
    else
      paint_border(c, br, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    resolve_text(v, buf, sizeof buf);
    paint_string(c, buf, bx + box + gap,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_SWITCH_LIST_TILE: {
    float box = theme->font_px + 4.f;
    float tw, th, thumb, tx;
    float bx, by;
    int on;
    if (box < 12.f)
      box = 12.f;
    if (box > theme->control_h - 4.f)
      box = theme->control_h - 4.f;
    tw = box * 2.f;
    th = box;
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    bx = v->frame.x + v->frame.w - theme->pad - tw;
    if (bx < v->frame.x + theme->pad)
      bx = v->frame.x + theme->pad;
    by = v->frame.y + (v->frame.h - th) * 0.5f;
    on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    paint_rect(c, bx, by, tw, th, on ? theme->primary : theme->muted);
    thumb = th > 4.f ? th - 4.f : th;
    tx = on ? bx + tw - thumb - 2.f : bx + 2.f;
    paint_rect(c, tx, by + (th - thumb) * 0.5f, thumb, thumb, theme->surface);
    resolve_text(v, buf, sizeof buf);
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_RADIO_LIST_TILE: {
    float box = theme->font_px + 4.f;
    float gap;
    float bx, by;
    float inset;
    int on;
    SzRect br;
    if (box < 12.f)
      box = 12.f;
    if (box > theme->control_h - 4.f)
      box = theme->control_h - 4.f;
    gap = layout_gap(theme);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    bx = v->frame.x + theme->pad;
    by = v->frame.y + (v->frame.h - box) * 0.5f;
    br.x = bx;
    br.y = by;
    br.w = box;
    br.h = box;
    paint_border(c, br, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    on = v->sig_int && sz_signal_int_get(v->sig_int) == v->radio_value;
    inset = box > 8.f ? 3.f : 1.f;
    if (on)
      paint_rect(c, bx + inset, by + inset, box - inset * 2.f, box - inset * 2.f,
                 theme->primary);
    resolve_text(v, buf, sizeof buf);
    paint_string(c, buf, bx + box + gap,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_SEGMENTED: {
    float hw = v->frame.w * 0.5f;
    float ty;
    int on;
    const char *left;
    const char *right;
    SzRect br = v->frame;
    on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    left = v->text ? v->text : "";
    right = v->prefix ? v->prefix : "";
    paint_rect(c, v->frame.x, v->frame.y, hw, v->frame.h,
               on ? theme->surface : theme->primary);
    paint_rect(c, v->frame.x + hw, v->frame.y, v->frame.w - hw, v->frame.h,
               on ? theme->primary : theme->surface);
    paint_border(c, br, (int)(scale_px(theme, 1.f) + 0.5f), theme->border);
    ty = v->frame.y + (v->frame.h + theme->font_px) * 0.5f;
    paint_string(c, left,
                 v->frame.x + (hw - text_width(left, theme->font_px)) * 0.5f, ty,
                 on ? theme->foreground : theme->on_primary, theme->font_px);
    paint_string(c, right,
                 v->frame.x + hw +
                     (hw - text_width(right, theme->font_px)) * 0.5f,
                 ty, on ? theme->on_primary : theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_BADGE: {
    float d;
    float bx, by;
    float font;
    int64_t n;
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    d = 14.f;
    if (d > v->frame.w)
      d = v->frame.w;
    if (d > v->frame.h)
      d = v->frame.h;
    if (d < 1.f)
      break;
    bx = v->frame.x + v->frame.w - d;
    by = v->frame.y;
    paint_rect(c, bx, by, d, d, theme->primary);
    n = v->sig_int ? sz_signal_int_get(v->sig_int) : 0;
    if (n < 0)
      n = 0;
    snprintf(buf, sizeof buf, "%lld", (long long)n);
    font = d * 0.6f;
    if (font < 8.f)
      font = 8.f;
    paint_string(c, buf, bx + 2.f, by + (d + font) * 0.5f, theme->on_primary,
                 font);
    break;
  }
  case SZ_VIEW_CARD: {
    SzRect br;
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    br.x = v->frame.x;
    br.y = v->frame.y;
    br.w = v->frame.w;
    br.h = v->frame.h;
    paint_border(c, br, (int)(scale_px(theme, 1.f) + 0.5f), theme->border);
    break;
  }
  case SZ_VIEW_DIVIDER: {
    float t = scale_px(theme, 2.f);
    float ly;
    if (t < 2.f)
      t = 2.f;
    if (t > v->frame.h)
      t = v->frame.h;
    ly = v->frame.y + (v->frame.h - t) * 0.5f;
    paint_rect(c, v->frame.x, ly, v->frame.w, t, theme->muted);
    break;
  }
  case SZ_VIEW_EXPANSION_TILE: {
    int on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    float hh = theme->control_h;
    char mark[2];
    resolve_text(v, buf, sizeof buf);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, hh, theme->surface);
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (hh + theme->font_px) * 0.5f, theme->foreground,
                 theme->font_px);
    mark[0] = on ? 'v' : '>';
    mark[1] = '\0';
    paint_string(c, mark, v->frame.x + v->frame.w - theme->pad - 8.f,
                 v->frame.y + (hh + theme->font_px) * 0.5f, theme->muted,
                 theme->font_px);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    break;
  }
  case SZ_VIEW_RADIO: {
    float box = theme->font_px + 4.f;
    float gap;
    float bx, by;
    float inset;
    int on;
    SzRect br;
    if (box < 12.f)
      box = 12.f;
    if (box > theme->control_h - 4.f)
      box = theme->control_h - 4.f;
    gap = layout_gap(theme);
    bx = v->frame.x;
    by = v->frame.y + (v->frame.h - box) * 0.5f;
    br.x = bx;
    br.y = by;
    br.w = box;
    br.h = box;
    paint_border(c, br, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    on = v->sig_int && sz_signal_int_get(v->sig_int) == v->radio_value;
    inset = box > 8.f ? 3.f : 1.f;
    if (on)
      paint_rect(c, bx + inset, by + inset, box - inset * 2.f, box - inset * 2.f,
                 theme->primary);
    resolve_text(v, buf, sizeof buf);
    paint_string(c, buf, bx + box + gap,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_SLIDER: {
    float track_h = 4.f;
    float thumb = 12.f;
    float n;
    float tx;
    float ty;
    float tw;
    if (v->frame.w < thumb)
      thumb = v->frame.w;
    n = (float)slider_clamp(v->sig_int ? sz_signal_int_get(v->sig_int) : 0);
    tw = v->frame.w > thumb ? v->frame.w - thumb : 0.f;
    tx = v->frame.x + (n / 100.f) * tw;
    ty = v->frame.y + (v->frame.h - thumb) * 0.5f;
    paint_rect(c, v->frame.x, v->frame.y + (v->frame.h - track_h) * 0.5f,
               v->frame.w, track_h, theme->muted);
    paint_rect(c, v->frame.x, v->frame.y + (v->frame.h - track_h) * 0.5f,
               tx - v->frame.x + thumb * 0.5f, track_h, theme->primary);
    paint_rect(c, tx, ty, thumb, thumb, theme->primary);
    break;
  }
  case SZ_VIEW_PROGRESS: {
    float n;
    float fw;
    n = (float)slider_clamp(v->sig_int ? sz_signal_int_get(v->sig_int) : 0);
    fw = v->frame.w * (n / 100.f);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->muted);
    if (fw > 0.f)
      paint_rect(c, v->frame.x, v->frame.y, fw, v->frame.h, theme->primary);
    break;
  }
  case SZ_VIEW_CIRCULAR_PROGRESS: {
    float n;
    float t;
    n = (float)slider_clamp(v->sig_int ? sz_signal_int_get(v->sig_int) : 0);
    t = scale_px(theme, 4.f);
    if (t < 2.f)
      t = 2.f;
    paint_border(c, v->frame, (int)(t + 0.5f), theme->muted);
    paint_ring_frac(c, v->frame, t, n / 100.f, theme->primary);
    break;
  }
  case SZ_VIEW_AVATAR: {
    int prev_on = g_radius_on;
    float prev_r = g_radius;
    SzRect prev_rect = g_radius_rect;
    float r = v->frame.w < v->frame.h ? v->frame.w * 0.5f : v->frame.h * 0.5f;
    resolve_text(v, buf, sizeof buf);
    g_radius_on = 1;
    g_radius = r;
    g_radius_rect = v->frame;
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->primary);
    g_radius_on = prev_on;
    g_radius = prev_r;
    g_radius_rect = prev_rect;
    paint_string(c, buf,
                 v->frame.x + (v->frame.w - text_width(buf, theme->font_px)) * 0.5f,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->on_primary, theme->font_px);
    break;
  }
  case SZ_VIEW_TEXT_FIELD: {
    const char *shown;
    resolve_text(v, buf, sizeof buf);
    shown = buf[0] ? buf : (v->placeholder ? v->placeholder : "");
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    /* border */
    {
      SkPaint *p = sk_paint_new();
      if (p) {
        sk_paint_set_color(p, sk_color_argb(apply_paint_alpha(
            v->focused ? theme->primary : theme->border)));
        sk_paint_set_stroke(p, 1);
        sk_paint_set_stroke_width(p, v->focused ? 2.f : 1.f);
        sk_canvas_draw_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, p);
        sk_paint_delete(p);
      }
    }
    if (buf[0] && field_has_sel(v)) {
      int a, b;
      float x0, x1, y, h;
      field_sel_bounds(v, &a, &b);
      x0 = v->frame.x + k_text_field_inset + span_width(buf, 0, a, theme->font_px);
      x1 = v->frame.x + k_text_field_inset + span_width(buf, 0, b, theme->font_px);
      if (x0 > v->frame.x + v->frame.w - 2.f)
        x0 = v->frame.x + v->frame.w - 2.f;
      if (x1 > v->frame.x + v->frame.w - 2.f)
        x1 = v->frame.x + v->frame.w - 2.f;
      if (x1 > x0) {
        h = theme->font_px;
        if (h > v->frame.h - 4.f)
          h = v->frame.h - 4.f;
        if (h < 1.f)
          h = 1.f;
        y = v->frame.y + (v->frame.h - h) * 0.5f;
        paint_rect(c, x0, y, x1 - x0, h, theme->accent);
      }
    }
    paint_string(c, shown, v->frame.x + k_text_field_inset,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 buf[0] ? theme->foreground : theme->muted, theme->font_px);
    if (v->focused) {
      SzRect caret = sz_view_caret_rect(v, theme);
      if (caret.w > 0.f)
        paint_rect(c, caret.x, caret.y, caret.w, caret.h, theme->primary);
    }
    break;
  }
  case SZ_VIEW_EDITOR: {
    const char *s = field_cstr(v);
    int n, i, line, a, b;
    float font_px = theme->font_px;
    float line_h = text_line_h(theme, font_px);
    float inset = k_text_field_inset;
    float gutter = editor_gutter_w(v, font_px);
    float text_w = v->frame.w - gutter;
    float base_x = v->frame.x + gutter + inset - v->scroll_x;
    float base_y = v->frame.y + inset - v->scroll_y;
    SzRect prev_clip = g_clip;
    int prev_on = g_clip_on;
    SzRect next;
    if (!s)
      s = "";
    n = (int)strlen(s);
    if (g_clip_on) {
      if (!rects_intersect(prev_clip, v->frame, &next)) {
        next = v->frame;
        next.w = 0.f;
        next.h = 0.f;
      }
      g_clip = next;
    } else {
      g_clip = v->frame;
      g_clip_on = 1;
    }
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    if (gutter > 0.f)
      paint_rect(c, v->frame.x, v->frame.y, gutter, v->frame.h, theme->background);
    {
      SkPaint *p = sk_paint_new();
      if (p) {
        sk_paint_set_color(p, sk_color_argb(apply_paint_alpha(
            v->focused ? theme->primary : theme->border)));
        sk_paint_set_stroke(p, 1);
        sk_paint_set_stroke_width(p, v->focused ? 2.f : 1.f);
        sk_canvas_draw_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, p);
        sk_paint_delete(p);
      }
    }
    field_sel_bounds(v, &a, &b);
    {
      int first_line;
      int last_line;
      if (line_h < 1.f)
        line_h = 1.f;
      first_line = (int)(v->scroll_y / line_h);
      if (first_line < 0)
        first_line = 0;
      last_line = first_line + (int)(v->frame.h / line_h) + 2;
      line = 0;
      i = 0;
      while (line < first_line && i < n) {
        if (s[i] == '\n')
          line++;
        i++;
      }
      while (line <= last_line) {
        int start = i;
        int end;
        float top;
        while (i < n && s[i] != '\n')
          i++;
        end = i;
        top = base_y + (float)line * line_h;
        {
          int sa = a > start ? a : start;
          int sb = b < end ? b : end;
          if (field_has_sel(v) && sb > sa) {
            float x0 = base_x + editor_span_width(s, start, sa, font_px);
            float x1 = base_x + editor_span_width(s, start, sb, font_px);
            float h = font_px;
            if (h < 1.f)
              h = 1.f;
            if (x1 > x0)
              paint_rect(c, x0, top, x1 - x0, h, theme->accent);
          }
        }
        {
          char num[16];
          int sev = editor_diag_at(v, line + 1);
          snprintf(num, sizeof num, "%d", line + 1);
          paint_mono_string(c, num, v->frame.x + 2.f, top + font_px, theme->muted,
                            font_px);
          if (sev) {
            float mark = font_px < 4.f ? 2.f : 3.f;
            paint_rect(c, v->frame.x + gutter - mark - 1.f, top + 1.f, mark, mark,
                       theme->accent);
          }
        }
        paint_editor_line_hl(c, s, start, end, base_x, top + font_px, font_px,
                             v->scroll_x, text_w > 8.f ? text_w : 8.f, theme);
        if (i >= n)
          break;
        i++;
        line++;
      }
    }
    if (v->focused) {
      SzRect caret = sz_view_caret_rect(v, theme);
      if (caret.w > 0.f)
        paint_rect(c, caret.x, caret.y, caret.w, caret.h, theme->primary);
    }
    g_clip = prev_clip;
    g_clip_on = prev_on;
    break;
  }
  case SZ_VIEW_ICON: {
    char g[2] = {v->glyph, '\0'};
    paint_string(c, g, v->frame.x + 2.f, v->frame.y + theme->font_px + 2.f,
                 v->fg_argb, theme->font_px);
    break;
  }
  case SZ_VIEW_BACKGROUND:
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, v->bg_argb);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    break;
  case SZ_VIEW_BORDER:
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    paint_border(c, v->frame, (int)(scale_px(theme, (float)v->img_w) + 0.5f),
                 v->bg_argb);
    break;
  case SZ_VIEW_RADIUS: {
    int prev_on = g_radius_on;
    float prev_r = g_radius;
    SzRect prev_rect = g_radius_rect;
    g_radius_on = 1;
    g_radius = scale_px(theme, (float)v->img_w);
    g_radius_rect = v->frame;
    paint_children_clipped(v, c, theme);
    g_radius_on = prev_on;
    g_radius = prev_r;
    g_radius_rect = prev_rect;
    break;
  }
  case SZ_VIEW_IMAGE:
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, v->bg_argb);
    if (v->text && v->text[0])
      paint_string(c, v->text, v->frame.x + 4.f,
                   v->frame.y + v->frame.h * 0.5f + 4.f, theme->on_primary,
                   theme->font_px);
    break;
  case SZ_VIEW_CLIP:
    paint_children_clipped(v, c, theme);
    break;
  case SZ_VIEW_OPACITY: {
    int prev = g_opacity;
    int pct = v->img_w;
    int next = (prev * pct) / 100;
    if (next < 0)
      next = 0;
    if (next > 100)
      next = 100;
    g_opacity = next;
    if (g_opacity > 0) {
      for (i = 0; i < v->child_count; i++)
        paint_node(v->children[i], c, theme);
    }
    g_opacity = prev;
    break;
  }
  case SZ_VIEW_MAX_LINES: {
    int prev = g_max_lines;
    g_max_lines = tighten_max_lines(v->img_w);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    g_max_lines = prev;
    break;
  }
  case SZ_VIEW_ELLIPSIS: {
    int prev = g_ellipsis;
    g_ellipsis = 1;
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    g_ellipsis = prev;
    break;
  }
  case SZ_VIEW_TEXT_COLOR: {
    int prev_on = g_text_color_on;
    uint32_t prev_argb = g_text_argb;
    g_text_color_on = 1;
    g_text_argb = v->bg_argb;
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    g_text_color_on = prev_on;
    g_text_argb = prev_argb;
    break;
  }
  case SZ_VIEW_FONT_SIZE: {
    float prev = g_font_px;
    g_font_px = scale_px(theme, (float)v->img_w);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    g_font_px = prev;
    break;
  }
  case SZ_VIEW_SCROLL:
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    paint_children_clipped(v, c, theme);
    break;
  case SZ_VIEW_COLUMN:
  case SZ_VIEW_ROW:
  case SZ_VIEW_WRAP:
  case SZ_VIEW_GRID:
  case SZ_VIEW_LIST:
  case SZ_VIEW_EXPANDED:
  case SZ_VIEW_STRETCH:
  case SZ_VIEW_CENTER:
  case SZ_VIEW_ALIGN:
  case SZ_VIEW_STACK:
  case SZ_VIEW_POSITIONED:
  case SZ_VIEW_PADDING:
  case SZ_VIEW_SIZED:
  case SZ_VIEW_MIN_SIZE:
  case SZ_VIEW_MAX_SIZE:
  case SZ_VIEW_ASPECT_RATIO:
  case SZ_VIEW_FRACTION:
  case SZ_VIEW_IGNORE_POINTER:
  case SZ_VIEW_ABSORB_POINTER:
  case SZ_VIEW_EXCLUDE_SEMANTICS:
  case SZ_VIEW_GAP:
  case SZ_VIEW_TOOLTIP:
  case SZ_VIEW_SEMANTICS:
  case SZ_VIEW_MERGE_SEMANTICS:
  case SZ_VIEW_INK_WELL:
  case SZ_VIEW_VISIBILITY:
  case SZ_VIEW_OFFSTAGE:
  case SZ_VIEW_UNCONSTRAINED_BOX:
  case SZ_VIEW_OVERLAY:
    if (v->kind == SZ_VIEW_LIST)
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    if (v->kind == SZ_VIEW_OVERLAY)
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, 0x66000000u);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    break;
  case SZ_VIEW_SPLIT: {
    float handle = 6.f;
    float n = (float)slider_clamp(v->sig_int ? sz_signal_int_get(v->sig_int) : 50);
    float hx;
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    hx = v->frame.x + (v->frame.w > handle ? (v->frame.w - handle) * (n / 100.f) : 0.f);
    paint_rect(c, hx, v->frame.y, handle, v->frame.h, theme->muted);
    break;
  }
  case SZ_VIEW_PLACEHOLDER: {
    SzRect f = v->frame;
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    paint_placeholder_mark(c, f, theme->muted);
    break;
  }
  default:
    break;
  }
}

static void paint_hover_bubble(SzView *v, SkCanvas *c, const SzTheme *theme,
                               float canvas_w, float canvas_h) {
  const char *msg;
  float pad, gap, tw, th, x, y;
  int prev_clip;
  if (!v || v->kind != SZ_VIEW_TOOLTIP || !v->hover || !c || !theme)
    return;
  msg = v->text ? v->text : "";
  if (!msg[0])
    return;
  pad = scale_px(theme, 4.f);
  gap = scale_px(theme, 4.f);
  tw = text_width(msg, theme->font_px) + pad * 2.f;
  th = theme->font_px + pad * 2.f;
  x = v->frame.x;
  y = v->frame.y + v->frame.h + gap;
  if (y + th > canvas_h && v->frame.y - gap - th >= 0.f)
    y = v->frame.y - gap - th;
  if (x + tw > canvas_w)
    x = canvas_w - tw;
  if (x < 0.f)
    x = 0.f;
  if (y < 0.f)
    y = 0.f;
  prev_clip = g_clip_on;
  g_clip_on = 0;
  paint_rect(c, x, y, tw, th, theme->surface);
  paint_string(c, msg, x + pad, y + pad + theme->font_px, theme->foreground,
               theme->font_px);
  g_clip_on = prev_clip;
}

static void paint_hover_tooltips(SzView *v, SkCanvas *c, const SzTheme *theme,
                                 float canvas_w, float canvas_h) {
  int i;
  if (!v)
    return;
  if (v->kind == SZ_VIEW_TOOLTIP && v->hover)
    paint_hover_bubble(v, c, theme, canvas_w, canvas_h);
  for (i = 0; i < v->child_count; i++)
    paint_hover_tooltips(v->children[i], c, theme, canvas_w, canvas_h);
}

/* Internal: used by ui.c */
int sz_view_paint(SzView *root, SkCanvas *canvas, int width, int height,
                  const SzTheme *theme) {
  if (!root || !canvas || !theme)
    return 0;
  g_clip_on = 0;
  g_opacity = 100;
  g_text_color_on = 0;
  g_radius_on = 0;
  sk_canvas_clear(canvas, sk_color_argb(theme->background));
  sz_view_layout(root, (float)width, (float)height, theme);
  paint_node(root, canvas, theme);
  paint_hover_tooltips(root, canvas, theme, (float)width, (float)height);
  return 1;
}

static void clear_focus(SzView *v) {
  int i;
  if (!v)
    return;
  v->focused = 0;
  for (i = 0; i < v->child_count; i++)
    clear_focus(v->children[i]);
}

float sz_view_scroll_x(const SzView *scroll) {
  if (!scroll || scroll->kind != SZ_VIEW_SCROLL)
    return 0.f;
  return scroll->scroll_x;
}

float sz_view_scroll_y(const SzView *scroll) {
  if (!scroll || scroll->kind != SZ_VIEW_SCROLL)
    return 0.f;
  return scroll->scroll_y;
}

int sz_view_scroll_is_h(const SzView *scroll) {
  return scroll && scroll->kind == SZ_VIEW_SCROLL && scroll->scroll_h;
}

void sz_view_scroll_by(SzView *scroll, float d) {
  if (!scroll)
    return;
  if (scroll->kind == SZ_VIEW_EDITOR) {
    const SzTheme *theme = sz_theme_default();
    const char *s = field_cstr(scroll);
    float line_h = text_line_h(theme, theme->font_px);
    int lines = 1;
    int i;
    float max_sy;
    if (s) {
      for (i = 0; s[i]; i++) {
        if (s[i] == '\n')
          lines++;
      }
    }
    scroll->scroll_y += d;
    max_sy = (float)lines * line_h + k_text_field_inset - scroll->frame.h;
    if (max_sy < 0.f)
      max_sy = 0.f;
    if (scroll->scroll_y > max_sy)
      scroll->scroll_y = max_sy;
    if (scroll->scroll_y < 0.f)
      scroll->scroll_y = 0.f;
    return;
  }
  if (scroll->kind != SZ_VIEW_SCROLL)
    return;
  if (scroll->scroll_h) {
    scroll->scroll_x += d;
    if (scroll->scroll_x < 0.f)
      scroll->scroll_x = 0.f;
  } else {
    scroll->scroll_y += d;
    if (scroll->scroll_y < 0.f)
      scroll->scroll_y = 0.f;
  }
}

static SzView *scroll_at_node(SzView *v, float x, float y) {
  int i;
  SzView *found;
  if (!v || !point_in(&v->frame, x, y))
    return NULL;
  if (v->kind == SZ_VIEW_IGNORE_POINTER || v->kind == SZ_VIEW_ABSORB_POINTER)
    return NULL;
  for (i = v->child_count - 1; i >= 0; i--) {
    found = scroll_at_node(v->children[i], x, y);
    if (found)
      return found;
  }
  return (v->kind == SZ_VIEW_SCROLL || v->kind == SZ_VIEW_EDITOR) ? v : NULL;
}

SzView *sz_view_scroll_at(SzView *root, float x, float y) {
  return scroll_at_node(root, x, y);
}

int sz_view_has_focused_text_field(SzView *root) {
  int i;
  if (!root)
    return 0;
  if (root->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(root))
    return 0;
  if (root->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(root))
    return 0;
  if (root->kind == SZ_VIEW_OVERLAY && !view_overlay_open(root))
    return 0;
  if (view_is_edit(root) && root->focused)
    return 1;
  for (i = 0; i < root->child_count; i++) {
    if (sz_view_has_focused_text_field(root->children[i]))
      return 1;
  }
  return 0;
}

static SzView *find_focused_edit(SzView *root) {
  int i;
  SzView *found;
  if (!root || !view_is_shown(root))
    return NULL;
  if (root->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(root))
    return NULL;
  if (root->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(root))
    return NULL;
  if (root->kind == SZ_VIEW_OVERLAY && !view_overlay_open(root))
    return NULL;
  if (view_is_edit(root) && root->focused)
    return root;
  for (i = 0; i < root->child_count; i++) {
    found = find_focused_edit(root->children[i]);
    if (found)
      return found;
  }
  return NULL;
}

static int line_index_at_off(const char *s, int off) {
  int i, line = 0;
  if (!s)
    return 0;
  if (off < 0)
    off = 0;
  for (i = 0; i < off && s[i]; i++) {
    if (s[i] == '\n')
      line++;
  }
  return line;
}

static int line_span(const char *s, int n, int want, int *start, int *end) {
  int i = 0, line = 0;
  *start = 0;
  *end = 0;
  if (!s)
    return 0;
  for (;;) {
    int st = i;
    while (i < n && s[i] != '\n')
      i++;
    if (line == want) {
      *start = st;
      *end = i;
      return 1;
    }
    if (i >= n)
      return 0;
    i++;
    line++;
  }
}

static SzRect editor_caret_rect(SzView *f, const SzTheme *theme) {
  SzRect z = {0, 0, 0, 0};
  const char *s;
  float font_px, line_h, inset, x, y, h;
  int c, ls, le, line;
  if (!f || f->kind != SZ_VIEW_EDITOR || !theme)
    return z;
  s = field_cstr(f);
  font_px = theme->font_px;
  line_h = text_line_h(theme, font_px);
  inset = k_text_field_inset;
  {
    float gutter = editor_gutter_w(f, font_px);
    c = field_caret_clamped(f);
    line_bounds_at_off(s, c, &ls, &le);
    (void)le;
    line = line_index_at_off(s, c);
    x = f->frame.x + gutter + inset + editor_span_width(s, ls, c, font_px) -
        f->scroll_x;
    y = f->frame.y + inset + (float)line * line_h - f->scroll_y;
    if (x > f->frame.x + f->frame.w - 2.f)
      x = f->frame.x + f->frame.w - 2.f;
    if (x < f->frame.x + gutter + 1.f)
      x = f->frame.x + gutter + 1.f;
  }
  if (y + line_h > f->frame.y + f->frame.h)
    y = f->frame.y + f->frame.h - line_h;
  if (y < f->frame.y)
    y = f->frame.y;
  h = font_px;
  if (h > line_h)
    h = line_h;
  if (h < 1.f)
    h = 1.f;
  z.x = x;
  z.y = y;
  z.w = 1.f;
  z.h = h;
  return z;
}

static void editor_scroll_to_caret(SzView *v) {
  const SzTheme *theme = sz_theme_default();
  const char *s;
  float font_px, line_h, inset, cx, cy;
  int c, ls, le, line;
  if (!v || v->kind != SZ_VIEW_EDITOR || v->frame.w <= 0.f || v->frame.h <= 0.f)
    return;
  s = field_cstr(v);
  font_px = theme->font_px;
  line_h = text_line_h(theme, font_px);
  inset = k_text_field_inset;
  {
    float gutter = editor_gutter_w(v, font_px);
    float text_w = v->frame.w - gutter;
    c = field_caret_clamped(v);
    line_bounds_at_off(s, c, &ls, &le);
    (void)le;
    line = line_index_at_off(s, c);
    cx = inset + editor_span_width(s, ls, c, font_px);
    cy = inset + (float)line * line_h;
    if (text_w < 8.f)
      text_w = 8.f;
    if (cx - v->scroll_x < inset)
      v->scroll_x = cx - inset;
    if (cx - v->scroll_x > text_w - 2.f)
      v->scroll_x = cx - (text_w - 2.f);
  }
  if (v->scroll_x < 0.f)
    v->scroll_x = 0.f;
  if (cy - v->scroll_y < 0.f)
    v->scroll_y = cy;
  if (cy + line_h - v->scroll_y > v->frame.h)
    v->scroll_y = cy + line_h - v->frame.h;
  if (v->scroll_y < 0.f)
    v->scroll_y = 0.f;
}

SzRect sz_view_caret_rect(SzView *root, const SzTheme *theme) {
  SzRect z = {0, 0, 0, 0};
  SzView *f;
  char buf[256];
  float x, y, h;
  int c, n;
  if (!root || !theme)
    return z;
  f = find_focused_edit(root);
  if (!f)
    return z;
  if (f->kind == SZ_VIEW_EDITOR)
    return editor_caret_rect(f, theme);
  resolve_text(f, buf, sizeof buf);
  c = field_caret_clamped(f);
  n = (int)strlen(buf);
  if (c < n)
    buf[c] = '\0';
  x = f->frame.x + k_text_field_inset + text_width(buf, theme->font_px);
  if (x > f->frame.x + f->frame.w - 2.f)
    x = f->frame.x + f->frame.w - 2.f;
  if (x < f->frame.x + k_text_field_inset)
    x = f->frame.x + k_text_field_inset;
  h = theme->font_px;
  if (h > f->frame.h - 4.f)
    h = f->frame.h - 4.f;
  if (h < 1.f)
    h = 1.f;
  y = f->frame.y + (f->frame.h - h) * 0.5f;
  z.x = x;
  z.y = y;
  z.w = 1.f;
  z.h = h;
  return z;
}

static int caret_offset_at_x(SzView *f, float x) {
  const char *s;
  int n, i, best;
  float local, best_d, font_px;
  if (!f || f->kind != SZ_VIEW_TEXT_FIELD)
    return 0;
  s = field_cstr(f);
  n = (int)strlen(s);
  font_px = sz_theme_default()->font_px;
  local = x - f->frame.x - k_text_field_inset;
  if (local <= 0.f || n == 0)
    return 0;
  best = 0;
  best_d = local;
  i = 0;
  while (i < n) {
    int clen = utf8_clen(s, i);
    float w, d;
    if (clen < 1)
      clen = 1;
    i += clen;
    if (i > n)
      i = n;
    w = span_width(s, 0, i, font_px);
    d = w > local ? w - local : local - w;
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

static int caret_offset_at_xy(SzView *f, float x, float y) {
  const char *s;
  const SzTheme *theme = sz_theme_default();
  float font_px, line_h, inset, local_x, local_y;
  int n, line, start, end;
  if (!f || f->kind != SZ_VIEW_EDITOR)
    return 0;
  s = field_cstr(f);
  n = (int)strlen(s);
  font_px = theme->font_px;
  line_h = text_line_h(theme, font_px);
  inset = k_text_field_inset;
  {
    float gutter = editor_gutter_w(f, font_px);
    local_y = y - f->frame.y - inset + f->scroll_y;
    local_x = x - f->frame.x - gutter - inset + f->scroll_x;
  }
  if (line_h < 1.f)
    line_h = 1.f;
  line = (int)(local_y / line_h);
  if (line < 0)
    line = 0;
  if (!line_span(s, n, line, &start, &end))
    return n;
  if (local_x <= 0.f)
    return start;
  return caret_on_line_at_width(s, start, end, local_x, font_px);
}

int sz_view_activate(SzView *root, SzView *hit, float x, float y) {
  if (!root || !hit)
    return 0;
  if (hit->kind != SZ_VIEW_TEXT_FIELD && hit->kind != SZ_VIEW_EDITOR)
    clear_focus(root);
  if ((hit->kind == SZ_VIEW_BUTTON || hit->kind == SZ_VIEW_ICON_BUTTON ||
       hit->kind == SZ_VIEW_FAB || hit->kind == SZ_VIEW_OUTLINED_BUTTON ||
       hit->kind == SZ_VIEW_TEXT_BUTTON || hit->kind == SZ_VIEW_ACTION_CHIP ||
       hit->kind == SZ_VIEW_INK_WELL) &&
      hit->on_tap) {
    hit->on_tap(hit, hit->tap_env);
    return 1;
  }
  if ((hit->kind == SZ_VIEW_CHECKBOX || hit->kind == SZ_VIEW_SWITCH ||
       hit->kind == SZ_VIEW_CHIP || hit->kind == SZ_VIEW_FILTER_CHIP ||
       hit->kind == SZ_VIEW_INPUT_CHIP ||
       hit->kind == SZ_VIEW_EXPANSION_TILE ||
       hit->kind == SZ_VIEW_CHECKBOX_LIST_TILE ||
       hit->kind == SZ_VIEW_SWITCH_LIST_TILE) &&
      hit->sig_int) {
    int64_t n = sz_signal_int_get(hit->sig_int);
    sz_signal_int_set(hit->sig_int, n == 0 ? 1 : 0);
    return 1;
  }
  if ((hit->kind == SZ_VIEW_RADIO || hit->kind == SZ_VIEW_RADIO_LIST_TILE ||
       hit->kind == SZ_VIEW_CHOICE_CHIP) &&
      hit->sig_int) {
    sz_signal_int_set(hit->sig_int, hit->radio_value);
    return 1;
  }
  if (hit->kind == SZ_VIEW_SEGMENTED && hit->sig_int) {
    float mid = hit->frame.x + hit->frame.w * 0.5f;
    sz_signal_int_set(hit->sig_int, x < mid ? 0 : 1);
    return 1;
  }
  if (hit->kind == SZ_VIEW_SLIDER)
    return sz_view_slider_set_at(hit, x);
  if (hit->kind == SZ_VIEW_SPLIT)
    return sz_view_split_set_at(hit, x);
  if (hit->kind == SZ_VIEW_OVERLAY) {
    if (hit->sig_int)
      sz_signal_int_set(hit->sig_int, 0);
    clear_focus(root);
    return 1;
  }
  if (hit->kind == SZ_VIEW_TEXT_FIELD) {
    clear_focus(root);
    hit->focused = 1;
    sz_view_set_text_field_caret(hit, caret_offset_at_x(hit, x));
    return 1;
  }
  if (hit->kind == SZ_VIEW_EDITOR) {
    clear_focus(root);
    hit->focused = 1;
    sz_view_set_editor_caret(hit, caret_offset_at_xy(hit, x, y));
    return 1;
  }
  return 0;
}

int sz_view_handle_tap(SzView *root, float x, float y) {
  SzView *hit;
  if (!root)
    return 0;
  hit = sz_view_hit_test(root, x, y);
  if (!hit)
    return 0;
  return sz_view_activate(root, hit, x, y);
}

static int collect_text_fields_node(SzView *v, SzView **out, int cap, int n) {
  int i;
  if (!v || !view_is_shown(v) || n >= cap)
    return n;
  if (v->kind == SZ_VIEW_EXCLUDE_SEMANTICS ||
      v->kind == SZ_VIEW_MERGE_SEMANTICS)
    return n;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return n;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return n;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return n;
  if (v->kind == SZ_VIEW_TEXT_FIELD)
    out[n++] = v;
  for (i = 0; i < v->child_count && n < cap; i++)
    n = collect_text_fields_node(v->children[i], out, cap, n);
  return n;
}

int sz_view_collect_text_fields(SzView *root, SzView **out, int cap) {
  if (!root || !out || cap <= 0)
    return 0;
  return collect_text_fields_node(root, out, cap, 0);
}

static int collect_editors_node(SzView *v, SzView **out, int cap, int n) {
  int i;
  if (!v || !view_is_shown(v) || n >= cap)
    return n;
  if (v->kind == SZ_VIEW_EXCLUDE_SEMANTICS ||
      v->kind == SZ_VIEW_MERGE_SEMANTICS)
    return n;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return n;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return n;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return n;
  if (v->kind == SZ_VIEW_EDITOR)
    out[n++] = v;
  for (i = 0; i < v->child_count && n < cap; i++)
    n = collect_editors_node(v->children[i], out, cap, n);
  return n;
}

int sz_view_collect_editors(SzView *root, SzView **out, int cap) {
  if (!root || !out || cap <= 0)
    return 0;
  return collect_editors_node(root, out, cap, 0);
}

static int collect_splits_node(SzView *v, SzView **out, int cap, int n) {
  int i;
  if (!v || !view_is_shown(v) || n >= cap)
    return n;
  if (v->kind == SZ_VIEW_EXCLUDE_SEMANTICS)
    return n;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return n;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return n;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return n;
  if (v->kind == SZ_VIEW_SPLIT)
    out[n++] = v;
  for (i = 0; i < v->child_count && n < cap; i++)
    n = collect_splits_node(v->children[i], out, cap, n);
  return n;
}

int sz_view_collect_splits(SzView *root, SzView **out, int cap) {
  if (!root || !out || cap <= 0)
    return 0;
  return collect_splits_node(root, out, cap, 0);
}

static int collect_overlays_node(SzView *v, SzView **out, int cap, int n) {
  int i;
  if (!v || !view_is_shown(v) || n >= cap)
    return n;
  if (v->kind == SZ_VIEW_EXCLUDE_SEMANTICS)
    return n;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return n;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return n;
  if (v->kind == SZ_VIEW_OVERLAY)
    out[n++] = v;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return n;
  for (i = 0; i < v->child_count && n < cap; i++)
    n = collect_overlays_node(v->children[i], out, cap, n);
  return n;
}

int sz_view_collect_overlays(SzView *root, SzView **out, int cap) {
  if (!root || !out || cap <= 0)
    return 0;
  return collect_overlays_node(root, out, cap, 0);
}

static SzView *find_open_overlay(SzView *v) {
  int i;
  SzView *found;
  if (!v || !view_is_shown(v))
    return NULL;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return NULL;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return NULL;
  if (v->kind == SZ_VIEW_OVERLAY)
    return view_overlay_open(v) ? v : NULL;
  for (i = v->child_count - 1; i >= 0; i--) {
    found = find_open_overlay(v->children[i]);
    if (found)
      return found;
  }
  return NULL;
}

const char *sz_view_focus_kind(SzView *root) {
  SzView *ed;
  if (!root)
    return "none";
  ed = find_focused_edit(root);
  if (ed && ed->kind == SZ_VIEW_EDITOR)
    return "editor";
  if (ed && ed->kind == SZ_VIEW_TEXT_FIELD)
    return "field";
  if (find_open_overlay(root))
    return "overlay";
  return "none";
}

static int collect_walk_hidden(const SzView *v) {
  if (!v || !view_is_shown(v))
    return 1;
  if (v->kind == SZ_VIEW_EXCLUDE_SEMANTICS)
    return 1;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return 1;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return 1;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return 1;
  return 0;
}

static int collect_tap_targets_node(SzView *v, SzView **out, int cap, int n) {
  int i;
  if (collect_walk_hidden(v) || n >= cap)
    return n;
  if (sz_view_is_tap_target(v))
    out[n++] = v;
  for (i = 0; i < v->child_count && n < cap; i++)
    n = collect_tap_targets_node(v->children[i], out, cap, n);
  return n;
}

int sz_view_collect_tap_targets(SzView *root, SzView **out, int cap) {
  if (!root || !out || cap <= 0)
    return 0;
  return collect_tap_targets_node(root, out, cap, 0);
}

int sz_view_tap_label(SzView *root, const char *label) {
  SzView *taps[64];
  int n;
  int i;
  if (!root || !label || !label[0])
    return 0;
  n = sz_view_collect_tap_targets(root, taps, 64);
  for (i = 0; i < n; i++) {
    const char *lab = taps[i]->a11y_label;
    SzRect fr;
    if (!lab || strcmp(lab, label) != 0)
      continue;
    fr = taps[i]->frame;
    return sz_view_activate(root, taps[i], fr.x + 4.f, fr.y + 4.f);
  }
  return 0;
}

static int collect_scrolls_node(SzView *v, SzView **out, int cap, int n) {
  int i;
  if (collect_walk_hidden(v) || n >= cap)
    return n;
  if (v->kind == SZ_VIEW_SCROLL)
    out[n++] = v;
  for (i = 0; i < v->child_count && n < cap; i++)
    n = collect_scrolls_node(v->children[i], out, cap, n);
  return n;
}

int sz_view_collect_scrolls(SzView *root, SzView **out, int cap) {
  if (!root || !out || cap <= 0)
    return 0;
  return collect_scrolls_node(root, out, cap, 0);
}

/* Focused TextField, else first shown field in a11y (preorder) order. */
SzView *sz_view_text_field_target(SzView *root) {
  SzView *fields[64];
  int n = sz_view_collect_text_fields(root, fields, 64);
  int i;
  for (i = 0; i < n; i++) {
    if (fields[i]->focused)
      return fields[i];
  }
  return n > 0 ? fields[0] : NULL;
}

SzView *sz_view_edit_target(SzView *root) {
  SzView *focused;
  SzView *field;
  SzView *eds[64];
  SzView *scope;
  int n;
  scope = find_open_overlay(root);
  if (!scope)
    scope = root;
  focused = find_focused_edit(scope);
  if (focused)
    return focused;
  field = sz_view_text_field_target(scope);
  if (field)
    return field;
  n = sz_view_collect_editors(scope, eds, 64);
  return n > 0 ? eds[0] : NULL;
}

static SzView *text_field_at(SzView *root, int index) {
  SzView *fields[64];
  int n;
  if (index < 0)
    return sz_view_text_field_target(root);
  n = sz_view_collect_text_fields(root, fields, 64);
  if (index >= n)
    return NULL;
  return fields[index];
}

int sz_view_focus_text_field_at(SzView *root, int index) {
  SzView *t = text_field_at(root, index);
  if (!t)
    return 0;
  clear_focus(root);
  t->focused = 1;
  return 1;
}

int sz_view_focus_edit_target(SzView *root) {
  SzView *t = sz_view_edit_target(root);
  if (!t)
    return 0;
  clear_focus(root);
  t->focused = 1;
  return 1;
}

int sz_view_handle_text(SzView *root, const char *text) {
  SzView *target = sz_view_edit_target(root);
  const char *s;
  if (!target || !target->sig_str)
    return 0;
  editor_push_undo(target);
  sz_signal_str_set(target->sig_str, text ? text : "");
  s = field_cstr(target);
  target->caret = (int)strlen(s);
  target->sel_anchor = target->caret;
  target->focused = 1;
  if (target->kind == SZ_VIEW_EDITOR)
    editor_scroll_to_caret(target);
  return 1;
}

static int field_delete_sel(SzView *t) {
  const char *cur;
  int a, b, n;
  char *buf;
  if (!t || !t->sig_str || !field_has_sel(t))
    return 0;
  editor_push_undo(t);
  cur = field_cstr(t);
  n = (int)strlen(cur);
  field_sel_bounds(t, &a, &b);
  if (a < 0)
    a = 0;
  if (b > n)
    b = n;
  if (b <= a)
    return 0;
  buf = (char *)sz_alloc((size_t)(n - (b - a)) + 1);
  memcpy(buf, cur, (size_t)a);
  memcpy(buf + a, cur + b, (size_t)(n - b + 1));
  sz_signal_str_set(t->sig_str, buf);
  sz_free(buf);
  t->caret = a;
  t->sel_anchor = a;
  return 1;
}

static void field_insert_at_caret(SzView *t, const char *text) {
  const char *cur;
  int c, n, add;
  char *buf;
  if (!t || !t->sig_str || !text || !text[0])
    return;
  if (!field_delete_sel(t))
    editor_push_undo(t);
  cur = field_cstr(t);
  n = (int)strlen(cur);
  c = field_caret_clamped(t);
  add = (int)strlen(text);
  buf = (char *)sz_alloc((size_t)n + (size_t)add + 1);
  memcpy(buf, cur, (size_t)c);
  memcpy(buf + c, text, (size_t)add);
  memcpy(buf + c + add, cur + c, (size_t)(n - c + 1));
  sz_signal_str_set(t->sig_str, buf);
  sz_free(buf);
  t->caret = c + add;
  t->sel_anchor = t->caret;
}

static void field_backspace_at_caret(SzView *t) {
  const char *cur;
  int c, n, keep;
  char *buf;
  if (!t || !t->sig_str)
    return;
  if (field_delete_sel(t))
    return;
  cur = field_cstr(t);
  n = (int)strlen(cur);
  c = field_caret_clamped(t);
  if (c <= 0)
    return;
  editor_push_undo(t);
  cur = field_cstr(t);
  n = (int)strlen(cur);
  c = field_caret_clamped(t);
  keep = utf8_prev(cur, c);
  buf = (char *)sz_alloc((size_t)(n - (c - keep)) + 1);
  memcpy(buf, cur, (size_t)keep);
  memcpy(buf + keep, cur + c, (size_t)(n - c + 1));
  sz_signal_str_set(t->sig_str, buf);
  sz_free(buf);
  t->caret = keep;
  t->sel_anchor = t->caret;
}

static void field_delete_at_caret(SzView *t) {
  const char *cur;
  int c, n, clen;
  char *buf;
  if (!t || !t->sig_str)
    return;
  if (field_delete_sel(t))
    return;
  cur = field_cstr(t);
  n = (int)strlen(cur);
  c = field_caret_clamped(t);
  if (c >= n)
    return;
  editor_push_undo(t);
  cur = field_cstr(t);
  n = (int)strlen(cur);
  c = field_caret_clamped(t);
  clen = utf8_clen(cur, c);
  if (clen < 1)
    clen = 1;
  if (c + clen > n)
    clen = n - c;
  buf = (char *)sz_alloc((size_t)(n - clen) + 1);
  memcpy(buf, cur, (size_t)c);
  memcpy(buf + c, cur + c + clen, (size_t)(n - c - clen + 1));
  sz_signal_str_set(t->sig_str, buf);
  sz_free(buf);
  t->caret = c;
  t->sel_anchor = t->caret;
}

static void field_move_caret(SzView *t, int dir) {
  const char *cur;
  int c, n, clen;
  if (!t)
    return;
  cur = field_cstr(t);
  n = (int)strlen(cur);
  c = field_caret_clamped(t);
  if (dir < 0)
    t->caret = utf8_prev(cur, c);
  else if (dir > 0) {
    clen = utf8_clen(cur, c);
    if (clen < 1)
      clen = 1;
    c += clen;
    if (c > n)
      c = n;
    t->caret = c;
  }
}

static void field_nudge_caret(SzView *t, int dir, int extend) {
  int a, b;
  if (!t)
    return;
  if (!extend && field_has_sel(t)) {
    field_sel_bounds(t, &a, &b);
    t->caret = dir < 0 ? a : b;
    t->sel_anchor = t->caret;
    return;
  }
  field_move_caret(t, dir);
  if (!extend)
    t->sel_anchor = field_caret_clamped(t);
}

static void editor_move_vert(SzView *t, int dir, int extend) {
  const char *s;
  float col_w, font_px;
  int c, ls, le, nls, nle, n;
  if (!t || t->kind != SZ_VIEW_EDITOR)
    return;
  s = field_cstr(t);
  n = (int)strlen(s);
  c = field_caret_clamped(t);
  line_bounds_at_off(s, c, &ls, &le);
  font_px = sz_theme_default()->font_px;
  col_w = editor_span_width(s, ls, c, font_px);
  if (dir < 0) {
    if (ls == 0)
      t->caret = 0;
    else {
      line_bounds_at_off(s, ls - 1, &nls, &nle);
      t->caret = caret_on_line_at_width(s, nls, nle, col_w, font_px);
    }
  } else if (le >= n)
    t->caret = n;
  else {
    line_bounds_at_off(s, le + 1, &nls, &nle);
    t->caret = caret_on_line_at_width(s, nls, nle, col_w, font_px);
  }
  if (!extend)
    t->sel_anchor = field_caret_clamped(t);
}

int sz_view_handle_text_edit(SzView *root, const char *text, int backspace) {
  SzView *target = sz_view_edit_target(root);
  if (!target || !target->sig_str)
    return 0;
  if (backspace)
    field_backspace_at_caret(target);
  else
    field_insert_at_caret(target, text);
  target->focused = 1;
  if (target->kind == SZ_VIEW_EDITOR)
    editor_scroll_to_caret(target);
  return 1;
}

static int key_is_one_code_point(const char *key) {
  int clen;
  if (!key || !key[0])
    return 0;
  clen = utf8_clen(key, 0);
  return clen > 0 && key[clen] == '\0' && (unsigned char)key[0] >= 32;
}

int sz_view_handle_key(SzView *root, const char *key, const char *text,
                       int mods) {
  SzView *target;
  SzView *overlay;
  int extend = (mods & SZ_KEY_SHIFT) != 0;
  int is_ed;
  if (!root)
    return 1;
  if (!key)
    key = "";
  overlay = find_open_overlay(root);
  if (overlay && strcmp(key, "Escape") == 0) {
    if (overlay->sig_int)
      sz_signal_int_set(overlay->sig_int, 0);
    clear_focus(root);
    return 1;
  }
  target = sz_view_edit_target(root);
  is_ed = target && target->kind == SZ_VIEW_EDITOR;
  if (strcmp(key, "Backspace") == 0) {
    (void)sz_view_handle_text_edit(root, NULL, 1);
    return 1;
  }
  if (strcmp(key, "Delete") == 0) {
    if (target)
      field_delete_at_caret(target);
    if (target)
      target->focused = 1;
    if (is_ed)
      editor_scroll_to_caret(target);
    return 1;
  }
  if (strcmp(key, "ArrowLeft") == 0) {
    if (target)
      field_nudge_caret(target, -1, extend);
    if (target)
      target->focused = 1;
    if (is_ed)
      editor_scroll_to_caret(target);
    return 1;
  }
  if (strcmp(key, "ArrowRight") == 0) {
    if (target)
      field_nudge_caret(target, 1, extend);
    if (target)
      target->focused = 1;
    if (is_ed)
      editor_scroll_to_caret(target);
    return 1;
  }
  if (strcmp(key, "ArrowUp") == 0) {
    if (is_ed) {
      editor_move_vert(target, -1, extend);
      target->focused = 1;
      editor_scroll_to_caret(target);
    }
    return 1;
  }
  if (strcmp(key, "ArrowDown") == 0) {
    if (is_ed) {
      editor_move_vert(target, 1, extend);
      target->focused = 1;
      editor_scroll_to_caret(target);
    }
    return 1;
  }
  if (strcmp(key, "Home") == 0) {
    if (target) {
      if (is_ed) {
        int ls, le;
        line_bounds_at_off(field_cstr(target), field_caret_clamped(target), &ls,
                           &le);
        target->caret = ls;
      } else
        target->caret = 0;
      if (!extend)
        target->sel_anchor = target->caret;
      target->focused = 1;
      if (is_ed)
        editor_scroll_to_caret(target);
    }
    return 1;
  }
  if (strcmp(key, "End") == 0) {
    if (target) {
      if (is_ed) {
        int ls, le;
        line_bounds_at_off(field_cstr(target), field_caret_clamped(target), &ls,
                           &le);
        target->caret = le;
      } else
        target->caret = (int)strlen(field_cstr(target));
      if (!extend)
        target->sel_anchor = target->caret;
      target->focused = 1;
      if (is_ed)
        editor_scroll_to_caret(target);
    }
    return 1;
  }
  if (is_ed && (strcmp(key, "PageUp") == 0 || strcmp(key, "PageDown") == 0)) {
    float line_h = text_line_h(sz_theme_default(), sz_theme_default()->font_px);
    int steps;
    int dir = strcmp(key, "PageUp") == 0 ? -1 : 1;
    int k;
    if (line_h < 1.f)
      line_h = 1.f;
    steps = (int)(target->frame.h / line_h);
    if (steps < 1)
      steps = 1;
    for (k = 0; k < steps; k++)
      editor_move_vert(target, dir, extend);
    target->focused = 1;
    editor_scroll_to_caret(target);
    return 1;
  }
  if (is_ed && strcmp(key, "Enter") == 0) {
    (void)sz_view_handle_text_edit(root, "\n", 0);
    return 1;
  }
  if (is_ed && strcmp(key, "Tab") == 0) {
    (void)sz_view_handle_text_edit(root, "  ", 0);
    return 1;
  }
  if ((mods & (SZ_KEY_CTRL | SZ_KEY_CMD)) != 0) {
    int z = strcmp(key, "z") == 0 || strcmp(key, "Z") == 0;
    int y = strcmp(key, "y") == 0 || strcmp(key, "Y") == 0;
    if (is_ed && z && !extend) {
      (void)sz_view_editor_undo(target);
      editor_scroll_to_caret(target);
      target->focused = 1;
      return 1;
    }
    if (is_ed && (y || (z && extend))) {
      (void)sz_view_editor_redo(target);
      editor_scroll_to_caret(target);
      target->focused = 1;
      return 1;
    }
    return 1;
  }
  if (text && text[0]) {
    (void)sz_view_handle_text_edit(root, text, 0);
    return 1;
  }
  if (strcmp(key, "Space") == 0) {
    (void)sz_view_handle_text_edit(root, " ", 0);
    return 1;
  }
  if (key_is_one_code_point(key))
    (void)sz_view_handle_text_edit(root, key, 0);
  return 1;
}

int sz_view_text_field_extend_to_x(SzView *view, float x) {
  if (!view || view->kind != SZ_VIEW_TEXT_FIELD)
    return 0;
  view->caret = caret_offset_at_x(view, x);
  return 1;
}

int sz_view_edit_extend_to_xy(SzView *view, float x, float y) {
  if (!view_is_edit(view))
    return 0;
  if (view->kind == SZ_VIEW_EDITOR)
    view->caret = caret_offset_at_xy(view, x, y);
  else
    view->caret = caret_offset_at_x(view, x);
  return 1;
}
