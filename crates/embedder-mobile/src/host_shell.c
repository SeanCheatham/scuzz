#define _POSIX_C_SOURCE 200809L

#include "scuzz_mobile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_keyboard;
static int g_frames;
static char *g_clip;

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

int sz_mobile_poll_event(SzInputEvent *out) {
  (void)out;
  return 0;
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
  free(g_clip);
  g_clip = NULL;
  if (shell_enabled())
    fprintf(stderr, "scuzz mobile: shutdown\n");
}

static char *clip_dup(const char *s) {
  size_t n;
  char *out;
  if (!s)
    return NULL;
  n = strlen(s);
  out = (char *)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, s, n + 1);
  return out;
}

int sz_mobile_clipboard_set(const char *text) {
  free(g_clip);
  g_clip = clip_dup(text ? text : "");
  return g_clip ? 1 : 0;
}

char *sz_mobile_clipboard_get(void) { return g_clip ? clip_dup(g_clip) : NULL; }
