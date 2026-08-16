/* Android sz_mobile_* shell. Strong defs override the weak runtime stubs.
 * JNI_OnLoad in scuzz_jni.c starts scuzz_app_main on a worker. Present
 * keeps the last frame so a SurfaceView can copy it later. */

#include "scuzz_mobile.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_CAP 64

static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;
static pthread_mutex_t g_q_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_alive;
static int g_frames;

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
  (void)title;
  if (!rgba || width <= 0 || height <= 0)
    return 0;
  if (nbytes < (size_t)width * (size_t)height * 4)
    return 0;
  g_frames++;
  return 1;
}

void sz_mobile_set_keyboard(int visible) { (void)visible; }
