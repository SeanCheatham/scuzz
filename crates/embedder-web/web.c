#include "web.h"
#include "sk_capi.h"
#include <stdlib.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <string.h>
#include <stdio.h>

static SzUiSession *active;
static SzString *snapshot;

static int loop_paused;

void sz_web_idle_wake(void) {
  if (!loop_paused)
    return;
  loop_paused = 0;
  emscripten_resume_main_loop();
}

void sz_web_loop_pause(void) {
  if (loop_paused)
    return;
  loop_paused = 1;
  emscripten_pause_main_loop();
}

void sz_web_live_loop(void (*frame)(void)) {
  /* rAF. Return so a later JS ccall is a new WASM entry. An idle frame
   * pauses the loop. sz_web_idle_wake resumes it. */
  emscripten_set_main_loop(frame, 0, 0);
}

EM_JS(void, sz_web_text_scale, (float scale), { Module.scuzzTextScale = scale > 0 ? scale : 1; });

EM_JS(void, sz_web_frame_begin, (float scale), {
  Module.webFrame = {scale, texts: [], sections: [], items: [], groups: [], transitions: []};
});
EM_JS(void, sz_web_group_begin_id, (uintptr_t view, int role, const char *label, uintptr_t related, int hidden, float x, float y, float w, float h, int tab_stop), {
  const f = Module.webFrame;
  f.items.push({id: Number(view), role, label: UTF8ToString(Number(label)), related: Number(related),
    group: true, tabStop: Boolean(tab_stop), hidden: Boolean(hidden), x, y, w, h, parent: f.groups.at(-1)});
  f.groups.push(Number(view));
});
EM_JS(void, sz_web_group_end, (), { Module.webFrame.groups.pop(); });
EM_JS(void, sz_web_text_begin_id, (uintptr_t view, const char *text, int level, uint32_t ink, uint32_t selection, int opacity), {
  const item = {id: Number(view), text: UTF8ToString(Number(text)), level, ink, selection, opacity, lines: [], parent: Module.webFrame.groups.at(-1)};
  Module.webFrame.texts.push(item);
  Module.webFrame.items.push(item);
});
EM_JS(void, sz_web_control_id, (uintptr_t view, int role, const char *label, const char *route,
    const char *copy, const char *value, uintptr_t related, int checked, float x, float y, float w, float h,
    int clipped, float cx, float cy, float cw, float ch, uint32_t ink, uint32_t selection, float font, float line_h, float inset, float gutter, int tab_stop, int opacity), {
  Module.webFrame.items.push({id: Number(view), role, label: UTF8ToString(Number(label)),
    related: Number(related), parent: Module.webFrame.groups.at(-1),
    route: route ? UTF8ToString(Number(route)) : null,
    copy: copy ? UTF8ToString(Number(copy)) : null, value: UTF8ToString(Number(value)),
    checked, opacity, tabStop: Boolean(tab_stop), x, y, w, h, ink, selection, font, line_h, inset, gutter, clip: clipped ? [cx, cy, cw, ch] : null});
});
EM_JS(void, web_transition, (uintptr_t view, int value, int target, int remaining, int suppressed), {
  Module.webFrame.transitions.push({id: Number(view), kind: 'opacity', value, target, remaining, suppressed: Boolean(suppressed)});
});
void sz_web_transition(SzView *view, int value, int target, int remaining, int suppressed) {
  web_transition(sz_view_id(view), value, target, remaining, suppressed);
}

EM_JS(void, web_size_transition, (uintptr_t view, int width, int height, int target_width, int target_height, int remaining, int suppressed), {
  Module.webFrame.transitions.push({id: Number(view), kind: 'size', width, height,
    targetWidth: target_width, targetHeight: target_height, remaining, suppressed: Boolean(suppressed)});
});
void sz_web_size_transition(SzView *view, int width, int height, int target_width, int target_height, int remaining, int suppressed) {
  web_size_transition(sz_view_id(view), width, height, target_width, target_height, remaining, suppressed);
}

EM_JS(void, web_select_state, (uintptr_t owner, uintptr_t popup, uintptr_t active, int count), {
  const item = Module.webFrame.items.at(-1);
  item.active = Number(active); item.optionCount = count;
  Module.webFrame.items.push({id: Number(popup), group: true, role: 56, label: item.label,
    parent: item.parent, related: Number(owner), hidden: !item.checked, x: 0, y: 0, w: 0, h: 0});
});
void sz_web_select_state(SzView *view, SzView *popup, SzView *active, int count) {
  web_select_state(sz_view_id(view), sz_view_id(popup), sz_view_id(active), count);
}
EM_JS(void, web_select_list, (uintptr_t popup, float x, float y, float w, float h), {
  const item = Module.webFrame.items.find(item => item.id === Number(popup));
  if (item) Object.assign(item, {x, y, w, h, hidden: false});
});
void sz_web_select_list(SzView *view, SzView *popup, SzRect frame) {
  (void)view;
  web_select_list(sz_view_id(popup), frame.x, frame.y, frame.w, frame.h);
}
EM_JS(void, web_select_option, (uintptr_t popup, int index, int count), {
  const item = Module.webFrame.items.at(-1);
  item.parent = Number(popup); item.optionIndex = index; item.optionCount = count;
});
void sz_web_select_option(SzView *option, SzView *view, int index, int count) {
  (void)option;
  web_select_option(sz_view_id(sz_view_select_popup(view)), index, count);
}

EM_JS(void, web_tooltip_id, (uintptr_t id, const char *text, float x, float y, float w, float h, int open), {
  Module.webFrame.items.push({id: Number(id), tooltip: true, label: UTF8ToString(Number(text)),
    x, y, w, h, hidden: !open});
});
void sz_web_tooltip(SzView *view, const char *text, SzRect frame, int open) {
  web_tooltip_id(sz_view_id(view), text, frame.x, frame.y, frame.w, frame.h, open);
}
EM_JS(void, web_description, (uintptr_t id), {
  Module.webFrame.items.at(-1).description = Number(id);
});
EM_JS(void, sz_web_copy_id, (uintptr_t view, const char *text), {
  Module.copyText(Number(view), UTF8ToString(Number(text)));
});
void sz_web_group_begin(SzView *view, int role, const char *label, SzView *related,
                         int hidden, float x, float y, float w, float h) {
  sz_web_group_begin_id(sz_view_id(view), role, label, sz_view_id(related), hidden, x, y, w, h, sz_view_tab_stop(view));
}
void sz_web_text_begin(SzView *view, const char *text, int level, uint32_t ink, uint32_t selection) {
  sz_web_text_begin_id(sz_view_id(view), text, level, ink, selection, sz_view_paint_opacity(view));
}
EM_JS(void, web_slider_range, (const char *minimum, const char *maximum, const char *value), {
  Module.webFrame.items.at(-1).range = {minimum: UTF8ToString(Number(minimum)),
    maximum: UTF8ToString(Number(maximum)), value: UTF8ToString(Number(value))};
});
void sz_web_control(SzView *view, int role, const char *label, const char *route,
                    const char *copy, const char *value, SzView *related,
                    int checked, float x, float y, float w, float h,
                    int clipped, float cx, float cy, float cw, float ch, uint32_t ink, uint32_t selection, float font, float line_h, float inset, float gutter) {
  sz_web_control_id(sz_view_id(view), role, label, route, copy, value,
                    sz_view_id(related), checked, x, y, w, h, clipped, cx, cy, cw, ch, ink, selection, font, line_h, inset, gutter, sz_view_tab_stop(view), sz_view_paint_opacity(view));
  web_description(sz_view_id(sz_view_tooltip_owner(view)));
  if (role == SZ_A11Y_SLIDER || role == SZ_A11Y_SPLIT || role == SZ_A11Y_PROGRESS || role == SZ_A11Y_CIRCULAR) {
    char minimum[32], maximum[32], current[32];
    snprintf(minimum, sizeof minimum, "%lld", (long long)(role == SZ_A11Y_SLIDER ? sz_view_slider_minimum(view) : 0));
    snprintf(maximum, sizeof maximum, "%lld", (long long)(role == SZ_A11Y_SLIDER ? sz_view_slider_maximum(view) : 100));
    snprintf(current, sizeof current, "%lld", (long long)(role == SZ_A11Y_SLIDER ? sz_view_slider_value(view) :
      role == SZ_A11Y_SPLIT ? sz_view_split_frac(view) : sz_view_progress_value(view)));
    web_slider_range(minimum, maximum, current);
  }
}
void sz_web_copy(SzView *view, const char *text) {
  sz_web_copy_id(sz_view_id(view), text);
}
EM_JS(void, sync_editor_tab, (uintptr_t id, const char *value, int start, int end), {
  const node = Module.nodes.get(Number(id));
  if (!node) return;
  const text = UTF8ToString(Number(value));
  const bytes = new TextEncoder().encode(text);
  const decoder = new TextDecoder();
  node.value = text;
  node.committed = text;
  node.setSelectionRange(decoder.decode(bytes.subarray(0, start)).length,
    decoder.decode(bytes.subarray(0, end)).length);
});

EM_JS(void, web_inline_action, (uintptr_t parent, const SzRect *boxes, int count), {
  const item = Module.webFrame.items.at(-1);
  item.parent = Number(parent);
  item.inline = true;
  const start = Number(boxes) / 4;
  item.boxes = Array.from({length: count}, (_, i) =>
    Array.from({length: 4}, (_, j) => HEAPF32[start + i * 4 + j]));
});
void sz_web_inline_action(SzView *text, const SzRect *boxes, int count) {
  web_inline_action(sz_view_id(text), boxes, count);
}

EM_JS(void, web_text_fragments, (const char *source, int context_start, float context_x,
    const float *leading, const float *trailing, int start, int end,
    const char *text, float x, float y, float width, float font, float line_h,
    int clipped, float cx, float cy, float cw, float ch, uint32_t ink, int style, int synthetic, uintptr_t action), {
  const lines = Module.webFrame.texts.at(-1).lines;
  const base = Number(source), display = UTF8ToString(Number(text));
  const offset = byte => byte ? UTF8ToString(base, byte).length : 0;
  const add = (text, first, last, left, width, rtl) => lines.push({
    start: offset(first), end: offset(last), text, x: left, y, width, font, line_h, ink, style,
    synthetic: Boolean(synthetic), action: Number(action), dir: rtl ? 'rtl' : 'ltr',
    clip: clipped ? [cx, cy, cw, ch] : null
  });
  if (synthetic || !Number(leading) || !display.length) { add(display, start, end, x, width, false); return; }
  const encoder = new TextEncoder();
  const total = encoder.encode(display).length;
  let byte = 0, unit = 0, firstByte = 0, firstUnit = 0, left = 0, right = 0, rtl, ready = false;
  const flush = () => {
    const last = byte === total ? end : Math.min(end, start + byte);
    add(display.slice(firstUnit, unit), Math.min(end, start + firstByte), last,
      context_x + left, right - left, Boolean(rtl));
  };
  for (const point of display) {
    const at = start - context_start + byte;
    const a = HEAPF32[Number(leading) / 4 + at], b = HEAPF32[Number(trailing) / 4 + at];
    const nextLeft = Math.min(a, b), nextRight = Math.max(a, b);
    const direction = Math.abs(a - b) > .001 ? b < a : rtl;
    if (ready && (rtl !== undefined && direction !== undefined && direction !== rtl ||
        nextLeft > right + .01 || nextRight < left - .01)) {
      flush(); firstByte = byte; firstUnit = unit; ready = false; rtl = undefined;
    }
    left = ready ? Math.min(left, nextLeft) : nextLeft;
    right = ready ? Math.max(right, nextRight) : nextRight;
    if (direction !== undefined) rtl = direction;
    ready = true;
    byte += encoder.encode(point).length; unit += point.length;
  }
  if (ready) flush();
});

void sz_web_text_line(const char *source, SkTextRange context, float context_x, int start, int end,
    const char *text, float x, float y, float width, float font, float line_h,
    int clipped, float cx, float cy, float cw, float ch, uint32_t ink, int style, int synthetic, uintptr_t action) {
  size_t count = context.end - context.start + 1;
  float *positions = !synthetic && count < SIZE_MAX / (2 * sizeof(float)) ? malloc(count * 2 * sizeof(float)) : NULL;
  if (positions) sk_font_text_positions(context, font, style, positions, positions + count);
  web_text_fragments(source, (int)context.start, context_x, positions, positions ? positions + count : NULL,
    start, end, text, x, y, width, font, line_h, clipped, cx, cy, cw, ch, ink, style, synthetic, action);
  free(positions);
}
EM_JS(int, sz_web_book_begin, (), {
  if (Module.webFrame.bookSeen) return 0;
  Module.webFrame.bookSeen = true;
  return 1;
});
EM_JS(void, sz_web_section, (const char *id, const char *title, int selected), {
  Module.webFrame.sections.push({id: UTF8ToString(Number(id)), title: UTF8ToString(Number(title)), selected});
});
EM_JS(void, sz_web_frame_end, (), { Module.presentText(Module.webFrame); });


EM_JS(void, sz_web_present, (int width, int height, const uint8_t *rgba), {
  const canvas = Module.canvas;
  if (canvas.width !== width) canvas.width = width;
  if (canvas.height !== height) canvas.height = height;
  /* Reuse the ImageData and context. A fresh pair per paint allocates the
   * full framebuffer twice. */
  let frame = Module.presentFrame;
  if (!frame || frame.width !== width || frame.height !== height)
    frame = Module.presentFrame = {width, height,
      context: canvas.getContext('2d'), image: new ImageData(width, height)};
  const start = Number(rgba);
  frame.image.data.set(HEAPU8.subarray(start, start + width * height * 4));
  frame.context.putImageData(frame.image, 0, 0);
});

static EM_BOOL resize(int type, const EmscriptenUiEvent *event, void *data) {
  (void)type; (void)event; (void)data;
  double width, height;
  emscripten_get_element_css_size("#canvas", &width, &height);
  if (!active) return EM_FALSE;
  SzInputEvent input = {0};
  input.kind = SZ_INPUT_RESIZE;
  input.width = (int)width;
  input.height = (int)height;
  input.scale = emscripten_get_device_pixel_ratio();
  sz_ui_session_live_inject(active, &input);
  return EM_TRUE;
}

static EM_BOOL mouse(int type, const EmscriptenMouseEvent *event, void *data) {
  (void)data;
  if (!active) return EM_FALSE;
  SzInputEvent input = {0};
  if (type == EMSCRIPTEN_EVENT_MOUSEDOWN)
    EM_ASM({ window.getSelection().removeAllRanges(); Module.canvas.focus(); });
  input.kind = SZ_INPUT_POINTER;
  input.x = event->clientX;
  input.y = event->clientY;
  input.pointer_phase = type == EMSCRIPTEN_EVENT_MOUSEDOWN ? SZ_POINTER_DOWN :
      type == EMSCRIPTEN_EVENT_MOUSEUP ? SZ_POINTER_UP : SZ_POINTER_MOVE;
  input.pointer_button = event->button == 2 ? 3 : 1;
  if (type == EMSCRIPTEN_EVENT_MOUSEMOVE && !event->buttons) input.pointer_button = 0;
  sz_ui_session_live_inject(active, &input);
  return EM_TRUE;
}

EMSCRIPTEN_KEEPALIVE void sz_web_scroll(double x, double y, double dx, double dy) {
  if (!active) return;
  SzInputEvent input = {0};
  input.kind = SZ_INPUT_SCROLL;
  input.x = x;
  input.y = y;
  input.dx = dx;
  input.dy = dy;
  sz_ui_session_live_inject(active, &input);
}

EMSCRIPTEN_KEEPALIVE int sz_web_navigate(const char *title) {
  if (!active || !sz_view_web_navigate(sz_ui_session_root(active), title)) return 0;
  sz_ui_session_invalidate(active);
  return 1;
}

EM_JS(void, sz_web_set_title, (const char *title), {
  document.title = UTF8ToString(Number(title));
});

void sz_web_start(SzUiSession *session) {
  active = session;
  EM_ASM({ Module.ready = true; document.title = UTF8ToString(Number($0)); }, sz_ui_session_title(session));
  EM_ASM({
    Module.motionPreference = matchMedia('(prefers-reduced-motion: reduce)');
    const update = () => Module.ccall('sz_web_reduced_motion', null, ['number'], [Number(Module.motionPreference.matches)]);
    Module.motionPreference.addEventListener('change', update);
    update();
    Module.readRoute();
  });
  resize(0, NULL, NULL);
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, 0, resize);
  emscripten_set_mousedown_callback("#canvas", NULL, 0, mouse);
  emscripten_set_mousemove_callback("#canvas", NULL, 0, mouse);
  emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, 0, mouse);
  EM_ASM({ Module.resizeViewport(); });
}

EMSCRIPTEN_KEEPALIVE const char *sz_web_snapshot(void) {
  if (snapshot) sz_release(snapshot);
  snapshot = active ? sz_view_a11y_dump(sz_ui_session_root(active)) : NULL;
  return snapshot ? sz_string_cstr(snapshot) : "";
}

EMSCRIPTEN_KEEPALIVE int sz_web_motion_pending(void) {
  return active && sz_view_motion_pending(sz_ui_session_root(active));
}

EMSCRIPTEN_KEEPALIVE unsigned sz_web_pumps(void) {
  return active ? sz_ui_session_pumps(active) : 0;
}

EMSCRIPTEN_KEEPALIVE unsigned sz_web_paints(void) {
  return active ? sz_ui_session_paints(active) : 0;
}

void sz_web_stop(void) {
  active = NULL;
  if (snapshot) sz_release(snapshot);
  snapshot = NULL;
}

EMSCRIPTEN_KEEPALIVE int sz_web_tab(int backwards) {
  if (!active || !sz_view_focus_next(sz_ui_session_root(active), backwards, 0)) return 0;
  sz_ui_session_invalidate(active);
  return 1;
}

EMSCRIPTEN_KEEPALIVE int sz_web_key(const char *key, int mods, int repeat) {
  if (!active) return 0;
  SzInputEvent input = {0};
  input.kind = SZ_INPUT_KEY;
  input.key = key;
  input.key_mods = mods;
  input.key_repeat = repeat;
  int handled = sz_ui_session_live_inject(active, &input);
  SzView *focused = sz_view_focused(sz_ui_session_root(active));
  if (!strcmp(key, "Tab") && focused && sz_view_kind(focused) == SZ_VIEW_EDITOR)
    sync_editor_tab(sz_view_id(focused), sz_view_editor_value(focused),
                    sz_view_editor_sel_start(focused), sz_view_editor_sel_end(focused));
  return handled;
}

EMSCRIPTEN_KEEPALIVE int sz_web_activate(double id) {
  SzView *view = active ? sz_view_web_find(sz_ui_session_root(active), (uintptr_t)id) : NULL;
  return view ? sz_ui_session_activate_view(active, view) : 0;
}

EMSCRIPTEN_KEEPALIVE void sz_web_reduced_motion(int reduced) {
  if (!active) return;
  SzInputEvent event = {.kind = SZ_INPUT_REDUCED_MOTION, .reduced_motion = reduced};
  sz_ui_session_live_inject(active, &event);
}

EMSCRIPTEN_KEEPALIVE int sz_web_modal(void) {
  return active && sz_view_has_open_overlay(sz_ui_session_root(active));
}

EMSCRIPTEN_KEEPALIVE double sz_web_focused(void) {
  return active ? (double)sz_view_id(sz_view_focused(sz_ui_session_root(active))) : 0;
}

EMSCRIPTEN_KEEPALIVE void sz_web_focus(double id) {
  SzView *view = active ? sz_view_web_find(sz_ui_session_root(active), (uintptr_t)id) : NULL;
  if (active && (view || id == 0)) sz_ui_session_focus_view(active, view);
}

EMSCRIPTEN_KEEPALIVE void sz_web_copy_done(double id, int success) {
  SzView *view = active ? sz_view_web_find(sz_ui_session_root(active), (uintptr_t)id) : NULL;
  if (view) {
    sz_view_copy_result(view, success);
    sz_ui_session_invalidate(active);
  }
}

EMSCRIPTEN_KEEPALIVE void sz_web_edit(double id, const char *value, int start, int end, int mode) {
  SzView *view = active ? sz_view_web_find(sz_ui_session_root(active), (uintptr_t)id) : NULL;
  if (!view || (sz_view_kind(view) != SZ_VIEW_TEXT_FIELD && sz_view_kind(view) != SZ_VIEW_EDITOR)) return;
  sz_ui_session_focus_view(active, view);
  SzInputEvent input = {0};
  if (mode != 2) {
    input.kind = mode == 1 ? SZ_INPUT_COMPOSE : SZ_INPUT_TEXT;
    input.text = value;
    sz_ui_session_live_inject(active, &input);
  }
  if (mode != 1) sz_ui_session_set_sel(active, -1, start, end);
}

EMSCRIPTEN_KEEPALIVE void sz_web_fonts_changed(void) {
  if (active) sz_ui_session_invalidate(active);
}

EMSCRIPTEN_KEEPALIVE void sz_web_resize(void) { resize(0, NULL, NULL); }

EMSCRIPTEN_KEEPALIVE void sz_web_pointer(double x, double y, int phase) {
  if (!active) return;
  SzInputEvent input = {0};
  input.kind = SZ_INPUT_POINTER;
  input.x = x;
  input.y = y;
  input.pointer_button = 1;
  input.pointer_phase = phase;
  sz_ui_session_live_inject(active, &input);
}
