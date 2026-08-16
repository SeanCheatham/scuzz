/* Android sz_mobile_* shell. Strong defs override the weak runtime stubs.
 * JNI_OnLoad in scuzz_jni.c starts scuzz_app_main on a worker. Present
 * keeps the last RGBA frame so MainActivity can blit it to a SurfaceView. */

#include "scuzz_mobile.h"

#include <android/log.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_CAP 64

static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;
static pthread_mutex_t g_q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_frame_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_alive;
static int g_frames;
static int g_w;
static int g_h;
static uint8_t *g_rgba;
static size_t g_rgba_cap;

int sz_mobile_available(void) { return 1; }

int sz_mobile_alive(void) { return g_alive; }

void scuzz_android_set_alive(int alive) { g_alive = alive ? 1 : 0; }

void sz_mobile_shutdown(void) { scuzz_android_set_alive(0); }

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
  pthread_mutex_unlock(&g_q_lock);
  return 1;
}

int sz_mobile_present(const char *title, int width, int height,
                      const uint8_t *rgba, size_t nbytes) {
  size_t need;
  (void)title;
  if (!rgba || width <= 0 || height <= 0)
    return 0;
  need = (size_t)width * (size_t)height * 4;
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
  g_w = width;
  g_h = height;
  g_frames++;
  pthread_mutex_unlock(&g_frame_lock);
  __android_log_print(ANDROID_LOG_INFO, "scuzz",
                      "scuzz android: present %dx%d frame=%d", width, height,
                      g_frames);
  return 1;
}

void sz_mobile_set_keyboard(int visible) { (void)visible; }

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
