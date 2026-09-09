#include "web.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <string.h>

static SzUiSession *active;
static SzString *snapshot;

EM_JS(void, sz_web_frame_begin, (float scale), {
  Module.webFrame = {scale, texts: [], sections: []};
});
EM_JS(void, sz_web_text_begin, (const char *text), {
  Module.webFrame.texts.push({text: UTF8ToString(Number(text)), lines: []});
});
EM_JS(void, sz_web_text_line, (const char *source, int start, int end,
    const char *text, float x, float y, float width, float font, float line_h,
    int clipped, float cx, float cy, float cw, float ch), {
  const f = Module.webFrame;
  const base = Number(source);
  f.texts[f.texts.length - 1].lines.push({
    start: start ? UTF8ToString(base, start).length : 0,
    end: end ? UTF8ToString(base, end).length : 0,
    text: UTF8ToString(Number(text)), x, y, width, font, line_h,
    clip: clipped ? [cx, cy, cw, ch] : null
  });
});
EM_JS(int, sz_web_book_begin, (), {
  if (Module.webFrame.bookSeen) return 0;
  Module.webFrame.bookSeen = true;
  return 1;
});
EM_JS(void, sz_web_section, (const char *title, int selected), {
  Module.webFrame.sections.push({title: UTF8ToString(Number(title)), selected});
});
EM_JS(void, sz_web_frame_end, (), { Module.presentText(Module.webFrame); });


EM_JS(void, sz_web_present, (int width, int height, const uint8_t *rgba), {
  const canvas = Module.canvas;
  if (canvas.width !== width) canvas.width = width;
  if (canvas.height !== height) canvas.height = height;
  const start = Number(rgba);
  const pixels = new Uint8ClampedArray(HEAPU8.subarray(start, start + width * height * 4));
  canvas.getContext('2d').putImageData(new ImageData(pixels, width, height), 0, 0);
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

static EM_BOOL wheel(int type, const EmscriptenWheelEvent *event, void *data) {
  (void)type; (void)data;
  if (!active) return EM_FALSE;
  SzInputEvent input = {0};
  input.kind = SZ_INPUT_SCROLL;
  input.x = event->mouse.clientX;
  input.y = event->mouse.clientY;
  input.dy = event->deltaY * (event->deltaMode == 1 ? 20 : 1);
  sz_ui_session_live_inject(active, &input);
  return EM_TRUE;
}

static EM_BOOL key(int type, const EmscriptenKeyboardEvent *event, void *data) {
  (void)type; (void)data;
  if (!active) return EM_FALSE;
  if (EM_ASM_INT({ return !window.getSelection().isCollapsed; })) return EM_FALSE;
  SzInputEvent input = {0};
  input.kind = SZ_INPUT_KEY;
  input.key = event->key;
  input.key_mods = (event->shiftKey ? SZ_KEY_SHIFT : 0) |
      (event->ctrlKey ? SZ_KEY_CTRL : 0) | (event->metaKey ? SZ_KEY_CMD : 0) |
      (event->altKey ? SZ_KEY_ALT : 0);
  input.key_repeat = event->repeat;
  if (!event->ctrlKey && !event->metaKey && strlen(event->key) == 1)
    input.text = event->key;
  sz_ui_session_live_inject(active, &input);
  return EM_TRUE;
}

static EM_BOOL touch(int type, const EmscriptenTouchEvent *event, void *data) {
  (void)data;
  for (int i = 0; i < event->numTouches; i++) {
    if (!event->touches[i].isChanged) continue;
    if (!active) return EM_FALSE;
    SzInputEvent input = {0};
    input.kind = SZ_INPUT_POINTER;
    input.x = event->touches[i].clientX;
    input.y = event->touches[i].clientY;
    input.pointer_button = 1;
    input.pointer_phase = type == EMSCRIPTEN_EVENT_TOUCHSTART ? SZ_POINTER_DOWN :
        type == EMSCRIPTEN_EVENT_TOUCHMOVE ? SZ_POINTER_MOVE : SZ_POINTER_UP;
    sz_ui_session_live_inject(active, &input);
    break;
  }
  return EM_TRUE;
}

EMSCRIPTEN_KEEPALIVE void sz_web_scroll(double x, double y, double dy) {
  if (!active) return;
  SzInputEvent input = {0};
  input.kind = SZ_INPUT_SCROLL;
  input.x = x;
  input.y = y;
  input.dy = dy;
  sz_ui_session_live_inject(active, &input);
}

EMSCRIPTEN_KEEPALIVE int sz_web_navigate(const char *title) {
  if (!active || !sz_view_web_navigate(sz_ui_session_root(active), title)) return 0;
  resize(0, NULL, NULL);
  return 1;
}

void sz_web_start(SzUiSession *session) {
  active = session;
  EM_ASM({ document.title = UTF8ToString(Number($0)); }, sz_ui_session_title(session));
  EM_ASM({ Module.readRoute(); });
  resize(0, NULL, NULL);
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, 0, resize);
  emscripten_set_mousedown_callback("#canvas", NULL, 0, mouse);
  emscripten_set_mousemove_callback("#canvas", NULL, 0, mouse);
  emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, 0, mouse);
  emscripten_set_wheel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, 0, wheel);
  emscripten_set_keydown_callback("#canvas", NULL, 0, key);
  emscripten_set_touchstart_callback("#canvas", NULL, 0, touch);
  emscripten_set_touchmove_callback("#canvas", NULL, 0, touch);
  emscripten_set_touchend_callback("#canvas", NULL, 0, touch);
  emscripten_set_touchcancel_callback("#canvas", NULL, 0, touch);
  EM_ASM({ Module.canvas.focus(); });
}

EMSCRIPTEN_KEEPALIVE const char *sz_web_snapshot(void) {
  if (snapshot) sz_release(snapshot);
  snapshot = active ? sz_view_a11y_dump(sz_ui_session_root(active)) : NULL;
  return snapshot ? sz_string_cstr(snapshot) : "";
}

void sz_web_stop(void) {
  active = NULL;
  if (snapshot) sz_release(snapshot);
  snapshot = NULL;
}
