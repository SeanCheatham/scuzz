#include "scuzz_ui.h"

#include "sk_capi.h"

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
  int toggled; /* LABEL */
  int img_w;
  int img_h;
  char glyph;
  float scroll_y;
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

  /* View.align: 0=start, 1=center, 2=end on each axis. */
  int align_x;
  int align_y;
  /* View.positioned: offset from Stack origin. */
  int pos_x;
  int pos_y;
  /* View.padding: uniform inset. */
  int pad;
  /* View.sized / View.minSize: requested w×h (img_* reused). */
};

static SzView *view_new(SzViewKind kind) {
  SzView *v = (SzView *)sz_alloc_zero(sizeof(SzView));
  v->kind = kind;
  return v;
}

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
         kind == SZ_VIEW_LIST || kind == SZ_VIEW_SCROLL ||
         kind == SZ_VIEW_EXPANDED || kind == SZ_VIEW_CENTER ||
         kind == SZ_VIEW_ALIGN || kind == SZ_VIEW_STACK ||
         kind == SZ_VIEW_POSITIONED || kind == SZ_VIEW_PADDING ||
         kind == SZ_VIEW_SIZED || kind == SZ_VIEW_MIN_SIZE;
}

SzViewKind sz_view_kind(const SzView *view) {
  return view ? view->kind : (SzViewKind)0;
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
  default:
    return "none";
  }
}

static void a11y_dump_node(SzView *v, char *buf, size_t cap, size_t *len) {
  int i;
  if (!v || !buf || !len || !view_is_shown(v))
    return;
  if (v->a11y_role != SZ_A11Y_NONE) {
    char line[256];
    int n = snprintf(line, sizeof line, "%s:%s\n", a11y_role_name(v->a11y_role),
                     v->a11y_label ? v->a11y_label : "");
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
    char line[256];
    snprintf(line, sizeof line, "- %s", s ? sz_string_cstr(s) : "");
    sz_view_add_child(v, sz_view_text(line));
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

SzView *sz_view_expanded(SzView *child) {
  SzView *v = view_new(SZ_VIEW_EXPANDED);
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

SzView *sz_view_label(const char *text, uint32_t bg_argb, uint32_t fg_argb) {
  SzView *v = view_new(SZ_VIEW_LABEL);
  v->text = sz_strdup(text);
  v->bg_argb = bg_argb;
  v->fg_argb = fg_argb;
  v->interactive = 1;
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

static void layout_node_ex(SzView *v, float x, float y, float min_w, float min_h,
                           float max_w, float max_h, const SzTheme *theme);

static void layout_node(SzView *v, float x, float y, float max_w, float max_h,
                        const SzTheme *theme) {
  layout_node_ex(v, x, y, 0.f, 0.f, max_w, max_h, theme);
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
  case SZ_VIEW_TEXT:
    resolve_text(v, buf, sizeof buf);
    v->frame.w = text_width(buf, font) + 4.f;
    v->frame.h = font + 6.f;
    if (v->frame.w > max_w && max_w > 0)
      v->frame.w = max_w;
    break;
  case SZ_VIEW_BUTTON:
    resolve_text(v, buf, sizeof buf);
    v->frame.w = text_width(buf, font) + theme->pad * 2.f;
    v->frame.h = theme->control_h;
    if (v->frame.w < 48.f)
      v->frame.w = 48.f;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  case SZ_VIEW_TEXT_FIELD:
    v->frame.w = max_w > 0 ? max_w : 120.f;
    v->frame.h = theme->control_h;
    break;
  case SZ_VIEW_ICON:
    v->frame.w = font + 4.f;
    v->frame.h = font + 4.f;
    break;
  case SZ_VIEW_IMAGE:
    v->frame.w = (float)v->img_w;
    v->frame.h = (float)v->img_h;
    break;
  case SZ_VIEW_LABEL:
    v->frame.w = max_w;
    v->frame.h = max_h;
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
        if (v->children[i]->kind == SZ_VIEW_EXPANDED)
          n_flex++;
      }
      if (n_flex > 0 && h_budget > 0.f) {
        /* Measure non-flex children for leftover height. */
        for (i = 0; i < v->child_count; i++) {
          SzView *ch = v->children[i];
          if (!view_is_shown(ch) || ch->kind == SZ_VIEW_EXPANDED)
            continue;
          layout_node(ch, x + theme->pad, y + theme->pad, inner_w, max_h, theme);
          fixed_h += ch->frame.h;
        }
        if (col_shown > 1)
          gaps = theme->gap * (float)(col_shown - 1);
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
              layout_node(ch, x + theme->pad, cy, inner_w, max_h, theme);
              continue;
            }
            if (ch->kind == SZ_VIEW_EXPANDED)
              layout_node(ch, x + theme->pad, cy, inner_w, flex_h, theme);
            else
              layout_node(ch, x + theme->pad, cy, inner_w, max_h, theme);
            cy += ch->frame.h + theme->gap;
          }
          v->frame.w = max_w;
          v->frame.h = h_budget;
          break;
        }
      }
    }
    for (i = 0; i < v->child_count; i++) {
      if (!view_is_shown(v->children[i])) {
        layout_node(v->children[i], x + theme->pad, cy, inner_w, max_h, theme);
        continue;
      }
      layout_node(v->children[i], x + theme->pad, cy, inner_w, max_h, theme);
      cy += v->children[i]->frame.h + theme->gap;
      h += v->children[i]->frame.h + theme->gap;
      shown++;
    }
    if (shown > 0)
      h -= theme->gap;
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
    v->frame.w = max_w;
    v->frame.h = max_h > 0 ? max_h : 0.f;
    if (ch) {
      /* Scroll defaults to pref_h=64; inside Expanded, fill the flex slot. */
      if (ch->kind == SZ_VIEW_SCROLL) {
        old_pref = ch->pref_h;
        ch->pref_h = 0.f;
      }
      layout_node(ch, x, y, max_w > 0 ? max_w : v->frame.w, v->frame.h, theme);
      if (ch->kind == SZ_VIEW_SCROLL)
        ch->pref_h = old_pref;
      /* Tight flex slot: child fills Expanded (constraints down). */
      ch->frame.x = x;
      ch->frame.y = y;
      ch->frame.w = v->frame.w;
      ch->frame.h = v->frame.h > 0.f ? v->frame.h : ch->frame.h;
    }
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
      layout_node(ch, x, y, max_w, max_h, theme);
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
      ch->frame.x = x + ox;
      ch->frame.y = y + oy;
      /* Re-layout nested children at the aligned origin. */
      layout_node(ch, ch->frame.x, ch->frame.y, cw, chh, theme);
      ch->frame.w = cw;
      ch->frame.h = chh;
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
    float row_inner_h = max_h > 0 ? max_h - theme->pad * 2.f : 0.f;
    if (row_inner_h < 0.f)
      row_inner_h = 0.f;
    float w_budget = max_w > 0.f ? max_w : min_w;
    for (i = 0; i < v->child_count; i++) {
      if (view_is_shown(v->children[i]) &&
          v->children[i]->kind == SZ_VIEW_EXPANDED)
        n_flex++;
    }
    if (n_flex > 0 && w_budget > 0.f) {
      /* Measure non-flex at intrinsic width (large max_w). */
      for (i = 0; i < v->child_count; i++) {
        SzView *ch = v->children[i];
        if (!view_is_shown(ch) || ch->kind == SZ_VIEW_EXPANDED)
          continue;
        layout_node(ch, x + theme->pad, y + theme->pad, max_w, row_inner_h,
                    theme);
        fixed_w += ch->frame.w;
      }
      if (shown > 1)
        gaps = theme->gap * (float)(shown - 1);
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
            layout_node(ch, cx, y + theme->pad, flex_w, row_inner_h, theme);
            continue;
          }
          if (ch->kind == SZ_VIEW_EXPANDED)
            layout_node(ch, cx, y + theme->pad, flex_w, row_inner_h, theme);
          else
            layout_node(ch, cx, y + theme->pad, max_w, row_inner_h, theme);
          cx += ch->frame.w + theme->gap;
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
            ? (max_w - theme->pad * 2.f - theme->gap * (float)(shown - 1)) /
                  (float)shown
            : max_w;
    if (child_max < 0)
      child_max = 0;
    for (i = 0; i < v->child_count; i++) {
      if (!view_is_shown(v->children[i])) {
        layout_node(v->children[i], cx, y + theme->pad, child_max, max_h, theme);
        continue;
      }
      layout_node(v->children[i], cx, y + theme->pad, child_max, max_h, theme);
      cx += v->children[i]->frame.w + theme->gap;
      w += v->children[i]->frame.w + theme->gap;
      if (v->children[i]->frame.h > inner_h)
        inner_h = v->children[i]->frame.h;
    }
    if (shown > 0)
      w -= theme->gap;
    w += theme->pad;
    v->frame.w = max_w > 0 ? max_w : w;
    v->frame.h = inner_h + theme->pad * 2.f;
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
        layout_node(ch, ix, iy, inner_w, inner_h, theme);
        continue;
      }
      layout_node(ch, ix, iy, inner_w > 0.f ? inner_w : max_w,
                  inner_h > 0.f ? inner_h : max_h, theme);
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
    float px = (float)v->pos_x;
    float py = (float)v->pos_y;
    float cw = 0.f;
    float chh = 0.f;
    float child_max_w = max_w > px ? max_w - px : 0.f;
    float child_max_h = max_h > py ? max_h - py : 0.f;
    if (ch)
      layout_node(ch, x + px, y + py, child_max_w, child_max_h, theme);
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
    float p = (float)v->pad;
    float inner_w = max_w > p * 2.f ? max_w - p * 2.f : 0.f;
    float inner_h = max_h > p * 2.f ? max_h - p * 2.f : 0.f;
    float inner_min_w = min_w > p * 2.f ? min_w - p * 2.f : 0.f;
    float inner_min_h = min_h > p * 2.f ? min_h - p * 2.f : 0.f;
    float cw = 0.f;
    float chh = 0.f;
    if (ch)
      layout_node_ex(ch, x + p, y + p, inner_min_w, inner_min_h, inner_w,
                     inner_h, theme);
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
    float tw = (float)v->img_w;
    float th = (float)v->img_h;
    if (max_w > 0.f && tw > max_w)
      tw = max_w;
    if (max_h > 0.f && th > max_h)
      th = max_h;
    v->frame.w = tw;
    v->frame.h = th;
    if (ch)
      layout_node(ch, x, y, tw, th, theme);
    break;
  }
  case SZ_VIEW_MIN_SIZE: {
    SzView *ch = v->child_count > 0 ? v->children[0] : NULL;
    float child_min_w = (float)v->img_w;
    float child_min_h = (float)v->img_h;
    if (child_min_w < min_w)
      child_min_w = min_w;
    if (child_min_h < min_h)
      child_min_h = min_h;
    if (max_w > 0.f && child_min_w > max_w)
      child_min_w = max_w;
    if (max_h > 0.f && child_min_h > max_h)
      child_min_h = max_h;
    if (ch)
      layout_node_ex(ch, x, y, child_min_w, child_min_h, max_w, max_h, theme);
    v->frame.w = ch ? ch->frame.w : child_min_w;
    v->frame.h = ch ? ch->frame.h : child_min_h;
    break;
  }
  case SZ_VIEW_SCROLL: {
    float inner_w = max_w - theme->pad * 2.f;
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
      layout_node(v->scroll_child, x + theme->pad, y + theme->pad - v->scroll_y,
                  inner_w > 0 ? inner_w : max_w, 10000.f, theme);
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

static void paint_rect(SkCanvas *c, float x, float y, float w, float h,
                       uint32_t argb) {
  SkPaint *p = sk_paint_new();
  if (!p)
    return;
  sk_paint_set_color(p, sk_color_argb(argb));
  sk_canvas_draw_rect(c, x, y, w, h, p);
  sk_paint_delete(p);
}

static void paint_string(SkCanvas *c, const char *s, float x, float y,
                         uint32_t argb, float font_px) {
  SkPaint *p = sk_paint_new();
  if (!p)
    return;
  sk_paint_set_color(p, sk_color_argb(argb));
  sk_paint_set_text_size(p, font_px);
  sk_canvas_draw_string(c, s ? s : "", x, y, p);
  sk_paint_delete(p);
}

static void paint_node(SzView *v, SkCanvas *c, const SzTheme *theme) {
  char buf[256];
  int i;
  float tx, ty;

  if (!v || !c || !view_is_shown(v))
    return;

  switch (v->kind) {
  case SZ_VIEW_LABEL: {
    uint32_t bg = v->toggled ? v->fg_argb : v->bg_argb;
    uint32_t fg = v->toggled ? v->bg_argb : v->fg_argb;
    float pad = 16.f;
    float bar_h = 40.f;
    sk_canvas_clear(c, sk_color_argb(bg));
    paint_rect(c, pad, pad, v->frame.w - pad * 2.f, bar_h, fg);
    paint_string(c, v->text, pad + 8.f, pad + 26.f, bg, theme->font_px);
    return;
  }
  case SZ_VIEW_TEXT:
    resolve_text(v, buf, sizeof buf);
    paint_string(c, buf, v->frame.x + 2.f, v->frame.y + theme->font_px + 2.f,
                 theme->foreground, theme->font_px);
    break;
  case SZ_VIEW_BUTTON:
    resolve_text(v, buf, sizeof buf);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->primary);
    tx = v->frame.x + theme->pad;
    ty = v->frame.y + (v->frame.h + theme->font_px) * 0.5f;
    paint_string(c, buf, tx, ty, theme->on_primary, theme->font_px);
    break;
  case SZ_VIEW_TEXT_FIELD: {
    const char *shown;
    resolve_text(v, buf, sizeof buf);
    shown = buf[0] ? buf : (v->placeholder ? v->placeholder : "");
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    /* border */
    {
      SkPaint *p = sk_paint_new();
      if (p) {
        sk_paint_set_color(p, sk_color_argb(v->focused ? theme->primary : theme->border));
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
  case SZ_VIEW_IMAGE:
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, v->bg_argb);
    if (v->text && v->text[0])
      paint_string(c, v->text, v->frame.x + 4.f,
                   v->frame.y + v->frame.h * 0.5f + 4.f, theme->on_primary,
                   theme->font_px);
    break;
  case SZ_VIEW_COLUMN:
  case SZ_VIEW_ROW:
  case SZ_VIEW_LIST:
  case SZ_VIEW_SCROLL:
  case SZ_VIEW_EXPANDED:
  case SZ_VIEW_CENTER:
  case SZ_VIEW_ALIGN:
  case SZ_VIEW_STACK:
  case SZ_VIEW_POSITIONED:
  case SZ_VIEW_PADDING:
  case SZ_VIEW_SIZED:
  case SZ_VIEW_MIN_SIZE:
    if (v->kind == SZ_VIEW_LIST || v->kind == SZ_VIEW_SCROLL)
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
  if (root->kind != SZ_VIEW_LABEL)
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

float sz_view_scroll_y(const SzView *scroll) {
  if (!scroll || scroll->kind != SZ_VIEW_SCROLL)
    return 0.f;
  return scroll->scroll_y;
}

void sz_view_scroll_by(SzView *scroll, float dy) {
  if (!scroll || scroll->kind != SZ_VIEW_SCROLL)
    return;
  scroll->scroll_y += dy;
  if (scroll->scroll_y < 0.f)
    scroll->scroll_y = 0.f;
}

static SzView *scroll_at_node(SzView *v, float x, float y) {
  int i;
  SzView *found;
  if (!v || !point_in(&v->frame, x, y))
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
  if (hit->kind == SZ_VIEW_LABEL) {
    hit->toggled = !hit->toggled;
    return 1;
  }
  if (hit->kind == SZ_VIEW_BUTTON && hit->on_tap) {
    hit->on_tap(hit, hit->tap_env);
    return 1;
  }
  if (hit->kind == SZ_VIEW_TEXT_FIELD) {
    clear_focus(root);
    hit->focused = 1;
    return 1;
  }
  return 0;
}

/* Focused TextField, else first TextField in DFS order. */
static SzView *find_text_field(SzView *root) {
  int i;
  SzView *target = NULL;
  SzView **stack = NULL;
  int sp = 0, scap = 0;
  SzView *first = NULL;

  if (!root)
    return NULL;

  stack = (SzView **)sz_alloc(sizeof(SzView *) * 32);
  scap = 32;
  stack[sp++] = root;
  while (sp > 0) {
    SzView *n = stack[--sp];
    if (n->kind == SZ_VIEW_TEXT_FIELD) {
      if (!first)
        first = n;
      if (n->focused) {
        target = n;
        break;
      }
    }
    for (i = 0; i < n->child_count; i++) {
      if (sp >= scap) {
        int ncap = scap * 2;
        SzView **ns = (SzView **)sz_alloc(sizeof(SzView *) * (size_t)ncap);
        memcpy(ns, stack, (size_t)scap * sizeof(SzView *));
        sz_free(stack);
        stack = ns;
        scap = ncap;
      }
      stack[sp++] = n->children[i];
    }
  }
  sz_free(stack);
  return target ? target : first;
}

int sz_view_handle_text(SzView *root, const char *text) {
  SzView *target = find_text_field(root);
  if (!target || !target->sig_str)
    return 0;
  sz_signal_str_set(target->sig_str, text ? text : "");
  target->focused = 1;
  return 1;
}

int sz_view_handle_text_edit(SzView *root, const char *text, int backspace) {
  SzView *target = find_text_field(root);
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
