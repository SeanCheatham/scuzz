#include "scalui_ui.h"

#include "sk_capi.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static char *su_strdup(const char *s) {
  size_t n;
  char *out;
  if (!s)
    s = "";
  n = strlen(s);
  out = (char *)su_alloc(n + 1);
  memcpy(out, s, n + 1);
  return out;
}

struct SuView {
  SuViewKind kind;
  SuView *parent;
  SuView **children;
  int child_count;
  int child_cap;
  SuRect frame;
  int interactive; /* participates in hit-test */
  int focused;

  /* common / kind-specific */
  char *text;
  char *prefix;
  char *placeholder;
  SuSignalInt *sig_int;
  SuSignalStr *sig_str;
  SuViewTapFn on_tap;
  void *tap_env;
  uint32_t bg_argb;
  uint32_t fg_argb;
  int toggled; /* LABEL */
  int img_w;
  int img_h;
  char glyph;
  float scroll_y;
  float pref_h; /* >0 overrides natural height when set */
  SuView *scroll_child; /* owned as sole child for SCROLL */

  /* a11y */
  SuA11yRole a11y_role;
  char *a11y_label;

  /* showWhen: when show_when_sig != NULL, visible iff signal == show_when_value */
  SuSignalInt *show_when_sig;
  int64_t show_when_value;

  /* View.each: rebuild children from Signal.list at layout (pull). */
  SuSignalList *each_sig;
  SuList *each_seen; /* last synced list pointer (not owned) */
};

static SuView *view_new(SuViewKind kind) {
  SuView *v = (SuView *)su_alloc_zero(sizeof(SuView));
  v->kind = kind;
  return v;
}

static int view_is_shown(const SuView *v) {
  if (!v)
    return 0;
  if (v->show_when_sig &&
      su_signal_int_get(v->show_when_sig) != v->show_when_value)
    return 0;
  return 1;
}

static int count_shown_children(const SuView *v) {
  int i, n = 0;
  if (!v)
    return 0;
  for (i = 0; i < v->child_count; i++) {
    if (view_is_shown(v->children[i]))
      n++;
  }
  return n;
}

SuViewKind su_view_kind(const SuView *view) {
  return view ? view->kind : (SuViewKind)0;
}

SuRect su_view_frame(const SuView *view) {
  SuRect z = {0, 0, 0, 0};
  return view ? view->frame : z;
}

void su_view_add_child(SuView *parent, SuView *child) {
  if (!parent || !child)
    return;
  if (parent->kind != SU_VIEW_COLUMN && parent->kind != SU_VIEW_ROW &&
      parent->kind != SU_VIEW_LIST && parent->kind != SU_VIEW_SCROLL)
    su_panic("su_view_add_child: parent cannot have children");
  if (parent->child_count >= parent->child_cap) {
    int ncap = parent->child_cap ? parent->child_cap * 2 : 4;
    SuView **n =
        (SuView **)su_alloc((size_t)ncap * sizeof(SuView *));
    if (parent->children) {
      memcpy(n, parent->children, (size_t)parent->child_count * sizeof(SuView *));
      su_free(parent->children);
    }
    parent->children = n;
    parent->child_cap = ncap;
  }
  parent->children[parent->child_count++] = child;
  child->parent = parent;
  if (parent->kind == SU_VIEW_SCROLL)
    parent->scroll_child = child;
}

SuView *su_view_text(const char *text) {
  SuView *v = view_new(SU_VIEW_TEXT);
  v->text = su_strdup(text);
  v->a11y_role = SU_A11Y_TEXT;
  v->a11y_label = su_strdup(text);
  return v;
}

SuView *su_view_text_signal_int(SuSignalInt *sig, const char *prefix) {
  SuView *v = view_new(SU_VIEW_TEXT);
  v->a11y_role = SU_A11Y_TEXT;
  v->a11y_label = su_strdup(prefix ? prefix : "value");
  v->sig_int = sig;
  v->prefix = su_strdup(prefix ? prefix : "");
  return v;
}

SuView *su_view_text_signal_str(SuSignalStr *sig) {
  SuView *v = view_new(SU_VIEW_TEXT);
  v->sig_str = sig;
  return v;
}

SuView *su_view_button(const char *label, SuViewTapFn on_tap, void *env) {
  SuView *v = view_new(SU_VIEW_BUTTON);
  v->text = su_strdup(label);
  v->on_tap = on_tap;
  v->tap_env = env;
  v->interactive = 1;
  v->a11y_role = SU_A11Y_BUTTON;
  v->a11y_label = su_strdup(label);
  return v;
}

SuView *su_view_text_field(SuSignalStr *text, const char *placeholder) {
  SuView *v = view_new(SU_VIEW_TEXT_FIELD);
  v->sig_str = text;
  v->placeholder = su_strdup(placeholder ? placeholder : "");
  v->interactive = 1;
  v->a11y_role = SU_A11Y_TEXT_FIELD;
  v->a11y_label = su_strdup(placeholder ? placeholder : "text field");
  return v;
}

void su_view_set_a11y(SuView *view, SuA11yRole role, const char *label) {
  if (!view)
    return;
  view->a11y_role = role;
  su_free(view->a11y_label);
  view->a11y_label = su_strdup(label ? label : "");
}

SuA11yRole su_view_a11y_role(const SuView *view) {
  return view ? view->a11y_role : SU_A11Y_NONE;
}

const char *su_view_a11y_label(const SuView *view) {
  return view && view->a11y_label ? view->a11y_label : "";
}

static const char *a11y_role_name(SuA11yRole role) {
  switch (role) {
  case SU_A11Y_BUTTON:
    return "button";
  case SU_A11Y_TEXT:
    return "text";
  case SU_A11Y_TEXT_FIELD:
    return "textfield";
  case SU_A11Y_IMAGE:
    return "image";
  case SU_A11Y_LIST:
    return "list";
  case SU_A11Y_SCROLL:
    return "scroll";
  default:
    return "none";
  }
}

static void a11y_dump_node(SuView *v, char *buf, size_t cap, size_t *len) {
  int i;
  if (!v || !buf || !len || !view_is_shown(v))
    return;
  if (v->a11y_role != SU_A11Y_NONE) {
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

SuString *su_view_a11y_dump(SuView *root) {
  char buf[4096];
  size_t len = 0;
  buf[0] = '\0';
  a11y_dump_node(root, buf, sizeof buf, &len);
  return su_string_from_cstr(buf);
}

SuView *su_view_column(void) { return view_new(SU_VIEW_COLUMN); }
SuView *su_view_row(void) { return view_new(SU_VIEW_ROW); }
SuView *su_view_list(void) {
  SuView *v = view_new(SU_VIEW_LIST);
  v->a11y_role = SU_A11Y_LIST;
  v->a11y_label = su_strdup("list");
  return v;
}

SuView *su_view_each(SuSignalList *sig) {
  SuView *v = su_view_list();
  v->each_sig = sig;
  /* Sentinel: force first sync even when the list is empty (NULL). */
  v->each_seen = (SuList *)(uintptr_t)1;
  return v;
}

static void sync_each(SuView *v) {
  SuList *xs;
  SuList *p;
  if (!v || !v->each_sig)
    return;
  xs = su_signal_list_get(v->each_sig);
  if (xs == v->each_seen)
    return;
  su_view_clear_children(v);
  for (p = xs; p; p = p->tail) {
    SuString *s = (SuString *)p->head;
    char line[256];
    snprintf(line, sizeof line, "- %s", s ? su_string_cstr(s) : "");
    su_view_add_child(v, su_view_text(line));
  }
  v->each_seen = xs;
}

SuView *su_view_scroll(SuView *child) {
  SuView *v = view_new(SU_VIEW_SCROLL);
  v->pref_h = 64.f;
  v->a11y_role = SU_A11Y_SCROLL;
  v->a11y_label = su_strdup("scroll");
  if (child)
    su_view_add_child(v, child);
  return v;
}

SuView *su_view_image(int w, int h, uint32_t argb, const char *caption) {
  SuView *v = view_new(SU_VIEW_IMAGE);
  v->img_w = w > 0 ? w : 32;
  v->img_h = h > 0 ? h : 32;
  v->bg_argb = argb ? argb : 0xFF888888u;
  v->text = su_strdup(caption ? caption : "");
  v->a11y_role = SU_A11Y_IMAGE;
  v->a11y_label = su_strdup(caption && caption[0] ? caption : "image");
  return v;
}

SuView *su_view_icon(char glyph, uint32_t argb) {
  SuView *v = view_new(SU_VIEW_ICON);
  v->glyph = glyph ? glyph : '*';
  v->fg_argb = argb ? argb : 0xFF1A1A1Au;
  return v;
}

SuView *su_view_label(const char *text, uint32_t bg_argb, uint32_t fg_argb) {
  SuView *v = view_new(SU_VIEW_LABEL);
  v->text = su_strdup(text);
  v->bg_argb = bg_argb;
  v->fg_argb = fg_argb;
  v->interactive = 1;
  return v;
}

void su_view_set_show_when(SuView *view, SuSignalInt *sig, int64_t value) {
  if (!view)
    return;
  view->show_when_sig = sig;
  view->show_when_value = value;
}

SuView *su_view_show_when(SuSignalInt *sig, int64_t value, SuView *child) {
  if (child)
    su_view_set_show_when(child, sig, value);
  return child;
}

void su_view_free(SuView *view) {
  int i;
  if (!view)
    return;
  for (i = 0; i < view->child_count; i++)
    su_view_free(view->children[i]);
  su_free(view->children);
  su_free(view->text);
  su_free(view->prefix);
  su_free(view->placeholder);
  su_free(view->a11y_label);
  /* Signals are owned by the demo/session, not the view. */
  su_free(view);
}

void su_view_clear_children(SuView *parent) {
  int i;
  if (!parent)
    return;
  if (parent->kind != SU_VIEW_COLUMN && parent->kind != SU_VIEW_ROW &&
      parent->kind != SU_VIEW_LIST && parent->kind != SU_VIEW_SCROLL)
    su_panic("su_view_clear_children: parent cannot have children");
  for (i = 0; i < parent->child_count; i++) {
    parent->children[i]->parent = NULL;
    su_view_free(parent->children[i]);
  }
  parent->child_count = 0;
  parent->scroll_child = NULL;
}

static float text_width(const char *s, float font_px) {
  size_t n = s ? strlen(s) : 0;
  return (float)n * font_px;
}

static void resolve_text(const SuView *v, char *buf, size_t buflen) {
  if (!buf || buflen == 0)
    return;
  buf[0] = '\0';
  if (v->sig_int) {
    snprintf(buf, buflen, "%s%lld", v->prefix ? v->prefix : "",
             (long long)su_signal_int_get(v->sig_int));
  } else if (v->sig_str) {
    snprintf(buf, buflen, "%s", su_signal_str_get(v->sig_str));
  } else if (v->text) {
    snprintf(buf, buflen, "%s", v->text);
  }
}

static void layout_node(SuView *v, float x, float y, float max_w, float max_h,
                        const SuTheme *theme) {
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
  case SU_VIEW_TEXT:
    resolve_text(v, buf, sizeof buf);
    v->frame.w = text_width(buf, font) + 4.f;
    v->frame.h = font + 6.f;
    if (v->frame.w > max_w && max_w > 0)
      v->frame.w = max_w;
    break;
  case SU_VIEW_BUTTON:
    resolve_text(v, buf, sizeof buf);
    v->frame.w = text_width(buf, font) + theme->pad * 2.f;
    v->frame.h = theme->control_h;
    if (v->frame.w < 48.f)
      v->frame.w = 48.f;
    if (max_w > 0 && v->frame.w > max_w)
      v->frame.w = max_w;
    break;
  case SU_VIEW_TEXT_FIELD:
    v->frame.w = max_w > 0 ? max_w : 120.f;
    v->frame.h = theme->control_h;
    break;
  case SU_VIEW_ICON:
    v->frame.w = font + 4.f;
    v->frame.h = font + 4.f;
    break;
  case SU_VIEW_IMAGE:
    v->frame.w = (float)v->img_w;
    v->frame.h = (float)v->img_h;
    break;
  case SU_VIEW_LABEL:
    v->frame.w = max_w;
    v->frame.h = max_h;
    break;
  case SU_VIEW_COLUMN:
  case SU_VIEW_LIST: {
    float cy = y + theme->pad;
    float inner_w = max_w - theme->pad * 2.f;
    float h = theme->pad;
    int shown = 0;
    if (v->kind == SU_VIEW_LIST)
      sync_each(v);
    if (inner_w < 0)
      inner_w = 0;
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
    if (max_h > 0 && v->frame.h > max_h && v->kind == SU_VIEW_LIST)
      v->frame.h = max_h;
    break;
  }
  case SU_VIEW_ROW: {
    float cx = x + theme->pad;
    float inner_h = theme->control_h;
    float w = theme->pad;
    int shown = count_shown_children(v);
    float child_max =
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
  case SU_VIEW_SCROLL: {
    float inner_w = max_w - theme->pad * 2.f;
    float vh = v->pref_h > 0 ? v->pref_h : (max_h > 0 ? max_h : 100.f);
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
}

void su_view_layout(SuView *root, float width, float height, const SuTheme *theme) {
  if (!root || !theme)
    return;
  layout_node(root, 0.f, 0.f, width, height, theme);
}

static int point_in(const SuRect *r, float x, float y) {
  return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static SuView *hit_node(SuView *v, float x, float y) {
  int i;
  SuView *hit;
  if (!v || !view_is_shown(v) || !point_in(&v->frame, x, y))
    return NULL;
  /* Front-to-back: last child wins. */
  for (i = v->child_count - 1; i >= 0; i--) {
    hit = hit_node(v->children[i], x, y);
    if (hit)
      return hit;
  }
  if (v->kind == SU_VIEW_SCROLL && v->scroll_child) {
    hit = hit_node(v->scroll_child, x, y);
    if (hit)
      return hit;
  }
  return v->interactive ? v : NULL;
}

SuView *su_view_hit_test(SuView *root, float x, float y) {
  return hit_node(root, x, y);
}

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
                         uint32_t argb) {
  SkPaint *p = sk_paint_new();
  if (!p)
    return;
  sk_paint_set_color(p, sk_color_argb(argb));
  sk_canvas_draw_string(c, s ? s : "", x, y, p);
  sk_paint_delete(p);
}

static void paint_node(SuView *v, SkCanvas *c, const SuTheme *theme) {
  char buf[256];
  int i;
  float tx, ty;

  if (!v || !c || !view_is_shown(v))
    return;

  switch (v->kind) {
  case SU_VIEW_LABEL: {
    uint32_t bg = v->toggled ? v->fg_argb : v->bg_argb;
    uint32_t fg = v->toggled ? v->bg_argb : v->fg_argb;
    float pad = 16.f;
    float bar_h = 40.f;
    sk_canvas_clear(c, sk_color_argb(bg));
    paint_rect(c, pad, pad, v->frame.w - pad * 2.f, bar_h, fg);
    paint_string(c, v->text, pad + 8.f, pad + 26.f, bg);
    return;
  }
  case SU_VIEW_TEXT:
    resolve_text(v, buf, sizeof buf);
    paint_string(c, buf, v->frame.x + 2.f, v->frame.y + theme->font_px + 2.f,
                 theme->foreground);
    break;
  case SU_VIEW_BUTTON:
    resolve_text(v, buf, sizeof buf);
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->primary);
    tx = v->frame.x + theme->pad;
    ty = v->frame.y + (v->frame.h + theme->font_px) * 0.5f;
    paint_string(c, buf, tx, ty, theme->on_primary);
    break;
  case SU_VIEW_TEXT_FIELD: {
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
    paint_string(c, shown, v->frame.x + 6.f,
                 v->frame.y + (v->frame.h + theme->font_px) * 0.5f,
                 buf[0] ? theme->foreground : theme->muted);
    break;
  }
  case SU_VIEW_ICON: {
    char g[2] = {v->glyph, '\0'};
    paint_string(c, g, v->frame.x + 2.f, v->frame.y + theme->font_px + 2.f,
                 v->fg_argb);
    break;
  }
  case SU_VIEW_IMAGE:
    paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, v->bg_argb);
    if (v->text && v->text[0])
      paint_string(c, v->text, v->frame.x + 4.f,
                   v->frame.y + v->frame.h * 0.5f + 4.f, theme->on_primary);
    break;
  case SU_VIEW_COLUMN:
  case SU_VIEW_ROW:
  case SU_VIEW_LIST:
  case SU_VIEW_SCROLL:
    if (v->kind == SU_VIEW_LIST || v->kind == SU_VIEW_SCROLL)
      paint_rect(c, v->frame.x, v->frame.y, v->frame.w, v->frame.h, theme->surface);
    for (i = 0; i < v->child_count; i++)
      paint_node(v->children[i], c, theme);
    break;
  default:
    break;
  }
}

/* Internal: used by ui.c */
int su_view_paint(SuView *root, SkCanvas *canvas, int width, int height,
                  const SuTheme *theme) {
  if (!root || !canvas || !theme)
    return 0;
  if (root->kind != SU_VIEW_LABEL)
    sk_canvas_clear(canvas, sk_color_argb(theme->background));
  su_view_layout(root, (float)width, (float)height, theme);
  paint_node(root, canvas, theme);
  return 1;
}

static void clear_focus(SuView *v) {
  int i;
  if (!v)
    return;
  v->focused = 0;
  for (i = 0; i < v->child_count; i++)
    clear_focus(v->children[i]);
  if (v->kind == SU_VIEW_SCROLL && v->scroll_child)
    clear_focus(v->scroll_child);
}

float su_view_scroll_y(const SuView *scroll) {
  if (!scroll || scroll->kind != SU_VIEW_SCROLL)
    return 0.f;
  return scroll->scroll_y;
}

void su_view_scroll_by(SuView *scroll, float dy) {
  if (!scroll || scroll->kind != SU_VIEW_SCROLL)
    return;
  scroll->scroll_y += dy;
  if (scroll->scroll_y < 0.f)
    scroll->scroll_y = 0.f;
}

static SuView *scroll_at_node(SuView *v, float x, float y) {
  int i;
  SuView *found;
  if (!v || !point_in(&v->frame, x, y))
    return NULL;
  for (i = v->child_count - 1; i >= 0; i--) {
    found = scroll_at_node(v->children[i], x, y);
    if (found)
      return found;
  }
  if (v->kind == SU_VIEW_SCROLL && v->scroll_child) {
    found = scroll_at_node(v->scroll_child, x, y);
    if (found)
      return found;
  }
  return v->kind == SU_VIEW_SCROLL ? v : NULL;
}

SuView *su_view_scroll_at(SuView *root, float x, float y) {
  return scroll_at_node(root, x, y);
}

int su_view_has_focused_text_field(SuView *root) {
  int i;
  if (!root)
    return 0;
  if (root->kind == SU_VIEW_TEXT_FIELD && root->focused)
    return 1;
  for (i = 0; i < root->child_count; i++) {
    if (su_view_has_focused_text_field(root->children[i]))
      return 1;
  }
  if (root->kind == SU_VIEW_SCROLL && root->scroll_child)
    return su_view_has_focused_text_field(root->scroll_child);
  return 0;
}

int su_view_handle_tap(SuView *root, float x, float y) {
  SuView *hit;
  if (!root)
    return 0;
  hit = su_view_hit_test(root, x, y);
  if (!hit)
    return 0;
  if (hit->kind == SU_VIEW_LABEL) {
    hit->toggled = !hit->toggled;
    return 1;
  }
  if (hit->kind == SU_VIEW_BUTTON && hit->on_tap) {
    hit->on_tap(hit, hit->tap_env);
    return 1;
  }
  if (hit->kind == SU_VIEW_TEXT_FIELD) {
    clear_focus(root);
    hit->focused = 1;
    return 1;
  }
  return 0;
}

int su_view_handle_text(SuView *root, const char *text) {
  /* Find focused TextField, else first TextField. */
  int i;
  SuView *target = NULL;

  if (!root)
    return 0;

  /* DFS search for focused, then any. */
  {
    SuView **stack = NULL;
    int sp = 0, scap = 0;
    SuView *first = NULL;
    stack = (SuView **)su_alloc(sizeof(SuView *) * 32);
    scap = 32;
    stack[sp++] = root;
    while (sp > 0) {
      SuView *n = stack[--sp];
      if (n->kind == SU_VIEW_TEXT_FIELD) {
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
          SuView **ns = (SuView **)su_alloc(sizeof(SuView *) * (size_t)ncap);
          memcpy(ns, stack, (size_t)scap * sizeof(SuView *));
          su_free(stack);
          stack = ns;
          scap = ncap;
        }
        stack[sp++] = n->children[i];
      }
    }
    su_free(stack);
    if (!target)
      target = first;
  }
  if (!target || !target->sig_str)
    return 0;
  su_signal_str_set(target->sig_str, text ? text : "");
  target->focused = 1;
  return 1;
}
