/* Android sz_mobile_* shell. Strong defs override the weak runtime stubs.
 * JNI_OnLoad in scuzz_jni.c loads the library. MainActivity calls
 * nativeStart, then blits frames and pushes pointer / text events. */

#include "scuzz_mobile.h"

#include <android/log.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_CAP 64
#define TEXT_RING 64
#define TEXT_LEN 64

static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;
static pthread_mutex_t g_q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_frame_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_alive;
static int g_frames;
static int g_w;
static int g_h;
static int g_point_w;
static int g_point_h;
static _Atomic int g_keyboard;
static uint8_t *g_rgba;
static size_t g_rgba_cap;
static char g_text_bufs[TEXT_RING][TEXT_LEN];
static int g_text_i;
static char g_poll_text[TEXT_LEN];

int sz_mobile_available(void) { return 1; }

int sz_mobile_alive(void) { return atomic_load(&g_alive); }

void scuzz_android_set_alive(int alive) { atomic_store(&g_alive, alive ? 1 : 0); }

void sz_mobile_shutdown(void) {
  pthread_mutex_lock(&g_frame_lock);
  free(g_rgba);
  g_rgba = NULL;
  g_rgba_cap = 0;
  g_w = g_h = 0;
  g_point_w = g_point_h = 0;
  pthread_mutex_unlock(&g_frame_lock);
  scuzz_android_set_alive(0);
}

static int q_full(void) { return ((g_q_tail + 1) % EVENT_CAP) == g_q_head; }

static int text_slot_queued(const char *slot) {
  int i;
  for (i = g_q_head; i != g_q_tail; i = (i + 1) % EVENT_CAP) {
    if (g_queue[i].kind == SZ_INPUT_TEXT_EDIT && g_queue[i].text == slot)
      return 1;
  }
  return 0;
}

static const char *stash_text(const char *s) {
  size_t n;
  char *dst;
  if (!s)
    s = "";
  n = strlen(s);
  if (n >= TEXT_LEN)
    n = TEXT_LEN - 1;
  dst = g_text_bufs[g_text_i];
  g_text_i = (g_text_i + 1) % TEXT_RING;
  memcpy(dst, s, n);
  dst[n] = '\0';
  return dst;
}

static int frame_bytes(int width, int height, size_t *out) {
  size_t w;
  size_t h;
  if (width <= 0 || height <= 0 || !out)
    return 0;
  w = (size_t)width;
  h = (size_t)height;
  if (w > SIZE_MAX / 4)
    return 0;
  if (h > SIZE_MAX / (w * 4))
    return 0;
  *out = w * h * 4;
  return 1;
}

int sz_mobile_push_event(const SzInputEvent *event) {
  int next;
  if (!event)
    return 0;
  pthread_mutex_lock(&g_q_lock);
  next = (g_q_tail + 1) % EVENT_CAP;
  if (next == g_q_head) {
    pthread_mutex_unlock(&g_q_lock);
    return 0;
  }
  g_queue[g_q_tail] = *event;
  g_q_tail = next;
  pthread_mutex_unlock(&g_q_lock);
  return 1;
}

int sz_mobile_poll_event(SzInputEvent *out) {
  if (!out)
    return 0;
  pthread_mutex_lock(&g_q_lock);
  if (g_q_head == g_q_tail) {
    pthread_mutex_unlock(&g_q_lock);
    return 0;
  }
  *out = g_queue[g_q_head];
  g_q_head = (g_q_head + 1) % EVENT_CAP;
  if (out->kind == SZ_INPUT_TEXT_EDIT) {
    const char *src = out->text ? out->text : "";
    size_t n = strlen(src);
    if (n >= TEXT_LEN)
      n = TEXT_LEN - 1;
    memcpy(g_poll_text, src, n);
    g_poll_text[n] = '\0';
    out->text = g_poll_text;
  }
  pthread_mutex_unlock(&g_q_lock);
  return 1;
}

int scuzz_android_push_pointer(float x, float y, int phase) {
  SzInputEvent ev;
  if (phase != SZ_POINTER_DOWN && phase != SZ_POINTER_MOVE &&
      phase != SZ_POINTER_UP)
    return 0;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_POINTER;
  ev.x = x;
  ev.y = y;
  ev.pointer_phase = (SzPointerPhase)phase;
  __android_log_print(ANDROID_LOG_INFO, "scuzz",
                      "scuzz android: pointer %.1f %.1f phase=%d", (double)x,
                      (double)y, phase);
  return sz_mobile_push_event(&ev);
}

int scuzz_android_push_text_edit(const char *text) {
  SzInputEvent ev;
  /* Drop if the queue is full, or if the next text slot is still queued. */
  pthread_mutex_lock(&g_q_lock);
  if (q_full() || text_slot_queued(g_text_bufs[g_text_i])) {
    pthread_mutex_unlock(&g_q_lock);
    return 0;
  }
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = stash_text(text ? text : "");
  g_queue[g_q_tail] = ev;
  g_q_tail = (g_q_tail + 1) % EVENT_CAP;
  pthread_mutex_unlock(&g_q_lock);
  __android_log_print(ANDROID_LOG_INFO, "scuzz", "scuzz android: text_edit %s",
                      text && text[0] ? text : "(backspace)");
  return 1;
}

int scuzz_android_push_resize(int w, int h) {
  SzInputEvent ev;
  if (w <= 0 || h <= 0)
    return 0;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_RESIZE;
  ev.width = w;
  ev.height = h;
  __android_log_print(ANDROID_LOG_INFO, "scuzz", "scuzz android: resize %dx%d",
                      w, h);
  return sz_mobile_push_event(&ev);
}

int scuzz_android_push_lifecycle(int phase) {
  SzInputEvent ev;
  if (phase != SZ_LIFECYCLE_RESUME && phase != SZ_LIFECYCLE_PAUSE &&
      phase != SZ_LIFECYCLE_STOP)
    return 0;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_LIFECYCLE;
  ev.lifecycle = (SzLifecyclePhase)phase;
  return sz_mobile_push_event(&ev);
}

int sz_mobile_present(const char *title, int point_w, int point_h, int pixel_w,
                      int pixel_h, const uint8_t *rgba, size_t nbytes) {
  size_t need;
  (void)title;
  if (!rgba || point_w <= 0 || point_h <= 0)
    return 0;
  if (!frame_bytes(pixel_w, pixel_h, &need))
    return 0;
  if (nbytes < need)
    return 0;
  pthread_mutex_lock(&g_frame_lock);
  if (need > g_rgba_cap) {
    uint8_t *next = (uint8_t *)realloc(g_rgba, need);
    if (!next) {
      pthread_mutex_unlock(&g_frame_lock);
      return 0;
    }
    g_rgba = next;
    g_rgba_cap = need;
  }
  memcpy(g_rgba, rgba, need);
  g_w = pixel_w;
  g_h = pixel_h;
  g_point_w = point_w;
  g_point_h = point_h;
  g_frames++;
  pthread_mutex_unlock(&g_frame_lock);
  if (g_frames <= 3 || (g_frames % 120) == 0)
    __android_log_print(ANDROID_LOG_INFO, "scuzz",
                        "scuzz android: present %dx%d px=%dx%d frame=%d",
                        point_w, point_h, pixel_w, pixel_h, g_frames);
  return 1;
}

void sz_mobile_set_keyboard(int visible) {
  atomic_store(&g_keyboard, visible ? 1 : 0);
}

int scuzz_android_keyboard_visible(void) { return atomic_load(&g_keyboard); }

int scuzz_android_frame_width(void) {
  int w;
  pthread_mutex_lock(&g_frame_lock);
  w = g_w;
  pthread_mutex_unlock(&g_frame_lock);
  return w;
}

int scuzz_android_frame_height(void) {
  int h;
  pthread_mutex_lock(&g_frame_lock);
  h = g_h;
  pthread_mutex_unlock(&g_frame_lock);
  return h;
}

int scuzz_android_point_width(void) {
  int w;
  pthread_mutex_lock(&g_frame_lock);
  w = g_point_w;
  pthread_mutex_unlock(&g_frame_lock);
  return w;
}

int scuzz_android_point_height(void) {
  int h;
  pthread_mutex_lock(&g_frame_lock);
  h = g_point_h;
  pthread_mutex_unlock(&g_frame_lock);
  return h;
}

int scuzz_android_frame_count(void) {
  int n;
  pthread_mutex_lock(&g_frame_lock);
  n = g_frames;
  pthread_mutex_unlock(&g_frame_lock);
  return n;
}

int scuzz_android_copy_argb(int32_t *dst, int cap) {
  int n;
  int i;
  int frames;
  const uint8_t *src;
  if (!dst || cap <= 0)
    return 0;
  pthread_mutex_lock(&g_frame_lock);
  n = g_w * g_h;
  frames = g_frames;
  src = g_rgba;
  if (!src || n <= 0 || cap < n) {
    pthread_mutex_unlock(&g_frame_lock);
    return 0;
  }
  for (i = 0; i < n; i++) {
    uint8_t r = src[i * 4];
    uint8_t g = src[i * 4 + 1];
    uint8_t b = src[i * 4 + 2];
    uint8_t a = src[i * 4 + 3];
    dst[i] = ((int32_t)(uint32_t)a << 24) | ((int32_t)(uint32_t)r << 16) |
             ((int32_t)(uint32_t)g << 8) | (int32_t)(uint32_t)b;
  }
  pthread_mutex_unlock(&g_frame_lock);
  return frames;
}
