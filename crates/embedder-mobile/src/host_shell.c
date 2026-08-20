#define _POSIX_C_SOURCE 200809L

#include "scuzz_mobile.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_CAP 64

static int g_keyboard;
static int g_frames;
static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;

static int shell_enabled(void) {
  const char *e = getenv("SCUZZ_MOBILE_SHELL");
  return e && e[0] && strcmp(e, "0") != 0;
}

int sz_mobile_available(void) { return shell_enabled(); }

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

static int q_push(const SzInputEvent *ev) {
  int next;
  if (!ev)
    return 0;
  next = (g_q_tail + 1) % EVENT_CAP;
  if (next == g_q_head)
    return 0; /* full */
  /* Text pointers must outlive poll (use literals / stable buffers). */
  g_queue[g_q_tail] = *ev;
  g_q_tail = next;
  return 1;
}

int sz_mobile_push_event(const SzInputEvent *event) { return q_push(event); }

int sz_mobile_poll_event(SzInputEvent *out) {
  if (!out || g_q_head == g_q_tail)
    return 0;
  *out = g_queue[g_q_head];
  g_q_head = (g_q_head + 1) % EVENT_CAP;
  return 1;
}

/* Host shell stays a single-frame smoke; Ui.run exits the live Mobile loop. */
int sz_mobile_alive(void) { return 0; }

void sz_mobile_set_keyboard(int visible) {
  g_keyboard = visible ? 1 : 0;
  if (shell_enabled()) {
    fprintf(stderr, "scuzz mobile: soft keyboard %s\n",
            g_keyboard ? "show" : "hide");
  }
}

int sz_mobile_present(const char *title, int point_w, int point_h, int pixel_w,
                      int pixel_h, const uint8_t *rgba, size_t nbytes) {
  size_t need;
  if (!shell_enabled())
    return 0;
  if (!rgba || point_w <= 0 || point_h <= 0)
    return 0;
  if (!frame_bytes(pixel_w, pixel_h, &need))
    return 0;
  if (nbytes < need)
    return 0;
  g_frames++;
  fprintf(stderr,
          "scuzz mobile: present %dx%d px=%dx%d frame=%d keyboard=%d title=%s\n",
          point_w, point_h, pixel_w, pixel_h, g_frames, g_keyboard,
          title && title[0] ? title : "Scuzz Lang");
  return 1;
}

void sz_mobile_shutdown(void) {
  g_keyboard = 0;
  g_frames = 0;
  g_q_head = g_q_tail = 0;
  if (shell_enabled())
    fprintf(stderr, "scuzz mobile: shutdown\n");
}
