#define _POSIX_C_SOURCE 200809L

#include "scuzz_mobile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_CAP 64

static int g_ready;
static int g_keyboard;
static int g_frames;
static int g_w;
static int g_h;
static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;
static char *g_last_title;

static int shell_enabled(void) {
  const char *e = getenv("SCUZZ_MOBILE_SHELL");
  return e && e[0] && strcmp(e, "0") != 0;
}

int sz_mobile_available(void) { return shell_enabled(); }

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

void sz_mobile_set_keyboard(int visible) {
  g_keyboard = visible ? 1 : 0;
  if (shell_enabled()) {
    fprintf(stderr, "scuzz mobile: soft keyboard %s\n",
            g_keyboard ? "show" : "hide");
  }
}

int sz_mobile_present(const char *title, int width, int height,
                      const uint8_t *rgba, size_t nbytes) {
  size_t need;
  if (!shell_enabled())
    return 0;
  if (!rgba || width <= 0 || height <= 0)
    return 0;
  need = (size_t)width * (size_t)height * 4;
  if (nbytes < need)
    return 0;
  g_ready = 1;
  g_w = width;
  g_h = height;
  g_frames++;
  if (title) {
    size_t n = strlen(title);
    free(g_last_title);
    g_last_title = (char *)malloc(n + 1);
    if (g_last_title)
      memcpy(g_last_title, title, n + 1);
  }
  fprintf(stderr, "scuzz mobile: present %dx%d frame=%d keyboard=%d title=%s\n",
          width, height, g_frames, g_keyboard,
          g_last_title ? g_last_title : "Scuzz Lang");
  (void)g_ready;
  (void)g_w;
  (void)g_h;
  return 1;
}

void sz_mobile_shutdown(void) {
  free(g_last_title);
  g_last_title = NULL;
  g_ready = 0;
  g_keyboard = 0;
  g_frames = 0;
  g_w = g_h = 0;
  g_q_head = g_q_tail = 0;
  if (shell_enabled())
    fprintf(stderr, "scuzz mobile: shutdown\n");
}
