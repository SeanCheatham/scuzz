#include "scuzz_ui.h"

#include "sk_capi.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static char *sz_strdup(const char *s) {
  size_t n;
  char *out;
  if (!s)
    s = "";
  n = strlen(s);
  out = (char *)sz_alloc(n + 1);
  memcpy(out, s, n + 1);
  return out;
}

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
};

static SzView *view_new(SzViewKind kind) {
  SzView *v = (SzView *)sz_alloc_zero(sizeof(SzView));
  v->kind = kind;
  return v;
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
         kind == SZ_VIEW_EXPANSION_TILE;
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
          view->kind == SZ_VIEW_SWITCH || view->kind == SZ_VIEW_CHIP ||
          view->kind == SZ_VIEW_EXPANSION_TILE ||
          view->kind == SZ_VIEW_ICON_BUTTON ||
          view->kind == SZ_VIEW_CHECKBOX_LIST_TILE);
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
  v->on_tap = on_tap;
  v->tap_env = env;
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_BUTTON;
  v->a11y_label = sz_strdup(label);
  return v;
}

SzView *sz_view_icon_button(const char *label, SzViewTapFn on_tap, void *env) {
  SzView *v = view_new(SZ_VIEW_ICON_BUTTON);
  v->text = sz_strdup(label ? label : "");
  v->on_tap = on_tap;
  v->tap_env = env;
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_ICON_BUTTON;
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

SzView *sz_view_text_field(SzSignalStr *text, const char *placeholder) {
  SzView *v = view_new(SZ_VIEW_TEXT_FIELD);
  v->sig_str = text;
  v->placeholder = sz_strdup(placeholder ? placeholder : "");
  v->interactive = 1;
  v->a11y_role = SZ_A11Y_TEXT_FIELD;
  v->a11y_label = sz_strdup(placeholder ? placeholder : "text field");
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
        v->kind == SZ_VIEW_CHIP || v->kind == SZ_VIEW_EXPANSION_TILE ||
        v->kind == SZ_VIEW_CHECKBOX_LIST_TILE) {
      int on = v->sig_int && sz_signal_int_get(v->sig_int) != 0;
      snprintf(live, sizeof live, "%s=%d", v->a11y_label ? v->a11y_label : "",
               on ? 1 : 0);
      label = live;
    }
    if (v->kind == SZ_VIEW_RADIO) {
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
    n = snprintf(line, sizeof line, "%s:%s\n", a11y_role_name(v->a11y_role),
                 label);
    if (n > 0 && *len + (size_t)n < cap) {
      memcpy(buf + *len, line, (size_t)n);
      *len += (size_t)n;
      buf[*len] = '\0';
    }
  }
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
    resolve_text(v, buf, sizeof buf);
    v->frame.w = text_width(buf, font) + theme->pad * 2.f;
    v->frame.h = theme->control_h;
    if (v->frame.w < 48.f)
      v->frame.w = 48.f;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  case SZ_VIEW_ICON_BUTTON:
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
    resolve_text(v, buf, sizeof buf);
    v->frame.w = text_width(buf, font) + theme->pad * 2.f;
    v->frame.h = theme->control_h;
    if (v->frame.w < 32.f)
      v->frame.w = 32.f;
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
    layout_pass_child(v, x, y, min_w, min_h, max_w, max_h, theme);
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
    if (v->kind == SZ_VIEW_LIST)
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    break;
  default:
    break;
  }
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
  if (!scroll || scroll->kind != SZ_VIEW_SCROLL)
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
  return v->kind == SZ_VIEW_SCROLL ? v : NULL;
}

SzView *sz_view_scroll_at(SzView *root, float x, float y) {
  return scroll_at_node(root, x, y);
}

int sz_view_has_focused_text_field(SzView *root) {
  int i;
  if (!root)
    return 0;
  if (root->kind == SZ_VIEW_TEXT_FIELD && root->focused)
    return 1;
  for (i = 0; i < root->child_count; i++) {
    if (sz_view_has_focused_text_field(root->children[i]))
      return 1;
  }
  return 0;
}

static SzView *find_focused_text_field(SzView *root) {
  int i;
  SzView *found;
  if (!root || !view_is_shown(root))
    return NULL;
  if (root->kind == SZ_VIEW_TEXT_FIELD && root->focused)
    return root;
  for (i = 0; i < root->child_count; i++) {
    found = find_focused_text_field(root->children[i]);
    if (found)
      return found;
  }
  return NULL;
}

SzRect sz_view_caret_rect(SzView *root, const SzTheme *theme) {
  SzRect z = {0, 0, 0, 0};
  SzView *f;
  char buf[256];
  float x, y, h;
  if (!root || !theme)
    return z;
  f = find_focused_text_field(root);
  if (!f)
    return z;
  resolve_text(f, buf, sizeof buf);
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

int sz_view_handle_tap(SzView *root, float x, float y) {
  SzView *hit;
  if (!root)
    return 0;
  hit = sz_view_hit_test(root, x, y);
  if (!hit)
    return 0;
  if ((hit->kind == SZ_VIEW_BUTTON || hit->kind == SZ_VIEW_ICON_BUTTON) &&
      hit->on_tap) {
    hit->on_tap(hit, hit->tap_env);
    return 1;
  }
  if ((hit->kind == SZ_VIEW_CHECKBOX || hit->kind == SZ_VIEW_SWITCH ||
       hit->kind == SZ_VIEW_CHIP || hit->kind == SZ_VIEW_EXPANSION_TILE ||
       hit->kind == SZ_VIEW_CHECKBOX_LIST_TILE) &&
      hit->sig_int) {
    int64_t n = sz_signal_int_get(hit->sig_int);
    sz_signal_int_set(hit->sig_int, n == 0 ? 1 : 0);
    return 1;
  }
  if (hit->kind == SZ_VIEW_RADIO && hit->sig_int) {
    sz_signal_int_set(hit->sig_int, hit->radio_value);
    return 1;
  }
  if (hit->kind == SZ_VIEW_SLIDER)
    return sz_view_slider_set_at(hit, x);
  if (hit->kind == SZ_VIEW_TEXT_FIELD) {
    clear_focus(root);
    hit->focused = 1;
    return 1;
  }
  return 0;
}

static int collect_text_fields_node(SzView *v, SzView **out, int cap, int n) {
  int i;
  if (!v || !view_is_shown(v) || n >= cap)
    return n;
  if (v->kind == SZ_VIEW_EXCLUDE_SEMANTICS)
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

int sz_view_handle_text(SzView *root, const char *text) {
  SzView *target = sz_view_text_field_target(root);
  if (!target || !target->sig_str)
    return 0;
  sz_signal_str_set(target->sig_str, text ? text : "");
  target->focused = 1;
  return 1;
}

int sz_view_handle_text_edit(SzView *root, const char *text, int backspace) {
  SzView *target = sz_view_text_field_target(root);
  const char *cur;
  size_t n;
  char *buf;

  if (!target || !target->sig_str)
    return 0;
  cur = sz_signal_str_get(target->sig_str);
  if (!cur)
    cur = "";
  n = strlen(cur);

  if (backspace) {
    if (n > 0) {
      buf = (char *)sz_alloc(n); /* n bytes: drop last, keep NUL */
      memcpy(buf, cur, n - 1);
      buf[n - 1] = '\0';
      sz_signal_str_set(target->sig_str, buf);
      sz_free(buf);
    }
  } else if (text && text[0]) {
    size_t add = strlen(text);
    buf = (char *)sz_alloc(n + add + 1);
    memcpy(buf, cur, n);
    memcpy(buf + n, text, add + 1);
    sz_signal_str_set(target->sig_str, buf);
    sz_free(buf);
  }
  target->focused = 1;
  return 1;
}
