#include "scuzz_embedder.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_CAP 64
#define TEXT_RING 32
#define TEXT_LEN 8

static Display *g_dpy;
static Window g_win;
static GC g_gc;
static XImage *g_img;
static char *g_img_data;
static Atom g_wm_delete;
static int g_w;
static int g_h;
static int g_ready;
static int g_user_quit;

static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;
static char g_text_bufs[TEXT_RING][TEXT_LEN];
static int g_text_i;

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
  if (g_user_quit)
    return 0;
  if (g_ready)
    return 1;
  return sz_embedder_available();
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

static int q_push(const SzInputEvent *ev) {
  int next;
  if (!ev)
    return 0;
  next = (g_q_tail + 1) % EVENT_CAP;
  if (next == g_q_head)
    return 0; /* full */
  g_queue[g_q_tail] = *ev;
  g_q_tail = next;
  return 1;
}

int sz_embedder_poll_event(SzInputEvent *out) {
  if (!out || g_q_head == g_q_tail)
    return 0;
  *out = g_queue[g_q_head];
  g_q_head = (g_q_head + 1) % EVENT_CAP;
  return 1;
}

static void enqueue_tap(float x, float y) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TAP;
  ev.x = x;
  ev.y = y;
  q_push(&ev);
}

static void enqueue_text_edit(const char *text) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = stash_text(text ? text : "");
  q_push(&ev);
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
  g_wm_delete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(g_dpy, g_win, &g_wm_delete, 1);
  XSelectInput(g_dpy, g_win,
               ExposureMask | StructureNotifyMask | KeyPressMask |
                   ButtonPressMask);
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

  /* Drain pending events: quit/close handled here; input only enqueued. */
  while (XPending(g_dpy)) {
    XEvent ev;
    XNextEvent(g_dpy, &ev);
    if (ev.type == DestroyNotify) {
      g_user_quit = 1;
      sz_embedder_shutdown();
      return 1;
    }
    if (ev.type == ClientMessage &&
        (Atom)ev.xclient.data.l[0] == g_wm_delete) {
      g_user_quit = 1;
      sz_embedder_shutdown();
      return 1;
    }
    if (ev.type == ButtonPress) {
      enqueue_tap((float)ev.xbutton.x, (float)ev.xbutton.y);
      continue;
    }
    if (ev.type == KeyPress) {
      KeySym ks;
      char buf[8];
      int n;
      ks = XLookupKeysym(&ev.xkey, 0);
      if (ks == XK_q || ks == XK_Escape) {
        g_user_quit = 1;
        sz_embedder_shutdown();
        return 1;
      }
      if (ks == XK_BackSpace) {
        enqueue_text_edit("");
        continue;
      }
      n = XLookupString(&ev.xkey, buf, (int)sizeof(buf) - 1, &ks, NULL);
      if (n == 1 && buf[0] >= 32 && buf[0] < 127) {
        buf[1] = '\0';
        enqueue_text_edit(buf);
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
  g_q_head = g_q_tail = 0;
}
