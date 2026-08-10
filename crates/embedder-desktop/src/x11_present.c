#include "scuzz_embedder.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Display *g_dpy;
static Window g_win;
static GC g_gc;
static XImage *g_img;
static char *g_img_data;
static int g_w;
static int g_h;
static int g_ready;
static int g_user_quit;

int sz_embedder_available(void) {
  const char *d = getenv("DISPLAY");
  if (!d || !*d)
    return 0;
  Display *dpy = XOpenDisplay(NULL);
  if (!dpy)
    return 0;
  XCloseDisplay(dpy);
  return 1;
}

int sz_embedder_alive(void) {
  return !g_user_quit && sz_embedder_available();
}

static int ensure_window(const char *title, int width, int height) {
  if (g_ready && g_w == width && g_h == height)
    return 1;

  sz_embedder_shutdown();
  g_user_quit = 0;

  g_dpy = XOpenDisplay(NULL);
  if (!g_dpy) {
    fprintf(stderr, "scuzz embedder: cannot open DISPLAY\n");
    return 0;
  }

  int screen = DefaultScreen(g_dpy);
  g_win = XCreateSimpleWindow(g_dpy, RootWindow(g_dpy, screen), 0, 0,
                              (unsigned)width, (unsigned)height, 1,
                              BlackPixel(g_dpy, screen),
                              WhitePixel(g_dpy, screen));
  XStoreName(g_dpy, g_win, title ? title : "Scuzz Lang");
  XSelectInput(g_dpy, g_win, ExposureMask | StructureNotifyMask | KeyPressMask);
  XMapWindow(g_dpy, g_win);
  g_gc = DefaultGC(g_dpy, screen);

  g_w = width;
  g_h = height;
  g_img_data = (char *)malloc((size_t)width * (size_t)height * 4);
  if (!g_img_data) {
    sz_embedder_shutdown();
    return 0;
  }

  g_img = XCreateImage(g_dpy, DefaultVisual(g_dpy, screen), 24, ZPixmap, 0,
                       g_img_data, (unsigned)width, (unsigned)height, 32,
                       width * 4);
  if (!g_img) {
    sz_embedder_shutdown();
    return 0;
  }

  /* Wait for MapNotify so the first frame is visible. */
  for (;;) {
    XEvent ev;
    XNextEvent(g_dpy, &ev);
    if (ev.type == MapNotify)
      break;
  }

  g_ready = 1;
  fprintf(stderr, "scuzz embedder: X11 window %dx%d\n", width, height);
  return 1;
}

int sz_embedder_present(const char *title, int width, int height,
                        const uint8_t *rgba, size_t nbytes) {
  size_t need;
  int x, y;

  if (g_user_quit)
    return 0;
  if (!rgba || width <= 0 || height <= 0)
    return 0;
  need = (size_t)width * (size_t)height * 4;
  if (nbytes < need)
    return 0;
  if (!sz_embedder_available())
    return 0;
  if (!ensure_window(title, width, height))
    return 0;

  /* Convert RGBA → X11 BGRA-ish via XPutPixel (handles endian). */
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      const uint8_t *p = rgba + ((size_t)y * (size_t)width + (size_t)x) * 4;
      unsigned long pixel =
          ((unsigned long)p[0] << 16) | ((unsigned long)p[1] << 8) | (unsigned long)p[2];
      XPutPixel(g_img, x, y, pixel);
    }
  }
  XPutImage(g_dpy, g_win, g_gc, g_img, 0, 0, 0, 0, (unsigned)width,
            (unsigned)height);
  XFlush(g_dpy);

  /* Drain pending events without blocking forever. */
  while (XPending(g_dpy)) {
    XEvent ev;
    XNextEvent(g_dpy, &ev);
    if (ev.type == ClientMessage || ev.type == DestroyNotify) {
      g_user_quit = 1;
      sz_embedder_shutdown();
      return 1;
    }
    if (ev.type == KeyPress) {
      KeySym ks = XLookupKeysym(&ev.xkey, 0);
      if (ks == XK_q || ks == XK_Escape) {
        g_user_quit = 1;
        sz_embedder_shutdown();
        return 1;
      }
    }
  }
  return 1;
}

void sz_embedder_shutdown(void) {
  if (g_img) {
    /* XDestroyImage frees g_img_data */
    g_img->data = g_img_data;
    XDestroyImage(g_img);
    g_img = NULL;
    g_img_data = NULL;
  } else if (g_img_data) {
    free(g_img_data);
    g_img_data = NULL;
  }
  if (g_dpy) {
    if (g_win)
      XDestroyWindow(g_dpy, g_win);
    XCloseDisplay(g_dpy);
  }
  g_dpy = NULL;
  g_win = 0;
  g_ready = 0;
  g_w = g_h = 0;
}
