#include "scuzz_ui.h"

#include "sk_capi.h"

#include "rt_util.h"
#include "image.h"
#include "text.h"
#ifdef __EMSCRIPTEN__
#include "web.h"
static SzView *web_route_book;
static SzView *web_modal_scope;
#endif

#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDITOR_UNDO_MAX 64

typedef struct SzEditHist {
  char *text;
  int caret;
  int sel_anchor;
} SzEditHist;

typedef struct SzVirtualItem {
  int64_t key;
  void *value;
  double height;
} SzVirtualItem;

typedef struct SzVirtualIndex {
  int64_t key;
  int index;
} SzVirtualIndex;

typedef struct SzVirtualRow {
  int64_t key;
  SzView *view;
  SzSignal *item;
  SzSignalScope *scope;
  int index;
} SzVirtualRow;

struct SzView {
  SzViewKind kind;
  uintptr_t id;
  SzView *parent;
  SzView **children;
  int child_count;
  int child_cap;
  SzRect frame;
  int interactive; /* participates in hit-test */
  int inline_action, action_text;
  SzRect *action_boxes;
  int action_count, action_capacity;
  int focused;
  uintptr_t focus_id;
  uintptr_t modal_id, modal_return_id;
  uintptr_t return_focus_id;
  int modal_active;
  int split_restore;
  int pressed;

  /* common / kind-specific */
  char *text;
  char *prefix;
  char *placeholder;
  char *route_id;
  char *copy_text;
  int copy_pending;
  int heading_level;
  SzSignalInt *sig_int;
  SzSignalStr *sig_str;
  SzViewTapFn on_tap;
  void *tap_env;
  uint32_t bg_argb;
  uint32_t fg_argb;
  SzImage *image;
  SzText *styled;
  SzTheme palette;
  SzTheme layout_theme;
  int img_w;
  int img_h;
  int grid_min_width;
  int adaptive_min_width;
  int adaptive_selected;
  int64_t flex_weight;
  char glyph;
  double scroll_y;
  double scroll_x;
  int scroll_h; /* 1 = pan x (View.scrollH); 0 = pan y */
  float pref_h; /* >0 overrides natural height when set */
  SzView *scroll_child; /* owned as sole child for SCROLL */

  /* a11y */
  SzA11yRole a11y_role;
  char *a11y_label;

  /* showWhen: when show_when_sig != NULL, visible iff signal == show_when_value */
  SzSignalInt *show_when_sig;
  int64_t show_when_value;

  /* Reactive list content retains the last list cell. */
  SzSignalList *each_sig;
  SzList *each_seen; /* last synced list (retained; sentinel 1 = never synced) */
  SzViewEachFn each_fn;
  void *each_env;
  SzViewKeyFn virtual_key;
  void *virtual_key_env;
  SzViewRowFn virtual_render;
  int virtual_height;
  int virtual_count;
  int virtual_measured;
  double *virtual_heights;
  float virtual_measure_width;
  SzTheme virtual_measure_theme;
  float virtual_measure_font;
  float virtual_measure_gap;
  int virtual_measure_lines;
  int virtual_measure_ellipsis;
  SzVirtualItem *virtual_items;
  SzVirtualIndex *virtual_index;
  SzVirtualRow *virtual_rows;
  int virtual_row_count;

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
  int64_t range_minimum, range_maximum, range_step;
  /* TextField / editor caret: byte offset into the live string (UTF-8 snapped). */
  int caret;
  /* TextField / editor selection anchor (UTF-8 snapped). Collapsed when equal to caret. */
  int sel_anchor;
  /* IME compose preview. Not in the committed SignalStr until commit. */
  char *preedit;
  SzEditHist *undo;
  int undo_n;
  int undo_cap;
  SzEditHist *redo;
  int redo_n;
  int redo_cap;
  int *diag_line;
  int *diag_sev;
  int diag_n;
  /* LSP semantic tokens: decoded (line, col, length, type), 0-based. */
  int *tok_line;
  int *tok_col;
  int *tok_len;
  int *tok_ty;
  int tok_n;
  /* LSP inlay hints: 0-based line/col plus a copied label. */
  int *inlay_line;
  int *inlay_col;
  char **inlay_label;
  int inlay_n;
  /* LSP folding ranges: 0-based start/end. closed is 1 when interior is hidden. */
  int *fold_start;
  int *fold_end;
  int *fold_closed;
  int fold_n;
  /* Tooltip input and popup bounds use the shared layout. */
  int hover, tooltip_open, tooltip_dismissed, tooltip_lines;
  int hover_position_set;
  float hover_x, hover_y;
  SzRect tooltip_frame;
  float viewport_w, viewport_h;

  char **select_labels;
  int select_count, select_open, select_active, select_first, select_reveal_pending;
  int64_t select_observed;
  uintptr_t select_option_base;
  char select_search[128];
  int select_search_bytes;
  int64_t select_search_time;
  int64_t input_time;

  /* Pump-counted motion. 0 is quiet. The step is motion_left. */
  int animation_value, animation_from, animation_target;
  SzSignalInt *animation_axis2_signal;
  int animation_axis2, animation_from_axis2, animation_target_axis2;
  float content_w, content_h, content_from_w, content_from_h;
  float content_target_w, content_target_h;
  int animation_step, animation_duration, animation_ready;
  int motion_reduced, motion_paused, motion_suppressed;
  int motion;
  int motion_left;
  /* 1 after this step has been painted. The next pump drops it. */
  int motion_painted;
  /* Bit 0: this node has been observed. Bit 1: it was hidden. */
  int motion_flags;
  /* View.tabs: 1 after the first paint records the selection. */
  int tab_ready;
  int64_t tab_sel;
};

static void view_drop(void *ptr);

static int g_view_drop_ready;
static uintptr_t g_view_next_id = 1;

static SzView *view_new(SzViewKind kind) {
  SzView *v;
  if (!g_view_drop_ready) {
    sz_rc_set_view_drop(view_drop);
    g_view_drop_ready = 1;
  }
  v = (SzView *)sz_rc_alloc(sizeof(SzView), SZ_RC_VIEW);
  memset(v, 0, sizeof *v);
  v->kind = kind;
  v->layout_theme = *sz_theme_default();
  if (!g_view_next_id) sz_panic("View identity space is full");
  v->id = g_view_next_id++;
  return v;
}

static void view_set_tap(SzView *v, SzViewTapFn on_tap, void *env) {
  v->on_tap = on_tap;
  sz_retain(env);
  v->tap_env = env;
}

static int view_is_shown(const SzView *v);
static int size_collapsed(const SzView *view);
static float scale_px(const SzTheme *theme, float v);
static int clear_focus(SzView *v);
static void field_commit_preedit(SzView *v);
static int collect_walk_hidden(const SzView *v);
static void resolve_text(const SzView *v, char *buf, size_t buflen);
static void each_seen_set(SzView *v, SzList *xs);
static int rects_intersect(SzRect a, SzRect b, SzRect *out);

static int view_is_branch_shown(const SzView *v) {
  if (!v || (v->inline_action && v->action_count == 0)) return 0;
  if (v->kind == SZ_VIEW_SELECT_OPTION) {
    SzView *popup = v->parent ? v->parent->parent : NULL;
    SzRect visible;
    if (!popup || v->frame.w <= 0.f || v->frame.h <= 0.f || !rects_intersect(v->frame, popup->frame, &visible)) return 0;
  }
  const SzView *parent = v->parent;
  if (parent && parent->kind == SZ_VIEW_SELECT && !parent->select_open) return 0;
  if (parent && parent->kind == SZ_VIEW_EXPANSION_TILE &&
      (!parent->sig_int || sz_signal_int_get(parent->sig_int) == 0)) return 0;
  if (parent && parent->kind == SZ_VIEW_SPLIT) {
    int n = sz_view_split_frac(parent);
    if ((parent->children[0] == v && n == 0) ||
        (parent->child_count > 1 && parent->children[1] == v && n == 100)) return 0;
  }
  return !(parent && parent->kind == SZ_VIEW_ADAPTIVE &&
      parent->adaptive_selected < parent->child_count &&
      parent->children[parent->adaptive_selected] != v);
}

static int view_is_path_shown(const SzView *v) {
  for (; v; v = v->parent) {
    if (!view_is_branch_shown(v) || size_collapsed(v))
      return 0;
  }
  return 1;
}

static int view_is_shown(const SzView *v) {
  if (!view_is_branch_shown(v))
    return 0;
  if (v->show_when_sig &&
      sz_signal_int_get(v->show_when_sig) != v->show_when_value)
    return 0;
  return 1;
}

/* A transition paints this many pumps. The next pump is settled. */
#define SZ_MOTION_NONE 0
#define SZ_MOTION_PRESS 1
#define SZ_MOTION_APPEAR 2
#define SZ_MOTION_TAB 3
#define SZ_MOTION_INSERT 4
#define SZ_MOTION_PUMPS 4

static void motion_start(SzView *v, int kind) {
  if (!v || kind == SZ_MOTION_NONE || v->motion_suppressed)
    return;
  v->motion = kind;
  v->motion_left = SZ_MOTION_PUMPS;
  v->motion_painted = 0;
}

static const char *motion_name(int kind) {
  switch (kind) {
  case SZ_MOTION_PRESS:
    return "press";
  case SZ_MOTION_APPEAR:
    return "appear";
  case SZ_MOTION_TAB:
    return "tab";
  case SZ_MOTION_INSERT:
    return "insert";
  default:
    return "";
  }
}

/* 4 -> 40, 3 -> 55, 2 -> 70, 1 -> 85. A settled node stays at 100. */
static int motion_appear_pct(const SzView *v) {
  if (!v || v->motion != SZ_MOTION_APPEAR || v->motion_left <= 0)
    return 100;
  return 100 - v->motion_left * 15;
}

static int motion_can_appear(const SzView *v) {
  if (!v)
    return 0;
  if (v->kind == SZ_VIEW_BUTTON)
    return 1;
  return v->show_when_sig && v->parent && v->parent->kind == SZ_VIEW_TABS;
}

static const char *motion_label_src(const SzView *v) {
  int i;
  if (!v)
    return NULL;
  if (v->kind == SZ_VIEW_SCROLL && v->child_count > 0 &&
      v->children[0]->a11y_label && v->children[0]->a11y_label[0])
    return v->children[0]->a11y_label;
  if (v->a11y_label && v->a11y_label[0])
    return v->a11y_label;
  for (i = 0; i < v->child_count; i++) {
    const char *label = motion_label_src(v->children[i]);
    if (label && label[0])
      return label;
  }
  return NULL;
}

static void motion_label_copy(const SzView *v, char *dst, size_t n) {
  const char *src = motion_label_src(v);
  size_t i = 0;
  if (!dst || n == 0)
    return;
  if (!src || !src[0])
    src = "row";
  while (src[i] && i + 1 < n && i < 64) {
    char c = src[i];
    if (c == '\n' || c == '\r')
      c = ' ';
    dst[i++] = c;
  }
  while (i > 0 && ((unsigned char)src[i] & 0xc0u) == 0x80u) i--;
  dst[i] = '\0';
  if (i == 0)
    memcpy(dst, "row", 4);
}

static void motion_note_node(SzView *v) {
  int shown;
  int observed;
  int was_hidden;
  int i;
  int64_t sel;
  if (!v)
    return;
  shown = view_is_shown(v);
  observed = v->motion_flags & 1;
  was_hidden = v->motion_flags & 2;
  if (v->kind == SZ_VIEW_TABS && v->sig_int) {
    sel = sz_signal_int_get(v->sig_int);
    if (!v->tab_ready) {
      v->tab_ready = 1;
      v->tab_sel = sel;
    } else if (sel != v->tab_sel) {
      motion_start(v, SZ_MOTION_TAB);
      v->tab_sel = sel;
    }
  }
  if (observed && was_hidden && shown && motion_can_appear(v) &&
      v->motion == SZ_MOTION_NONE)
    motion_start(v, SZ_MOTION_APPEAR);
  v->motion_flags = 1 | (shown ? 0 : 2);
  for (i = 0; i < v->child_count; i++)
    motion_note_node(v->children[i]);
}

/* Drop a step that this tree already painted. A new step stays for this pump. */
static void motion_tick_node(SzView *v) {
  int i;
  if (!v)
    return;
  if (v->motion_left > 0 && v->motion_painted) {
    v->motion_left -= 1;
    v->motion_painted = 0;
    if (v->motion_left == 0)
      v->motion = SZ_MOTION_NONE;
  }
  for (i = 0; i < v->child_count; i++)
    motion_tick_node(v->children[i]);
}

static void motion_mark_painted(SzView *v) {
  int i;
  if (!v)
    return;
  v->motion_painted = v->motion_left > 0 ? 1 : 0;
  for (i = 0; i < v->child_count; i++)
    motion_mark_painted(v->children[i]);
}

static int transition_has_axes(const SzView *view) {
  return view->kind == SZ_VIEW_ANIMATED_SIZE || view->kind == SZ_VIEW_ANIMATED_POSITION;
}

static int view_has_transition(const SzView *view) {
  return transition_has_axes(view) || view->kind == SZ_VIEW_ANIMATED_OPACITY ||
    view->kind == SZ_VIEW_ANIMATED_CONTENT_SIZE || view->kind == SZ_VIEW_ANIMATED_SCALE;
}

static int motion_pending_node(const SzView *v) {
  int i;
  if (!v)
    return 0;
  if (v->motion_left > 0 || (view_has_transition(v) &&
      v->animation_ready && v->animation_step < v->animation_duration))
    return 1;
  for (i = 0; i < v->child_count; i++)
    if (motion_pending_node(v->children[i]))
      return 1;
  return 0;
}

static int transition_pending_node(const SzView *view) {
  if (!view) return 0;
  if (view_has_transition(view) && view->animation_ready &&
      view->animation_step < view->animation_duration) return 1;
  for (int i = 0; i < view->child_count; i++)
    if (transition_pending_node(view->children[i])) return 1;
  return 0;
}

int sz_view_transition_pending(const SzView *root) {
  return root && !root->motion_paused && transition_pending_node(root);
}

/* Return 1 when a transition still owns a later pump. */
int sz_view_motion_pending(const SzView *root) {
  if (root && root->motion_paused) return 0;
  return motion_pending_node(root);
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

static int collect_tap_targets_node(SzView *v, SzView **out, int cap, int n);
static void restore_group_focus(SzView *group);

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

static int view_accepts_children(const SzView *view) {
  SzViewKind kind = view->kind;
  return (kind == SZ_VIEW_TEXT && view->action_text) || kind == SZ_VIEW_VIRTUAL_LIST || kind == SZ_VIEW_ADAPTIVE || kind == SZ_VIEW_COLUMN || kind == SZ_VIEW_ROW ||
         kind == SZ_VIEW_WRAP || kind == SZ_VIEW_GRID || kind == SZ_VIEW_LIST ||
         kind == SZ_VIEW_SCROLL ||
         kind == SZ_VIEW_EXPANDED || kind == SZ_VIEW_CENTER ||
         kind == SZ_VIEW_ALIGN || kind == SZ_VIEW_STACK ||
         kind == SZ_VIEW_POSITIONED || kind == SZ_VIEW_ANIMATED_POSITION || kind == SZ_VIEW_PADDING ||
         kind == SZ_VIEW_SIZED || kind == SZ_VIEW_MIN_SIZE ||
         kind == SZ_VIEW_SELECT || kind == SZ_VIEW_THEME || kind == SZ_VIEW_BACKGROUND || kind == SZ_VIEW_ASPECT_RATIO ||
         kind == SZ_VIEW_FRACTION || kind == SZ_VIEW_STRETCH ||
         kind == SZ_VIEW_MAX_SIZE || kind == SZ_VIEW_CLIP ||
         kind == SZ_VIEW_OPACITY || kind == SZ_VIEW_ANIMATED_OPACITY || kind == SZ_VIEW_SCALE || kind == SZ_VIEW_ANIMATED_SCALE || kind == SZ_VIEW_ANIMATED_SIZE || kind == SZ_VIEW_ANIMATED_CONTENT_SIZE ||
         kind == SZ_VIEW_REDUCED_MOTION || kind == SZ_VIEW_MAX_LINES ||
         kind == SZ_VIEW_IGNORE_POINTER || kind == SZ_VIEW_ABSORB_POINTER ||
         kind == SZ_VIEW_EXCLUDE_SEMANTICS || kind == SZ_VIEW_ELLIPSIS ||
         kind == SZ_VIEW_TEXT_COLOR || kind == SZ_VIEW_GAP ||
         kind == SZ_VIEW_FONT_SIZE || kind == SZ_VIEW_BORDER ||
         kind == SZ_VIEW_RADIUS || kind == SZ_VIEW_LIST_TILE ||
         kind == SZ_VIEW_BADGE || kind == SZ_VIEW_CARD ||
         kind == SZ_VIEW_EXPANSION_TILE || kind == SZ_VIEW_SEGMENTED || kind == SZ_VIEW_TOOLTIP ||
         kind == SZ_VIEW_ON_SECONDARY || kind == SZ_VIEW_FOCUS_GROUP ||
         kind == SZ_VIEW_PLACEHOLDER || kind == SZ_VIEW_SEMANTICS ||
         kind == SZ_VIEW_MERGE_SEMANTICS || kind == SZ_VIEW_INK_WELL ||
         kind == SZ_VIEW_VISIBILITY || kind == SZ_VIEW_OFFSTAGE ||
         kind == SZ_VIEW_UNCONSTRAINED_BOX || kind == SZ_VIEW_SPLIT ||
         kind == SZ_VIEW_OVERLAY || kind == SZ_VIEW_INDEX_BOOK ||
         kind == SZ_VIEW_SECTION || kind == SZ_VIEW_TABS ||
         kind == SZ_VIEW_APP_SHELL || kind == SZ_VIEW_APP_BAR ||
         kind == SZ_VIEW_BREADCRUMB;
}

/* Expanded, or Stretch wrapping Expanded. */
static int64_t view_flex_weight(const SzView *v) {
  while (v) {
    if (v->kind == SZ_VIEW_EXPANDED)
      return v->flex_weight;
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

static int view_in_breadcrumb(const SzView *v) {
  return v && v->parent && v->parent->kind == SZ_VIEW_BREADCRUMB;
}

SzViewKind sz_view_kind(const SzView *view) {
  return view ? view->kind : (SzViewKind)0;
}

int sz_view_is_tap_target(const SzView *view) {
  return view &&
         ((view->kind == SZ_VIEW_SELECT && view->select_count > 0) ||
          view->kind == SZ_VIEW_SELECT_OPTION || view->kind == SZ_VIEW_BUTTON || view->kind == SZ_VIEW_CHECKBOX ||
          view->kind == SZ_VIEW_SLIDER || view->kind == SZ_VIEW_SPLIT || view->kind == SZ_VIEW_RADIO ||
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
          view->kind == SZ_VIEW_LINK ||
          view->kind == SZ_VIEW_NAV_TILE);
}

static float view_scale(const SzView *view) {
  if (view->kind == SZ_VIEW_SCALE) return view->img_w / 100.f;
  if (view->kind == SZ_VIEW_ANIMATED_SCALE) return view->animation_value / 100.f;
  return 1.f;
}

static SzRect scale_pivot_frame(const SzView *view) {
  return view->child_count ? view->children[0]->frame : view->frame;
}

typedef struct { double scale, x, y; } ViewTransform;

static ViewTransform display_transform(const SzView *view) {
  ViewTransform matrix = {1, 0, 0};
  for (const SzView *p = view; p; p = p->parent) {
    double scale = view_scale(p);
    if (scale != 1) {
      SzRect frame = scale_pivot_frame(p);
      double x = frame.x + frame.w * .5f;
      double y = frame.y + frame.h * .5f;
      matrix.x = scale * matrix.x + x * (1 - scale);
      matrix.y = scale * matrix.y + y * (1 - scale);
      matrix.scale *= scale;
    }
    /* Select popups use viewport coordinates. */
    if (p->parent && p->parent->kind == SZ_VIEW_SELECT) break;
  }
  return matrix;
}

float sz_view_paint_scale(const SzView *view) {
  ViewTransform matrix = display_transform(view);
  return isfinite(matrix.scale) && matrix.scale <= FLT_MAX &&
    isfinite(matrix.x) && isfinite(matrix.y) && fabs(matrix.x) <= FLT_MAX && fabs(matrix.y) <= FLT_MAX
      ? (float)matrix.scale : 0;
}

static float display_float(double value) {
  return (float)fmax(-FLT_MAX, fmin(FLT_MAX, value));
}

static SzRect display_rect(const SzView *view, SzRect frame) {
  ViewTransform matrix = display_transform(view);
  if (!(matrix.scale > 0) || matrix.scale > FLT_MAX ||
      !isfinite(matrix.x) || !isfinite(matrix.y) ||
      fabs(matrix.x) > FLT_MAX || fabs(matrix.y) > FLT_MAX) return (SzRect){0};
  return (SzRect){display_float(matrix.scale * frame.x + matrix.x),
    display_float(matrix.scale * frame.y + matrix.y),
    display_float(matrix.scale * frame.w), display_float(matrix.scale * frame.h)};
}

static int layout_point(const SzView *view, float *x, float *y) {
  if (!isfinite(*x) || !isfinite(*y)) return 0;
  ViewTransform matrix = display_transform(view);
  if (!(matrix.scale > 0) || matrix.scale > FLT_MAX ||
      !isfinite(matrix.x) || !isfinite(matrix.y) ||
      fabs(matrix.x) > FLT_MAX || fabs(matrix.y) > FLT_MAX) return 0;
  *x = display_float(((double)*x - matrix.x) / matrix.scale);
  *y = display_float(((double)*y - matrix.y) / matrix.scale);
  return isfinite(*x) && isfinite(*y);
}

SzRect sz_view_frame(const SzView *view) {
  return view ? display_rect(view, view->frame) : (SzRect){0};
}

void sz_view_add_child(SzView *parent, SzView *child) {
  if (!parent || !child)
    return;
  if (!view_accepts_children(parent))
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

SzView *sz_view_rich_text(const SzText *text) {
  SzView *view = sz_view_text(sz_text_source(text));
  view->styled = (SzText *)text;
  sz_retain(view->styled);
  return view;
}

SzView *sz_view_action_text(const SzText *text, SzViewTapFn tap, void *env) {
  SzView *view = sz_view_rich_text(text);
  view->action_text = 1;
  int run = 0;
  for (int index = 0; text && index < text->actions; index++) {
    int group = index + 1;
    while (run < text->count && text->runs[run].action != group) run++;
    int start = run < text->count ? text->runs[run].start : 0;
    int end = start;
    int64_t key = run < text->count ? text->runs[run].key : 0;
    while (run < text->count && text->runs[run].action == group) end = text->runs[run++].end;
    SzString *label = sz_string_from_bytes(sz_text_source(text) + start, (size_t)(end - start));
    SzView *action = sz_view_text_button(sz_string_cstr(label), tap, env);
    sz_release(label);
    action->inline_action = 1;
    action->radio_value = key;
    sz_view_add_child(view, action);
  }
  return view;
}

int64_t sz_view_text_action_id(const SzView *view) {
  return view && view->inline_action ? view->radio_value : 0;
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

int64_t sz_view_slider_minimum(const SzView *view) {
  return view && view->kind == SZ_VIEW_SLIDER ? view->range_minimum : 0;
}

int64_t sz_view_slider_maximum(const SzView *view) {
  return view && view->kind == SZ_VIEW_SLIDER ? view->range_maximum : 0;
}

static int64_t slider_value(const SzView *view, int64_t value) {
  if (value <= view->range_minimum) return view->range_minimum;
  if (value >= view->range_maximum) return view->range_maximum;
  __int128 distance = (__int128)value - view->range_minimum;
  __int128 index = (distance + view->range_step / 2) / view->range_step;
  return (int64_t)((__int128)view->range_minimum + index * view->range_step);
}

int64_t sz_view_slider_value(const SzView *view) {
  return view && view->kind == SZ_VIEW_SLIDER ?
    slider_value(view, view->sig_int ? sz_signal_int_get(view->sig_int) : view->range_minimum) : 0;
}

static double slider_fraction(const SzView *view) {
  double span = (double)((__int128)view->range_maximum - view->range_minimum);
  return span > 0 ? (double)((__int128)sz_view_slider_value(view) - view->range_minimum) / span : 0;
}

SzView *sz_view_slider(SzSignalInt *sig, const char *label, int64_t minimum,
                        int64_t maximum, int64_t step) {
  if (step <= 0) sz_panic("View.slider step must be positive");
  if (minimum > maximum) sz_panic("View.slider minimum must not exceed maximum");
  SzView *v = view_new(SZ_VIEW_SLIDER);
  v->sig_int = sig;
  v->range_minimum = minimum;
  v->range_step = step;
  __int128 count = ((__int128)maximum - minimum) / step;
  v->range_maximum = (int64_t)((__int128)minimum + count * step);
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_SLIDER;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_split(SzSignalInt *frac, const char *label, SzView *start, SzView *end) {
  if (!start || !end) sz_panic("Split requires two panes");
  SzView *v = view_new(SZ_VIEW_SPLIT);
  v->sig_int = frac;
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_SPLIT;
  v->a11y_label = sz_strdup(label && label[0] ? label : "Primary pane");
  v->split_restore = 50;
  if (start) {
    SzView *pane = sz_view_clip(start);
    pane->a11y_role = SZ_A11Y_REGION;
    pane->a11y_label = sz_strdup(v->a11y_label);
    sz_view_add_child(v, pane);
  }
  if (end) sz_view_add_child(v, sz_view_clip(end));
  return v;
}

SzView *sz_view_overlay(SzSignalInt *open, const char *label, SzView *child) {
  SzView *v = view_new(SZ_VIEW_OVERLAY);
  v->sig_int = open;
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_OVERLAY;
  v->a11y_label = sz_strdup(label && label[0] ? label : "Dialog");
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

SzView *sz_view_progress(SzSignalInt *sig, const char *label) {
  SzView *v = view_new(SZ_VIEW_PROGRESS);
  v->sig_int = sig;
  v->a11y_role = SZ_A11Y_PROGRESS;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_circular_progress(SzSignalInt *sig, const char *label) {
  SzView *v = view_new(SZ_VIEW_CIRCULAR_PROGRESS);
  v->sig_int = sig;
  v->a11y_role = SZ_A11Y_CIRCULAR;
  v->a11y_label = sz_strdup(label ? label : "");
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

SzView *sz_view_segmented(SzSignalInt *sig, const char *label,
                           const char *const *choices, int count) {
  if (count < 0 || (count > 0 && !choices)) sz_panic("Segmented choices are missing");
  SzView *view = view_new(SZ_VIEW_SEGMENTED);
  view->a11y_role = SZ_A11Y_RADIO_GROUP;
  view->a11y_label = sz_strdup(label && label[0] ? label : "Choices");
  for (int i = 0; i < count; i++)
    sz_view_add_child(view, sz_view_choice_chip(sig, i, choices[i]));
  return view;
}

static void select_sync_options(SzView *view) {
  SzList *items = sz_signal_list_get(view->each_sig);
  if (items == view->each_seen) return;
  int64_t count = sz_list_len(items);
  if (count > INT_MAX) sz_panic("Too many select options");
  char **labels = count ? sz_alloc((size_t)count * sizeof(*labels)) : NULL;
  int index = 0;
  for (SzList *cell = items; cell; cell = cell->tail) {
    const char *label = sz_string_cstr(cell->head);
    if (!label[0]) sz_panic("Select option needs a name");
    labels[index++] = sz_strdup(label);
  }
  for (int i = 0; i < view->select_count; i++) sz_free(view->select_labels[i]);
  sz_free(view->select_labels);
  view->select_labels = labels;
  view->select_count = (int)count;
  view->interactive = count > 0;
  view->select_open = 0;
  view->select_active = -1;
  view->select_first = -1;
  view->select_search_bytes = 0;
  view->select_observed = sz_signal_int_get(view->sig_int);
  if ((uintptr_t)count > UINTPTR_MAX - g_view_next_id) sz_panic("Select identity space is full");
  view->select_option_base = g_view_next_id;
  g_view_next_id += (uintptr_t)count;
  SzView *popup = sz_view_select_popup(view);
  if (popup) {
    popup->scroll_y = 0;
    SzView *rows = popup->scroll_child;
    for (int i = 0; i < rows->child_count; i++) {
      SzView *option = rows->children[i];
      option->radio_value = -1;
      option->id = 0;
      option->frame = (SzRect){0};
    }
  }
  each_seen_set(view, items);
}

SzView *sz_view_select(SzSignalInt *sig, const char *label, SzSignalList *choices) {
  if (!sig || !label || !label[0] || !choices) sz_panic("Select needs Signals and a name");
  SzView *view = view_new(SZ_VIEW_SELECT);
  view->sig_int = sig;
  view->each_sig = choices;
  view->each_seen = (SzList *)(uintptr_t)1;
  view->a11y_role = SZ_A11Y_COMBOBOX;
  view->a11y_label = sz_strdup(label);
  SzView *rows = sz_view_column();
  for (int i = 0; i < 9; i++) {
    SzView *option = view_new(SZ_VIEW_SELECT_OPTION);
    option->a11y_role = SZ_A11Y_OPTION;
    option->radio_value = -1;
    option->interactive = 1;
    sz_view_add_child(rows, option);
  }
  SzView *popup = sz_view_scroll(rows);
  popup->a11y_role = SZ_A11Y_LISTBOX;
  sz_free(popup->a11y_label);
  popup->a11y_label = sz_strdup(label);
  sz_view_add_child(view, popup);
  select_sync_options(view);
  return view;
}

static void sync_select_options(SzView *root) {
  if (root->kind == SZ_VIEW_SELECT) select_sync_options(root);
  for (int i = 0; i < root->child_count; i++) sync_select_options(root->children[i]);
}

int sz_view_select_open(const SzView *view) {
  return view && view->kind == SZ_VIEW_SELECT && view->select_open;
}
int sz_view_select_active(const SzView *view) {
  return view && view->kind == SZ_VIEW_SELECT ? view->select_active : -1;
}
int sz_view_select_count(const SzView *view) {
  return view && view->kind == SZ_VIEW_SELECT ? view->select_count : 0;
}
int64_t sz_view_select_value(const SzView *view) {
  return view && view->kind == SZ_VIEW_SELECT ? sz_signal_int_get(view->sig_int) : -1;
}
const char *sz_view_select_text(const SzView *view) {
  int64_t selected = sz_view_select_value(view);
  return selected >= 0 && selected < sz_view_select_count(view) ? view->select_labels[selected] : "";
}
SzView *sz_view_select_popup(SzView *view) {
  return view && view->kind == SZ_VIEW_SELECT && view->child_count ? view->children[0] : NULL;
}
static SzView *select_owner(const SzView *view) {
  for (SzView *p = view ? view->parent : NULL; p; p = p->parent)
    if (p->kind == SZ_VIEW_SELECT) return p;
  return NULL;
}
int sz_view_select_option_index(const SzView *view) {
  return view && view->kind == SZ_VIEW_SELECT_OPTION ? (int)view->radio_value : -1;
}
int sz_view_select_option_count(const SzView *view) {
  return sz_view_select_count(select_owner(view));
}
SzView *sz_view_select_active_option(SzView *view) {
  SzView *popup = sz_view_select_popup(view);
  if (!popup || !view->select_open || !popup->scroll_child) return NULL;
  for (int i = 0; i < popup->scroll_child->child_count; i++) {
    SzView *option = popup->scroll_child->children[i];
    if (option->radio_value == view->select_active && option->frame.h > 0.f) return option;
  }
  return NULL;
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
  SzString *source = sz_string_from_cstr(v->text);
  v->styled = sz_text_span(source);
  sz_release(source);
  v->a11y_role = SZ_A11Y_TOOLTIP;
  v->a11y_label = sz_strdup(message ? message : "");
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_on_secondary(SzView *child, SzViewTapFn on_tap, void *env) {
  SzView *v = view_new(SZ_VIEW_ON_SECONDARY);
  view_set_tap(v, on_tap, env);
  if (child)
    sz_view_add_child(v, child);
  return v;
}

SzView *sz_view_focus_group(SzView *child) {
  SzView *v = view_new(SZ_VIEW_FOCUS_GROUP);
  v->caret = -1;
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

SzView *sz_view_radio_group(const char *label, SzView *child) {
  SzView *view = sz_view_semantics(label, child);
  view->a11y_role = SZ_A11Y_RADIO_GROUP;
  return view;
}

SzView *sz_view_section(const char *id, const char *title, SzView *child) {
  SzView *v = sz_view_semantics(title, child);
  v->kind = SZ_VIEW_SECTION;
  v->route_id = sz_strdup(id ? id : "");
  return v;
}

static SzView *section_pages(SzSignalInt *selected, SzView *sections, int local) {
  SzView *book, *index;
  if (!selected || !sections || sections->kind != SZ_VIEW_COLUMN ||
      sections->child_count == 0)
    sz_panic("Page group needs a selection and a nonempty column of sections");
  for (int i = 0; i < sections->child_count; i++)
    if (sections->children[i]->kind != SZ_VIEW_SECTION ||
        !sections->children[i]->text || !sections->children[i]->text[0])
      sz_panic("Page group needs named sections");
  for (int i = 0; i < sections->child_count; i++) {
    const char *id = sections->children[i]->route_id;
    if (!id || !id[0]) sz_panic("Page group needs nonempty section IDs");
    for (int j = 0; j < i; j++)
      if (!strcmp(id, sections->children[j]->route_id))
        sz_panic("Page group needs unique section IDs");
  }
  book = view_new(local ? SZ_VIEW_TABS : SZ_VIEW_INDEX_BOOK);
  book->sig_int = selected;
  book->a11y_role = SZ_A11Y_SEMANTICS;
  book->a11y_label = sz_strdup(local ? "Tabs" : "Index book");
  index = sz_view_wrap();
  sz_view_add_child(book, sz_view_focus_group(sz_view_scroll(index)));
  if (local) {
    book->children[0]->a11y_role = SZ_A11Y_TAB_LIST;
    book->children[0]->a11y_label = sz_strdup("Tabs");
  }
  for (int i = 0; i < sections->child_count; i++) {
    SzView *section = sections->children[i];
    SzView *tab = sz_view_choice_chip(selected, i, section->text);
    if (local) {
      tab->a11y_role = SZ_A11Y_TAB;
      section->a11y_role = SZ_A11Y_TAB_PANEL;
    } else {
      tab->route_id = sz_strdup(section->route_id);
      size_t label_size = strlen(section->text) + 32;
      sz_free(tab->text);
      tab->text = (char *)sz_alloc(label_size);
      snprintf(tab->text, label_size, "%02d  %s", i + 1, section->text);
    }
    sz_view_add_child(index, tab);
    sz_view_add_child(book, sz_view_show_when(selected, i, sz_view_scroll(section)));
  }
  sections->child_count = 0;
  sz_view_free(sections);
  return book;
}

SzView *sz_view_index_book(SzSignalInt *selected, SzView *sections) {
  return section_pages(selected, sections, 0);
}

SzView *sz_view_tabs(SzSignalInt *selected, SzView *sections) {
  return section_pages(selected, sections, 1);
}

SzView *sz_view_app_shell(SzView *bar, SzView *body) {
  if (!bar || !body) sz_panic("App shell needs a bar and a body");
  SzView *v = view_new(SZ_VIEW_APP_SHELL);
  sz_view_add_child(v, bar);
  sz_view_add_child(v, body);
  return v;
}

SzView *sz_view_app_bar(SzView *title, SzView *actions) {
  if (!title || !actions) sz_panic("App bar needs a title and actions");
  SzView *v = view_new(SZ_VIEW_APP_BAR);
  v->a11y_role = SZ_A11Y_APP_BAR;
  v->a11y_label = sz_strdup("App bar");
  sz_view_add_child(v, sz_view_heading(1, title));
  sz_view_add_child(v, actions);
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
  SzView *view = view_new(SZ_VIEW_EXPANSION_TILE);
  view->sig_int = sig;
  view->text = sz_strdup(title && title[0] ? title : "Details");
  view->interactive = 1;
  view->a11y_role = SZ_A11Y_EXPANSION;
  view->a11y_label = sz_strdup(view->text);
  if (child) {
    SzView *body = sz_view_clip(child);
    body->a11y_role = SZ_A11Y_REGION;
    body->a11y_label = sz_strdup(view->text);
    sz_view_add_child(view, body);
  }
  return view;
}

static int64_t percent_clamp(int64_t n) {
  if (n < 0)
    return 0;
  if (n > 100)
    return 100;
  return n;
}

int sz_view_progress_value(const SzView *view) {
  if (!view || (view->kind != SZ_VIEW_PROGRESS && view->kind != SZ_VIEW_CIRCULAR_PROGRESS))
    return 0;
  return (int)percent_clamp(view->sig_int ? sz_signal_int_get(view->sig_int) : 0);
}

static SzRect control_layout_frame(const SzView *view) {
  if (view && view->kind == SZ_VIEW_SELECT_OPTION && view->parent && view->parent->parent) {
    SzRect visible;
    return rects_intersect(view->frame, view->parent->parent->frame, &visible) ? visible : (SzRect){0};
  }
  SzRect frame = view ? view->frame : (SzRect){0};
  if (!view) return frame;
  if (view->inline_action && view->action_count) return view->action_boxes[0];
  if (view->kind == SZ_VIEW_EXPANSION_TILE) {
    if (frame.h > view->layout_theme.control_h) frame.h = view->layout_theme.control_h;
  } else if (view->kind == SZ_VIEW_SPLIT) {
    float width = scale_px(&view->layout_theme, 6.f);
    if (width > frame.w) width = frame.w;
    frame.x += (frame.w - width) * sz_view_split_frac(view) / 100.f;
    frame.w = width;
  }
  return frame;
}

SzRect sz_view_control_frame(const SzView *view) {
  return display_rect(view, control_layout_frame(view));
}

static int split_set_fraction(SzView *view, int64_t value) {
  if (!view->sig_int) return 0;
  int previous = sz_view_split_frac(view);
  int next = (int)percent_clamp(value);
  if (previous > 0) view->split_restore = previous;
  sz_signal_int_set(view->sig_int, next);
  return 1;
}

int sz_view_split_set_at(SzView *view, float x) {
  if (!view || view->kind != SZ_VIEW_SPLIT || !view->sig_int || !isfinite(x)) return 0;
  float y = 0;
  if (!layout_point(view, &x, &y)) return 0;
  SzRect handle = control_layout_frame(view);
  double width = (double)view->frame.w - handle.w;
  double distance = (double)x - view->frame.x - handle.w / 2;
  int64_t value = 0;
  if (width > 0 && isfinite(distance) && isfinite(width)) {
    double fraction = distance / width;
    value = fraction <= 0 ? 0 : fraction >= 1 ? 100 : (int64_t)(fraction * 100 + .5);
  }
  return split_set_fraction(view, value);
}

int sz_view_slider_set_at(SzView *view, float x) {
  if (!view || view->kind != SZ_VIEW_SLIDER || !view->sig_int || !isfinite(x) ||
      !isfinite(view->frame.x) || !isfinite(view->frame.w)) return 0;
  float y = 0;
  if (!layout_point(view, &x, &y)) return 0;
  int64_t value = view->range_minimum;
  double distance = (double)x - view->frame.x;
  double width = view->frame.w;
  if (width > 0 && distance > 0) {
    if (distance >= width) value = view->range_maximum;
    else {
      uint64_t count = (uint64_t)(((__int128)view->range_maximum - view->range_minimum) / view->range_step);
      int distance_exp, width_exp;
      double distance_mantissa = frexp(distance, &distance_exp);
      double width_mantissa = frexp(width, &width_exp);
      int shift = width_exp - distance_exp;
      /* Double mantissas use 53 bits. A shift above 65 selects the first step. */
      if (shift <= 65) {
        unsigned __int128 numerator = (unsigned __int128)(uint64_t)ldexp(distance_mantissa, 53) * count;
        unsigned __int128 denominator = (unsigned __int128)(uint64_t)ldexp(width_mantissa, 53) << shift;
        uint64_t index = (uint64_t)((numerator + denominator / 2) / denominator);
        value = (int64_t)((__int128)view->range_minimum + (__int128)index * view->range_step);
      }
    }
  }
  sz_signal_int_set(view->sig_int, value);
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

void sz_view_format_hit_id(const SzView *hit, char *buf, size_t cap) {
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
  case SZ_A11Y_ICON:
    role = "icon";
    break;
  case SZ_A11Y_LINK:
    role = "link";
    break;
  case SZ_A11Y_BREADCRUMB:
    role = "breadcrumb";
    break;
  case SZ_A11Y_NAV_TILE:
    role = "navtile";
    break;
  case SZ_A11Y_COMBOBOX: role = "combobox"; break;
  case SZ_A11Y_LISTBOX: role = "listbox"; break;
  case SZ_A11Y_OPTION: role = "option"; break;
  case SZ_A11Y_TAB:
    role = "tab";
    break;
  case SZ_A11Y_TAB_LIST:
    role = "tablist";
    break;
  case SZ_A11Y_TAB_PANEL:
    role = "tabpanel";
    break;
  case SZ_A11Y_SPLIT:
    role = "split";
    break;
  case SZ_A11Y_APP_BAR:
    role = "appbar";
    break;
  default:
    break;
  }
  snprintf(buf, cap, "%s:%s", role, sz_view_a11y_label(hit));
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

static const char *edit_preedit(const SzView *view) {
  if (!view || !view->preedit)
    return "";
  return view->preedit;
}

const char *sz_view_text_field_preedit(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_TEXT_FIELD)
    return "";
  return edit_preedit(view);
}

const char *sz_view_editor_preedit(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return "";
  return edit_preedit(view);
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
  case SZ_A11Y_HEADING:
    return "heading";
  case SZ_A11Y_COMBOBOX: return "combobox";
  case SZ_A11Y_LISTBOX: return "listbox";
  case SZ_A11Y_OPTION: return "option";
  case SZ_A11Y_TAB:
    return "tab";
  case SZ_A11Y_TAB_LIST:
    return "tablist";
  case SZ_A11Y_TAB_PANEL:
    return "tabpanel";
  case SZ_A11Y_APP_BAR:
    return "appbar";
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
  case SZ_A11Y_REGION: return "region";
  case SZ_A11Y_RADIO_GROUP:
    return "radiogroup";
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
  case SZ_A11Y_ICON:
    return "icon";
  case SZ_A11Y_LINK:
    return "link";
  case SZ_A11Y_BREADCRUMB:
    return "breadcrumb";
  case SZ_A11Y_NAV_TILE:
    return "navtile";
  default:
    return "none";
  }
}

SzString *sz_view_show(const SzView *view) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  SzString *out;
  if (!view || sz_rc_kind((void *)view) != SZ_RC_VIEW)
    return sz_string_from_cstr("view");
  sz_dump_append(&buf, &len, &cap, a11y_role_name(view->a11y_role));
  sz_dump_append(&buf, &len, &cap, ":");
  if (view->a11y_label)
    sz_dump_append(&buf, &len, &cap, view->a11y_label);
  out = sz_string_from_cstr(buf ? buf : "");
  sz_free(buf);
  return out;
}

/* Append one a11y dump line: `role:label` plus `=N` when has_num. Labels
 * come from app text, so no fixed buffer: grow with sz_dump_append. */
static void a11y_dump_line(char **buf, size_t *len, size_t *cap,
                           SzA11yRole role, const char *label, int has_num,
                           int64_t num) {
  char tmp[24];
  sz_dump_append(buf, len, cap, a11y_role_name(role));
  sz_dump_append(buf, len, cap, ":");
  if (label)
    sz_dump_append(buf, len, cap, label);
  if (has_num) {
    snprintf(tmp, sizeof tmp, "=%lld", (long long)num);
    sz_dump_append(buf, len, cap, tmp);
  }
  sz_dump_append(buf, len, cap, "\n");
}

/* Per-node a11y info shared by the text dump and the JSON tree. `label` is
 * the text-line label; it borrows from the view unless `live` holds it.
 * cls: 0 default (label only), 1 toggle (`num` is the on state), 2 numeric
 * (`num` is the value; `label` keeps the name when available). */
typedef struct SzA11yInfo {
  const char *label;
  char *live;
  int cls;
  int64_t num;
} SzA11yInfo;

static SzA11yInfo a11y_node_info(SzView *v) {
  SzA11yInfo info;
  char tmp[24];
  info.label = v->a11y_label ? v->a11y_label : "";
  info.live = NULL;
  info.cls = 0;
  info.num = 0;
  if (v->kind == SZ_VIEW_TEXT && (v->sig_int || v->sig_str)) {
    size_t live_len = 0, live_cap = 0;
    if (v->sig_int) {
      sz_dump_append(&info.live, &live_len, &live_cap,
                     v->prefix ? v->prefix : "");
      snprintf(tmp, sizeof tmp, "%lld",
               (long long)sz_signal_int_get(v->sig_int));
      sz_dump_append(&info.live, &live_len, &live_cap, tmp);
    } else {
      sz_dump_append(&info.live, &live_len, &live_cap,
                     sz_signal_str_get(v->sig_str));
    }
    info.label = info.live;
  } else if (v->kind == SZ_VIEW_SELECT) {
    info.cls = 2;
    info.num = sz_view_select_value(v);
  } else if (v->kind == SZ_VIEW_SELECT_OPTION) {
    SzView *owner = select_owner(v);
    info.cls = 1;
    info.num = owner && v->radio_value == owner->select_active;
  } else if (v->kind == SZ_VIEW_CHECKBOX || v->kind == SZ_VIEW_SWITCH ||
             v->kind == SZ_VIEW_CHIP || v->kind == SZ_VIEW_FILTER_CHIP ||
             v->kind == SZ_VIEW_INPUT_CHIP ||
             v->kind == SZ_VIEW_EXPANSION_TILE ||
             v->kind == SZ_VIEW_CHECKBOX_LIST_TILE ||
             v->kind == SZ_VIEW_SWITCH_LIST_TILE) {
    info.cls = 1;
    info.num = v->sig_int && sz_signal_int_get(v->sig_int) != 0 ? 1 : 0;
  } else if (v->kind == SZ_VIEW_RADIO ||
             v->kind == SZ_VIEW_RADIO_LIST_TILE ||
             v->kind == SZ_VIEW_CHOICE_CHIP) {
    info.cls = 1;
    info.num =
        v->sig_int && sz_signal_int_get(v->sig_int) == v->radio_value ? 1 : 0;
  } else if (v->kind == SZ_VIEW_SLIDER || v->kind == SZ_VIEW_SPLIT) {
    info.cls = 2;
    info.num = v->kind == SZ_VIEW_SLIDER ? sz_view_slider_value(v) : sz_view_split_frac(v);
  } else if (v->kind == SZ_VIEW_PROGRESS ||
             v->kind == SZ_VIEW_CIRCULAR_PROGRESS) {
    info.cls = 2;
    info.num = sz_view_progress_value(v);
  } else if (v->kind == SZ_VIEW_BADGE) {
    info.cls = 2;
    info.num = v->sig_int ? sz_signal_int_get(v->sig_int) : 0;
    snprintf(tmp, sizeof tmp, "%lld", (long long)info.num);
    info.live = sz_strdup(tmp);
    info.label = info.live;
  } else if (v->kind == SZ_VIEW_VISIBILITY) {
    info.cls = 1;
    info.num = view_visibility_on(v) ? 1 : 0;
    info.label = info.num ? "1" : "0";
  } else if (v->kind == SZ_VIEW_OFFSTAGE) {
    info.cls = 1;
    info.num = view_offstage_shown(v) ? 1 : 0;
    info.label = info.num ? "1" : "0";
  } else if (v->kind == SZ_VIEW_TOOLTIP) {
    info.cls = 1;
    info.num = v->tooltip_open;
  } else if (v->kind == SZ_VIEW_OVERLAY) {
    info.cls = 1;
    info.num = view_overlay_open(v) ? 1 : 0;
    info.label = v->a11y_label;
  }
  return info;
}

/* The label is the on/off state, not app text: the JSON tree drops it. */
static int a11y_kind_flag(SzViewKind kind) {
  return kind == SZ_VIEW_VISIBILITY ||
         kind == SZ_VIEW_OFFSTAGE || kind == SZ_VIEW_OVERLAY;
}

static int a11y_recurse(const SzView *v) {
  if (size_collapsed(v)) return 0;
  if (v->kind == SZ_VIEW_MERGE_SEMANTICS)
    return 0;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return 0;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return 0;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return 0;
  return 1;
}

static void a11y_dump_motion(SzView *v, char **buf, size_t *len, size_t *cap) {
  char label[80];
  char line[512];
  if (!v) return;
  if (v->kind == SZ_VIEW_ANIMATED_OPACITY && v->animation_ready) {
    motion_label_copy(v, label, sizeof label);
    snprintf(line, sizeof line,
      "opacity:%s=%d\ntarget:opacity:%s=%d\npumps:opacity:%s=%d\n",
      label, v->animation_value, label, v->animation_target, label,
      v->animation_duration - v->animation_step);
    sz_dump_append(buf, len, cap, line);
  }
  if (v->kind == SZ_VIEW_ANIMATED_SIZE && v->animation_ready) {
    motion_label_copy(v, label, sizeof label);
    snprintf(line, sizeof line,
      "size:%s=%d,%d\ntarget:size:%s=%d,%d\npumps:size:%s=%d\n",
      label, v->animation_value, v->animation_axis2,
      label, v->animation_target, v->animation_target_axis2,
      label, v->animation_duration - v->animation_step);
    sz_dump_append(buf, len, cap, line);
  }
  if (v->kind == SZ_VIEW_ANIMATED_CONTENT_SIZE && v->animation_ready) {
    motion_label_copy(v, label, sizeof label);
    snprintf(line, sizeof line,
      "contentSize:%s=%.3f,%.3f\ntarget:contentSize:%s=%.3f,%.3f\npumps:contentSize:%s=%d\n",
      label, (double)v->content_w, (double)v->content_h,
      label, (double)v->content_target_w, (double)v->content_target_h,
      label, v->animation_duration - v->animation_step);
    sz_dump_append(buf, len, cap, line);
  }
  if (v->kind == SZ_VIEW_ANIMATED_POSITION && v->animation_ready) {
    motion_label_copy(v, label, sizeof label);
    snprintf(line, sizeof line,
      "position:%s=%d,%d\ntarget:position:%s=%d,%d\npumps:position:%s=%d\n",
      label, v->animation_value, v->animation_axis2,
      label, v->animation_target, v->animation_target_axis2,
      label, v->animation_duration - v->animation_step);
    sz_dump_append(buf, len, cap, line);
  }
  if (v->kind == SZ_VIEW_ANIMATED_SCALE && v->animation_ready) {
    motion_label_copy(v, label, sizeof label);
    snprintf(line, sizeof line, "scale:%s=%d\ntarget:scale:%s=%d\npumps:scale:%s=%d\n",
      label, v->animation_value, label, v->animation_target,
      label, v->animation_duration - v->animation_step);
    sz_dump_append(buf, len, cap, line);
  }
  if (v->motion_left <= 0) return;
  motion_label_copy(v, label, sizeof label);
  snprintf(line, sizeof line, "motion:%s:%s=%d\n", motion_name(v->motion),
           label, v->motion_left);
  sz_dump_append(buf, len, cap, line);
}

static void a11y_dump_node(SzView *v, char **buf, size_t *len, size_t *cap) {
  int i;
  if (!v || !buf || !len || !cap || !view_is_shown(v))
    return;
  if (v->kind == SZ_VIEW_EXCLUDE_SEMANTICS)
    return;
  if (v->a11y_role != SZ_A11Y_NONE) {
    SzA11yInfo info = a11y_node_info(v);
    a11y_dump_line(buf, len, cap, v->a11y_role, info.label, info.cls == 1 || v->kind == SZ_VIEW_SLIDER || v->kind == SZ_VIEW_SPLIT ||
                   v->kind == SZ_VIEW_PROGRESS || v->kind == SZ_VIEW_CIRCULAR_PROGRESS,
                   info.num);
    sz_free(info.live);
  }
  a11y_dump_motion(v, buf, len, cap);
  if (!a11y_recurse(v))
    return;
  for (i = 0; i < v->child_count; i++)
    a11y_dump_node(v->children[i], buf, len, cap);
}

SzString *sz_view_a11y_dump(SzView *root) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  SzString *out;
  sz_dump_append(&buf, &len, &cap, "");
  a11y_dump_node(root, &buf, &len, &cap);
  out = sz_string_from_cstr(buf);
  sz_free(buf);
  return out;
}

/* --- typed session schema v=2: a11y tree (JSON) ---------------------------- */

/* 1 when the node or a hoisted descendant puts a node in the JSON forest. */
static int a11y_node_dumpable(SzView *v) {
  int i;
  if (!v || !view_is_shown(v))
    return 0;
  if (v->kind == SZ_VIEW_EXCLUDE_SEMANTICS)
    return 0;
  if (v->a11y_role != SZ_A11Y_NONE)
    return 1;
  if (v->motion_left > 0)
    return 1;
  if (!a11y_recurse(v))
    return 0;
  for (i = 0; i < v->child_count; i++) {
    if (a11y_node_dumpable(v->children[i]))
      return 1;
  }
  return 0;
}

/* Emit one node object into the current array. A node with no role hoists
 * its children, same as the text preorder. */
static void a11y_json_node(SzView *v, FILE *f, int *first) {
  SzA11yInfo info;
  int i;
  if (!a11y_node_dumpable(v))
    return;
  if (v->a11y_role == SZ_A11Y_NONE && v->motion_left <= 0) {
    for (i = 0; i < v->child_count; i++)
      a11y_json_node(v->children[i], f, first);
    return;
  }
  if (!*first)
    fputc(',', f);
  *first = 0;
  info = a11y_node_info(v);
  fputs("{\"role\":\"", f);
  if (v->a11y_role == SZ_A11Y_NONE)
    fputs("motion", f);
  else
    fputs(a11y_role_name(v->a11y_role), f);
  fputc('"', f);
  if (v->a11y_role != SZ_A11Y_NONE && info.cls == 2) {
    fprintf(f, ",\"value\":%lld", (long long)info.num);
    if (v->kind == SZ_VIEW_SELECT) {
      fputs(",\"label\":\"", f); sz_json_fputs_escaped(f, info.label);
      fputs("\",\"text\":\"", f); sz_json_fputs_escaped(f, sz_view_select_text(v));
      fprintf(f, "\",\"expanded\":%s", v->select_open ? "true" : "false");
    }
    if (v->kind == SZ_VIEW_SPLIT) {
      fputs(",\"label\":\"", f);
      sz_json_fputs_escaped(f, info.label);
      fputs("\",\"minimum\":0,\"maximum\":100,\"orientation\":\"vertical\"", f);
    }
    if (v->kind == SZ_VIEW_PROGRESS || v->kind == SZ_VIEW_CIRCULAR_PROGRESS) {
      fputs(",\"label\":\"", f);
      sz_json_fputs_escaped(f, info.label);
      fputs("\",\"minimum\":0,\"maximum\":100", f);
    }
    if (v->kind == SZ_VIEW_SLIDER) {
      fputs(",\"label\":\"", f);
      sz_json_fputs_escaped(f, info.label);
      fprintf(f, "\",\"minimum\":%lld,\"maximum\":%lld,\"step\":%lld",
        (long long)v->range_minimum, (long long)v->range_maximum, (long long)v->range_step);
    }
  } else if (v->a11y_role != SZ_A11Y_NONE) {
    if (!(info.cls == 1 && a11y_kind_flag(v->kind))) {
      fputs(",\"label\":\"", f);
      sz_json_fputs_escaped(f, info.label);
      fputc('"', f);
    }
    if (info.cls == 1)
      fprintf(f, ",\"on\":%s", info.num ? "true" : "false");
  }
  if (v->motion_left > 0) {
    char label[80];
    motion_label_copy(v, label, sizeof label);
    fputs(",\"motion\":\"", f);
    fputs(motion_name(v->motion), f);
    fputs("\",\"motionLabel\":\"", f);
    sz_json_fputs_escaped(f, label);
    fprintf(f, "\",\"step\":%d", v->motion_left);
  }
  if (a11y_recurse(v)) {
    int any = 0;
    for (i = 0; i < v->child_count; i++) {
      if (a11y_node_dumpable(v->children[i])) {
        any = 1;
        break;
      }
    }
    if (any) {
      int child_first = 1;
      fputs(",\"children\":[", f);
      for (i = 0; i < v->child_count; i++)
        a11y_json_node(v->children[i], f, &child_first);
      fputc(']', f);
    }
  }
  fputc('}', f);
  sz_free(info.live);
}

void sz_view_a11y_dump_json(SzView *root, FILE *f) {
  int first = 1;
  fputc('[', f);
  if (root)
    a11y_json_node(root, f, &first);
  fputc(']', f);
}

SzView *sz_view_column(void) { return view_new(SZ_VIEW_COLUMN); }
SzView *sz_view_row(void) { return view_new(SZ_VIEW_ROW); }
SzView *sz_view_wrap(void) { return view_new(SZ_VIEW_WRAP); }

SzView *sz_view_breadcrumb(void) {
  SzView *v = view_new(SZ_VIEW_BREADCRUMB);
  v->a11y_role = SZ_A11Y_BREADCRUMB;
  v->a11y_label = sz_strdup("Breadcrumb");
  return v;
}

SzView *sz_view_grid(int cols) {
  SzView *v = view_new(SZ_VIEW_GRID);
  v->img_w = cols < 1 ? 1 : cols;
  return v;
}
SzView *sz_view_auto_grid(int min_width) {
  SzView *v = sz_view_grid(1);
  v->grid_min_width = min_width < 1 ? 1 : min_width;
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

static int each_seen_is_sentinel(SzList *p) {
  return p == (SzList *)(uintptr_t)1;
}

static void each_seen_set(SzView *v, SzList *xs) {
  if (!v)
    return;
  if (v->each_seen && !each_seen_is_sentinel(v->each_seen))
    sz_release(v->each_seen);
  if (xs)
    sz_retain(xs);
  v->each_seen = xs;
}

static int each_ptr_in(SzList *xs, void *p) {
  SzList *q;
  for (q = xs; q; q = q->tail)
    if (q->head == p)
      return 1;
  return 0;
}

static void sync_each(SzView *v) {
  SzList *xs;
  SzList *p;
  SzList *old;
  int first;
  int grew;
  if (!v || !v->each_sig)
    return;
  xs = sz_signal_list_get(v->each_sig);
  if (xs == v->each_seen)
    return;
  first = each_seen_is_sentinel(v->each_seen);
  old = first ? NULL : v->each_seen;
  grew = !first && sz_list_len(xs) > sz_list_len(old);
  sz_view_clear_children(v);
  for (p = xs; p; p = p->tail) {
    SzString *s = (SzString *)p->head;
    SzView *row = NULL;
    if (v->each_fn) {
      row = v->each_fn(s, v->each_env);
      if (row)
        sz_view_add_child(v, row);
    } else if (sz_signal_list_elem_str(v->each_sig)) {
      /* Item text has no length cap. Build "- <text>" without truncation. */
      char *line = NULL;
      size_t line_len = 0, line_cap = 0;
      sz_dump_append(&line, &line_len, &line_cap, "- ");
      if (s)
        sz_dump_append(&line, &line_len, &line_cap, sz_string_cstr(s));
      row = sz_view_text(line);
      sz_free(line);
      sz_view_add_child(v, row);
    } else {
      row = sz_view_text("- <item>");
      sz_view_add_child(v, row);
    }
    /* The first sync is the static tree. A longer list marks new heads. */
    if (row && grew && !each_ptr_in(old, p->head))
      motion_start(row, SZ_MOTION_INSERT);
  }
  each_seen_set(v, xs);
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

SzView *sz_view_adaptive(int64_t min_width, SzView *compact, SzView *wide) {
  if (!compact || !wide || compact == wide)
    sz_panic("View.adaptive requires two distinct Views");
  SzView *view = view_new(SZ_VIEW_ADAPTIVE);
  view->adaptive_min_width = min_width < 1 ? 1 : min_width > INT_MAX ? INT_MAX : (int)min_width;
  view->adaptive_selected = 1;
  sz_view_add_child(view, compact);
  sz_view_add_child(view, wide);
  return view;
}

SzView *sz_view_virtual_list(int64_t row_height, SzSignalList *items,
                             SzViewKeyFn key, void *key_env,
                             SzViewRowFn render, void *render_env) {
  if (!items || !key || !render)
    sz_panic("View.virtualList requires items, keys, and a renderer");
  SzView *content = view_new(SZ_VIEW_VIRTUAL_LIST);
  content->virtual_height = row_height < 1 ? 1 : row_height > INT_MAX ? INT_MAX : (int)row_height;
  content->each_sig = items;
  content->each_seen = (SzList *)(uintptr_t)1;
  content->virtual_key = key;
  content->virtual_key_env = key_env;
  content->virtual_render = render;
  content->each_env = render_env;
  sz_retain(key_env);
  sz_retain(render_env);
  SzView *scroll = sz_view_scroll(content);
  scroll->pref_h = 0.f;
  return scroll;
}

SzView *sz_view_measured_list(int64_t estimate, SzSignalList *items,
                              SzViewKeyFn key, void *key_env,
                              SzViewRowFn render, void *render_env) {
  SzView *scroll = sz_view_virtual_list(estimate, items, key, key_env, render, render_env);
  scroll->scroll_child->virtual_measured = 1;
  return scroll;
}

SzView *sz_view_expanded(int64_t weight, SzView *child) {
  SzView *v = view_new(SZ_VIEW_EXPANDED);
  v->flex_weight = weight < 1 ? 1 : weight;
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

SzView *sz_view_scale(int64_t percent, SzView *child) {
  SzView *view = view_new(SZ_VIEW_SCALE);
  view->img_w = percent < 0 ? 0 : percent > INT_MAX ? INT_MAX : (int)percent;
  if (child) sz_view_add_child(view, child);
  return view;
}

SzView *sz_view_animated_scale(SzSignalInt *value, int64_t pumps, SzView *child) {
  if (!value || !child || pumps < 1 || pumps > INT_MAX)
    sz_panic("Animated scale needs a Signal, a child, and positive pump count within Int32");
  SzView *view = view_new(SZ_VIEW_ANIMATED_SCALE);
  view->sig_int = value;
  view->animation_duration = (int)pumps;
  sz_view_add_child(view, child);
  return view;
}

SzView *sz_view_animated_opacity(SzSignalInt *value, int64_t pumps, SzView *child) {
  if (!value || !child || pumps < 1 || pumps > INT_MAX)
    sz_panic("Animated opacity needs a Signal, a child, and positive pump count within Int32");
  SzView *view = view_new(SZ_VIEW_ANIMATED_OPACITY);
  view->sig_int = value;
  view->animation_duration = (int)pumps;
  sz_view_add_child(view, child);
  return view;
}

SzView *sz_view_animated_size(SzSignalInt *width, SzSignalInt *height, int64_t pumps, SzView *child) {
  if (!width || !height || !child || pumps < 1 || pumps > INT_MAX)
    sz_panic("Animated size needs two Signals, a child, and positive pump count within Int32");
  SzView *view = view_new(SZ_VIEW_ANIMATED_SIZE);
  view->sig_int = width;
  view->animation_axis2_signal = height;
  view->animation_duration = (int)pumps;
  sz_view_add_child(view, child);
  return view;
}

SzView *sz_view_animated_content_size(int64_t pumps, SzView *child) {
  if (!child || pumps < 1 || pumps > INT_MAX)
    sz_panic("Animated content size needs a child and positive pump count within Int32");
  SzView *view = view_new(SZ_VIEW_ANIMATED_CONTENT_SIZE);
  view->animation_duration = (int)pumps;
  sz_view_add_child(view, child);
  return view;
}

SzView *sz_view_animated_position(SzSignalInt *x, SzSignalInt *y, int64_t pumps, SzView *child) {
  if (!x || !y || !child || pumps < 1 || pumps > INT_MAX)
    sz_panic("Animated position needs two Signals, a child, and positive pump count within Int32");
  SzView *view = view_new(SZ_VIEW_ANIMATED_POSITION);
  view->sig_int = x;
  view->animation_axis2_signal = y;
  view->animation_duration = (int)pumps;
  sz_view_add_child(view, child);
  return view;
}

static int transition_axis(int64_t value) {
  return value < 0 ? 0 : value > INT_MAX ? INT_MAX : (int)value;
}

static int size_collapsed(const SzView *view) {
  if (!view) return 0;
  if (view->kind == SZ_VIEW_SCALE) return sz_view_paint_scale(view) == 0;
  if (view->kind == SZ_VIEW_ANIMATED_SCALE) return view->animation_ready && sz_view_paint_scale(view) == 0;
  return (view->kind == SZ_VIEW_ANIMATED_SIZE || view->kind == SZ_VIEW_ANIMATED_CONTENT_SIZE) && view->animation_ready &&
    (view->frame.w <= 0 || view->frame.h <= 0);
}

static void sync_scalar(SzView *view, int suppressed) {
  int64_t source = sz_signal_int_get(view->sig_int);
  int maximum = view->kind == SZ_VIEW_ANIMATED_OPACITY ? 100 : INT_MAX;
  int target = source < 0 ? 0 : source > maximum ? maximum : (int)source;
  if (!view->animation_ready || suppressed) {
    view->animation_ready = 1;
    view->animation_value = view->animation_from = view->animation_target = target;
    view->animation_step = view->animation_duration;
  } else if (target != view->animation_target) {
    view->animation_from = view->animation_value;
    view->animation_target = target;
    view->animation_step = view->animation_value == target ? view->animation_duration : 0;
  }
}

static void sync_axes(SzView *view, int suppressed) {
  int first = transition_axis(sz_signal_int_get(view->sig_int));
  int second = transition_axis(sz_signal_int_get(view->animation_axis2_signal));
  if (!view->animation_ready || suppressed) {
    view->animation_ready = 1;
    view->animation_value = view->animation_from = view->animation_target = first;
    view->animation_axis2 = view->animation_from_axis2 = view->animation_target_axis2 = second;
    view->animation_step = view->animation_duration;
  } else if (first != view->animation_target || second != view->animation_target_axis2) {
    view->animation_from = view->animation_value;
    view->animation_from_axis2 = view->animation_axis2;
    view->animation_target = first;
    view->animation_target_axis2 = second;
    view->animation_step = view->animation_value == first && view->animation_axis2 == second ? view->animation_duration : 0;
  }
}

static int transition_suppressed(const SzView *view) {
  for (const SzView *p = view; p; p = p->parent) {
    if (!view_is_shown(p) || p->motion_reduced ||
        (p != view && size_collapsed(p)) ||
        (p->kind == SZ_VIEW_REDUCED_MOTION && sz_signal_int_get(p->sig_int)) ||
        (p->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(p)) ||
        (p->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(p)) ||
        (p->kind == SZ_VIEW_OVERLAY && !view_overlay_open(p))) return 1;
  }
  return 0;
}

static void sync_content_size(SzView *view, float width, float height) {
  if (!view->animation_ready || transition_suppressed(view)) {
    view->animation_ready = 1;
    view->content_w = view->content_from_w = view->content_target_w = width;
    view->content_h = view->content_from_h = view->content_target_h = height;
    view->animation_step = view->animation_duration;
  } else if (width != view->content_target_w || height != view->content_target_h) {
    view->content_from_w = view->content_w;
    view->content_from_h = view->content_h;
    view->content_target_w = width;
    view->content_target_h = height;
    view->animation_step = view->content_w == width && view->content_h == height ? view->animation_duration : 0;
  }
}

SzView *sz_view_reduced_motion(SzSignalInt *reduced, SzView *child) {
  if (!reduced || !child) sz_panic("Reduced motion needs a Signal and a child");
  SzView *view = view_new(SZ_VIEW_REDUCED_MOTION);
  view->sig_int = reduced;
  sz_view_add_child(view, child);
  return view;
}

int sz_view_animated_opacity_value(const SzView *view) {
  return view && view->kind == SZ_VIEW_ANIMATED_OPACITY ? view->animation_value : 100;
}

static void sync_transitions(SzView *view, int shown, int reduced);

void sz_view_reduce_motion(SzView *root, int reduced) {
  if (!root) return;
  root->motion_reduced = reduced != 0;
  sync_transitions(root, 1, root->motion_reduced);
}

void sz_view_pause_motion(SzView *root, int paused) {
  if (root) root->motion_paused = paused != 0;
}

static void sync_transitions(SzView *view, int shown, int reduced) {
  if (!view) return;
  shown = shown && view_is_shown(view) &&
    !(view->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(view)) &&
    !(view->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(view)) &&
    !(view->kind == SZ_VIEW_OVERLAY && !view_overlay_open(view));
  if (view->kind == SZ_VIEW_REDUCED_MOTION && sz_signal_int_get(view->sig_int)) reduced = 1;
  view->motion_suppressed = reduced || !shown;
  if (view->motion_suppressed) {
    view->motion = SZ_MOTION_NONE; view->motion_left = 0; view->motion_painted = 0;
  }
  if (view->kind == SZ_VIEW_ANIMATED_OPACITY || view->kind == SZ_VIEW_ANIMATED_SCALE)
    sync_scalar(view, view->motion_suppressed);
  if (transition_has_axes(view)) sync_axes(view, view->motion_suppressed);
  if (view->kind == SZ_VIEW_ANIMATED_CONTENT_SIZE && view->motion_suppressed) {
    view->content_w = view->content_target_w;
    view->content_h = view->content_target_h;
    view->animation_step = view->animation_duration;
    if (!shown) view->animation_ready = 0;
  }
  for (int i = 0; i < view->child_count; i++)
    sync_transitions(view->children[i], shown && !size_collapsed(view), reduced);
}

static int animation_lerp(int from, int target, int64_t step, int64_t duration) {
  return (int)(((duration - step) * from + step * target + duration / 2) / duration);
}

static int step_transitions(SzView *view) {
  if (!view) return 0;
  int resized = 0;
  if (view_has_transition(view) &&
      view->animation_ready && view->animation_step < view->animation_duration) {
    int64_t step = ++view->animation_step, duration = view->animation_duration;
    if (view->kind == SZ_VIEW_ANIMATED_CONTENT_SIZE) {
      view->content_w = (float)(((double)(duration - step) * view->content_from_w + (double)step * view->content_target_w) / duration);
      view->content_h = (float)(((double)(duration - step) * view->content_from_h + (double)step * view->content_target_h) / duration);
      resized = 1;
    } else view->animation_value = animation_lerp(view->animation_from, view->animation_target, step, duration);
    if (view->kind == SZ_VIEW_ANIMATED_SCALE) resized = 1;
    if (transition_has_axes(view)) {
      view->animation_axis2 = animation_lerp(view->animation_from_axis2, view->animation_target_axis2, step, duration);
      resized = 1;
    }
  }
  for (int i = 0; i < view->child_count; i++) resized |= step_transitions(view->children[i]);
  return resized;
}

int sz_view_paint_opacity(const SzView *view) {
  int opacity = 100;
  for (const SzView *p = view; p; p = p->parent) {
    if (p->kind == SZ_VIEW_OPACITY) opacity = opacity * p->img_w / 100;
    if (p->kind == SZ_VIEW_ANIMATED_OPACITY) opacity = opacity * p->animation_value / 100;
    opacity = opacity * motion_appear_pct(p) / 100;
  }
  return opacity;
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

SzView *sz_view_theme(const SzTheme *theme, SzView *child) {
  SzView *v = view_new(SZ_VIEW_THEME);
  v->palette = theme ? *theme : *sz_theme_default();
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

static void image_bitmap_drop(void *ptr) { sk_image_unref((SkImage *)ptr); }

SzView *sz_view_image(SzImage *image, const char *label) {
  if (!image)
    sz_panic("View.image requires a decoded image");
  SzView *v = view_new(SZ_VIEW_IMAGE);
  sz_retain(image);
  v->image = image;
  atomic_store(&image->drop_bitmap, image_bitmap_drop);
  if (!atomic_load(&image->bitmap)) {
    SkImage *bitmap = sk_image_make_rgba(image->pixels, image->width, image->height);
    if (!bitmap)
      sz_panic("Image paint allocation fails");
    void *expected = NULL;
    if (!atomic_compare_exchange_strong(&image->bitmap, &expected, bitmap))
      sk_image_unref(bitmap);
  }
  v->a11y_role = SZ_A11Y_IMAGE;
  v->a11y_label = sz_strdup(label && label[0] ? label : "image");
  return v;
}

SzView *sz_view_icon(char glyph, uint32_t argb) {
  SzView *v = view_new(SZ_VIEW_ICON);
  char label[2];
  v->glyph = glyph ? glyph : '*';
  v->fg_argb = argb ? argb : 0xFF1A1A1Au;
  v->a11y_role = SZ_A11Y_ICON;
  label[0] = v->glyph;
  label[1] = '\0';
  v->a11y_label = sz_strdup(label);
  return v;
}

SzView *sz_view_link(const char *label, const char *route) {
  SzView *v = view_new(SZ_VIEW_LINK);
  v->text = sz_strdup(label ? label : "");
  v->route_id = sz_strdup(route ? route : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_LINK;
  v->a11y_label = sz_strdup(label ? label : "");
  return v;
}

SzView *sz_view_nav_tile(char glyph, const char *title, const char *route) {
  SzView *v = view_new(SZ_VIEW_NAV_TILE);
  v->glyph = glyph ? glyph : '*';
  v->text = sz_strdup(title ? title : "");
  v->route_id = sz_strdup(route ? route : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_NAV_TILE;
  v->a11y_label = sz_strdup(title ? title : "");
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

static void view_drop(void *ptr) {
  SzView *view = (SzView *)ptr;
  int i, n;
  SzView **kids;
  if (!view)
    return;
  for (int j = 0; j < view->select_count; j++) sz_free(view->select_labels[j]);
  sz_free(view->select_labels);
  n = view->child_count;
  kids = view->children;
  view->child_count = 0;
  view->children = NULL;
  view->scroll_child = NULL;
  for (i = 0; i < n; i++) {
    SzView *c = kids[i];
    kids[i] = NULL;
    if (!c)
      continue;
    c->parent = NULL;
    sz_release(c);
  }
  sz_free(kids);
  for (i = 0; i < view->virtual_row_count; i++) {
    SzVirtualRow *row = &view->virtual_rows[i];
    sz_signal_scope_free(row->scope);
  }
  sz_free(view->virtual_rows);
  sz_free(view->virtual_items);
  sz_free(view->virtual_index);
  sz_free(view->virtual_heights);
  sz_release(view->virtual_key_env);
  sz_release(view->image);
  sz_release(view->styled);
  sz_free(view->text);
  sz_free(view->prefix);
  sz_free(view->placeholder);
  sz_free(view->route_id);
  sz_free(view->action_boxes);
  sz_free(view->copy_text);
  sz_free(view->a11y_label);
  sz_free(view->preedit);
  /* Signals are owned by the demo/session, not the view. */
  sz_release(view->tap_env);
  view->tap_env = NULL;
  sz_release(view->each_env);
  view->each_env = NULL;
  if (view->each_seen && view->each_seen != (SzList *)(uintptr_t)1)
    sz_release(view->each_seen);
  view->each_seen = NULL;
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
    sz_free(view->tok_line);
    sz_free(view->tok_col);
    sz_free(view->tok_len);
    sz_free(view->tok_ty);
    {
      int k;
      for (k = 0; k < view->inlay_n; k++)
        sz_free(view->inlay_label[k]);
    }
    sz_free(view->inlay_line);
    sz_free(view->inlay_col);
    sz_free(view->inlay_label);
    sz_free(view->fold_start);
    sz_free(view->fold_end);
    sz_free(view->fold_closed);
  }
}

void sz_view_free(SzView *view) { sz_release(view); }

void sz_view_clear_children(SzView *parent) {
  int i, n;
  if (!parent)
    return;
  if (!view_accepts_children(parent))
    sz_panic("sz_view_clear_children: parent cannot have children");
  n = parent->child_count;
  parent->child_count = 0;
  parent->scroll_child = NULL;
  for (i = 0; i < n; i++) {
    SzView *c = parent->children[i];
    parent->children[i] = NULL;
    if (!c)
      continue;
    c->parent = NULL;
    sz_release(c);
  }
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
  if (!line || cap < 4) {
    if (line) line[0] = '\0';
    return;
  }
  size_t length = strlen(line);
  uint8_t *breaks = sz_alloc(length + 1);
  sk_text_cluster_breaks(line, length, breaks);
  char *out = sz_alloc(length + 4);
  memcpy(out, line, length);
  size_t keep = length < cap - 4 ? length : cap - 4;
  while (keep && !breaks[keep]) keep--;
  for (;;) {
    memcpy(out + keep, "...", 4);
    if (text_width(out, font_px) <= max_w) {
      memcpy(line, out, keep + 4);
      break;
    }
    if (!keep) { line[0] = '\0'; break; }
    do { keep--; } while (keep && !breaks[keep]);
  }
  sz_free(out);
  sz_free(breaks);
}

static float span_width(const char *s, int start, int end, float font_px) {
  return s && end > start ? sk_font_measure_span(sk_text_slice(s, strlen(s), (size_t)start, (size_t)end), font_px, 0) : 0.f;
}

static float *text_positions(const char *text, int length, float font) {
  float *positions = sz_alloc(((size_t)length + 1) * 2 * sizeof(float));
  sk_font_text_positions(sk_text_range(text, (size_t)length), font, 0, positions, positions + length + 1);
  return positions;
}

static int field_caret_after_zero_width(const char *source, int length, int caret, const float *positions) {
  while (caret < length) {
    int next = caret + utf8_clen(source, caret);
    if (next > length || positions[caret] != positions[next] ||
        positions[caret] != positions[length + 1 + caret]) break;
    caret = next;
  }
  return caret;
}

static char *field_display(const SzView *view, int *caret, int *pre_start, int *pre_end) {
  const char *source = field_cstr(view), *preedit = edit_preedit(view);
  int length = (int)strlen(source), extra = (int)strlen(preedit);
  int a = field_caret_clamped(view), b = a;
  if (extra && field_has_sel(view)) field_sel_bounds(view, &a, &b);
  char *display = sz_alloc((size_t)length - (size_t)(b - a) + (size_t)extra + 1);
  memcpy(display, source, (size_t)a);
  memcpy(display + a, preedit, (size_t)extra);
  memcpy(display + a + extra, source + b, (size_t)(length - b + 1));
  *caret = a + extra;
  *pre_start = a;
  *pre_end = a + extra;
  return display;
}

static int editor_cols(const char *s, int start, int end);

/* Unbounded span measure. Editor paint/caret must not use the 256-byte cap.
 * Uses the same code point walk as editor_cols. Caret and paint must agree. */
static float editor_span_width(const char *s, int start, int end, float font_px) {
  float cell;
  if (!s || end <= start)
    return 0.f;
  cell = sk_font_mono_cell(font_px);
  return (float)editor_cols(s, start, end) * cell;
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

static float scale_px(const SzTheme *theme, float v);

static float editor_gutter_w(const SzView *v, const SzTheme *theme) {
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
  cell = sk_font_mono_cell(theme->font_px);
  return (float)digits * cell + scale_px(theme, 4.f);
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
  const SzTheme *theme = view ? &view->layout_theme : sz_theme_default();
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0.f;
  return editor_gutter_w(view, theme);
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

static void editor_clear_tokens(SzView *view) {
  if (!view)
    return;
  sz_free(view->tok_line);
  sz_free(view->tok_col);
  sz_free(view->tok_len);
  sz_free(view->tok_ty);
  view->tok_line = NULL;
  view->tok_col = NULL;
  view->tok_len = NULL;
  view->tok_ty = NULL;
  view->tok_n = 0;
}

int sz_view_editor_set_tokens(SzView *view, const int *data, int n) {
  int i;
  int line = 0;
  int col = 0;
  int ntok;
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  editor_clear_tokens(view);
  if (n <= 0 || !data)
    return 1;
  ntok = n / 5;
  if (ntok <= 0)
    return 1;
  view->tok_line = (int *)sz_alloc(sizeof(int) * (size_t)ntok);
  view->tok_col = (int *)sz_alloc(sizeof(int) * (size_t)ntok);
  view->tok_len = (int *)sz_alloc(sizeof(int) * (size_t)ntok);
  view->tok_ty = (int *)sz_alloc(sizeof(int) * (size_t)ntok);
  for (i = 0; i < ntok; i++) {
    int dline = data[i * 5];
    int dcol = data[i * 5 + 1];
    line += dline;
    if (dline == 0)
      col += dcol;
    else
      col = dcol;
    if (line < 0)
      line = 0;
    if (col < 0)
      col = 0;
    view->tok_line[i] = line;
    view->tok_col[i] = col;
    view->tok_len[i] = data[i * 5 + 2];
    view->tok_ty[i] = data[i * 5 + 3];
  }
  view->tok_n = ntok;
  return 1;
}

int sz_view_editor_token_count(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  return view->tok_n;
}

static void editor_clear_inlays(SzView *view) {
  int i;
  if (!view)
    return;
  for (i = 0; i < view->inlay_n; i++)
    sz_free(view->inlay_label[i]);
  sz_free(view->inlay_line);
  sz_free(view->inlay_col);
  sz_free(view->inlay_label);
  view->inlay_line = NULL;
  view->inlay_col = NULL;
  view->inlay_label = NULL;
  view->inlay_n = 0;
}

int sz_view_editor_set_inlays(SzView *view, const int *lines, const int *cols,
                             const char *const *labels, int n) {
  int i;
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  editor_clear_inlays(view);
  if (n <= 0 || !lines || !cols || !labels)
    return 1;
  view->inlay_line = (int *)sz_alloc(sizeof(int) * (size_t)n);
  view->inlay_col = (int *)sz_alloc(sizeof(int) * (size_t)n);
  view->inlay_label = (char **)sz_alloc(sizeof(char *) * (size_t)n);
  for (i = 0; i < n; i++) {
    view->inlay_line[i] = lines[i];
    view->inlay_col[i] = cols[i];
    view->inlay_label[i] = sz_strdup(labels[i] ? labels[i] : "");
  }
  view->inlay_n = n;
  return 1;
}

int sz_view_editor_inlay_count(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  return view->inlay_n;
}

static void editor_clear_folds(SzView *view) {
  if (!view)
    return;
  sz_free(view->fold_start);
  sz_free(view->fold_end);
  sz_free(view->fold_closed);
  view->fold_start = NULL;
  view->fold_end = NULL;
  view->fold_closed = NULL;
  view->fold_n = 0;
}

int sz_view_editor_set_folds(SzView *view, const int *starts, const int *ends,
                            int n) {
  int i;
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  editor_clear_folds(view);
  if (n <= 0 || !starts || !ends)
    return 1;
  view->fold_start = (int *)sz_alloc(sizeof(int) * (size_t)n);
  view->fold_end = (int *)sz_alloc(sizeof(int) * (size_t)n);
  view->fold_closed = (int *)sz_alloc_zero(sizeof(int) * (size_t)n);
  for (i = 0; i < n; i++) {
    int a = starts[i];
    int b = ends[i];
    if (a < 0)
      a = 0;
    if (b < a)
      b = a;
    view->fold_start[i] = a;
    view->fold_end[i] = b;
    view->fold_closed[i] = 0;
  }
  view->fold_n = n;
  return 1;
}

int sz_view_editor_fold_count(const SzView *view) {
  if (!view || view->kind != SZ_VIEW_EDITOR)
    return 0;
  return view->fold_n;
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

static int editor_fold_at(const SzView *v, int line0) {
  int i;
  if (!v)
    return -1;
  for (i = 0; i < v->fold_n; i++) {
    if (v->fold_start[i] == line0)
      return i;
  }
  return -1;
}

static int editor_line_hidden(const SzView *v, int line0) {
  int i;
  if (!v)
    return 0;
  for (i = 0; i < v->fold_n; i++) {
    if (v->fold_closed[i] && v->fold_start[i] < line0 && line0 <= v->fold_end[i])
      return 1;
  }
  return 0;
}

static int editor_vis_row(const SzView *v, int line0) {
  int i;
  int vis = 0;
  if (!v)
    return line0;
  for (i = 0; i < line0; i++) {
    if (!editor_line_hidden(v, i))
      vis++;
  }
  return vis;
}

static int editor_line_at_vis(const SzView *v, int want_vis) {
  int line = 0;
  int vis = 0;
  int nlines;
  if (!v)
    return want_vis;
  nlines = editor_line_count(field_cstr(v));
  if (want_vis < 0)
    want_vis = 0;
  while (line < nlines) {
    if (!editor_line_hidden(v, line)) {
      if (vis == want_vis)
        return line;
      vis++;
    }
    line++;
  }
  return nlines > 0 ? nlines - 1 : 0;
}

static int editor_is_open_br(char c) { return c == '(' || c == '[' || c == '{'; }

static int editor_is_close_br(char c) { return c == ')' || c == ']' || c == '}'; }

static char editor_br_match(char c) {
  if (c == '(')
    return ')';
  if (c == '[')
    return ']';
  if (c == '{')
    return '}';
  if (c == ')')
    return '(';
  if (c == ']')
    return '[';
  if (c == '}')
    return '{';
  return 0;
}

static int editor_bracket_pair(const char *s, int caret, int *a, int *b) {
  int n;
  int i;
  int depth;
  int pos;
  char ch;
  char other;
  if (!s || !a || !b)
    return 0;
  n = (int)strlen(s);
  if (n <= 0)
    return 0;
  pos = -1;
  if (caret < n && editor_is_open_br(s[caret]))
    pos = caret;
  else if (caret > 0 && editor_is_close_br(s[caret - 1]))
    pos = caret - 1;
  else if (caret > 0 && editor_is_open_br(s[caret - 1]))
    pos = caret - 1;
  else if (caret < n && editor_is_close_br(s[caret]))
    pos = caret;
  if (pos < 0)
    return 0;
  ch = s[pos];
  other = editor_br_match(ch);
  if (!other)
    return 0;
  if (editor_is_open_br(ch)) {
    depth = 1;
    for (i = pos + 1; i < n; i++) {
      if (s[i] == ch)
        depth++;
      else if (s[i] == other) {
        depth--;
        if (depth == 0) {
          *a = pos;
          *b = i;
          return 1;
        }
      }
    }
  } else {
    depth = 1;
    for (i = pos - 1; i >= 0; i--) {
      if (s[i] == ch)
        depth++;
      else if (s[i] == other) {
        depth--;
        if (depth == 0) {
          *a = i;
          *b = pos;
          return 1;
        }
      }
    }
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
  double column = (double)want / cell + .5;
  if (column >= end - start) return end;
  col = (int)column;
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
                           float max_inner, const uint8_t *breaks, SzTextLineFn fn, void *env) {
  int i = start;
  int line = start;
  int last_space = -1;
  if (start >= end || max_inner <= 0.f || span_width(s, start, end, font_px) <= max_inner) {
    emit_text_line(s, start, end, font_px, fn, env);
    return;
  }
  while (i < end) {
    int next = i + 1;
    while (next < end && !breaks[next]) next++;
    float w;
    if (s[i] == ' ' && breaks[i + 1])
      last_space = i;
    w = span_width(s, line, next, font_px);
    if (max_inner > 0.f && w > max_inner && next > line) {
      if (last_space >= line) {
        emit_text_line(s, line, last_space, font_px, fn, env);
        line = last_space + 1;
        while (line < end && s[line] == ' ' && breaks[line + 1])
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
  size_t length = strlen(s);
  uint8_t *breaks = sz_alloc(length + 1);
  sk_text_cluster_breaks(s, length, breaks);
  for (;;) {
    if (s[i] == '\0' || s[i] == '\n') {
      wrap_paragraph(s, para, i, font_px, max_inner, breaks, fn, env);
      if (s[i] == '\0')
        break;
      i++;
      para = i;
      continue;
    }
    i++;
  }
  sz_free(breaks);
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
  if (v->kind == SZ_VIEW_TEXT && v->sig_int) {
    snprintf(buf, buflen, "%s%lld", v->prefix ? v->prefix : "",
             (long long)sz_signal_int_get(v->sig_int));
  } else if (v->sig_str) {
    snprintf(buf, buflen, "%s", sz_signal_str_get(v->sig_str));
  } else if (v->text) {
    snprintf(buf, buflen, "%s", v->text);
  }
}

static const char *text_content(const SzView *v, char *buf, size_t size) {
  if (v->sig_str) return sz_signal_str_get(v->sig_str);
  if (!v->sig_int && v->text) return v->text;
  resolve_text(v, buf, size);
  return buf;
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
  if (flexing && view_flex_weight(ch))
    return box_tight(inner_w, flex_h);
  if (view_is_cross_stretch(ch))
    return box_tight_width(inner_w, max_h);
  return box_loose(inner_w, max_h);
}

static SzBoxConstraints row_child_box(SzView *ch, float max_w, float inner_h,
                                      float flex_w, int flexing) {
  if (flexing && view_flex_weight(ch))
    return box_tight(flex_w, inner_h);
  if (view_is_cross_stretch(ch))
    return box_tight_height(max_w, inner_h);
  return box_loose(max_w, inner_h);
}

/* Zero means unbounded. Keep a consumed finite width positive. */
static float inset_max_axis(float maximum, float inset) {
  return maximum > 0.f ? fmaxf(0.01f, maximum - inset * 2.f) : 0.f;
}

/* Zero cap has no extra limit. Use the tighter incoming limit. */
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

static const SzTheme *scope_theme(const SzView *v, const SzTheme *theme, SzTheme *scoped) {
  if (v->kind != SZ_VIEW_THEME) return theme;
  float scale = theme_px_scale(theme);
  *scoped = v->palette;
  scoped->font_px *= scale;
  scoped->pad *= scale;
  scoped->gap *= scale;
  scoped->control_h *= scale;
  scoped->px_scale = theme->px_scale;
  return scoped;
}

static float text_line_h(const SzTheme *theme, float font_px) {
  return font_px + scale_px(theme, theme->line_gap);
}

static float plain_text_metrics(const char *text, const SzTheme *theme, float font, float *baseline) {
  float ascent, descent;
  sk_font_span_metrics(sk_text_range(text, strlen(text)), font, 0, &ascent, &descent);
  *baseline = fmaxf(font + scale_px(theme, 2.f), ascent);
  return fmaxf(text_line_h(theme, font), *baseline + descent);
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

static int virtual_index_compare(const void *a, const void *b) {
  int64_t left = ((const SzVirtualIndex *)a)->key;
  int64_t right = ((const SzVirtualIndex *)b)->key;
  return (left > right) - (left < right);
}

static int virtual_find(const SzView *v, int64_t key) {
  int lo = 0, hi = v->virtual_count;
  while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    if (v->virtual_index[mid].key < key) lo = mid + 1;
    else hi = mid;
  }
  return lo < v->virtual_count && v->virtual_index[lo].key == key ?
    v->virtual_index[lo].index : -1;
}

static int virtual_has_focus(const SzView *v) {
  if (v->focused) return 1;
  for (int i = 0; i < v->child_count; i++)
    if (virtual_has_focus(v->children[i])) return 1;
  return 0;
}

static double measured_prefix(const SzView *v, int end) {
  double sum = 0.0;
  for (int i = end; i > 0; i -= i & -i) sum += v->virtual_heights[i];
  return sum;
}

static void measured_height(SzView *v, int index, double height) {
  double delta = height - v->virtual_items[index].height;
  v->virtual_items[index].height = height;
  for (int64_t i = (int64_t)index + 1; i <= v->virtual_count; i += i & -i)
    v->virtual_heights[i] += delta;
}

static void measured_index(SzView *v) {
  sz_free(v->virtual_heights);
  v->virtual_heights = sz_alloc_zero(((size_t)v->virtual_count + 1) * sizeof(double));
  for (int64_t i = 1; i <= v->virtual_count; i++) {
    v->virtual_heights[i] += v->virtual_items[i - 1].height;
    int64_t parent = (int64_t)i + (i & -(int64_t)i);
    if (parent <= v->virtual_count) v->virtual_heights[parent] += v->virtual_heights[i];
  }
}

static int measured_at(const SzView *v, double offset) {
  int index = 0;
  double sum = 0.0;
  unsigned step = 1;
  while (step <= (unsigned)v->virtual_count / 2) step *= 2;
  for (; step; step /= 2) {
    int64_t next = (int64_t)index + step;
    if (next <= v->virtual_count && sum + v->virtual_heights[next] <= offset) {
      index = (int)next;
      sum += v->virtual_heights[next];
    }
  }
  return index < v->virtual_count ? index : v->virtual_count - 1;
}

static int virtual_refresh(SzView *v) {
  SzList *items = sz_signal_list_get(v->each_sig);
  if (items == v->each_seen) return 0;
  sz_retain(items);
  int64_t length = sz_list_len(items);
  if (length > INT_MAX) sz_panic("View.virtualList has too many items");
  int count = (int)length;
  SzVirtualItem *values = sz_alloc_zero((size_t)count * sizeof(*values));
  SzVirtualIndex *index = sz_alloc_zero((size_t)count * sizeof(*index));
  int i = 0;
  for (SzList *node = items; node; node = node->tail, i++) {
    void *key = v->virtual_key(node->head, v->virtual_key_env);
    values[i].key = sz_unbox_i64(key);
    values[i].value = node->head;
    if (v->virtual_measured) {
      int previous = virtual_find(v, values[i].key);
      values[i].height = previous >= 0 && v->virtual_items[previous].value == node->head ?
        v->virtual_items[previous].height : v->virtual_height;
    }
    index[i].key = values[i].key;
    index[i].index = i;
    sz_release(key);
  }
  if (count > 1) qsort(index, (size_t)count, sizeof(*index), virtual_index_compare);
  for (i = 1; i < count; i++) {
    if (index[i - 1].key == index[i].key)
      sz_panic("View.virtualList requires unique keys");
  }
  sz_free(v->virtual_items);
  sz_free(v->virtual_index);
  v->virtual_items = values;
  v->virtual_index = index;
  v->virtual_count = count;
  if (v->virtual_measured) measured_index(v);
  each_seen_set(v, items);
  sz_release(items);
  return 1;
}

static SzVirtualRow virtual_row_take(SzView *v, const SzVirtualItem *item,
                                      SzVirtualRow *old, int old_count, SzString *hidden) {
  SzVirtualRow row = {0};
  for (int j = 0; j < old_count; j++) {
    if (old[j].view && old[j].key == item->key) {
      row = old[j];
      old[j].view = NULL;
      break;
    }
  }
  if (row.view) sz_signal_write(row.item, item->value);
  else {
    row.key = item->key;
    row.scope = sz_signal_scope_new();
    SzSignalScope *previous = sz_signal_scope_enter(row.scope);
    row.item = sz_signal_new(item->value, 4, hidden);
    SzView *body = v->virtual_render(row.item, v->each_env);
    if (!body) sz_panic("Virtual list renderer requires a View");
    row.view = sz_view_clip(body);
    sz_signal_scope_enter(previous);
  }
  return row;
}

static void measured_row(SzView *v, int index, SzVirtualRow *old, int old_count,
                          SzVirtualRow **rows, int *count, int *capacity,
                          float x, float width, const SzTheme *theme, SzString *hidden) {
  if (*count == *capacity) {
    int next = *capacity ? *capacity * 2 : 8;
    SzVirtualRow *grown = sz_alloc_zero((size_t)next * sizeof(*grown));
    if (*count) memcpy(grown, *rows, (size_t)*count * sizeof(*grown));
    sz_free(*rows);
    *rows = grown;
    *capacity = next;
  }
  SzVirtualRow *row = &(*rows)[(*count)++];
  *row = virtual_row_take(v, &v->virtual_items[index], old, old_count, hidden);
  row->index = index;
  row->view->parent = v;
  SzSignalScope *previous = sz_signal_scope_enter(row->scope);
  layout_constrained(row->view, x, 0.f, box_tight_width(width, 0.f), theme);
  sz_signal_scope_enter(previous);
  double height = row->view->frame.h / theme_px_scale(theme);
  if (!isfinite(height)) sz_panic("View.measuredList row height must be finite");
  height = fmax(1.0, height);
  row->view->frame.h = (float)height * theme_px_scale(theme);
  measured_height(v, index, height);
}

static int measured_row_compare(const void *a, const void *b) {
  int left = ((const SzVirtualRow *)a)->index;
  int right = ((const SzVirtualRow *)b)->index;
  return (left > right) - (left < right);
}

static void measured_move(SzView *v, float dy) {
  v->frame.y += dy;
  for (int i = 0; i < v->child_count; i++) measured_move(v->children[i], dy);
}

static int measured_cache_changed(SzView *v, float width, const SzTheme *theme) {
  SzTheme *old = &v->virtual_measure_theme;
  float gap = layout_gap(theme);
  int changed = v->virtual_measure_width != width || old->font_px != theme->font_px || old->line_gap != theme->line_gap ||
    old->pad != theme->pad || old->gap != theme->gap || old->control_h != theme->control_h ||
    old->px_scale != theme->px_scale || v->virtual_measure_font != g_font_px ||
    v->virtual_measure_gap != gap || v->virtual_measure_lines != g_max_lines ||
    v->virtual_measure_ellipsis != g_ellipsis;
  v->virtual_measure_width = width;
  *old = *theme;
  v->virtual_measure_font = g_font_px;
  v->virtual_measure_gap = gap;
  v->virtual_measure_lines = g_max_lines;
  v->virtual_measure_ellipsis = g_ellipsis;
  return changed;
}

static void layout_measured(SzView *v, float x, float width, const SzTheme *theme) {
  SzView *scroll = v->parent;
  float scale = theme_px_scale(theme);
  double viewport = fmax(0.0, (scroll->frame.h - theme->pad * 2.f) / scale);
  double offset = fmax(0.0, scroll->scroll_y);
  double old_total = measured_prefix(v, v->virtual_count);
  int anchor = v->virtual_count ? measured_at(v, offset) : 0;
  int64_t anchor_key = v->virtual_count ? v->virtual_items[anchor].key : 0;
  double inset = v->virtual_count ? offset - measured_prefix(v, anchor) : 0.0;
  int resized = measured_cache_changed(v, width, theme);
  if (resized && v->virtual_count) {
    for (int i = 0; i < v->virtual_count; i++) v->virtual_items[i].height = v->virtual_height;
    measured_index(v);
  }
  int changed = virtual_refresh(v);
  int follow_focus = 0;
  if (v->virtual_count) {
    int found = virtual_find(v, anchor_key);
    anchor = found >= 0 ? found : anchor < v->virtual_count ? anchor : v->virtual_count - 1;
    if (changed || resized) {
      for (int i = 0; i < v->virtual_row_count; i++) {
        if (!virtual_has_focus(v->virtual_rows[i].view)) continue;
        int focus = virtual_find(v, v->virtual_rows[i].key);
        if (focus >= 0) { anchor = focus; inset = 0.0; follow_focus = 1; }
        break;
      }
    }
    if (!changed && !resized && offset > 0.0 && offset >= old_total - viewport - 0.01) {
      anchor = v->virtual_count - 1;
      inset = INFINITY;
    }
    inset = fmax(0.0, inset);
    offset = measured_prefix(v, anchor) + inset;
  } else { anchor = 0; offset = 0.0; inset = 0.0; }
  SzVirtualRow *old = v->virtual_rows;
  int old_count = v->virtual_row_count;
  for (int i = 0; i < v->child_count; i++) {
    v->children[i]->parent = NULL;
    v->children[i] = NULL;
  }
  v->child_count = 0;
  SzVirtualRow *rows = NULL;
  int count = 0, capacity = 0;
  int first = anchor > 0 ? anchor - 1 : 0;
  int end = first;
  SzString *hidden = sz_string_from_cstr("$mirror");
  if (viewport > 0.0) {
    while (end < v->virtual_count) {
      int overscan = end > anchor && measured_prefix(v, end) >= offset + viewport;
      measured_row(v, end++, old, old_count, &rows, &count, &capacity, x, width, theme, hidden);
      if (end - 1 == anchor && follow_focus) {
        SzView *focus = sz_view_focused(rows[count - 1].view);
        if (focus) inset = fmax(0.0, (focus->frame.y + focus->frame.h) / scale - viewport);
      }
      if (end - 1 >= anchor) inset = fmin(inset, v->virtual_items[anchor].height - 1.0);
      offset = measured_prefix(v, anchor) + inset;
      if (overscan) break;
    }
    if (end == v->virtual_count) {
      for (;;) {
        offset = fmin(measured_prefix(v, anchor) + inset,
                      fmax(0.0, measured_prefix(v, v->virtual_count) - viewport));
        int start = v->virtual_count ? measured_at(v, offset) : 0;
        if (start > 0) start--;
        if (first <= start) break;
        measured_row(v, --first, old, old_count, &rows, &count, &capacity, x, width, theme, hidden);
      }
    }
  }
  sz_release(hidden);
  if (viewport <= 0.0) offset = fmin(offset, measured_prefix(v, v->virtual_count));
  if (count > 1) qsort(rows, (size_t)count, sizeof(*rows), measured_row_compare);
  scroll->scroll_y = offset;
  float y = scroll->frame.y + theme->pad;
  v->frame.y = y;
  for (int i = 0; i < count; i++) {
    SzVirtualRow *row = &rows[i];
    measured_move(row->view, y + (float)((measured_prefix(v, row->index) - offset) * scale));
    sz_view_add_child(v, row->view);
  }
  for (int i = 0; i < old_count; i++) {
    if (!old[i].view) continue;
    sz_view_free(old[i].view);
    sz_signal_scope_free(old[i].scope);
  }
  sz_free(old);
  v->virtual_rows = rows;
  v->virtual_row_count = count;
  v->frame.w = width;
  v->frame.h = (float)measured_prefix(v, v->virtual_count) * scale;
}

static void layout_virtual(SzView *v, float x, float y, float width,
                           const SzTheme *theme) {
  SzView *scroll = v->parent;
  if (!scroll || scroll->kind != SZ_VIEW_SCROLL)
    sz_panic("View.virtualList requires its scroll container");
  if (v->virtual_measured) { layout_measured(v, x, width, theme); return; }
  int changed = virtual_refresh(v);
  float height = scale_px(theme, (float)v->virtual_height);
  float viewport = fmaxf(0.f, scroll->frame.h - theme->pad * 2.f);
  double total = (double)v->virtual_count * height;
  float offset = scale_px(theme, scroll->scroll_y);
  offset = fminf(offset, fmaxf(0.f, (float)total - viewport));
  if (changed) {
    for (int i = 0; i < v->virtual_row_count; i++) {
      SzVirtualRow *row = &v->virtual_rows[i];
      int index = virtual_find(v, row->key);
      if (index < 0 || !virtual_has_focus(row->view)) continue;
      float start = (float)((double)index * height);
      if (start < offset) offset = start;
      else if (start + height > offset + viewport)
        offset = start + height - viewport;
      break;
    }
  }
  scroll->scroll_y = offset / theme_px_scale(theme);
  (void)y;
  v->frame.y = scroll->frame.y + theme->pad;
  int first = 0, end = 0;
  if (v->virtual_count > 0 && viewport > 0.f) {
    double first_slot = fmax(0.0, floor((double)offset / height) - 1.0);
    double last_slot = fmin((double)v->virtual_count,
                           ceil(((double)offset + viewport) / height) + 1.0);
    first = (int)fmin(first_slot, (double)v->virtual_count);
    end = (int)last_slot;
  }
  int count = end - first;
  SzVirtualRow *old = v->virtual_rows;
  int old_count = v->virtual_row_count;
  SzVirtualRow *rows = sz_alloc_zero((size_t)count * sizeof(*rows));
  for (int i = 0; i < v->child_count; i++) {
    v->children[i]->parent = NULL;
    v->children[i] = NULL;
  }
  v->child_count = 0;
  SzString *hidden = sz_string_from_cstr("$mirror");
  for (int i = 0; i < count; i++) {
    SzVirtualItem *item = &v->virtual_items[first + i];
    SzVirtualRow *row = &rows[i];
    *row = virtual_row_take(v, item, old, old_count, hidden);
    sz_view_add_child(v, row->view);
    SzSignalScope *previous = sz_signal_scope_enter(row->scope);
    layout_constrained(row->view, x, scroll->frame.y + theme->pad +
                       (float)((double)(first + i) * height - offset),
                       box_tight(width, height), theme);
    sz_signal_scope_enter(previous);
  }
  sz_release(hidden);
  for (int i = 0; i < old_count; i++) {
    if (!old[i].view) continue;
    sz_view_free(old[i].view);
    sz_signal_scope_free(old[i].scope);
  }
  sz_free(old);
  v->virtual_rows = rows;
  v->virtual_row_count = count;
  v->frame.w = width;
  v->frame.h = (float)total;
}

static void layout_text_action(SkTextRange context, int start, int end,
  float x, float baseline, float width, float font, float line_height,
  int style, uint32_t color, int colored, int ellipsis, int group,
  float context_x, void *env) {
  const char *source = context.source;
  (void)context; (void)context_x;
  (void)source; (void)start; (void)end; (void)line_height; (void)color; (void)colored;
  SzView *view = env;
  if (group <= 0 || group > view->child_count || ellipsis || width <= 0) return;
  SzView *action = view->children[group - 1];
  if (action->action_count == action->action_capacity) {
    if (action->action_capacity > INT_MAX / 2) sz_panic("Text action has too many fragments");
    int capacity = action->action_capacity ? action->action_capacity * 2 : 4;
    SzRect *boxes = sz_alloc((size_t)capacity * sizeof(*boxes));
    if (action->action_count) memcpy(boxes, action->action_boxes, (size_t)action->action_count * sizeof(*boxes));
    sz_free(action->action_boxes);
    action->action_boxes = boxes;
    action->action_capacity = capacity;
  }
  float ascent, descent;
  sk_font_metrics(font, style, &ascent, &descent);
  if (style & SK_TEXT_UNDERLINE) descent = fmaxf(descent, font * .1f + fmaxf(1.f, font / 16.f));
  SzRect box = {view->frame.x + scale_px(&view->layout_theme, 2.f) + x,
    view->frame.y + baseline - ascent, width, ascent + descent};
  action->action_boxes[action->action_count++] = box;
  if (action->action_count == 1) action->frame = box;
  else {
    float right = fmaxf(action->frame.x + action->frame.w, box.x + box.w);
    float bottom = fmaxf(action->frame.y + action->frame.h, box.y + box.h);
    action->frame.x = fminf(action->frame.x, box.x);
    action->frame.y = fminf(action->frame.y, box.y);
    action->frame.w = right - action->frame.x;
    action->frame.h = bottom - action->frame.y;
  }
}

static void layout_node(SzView *v, float x, float y, float max_w, float max_h,
                        const SzTheme *theme) {
  layout_constrained(v, x, y, box_loose(max_w, max_h), theme);
}

static void layout_node_ex(SzView *v, float x, float y, float min_w, float min_h,
                           float max_w, float max_h, const SzTheme *theme) {
  SzTheme scoped;
  theme = scope_theme(v, theme, &scoped);
  v->layout_theme = *theme;
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
  case SZ_VIEW_VIRTUAL_LIST:
    layout_virtual(v, x, y, max_w, theme);
    break;
  case SZ_VIEW_TEXT: {
    if (v->styled) {
      float inset = scale_px(theme, 4.f);
      float inner = max_w > 0.f ? fmaxf(.001f, max_w - inset) : 0.f;
      for (int i = 0; i < v->child_count; i++) {
        v->children[i]->action_count = 0;
        v->children[i]->frame = (SzRect){0};
        v->children[i]->layout_theme = *theme;
      }
      SzTextMetrics metrics = sz_text_layout(v->styled, layout_font_px(theme),
        theme_px_scale(theme), scale_px(theme, theme->line_gap), inner,
        text_line_cap(), g_ellipsis, layout_text_action, v);
      for (int i = 0; i < v->child_count; i++)
        if (!v->children[i]->action_count) clear_focus(v->children[i]);
      v->frame.w = metrics.truncated && g_ellipsis && max_w > 0.f ? max_w : metrics.width + inset;
      v->frame.h = metrics.height;
      break;
    }
    SzWrapMetrics m;
    float inner;
    float font_px = layout_font_px(theme);
    float line_h = text_line_h(theme, font_px);
    float inset = scale_px(theme, 4.f);
    const char *content = text_content(v, buf, sizeof buf);
    float baseline;
    line_h = plain_text_metrics(content, theme, font_px, &baseline);
    inner = 0.f;
    if (max_w > inset)
      inner = max_w - inset;
    m.max_line_w = 0.f;
    m.n = 0;
    m.cap = text_line_cap();
    m.truncated = 0;
    each_text_line(content, font_px, inner, accum_wrap_line, &m);
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
  case SZ_VIEW_SELECT:
    select_sync_options(v);
    v->frame.w = max_w > 0.f ? max_w : fmaxf(120.f * theme_px_scale(theme), text_width(sz_view_select_text(v), font) + theme->pad * 2.f + theme->control_h);
    v->frame.h = theme->control_h;
    break;
  case SZ_VIEW_BUTTON:
  case SZ_VIEW_OUTLINED_BUTTON:
  case SZ_VIEW_TEXT_BUTTON:
    resolve_text(v, buf, sizeof buf);
    v->frame.w = text_width(buf, font) + theme->pad * 2.f;
    v->frame.h = theme->control_h;
    if (v->frame.w < scale_px(theme, 48.f))
      v->frame.w = scale_px(theme, 48.f);
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  case SZ_VIEW_LINK:
    resolve_text(v, buf, sizeof buf);
    if (view_in_breadcrumb(v)) {
      float inset = scale_px(theme, 2.f);
      v->frame.w = text_width(buf, font) + inset * 2.f;
      v->frame.h = text_line_h(theme, font);
    } else {
      v->frame.w = text_width(buf, font) + theme->pad * 2.f;
      v->frame.h = theme->control_h;
      if (v->frame.w < scale_px(theme, 48.f))
        v->frame.w = scale_px(theme, 48.f);
    }
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
    v->frame.w = scale_px(theme, 8.f);
    v->frame.h = theme->control_h;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    if (max_h > 0 && v->frame.h > max_h)
      v->frame.h = max_h;
    break;
  case SZ_VIEW_CHECKBOX:
  case SZ_VIEW_RADIO: {
    float box = font + scale_px(theme, 4.f);
    float gap = layout_gap(theme);
    resolve_text(v, buf, sizeof buf);
    if (box < scale_px(theme, 12.f))
      box = scale_px(theme, 12.f);
    if (box > fmaxf(0.f, theme->control_h - scale_px(theme, 4.f)))
      box = fmaxf(0.f, theme->control_h - scale_px(theme, 4.f));
    v->frame.h = theme->control_h;
    v->frame.w = box + gap + text_width(buf, font);
    if (v->frame.w < box)
      v->frame.w = box;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  }
  case SZ_VIEW_SWITCH: {
    float box = font + scale_px(theme, 4.f);
    float gap = layout_gap(theme);
    float track;
    resolve_text(v, buf, sizeof buf);
    if (box < scale_px(theme, 12.f))
      box = scale_px(theme, 12.f);
    if (box > fmaxf(0.f, theme->control_h - scale_px(theme, 4.f)))
      box = fmaxf(0.f, theme->control_h - scale_px(theme, 4.f));
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
    float box = font + scale_px(theme, 4.f);
    float gap = layout_gap(theme);
    resolve_text(v, buf, sizeof buf);
    if (box < scale_px(theme, 12.f))
      box = scale_px(theme, 12.f);
    if (box > fmaxf(0.f, theme->control_h - scale_px(theme, 4.f)))
      box = fmaxf(0.f, theme->control_h - scale_px(theme, 4.f));
    v->frame.w = theme->pad * 2.f + box + gap + text_width(buf, font);
    v->frame.h = theme->control_h;
    if (v->frame.w < 32.f)
      v->frame.w = 32.f;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  }
  case SZ_VIEW_NAV_TILE:
    resolve_text(v, buf, sizeof buf);
    v->frame.w = max_w > 0 ? max_w : 160.f;
    v->frame.h = theme->control_h + theme->pad;
    if (v->frame.w < scale_px(theme, 96.f))
      v->frame.w = scale_px(theme, 96.f);
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
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
    v->frame.w = max_w > 0 ? max_w : 120.f;
    v->frame.h = theme->control_h;
    break;
  case SZ_VIEW_SEGMENTED: {
    float width = max_w > 0 ? max_w : scale_px(theme, 120.f) * v->child_count;
    float height = v->child_count > 0 ? theme->control_h : 0.f;
    for (int i = 0; i < v->child_count; i++) {
      float left = width * i / v->child_count;
      float right = width * (i + 1) / v->child_count;
      layout_constrained(v->children[i], x + left, y, box_tight(right - left, height), theme);
    }
    v->frame.w = width;
    v->frame.h = height;
    break;
  }
  case SZ_VIEW_SLIDER:
    v->frame.w = max_w > 0 ? max_w : 120.f;
    if (v->frame.w < scale_px(theme, 48.f))
      v->frame.w = scale_px(theme, 48.f);
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    v->frame.h = theme->control_h;
    break;
  case SZ_VIEW_PROGRESS:
    v->frame.w = max_w > 0 ? max_w : 120.f;
    if (v->frame.w < scale_px(theme, 48.f))
      v->frame.w = scale_px(theme, 48.f);
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    v->frame.h = scale_px(theme, 8.f);
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
    v->frame.h = scale_px(theme, 8.f);
    break;
  case SZ_VIEW_EXPANSION_TILE: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float hh = theme->control_h;
    float child_max_h;
    v->frame.w = max_w > 0 ? max_w : 120.f;
    v->frame.h = hh;
    if (ch && !view_is_shown(ch) && clear_focus(ch)) {
      SzView *root = v;
      while (root->parent) root = root->parent;
      sz_view_focus(root, v);
    }
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
    float h = 16.f * line_h;
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
  case SZ_VIEW_IMAGE: {
    float width = scale_px(theme, (float)sz_image_width(v->image));
    float height = scale_px(theme, (float)sz_image_height(v->image));
    float scale = 1.f;
    if (min_w > 0.f) scale = fmaxf(scale, min_w / width);
    if (min_h > 0.f) scale = fmaxf(scale, min_h / height);
    if (max_w > 0.f) scale = fminf(scale, max_w / width);
    if (max_h > 0.f) scale = fminf(scale, max_h / height);
    v->frame.w = width * scale;
    v->frame.h = height * scale;
    break;
  }
  case SZ_VIEW_COLUMN:
  case SZ_VIEW_LIST: {
    float cy = y + theme->pad;
    float inner_w = inset_max_axis(max_w, theme->pad);
    float h = theme->pad;
    int shown = 0;
    double flex_weight = 0.0;
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
        if (view_flex_weight(v->children[i]))
          flex_weight += (double)view_flex_weight(v->children[i]);
      }
      if (flex_weight > 0.0 && h_budget > 0.f) {
        /* Measure non-flex children for leftover height. */
        for (i = 0; i < v->child_count; i++) {
          SzView *ch = v->children[i];
          if (!view_is_shown(ch) || view_flex_weight(ch))
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
          flex_weight = 0.0;
        else {
          flex_h = h_budget - theme->pad * 2.f - fixed_h - gaps;
          if (flex_h < 0.f)
            flex_h = 0.f;
          cy = y + theme->pad;
          for (i = 0; i < v->child_count; i++) {
            SzView *ch = v->children[i];
            if (!view_is_shown(ch)) {
              layout_constrained(ch, x + theme->pad, cy, box_loose(inner_w, max_h),
                                 theme);
              continue;
            }
            layout_constrained(ch, x + theme->pad, cy,
                               column_child_box(ch, inner_w, max_h,
                                 (float)(flex_h * ((double)view_flex_weight(ch) / flex_weight)), 1),
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
      /* A bounded slot is tight. An unbounded height follows the content. */
      layout_constrained(ch, x, y, slot_h > 0.f ? box_tight(slot_w, slot_h)
                                      : box_tight_width(slot_w, 0.f), theme);
      if (slot_h <= 0.f)
        v->frame.h = ch->frame.h;
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
    double flex_weight = 0.0;
    float fixed_w = 0.f;
    float flex_w = 0.f;
    float gaps = 0.f;
    float child_max;
    float row_inner_h = row_cross_inner(min_h, max_h, theme);
    float w_budget = max_w > 0.f ? max_w : min_w;
    for (i = 0; i < v->child_count; i++) {
      if (view_is_shown(v->children[i]) && view_flex_weight(v->children[i]))
        flex_weight += (double)view_flex_weight(v->children[i]);
    }
    if (flex_weight > 0.0 && w_budget > 0.f) {
      /* Measure non-flex at intrinsic width (large max_w). */
      for (i = 0; i < v->child_count; i++) {
        SzView *ch = v->children[i];
        if (!view_is_shown(ch) || view_flex_weight(ch))
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
        flex_weight = 0.0;
      else {
        flex_w = w_budget - theme->pad * 2.f - fixed_w - gaps;
        if (flex_w < 0.f)
          flex_w = 0.f;
        /* Unbounded rows measure flex content before setting the cross slot. */
        if (max_h <= 0.f) {
          for (i = 0; i < v->child_count; i++) {
            SzView *ch = v->children[i];
            if (!view_is_shown(ch) || !view_flex_weight(ch))
              continue;
            layout_constrained(ch, x + theme->pad, y + theme->pad,
                               box_tight_width((float)(flex_w * ((double)view_flex_weight(ch) / flex_weight)), 0.f), theme);
            if (ch->frame.h > row_inner_h)
              row_inner_h = ch->frame.h;
          }
        }
        cx = x + theme->pad;
        for (i = 0; i < v->child_count; i++) {
          SzView *ch = v->children[i];
          if (!view_is_shown(ch)) {
            layout_constrained(ch, cx, y + theme->pad,
                               box_loose(flex_w, row_inner_h), theme);
            continue;
          }
          layout_constrained(ch, cx, y + theme->pad,
                             row_child_box(ch, max_w, row_inner_h,
                               (float)(flex_w * ((double)view_flex_weight(ch) / flex_weight)), 1),
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
    /* Give each non-flex child the inner max width. Do not split the row
     * equally. Equal split shrinks button labels when the row overflows. */
    child_max = max_w > theme->pad * 2.f ? max_w - theme->pad * 2.f : 0.f;
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
  case SZ_VIEW_BREADCRUMB: {
    float gap = layout_gap(theme);
    float sep = text_width(">", font);
    float slot = gap + sep + gap;
    float cx;
    float inner_h = 0.f;
    int n_shown = 0;
    for (i = 0; i < v->child_count; i++) {
      SzView *ch = v->children[i];
      layout_constrained(ch, x, y, box_loose(0.f, 0.f), theme);
      if (!view_is_shown(ch))
        continue;
      n_shown++;
      if (ch->frame.h > inner_h)
        inner_h = ch->frame.h;
    }
    if (inner_h < text_line_h(theme, font))
      inner_h = text_line_h(theme, font);
    cx = x;
    for (i = 0; i < v->child_count; i++) {
      SzView *ch = v->children[i];
      if (!view_is_shown(ch)) {
        layout_constrained(ch, cx, y, box_loose(0.f, 0.f), theme);
        continue;
      }
      layout_constrained(ch, cx, y + (inner_h - ch->frame.h) * 0.5f,
                         box_loose(0.f, 0.f), theme);
      cx += ch->frame.w + slot;
    }
    if (n_shown > 0)
      cx -= slot;
    v->frame.w = cx - x;
    v->frame.h = inner_h;
    break;
  }
  case SZ_VIEW_WRAP: {
    float pad = theme->pad;
    float gap = layout_gap(theme);
    float inner_w = inset_max_axis(max_w, pad);
    int unbounded = !(max_w > 0.f);
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
    int unbounded = v->grid_min_width > 0 ? !(max_w > 0.f) : !(max_w > pad * 2.f);
    if (v->grid_min_width > 0 && !unbounded && pad * 2.f >= max_w)
      pad = max_w * 0.25f;
    float inner_w = unbounded ? 0.f : max_w - pad * 2.f;
    float cell_w = 0.f;
    if (v->grid_min_width > 0 && !unbounded) {
      float minimum = scale_px(theme, (float)v->grid_min_width);
      float count = floorf((inner_w + gap) / (minimum + gap));
      /* Empty columns do not reduce the width of the shown children. */
      int shown = count_shown_children(v);
      cols = count >= (float)shown ? shown : count >= 1.f ? (int)count : 1;
      if (cols < 1)
        cols = 1;
    }
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
  case SZ_VIEW_ANIMATED_POSITION:
  case SZ_VIEW_POSITIONED: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    if (v->kind == SZ_VIEW_ANIMATED_POSITION) sync_axes(v, transition_suppressed(v));
    float px = scale_px(theme, (float)(v->kind == SZ_VIEW_ANIMATED_POSITION ? v->animation_value : v->pos_x));
    float py = scale_px(theme, (float)(v->kind == SZ_VIEW_ANIMATED_POSITION ? v->animation_axis2 : v->pos_y));
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
    float inner_w = inset_max_axis(max_w, p);
    float inner_h = inset_max_axis(max_h, p);
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
    float inner_w = inset_max_axis(max_w, p);
    float inner_h = inset_max_axis(max_h, p);
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
  case SZ_VIEW_ANIMATED_CONTENT_SIZE: {
    layout_pass_child(v, x, y, min_w, min_h, max_w, max_h, theme);
    float scale = theme_px_scale(theme);
    sync_content_size(v, v->frame.w / scale, v->frame.h / scale);
    v->frame.w = fmaxf(min_w, v->content_w * scale);
    v->frame.h = fmaxf(min_h, v->content_h * scale);
    if (max_w > 0.f && v->frame.w > max_w) v->frame.w = max_w;
    if (max_h > 0.f && v->frame.h > max_h) v->frame.h = max_h;
    break;
  }
  case SZ_VIEW_ANIMATED_SIZE:
  case SZ_VIEW_SIZED: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    if (v->kind == SZ_VIEW_ANIMATED_SIZE) sync_axes(v, transition_suppressed(v));
    float tw = scale_px(theme, (float)(v->kind == SZ_VIEW_ANIMATED_SIZE ? v->animation_value : v->img_w));
    float th = scale_px(theme, (float)(v->kind == SZ_VIEW_ANIMATED_SIZE ? v->animation_axis2 : v->img_h));
    if (tw < min_w) tw = min_w;
    if (th < min_h) th = min_h;
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
  case SZ_VIEW_BADGE: {
    SzView *child = v->child_count ? v->children[0] : NULL;
    float ticket = scale_px(theme, 40.f);
    float gap = scale_px(theme, 4.f);
    if (max_w > 0.f) {
      ticket = fminf(ticket, max_w * 0.5f);
      gap = fminf(gap, max_w * 0.1f);
    }
    if (child)
      layout_constrained(child, x, y,
                         box_loose(max_w > 0.f ? max_w - ticket - gap : 0.f, max_h), theme);
    v->frame.w = (child ? child->frame.w + gap : 0.f) + ticket;
    v->frame.h = fmaxf(child ? child->frame.h : 0.f, scale_px(theme, 24.f));
    break;
  }
  case SZ_VIEW_SCALE:
  case SZ_VIEW_ANIMATED_SCALE:
  case SZ_VIEW_THEME:
  case SZ_VIEW_BACKGROUND:
  case SZ_VIEW_CLIP:
  case SZ_VIEW_OPACITY:
  case SZ_VIEW_ANIMATED_OPACITY:
  case SZ_VIEW_REDUCED_MOTION:
  case SZ_VIEW_IGNORE_POINTER:
  case SZ_VIEW_ABSORB_POINTER:
  case SZ_VIEW_EXCLUDE_SEMANTICS:
  case SZ_VIEW_TEXT_COLOR:
  case SZ_VIEW_BORDER:
  case SZ_VIEW_RADIUS:
  case SZ_VIEW_TOOLTIP:
  case SZ_VIEW_ON_SECONDARY:
  case SZ_VIEW_FOCUS_GROUP:
  case SZ_VIEW_PLACEHOLDER:
  case SZ_VIEW_SECTION:
  case SZ_VIEW_SEMANTICS:
  case SZ_VIEW_MERGE_SEMANTICS:
  case SZ_VIEW_INK_WELL:
  case SZ_VIEW_VISIBILITY:
    if (v->kind == SZ_VIEW_ANIMATED_SCALE) sync_scalar(v, transition_suppressed(v));
    layout_pass_child(v, x, y, min_w, min_h, max_w, max_h, theme);
    if (v->kind == SZ_VIEW_FOCUS_GROUP)
      restore_group_focus(v);
    break;
  case SZ_VIEW_OFFSTAGE:
    layout_pass_child(v, x, y, min_w, min_h, max_w, max_h, theme);
    if (!view_offstage_shown(v)) {
      v->frame.w = 0.f;
      v->frame.h = 0.f;
    }
    break;
  case SZ_VIEW_ADAPTIVE: {
    if (v->child_count != 2)
      sz_panic("View.adaptive requires two branches");
    v->adaptive_selected = max_w <= 0.f || max_w >= scale_px(theme, (float)v->adaptive_min_width);
    SzView *selected = v->children[v->adaptive_selected];
    SzView *hidden = v->children[1 - v->adaptive_selected];
    clear_focus(hidden);
    layout_constrained(hidden, x, y, box_loose(max_w, max_h), theme);
    SzBoxConstraints branch = {min_w, min_h, max_w, max_h};
    layout_constrained(selected, x, y, branch, theme);
    v->frame.w = selected->frame.w;
    v->frame.h = selected->frame.h;
    break;
  }
  case SZ_VIEW_UNCONSTRAINED_BOX:
    /* Child gets unbounded max (0). Incoming max still clamps this frame. */
    layout_pass_child(v, x, y, 0.f, 0.f, 0.f, 0.f, theme);
    break;
  case SZ_VIEW_APP_SHELL: {
    float width = max_w > 0.f ? max_w : scale_px(theme, 640.f);
    float height = max_h > 0.f ? max_h : scale_px(theme, 480.f);
    SzView *bar = v->children[0];
    layout_constrained(bar, x, y, box_tight_width(width, 0.f), theme);
    float bh = fminf(bar->frame.h, height);
    layout_constrained(v->children[1], x, y + bh,
                       box_tight(width, fmaxf(0.01f, height - bh)), theme);
    v->frame.w = width;
    v->frame.h = height;
    break;
  }
  case SZ_VIEW_APP_BAR: {
    SzView *title = v->children[0], *actions = v->children[1];
    float pad = theme->pad, gap = layout_gap(theme);
    float width = max_w > 0.f ? max_w : scale_px(theme, 640.f);
    float inner = fmaxf(0.01f, width - 2.f * pad);
    layout_constrained(title, x + pad, y + pad, box_loose(0.f, 0.f), theme);
    layout_constrained(actions, x + pad, y + pad, box_loose(0.f, 0.f), theme);
    float tw = title->frame.w, aw = actions->frame.w;
    if (tw + gap + aw <= inner) {
      float h = fmaxf(title->frame.h, actions->frame.h);
      layout_constrained(title, x + pad, y + pad + (h - title->frame.h) / 2.f,
                         box_loose(tw, 0.f), theme);
      layout_constrained(actions, x + width - pad - aw,
                         y + pad + (h - actions->frame.h) / 2.f, box_loose(aw, 0.f), theme);
      v->frame.h = h + 2.f * pad;
    } else {
      layout_constrained(title, x + pad, y + pad, box_loose(inner, 0.f), theme);
      layout_constrained(actions, x + pad, y + pad + title->frame.h + gap,
                         box_loose(inner, 0.f), theme);
      v->frame.h = title->frame.h + gap + actions->frame.h + 2.f * pad;
    }
    v->frame.w = width;
    break;
  }
  case SZ_VIEW_TABS:
  case SZ_VIEW_INDEX_BOOK: {
    SzView *nav = v->children[0];
    SzView *tabs = nav->children[0]->children[0];
    float width = max_w > 0.f ? max_w : scale_px(theme, 640.f);
    float height = max_h > 0.f ? max_h : scale_px(theme, 480.f);
    float gap = layout_gap(theme);
    int wide = v->kind == SZ_VIEW_INDEX_BOOK && width >= scale_px(theme, 640.f);
    int64_t selected = sz_signal_int_get(v->sig_int);
    float nx, nh, px, py, pw, ph;
    if (selected < 0 || selected >= v->child_count - 1)
      sz_panic("Page selection is outside its sections");
    tabs->kind = wide ? SZ_VIEW_COLUMN : SZ_VIEW_WRAP;
    nx = wide ? scale_px(theme, 192.f) : width;
    if (wide) {
      nh = height;
    } else {
      layout_constrained(tabs, x, y,
                         box_tight_width(fmaxf(0.f, width - 2.f * theme->pad), 0.f), theme);
      nh = fminf(tabs->frame.h + 2.f * theme->pad, height * 0.4f);
    }
    layout_constrained(nav, x, y, box_tight(nx, nh), theme);
    float nav_limit = fmaxf(0.f, (tabs->frame.h + 2.f * theme->pad - nh) /
                                     theme_px_scale(theme));
    if (nav->children[0]->scroll_y > nav_limit) {
      nav->children[0]->scroll_y = nav_limit;
      layout_constrained(nav, x, y, box_tight(nx, nh), theme);
    }
    px = wide ? x + nx + gap : x;
    py = wide ? y : y + nh + gap;
    pw = wide ? fmaxf(0.f, width - nx - gap) : width;
    ph = wide ? height : fmaxf(0.f, height - nh - gap);
    for (i = 1; i < v->child_count; i++) {
      SzView *page = v->children[i];
      layout_constrained(page, px, py, box_tight(pw, ph), theme);
      if (view_is_shown(page)) {
        float limit = fmaxf(0.f, (page->scroll_child->frame.h + 2.f * theme->pad - ph) /
                                    theme_px_scale(theme));
        if (page->scroll_y > limit) {
          page->scroll_y = limit;
          layout_constrained(page, px, py, box_tight(pw, ph), theme);
        }
      }
    }
    v->frame.w = width;
    v->frame.h = height;
    break;
  }
  case SZ_VIEW_SPLIT: {
    SzView *left = v->child_count > 0 ? v->children[0] : NULL;
    SzView *right = v->child_count > 1 ? v->children[1] : NULL;
    float handle = scale_px(theme, 6.f);
    float avail = max_w > handle ? max_w - handle : 0.f;
    float n = (float)percent_clamp(v->sig_int ? sz_signal_int_get(v->sig_int) : 50);
    if (n > 0) v->split_restore = (int)n;
    float lw = avail * (n / 100.f);
    float rw = avail - lw;
    float inner_h = max_h > 0.f ? max_h : theme->control_h;
    if (lw < 0.f)
      lw = 0.f;
    if (rw < 0.f)
      rw = 0.f;
    if (left) {
      if (n == 0) clear_focus(left);
      layout_constrained(left, x, y, box_tight(lw, inner_h), theme);
    }
    if (right) {
      if (n == 100) clear_focus(right);
      layout_constrained(right, x + lw + handle, y, box_tight(rw, inner_h), theme);
    }
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
        layout_constrained(v->scroll_child, x + pad - scale_px(theme, v->scroll_x), y + pad,
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
        vh = scale_px(theme, v->pref_h);
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
        layout_constrained(v->scroll_child, x + pad, y + pad - scale_px(theme, v->scroll_y),
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

static void sync_modal_focus(SzView *root);
static SzView *find_open_overlay(SzView *v);
static void sync_tooltips(SzView *root);
static int rects_intersect(SzRect a, SzRect b, SzRect *out);

static int popup_owner_in_scope(SzView *v, SzView *scope);

static SzView *open_select_node(SzView *v) {
  if (!v || !view_is_shown(v) || size_collapsed(v) ||
      (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v)) ||
      (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v)) ||
      (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))) return NULL;
  if (v->kind == SZ_VIEW_SELECT) return v->select_open ? v : NULL;
  for (int i = v->child_count - 1; i >= 0; i--) {
    SzView *found = open_select_node(v->children[i]);
    if (found) return found;
  }
  return NULL;
}

static void close_selects(SzView *v, SzView *except) {
  if (!v) return;
  if (v->kind == SZ_VIEW_SELECT && v != except) {
    v->select_open = 0;
    v->select_search_bytes = 0;
  }
  for (int i = 0; i < v->child_count; i++) close_selects(v->children[i], except);
}

static void select_reveal(SzView *v) {
  v->select_reveal_pending = 1;
  SzView *popup = sz_view_select_popup(v);
  if (!popup || v->select_active < 0 || popup->frame.h <= 0.f) return;
  const SzTheme *theme = &v->layout_theme;
  double row = theme->control_h / theme_px_scale(theme);
  double height = popup->frame.h / theme_px_scale(theme);
  double top = v->select_active * row;
  if (top < popup->scroll_y) popup->scroll_y = top;
  if (top + row > popup->scroll_y + height) popup->scroll_y = top + row - height;
}

static void select_begin(SzView *root, SzView *v) {
  if (!v || !v->select_count) return;
  close_selects(root, v);
  int64_t value = sz_signal_int_get(v->sig_int);
  v->select_observed = value;
  v->select_active = value >= 0 && value < v->select_count ? (int)value : 0;
  v->select_search_bytes = 0;
  v->select_open = 1;
  select_reveal(v);
}

static void select_commit(SzView *v) {
  if (v) select_sync_options(v);
  if (v && v->select_open && v->select_active >= 0 && v->select_active < v->select_count &&
      sz_signal_int_get(v->sig_int) == v->select_observed)
    sz_signal_int_set(v->sig_int, v->select_active);
  if (v) { v->select_open = 0; v->select_search_bytes = 0; }
}

void sz_view_input_time(SzView *root, int64_t milliseconds) {
  if (!root || milliseconds < 0) sz_panic("UI input time must not be negative");
  root->input_time = milliseconds;
}

static unsigned char select_fold(unsigned char c) {
  return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 'a' - 'A') : c;
}

static int select_prefix(const char *label, const char *prefix, int length) {
  for (int i = 0; i < length; i++)
    if (!label[i] || select_fold((unsigned char)label[i]) != select_fold((unsigned char)prefix[i])) return 0;
  return 1;
}

static int select_search(SzView *root, SzView *v, const char *text) {
  if (!text || !text[0] || !v->select_count) return 0;
  if (!v->select_open) select_begin(root, v);
  size_t length = strlen(text);
  if (length >= sizeof v->select_search) return 1;
  if (root->input_time < v->select_search_time || root->input_time - v->select_search_time > 1000) v->select_search_bytes = 0;
  v->select_search_time = root->input_time;
  int repeated = v->select_search_bytes == (int)length && select_prefix(v->select_search, text, (int)length);
  int start = repeated || !v->select_search_bytes ? v->select_active + 1 : v->select_active;
  if (repeated) v->select_search_bytes = 0;
  if ((size_t)v->select_search_bytes + length >= sizeof v->select_search) v->select_search_bytes = 0;
  memcpy(v->select_search + v->select_search_bytes, text, length);
  v->select_search_bytes += (int)length;
  v->select_search[v->select_search_bytes] = 0;
  for (int pass = 0; pass < 2; pass++) {
    for (int offset = 0; offset < v->select_count; offset++) {
      int index = (int)(((int64_t)start + offset) % v->select_count);
      if (select_prefix(v->select_labels[index], v->select_search, v->select_search_bytes)) {
        v->select_active = index;
        select_reveal(v);
        return 1;
      }
    }
    if ((size_t)v->select_search_bytes == length) break;
    memcpy(v->select_search, text, length + 1);
    v->select_search_bytes = (int)length;
    start = v->select_active + 1;
  }
  return 1;
}

static int select_key(SzView *root, SzView *v, const char *key, const char *text, int mods) {
  if (!v->select_count) return 0;
  if (mods & (SZ_KEY_CTRL | SZ_KEY_CMD)) return 0;
  if (!strcmp(key, "Escape")) {
    if (!v->select_open) return 0;
    close_selects(root, NULL);
    return 1;
  }
  if (!strcmp(key, "Tab")) { select_commit(v); return 0; }
  if (!strcmp(key, "Backspace")) { v->select_search_bytes = 0; return 1; }
  if ((mods & SZ_KEY_ALT) && strcmp(key, "ArrowUp") && strcmp(key, "ArrowDown")) return 0;
  int was_open = v->select_open;
  if (!was_open && (mods & SZ_KEY_ALT) && !strcmp(key, "ArrowUp")) return 0;
  if (!strcmp(key, "Enter") || !strcmp(key, "Space") || !strcmp(key, " ") ||
      ((mods & SZ_KEY_ALT) && !strcmp(key, "ArrowUp"))) {
    if (was_open) select_commit(v); else select_begin(root, v);
    return 1;
  }
  int64_t next = v->select_active;
  if (!strcmp(key, "ArrowDown") || !strcmp(key, "ArrowUp") || !strcmp(key, "Home") ||
      !strcmp(key, "End") || !strcmp(key, "PageDown") || !strcmp(key, "PageUp")) {
    if (!was_open) select_begin(root, v);
    next = v->select_active;
    if (!strcmp(key, "Home") || (!was_open && !strcmp(key, "ArrowUp"))) next = 0;
    else if (!strcmp(key, "End")) next = v->select_count - 1;
    else if (!strcmp(key, "PageDown")) next += 10;
    else if (!strcmp(key, "PageUp")) next -= 10;
    else if (was_open && !(mods & SZ_KEY_ALT)) next += !strcmp(key, "ArrowUp") ? -1 : 1;
    if (next < 0) next = 0;
    if (next >= v->select_count) next = v->select_count - 1;
    v->select_active = (int)next;
    v->select_search_bytes = 0;
    select_reveal(v);
    return 1;
  }
  if (mods & SZ_KEY_ALT) return 0;
  if (text && text[0]) return select_search(root, v, text);
  size_t length = strlen(key);
  if (length && (unsigned char)key[0] >= 32 && (size_t)utf8_clen(key, 0) == length)
    return select_search(root, v, key);
  return 0;
}

static void layout_select_popup(SzView *root, SzView *v) {
  SzRect anchor = sz_view_frame(v);
  const SzTheme *theme = &v->layout_theme;
  float scale = theme_px_scale(theme), row = theme->control_h;
  float edge = scale_px(theme, 4.f);
  float w = fminf(anchor.w, fmaxf(0.f, root->viewport_w - edge * 2.f));
  float below = fmaxf(0.f, root->viewport_h - edge - anchor.y - anchor.h);
  float above = fmaxf(0.f, anchor.y - edge);
  float wanted = fminf((float)v->select_count, 8.f) * row;
  int up = below < wanted && above > below;
  float h = fminf(wanted, up ? above : below);
  if (w <= 0.f || h <= 0.f || row <= 0.f) { v->select_open = 0; return; }
  float x = fminf(fmaxf(anchor.x, edge), root->viewport_w - edge - w);
  SzView *popup = sz_view_select_popup(v), *rows = popup->scroll_child;
  if (popup->frame.h != h || popup->layout_theme.control_h != row) v->select_reveal_pending = 1;
  popup->frame = (SzRect){x, up ? anchor.y - h : anchor.y + anchor.h, w, h};
  popup->layout_theme = *theme;
  popup->layout_theme.pad = 0.f;
  if (v->select_reveal_pending) { select_reveal(v); v->select_reveal_pending = 0; }
  double maximum = fmax(0.0, ((double)v->select_count * row - h) / scale);
  if (popup->scroll_y < 0.0) popup->scroll_y = 0.0;
  if (popup->scroll_y > maximum) popup->scroll_y = maximum;
  rows->frame = (SzRect){x, popup->frame.y - (float)(popup->scroll_y * scale), w, v->select_count * row};
  rows->layout_theme = *theme;
  int first = (int)floor(popup->scroll_y * scale / row);
  int visible_first = (int)ceil(popup->scroll_y * scale / row);
  int visible_last = (int)floor((popup->scroll_y * scale + h) / row) - 1;
  if (visible_first >= v->select_count) visible_first = v->select_count - 1;
  if (visible_last < visible_first) visible_last = visible_first;
  if (visible_last >= v->select_count) visible_last = v->select_count - 1;
  if (v->select_active < visible_first) { v->select_active = visible_first; v->select_search_bytes = 0; }
  if (v->select_active > visible_last) { v->select_active = visible_last; v->select_search_bytes = 0; }
  v->select_first = first;
  for (int i = 0; i < rows->child_count; i++) {
    SzView *option = rows->children[i];
    int index = first + i;
    if (index >= v->select_count) { option->frame = (SzRect){0}; continue; }
    if (option->radio_value != index) {
      sz_free(option->text); sz_free(option->a11y_label);
      option->text = sz_strdup(v->select_labels[index]);
      option->a11y_label = sz_strdup(v->select_labels[index]);
      option->radio_value = index;
    }
    option->id = v->select_option_base + (uintptr_t)index;
    option->layout_theme = *theme;
    option->frame = (SzRect){x, rows->frame.y + index * row, w, row};
  }
}

static void sync_selects(SzView *root) {
  SzView *modal = find_open_overlay(root);
  SzView *scope = modal ? modal : root;
  SzView *v = open_select_node(scope);
  if (v && (!popup_owner_in_scope(v, scope) || !v->focused)) v = NULL;
  close_selects(root, v);
  if (!v) return;
  int64_t value = sz_signal_int_get(v->sig_int);
  if (value != v->select_observed) {
    v->select_observed = value;
    v->select_active = value >= 0 && value < v->select_count ? (int)value : 0;
    v->select_search_bytes = 0;
    select_reveal(v);
  }
  layout_select_popup(root, v);
}

void sz_view_prepare_input(SzView *root) {
  if (!root) return;
  sync_select_options(root);
  sync_modal_focus(root);
  sync_selects(root);
}

void sz_view_layout(SzView *root, float width, float height, const SzTheme *theme) {
  if (!root || !theme)
    return;
#ifdef __EMSCRIPTEN__
  sz_web_text_scale(theme->px_scale);
#endif
  g_max_lines = 0;
  g_ellipsis = 0;
  g_gap_on = 0;
  g_font_px = 0.f;
  root->viewport_w = width;
  root->viewport_h = height;
  layout_node(root, 0.f, 0.f, width, height, theme);
  sync_modal_focus(root);
  sync_selects(root);
  sync_transitions(root, 1, root->motion_reduced);
  sync_tooltips(root);
}

static int point_in(const SzRect *r, float x, float y) {
  return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static int view_clips_children(const SzView *view) {
  return view->kind == SZ_VIEW_CLIP || view->kind == SZ_VIEW_RADIUS ||
    view->kind == SZ_VIEW_SCROLL || view->kind == SZ_VIEW_APP_SHELL ||
    view->kind == SZ_VIEW_EXPANSION_TILE || view->kind == SZ_VIEW_ANIMATED_SIZE ||
    view->kind == SZ_VIEW_ANIMATED_CONTENT_SIZE;
}

static int point_in_view(const SzView *view, float x, float y) {
  SzRect frame = sz_view_frame(view);
  return point_in(&frame, x, y);
}

static int input_node_allows(const SzView *view, float x, float y) {
  if (!view || !view_is_shown(view) || size_collapsed(view)) return 0;
  if (!view->parent && view->viewport_w > 0 && view->viewport_h > 0 &&
      (x < 0 || y < 0 || x >= view->viewport_w || y >= view->viewport_h)) return 0;
  return !view_clips_children(view) || point_in_view(view, x, y);
}

static SzView *tooltip_for_focus(SzView *root) {
  for (SzView *p = sz_view_focused(root); p; p = p->parent)
    if (p->kind == SZ_VIEW_TOOLTIP && p->text && p->text[0]) return p;
  return NULL;
}

static int popup_owner_in_scope(SzView *v, SzView *scope) {
  if (!view_is_path_shown(v) || v->frame.w <= 0.f || v->frame.h <= 0.f) return 0;
  int inside = 0;
  SzRect visible = sz_view_frame(v);
  for (SzView *p = v; p; p = p->parent) {
    if (p == scope) inside = 1;
    if (!view_is_shown(p) ||
        (p->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(p)) ||
        (p->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(p)) ||
        (p->kind == SZ_VIEW_OVERLAY && !view_overlay_open(p))) return 0;
    if (p->kind == SZ_VIEW_IGNORE_POINTER || p->kind == SZ_VIEW_ABSORB_POINTER) return 0;
    if (view_clips_children(p)) {
      SzRect clipped;
      if (!rects_intersect(visible, sz_view_frame(p), &clipped)) return 0;
      visible = clipped;
    }
  }
  return inside;
}

static void sync_tooltip_node(SzView *v, SzView *root, SzView *scope, SzView *focus) {
  if (v->kind == SZ_VIEW_TOOLTIP) {
    SzRect anchor = sz_view_frame(v);
    SzRect viewport = {0, 0, root->viewport_w, root->viewport_h}, visible;
    int eligible = popup_owner_in_scope(v, scope) && v->text && v->text[0] &&
      rects_intersect(anchor, viewport, &visible);
    if (!eligible) v->hover = 0;
    if ((!v->hover && focus != v) || !eligible) v->tooltip_dismissed = 0;
    v->tooltip_open = eligible && (v->hover || focus == v) && !v->tooltip_dismissed;
    v->tooltip_frame = (SzRect){0};
    if (v->tooltip_open && root->viewport_w > 0.f && root->viewport_h > 0.f) {
      const SzTheme *theme = &v->layout_theme;
      float pad = scale_px(theme, 4.f);
      float width = fminf(root->viewport_w, scale_px(theme, 320.f));
      float inner = fmaxf(1.f, width - 2.f * pad);
      float ascent, descent;
      sk_font_metrics(theme->font_px, 0, &ascent, &descent);
      float line = fmaxf(text_line_h(theme, theme->font_px),
        fmaxf(theme->font_px, ascent) + 2.f * theme_px_scale(theme) + descent);
      int cap = (int)fmaxf(1.f, floorf((root->viewport_h - 2.f * pad) / line));
      v->tooltip_lines = cap;
      SzTextMetrics metrics = sz_text_layout(v->styled, theme->font_px,
        theme_px_scale(theme), scale_px(theme, theme->line_gap), inner, cap, 1, NULL, NULL);
      float w = fminf(width, metrics.width + 2.f * pad);
      float h = fminf(root->viewport_h, metrics.height + 2.f * pad);
      float x = fmaxf(0.f, fminf(anchor.x, root->viewport_w - w));
      float y = anchor.y + anchor.h;
      if (y + h > root->viewport_h) y = anchor.y - h;
      y = fmaxf(0.f, fminf(y, root->viewport_h - h));
      v->tooltip_frame = (SzRect){x, y, w, h};
    }
  }
  for (int i = 0; i < v->child_count; i++) sync_tooltip_node(v->children[i], root, scope, focus);
}

static void sync_tooltips(SzView *root) {
  if (root) sync_tooltip_node(root, root, sz_view_input_scope(root), tooltip_for_focus(root));
}

SzView *sz_view_tooltip_owner(SzView *view) {
  for (SzView *p = view ? view->parent : NULL; p; p = p->parent)
    if (p->kind == SZ_VIEW_TOOLTIP && p->text && p->text[0]) return p;
  return NULL;
}

int sz_view_tooltip_visible(const SzView *view) {
  return view && view->kind == SZ_VIEW_TOOLTIP && view->tooltip_open;
}

SzRect sz_view_tooltip_frame(const SzView *view) {
  return sz_view_tooltip_visible(view) ? view->tooltip_frame : (SzRect){0};
}

static SzView *tooltip_popup_at(SzView *v, float x, float y) {
  if (!v) return NULL;
  for (int i = v->child_count - 1; i >= 0; i--) {
    SzView *found = tooltip_popup_at(v->children[i], x, y);
    if (found) return found;
  }
  return v->tooltip_open && point_in(&v->tooltip_frame, x, y) ? v : NULL;
}

static int dismiss_tooltips(SzView *root) {
  int dismissed = 0;
  if (!root) return 0;
  if (root->tooltip_open) {
    root->tooltip_dismissed = 1;
    root->tooltip_open = 0;
    dismissed = 1;
  }
  for (int i = 0; i < root->child_count; i++) dismissed |= dismiss_tooltips(root->children[i]);
  return dismissed;
}

static SzView *input_scope_at(SzView *root, float x, float y) {
  SzView *scope = sz_view_input_scope(root);
  if (scope == root || (scope && scope->kind == SZ_VIEW_SELECT)) return scope;
  for (SzView *p = scope ? scope->parent : NULL; p; p = p->parent) {
    if (!input_node_allows(p, x, y) || p->kind == SZ_VIEW_IGNORE_POINTER ||
        p->kind == SZ_VIEW_ABSORB_POINTER) return NULL;
    if (p == root) break;
  }
  return scope;
}

static SzView *hit_node(SzView *v, float x, float y) {
  int i;
  SzView *hit;
  if (!input_node_allows(v, x, y))
    return NULL;
  if (v->inline_action) {
    for (int i = 0; i < v->action_count; i++)
      { SzRect box = display_rect(v, v->action_boxes[i]);
        if (point_in(&box, x, y)) return v; }
    return NULL;
  }
  if (v->kind == SZ_VIEW_IGNORE_POINTER)
    return NULL;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return NULL;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return NULL;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return NULL;
  if (v->kind == SZ_VIEW_ABSORB_POINTER)
    return point_in_view(v, x, y) ? v : NULL;
  /* Front-to-back: last child wins. */
  for (i = v->child_count - 1; i >= 0; i--) {
    hit = hit_node(v->children[i], x, y);
    if (hit)
      return hit;
  }
  if (v->kind == SZ_VIEW_CARD && point_in_view(v, x, y)) {
    for (SzView *p = v->parent; p; p = p->parent) {
      if (p->kind == SZ_VIEW_OVERLAY) return v;
      if (p->interactive) break;
    }
  }
  if (v->kind == SZ_VIEW_SPLIT || v->kind == SZ_VIEW_EXPANSION_TILE) {
    SzRect handle = sz_view_control_frame(v);
    return point_in(&handle, x, y) ? v : NULL;
  }
  return v->interactive && point_in_view(v, x, y) ? v : NULL;
}

SzView *sz_view_hit_test(SzView *root, float x, float y) {
  SzView *select = open_select_node(sz_view_input_scope(root));
  if (select) {
    if (point_in_view(select, x, y)) return select;
    SzView *hit = hit_node(sz_view_select_popup(select), x, y);
    return hit ? hit : select;
  }
  SzView *popup = tooltip_popup_at(sz_view_input_scope(root), x, y);
  return popup ? popup : hit_node(input_scope_at(root, x, y), x, y);
}

static SzView *tooltip_at_node(SzView *v, float x, float y) {
  int i;
  SzView *found;
  if (!input_node_allows(v, x, y))
    return NULL;
  if (v->kind == SZ_VIEW_IGNORE_POINTER || v->kind == SZ_VIEW_ABSORB_POINTER)
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
  return v->kind == SZ_VIEW_TOOLTIP && v->text && v->text[0] && point_in_view(v, x, y) ? v : NULL;
}

SzView *sz_view_tooltip_at(SzView *root, float x, float y) {
  SzView *popup = tooltip_popup_at(sz_view_input_scope(root), x, y);
  return popup ? popup : tooltip_at_node(input_scope_at(root, x, y), x, y);
}

static SzView *on_secondary_at_node(SzView *v, float x, float y) {
  int i;
  SzView *found;
  if (!input_node_allows(v, x, y))
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
    found = on_secondary_at_node(v->children[i], x, y);
    if (found)
      return found;
  }
  return v->kind == SZ_VIEW_ON_SECONDARY && point_in_view(v, x, y) ? v : NULL;
}

int sz_view_handle_secondary(SzView *root, float x, float y) {
  if (tooltip_popup_at(sz_view_input_scope(root), x, y)) return 0;
  SzView *sec = on_secondary_at_node(input_scope_at(root, x, y), x, y);
  if (!sec || !sec->on_tap)
    return 0;
  sec->on_tap(sec, sec->tap_env);
  return 1;
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
  if (!root) return 0;
  int moved = !root->hover_position_set || x != root->hover_x || y != root->hover_y;
  root->hover_position_set = 1;
  root->hover_x = x; root->hover_y = y;
  SzView *select = open_select_node(sz_view_input_scope(root));
  if (select) {
    if (!moved) return 0;
    SzView *hit = sz_view_hit_test(root, x, y);
    if (hit && hit->kind == SZ_VIEW_SELECT_OPTION) {
      int changed = select->select_active != hit->radio_value;
      select->select_active = (int)hit->radio_value;
      select->select_search_bytes = 0;
      return changed;
    }
    return 0;
  }
  SzView *tip;
  tip = sz_view_tooltip_at(root, x, y);
  sz_view_clear_hover(root);
  if (tip) tip->hover = 1;
  sync_tooltips(root);
  return tip != NULL;
}

static void clear_pressed(SzView *v) {
  if (!v)
    return;
  v->pressed = 0;
  for (int i = 0; i < v->child_count; i++)
    clear_pressed(v->children[i]);
}

void sz_view_set_pressed_at(SzView *root, float x, float y, int active) {
  SzView *hit;
  clear_pressed(root);
  hit = active ? sz_view_hit_test(root, x, y) : NULL;
  if (hit && hit->kind == SZ_VIEW_BUTTON)
    hit->pressed = 1;
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

/* Selection marks keep their shape when color is unavailable. */
static void paint_check(SkCanvas *c, SzRect box, int on, const SzTheme *theme) {
  int i;
  float unit = box.w / 18.f;
  if (on) {
    paint_rect(c, box.x, box.y, box.w, box.h, theme->primary);
    for (i = 0; i < 11; i++) {
      float y = i < 4 ? 8.f + i : 15.f - i;
      paint_rect(c, box.x + (3.f + i) * unit, box.y + y * unit,
                 2.f * unit, 2.f * unit, theme->foreground);
    }
  }
  paint_border(c, box, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
}

static void paint_switch_track(SkCanvas *c, SzRect track, int on,
                               const SzTheme *theme) {
  float inset = scale_px(theme, 3.f);
  float thumb = track.h - 2.f * inset;
  float x = on ? track.x + track.w - inset - thumb : track.x + inset;
  paint_rect(c, track.x, track.y, track.w, track.h,
             on ? theme->primary : theme->surface);
  paint_border(c, track, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
  paint_rect(c, x, track.y + inset, thumb, thumb, theme->foreground);
}

static void paint_radio_mark(SkCanvas *c, SzRect box, int on,
                            const SzTheme *theme) {
  float inset = box.w / 6.f;
  paint_border(c, box, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
  if (on) {
    paint_rect(c, box.x + inset, box.y + inset, box.w - 2.f * inset,
               box.h - 2.f * inset, theme->primary);
    paint_rect(c, box.x + 2.f * inset, box.y + 2.f * inset,
               box.w - 4.f * inset, box.h - 4.f * inset, theme->foreground);
  }
}

static void paint_selection_tab(SkCanvas *c, SzRect frame, int on,
                                const SzTheme *theme) {
  float stroke = scale_px(theme, 2.f);
  paint_rect(c, frame.x, frame.y, frame.w, frame.h,
             on ? theme->primary : theme->surface);
  paint_border(c, frame, (int)(stroke + 0.5f), theme->border);
  if (on) {
    float rail = fminf(scale_px(theme, 4.f), frame.h);
    float corner = fminf(scale_px(theme, 8.f), fminf(frame.w, frame.h) * 0.25f);
    float step = scale_px(theme, 1.f);
    paint_rect(c, frame.x, frame.y + frame.h - rail, frame.w, rail, theme->border);
    for (float y = 0.f; y < corner; y += step) {
      float width = corner - y;
      paint_rect(c, frame.x + frame.w - width, frame.y + y, width,
                 fminf(step, corner - y), theme->border);
    }
  }
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
    paint_rect(c, floorf(x), floorf(y0), 1.f, 1.f, argb);
    paint_rect(c, floorf(x), floorf(y1), 1.f, 1.f, argb);
  }
}

/* Clockwise from the top edge. `frac` is 0-1 of the perimeter. */
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
  p = sk_paint_new();
  if (!p)
    return;
  sk_paint_set_color(p, sk_color_argb(apply_paint_alpha(argb)));
  sk_paint_set_text_size(p, font_px);
  sk_canvas_draw_string(c, s ? s : "", x, y, p);
  sk_paint_delete(p);
}

typedef struct TextInterval { float left, right; } TextInterval;

static int compare_text_intervals(const void *a, const void *b) {
  float x = ((const TextInterval *)a)->left, y = ((const TextInterval *)b)->left;
  return x < y ? -1 : x > y ? 1 : 0;
}

static void paint_text_range(SkCanvas *canvas, const char *source, const float *positions,
  int length, int start, int end, float x, float y, float height, uint32_t color) {
  if (end <= start) return;
  TextInterval *ranges = sz_alloc((size_t)(end - start) * sizeof(TextInterval));
  const float *trailing = positions + length + 1;
  int count = 0;
  for (int byte = start; byte < end;) {
    float left = fminf(positions[byte], trailing[byte]);
    float right = fmaxf(positions[byte], trailing[byte]);
    if (right > left) ranges[count++] = (TextInterval){left, right};
    byte += utf8_clen(source, byte);
  }
  qsort(ranges, (size_t)count, sizeof(TextInterval), compare_text_intervals);
  for (int i = 0; i < count; i++) {
    TextInterval range = ranges[i];
    while (i + 1 < count && ranges[i + 1].left <= range.right + .01f) {
      range.right = fmaxf(range.right, ranges[++i].right);
    }
    paint_rect(canvas, x + range.left, y, range.right - range.left, height, color);
  }
  sz_free(ranges);
}

static void paint_mono_string(SkCanvas *c, const char *s, float x, float y,
                              uint32_t argb, float font_px) {
  SkPaint *p;
  p = sk_paint_new();
  if (!p)
    return;
  sk_paint_set_color(p, sk_color_argb(apply_paint_alpha(argb)));
  sk_paint_set_text_size(p, font_px);
  sk_canvas_draw_mono_string(c, s ? s : "", x, y, p);
  sk_paint_delete(p);
}

static void paint_preedit_run(SkCanvas *c, const char *s, float x, float y,
                              float w, uint32_t argb, float font_px, int mono) {
  if (!s || !s[0] || w < 0.f)
    return;
  if (mono)
    paint_mono_string(c, s, x, y, argb, font_px);
  else
    paint_string(c, s, x, y, argb, font_px);
  if (w < 1.f)
    w = 1.f;
  paint_rect(c, x, y, w, 1.f, argb);
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
      col = theme->accent;
    } else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
               ch == '_') {
      tok_e = editor_ident_end(s, i, end);
      if (editor_is_kw(s, tok_s, tok_e))
        col = theme->accent;
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

static uint32_t editor_tok_color(int ty, const SzTheme *theme) {
  if (!theme)
    return 0;
  if (ty == 8 || ty == 12 || ty == 4 || ty == 5 || ty == 10 ||
      ty == 9 || ty == 1 || ty == 2 || ty == 3)
    return theme->accent;
  return theme->foreground;
}

static int editor_byte_at_cols(const char *s, int start, int end, int cols) {
  int i = start;
  int n = 0;
  if (!s || cols <= 0)
    return start;
  while (i < end && n < cols) {
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

static void paint_editor_line_lsp(SkCanvas *c, const SzView *v, const char *s,
                                 int line0, int start, int end, float base_x,
                                 float baseline, float font_px, float scroll_x,
                                 float text_w, const SzTheme *theme) {
  int t;
  int i = start;
  if (!theme)
    return;
  if (!v || v->tok_n <= 0) {
    paint_editor_line_hl(c, s, start, end, base_x, baseline, font_px, scroll_x,
                         text_w, theme);
    return;
  }
  for (t = 0; t < v->tok_n; t++) {
    int tok_s;
    int tok_e;
    if (v->tok_line[t] != line0)
      continue;
    tok_s = editor_byte_at_cols(s, start, end, v->tok_col[t]);
    tok_e = editor_byte_at_cols(s, start, end, v->tok_col[t] + v->tok_len[t]);
    if (tok_s > i)
      paint_editor_token(c, s, start, i, tok_s, base_x, baseline, font_px,
                         scroll_x, text_w, theme->foreground);
    if (tok_e > tok_s)
      paint_editor_token(c, s, start, tok_s, tok_e, base_x, baseline, font_px,
                         scroll_x, text_w, editor_tok_color(v->tok_ty[t], theme));
    if (tok_e > i)
      i = tok_e;
  }
  if (i < end)
    paint_editor_token(c, s, start, i, end, base_x, baseline, font_px, scroll_x,
                       text_w, theme->foreground);
}

static void paint_editor_inlays(SkCanvas *c, const SzView *v, int line0,
                               float base_x, float baseline, float font_px,
                               float scroll_x, float text_w,
                               const SzTheme *theme) {
  int i;
  float cell;
  if (!v || !theme)
    return;
  cell = sk_font_mono_cell(font_px);
  if (cell < 1.f)
    cell = 1.f;
  for (i = 0; i < v->inlay_n; i++) {
    int col;
    int first_col;
    int vis_cols;
    if (v->inlay_line[i] != line0 || !v->inlay_label[i] || !v->inlay_label[i][0])
      continue;
    col = v->inlay_col[i];
    if (col < 0)
      col = 0;
    first_col = (int)(scroll_x / cell);
    if (first_col < 0)
      first_col = 0;
    vis_cols = (int)(text_w / cell) + 2;
    if (vis_cols < 1)
      vis_cols = 1;
    if (col + 1 <= first_col || col >= first_col + vis_cols)
      continue;
    paint_mono_string(c, v->inlay_label[i], base_x + (float)col * cell, baseline,
                      theme->muted, font_px);
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
#ifdef __EMSCRIPTEN__
  int selectable;
#endif
} SzWrapPaint;

static void paint_wrap_line(const char *s, int start, int end, float width,
                            void *env) {
  SzWrapPaint *wp = (SzWrapPaint *)env;
  char *tmp;
  int n;
  (void)width;
  if (wp->cap > 0 && wp->drawn >= wp->cap)
    return;
  n = end - start;
  if (n < 0)
    n = 0;
  tmp = sz_alloc((size_t)n + 4);
  if (n > 0)
    memcpy(tmp, s + start, (size_t)n);
  tmp[n] = '\0';
  int shortened = wp->ellipsis && wp->cap > 0 && wp->drawn == wp->cap - 1;
  if (shortened)
    ellipsize_to_width(tmp, (size_t)n + 4, wp->inner, wp->font_px);
  SkTextRange context = sk_text_slice(s, strlen(s), (size_t)start, (size_t)end);
  char *source = NULL;
  if (shortened && tmp[0]) {
    /* Keep hidden source characters in the paragraph context. */
    size_t insert = (size_t)start + strlen(tmp) - 3;
    source = sz_alloc(context.bytes + 4);
    memcpy(source, s, insert);
    memcpy(source + insert, "...", 3);
    memcpy(source + insert + 3, s + insert, context.bytes - insert + 1);
    context = sk_text_slice(source, context.bytes + 3, (size_t)start, insert + 3);
  } else if (shortened) context.end = context.start;
#ifdef __EMSCRIPTEN__
  if (wp->selectable) {
    int visible_end = source ? (int)context.end - 3 : shortened && !tmp[0] ? start : end;
    char saved = tmp[visible_end - start];
    tmp[visible_end - start] = 0;
    sz_web_text_line(s, context, wp->x, start, visible_end, tmp, wp->x, wp->y, width, wp->font_px,
                     wp->line_h, g_clip_on, g_clip.x, g_clip.y, g_clip.w, g_clip.h, wp->argb, 0, 0, 0);
    tmp[visible_end - start] = saved;
    if (source) {
      size_t count = context.end - context.start + 1;
      float *leading = sz_alloc(count * 2 * sizeof(float)), *trailing = leading + count;
      sk_font_text_positions(context, wp->font_px, 0, leading, trailing);
      float left = INFINITY, right = -INFINITY;
      for (int byte = visible_end - start; byte < visible_end - start + 3; byte++) {
        left = fminf(left, fminf(leading[byte], trailing[byte]));
        right = fmaxf(right, fmaxf(leading[byte], trailing[byte]));
      }
      sz_web_text_line(s, context, wp->x, visible_end, visible_end, "...", wp->x + left, wp->y,
        right - left, wp->font_px, wp->line_h, g_clip_on, g_clip.x, g_clip.y, g_clip.w, g_clip.h,
        wp->argb, 0, 1, 0);
      sz_free(leading);
    }
  }
#endif
  SkPaint *paint = sk_paint_new();
  if (paint) {
    sk_paint_set_color(paint, sk_color_argb(apply_paint_alpha(wp->argb)));
    sk_paint_set_text_size(paint, wp->font_px);
    sk_canvas_draw_span(wp->c, context, wp->x, wp->y, paint, 0);
    sk_paint_delete(paint);
  }
  sz_free(source);
  wp->y += wp->line_h;
  wp->drawn++;
  sz_free(tmp);
}


typedef struct SzRichPaint {
  SkCanvas *canvas;
  float x, y;
  uint32_t color;
  SzView *view;
#ifdef __EMSCRIPTEN__
  int selectable;
#endif
} SzRichPaint;

static void paint_rich_fragment(SkTextRange context, int start, int end,
  float x, float baseline, float width, float font, float line_height,
  int style, uint32_t color, int colored, int ellipsis, int action,
  float context_x, void *env) {
  const char *source = context.source;
  SzRichPaint *paint = env;
  (void)action;
  uint32_t ink = colored ? color : paint->color;
  SkPaint *p = sk_paint_new();
  sk_paint_set_color(p, sk_color_argb(apply_paint_alpha(ink)));
  sk_paint_set_text_size(p, font);
  if ((size_t)start == context.start && (size_t)end == context.end) {
    sk_canvas_draw_span(paint->canvas, context, paint->x + context_x, paint->y + baseline, p, style);
  } else if (width > 0.f) {
    SzView *root = paint->view;
    while (root->parent) root = root->parent;
    float context_width = sk_font_measure_span(context, font, style);
    /* Keep one glyph and underline mask at each pixel boundary. */
    float left = x - context_x <= .001f ? 0.f : roundf(paint->x + x);
    float right = x + width - context_x >= context_width - .001f ?
      root->viewport_w : roundf(paint->x + x + width);
    sk_canvas_save(paint->canvas);
    sk_canvas_clip_rect(paint->canvas, left, 0.f, fmaxf(0.f, right - left), root->viewport_h);
    sk_canvas_draw_span(paint->canvas, context, paint->x + context_x, paint->y + baseline, p, style);
    sk_canvas_restore(paint->canvas);
  }
  sk_paint_delete(p);
#ifdef __EMSCRIPTEN__
  if (paint->selectable) {
    SzString *display = ellipsis ? sz_string_from_cstr("...") :
      sz_string_from_bytes(source + start, (size_t)(end - start));
    sz_web_text_line(source, context, paint->x + context_x, start, end, sz_string_cstr(display), paint->x + x,
      paint->y + baseline, width, font, line_height, g_clip_on,
      g_clip.x, g_clip.y, g_clip.w, g_clip.h, ink, style, ellipsis,
      action > 0 && action <= paint->view->child_count ? sz_view_id(paint->view->children[action - 1]) : 0);
    sz_release(display);
  }
#else
  (void)source;
  (void)ellipsis;
  (void)width;
  (void)line_height;
#endif
}

static void paint_select_label(SkCanvas *canvas, const char *text, SzRect frame,
                                float reserve, const SzTheme *theme, uint32_t color);
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
  sk_canvas_save(c);
  sk_canvas_clip_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h);
  for (i = 0; i < v->child_count; i++)
    paint_node(v->children[i], c, theme);
  sk_canvas_restore(c);
  g_clip = prev;
  g_clip_on = prev_on;
}

/* Round the inverse bounds outward. The device clip stays exact. */
static SzRect inverse_scale_clip(SzRect rect, float scale, float x, float y) {
  if (scale == 1) return rect;
  double left = x + ((double)rect.x - x) / scale;
  double top = y + ((double)rect.y - y) / scale;
  double right = x + ((double)rect.x + rect.w - x) / scale;
  double bottom = y + ((double)rect.y + rect.h - y) / scale;
  float x0 = nextafterf((float)left, -INFINITY);
  float y0 = nextafterf((float)top, -INFINITY);
  float x1 = nextafterf((float)right, INFINITY);
  float y1 = nextafterf((float)bottom, INFINITY);
  return (SzRect){x0, y0, x1 - x0, y1 - y0};
}

static void paint_node(SzView *v, SkCanvas *c, const SzTheme *theme) {
  char buf[256];
  int i;
  float tx, ty;
  int opacity_save;

  if (!v || !c || !view_is_shown(v))
    return;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return;

  SzTheme scoped;
  theme = scope_theme(v, theme, &scoped);

  opacity_save = g_opacity;
  if (motion_appear_pct(v) < 100)
    g_opacity = (opacity_save * motion_appear_pct(v)) / 100;

#ifdef __EMSCRIPTEN__
  int in_modal = web_modal_scope == NULL;
  for (SzView *p = v; p; p = p->parent) if (p == web_modal_scope) in_modal = 1;
  int excluded = !in_modal;
  for (SzView *p = v; p; p = p->parent)
    if (p->kind == SZ_VIEW_EXCLUDE_SEMANTICS || p->kind == SZ_VIEW_IGNORE_POINTER)
      excluded = 1;
  int web_group = !excluded && (v->a11y_role == SZ_A11Y_SEMANTICS || v->a11y_role == SZ_A11Y_APP_BAR ||
    v->a11y_role == SZ_A11Y_TAB_LIST || v->a11y_role == SZ_A11Y_TAB_PANEL ||
    v->a11y_role == SZ_A11Y_BREADCRUMB || v->a11y_role == SZ_A11Y_RADIO_GROUP ||
    v->a11y_role == SZ_A11Y_REGION ||
    v->kind == SZ_VIEW_OVERLAY);
  if (!excluded && v->kind == SZ_VIEW_TOOLTIP && v->text && v->text[0])
    sz_web_tooltip(v, v->text, v->tooltip_frame, v->tooltip_open);
  if (web_group) {
    SzView *related = NULL;
    if (v->a11y_role == SZ_A11Y_REGION && v->parent && v->parent->kind == SZ_VIEW_EXPANSION_TILE) related = v->parent;
    if (v->a11y_role == SZ_A11Y_TAB_PANEL) {
      SzView *book = v->parent->parent;
      related = book->children[0]->children[0]->children[0]->children[sz_signal_int_get(book->sig_int)];
    }
    SzRect frame = v->a11y_role == SZ_A11Y_TAB_PANEL ? v->parent->frame : v->frame;
    sz_web_group_begin(v, v->a11y_role, sz_view_a11y_label(v), related, 0,
                       frame.x, frame.y, frame.w, frame.h);
  }
  if (!excluded && ((v->interactive && v->kind != SZ_VIEW_OVERLAY && v->kind != SZ_VIEW_SPLIT && v->kind != SZ_VIEW_SELECT) || v->kind == SZ_VIEW_IMAGE ||
                    v->kind == SZ_VIEW_ICON || v->kind == SZ_VIEW_PROGRESS || v->kind == SZ_VIEW_CIRCULAR_PROGRESS)) {
    int checked = v->kind == SZ_VIEW_EXPANSION_TILE ? v->sig_int && sz_signal_int_get(v->sig_int) != 0 :
      v->kind != SZ_VIEW_SLIDER && v->kind != SZ_VIEW_PROGRESS && v->kind != SZ_VIEW_CIRCULAR_PROGRESS && v->sig_int ? (int)sz_signal_int_get(v->sig_int) : 0;
    if (v->kind == SZ_VIEW_CHOICE_CHIP || v->kind == SZ_VIEW_RADIO || v->kind == SZ_VIEW_RADIO_LIST_TILE)
      checked = v->sig_int && sz_signal_int_get(v->sig_int) == v->radio_value;
    SzView *related = NULL;
    if (v->a11y_role == SZ_A11Y_TAB) {
      SzView *tabs = v->parent->parent->parent->parent;
      related = tabs->children[v->radio_value + 1]->children[0];
    }
    if (v->kind == SZ_VIEW_EXPANSION_TILE && v->child_count > 0) related = v->children[0];
    SzRect control = control_layout_frame(v);
    sz_web_control(v, v->a11y_role, sz_view_a11y_label(v),
      v->route_id && v->route_id[0] ? v->route_id : NULL,
      v->copy_text, v->sig_str ? sz_signal_str_get(v->sig_str) : "", related, checked,
      control.x, control.y, control.w, control.h,
      g_clip_on, g_clip.x, g_clip.y, g_clip.w, g_clip.h,
      theme->foreground, theme->selection, theme->font_px, text_line_h(theme, theme->font_px),
      scale_px(theme, k_text_field_inset), v->kind == SZ_VIEW_EDITOR ? editor_gutter_w(v, theme) : 0.f);
  }
#endif

  switch (v->kind) {
  case SZ_VIEW_TEXT: {
    if (v->styled) {
      SzRichPaint paint = {.canvas = c, .view = v, .x = v->frame.x + scale_px(theme, 2.f),
        .y = v->frame.y, .color = g_text_color_on ? g_text_argb : theme->foreground};
#ifdef __EMSCRIPTEN__
      paint.selectable = !excluded;
      for (SzView *parent = v->parent; parent; parent = parent->parent)
        if ((parent->interactive && !v->child_count && parent->kind != SZ_VIEW_OVERLAY && parent->kind != SZ_VIEW_SPLIT && parent->kind != SZ_VIEW_EXPANSION_TILE) || parent->kind == SZ_VIEW_EXCLUDE_SEMANTICS) paint.selectable = 0;
      int level = 0;
      for (SzView *parent = v->parent; parent; parent = parent->parent)
        if (parent->heading_level) { level = parent->heading_level; break; }
      if (paint.selectable) sz_web_text_begin(v, sz_text_source(v->styled), level,
        paint.color, theme->selection);
#endif
      float inset = scale_px(theme, 4.f);
      sz_text_layout(v->styled, layout_font_px(theme), theme_px_scale(theme),
        scale_px(theme, theme->line_gap), v->frame.w > 0.f ? fmaxf(.001f, v->frame.w - inset) : 0.f,
        text_line_cap(), g_ellipsis, paint_rich_fragment, &paint);
      for (int i = 0; i < v->child_count; i++) {
        SzView *action = v->children[i];
        if (!view_is_shown(action)) continue;
        if (action->focused || action->pressed) {
          for (int j = 0; j < action->action_count; j++) {
            SzRect focus = action->action_boxes[j];
            while (j + 1 < action->action_count) {
              SzRect next = action->action_boxes[j + 1];
              if (fabsf(focus.x + focus.w - next.x) > .1f || next.y >= focus.y + focus.h || focus.y >= next.y + next.h) break;
              float bottom = fmaxf(focus.y + focus.h, next.y + next.h);
              focus.w = next.x + next.w - focus.x;
              focus.y = fminf(focus.y, next.y);
              focus.h = bottom - focus.y;
              j++;
            }
            float edge = scale_px(theme, 1.f);
            focus.x -= edge; focus.y -= edge;
            focus.w += edge * 2; focus.h += edge * 2;
            paint_border(c, focus, (int)fmaxf(1.f, edge), theme->foreground);
          }
        }
#ifdef __EMSCRIPTEN__
        if (!excluded && paint.selectable) {
          SzRect frame = action->frame;
          sz_web_control(action, action->a11y_role, sz_view_a11y_label(action), NULL, NULL, "", NULL, 0,
            frame.x, frame.y, frame.w, frame.h, g_clip_on, g_clip.x, g_clip.y, g_clip.w, g_clip.h,
            theme->foreground, theme->selection, theme->font_px, text_line_h(theme, theme->font_px), 0, 0);
          sz_web_inline_action(v, action->action_boxes, action->action_count);
        }
#endif
      }
      break;
    }
    SzWrapPaint wp;
    float inner = 0.f;
    float font_px = layout_font_px(theme);
    const char *content = text_content(v, buf, sizeof buf);
    if (v->frame.w > scale_px(theme, 4.f))
      inner = v->frame.w - scale_px(theme, 4.f);
    wp.c = c;
    wp.x = v->frame.x + scale_px(theme, 2.f);
    float baseline;
    float line_h = plain_text_metrics(content, theme, font_px, &baseline);
    wp.y = v->frame.y + baseline;
    wp.font_px = font_px;
    wp.line_h = line_h;
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
      each_text_line(content, font_px, inner, accum_wrap_line, &m);
      wp.ellipsis = m.truncated;
    }
#ifdef __EMSCRIPTEN__
    wp.selectable = !excluded;
    for (SzView *parent = v->parent; parent; parent = parent->parent)
      if ((parent->interactive && parent->kind != SZ_VIEW_OVERLAY && parent->kind != SZ_VIEW_SPLIT && parent->kind != SZ_VIEW_EXPANSION_TILE) || parent->kind == SZ_VIEW_EXCLUDE_SEMANTICS) wp.selectable = 0;
    int level = 0;
    for (SzView *parent = v->parent; parent; parent = parent->parent)
      if (parent->heading_level) { level = parent->heading_level; break; }
    if (wp.selectable) sz_web_text_begin(v, content, level, theme->foreground, theme->selection);
#endif
    each_text_line(content, font_px, inner, paint_wrap_line, &wp);
    break;
  }
  case SZ_VIEW_SELECT: {
    SzRect frame = v->frame;
    paint_rect(c, frame.x, frame.y, frame.w, frame.h, theme->surface);
    paint_border(c, frame, (int)fmaxf(1.f, theme_px_scale(theme)), theme->border);
    const char *value = sz_view_select_text(v);
    uint32_t color = v->select_count ? theme->foreground : theme->muted;
    paint_select_label(c, value[0] ? value : v->a11y_label, frame, theme->control_h * .5f, theme, color);
    float size = scale_px(theme, 4.f);
    float cx = frame.x + frame.w - theme->pad - size;
    float cy = frame.y + frame.h * .5f;
    for (int j = 0; j < 4; j++) {
      float step = scale_px(theme, (float)j);
      paint_rect(c, cx - size + step, cy - size * .5f + step, size * 2.f - step * 2.f, theme_px_scale(theme), color);
    }
#ifdef __EMSCRIPTEN__
    if (!excluded) {
      sz_web_control(v, SZ_A11Y_COMBOBOX, v->a11y_label, NULL, NULL, value,
        sz_view_select_popup(v), v->select_open, frame.x, frame.y, frame.w, frame.h,
        g_clip_on, g_clip.x, g_clip.y, g_clip.w, g_clip.h,
        color, theme->selection, theme->font_px, text_line_h(theme, theme->font_px), 0, 0);
      sz_web_select_state(v, sz_view_select_popup(v), sz_view_select_active_option(v), v->select_count);
    }
#endif
    break;
  }
  case SZ_VIEW_BUTTON: {
    SzRect face = v->frame;
    float offset = scale_px(theme, 3.f);
    if (offset > face.w * 0.25f)
      offset = face.w * 0.25f;
    if (offset > face.h * 0.25f)
      offset = face.h * 0.25f;
    resolve_text(v, buf, sizeof buf);
    if (text_width(buf, theme->font_px) > v->frame.w - theme->pad * 2.f)
      ellipsize_to_width(buf, sizeof buf, v->frame.w - theme->pad * 2.f,
                        theme->font_px);
    /* The shadow stays inside the control bounds. */
    paint_rect(c, face.x + offset, face.y + offset,
               face.w - offset, face.h - offset, theme->border);
    face.w -= offset;
    face.h -= offset;
    {
      float shift = 0.f;
      if (v->pressed)
        shift = offset;
      if (v->motion == SZ_MOTION_PRESS && v->motion_left > 0) {
        float step = offset * (float)v->motion_left / (float)SZ_MOTION_PUMPS;
        if (step > shift)
          shift = step;
      }
      face.x += shift;
      face.y += shift;
    }
    paint_rect(c, face.x, face.y, face.w, face.h, theme->primary);
    paint_border(c, face, (int)(scale_px(theme, 1.f) + 0.5f),
                 v->motion == SZ_MOTION_PRESS && v->motion_left > 0
                     ? theme->accent
                     : theme->border);
    tx = face.x + theme->pad;
    ty = face.y + (face.h + theme->font_px) * 0.5f;
    paint_string(c, buf, tx, ty, theme->on_primary, theme->font_px);
    break;
  }
  case SZ_VIEW_OUTLINED_BUTTON: {
    SzRect br = v->frame;
    resolve_text(v, buf, sizeof buf);
    if (text_width(buf, theme->font_px) > v->frame.w - theme->pad * 2.f)
      ellipsize_to_width(buf, sizeof buf, v->frame.w - theme->pad * 2.f,
                        theme->font_px);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    paint_border(c, br, (int)(scale_px(theme, 1.f) + 0.5f), theme->border);
    tx = v->frame.x + theme->pad;
    ty = v->frame.y + (v->frame.h + theme->font_px) * 0.5f;
    paint_string(c, buf, tx, ty, theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_TEXT_BUTTON:
    resolve_text(v, buf, sizeof buf);
    if (text_width(buf, theme->font_px) > v->frame.w - theme->pad * 2.f)
      ellipsize_to_width(buf, sizeof buf, v->frame.w - theme->pad * 2.f,
                        theme->font_px);
    tx = v->frame.x + theme->pad;
    ty = v->frame.y + (v->frame.h + theme->font_px) * 0.5f;
    paint_string(c, buf, tx, ty, theme->foreground, theme->font_px);
    break;
  case SZ_VIEW_LINK: {
    float underline;
    float inset = view_in_breadcrumb(v) ? scale_px(theme, 2.f) : theme->pad;
    resolve_text(v, buf, sizeof buf);
    if (text_width(buf, theme->font_px) > v->frame.w - inset * 2.f)
      ellipsize_to_width(buf, sizeof buf, v->frame.w - inset * 2.f,
                        theme->font_px);
    tx = v->frame.x + inset;
    ty = v->frame.y + (v->frame.h + theme->font_px) * 0.5f;
    paint_string(c, buf, tx, ty, theme->accent, theme->font_px);
    underline = text_width(buf, theme->font_px);
    paint_rect(c, tx, ty + scale_px(theme, 2.f), underline, scale_px(theme, 1.f),
               theme->accent);
    break;
  }
  case SZ_VIEW_NAV_TILE: {
    char g[2] = {v->glyph, '\0'};
    SzRect br = v->frame;
    resolve_text(v, buf, sizeof buf);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    paint_border(c, br, (int)(scale_px(theme, 1.f) + 0.5f), theme->border);
    paint_string(c, g, v->frame.x + theme->pad,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f, theme->accent,
                 theme->font_px);
    tx = v->frame.x + theme->pad + theme->font_px + layout_gap(theme);
    ty = v->frame.y + (v->frame.h + theme->font_px) * 0.5f;
    if (text_width(buf, theme->font_px) > v->frame.w - (tx - v->frame.x) - theme->pad)
      ellipsize_to_width(buf, sizeof buf,
                        v->frame.w - (tx - v->frame.x) - theme->pad, theme->font_px);
    paint_string(c, buf, tx, ty, theme->foreground, theme->font_px);
    break;
  }
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
    float box = theme->font_px + scale_px(theme, 4.f);
    float gap;
    float bx, by;
    int on;
    SzRect br;
    if (box < scale_px(theme, 12.f))
      box = scale_px(theme, 12.f);
    if (box > fmaxf(0.f, theme->control_h - scale_px(theme, 4.f)))
      box = fmaxf(0.f, theme->control_h - scale_px(theme, 4.f));
    gap = layout_gap(theme);
    bx = v->frame.x;
    by = v->frame.y + (v->frame.h - box) * 0.5f;
    br.x = bx;
    br.y = by;
    br.w = box;
    br.h = box;
    on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    paint_check(c, br, on, theme);
    resolve_text(v, buf, sizeof buf);
    float label_w = v->frame.x + v->frame.w - 0.f - bx - box - gap;
    if (text_width(buf, theme->font_px) > label_w)
      ellipsize_to_width(buf, sizeof buf, label_w, theme->font_px);
    paint_string(c, buf, bx + box + gap,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_SWITCH: {
    float box = theme->font_px + scale_px(theme, 4.f);
    float gap;
    float bx, by;
    float tw, th;
    int on;
    if (box < scale_px(theme, 12.f))
      box = scale_px(theme, 12.f);
    if (box > fmaxf(0.f, theme->control_h - scale_px(theme, 4.f)))
      box = fmaxf(0.f, theme->control_h - scale_px(theme, 4.f));
    gap = layout_gap(theme);
    tw = box * 2.f;
    th = box;
    bx = v->frame.x;
    by = v->frame.y + (v->frame.h - th) * 0.5f;
    on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    paint_switch_track(c, (SzRect){bx, by, tw, th}, on, theme);
    resolve_text(v, buf, sizeof buf);
    float label_w = v->frame.x + v->frame.w - 0.f - bx - tw - gap;
    if (text_width(buf, theme->font_px) > label_w)
      ellipsize_to_width(buf, sizeof buf, label_w, theme->font_px);
    paint_string(c, buf, bx + tw + gap,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_CHIP: {
    int on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    SzRect br = v->frame;
    resolve_text(v, buf, sizeof buf);
    if (text_width(buf, theme->font_px) > v->frame.w - 2.f * theme->pad)
      ellipsize_to_width(buf, sizeof buf, v->frame.w - 2.f * theme->pad, theme->font_px);
    paint_selection_tab(c, br, on, theme);
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 on ? theme->on_primary : theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_CHOICE_CHIP: {
    int on = v->sig_int && sz_signal_int_get(v->sig_int) == v->radio_value;
    SzRect br = v->frame;
    resolve_text(v, buf, sizeof buf);
    if (text_width(buf, theme->font_px) > v->frame.w - 2.f * theme->pad)
      ellipsize_to_width(buf, sizeof buf, v->frame.w - 2.f * theme->pad, theme->font_px);
    paint_selection_tab(c, br, on, theme);
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 on ? theme->on_primary : theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_ACTION_CHIP: {
    SzRect br = v->frame;
    resolve_text(v, buf, sizeof buf);
    if (text_width(buf, theme->font_px) > v->frame.w - 2.f * theme->pad)
      ellipsize_to_width(buf, sizeof buf, v->frame.w - 2.f * theme->pad, theme->font_px);
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
    float box = theme->font_px + scale_px(theme, 4.f);
    float gap;
    float bx, by;
    if (box < scale_px(theme, 12.f))
      box = scale_px(theme, 12.f);
    if (box > fmaxf(0.f, theme->control_h - scale_px(theme, 4.f)))
      box = fmaxf(0.f, theme->control_h - scale_px(theme, 4.f));
    gap = layout_gap(theme);
    resolve_text(v, buf, sizeof buf);
    if (text_width(buf, theme->font_px) > v->frame.w - 2.f * theme->pad - box - layout_gap(theme))
      ellipsize_to_width(buf, sizeof buf, v->frame.w - 2.f * theme->pad - box - layout_gap(theme), theme->font_px);
    paint_selection_tab(c, br, on, theme);
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
    float box = theme->font_px + scale_px(theme, 4.f);
    float bx, by;
    if (box < scale_px(theme, 12.f))
      box = scale_px(theme, 12.f);
    if (box > fmaxf(0.f, theme->control_h - scale_px(theme, 4.f)))
      box = fmaxf(0.f, theme->control_h - scale_px(theme, 4.f));
    resolve_text(v, buf, sizeof buf);
    if (text_width(buf, theme->font_px) > v->frame.w - 2.f * theme->pad - box - layout_gap(theme))
      ellipsize_to_width(buf, sizeof buf, v->frame.w - 2.f * theme->pad - box - layout_gap(theme), theme->font_px);
    paint_selection_tab(c, br, on, theme);
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
    float label_end = v->frame.x + v->frame.w - theme->pad;
    if (v->child_count > 0)
      label_end = v->children[0]->frame.x - layout_gap(theme);
    float label_w = label_end - v->frame.x - theme->pad;
    if (text_width(buf, theme->font_px) > label_w)
      ellipsize_to_width(buf, sizeof buf, label_w, theme->font_px);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    break;
  }
  case SZ_VIEW_CHECKBOX_LIST_TILE: {
    float box = theme->font_px + scale_px(theme, 4.f);
    float gap;
    float bx, by;
    int on;
    SzRect br;
    if (box < scale_px(theme, 12.f))
      box = scale_px(theme, 12.f);
    if (box > fmaxf(0.f, theme->control_h - scale_px(theme, 4.f)))
      box = fmaxf(0.f, theme->control_h - scale_px(theme, 4.f));
    gap = layout_gap(theme);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    bx = v->frame.x + theme->pad;
    by = v->frame.y + (v->frame.h - box) * 0.5f;
    br.x = bx;
    br.y = by;
    br.w = box;
    br.h = box;
    on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    paint_check(c, br, on, theme);
    resolve_text(v, buf, sizeof buf);
    float label_w = v->frame.x + v->frame.w - theme->pad - bx - box - gap;
    if (text_width(buf, theme->font_px) > label_w)
      ellipsize_to_width(buf, sizeof buf, label_w, theme->font_px);
    paint_string(c, buf, bx + box + gap,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_SWITCH_LIST_TILE: {
    float box = theme->font_px + scale_px(theme, 4.f);
    float tw, th;
    float bx, by;
    int on;
    if (box < scale_px(theme, 12.f))
      box = scale_px(theme, 12.f);
    if (box > fmaxf(0.f, theme->control_h - scale_px(theme, 4.f)))
      box = fmaxf(0.f, theme->control_h - scale_px(theme, 4.f));
    tw = box * 2.f;
    th = box;
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    bx = v->frame.x + v->frame.w - theme->pad - tw;
    if (bx < v->frame.x + theme->pad)
      bx = v->frame.x + theme->pad;
    by = v->frame.y + (v->frame.h - th) * 0.5f;
    on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
    paint_switch_track(c, (SzRect){bx, by, tw, th}, on, theme);
    resolve_text(v, buf, sizeof buf);
    if (text_width(buf, theme->font_px) > bx - v->frame.x - theme->pad - theme->gap)
      ellipsize_to_width(buf, sizeof buf, bx - v->frame.x - theme->pad - theme->gap,
                        theme->font_px);
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_RADIO_LIST_TILE: {
    float box = theme->font_px + scale_px(theme, 4.f);
    float gap;
    float bx, by;
    int on;
    SzRect br;
    if (box < scale_px(theme, 12.f))
      box = scale_px(theme, 12.f);
    if (box > fmaxf(0.f, theme->control_h - scale_px(theme, 4.f)))
      box = fmaxf(0.f, theme->control_h - scale_px(theme, 4.f));
    gap = layout_gap(theme);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    bx = v->frame.x + theme->pad;
    by = v->frame.y + (v->frame.h - box) * 0.5f;
    br.x = bx;
    br.y = by;
    br.w = box;
    br.h = box;
    on = v->sig_int && sz_signal_int_get(v->sig_int) == v->radio_value;
    paint_radio_mark(c, br, on, theme);
    resolve_text(v, buf, sizeof buf);
    float label_w = v->frame.x + v->frame.w - theme->pad - bx - box - gap;
    if (text_width(buf, theme->font_px) > label_w)
      ellipsize_to_width(buf, sizeof buf, label_w, theme->font_px);
    paint_string(c, buf, bx + box + gap,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_SEGMENTED:
    for (i = 0; i < v->child_count; i++) paint_node(v->children[i], c, theme);
    break;
  case SZ_VIEW_BADGE: {
    SzView *child = v->child_count ? v->children[0] : NULL;
    float gap = fminf(scale_px(theme, 4.f), v->frame.w * 0.1f);
    float left = child ? child->frame.x + child->frame.w + gap : v->frame.x;
    float height = fminf(scale_px(theme, 24.f), v->frame.h);
    SzRect ticket = {left, v->frame.y + (v->frame.h - height) * 0.5f,
                     fmaxf(0.f, v->frame.x + v->frame.w - left), height};
    int64_t n = v->sig_int ? sz_signal_int_get(v->sig_int) : 0;
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    paint_rect(c, ticket.x, ticket.y, ticket.w, ticket.h, theme->primary);
    paint_border(c, ticket, (int)(scale_px(theme, 1.f) + 0.5f), theme->border);
    if (n > 99)
      snprintf(buf, sizeof buf, "99+");
    else
      snprintf(buf, sizeof buf, "%lld", (long long)(n < 0 ? 0 : n));
    float available = ticket.w - scale_px(theme, 8.f);
    if (text_width(buf, theme->font_px) > available)
      ellipsize_to_width(buf, sizeof buf, available, theme->font_px);
    paint_string(c, buf, ticket.x + (ticket.w - text_width(buf, theme->font_px)) * 0.5f,
                 ticket.y + (ticket.h + theme->font_px) * 0.5f,
                 theme->on_primary, theme->font_px);
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
    float mark_w = text_width(">", theme->font_px);
    float label_w = v->frame.w - 2.f * theme->pad - mark_w - layout_gap(theme);
    if (text_width(buf, theme->font_px) > label_w)
      ellipsize_to_width(buf, sizeof buf, label_w, theme->font_px);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, hh, theme->surface);
    paint_string(c, buf, v->frame.x + theme->pad,
                 v->frame.y + (hh + theme->font_px) * 0.5f, theme->foreground,
                 theme->font_px);
    mark[0] = on ? 'v' : '>';
    mark[1] = '\0';
    paint_string(c, mark, v->frame.x + v->frame.w - theme->pad - mark_w,
                 v->frame.y + (hh + theme->font_px) * 0.5f, theme->muted,
                 theme->font_px);
#ifdef __EMSCRIPTEN__
    if (!excluded && !on && v->child_count > 0) {
      sz_web_group_begin(v->children[0], SZ_A11Y_REGION, sz_view_a11y_label(v), v, 1, 0, 0, 0, 0);
      sz_web_group_end();
    }
#endif
    paint_children_clipped(v, c, theme);
    break;
  }
  case SZ_VIEW_RADIO: {
    float box = theme->font_px + scale_px(theme, 4.f);
    float gap;
    float bx, by;
    int on;
    SzRect br;
    if (box < scale_px(theme, 12.f))
      box = scale_px(theme, 12.f);
    if (box > fmaxf(0.f, theme->control_h - scale_px(theme, 4.f)))
      box = fmaxf(0.f, theme->control_h - scale_px(theme, 4.f));
    gap = layout_gap(theme);
    bx = v->frame.x;
    by = v->frame.y + (v->frame.h - box) * 0.5f;
    br.x = bx;
    br.y = by;
    br.w = box;
    br.h = box;
    on = v->sig_int && sz_signal_int_get(v->sig_int) == v->radio_value;
    paint_radio_mark(c, br, on, theme);
    resolve_text(v, buf, sizeof buf);
    float label_w = v->frame.x + v->frame.w - 0.f - bx - box - gap;
    if (text_width(buf, theme->font_px) > label_w)
      ellipsize_to_width(buf, sizeof buf, label_w, theme->font_px);
    paint_string(c, buf, bx + box + gap,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 theme->foreground, theme->font_px);
    break;
  }
  case SZ_VIEW_SLIDER: {
    float track_h = scale_px(theme, 4.f);
    float thumb = scale_px(theme, 12.f);
    float n;
    float tx;
    float ty;
    float tw;
    if (v->frame.w < thumb)
      thumb = v->frame.w;
    n = (float)slider_fraction(v);
    tw = v->frame.w > thumb ? v->frame.w - thumb : 0.f;
    tx = v->frame.x + n * tw;
    ty = v->frame.y + (v->frame.h - thumb) * 0.5f;
    paint_rect(c, v->frame.x, v->frame.y + (v->frame.h - track_h) * 0.5f,
               v->frame.w, track_h, theme->muted);
    paint_rect(c, v->frame.x, v->frame.y + (v->frame.h - track_h) * 0.5f,
               tx - v->frame.x + thumb * 0.5f, track_h, theme->primary);
    paint_rect(c, tx, ty, thumb, thumb, theme->primary);
    paint_border(c, (SzRect){tx, ty, thumb, thumb},
                 (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    break;
  }
  case SZ_VIEW_PROGRESS: {
    float n = (float)sz_view_progress_value(v);
    SzRect fill = v->frame;
    int edge = (int)(scale_px(theme, 1.f) + 0.5f);
    fill.w *= n / 100.f;
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    paint_border(c, v->frame, edge, theme->border);
    if (fill.w > 0.f) {
      paint_rect(c, fill.x, fill.y, fill.w, fill.h, theme->primary);
      paint_border(c, fill, edge * 2, theme->border);
    }
    break;
  }
  case SZ_VIEW_CIRCULAR_PROGRESS: {
    float n = (float)sz_view_progress_value(v);
    float track = fminf(scale_px(theme, 2.f), fminf(v->frame.w, v->frame.h) * 0.5f);
    float fill = fminf(scale_px(theme, 6.f), fminf(v->frame.w, v->frame.h) * 0.5f);
    paint_border(c, v->frame, (int)(track + 0.5f), theme->muted);
    paint_ring_frac(c, v->frame, fill, n / 100.f, theme->foreground);
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
    SzRect prev_clip = g_clip;
    int prev_on = g_clip_on;
    SzRect next = v->frame;
    if (g_clip_on && !rects_intersect(g_clip, v->frame, &next)) { next.w = 0.f; next.h = 0.f; }
    g_clip = next;
    g_clip_on = 1;
    sk_canvas_save(c);
    sk_canvas_clip_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    paint_border(c, v->frame, (int)(scale_px(theme, v->focused ? 2.f : 1.f) + .5f),
      v->focused ? theme->foreground : theme->border);
    int caret, pre_start, pre_end;
    char *display = field_display(v, &caret, &pre_start, &pre_end);
    int length = (int)strlen(display);
    float *positions = text_positions(display, length, theme->font_px);
    float base_x = v->frame.x + scale_px(theme, k_text_field_inset);
    float base_y = v->frame.y + (v->frame.h + theme->font_px) * .5f;
    float height = fmaxf(1.f, fminf(theme->font_px, v->frame.h - scale_px(theme, 4.f)));
    if (pre_start == pre_end && field_has_sel(v)) {
      int a, b;
      field_sel_bounds(v, &a, &b);
      paint_text_range(c, display, positions, length, a, b, base_x,
        v->frame.y + (v->frame.h - height) * .5f, height, theme->selection);
    }
    paint_string(c, display[0] ? display : (v->placeholder ? v->placeholder : ""),
      base_x, base_y, display[0] ? theme->foreground : theme->muted, theme->font_px);
    paint_text_range(c, display, positions, length, pre_start, pre_end, base_x,
      base_y + scale_px(theme, 2.f), scale_px(theme, 1.f), theme->foreground);
    sz_free(positions);
    sz_free(display);
    if (v->focused) {
      SzRect cursor = sz_view_caret_rect(v, theme);
      if (cursor.w > 0.f) paint_rect(c, floorf(cursor.x), cursor.y, cursor.w, cursor.h, theme->foreground);
    }
    sk_canvas_restore(c);
    g_clip = prev_clip;
    g_clip_on = prev_on;
    break;
  }
  case SZ_VIEW_EDITOR: {
    const char *s = field_cstr(v);
    int n, i, line, a, b;
    float font_px = theme->font_px;
    float line_h = text_line_h(theme, font_px);
    float inset = scale_px(theme, k_text_field_inset);
    float gutter = editor_gutter_w(v, theme);
    float text_w = v->frame.w - gutter;
    float base_x = v->frame.x + gutter + inset - scale_px(theme, v->scroll_x);
    float base_y = v->frame.y + inset - scale_px(theme, v->scroll_y);
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
    sk_canvas_save(c);
    sk_canvas_clip_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    if (gutter > 0.f)
      paint_rect(c, v->frame.x, v->frame.y, gutter, v->frame.h, theme->background);
    paint_border(c, v->frame,
                 (int)(scale_px(theme, v->focused ? 2.f : 1.f) + 0.5f),
                 v->focused ? theme->foreground : theme->border);
    field_sel_bounds(v, &a, &b);
    {
      int first_vis;
      int last_vis;
      int vis;
      int br_a = -1;
      int br_b = -1;
      int has_br;
      if (line_h < 1.f)
        line_h = 1.f;
      first_vis = (int)(scale_px(theme, v->scroll_y) / line_h);
      if (first_vis < 0)
        first_vis = 0;
      last_vis = first_vis + (int)(v->frame.h / line_h) + 2;
      has_br = editor_bracket_pair(s, field_caret_clamped(v), &br_a, &br_b);
      line = 0;
      i = 0;
      vis = 0;
      for (;;) {
        int start = i;
        int end;
        float top;
        int hidden;
        while (i < n && s[i] != '\n')
          i++;
        end = i;
        hidden = editor_line_hidden(v, line);
        if (!hidden) {
          if (vis > last_vis)
            break;
          if (vis >= first_vis) {
            top = base_y + (float)vis * line_h;
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
                  paint_rect(c, x0, top, x1 - x0, h, theme->selection);
              }
            }
            if (has_br) {
              float cell = sk_font_mono_cell(font_px);
              float h = font_px < 1.f ? 1.f : font_px;
              if (cell < 1.f)
                cell = 1.f;
              if (br_a >= start && br_a < end)
                paint_rect(c, base_x + editor_span_width(s, start, br_a, font_px),
                           top, cell, h, theme->selection);
              if (br_b >= start && br_b < end && br_b != br_a)
                paint_rect(c, base_x + editor_span_width(s, start, br_b, font_px),
                           top, cell, h, theme->selection);
            }
            {
              char num[16];
              int sev = editor_diag_at(v, line + 1);
              int fold = editor_fold_at(v, line);
              snprintf(num, sizeof num, "%d", line + 1);
              paint_mono_string(c, num, v->frame.x + 2.f, top + font_px,
                                theme->muted, font_px);
              if (sev) {
                float mark = font_px < 4.f ? 2.f : 3.f;
                paint_rect(c, v->frame.x + gutter - mark - 1.f, top + 1.f, mark,
                           mark, theme->accent);
              }
              if (fold >= 0) {
                float mark = font_px < 4.f ? 2.f : 3.f;
                paint_rect(c, v->frame.x + 1.f, top + font_px * 0.35f, mark, mark,
                           theme->primary);
              }
            }
            {
              const char *pre = edit_preedit(v);
              int caret = field_caret_clamped(v);
              float tw = text_w > 8.f ? text_w : 8.f;
              if (pre[0] && caret >= start && caret <= end) {
                float pre_w =
                    editor_span_width(pre, 0, (int)strlen(pre), font_px);
                float caret_x =
                    base_x + editor_span_width(s, start, caret, font_px);
                paint_editor_line_lsp(c, v, s, line, start, caret, base_x,
                                     top + font_px, font_px, scale_px(theme, v->scroll_x), tw,
                                     theme);
                paint_preedit_run(c, pre, caret_x, top + font_px, pre_w,
                                  theme->foreground, font_px, 1);
                paint_editor_line_lsp(c, v, s, line, caret, end, base_x + pre_w,
                                     top + font_px, font_px, scale_px(theme, v->scroll_x), tw,
                                     theme);
              } else {
                paint_editor_line_lsp(c, v, s, line, start, end, base_x,
                                     top + font_px, font_px, scale_px(theme, v->scroll_x), tw,
                                     theme);
              }
            }
            paint_editor_inlays(c, v, line, base_x, top + font_px, font_px,
                               scale_px(theme, v->scroll_x), text_w > 8.f ? text_w : 8.f, theme);
          }
          vis++;
        }
        if (i >= n)
          break;
        i++;
        line++;
      }
    }
    if (v->focused) {
      SzRect caret = sz_view_caret_rect(v, theme);
      if (caret.w > 0.f)
        paint_rect(c, floorf(caret.x), caret.y, caret.w, caret.h, theme->foreground);
    }
    sk_canvas_restore(c);
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
  case SZ_VIEW_THEME:
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->background);
    for (i = 0; i < v->child_count; i++) paint_node(v->children[i], c, theme);
    break;
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
  case SZ_VIEW_IMAGE: {
    float iw = (float)sz_image_width(v->image);
    float ih = (float)sz_image_height(v->image);
    float scale = fminf(v->frame.w / iw, v->frame.h / ih);
    float width = iw * scale, height = ih * scale;
    float x = v->frame.x + (v->frame.w - width) * 0.5f;
    float y = v->frame.y + (v->frame.h - height) * 0.5f;
    float opacity = (float)g_opacity / 100.f;
    sk_canvas_save(c);
    if (g_clip_on)
      sk_canvas_clip_rect(c, g_clip.x, g_clip.y, g_clip.w, g_clip.h);
    if (g_radius_on && g_radius >= 0.5f) {
      float top = fmaxf(y, g_radius_rect.y);
      float bottom = fminf(y + height, g_radius_rect.y + g_radius_rect.h);
      for (float row = floorf(top); row < bottom; row += 1.f) {
        float left, right;
        if (!rrect_x_span(g_radius_rect, g_radius, row + 0.5f, &left, &right))
          continue;
        sk_canvas_save(c);
        sk_canvas_clip_rect(c, left, row, right - left, 1.f);
        sk_canvas_draw_image(c, (SkImage *)atomic_load(&v->image->bitmap), x, y, width, height, opacity);
        sk_canvas_restore(c);
      }
    } else {
      sk_canvas_draw_image(c, (SkImage *)atomic_load(&v->image->bitmap), x, y, width, height, opacity);
    }
    sk_canvas_restore(c);
    break;
  }
  case SZ_VIEW_SCALE:
  case SZ_VIEW_ANIMATED_SCALE: {
#ifdef __EMSCRIPTEN__
    if (v->kind == SZ_VIEW_ANIMATED_SCALE)
      sz_web_scale_transition(v, v->animation_value, v->animation_target,
        v->animation_duration - v->animation_step, v->motion_suppressed);
#endif
    float scale = view_scale(v);
    if (!(scale > 0)) break;
    SzRect frame = scale_pivot_frame(v), previous_clip = g_clip, previous_radius = g_radius_rect;
    float x = frame.x + frame.w * .5f, y = frame.y + frame.h * .5f;
    float radius = g_radius;
    sk_canvas_save(c);
    if (sk_canvas_scale(c, scale, x, y)) {
#ifdef __EMSCRIPTEN__
      sz_web_scale_begin(v, scale, x, y, g_clip_on, g_clip);
#endif
      if (g_clip_on) g_clip = inverse_scale_clip(g_clip, scale, x, y);
      if (g_radius_on) {
        g_radius_rect = (SzRect){x + (g_radius_rect.x - x) / scale,
          y + (g_radius_rect.y - y) / scale, g_radius_rect.w / scale, g_radius_rect.h / scale};
        g_radius /= scale;
      }
      for (i = 0; i < v->child_count; i++) paint_node(v->children[i], c, theme);
#ifdef __EMSCRIPTEN__
      sz_web_group_end();
#endif
    }
    sk_canvas_restore(c);
    g_clip = previous_clip; g_radius_rect = previous_radius; g_radius = radius;
    break;
  }
  case SZ_VIEW_ANIMATED_POSITION:
#ifdef __EMSCRIPTEN__
    sz_web_position_transition(v, v->animation_value, v->animation_axis2,
      v->animation_target, v->animation_target_axis2,
      v->animation_duration - v->animation_step, v->motion_suppressed);
#endif
    for (i = 0; i < v->child_count; i++) paint_node(v->children[i], c, theme);
    break;
  case SZ_VIEW_ANIMATED_CONTENT_SIZE:
#ifdef __EMSCRIPTEN__
    sz_web_content_size_transition(v, v->content_w, v->content_h,
      v->content_target_w, v->content_target_h,
      v->animation_duration - v->animation_step, v->motion_suppressed);
#endif
    if (!size_collapsed(v)) paint_children_clipped(v, c, theme);
    break;
  case SZ_VIEW_ANIMATED_SIZE:
#ifdef __EMSCRIPTEN__
    sz_web_size_transition(v, v->animation_value, v->animation_axis2,
      v->animation_target, v->animation_target_axis2,
      v->animation_duration - v->animation_step, v->motion_suppressed);
#endif
    if (!size_collapsed(v)) paint_children_clipped(v, c, theme);
    break;
  case SZ_VIEW_CLIP:
    paint_children_clipped(v, c, theme);
    break;
  case SZ_VIEW_REDUCED_MOTION:
    for (i = 0; i < v->child_count; i++) paint_node(v->children[i], c, theme);
    break;
  case SZ_VIEW_OPACITY:
  case SZ_VIEW_ANIMATED_OPACITY: {
#ifdef __EMSCRIPTEN__
    if (v->kind == SZ_VIEW_ANIMATED_OPACITY)
      sz_web_transition(v, v->animation_value, v->animation_target,
        v->animation_duration - v->animation_step, v->motion_suppressed);
#endif
    int prev = g_opacity;
    int pct = v->kind == SZ_VIEW_OPACITY ? v->img_w : v->animation_value;
    int alpha = (prev * pct * 255 + 5000) / 10000;
    int layer = alpha < 255;
    if (layer && !sk_canvas_save_layer_alpha(c, (uint8_t)alpha))
      sz_panic("Opacity layer allocation fails");
    g_opacity = 100;
    for (i = 0; i < v->child_count; i++) paint_node(v->children[i], c, theme);
    if (layer) sk_canvas_restore(c);
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
  case SZ_VIEW_GAP: {
    /* Paint must see the same gap as layout. Control label x uses it. */
    int prev_on = g_gap_on;
    float prev = g_gap;
    g_gap_on = 1;
    g_gap = scale_px(theme, (float)v->img_w);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    g_gap_on = prev_on;
    g_gap = prev;
    break;
  }
  case SZ_VIEW_SCROLL:
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    paint_children_clipped(v, c, theme);
    break;
  case SZ_VIEW_VIRTUAL_LIST:
  case SZ_VIEW_ADAPTIVE:
  case SZ_VIEW_COLUMN:
  case SZ_VIEW_ROW:
  case SZ_VIEW_WRAP:
  case SZ_VIEW_BREADCRUMB:
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
  case SZ_VIEW_TOOLTIP:
  case SZ_VIEW_ON_SECONDARY:
  case SZ_VIEW_FOCUS_GROUP:
  case SZ_VIEW_SECTION:
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
    if (v->kind == SZ_VIEW_BREADCRUMB) {
      for (i = 0; i + 1 < v->child_count; i++) {
        SzView *a = v->children[i];
        SzView *b = v->children[i + 1];
        float mid;
        float ay;
        float by;
        if (!view_is_shown(a) || !view_is_shown(b))
          continue;
        mid = (a->frame.x + a->frame.w + b->frame.x) * 0.5f;
        ay = a->frame.y + (a->frame.h + theme->font_px) * 0.5f;
        by = b->frame.y + (b->frame.h + theme->font_px) * 0.5f;
        paint_string(c, ">", mid - text_width(">", theme->font_px) * 0.5f,
                     (ay + by) * 0.5f, theme->muted, theme->font_px);
      }
    }
    break;
  case SZ_VIEW_APP_SHELL:
    paint_children_clipped(v, c, theme);
    break;
  case SZ_VIEW_APP_BAR:
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    paint_rect(c, v->frame.x, v->frame.y + v->frame.h - scale_px(theme, 2.f),
               v->frame.w, scale_px(theme, 2.f), theme->border);
    break;
  case SZ_VIEW_TABS:
  case SZ_VIEW_INDEX_BOOK: {
#ifdef __EMSCRIPTEN__
    if (sz_web_book_begin()) {
      web_route_book = v;
      for (i = 1; i < v->child_count; i++) {
        SzView *section = v->children[i]->children[0];
        sz_web_section(section->route_id, section->text, i - 1 == sz_signal_int_get(v->sig_int));
      }
    }
#endif
    SzRect nav = v->children[0]->frame;
    paint_rect(c, nav.x, nav.y, nav.w, nav.h, theme->surface);
    for (i = 0; i < v->child_count; i++) {
#ifdef __EMSCRIPTEN__
      /* Keep panel order stable so tab selection keeps browser focus. */
      if (!excluded && v->kind == SZ_VIEW_TABS && i > 0 && !view_is_shown(v->children[i])) {
        SzView *section = v->children[i]->children[0];
        SzView *tab = v->children[0]->children[0]->children[0]->children[i - 1];
        sz_web_group_begin(section, SZ_A11Y_TAB_PANEL, section->text, tab, 1, 0, 0, 0, 0);
        sz_web_group_end();
      } else
#endif
        paint_node(v->children[i], c, theme);
    }
    paint_border(c, nav, (int)(scale_px(theme, 2.f) + 0.5f), theme->border);
    /* A tab change paints a rust bar on the selected chip. It steps down. */
    if (v->kind == SZ_VIEW_TABS && v->motion == SZ_MOTION_TAB &&
        v->motion_left > 0 && v->sig_int) {
      SzView *nav_v = v->children[0];
      SzView *wrap = nav_v && nav_v->child_count > 0 &&
                             nav_v->children[0]->child_count > 0
                         ? nav_v->children[0]->children[0]
                         : NULL;
      int64_t sel = sz_signal_int_get(v->sig_int);
      if (wrap && sel >= 0 && sel < wrap->child_count) {
        SzRect chip = wrap->children[sel]->frame;
        float bh = scale_px(theme, (float)v->motion_left);
        if (bh > chip.h)
          bh = chip.h;
        if (bh > 0.f && chip.w > 0.f)
          paint_rect(c, chip.x, chip.y + chip.h - bh, chip.w, bh, theme->accent);
      }
    }
    break;
  }
  case SZ_VIEW_SPLIT: {
#ifdef __EMSCRIPTEN__
    if (!excluded && v->child_count > 0 && sz_view_split_frac(v) == 0) {
      sz_web_group_begin(v->children[0], SZ_A11Y_REGION, sz_view_a11y_label(v), NULL, 1, 0, 0, 0, 0);
      sz_web_group_end();
    }
#endif
    if (v->child_count > 0) paint_node(v->children[0], c, theme);
    SzRect handle = control_layout_frame(v);
    paint_rect(c, handle.x, handle.y, handle.w, handle.h, theme->muted);
#ifdef __EMSCRIPTEN__
    if (!excluded) sz_web_control(v, SZ_A11Y_SPLIT, sz_view_a11y_label(v), NULL,
      NULL, "", v->child_count > 0 ? v->children[0] : NULL, sz_view_split_frac(v),
      handle.x, handle.y, handle.w, handle.h, g_clip_on, g_clip.x, g_clip.y, g_clip.w, g_clip.h,
      theme->foreground, theme->selection, theme->font_px, text_line_h(theme, theme->font_px), 0, 0);
#endif
    if (v->child_count > 1) paint_node(v->children[1], c, theme);
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
  if (v->focused && (v->interactive || v->a11y_role == SZ_A11Y_TAB_PANEL) && !view_is_edit(v)) {
    SzRect focus = control_layout_frame(v);
    float inset = v->kind == SZ_VIEW_SPLIT ? 0.f : scale_px(theme, 4.f);
    focus.x += inset;
    focus.y += inset;
    focus.w -= inset * 2.f;
    focus.h -= inset * 2.f;
    paint_border(c, focus, (int)(scale_px(theme, 3.f) + 0.5f), theme->surface);
    paint_border(c, focus, (int)(scale_px(theme, 1.f) + 0.5f), theme->foreground);
  }
#ifdef __EMSCRIPTEN__
  if (web_group) sz_web_group_end();
#endif
  g_opacity = opacity_save;
  /* A new list row paints a rust bar. The bar does not change layout. */
  if (v->motion == SZ_MOTION_INSERT && v->motion_left > 0) {
    float bw = scale_px(theme, (float)(v->motion_left * 2));
    if (bw > v->frame.w)
      bw = v->frame.w;
    if (bw > 0.f && v->frame.h > 0.f)
      paint_rect(c, v->frame.x, v->frame.y, bw, v->frame.h, theme->accent);
  }
}

static void paint_select_label(SkCanvas *canvas, const char *text, SzRect frame,
                                float reserve, const SzTheme *theme, uint32_t color) {
  float pad = theme->pad;
  float width = fmaxf(0.f, frame.w - pad * 2.f - reserve);
  if (width <= 0.f) return;
  sk_canvas_save(canvas);
  sk_canvas_clip_rect(canvas, frame.x + pad, frame.y, width, frame.h);
  float baseline = frame.y + (frame.h - theme->font_px) * .5f + theme->font_px - scale_px(theme, 2.f);
  paint_string(canvas, text, frame.x + pad, baseline, color, theme->font_px);
  sk_canvas_restore(canvas);
}

static void paint_select_popup(SzView *view, SkCanvas *canvas) {
  if (!view || !view->select_open) return;
  SzView *popup = sz_view_select_popup(view), *rows = popup->scroll_child;
  const SzTheme *theme = &view->layout_theme;
  SzRect frame = popup->frame;
  paint_rect(canvas, frame.x, frame.y, frame.w, frame.h, theme->surface);
  sk_canvas_save(canvas);
  sk_canvas_clip_rect(canvas, frame.x, frame.y, frame.w, frame.h);
#ifdef __EMSCRIPTEN__
  sz_web_select_list(view, popup, frame);
#endif
  for (int i = 0; i < rows->child_count; i++) {
    SzView *option = rows->children[i];
    if (!view_is_shown(option)) continue;
    SzRect box = option->frame;
    int active = option->radio_value == view->select_active;
    if (active) paint_rect(canvas, box.x, box.y, box.w, box.h, theme->selection);
    paint_select_label(canvas, option->text, box, 0.f, theme, theme->foreground);
#ifdef __EMSCRIPTEN__
    sz_web_control(option, SZ_A11Y_OPTION, option->a11y_label, NULL, NULL, "", view, active,
      box.x, box.y, box.w, box.h, 1, frame.x, frame.y, frame.w, frame.h,
      theme->foreground, theme->selection, theme->font_px, text_line_h(theme, theme->font_px), 0, 0);
    sz_web_select_option(option, view, (int)option->radio_value, view->select_count);
#endif
  }
  sk_canvas_restore(canvas);
  paint_border(canvas, frame, (int)fmaxf(1.f, theme_px_scale(theme)), theme->border);
}

static void paint_tooltips(SzView *v, SkCanvas *c) {
  if (!v) return;
  if (v->tooltip_open) {
    const SzTheme *theme = &v->layout_theme;
    SzRect frame = v->tooltip_frame;
    float pad = scale_px(theme, 4.f);
    float inner = fmaxf(1.f, frame.w - 2.f * pad);
    int cap = v->tooltip_lines;
    paint_rect(c, frame.x, frame.y, frame.w, frame.h, theme->surface);
    paint_border(c, frame, (int)fmaxf(1.f, theme_px_scale(theme)), theme->border);
    sk_canvas_save(c);
    sk_canvas_clip_rect(c, frame.x, frame.y, frame.w, frame.h);
    SzRichPaint paint = {.canvas = c, .view = v, .x = frame.x + pad, .y = frame.y + pad,
      .color = theme->foreground};
    sz_text_layout(v->styled, theme->font_px, theme_px_scale(theme), scale_px(theme, theme->line_gap),
      inner, cap, 1, paint_rich_fragment, &paint);
    sk_canvas_restore(c);
  }
  for (int i = 0; i < v->child_count; i++) paint_tooltips(v->children[i], c);
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
  /* A painted step ends here. A step that starts during layout stays. */
  if (!root->motion_paused) motion_tick_node(root);
  sz_view_layout(root, (float)width, (float)height, theme);
  /* Record press, appear, tab, and insert steps before this frame paints. */
  motion_note_node(root);
  if (!root->motion_paused && step_transitions(root))
    sz_view_layout(root, (float)width, (float)height, theme);
#ifdef __EMSCRIPTEN__
  web_route_book = NULL;
  web_modal_scope = find_open_overlay(root);
  sz_web_frame_begin(theme->px_scale > 0 ? theme->px_scale : 1);
#endif
  paint_node(root, canvas, theme);
  g_clip_on = 0;
  g_opacity = 100;
  SzView *select = open_select_node(sz_view_input_scope(root));
  if (select) {
    int alpha = (sz_view_paint_opacity(select) * 255 + 50) / 100;
    if (alpha < 255 && !sk_canvas_save_layer_alpha(canvas, (uint8_t)alpha))
      sz_panic("Opacity layer allocation fails");
    paint_select_popup(select, canvas);
    if (alpha < 255) sk_canvas_restore(canvas);
  }
#ifdef __EMSCRIPTEN__
  sz_web_frame_end();
#endif
  paint_tooltips(root, canvas);
  motion_mark_painted(root);
  return 1;
}

static int clear_focus(SzView *v) {
  if (!v) return 0;
  int focused = v->focused;
  if (view_is_edit(v)) {
    sz_free(v->preedit);
    v->preedit = NULL;
  }
  v->focused = 0;
  for (int i = 0; i < v->child_count; i++) focused |= clear_focus(v->children[i]);
  return focused;
}

static void clear_group_memories(SzView *v) {
  int i;
  if (!v)
    return;
  if (v->kind == SZ_VIEW_FOCUS_GROUP) {
    sz_free(v->text);
    v->text = NULL;
    v->caret = -1;
    if (v->a11y_role != SZ_A11Y_TAB_LIST) v->focus_id = 0;
  }
  for (i = 0; i < v->child_count; i++)
    clear_group_memories(v->children[i]);
}

static SzView *ancestor_focus_group(SzView *v) {
  for (; v; v = v->parent) {
    if (v->kind == SZ_VIEW_FOCUS_GROUP)
      return v;
  }
  return NULL;
}

typedef struct SzGroupScan {
  int count, index, hit_index;
  SzView *hit, *at, *by_id, *by_label;
  uintptr_t id;
  const char *label;
} SzGroupScan;

static void group_scan(SzView *v, SzGroupScan *scan) {
  if (collect_walk_hidden(v)) return;
  if (sz_view_is_tap_target(v)) {
    if (scan->count == scan->index) scan->at = v;
    if (v == scan->hit) scan->hit_index = scan->count;
    if (v->id == scan->id) scan->by_id = v;
    if (!scan->by_label && scan->label && v->a11y_label &&
        !strcmp(v->a11y_label, scan->label)) scan->by_label = v;
    scan->count++;
  }
  for (int i = 0; i < v->child_count; i++) group_scan(v->children[i], scan);
}

static void group_mark_focus(SzView *v, SzView *target) {
  if (collect_walk_hidden(v)) return;
  if (sz_view_is_tap_target(v)) v->focused = v == target;
  for (int i = 0; i < v->child_count; i++) group_mark_focus(v->children[i], target);
}

static void remember_group_focus(SzView *group, SzView *hit) {
  if (!group || !hit) return;
  SzGroupScan scan = {.hit = hit, .hit_index = -1, .index = -1};
  group_scan(group, &scan);
  sz_free(group->text);
  group->text = sz_strdup(hit->a11y_label ? hit->a11y_label : "");
  group->caret = scan.hit_index;
  group->focus_id = hit->id;
}

static void restore_group_focus(SzView *group) {
  if (!group || group->kind != SZ_VIEW_FOCUS_GROUP ||
      (group->caret < 0 && (!group->text || !group->text[0]))) return;
  SzGroupScan scan = {.id = group->focus_id, .label = group->text, .index = group->caret};
  group_scan(group, &scan);
  SzView *target = scan.by_id ? scan.by_id : scan.by_label ? scan.by_label : scan.at;
  if (!target && scan.count > 0) {
    SzGroupScan last = {.index = scan.count - 1};
    group_scan(group, &last);
    target = last.at;
  }
  group_mark_focus(group, target);
  if (target) remember_group_focus(group, target);
}

static SzView *find_active_focus_group(SzView *v) {
  int i;
  SzView *found;
  if (!v || !view_is_shown(v) || size_collapsed(v))
    return NULL;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return NULL;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return NULL;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))
    return NULL;
  if (v->kind == SZ_VIEW_FOCUS_GROUP) {
    restore_group_focus(v);
    if (v->caret >= 0)
      return v;
  }
  for (i = 0; i < v->child_count; i++) {
    found = find_active_focus_group(v->children[i]);
    if (found)
      return found;
  }
  return NULL;
}

static SzView *group_focused_target(SzView *group) {
  if (!group) return NULL;
  restore_group_focus(group);
  SzGroupScan scan = {.index = group->caret};
  group_scan(group, &scan);
  return scan.at;
}

static int move_group_focus(SzView *group, int dir) {
  if (!group) return 0;
  restore_group_focus(group);
  SzGroupScan scan = {.index = -1};
  group_scan(group, &scan);
  if (!scan.count) return 0;
  int index = group->caret < 0 ? 0 : group->caret;
  index += dir;
  if (index < 0) index = 0;
  if (index >= scan.count) index = scan.count - 1;
  scan = (SzGroupScan){.index = index};
  group_scan(group, &scan);
  remember_group_focus(group, scan.at);
  restore_group_focus(group);
  sz_view_reveal_focus(group);
  return 1;
}

static int activate_group_focus(SzView *root, SzView *group) {
  SzView *hit;
  SzRect fr;
  hit = group_focused_target(group);
  if (!hit)
    return 0;
  fr = sz_view_control_frame(hit);
  return sz_view_activate(root, hit, fr.x + fr.w / 2.f, fr.y + fr.h / 2.f);
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

static void clamp_scroll(double *slot, float d, double maxv) {
  *slot += d;
  if (*slot < 0.f)
    *slot = 0.f;
  if (*slot > maxv)
    *slot = maxv;
}

static float editor_max_scroll_x(const SzView *v) {
  const SzTheme *theme = v ? &v->layout_theme : sz_theme_default();
  const char *s = field_cstr(v);
  float cell = sk_font_mono_cell(theme->font_px);
  float gutter = editor_gutter_w(v, theme);
  float text_w = v->frame.w - gutter;
  float extra;
  int i;
  int start = 0;
  int max_cols = 0;
  if (text_w < 8.f)
    text_w = 8.f;
  if (!s)
    s = "";
  for (i = 0;; i++) {
    if (s[i] == '\0' || s[i] == '\n') {
      int cols = editor_cols(s, start, i);
      if (cols > max_cols)
        max_cols = cols;
      if (s[i] == '\0')
        break;
      start = i + 1;
    }
  }
  extra = (scale_px(theme, k_text_field_inset) + (float)max_cols * cell -
           (text_w - scale_px(theme, 2.f))) / theme_px_scale(theme);
  return extra > 0.f ? extra : 0.f;
}

static float editor_max_scroll_y(const SzView *v) {
  const SzTheme *theme = v ? &v->layout_theme : sz_theme_default();
  float line_h = text_line_h(theme, theme->font_px);
  float extra =
      ((float)editor_line_count(field_cstr(v)) * line_h +
       scale_px(theme, k_text_field_inset) - v->frame.h) / theme_px_scale(theme);
  return extra > 0.f ? extra : 0.f;
}

static float scroll_max_x(const SzView *v) {
  const SzTheme *theme = v ? &v->layout_theme : sz_theme_default();
  float extra;
  if (!v || v->kind != SZ_VIEW_SCROLL || !v->scroll_h || !v->scroll_child)
    return 0.f;
  extra = v->scroll_child->frame.w + theme->pad * 2.f - v->frame.w;
  extra /= theme_px_scale(theme);
  return extra > 0.f ? extra : 0.f;
}

static double scroll_max_y(const SzView *v) {
  const SzTheme *theme = v ? &v->layout_theme : sz_theme_default();
  float extra;
  if (!v || v->kind != SZ_VIEW_SCROLL || v->scroll_h || !v->scroll_child)
    return 0.f;
  if (v->scroll_child->virtual_measured) {
    const SzView *content = v->scroll_child;
    const SzTheme *measured = &content->virtual_measure_theme;
    double viewport = fmax(0.0, (v->frame.h - measured->pad * 2.f) / theme_px_scale(measured));
    return fmax(0.0, measured_prefix(content, content->virtual_count) - viewport);
  }
  extra = v->scroll_child->frame.h + theme->pad * 2.f - v->frame.h;
  extra /= theme_px_scale(theme);
  return extra > 0.f ? extra : 0.f;
}

static SzView *parent_scrollable(SzView *v, SzView *boundary) {
  if (v == boundary) return NULL;
  SzView *p;
  for (p = v ? v->parent : NULL; p; p = p->parent) {
    if (p->kind == SZ_VIEW_SCROLL || p->kind == SZ_VIEW_EDITOR)
      return p;
    if (p == boundary) break;
  }
  return NULL;
}

int sz_view_scroll_pan(SzView *scroll, float dx, float dy) {
  double ox;
  double oy;
  if (!scroll)
    return 0;
  ox = scroll->scroll_x;
  oy = scroll->scroll_y;
  if (scroll->kind == SZ_VIEW_EDITOR) {
    clamp_scroll(&scroll->scroll_x, dx, editor_max_scroll_x(scroll));
    clamp_scroll(&scroll->scroll_y, dy, editor_max_scroll_y(scroll));
  } else if (scroll->kind == SZ_VIEW_SCROLL && scroll->scroll_h) {
    clamp_scroll(&scroll->scroll_x, dx != 0.f ? dx : dy, scroll_max_x(scroll));
  } else if (scroll->kind == SZ_VIEW_SCROLL) {
    clamp_scroll(&scroll->scroll_y, dy, scroll_max_y(scroll));
  }
  return scroll->scroll_x != ox || scroll->scroll_y != oy;
}

void sz_view_scroll_by(SzView *scroll, float d) {
  if (!scroll)
    return;
  if (scroll->kind == SZ_VIEW_EDITOR) {
    (void)sz_view_scroll_pan(scroll, 0.f, d);
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

SzView *sz_view_scroll_wheel_target(SzView *root, float x, float y, float dx,
                                    float dy) {
  SzView *v = sz_view_scroll_at(root, x, y);
  while (v) {
    double ox = v->scroll_x;
    double oy = v->scroll_y;
    float scale = sz_view_paint_scale(v);
    if (scale > 0 && sz_view_scroll_pan(v, dx / scale, dy / scale)) {
      v->scroll_x = ox;
      v->scroll_y = oy;
      return v;
    }
    v = parent_scrollable(v, sz_view_input_scope(root));
  }
  return NULL;
}

static SzView *scroll_at_node(SzView *v, float x, float y) {
  int i;
  SzView *found;
  if (!input_node_allows(v, x, y))
    return NULL;
  if (v->kind == SZ_VIEW_IGNORE_POINTER || v->kind == SZ_VIEW_ABSORB_POINTER ||
      (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v)) ||
      (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v)) ||
      (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v)))
    return NULL;
  for (i = v->child_count - 1; i >= 0; i--) {
    found = scroll_at_node(v->children[i], x, y);
    if (found)
      return found;
  }
  return (v->kind == SZ_VIEW_SCROLL || v->kind == SZ_VIEW_EDITOR) && point_in_view(v, x, y) ? v : NULL;
}

SzView *sz_view_scroll_at(SzView *root, float x, float y) {
  SzView *select = open_select_node(sz_view_input_scope(root));
  if (select) {
    SzView *popup = sz_view_select_popup(select);
    return point_in(&popup->frame, x, y) ? popup : NULL;
  }
  return scroll_at_node(input_scope_at(root, x, y), x, y);
}

int sz_view_has_focused_text_field(SzView *root) {
  int i;
  if (!root || !view_is_shown(root))
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
  if (!root || !view_is_shown(root) || size_collapsed(root))
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
  inset = scale_px(theme, k_text_field_inset);
  {
    float gutter = editor_gutter_w(f, theme);
    c = field_caret_clamped(f);
    line_bounds_at_off(s, c, &ls, &le);
    (void)le;
    line = line_index_at_off(s, c);
    x = f->frame.x + gutter + inset + editor_span_width(s, ls, c, font_px) -
        scale_px(theme, f->scroll_x);
    {
      const char *pre = edit_preedit(f);
      if (pre[0])
        x += editor_span_width(pre, 0, (int)strlen(pre), font_px);
    }
    y = f->frame.y + inset + (float)editor_vis_row(f, line) * line_h -
        scale_px(theme, f->scroll_y);
    if (x > f->frame.x + f->frame.w - scale_px(theme, 2.f))
      x = f->frame.x + f->frame.w - scale_px(theme, 2.f);
    if (x < f->frame.x + gutter + scale_px(theme, 1.f))
      x = f->frame.x + gutter + scale_px(theme, 1.f);
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
  z.w = scale_px(theme, 1.f);
  z.h = h;
  return z;
}

static void editor_scroll_to_caret(SzView *v) {
  const SzTheme *theme = v ? &v->layout_theme : sz_theme_default();
  const char *s;
  float font_px, line_h, inset, cx, cy;
  int c, ls, le, line;
  if (!v || v->kind != SZ_VIEW_EDITOR || v->frame.w <= 0.f || v->frame.h <= 0.f)
    return;
  s = field_cstr(v);
  font_px = theme->font_px;
  float scale = theme_px_scale(theme);
  line_h = text_line_h(theme, font_px) / scale;
  inset = k_text_field_inset;
  {
    float gutter = editor_gutter_w(v, theme);
    float text_w = (v->frame.w - gutter) / scale;
    c = field_caret_clamped(v);
    line_bounds_at_off(s, c, &ls, &le);
    (void)le;
    line = line_index_at_off(s, c);
    cx = inset + editor_span_width(s, ls, c, font_px) / scale;
    cy = inset + (float)editor_vis_row(v, line) * line_h;
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
  if (cy + line_h - v->scroll_y > v->frame.h / scale)
    v->scroll_y = cy + line_h - v->frame.h / scale;
  if (v->scroll_y < 0.f)
    v->scroll_y = 0.f;
}

SzRect sz_view_caret_rect(SzView *root, const SzTheme *theme) {
  SzRect z = {0, 0, 0, 0};
  SzView *f;
  float x, y, h;
  int c, n, pre_start, pre_end;
  if (!root || !theme)
    return z;
  f = find_focused_edit(root);
  if (!f)
    return z;
  theme = &f->layout_theme;
  if (f->kind == SZ_VIEW_EDITOR)
    return display_rect(f, editor_caret_rect(f, theme));
  char *display = field_display(f, &c, &pre_start, &pre_end);
  n = (int)strlen(display);
  float *positions = text_positions(display, n, theme->font_px);
  x = f->frame.x + scale_px(theme, k_text_field_inset) + positions[c];
  sz_free(positions);
  sz_free(display);
  if (x > f->frame.x + f->frame.w - scale_px(theme, 2.f))
    x = f->frame.x + f->frame.w - scale_px(theme, 2.f);
  if (x < f->frame.x + scale_px(theme, k_text_field_inset))
    x = f->frame.x + scale_px(theme, k_text_field_inset);
  h = theme->font_px;
  if (h > f->frame.h - scale_px(theme, 4.f))
    h = f->frame.h - scale_px(theme, 4.f);
  if (h < 1.f)
    h = 1.f;
  y = f->frame.y + (f->frame.h - h) * 0.5f;
  z.x = x;
  z.y = y;
  z.w = scale_px(theme, 1.f);
  z.h = h;
  return display_rect(f, z);
}

static int caret_offset_at_x(SzView *f, float x) {
  if (!f || f->kind != SZ_VIEW_TEXT_FIELD) return 0;
  const char *source = field_cstr(f);
  int length = (int)strlen(source), best = 0;
  float *positions = text_positions(source, length, f->layout_theme.font_px);
  double local = (double)x - f->frame.x - scale_px(&f->layout_theme, k_text_field_inset);
  float minimum = positions[0], maximum = positions[0];
  for (int byte = 0; byte <= length;) {
    minimum = fminf(minimum, positions[byte]); maximum = fmaxf(maximum, positions[byte]);
    if (byte == length) break;
    byte += utf8_clen(source, byte);
  }
  local = fmax(minimum - 1., fmin(maximum + 1., local));
  double distance = fabs(positions[0] - local);
  for (int byte = 0; byte <= length;) {
    double next = fabs(positions[byte] - local);
    if (next < distance) { distance = next; best = byte; }
    if (byte == length) break;
    byte += utf8_clen(source, byte);
  }
  best = field_caret_after_zero_width(source, length, best, positions);
  sz_free(positions);
  return best;
}

static int text_row_at_y(double local_y, double line_h) {
  double row = local_y / line_h;
  return !(row > 0) ? 0 : row >= INT_MAX ? INT_MAX : (int)row;
}

static int caret_offset_at_xy(SzView *f, float x, float y) {
  const char *s;
  const SzTheme *theme = f ? &f->layout_theme : sz_theme_default();
  float font_px, line_h, inset, local_x, local_y;
  int n, line, start, end;
  if (!f || f->kind != SZ_VIEW_EDITOR)
    return 0;
  s = field_cstr(f);
  n = (int)strlen(s);
  font_px = theme->font_px;
  line_h = text_line_h(theme, font_px);
  inset = scale_px(theme, k_text_field_inset);
  {
    float gutter = editor_gutter_w(f, theme);
    local_y = y - f->frame.y - inset + scale_px(theme, f->scroll_y);
    local_x = x - f->frame.x - gutter - inset + scale_px(theme, f->scroll_x);
  }
  if (line_h < 1.f)
    line_h = 1.f;
  line = text_row_at_y(local_y, line_h);
  line = editor_line_at_vis(f, line);
  if (!line_span(s, n, line, &start, &end))
    return n;
  if (local_x <= 0.f)
    return start;
  return caret_on_line_at_width(s, start, end, local_x, font_px);
}

static int editor_toggle_fold_at(SzView *v, float x, float y) {
  const SzTheme *theme = v ? &v->layout_theme : sz_theme_default();
  float font_px, line_h, inset, gutter, local_y;
  int vis, line, idx;
  if (!v || v->kind != SZ_VIEW_EDITOR || v->fold_n <= 0)
    return 0;
  font_px = theme->font_px;
  line_h = text_line_h(theme, font_px);
  inset = scale_px(theme, k_text_field_inset);
  gutter = editor_gutter_w(v, theme);
  if (x >= v->frame.x + gutter)
    return 0;
  if (line_h < 1.f)
    line_h = 1.f;
  local_y = y - v->frame.y - inset + scale_px(theme, v->scroll_y);
  vis = text_row_at_y(local_y, line_h);
  line = editor_line_at_vis(v, vis);
  idx = editor_fold_at(v, line);
  if (idx < 0)
    return 0;
  v->fold_closed[idx] = v->fold_closed[idx] ? 0 : 1;
  return 1;
}

const char *sz_view_copy_payload(SzView *v) { return v ? v->copy_text : NULL; }

void sz_view_copy_result(SzView *v, int success) {
  if (!v || !v->copy_text) return;
  sz_free(v->text);
  sz_free(v->a11y_label);
  v->text = sz_strdup(success ? "Copied" : "Copy failed");
  v->a11y_label = sz_strdup(v->text);
}

SzView *sz_view_take_copy(SzView *v) {
  if (!v || !view_is_shown(v)) return NULL;
  if (v->copy_pending) { v->copy_pending = 0; return v; }
  for (int i = 0; i < v->child_count; i++) {
    SzView *copy = sz_view_take_copy(v->children[i]);
    if (copy) return copy;
  }
  return NULL;
}

SzView *sz_view_code(const char *text) {
  SzView *row = sz_view_row();
  SzView *button = sz_view_outlined_button("Copy", NULL, NULL);
  button->copy_text = sz_strdup(text ? text : "");
  sz_view_add_child(row, sz_view_expanded(1, sz_view_scroll_h(sz_view_text(text))));
  sz_view_add_child(row, button);
  return sz_view_card(sz_view_gap(8, row));
}

SzView *sz_view_heading(int level, SzView *child) {
  if (level < 1 || level > 6) sz_panic("Heading level must be 1 through 6");
  SzView *v = sz_view_font_size(level == 1 ? 28 : 20, child);
  v->heading_level = level;
  v->a11y_role = SZ_A11Y_HEADING;
  char label[8];
  snprintf(label, sizeof label, "%d", level);
  v->a11y_label = sz_strdup(label);
  return v;
}

int sz_view_activate(SzView *root, SzView *hit, float x, float y) {
  SzView *group;
  if (!root || !hit || !view_is_path_shown(hit))
    return 0;
  SzView *modal = find_open_overlay(root);
  if (modal && !sz_view_find(modal, hit->id)) return 0;
  SzView *select = open_select_node(modal ? modal : root);
  if (select && hit != select && select_owner(hit) != select) {
    close_selects(root, NULL);
    return 1;
  }
  if (hit->kind == SZ_VIEW_SELECT_OPTION) {
    SzView *owner = select_owner(hit);
    if (!owner || !owner->select_open) return 0;
    owner->select_active = (int)hit->radio_value;
    owner->select_observed = sz_signal_int_get(owner->sig_int);
    select_commit(owner);
    return 1;
  }
  if (hit->kind == SZ_VIEW_SELECT) {
    if (hit->select_open) { close_selects(root, NULL); return 1; }
    if (!hit->select_count) return 0;
    clear_focus(root);
    hit->focused = 1;
    select_begin(root, hit);
    return 1;
  }
  if (hit->kind == SZ_VIEW_TOOLTIP || (modal && hit->kind == SZ_VIEW_CARD)) return 1;
  if (hit->copy_text) { hit->copy_pending = 1; return 1; }
  group = ancestor_focus_group(hit);
  if (hit->kind != SZ_VIEW_TEXT_FIELD && hit->kind != SZ_VIEW_EDITOR)
    clear_focus(root);
  if (sz_view_is_tap_target(hit)) hit->focused = 1;
  clear_group_memories(root);
  if (group && sz_view_is_tap_target(hit) && hit->kind != SZ_VIEW_TEXT_FIELD &&
      hit->kind != SZ_VIEW_EDITOR) {
    hit->focused = 1;
    remember_group_focus(group, hit);
  }
  if ((hit->kind == SZ_VIEW_BUTTON || hit->kind == SZ_VIEW_ICON_BUTTON ||
       hit->kind == SZ_VIEW_FAB || hit->kind == SZ_VIEW_OUTLINED_BUTTON ||
       hit->kind == SZ_VIEW_TEXT_BUTTON || hit->kind == SZ_VIEW_ACTION_CHIP ||
       hit->kind == SZ_VIEW_INK_WELL) &&
      hit->on_tap) {
    /* View.button keeps a press for a fixed pump count. Other taps do not. */
    if (hit->kind == SZ_VIEW_BUTTON)
      motion_start(hit, SZ_MOTION_PRESS);
    hit->on_tap(hit, hit->tap_env);
    return 1;
  }
  if (hit->kind == SZ_VIEW_LINK || hit->kind == SZ_VIEW_NAV_TILE) {
    if (hit->route_id && strncmp(hit->route_id, "http://", 7) &&
        strncmp(hit->route_id, "https://", 8))
      sz_view_navigate(root, hit->route_id);
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
    if (!layout_point(hit, &x, &y)) return 0;
    clear_focus(root);
    hit->focused = 1;
    sz_view_set_text_field_caret(hit, caret_offset_at_x(hit, x));
    return 1;
  }
  if (hit->kind == SZ_VIEW_EDITOR) {
    if (!layout_point(hit, &x, &y)) return 0;
    clear_focus(root);
    hit->focused = 1;
    if (!editor_toggle_fold_at(hit, x, y))
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
  if (!v || !view_is_shown(v) || size_collapsed(v) || n >= cap)
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
  SzView *modal = sz_view_input_scope(root);
  return collect_text_fields_node(modal ? modal : root, out, cap, 0);
}

SzView *sz_view_first_editor(SzView *root) {
  if (!view_is_branch_shown(root) || size_collapsed(root)) return NULL;
  if (root->kind == SZ_VIEW_EDITOR) return root;
  for (int i = 0; i < root->child_count; i++) {
    SzView *editor = sz_view_first_editor(root->children[i]);
    if (editor) return editor;
  }
  return NULL;
}

static int collect_editors_node(SzView *v, SzView **out, int cap, int n) {
  int i;
  if (!v || !view_is_shown(v) || size_collapsed(v) || n >= cap)
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
  SzView *modal = sz_view_input_scope(root);
  return collect_editors_node(modal ? modal : root, out, cap, 0);
}

static int collect_splits_node(SzView *v, SzView **out, int cap, int n) {
  int i;
  if (!v || !view_is_shown(v) || size_collapsed(v) || n >= cap)
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
  if (!v || !view_is_shown(v) || size_collapsed(v) || n >= cap)
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
  if (!v || !view_is_shown(v) || size_collapsed(v))
    return NULL;
  if (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v))
    return NULL;
  if (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))
    return NULL;
  if (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v)) return NULL;
  for (i = v->child_count - 1; i >= 0; i--) {
    found = find_open_overlay(v->children[i]);
    if (found)
      return found;
  }
  return v->kind == SZ_VIEW_OVERLAY ? v : NULL;
}

SzView *sz_view_input_scope(SzView *root) {
  SzView *modal = find_open_overlay(root);
  SzView *scope = modal ? modal : root;
  SzView *select = open_select_node(scope);
  return select ? select : scope;
}

static int focus_hidden(const SzView *v) {
  return !v || v->kind == SZ_VIEW_SELECT_OPTION || !view_is_shown(v) || size_collapsed(v) ||
    (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v)) ||
    (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v)) ||
    (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v));
}

static void dump_transition_nodes(SzView *view, FILE *file, int *index) {
  if (!view) return;
  if (view->kind == SZ_VIEW_ANIMATED_OPACITY) {
    fprintf(file, "%s{\"i\":%d,\"kind\":\"opacity\",\"value\":%d,\"target\":%d,\"remaining\":%d,\"suppressed\":%s}",
      *index ? "," : "", *index, view->animation_value, view->animation_target,
      view->animation_duration - view->animation_step, view->motion_suppressed ? "true" : "false");
    (*index)++;
  }
  if (view->kind == SZ_VIEW_ANIMATED_SIZE) {
    fprintf(file, "%s{\"i\":%d,\"kind\":\"size\",\"width\":%d,\"height\":%d,\"targetWidth\":%d,\"targetHeight\":%d,\"remaining\":%d,\"suppressed\":%s}",
      *index ? "," : "", *index, view->animation_value, view->animation_axis2,
      view->animation_target, view->animation_target_axis2,
      view->animation_duration - view->animation_step, view->motion_suppressed ? "true" : "false");
    (*index)++;
  }
  if (view->kind == SZ_VIEW_ANIMATED_CONTENT_SIZE) {
    fprintf(file, "%s{\"i\":%d,\"kind\":\"contentSize\",\"width\":%.9g,\"height\":%.9g,\"targetWidth\":%.9g,\"targetHeight\":%.9g,\"remaining\":%d,\"suppressed\":%s}",
      *index ? "," : "", *index, (double)view->content_w, (double)view->content_h,
      (double)view->content_target_w, (double)view->content_target_h,
      view->animation_duration - view->animation_step, view->motion_suppressed ? "true" : "false");
    (*index)++;
  }
  if (view->kind == SZ_VIEW_ANIMATED_POSITION) {
    fprintf(file, "%s{\"i\":%d,\"kind\":\"position\",\"x\":%d,\"y\":%d,\"targetX\":%d,\"targetY\":%d,\"remaining\":%d,\"suppressed\":%s}",
      *index ? "," : "", *index, view->animation_value, view->animation_axis2,
      view->animation_target, view->animation_target_axis2,
      view->animation_duration - view->animation_step, view->motion_suppressed ? "true" : "false");
    (*index)++;
  }
  if (view->kind == SZ_VIEW_ANIMATED_SCALE) {
    fprintf(file, "%s{\"i\":%d,\"kind\":\"scale\",\"value\":%d,\"target\":%d,\"remaining\":%d,\"suppressed\":%s}",
      *index ? "," : "", *index, view->animation_value, view->animation_target,
      view->animation_duration - view->animation_step, view->motion_suppressed ? "true" : "false");
    (*index)++;
  }
  for (int i = 0; i < view->child_count; i++) dump_transition_nodes(view->children[i], file, index);
}

void sz_view_transitions_dump_json(SzView *root, FILE *file) {
  int index = 0;
  fputc('[', file); dump_transition_nodes(root, file, &index); fputc(']', file);
}

static void dump_select_nodes(SzView *view, FILE *file, int *index) {
  if (collect_walk_hidden(view)) return;
  if (view->kind == SZ_VIEW_SELECT) {
    fprintf(file, "%s{\"i\":%d,\"label\":\"", *index ? "," : "", *index);
    sz_json_fputs_escaped(file, view->a11y_label);
    fprintf(file, "\",\"value\":%lld,\"open\":%s,\"active\":%d,\"count\":%d,\"first\":%d",
      (long long)sz_view_select_value(view), view->select_open ? "true" : "false",
      view->select_open ? view->select_active : -1, view->select_count, view->select_open ? view->select_first : -1);
    if (view->select_open) {
      SzRect frame = sz_view_select_popup(view)->frame;
      fprintf(file, ",\"popup\":[%.3f,%.3f,%.3f,%.3f]", (double)frame.x, (double)frame.y, (double)frame.w, (double)frame.h);
    }
    fputc('}', file);
    (*index)++;
    return;
  }
  for (int i = 0; i < view->child_count; i++) dump_select_nodes(view->children[i], file, index);
}

void sz_view_selects_dump_json(SzView *root, FILE *file) {
  int index = 0;
  fputc('[', file);
  dump_select_nodes(sz_view_input_scope(root), file, &index);
  fputc(']', file);
}

static SzView *focused_node(SzView *root) {
  if (focus_hidden(root)) return NULL;
  if (root->focused && (view_is_edit(root) || sz_view_is_tap_target(root) ||
      (root->a11y_role == SZ_A11Y_TAB_PANEL || root->kind == SZ_VIEW_OVERLAY))) return root;
  for (int i = 0; i < root->child_count; i++) {
    SzView *found = focused_node(root->children[i]);
    if (found) return found;
  }
  return NULL;
}

int sz_view_has_open_overlay(SzView *root) { return find_open_overlay(root) != NULL; }

SzView *sz_view_focused(SzView *root) {
  SzView *overlay = find_open_overlay(root);
  return focused_node(overlay ? overlay : root);
}

typedef struct SzFocusWalk {
  SzView *current, *first, *last, *before, *after;
  SzSignalInt *radio;
  SzView *radio_scope;
  int seen;
} SzFocusWalk;

static int focus_radio(const SzView *v) {
  return v && (!v->route_id || !v->route_id[0]) &&
    (v->a11y_role == SZ_A11Y_RADIO || v->a11y_role == SZ_A11Y_RADIO_TILE ||
     v->a11y_role == SZ_A11Y_CHOICE_CHIP);
}

static SzView *radio_scope(SzView *v) {
  for (SzView *p = v->parent; p; p = p->parent)
    if (p->a11y_role == SZ_A11Y_RADIO_GROUP) return p;
  return NULL;
}

typedef struct SzChoiceStop {
  SzView *member, *scope, *first, *selected, *focused, *remembered;
  int tab;
} SzChoiceStop;

static void choice_stop_scan(SzView *v, SzChoiceStop *scan) {
  if (focus_hidden(v)) return;
  int member = scan->tab ? v->a11y_role == SZ_A11Y_TAB && ancestor_focus_group(v) == scan->scope :
    focus_radio(v) && v->sig_int == scan->member->sig_int && radio_scope(v) == scan->scope;
  if (member && v->frame.w > 0.f && v->frame.h > 0.f) {
    if (!scan->first) scan->first = v;
    if (scan->tab && scan->scope && v->id == scan->scope->focus_id) scan->remembered = v;
    if (v->focused && !scan->focused) scan->focused = v;
    if (v->sig_int && sz_signal_int_get(v->sig_int) == v->radio_value && !scan->selected)
      scan->selected = v;
  }
  for (int i = 0; i < v->child_count; i++) choice_stop_scan(v->children[i], scan);
}

static SzView *panel_first_content(SzView *v) {
  if (focus_hidden(v)) return NULL;
  if (view_is_edit(v) || sz_view_is_tap_target(v) || v->kind == SZ_VIEW_IMAGE) return v;
  if (v->kind == SZ_VIEW_TEXT) {
    char buf[128];
    if (text_content(v, buf, sizeof buf)[0]) return v;
  }
  for (int i = 0; i < v->child_count; i++) {
    SzView *found = panel_first_content(v->children[i]);
    if (found) return found;
  }
  return NULL;
}

int sz_view_tab_stop(SzView *v) {
  if (v && v->kind == SZ_VIEW_SELECT_OPTION) return 0;
  if (focus_hidden(v) || v->frame.w <= 0.f || v->frame.h <= 0.f) return 0;
  if (v->a11y_role == SZ_A11Y_TAB_PANEL) {
    SzView *first = panel_first_content(v);
    return !first || !(view_is_edit(first) || sz_view_is_tap_target(first));
  }
  int tab = v->a11y_role == SZ_A11Y_TAB;
  if (tab || focus_radio(v)) {
    SzView *scope = tab ? ancestor_focus_group(v) : radio_scope(v);
    SzView *boundary = scope ? scope : v;
    if (!scope) while (boundary->parent) boundary = boundary->parent;
    SzView *overlay = find_open_overlay(boundary);
    if (overlay) boundary = overlay;
    SzChoiceStop scan = {.member = v, .scope = scope, .tab = tab};
    choice_stop_scan(boundary, &scan);
    return v == (scan.focused ? scan.focused : scan.remembered ? scan.remembered :
                 scan.selected ? scan.selected : scan.first);
  }
  return view_is_edit(v) || sz_view_is_tap_target(v);
}

static void focus_walk(SzView *v, SzFocusWalk *walk) {
  if (focus_hidden(v)) return;
  if (v->kind == SZ_VIEW_SPLIT && v->child_count > 0) focus_walk(v->children[0], walk);
  if (v->frame.w > 0.f && v->frame.h > 0.f &&
      (walk->radio ? focus_radio(v) && v->sig_int == walk->radio && radio_scope(v) == walk->radio_scope :
                     sz_view_tab_stop(v))) {
    if (!walk->first) walk->first = v;
    if (v == walk->current) walk->seen = 1;
    else if (walk->seen && !walk->after) walk->after = v;
    else if (!walk->seen) walk->before = v;
    walk->last = v;
  }
  for (int i = v->kind == SZ_VIEW_SPLIT ? 1 : 0; i < v->child_count; i++) focus_walk(v->children[i], walk);
}

static int focus_step(SzView *root, SzView *boundary, int backwards, int wrap) {
  SzFocusWalk walk = {0};
  walk.current = sz_view_focused(boundary);
  focus_walk(boundary, &walk);
  SzView *next = backwards ? walk.before : walk.after;
  if (!next && (!walk.current || wrap)) next = backwards ? walk.last : walk.first;
  if (!next && boundary->kind == SZ_VIEW_OVERLAY) { sz_view_focus(root, boundary); return 1; }
  if (!next) return 0;
  if (walk.current && view_is_edit(walk.current)) field_commit_preedit(walk.current);
  sz_view_focus(root, next);
  return 1;
}

static int focus_path_shown(SzView *v) {
  for (; v; v = v->parent) if (focus_hidden(v)) return 0;
  return 1;
}

static void reset_closed_modals(SzView *v) {
  if (v->kind == SZ_VIEW_OVERLAY && !focus_path_shown(v)) v->modal_active = 0;
  for (int i = 0; i < v->child_count; i++) reset_closed_modals(v->children[i]);
}

static SzView *modal_restore_target(SzView *root, uintptr_t id) {
  SzView *target = sz_view_find(root, id);
  while (target && !focus_path_shown(target)) {
    SzView *closed = NULL;
    for (SzView *p = target; p; p = p->parent)
      if (p->kind == SZ_VIEW_OVERLAY && !focus_path_shown(p)) closed = p;
    if (!closed || closed->return_focus_id == id) return NULL;
    id = closed->return_focus_id;
    target = sz_view_find(root, id);
  }
  return target;
}

static void sync_modal_focus(SzView *root) {
  SzView *modal = find_open_overlay(root);
  uintptr_t id = sz_view_id(modal);
  if (id != root->modal_id) {
    SzView *restore = modal_restore_target(root, root->modal_return_id);
    if (modal && !modal->modal_active) {
      SzView *opener = focused_node(root);
      modal->return_focus_id = sz_view_id(opener ? opener : restore);
      modal->modal_active = 1;
      restore = NULL;
    }
    root->modal_id = id;
    root->modal_return_id = modal ? modal->return_focus_id : 0;
    SzView *boundary = modal ? modal : root;
    if (restore && (!modal || sz_view_find(modal, sz_view_id(restore))))
      sz_view_focus(root, restore);
    else {
      clear_focus(root);
      if (!focus_step(root, boundary, 0, 1) && modal) sz_view_focus(root, modal);
    }
  } else if (modal && !focused_node(modal)) {
    if (!focus_step(root, modal, 0, 1)) sz_view_focus(root, modal);
  }
  reset_closed_modals(root);
}

int sz_view_focus_next(SzView *root, int backwards, int wrap) {
  if (!root) return 0;
  SzView *select = open_select_node(sz_view_input_scope(root));
  if (select) select_commit(select);
  SzView *overlay = find_open_overlay(root);
  return focus_step(root, overlay ? overlay : root, backwards, wrap || overlay != NULL);
}

const char *sz_view_focus_kind(SzView *root) {
  static char buf[160];
  SzView *ed;
  SzView *group;
  SzView *hit;
  const char *lab;
  if (!root)
    return "none";
  SzView *boundary = find_open_overlay(root);
  ed = sz_view_focused(boundary ? boundary : root);
  if (ed && ed->kind == SZ_VIEW_EDITOR)
    return "editor";
  if (ed && ed->kind == SZ_VIEW_TEXT_FIELD)
    return "field";
  if (ed) {
    snprintf(buf, sizeof buf, "%s:%s", a11y_role_name(ed->a11y_role), sz_view_a11y_label(ed));
    return buf;
  }
  if (find_open_overlay(root))
    return "overlay";
  group = find_active_focus_group(root);
  hit = group_focused_target(group);
  if (!hit)
    return "none";
  lab = hit->a11y_label ? hit->a11y_label : "";
  snprintf(buf, sizeof buf, "button:%s", lab);
  return buf;
}

static int collect_walk_hidden(const SzView *v) {
  if (!v || !view_is_shown(v) || size_collapsed(v))
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
  SzView *modal = sz_view_input_scope(root);
  return collect_tap_targets_node(modal ? modal : root, out, cap, 0);
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
    fr = sz_view_control_frame(taps[i]);
    return sz_view_activate(root, taps[i], fr.x + fr.w / 2.f, fr.y + fr.h / 2.f);
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
  SzView *modal = sz_view_input_scope(root);
  return collect_scrolls_node(modal ? modal : root, out, cap, 0);
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
  clear_group_memories(root);
  t->focused = 1;
  return 1;
}

int sz_view_focus_edit_target(SzView *root) {
  SzView *t = sz_view_edit_target(root);
  if (!t)
    return 0;
  clear_focus(root);
  clear_group_memories(root);
  t->focused = 1;
  return 1;
}

int sz_view_handle_text(SzView *root, const char *text) {
  sync_selects(root);
  SzView *select = sz_view_focused(root);
  if (select && select->kind == SZ_VIEW_SELECT) return select_search(root, select, text);
  SzView *target = sz_view_edit_target(root);
  const char *s;
  if (!target || !target->sig_str)
    return 0;
  sz_free(target->preedit);
  target->preedit = NULL;
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

static void field_clear_preedit(SzView *t) {
  if (!t)
    return;
  sz_free(t->preedit);
  t->preedit = NULL;
}

static void field_set_preedit(SzView *t, const char *text) {
  if (!t)
    return;
  sz_free(t->preedit);
  t->preedit = sz_strdup(text ? text : "");
}

static int field_has_preedit(const SzView *t) {
  return t && t->preedit && t->preedit[0];
}

static void field_preedit_backspace(SzView *t) {
  int n, keep;
  char *p;
  if (!field_has_preedit(t))
    return;
  n = (int)strlen(t->preedit);
  keep = utf8_prev(t->preedit, n);
  if (keep <= 0) {
    field_clear_preedit(t);
    return;
  }
  p = (char *)sz_alloc((size_t)keep + 1);
  memcpy(p, t->preedit, (size_t)keep);
  p[keep] = '\0';
  sz_free(t->preedit);
  t->preedit = p;
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

static void field_commit_preedit(SzView *t) {
  char *p;
  if (!field_has_preedit(t))
    return;
  p = t->preedit;
  t->preedit = NULL;
  field_insert_at_caret(t, p);
  sz_free(p);
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
  if (t->kind == SZ_VIEW_TEXT_FIELD && dir) {
    float *positions = text_positions(cur, n, t->layout_theme.font_px);
    float current = positions[c], distance = INFINITY;
    int best = c;
    for (int byte = 0; byte <= n;) {
      float delta = (positions[byte] - current) * dir;
      if (delta > .001f && delta < distance) { distance = delta; best = byte; }
      if (byte == n) break;
      byte += utf8_clen(cur, byte);
    }
    t->caret = field_caret_after_zero_width(cur, n, best, positions);
    sz_free(positions);
    return;
  }
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
    if (t->kind == SZ_VIEW_TEXT_FIELD) {
      const char *source = field_cstr(t);
      int n = (int)strlen(source);
      float *positions = text_positions(source, n, t->layout_theme.font_px);
      t->caret = (positions[a] < positions[b]) == (dir < 0) ? a : b;
      sz_free(positions);
    } else t->caret = dir < 0 ? a : b;
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
  font_px = t->layout_theme.font_px;
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
  if (backspace && field_has_preedit(target)) {
    field_preedit_backspace(target);
    target->focused = 1;
    return 1;
  }
  field_commit_preedit(target);
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
  SzView *group;
  int extend = (mods & SZ_KEY_SHIFT) != 0;
  int is_ed;
  if (!root)
    return 1;
  if (!key)
    key = "";
  sz_view_prepare_input(root);
  overlay = find_open_overlay(root);
  SzView *select_focus = sz_view_focused(root);
  if (select_focus && select_focus->kind == SZ_VIEW_SELECT && select_key(root, select_focus, key, text, mods)) return 1;
  if (strcmp(key, "Escape") == 0) {
    SzView *focused = find_focused_edit(root);
    if (field_has_preedit(focused)) {
      field_clear_preedit(focused);
      return 1;
    }
    sync_tooltips(root);
    if (dismiss_tooltips(root)) return 1;
    if (overlay) {
      if (overlay->sig_int)
        sz_signal_int_set(overlay->sig_int, 0);
      clear_focus(root);
      sync_modal_focus(root);
      return 1;
    }
  }
  SzView *focused = sz_view_focused(overlay ? overlay : root);
  if (!strcmp(key, "Tab") && !(mods & (SZ_KEY_CMD | SZ_KEY_ALT)) &&
      (!focused || focused->kind != SZ_VIEW_EDITOR || (mods & SZ_KEY_CTRL)))
    return focus_step(root, overlay ? overlay : root, extend, 1);
  if (focus_radio(focused) && focused->sig_int && (radio_scope(focused) || !ancestor_focus_group(focused)) &&
      !(mods & (SZ_KEY_CTRL | SZ_KEY_CMD | SZ_KEY_ALT))) {
    int backwards = !strcmp(key, "ArrowLeft") || !strcmp(key, "ArrowUp");
    int forwards = !strcmp(key, "ArrowRight") || !strcmp(key, "ArrowDown");
    if (backwards || forwards || !strcmp(key, "Home") || !strcmp(key, "End")) {
      SzFocusWalk walk = {0};
      walk.current = focused;
      walk.radio = focused->sig_int;
      walk.radio_scope = radio_scope(focused);
      SzView *boundary = walk.radio_scope ? walk.radio_scope : overlay ? overlay : root;
      SzView *modal = find_open_overlay(boundary);
      focus_walk(modal ? modal : boundary, &walk);
      SzView *next = !strcmp(key, "Home") ? walk.first : !strcmp(key, "End") ? walk.last :
        backwards ? (walk.before ? walk.before : walk.last) : (walk.after ? walk.after : walk.first);
      if (next) {
        sz_signal_int_set(next->sig_int, next->radio_value);
        sz_view_focus(root, next);
      }
      return 1;
    }
  }
  if (focused && focused->kind == SZ_VIEW_SPLIT) {
    if (mods & (SZ_KEY_CTRL | SZ_KEY_CMD | SZ_KEY_ALT)) return 1;
    int value = sz_view_split_frac(focused);
    if (!strcmp(key, "ArrowLeft")) value--;
    else if (!strcmp(key, "ArrowRight")) value++;
    else if (!strcmp(key, "Home")) value = 0;
    else if (!strcmp(key, "End")) value = 100;
    else if (!strcmp(key, "Enter")) value = value ? 0 : focused->split_restore;
    else return 1;
    return split_set_fraction(focused, value);
  }
  if (focused && focused->kind == SZ_VIEW_SLIDER) {
    if (!focused->sig_int || (mods & (SZ_KEY_CTRL | SZ_KEY_CMD | SZ_KEY_ALT))) return 1;
    int steps = 0;
    int64_t value = sz_view_slider_value(focused);
    if (!strcmp(key, "ArrowLeft") || !strcmp(key, "ArrowDown")) steps = -1;
    else if (!strcmp(key, "ArrowRight") || !strcmp(key, "ArrowUp")) steps = 1;
    else if (!strcmp(key, "PageDown")) steps = -10;
    else if (!strcmp(key, "PageUp")) steps = 10;
    else if (!strcmp(key, "Home")) value = focused->range_minimum;
    else if (!strcmp(key, "End")) value = focused->range_maximum;
    else return 1;
    __int128 next = (__int128)value + (__int128)steps * focused->range_step;
    if (next < focused->range_minimum) next = focused->range_minimum;
    if (next > focused->range_maximum) next = focused->range_maximum;
    sz_signal_int_set(focused->sig_int, (int64_t)next);
    return 1;
  }
  if (focused && !view_is_edit(focused) && focused->kind != SZ_VIEW_SLIDER &&
      !(mods & (SZ_KEY_CTRL | SZ_KEY_CMD | SZ_KEY_ALT)) &&
      (!strcmp(key, "Enter") || !strcmp(key, "Space"))) {
    SzRect frame = sz_view_control_frame(focused);
    int handled = sz_view_activate(root, focused, frame.x + frame.w / 2.f, frame.y + frame.h / 2.f);
    if (!sz_view_focused(root)) sz_view_focus(root, focused);
    return handled;
  }
  if (focus_radio(focused) && (radio_scope(focused) || !ancestor_focus_group(focused))) return 1;
  if (!overlay) {
    if (!find_focused_edit(root)) {
      group = find_active_focus_group(root);
      if (group && (!focused || ancestor_focus_group(focused) == group)) {
        if (group->a11y_role == SZ_A11Y_TAB_LIST) {
          SzView *tabs = group->parent;
          int n = tabs->child_count - 1;
          int next = group->caret;
          if (strcmp(key, "ArrowLeft") == 0) next = (next + n - 1) % n;
          else if (strcmp(key, "ArrowRight") == 0) next = (next + 1) % n;
          else if (strcmp(key, "Home") == 0) next = 0;
          else if (strcmp(key, "End") == 0) next = n - 1;
          else next = -1;
          if (next >= 0 && !(mods & (SZ_KEY_CTRL | SZ_KEY_CMD | SZ_KEY_ALT))) {
            SzView *tab = group->children[0]->children[0]->children[next];
            remember_group_focus(group, tab);
            restore_group_focus(group);
            sz_view_reveal_focus(root);
            return 1;
          }
        }
        if (group->a11y_role == SZ_A11Y_TAB_LIST &&
            (!strcmp(key, "ArrowUp") || !strcmp(key, "ArrowDown"))) return 0;
        if (strcmp(key, "ArrowUp") == 0) {
          (void)move_group_focus(group, -1);
          return 1;
        }
        if (strcmp(key, "ArrowDown") == 0) {
          (void)move_group_focus(group, 1);
          return 1;
        }
        if (strcmp(key, "Enter") == 0 || strcmp(key, "Space") == 0) {
          (void)activate_group_focus(root, group);
          return 1;
        }
        return 1;
      }
    }
  }
  if (focused && !view_is_edit(focused)) return 1;
  if (overlay && !focused) {
    SzView *edit = sz_view_edit_target(overlay);
    if (!edit) return 1;
    sz_view_focus(root, edit);
  }
  target = sz_view_edit_target(root);
  is_ed = target && target->kind == SZ_VIEW_EDITOR;
  if (strcmp(key, "Backspace") == 0) {
    if (field_has_preedit(target)) {
      field_preedit_backspace(target);
      target->focused = 1;
      return 1;
    }
    (void)sz_view_handle_text_edit(root, NULL, 1);
    return 1;
  }
  if (field_has_preedit(target) &&
      (strcmp(key, "Delete") == 0 || strncmp(key, "Arrow", 5) == 0 ||
       strcmp(key, "Home") == 0 || strcmp(key, "End") == 0 ||
       strcmp(key, "PageUp") == 0 || strcmp(key, "PageDown") == 0 ||
       strcmp(key, "Enter") == 0 || strcmp(key, "Tab") == 0 ||
       (text && text[0]) || strcmp(key, "Space") == 0 ||
       key_is_one_code_point(key)))
    field_commit_preedit(target);
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
    float line_h = text_line_h(&target->layout_theme, target->layout_theme.font_px);
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

int sz_view_handle_compose(SzView *root, const char *text) {
  SzView *target = sz_view_edit_target(root);
  if (!target || !target->sig_str)
    return 0;
  if (!text || !text[0])
    field_commit_preedit(target);
  else
    field_set_preedit(target, text);
  target->focused = 1;
  if (target->kind == SZ_VIEW_EDITOR)
    editor_scroll_to_caret(target);
  return 1;
}

int sz_view_edit_extend_to_xy(SzView *view, float x, float y) {
  if (!view_is_edit(view) || !view_is_path_shown(view) || !layout_point(view, &x, &y))
    return 0;
  if (view->kind == SZ_VIEW_EDITOR)
    view->caret = caret_offset_at_xy(view, x, y);
  else
    view->caret = caret_offset_at_x(view, x);
  return 1;
}

/* Select the first Index Book or Tabs. Browser URL and View.link share this walk. */
static SzView *first_book(SzView *v) {
  if (collect_walk_hidden(v)) return NULL;
  if (v->kind == SZ_VIEW_INDEX_BOOK || v->kind == SZ_VIEW_TABS) return v;
  for (int i = 0; i < v->child_count; i++) {
    SzView *book = first_book(v->children[i]);
    if (book) return book;
  }
  return NULL;
}

int sz_view_navigate(SzView *root, const char *id) {
  SzView *v;
  if (!root || !id || !id[0]) return 0;
  v = first_book(root);
  if (!v) return 0;
  for (int i = 1; i < v->child_count; i++) {
    SzView *section = v->children[i]->children[0];
    if (section->route_id && !strcmp(section->route_id, id)) {
      sz_signal_int_set(v->sig_int, i - 1);
      return 1;
    }
  }
  return 0;
}

#ifdef __EMSCRIPTEN__
int sz_view_web_navigate(SzView *root, const char *title) {
  return sz_view_navigate(root, title);
}
#endif

uintptr_t sz_view_id(const SzView *v) { return v ? v->id : 0; }

SzView *sz_view_find(SzView *v, uintptr_t id) {
  if (!id || !v || !view_is_shown(v) || size_collapsed(v) ||
      (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v)) ||
      (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v))) return NULL;
  if (v->id == id) return v;
  for (int i = 0; i < v->child_count; i++) {
    SzView *found = sz_view_find(v->children[i], id);
    if (found) return found;
  }
  return NULL;
}

#ifdef __EMSCRIPTEN__
SzView *sz_view_web_find(SzView *v, uintptr_t id) {
  if (!v || !view_is_shown(v) || size_collapsed(v) || v->kind == SZ_VIEW_EXCLUDE_SEMANTICS ||
      (v->kind == SZ_VIEW_VISIBILITY && !view_visibility_on(v)) ||
      (v->kind == SZ_VIEW_OFFSTAGE && !view_offstage_shown(v)) ||
      (v->kind == SZ_VIEW_OVERLAY && !view_overlay_open(v))) return NULL;
  if (v->id == id) return v;
  for (int i = 0; i < v->child_count; i++) {
    SzView *found = sz_view_web_find(v->children[i], id);
    if (found) return found;
  }
  return NULL;
}
#endif

static void reveal_view(SzView *view) {
  SzRect frame = sz_view_frame(view);
  for (SzView *p = view->parent; p; p = p->parent) {
    if (p->kind != SZ_VIEW_SCROLL) continue;
    float start = p->scroll_h ? frame.x : frame.y;
    float extent = p->scroll_h ? frame.w : frame.h;
    SzRect viewport = sz_view_frame(p);
    float origin = p->scroll_h ? viewport.x : viewport.y;
    float available = p->scroll_h ? viewport.w : viewport.h;
    float delta = start < origin || extent > available ? start - origin :
      start + extent > origin + available ? start + extent - origin - available : 0;
    float scale = sz_view_paint_scale(p);
    if (delta && scale > 0) {
      sz_view_scroll_by(p, delta / (theme_px_scale(&p->layout_theme) * scale));
      if (p->scroll_h) frame.x -= delta; else frame.y -= delta;
    }
  }
}

void sz_view_reveal_focus(SzView *root) {
  if (!root || !view_is_shown(root)) return;
  if (root->focused) reveal_view(root);
  for (int i = 0; i < root->child_count; i++) sz_view_reveal_focus(root->children[i]);
}

void sz_view_focus(SzView *root, SzView *view) {
  if (view && view->kind == SZ_VIEW_SELECT_OPTION) view = select_owner(view);
  SzView *select = open_select_node(sz_view_input_scope(root));
  if (select && view != select && select_owner(view) != select) select_commit(select);
  SzView *modal = find_open_overlay(root);
  if (view && modal && !sz_view_find(modal, view->id)) return;
  if (view && !view_is_path_shown(view))
    return;
  clear_focus(root);
  clear_group_memories(root);
  if (view) {
    view->focused = 1;
    reveal_view(view);
    SzView *group = ancestor_focus_group(view);
    if (group) remember_group_focus(group, view);
  }
  sync_tooltips(root);
}

struct SzA11ySnapshot {
  SzA11yNode *nodes;
  size_t count, capacity;
};

static char *a11y_copy(const char *text) {
  size_t length = strlen(text ? text : "") + 1;
  char *copy = malloc(length);
  if (!copy) sz_panic("Accessibility snapshot allocation fails");
  memcpy(copy, text ? text : "", length);
  return copy;
}

static unsigned a11y_actions(const SzView *view) {
  int edit = view_is_edit(view) && view->sig_str;
  unsigned actions = 0;
  if (edit || (sz_view_is_tap_target(view) && view->kind != SZ_VIEW_SELECT_OPTION))
    actions |= SZ_A11Y_FOCUS;
  if (edit) actions |= SZ_A11Y_ACTIVATE | SZ_A11Y_SET_TEXT | SZ_A11Y_SET_SELECTION;
  if ((view->kind == SZ_VIEW_SLIDER || view->kind == SZ_VIEW_SPLIT) && view->sig_int)
    return actions | SZ_A11Y_INCREMENT | SZ_A11Y_DECREMENT | SZ_A11Y_SET_VALUE;
  if (view->kind == SZ_VIEW_SCROLL)
    return actions | SZ_A11Y_INCREMENT | SZ_A11Y_DECREMENT;
  if (sz_view_is_tap_target(view)) {
    int button = view->kind == SZ_VIEW_BUTTON || view->kind == SZ_VIEW_ICON_BUTTON ||
      view->kind == SZ_VIEW_FAB || view->kind == SZ_VIEW_OUTLINED_BUTTON ||
      view->kind == SZ_VIEW_TEXT_BUTTON || view->kind == SZ_VIEW_ACTION_CHIP ||
      view->kind == SZ_VIEW_INK_WELL;
    if (!button || view->on_tap) actions |= SZ_A11Y_ACTIVATE;
  }
  return actions;
}

static SzRect a11y_bounds(SzView *view, SzRect viewport) {
  SzRect bounds = sz_view_control_frame(view);
  if (view->inline_action && view->action_count) {
    for (int i = 1; i < view->action_count; i++) {
      SzRect next = display_rect(view, view->action_boxes[i]);
      float right = fmaxf(bounds.x + bounds.w, next.x + next.w);
      float bottom = fmaxf(bounds.y + bounds.h, next.y + next.h);
      bounds.x = fminf(bounds.x, next.x);
      bounds.y = fminf(bounds.y, next.y);
      bounds.w = right - bounds.x;
      bounds.h = bottom - bounds.y;
    }
  }
  if (!rects_intersect(bounds, viewport, &bounds)) return (SzRect){0};
  for (SzView *p = view->parent; p; p = p->parent) {
    /* A select popup uses viewport coordinates outside its ancestor clips. */
    if (p->kind == SZ_VIEW_SELECT) break;
    if (view_clips_children(p) && !rects_intersect(bounds, sz_view_frame(p), &bounds))
      return (SzRect){0};
  }
  return bounds;
}

static SzA11yNode *a11y_append(SzA11ySnapshot *snapshot) {
  if (snapshot->count == snapshot->capacity) {
    size_t capacity = snapshot->capacity ? snapshot->capacity * 2 : 16;
    if (capacity < snapshot->capacity || capacity > SIZE_MAX / sizeof(SzA11yNode))
      sz_panic("Accessibility snapshot is too large");
    SzA11yNode *nodes = realloc(snapshot->nodes, capacity * sizeof(*nodes));
    if (!nodes) sz_panic("Accessibility snapshot allocation fails");
    snapshot->nodes = nodes;
    snapshot->capacity = capacity;
  }
  SzA11yNode *node = &snapshot->nodes[snapshot->count++];
  memset(node, 0, sizeof(*node));
  node->checked = node->selected = node->expanded = -1;
  return node;
}

static void a11y_snapshot_walk(SzView *view, uintptr_t parent, SzRect viewport,
                                SzA11ySnapshot *snapshot) {
  if (!view || !view_is_shown(view) || view->kind == SZ_VIEW_EXCLUDE_SEMANTICS ||
      size_collapsed(view)) return;
  SzRect bounds = a11y_bounds(view, viewport);
  int semantic = view->a11y_role != SZ_A11Y_NONE &&
    view->kind != SZ_VIEW_VISIBILITY && view->kind != SZ_VIEW_OFFSTAGE &&
    view->kind != SZ_VIEW_UNCONSTRAINED_BOX;
  if (semantic && bounds.w > 0 && bounds.h > 0) {
    SzA11yInfo info = a11y_node_info(view);
    SzA11yNode *node = a11y_append(snapshot);
    node->id = view->id;
    node->parent_id = parent;
    node->role = view->a11y_role;
    node->bounds = bounds;
    node->label = a11y_copy(info.label);
    node->actions = a11y_actions(view);
    node->focused = view->focused;
    node->editable = view_is_edit(view) && view->sig_str;
    node->multiline = view->kind == SZ_VIEW_EDITOR;
    node->number = info.num;
    const char *value = "";
    char number[24];
    if (node->editable) {
      value = field_cstr(view);
      node->selection_start = view->sel_anchor;
      node->selection_end = view->caret;
    } else if (view->kind == SZ_VIEW_SELECT) {
      value = sz_view_select_text(view);
      node->expanded = view->select_open;
    } else if (info.cls == 2) {
      snprintf(number, sizeof number, "%lld", (long long)info.num);
      value = number;
    }
    node->value = a11y_copy(value);
    SzView *tooltip = sz_view_tooltip_owner(view);
    node->description = a11y_copy(tooltip ? tooltip->text : "");
    if (view->kind == SZ_VIEW_EXPANSION_TILE) node->expanded = info.num != 0;
    else if (info.cls == 1 && !a11y_kind_flag(view->kind) && view->kind != SZ_VIEW_TOOLTIP) {
      if (view->a11y_role == SZ_A11Y_TAB || view->a11y_role == SZ_A11Y_OPTION)
        node->selected = info.num != 0;
      else node->checked = info.num != 0;
    }
    if (view->kind == SZ_VIEW_SLIDER) {
      node->minimum = view->range_minimum;
      node->maximum = view->range_maximum;
      node->step = view->range_step;
    } else if (view->kind == SZ_VIEW_SPLIT || view->kind == SZ_VIEW_PROGRESS ||
               view->kind == SZ_VIEW_CIRCULAR_PROGRESS) {
      node->maximum = 100;
      node->step = 1;
    }
    parent = node->id;
    sz_free(info.live);
  }
  if (!a11y_recurse(view)) return;
  for (int i = 0; i < view->child_count; i++)
    a11y_snapshot_walk(view->children[i], parent, viewport, snapshot);
}

SzA11ySnapshot *sz_view_a11y_snapshot(SzView *root, SzRect viewport) {
  SzA11ySnapshot *snapshot = calloc(1, sizeof(*snapshot));
  if (!snapshot) sz_panic("Accessibility snapshot allocation fails");
  if (!isfinite(viewport.x) || !isfinite(viewport.y) || !isfinite(viewport.w) ||
      !isfinite(viewport.h) || viewport.w <= 0 || viewport.h <= 0) return snapshot;
  SzView *scope = sz_view_input_scope(root);
  for (SzView *p = scope; p; p = p->parent) {
    if (!view_is_shown(p) || p->kind == SZ_VIEW_EXCLUDE_SEMANTICS ||
        (p != scope && !a11y_recurse(p))) return snapshot;
  }
  a11y_snapshot_walk(scope, 0, viewport, snapshot);
  return snapshot;
}

size_t sz_a11y_snapshot_count(const SzA11ySnapshot *snapshot) {
  return snapshot ? snapshot->count : 0;
}

const SzA11yNode *sz_a11y_snapshot_node(const SzA11ySnapshot *snapshot, size_t index) {
  return snapshot && index < snapshot->count ? &snapshot->nodes[index] : NULL;
}

void sz_a11y_snapshot_free(SzA11ySnapshot *snapshot) {
  if (!snapshot) return;
  for (size_t i = 0; i < snapshot->count; i++) {
    free((void *)snapshot->nodes[i].label);
    free((void *)snapshot->nodes[i].value);
    free((void *)snapshot->nodes[i].description);
  }
  free(snapshot->nodes);
  free(snapshot);
}

int sz_view_a11y_action(SzView *root, const SzA11yRequest *request) {
  if (!root || !request || !request->id) return 0;
  SzA11ySnapshot *snapshot = sz_view_a11y_snapshot(root,
    (SzRect){0, 0, root->viewport_w, root->viewport_h});
  const SzA11yNode *node = NULL;
  for (size_t i = 0; i < snapshot->count; i++)
    if (snapshot->nodes[i].id == request->id) { node = &snapshot->nodes[i]; break; }
  SzView *view = node ? sz_view_find(root, request->id) : NULL;
  int result = 0;
  if (view && (node->actions & request->action)) {
    switch (request->action) {
    case SZ_A11Y_FOCUS:
      sz_view_focus(root, view);
      result = 1;
      break;
    case SZ_A11Y_ACTIVATE: {
      SzRect bounds = sz_view_control_frame(view);
      result = sz_view_activate(root, view, bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
      break;
    }
    case SZ_A11Y_SET_TEXT:
      sz_view_focus(root, view);
      result = sz_view_handle_text(root, request->text);
      break;
    case SZ_A11Y_SET_SELECTION:
      sz_view_focus(root, view);
      result = view->kind == SZ_VIEW_EDITOR
        ? sz_view_set_editor_sel(view, request->selection_start, request->selection_end)
        : sz_view_set_text_field_sel(view, request->selection_start, request->selection_end);
      break;
    case SZ_A11Y_INCREMENT:
    case SZ_A11Y_DECREMENT:
      if (view->kind == SZ_VIEW_SCROLL) {
        float distance = (view->scroll_h ? view->frame.w : view->frame.h) * .8f;
        if (request->action == SZ_A11Y_DECREMENT) distance = -distance;
        result = sz_view_scroll_pan(view, view->scroll_h ? distance : 0,
                                     view->scroll_h ? 0 : distance);
        break;
      }
      /* Use wide arithmetic across the full signed range. */
      {
        __int128 value = (__int128)node->number +
          (request->action == SZ_A11Y_INCREMENT ? (__int128)node->step : -(__int128)node->step);
        int64_t next = value < node->minimum ? node->minimum :
          value > node->maximum ? node->maximum : (int64_t)value;
        if (view->kind == SZ_VIEW_SPLIT) result = split_set_fraction(view, next);
        else { sz_signal_int_set(view->sig_int, slider_value(view, next)); result = 1; }
      }
      break;
    case SZ_A11Y_SET_VALUE:
      if (view->kind == SZ_VIEW_SPLIT) result = split_set_fraction(view, request->value);
      else { sz_signal_int_set(view->sig_int, slider_value(view, request->value)); result = 1; }
      break;
    }
  }
  sz_a11y_snapshot_free(snapshot);
  return result;
}
