#include "scuzz_embedder.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_CAP 64
#define TEXT_RING 64
#define TEXT_LEN 64

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
static int g_probed;
static int g_probe_ok;

static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;
static char g_text_bufs[TEXT_RING][TEXT_LEN];
static int g_text_i;

int sz_embedder_available(void) {
  if (g_dpy)
    return 1;
  if (g_user_quit)
    return 0;
  if (g_probed)
    return g_probe_ok;
  g_probed = 1;
  g_probe_ok = 0;
  {
    const char *d = getenv("DISPLAY");
    Display *dpy;
    if (!d || !*d)
      return 0;
    dpy = XOpenDisplay(NULL);
    if (!dpy)
      return 0;
    XCloseDisplay(dpy);
    g_probe_ok = 1;
    return 1;
  }
}

double sz_embedder_display_scale(void) {
  /* X11 path stays 1× (CI / typical Linux). HiDPI X11 later. */
  return 1.0;
}

int sz_embedder_alive(void) {
  if (g_user_quit)
    return 0;
  if (g_ready)
    return 1;
  return sz_embedder_available();
}

static int q_full(void) {
  return ((g_q_tail + 1) % EVENT_CAP) == g_q_head;
}

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

static void enqueue_pointer(SzPointerPhase phase, float x, float y) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = phase;
  ev.x = x;
  ev.y = y;
  q_push(&ev);
}

static void enqueue_scroll(float x, float y, float dy) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_SCROLL;
  ev.x = x;
  ev.y = y;
  ev.dy = dy;
  q_push(&ev);
}

static void enqueue_text_edit(const char *text) {
  SzInputEvent ev;
  /* Drop if the queue is full, or if the next text slot is still queued. */
  if (q_full() || text_slot_queued(g_text_bufs[g_text_i]))
    return;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = stash_text(text ? text : "");
  q_push(&ev);
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

/* Put an 8-bit channel into a TrueColor mask. */
static uint32_t place_chan(uint8_t v, unsigned long mask) {
  unsigned long m;
  unsigned shift;
  unsigned bits;
  if (!mask)
    return 0;
  m = mask;
  shift = 0;
  while ((m & 1UL) == 0UL) {
    m >>= 1;
    shift++;
  }
  bits = 0;
  while (m & 1UL) {
    m >>= 1;
    bits++;
  }
  if (bits == 0)
    return 0;
  if (bits >= 8)
    return ((uint32_t)v << (shift + bits - 8)) & (uint32_t)mask;
  return (((uint32_t)v >> (8 - bits)) << shift) & (uint32_t)mask;
}

static int ensure_window(const char *title, int width, int height) {
  size_t need;
  Visual *vis;
  int depth;
  int screen;

  if (g_ready && g_w == width && g_h == height)
    return 1;

  sz_embedder_shutdown();
  g_user_quit = 0;

  if (!frame_bytes(width, height, &need))
    return 0;

  g_dpy = XOpenDisplay(NULL);
  if (!g_dpy) {
    fprintf(stderr, "scuzz embedder: cannot open DISPLAY\n");
    return 0;
  }

  screen = DefaultScreen(g_dpy);
  vis = DefaultVisual(g_dpy, screen);
  depth = DefaultDepth(g_dpy, screen);
  g_win = XCreateSimpleWindow(g_dpy, RootWindow(g_dpy, screen), 0, 0,
                              (unsigned)width, (unsigned)height, 1,
                              BlackPixel(g_dpy, screen),
                              WhitePixel(g_dpy, screen));
  XStoreName(g_dpy, g_win, title ? title : "Scuzz Lang");
  g_wm_delete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(g_dpy, g_win, &g_wm_delete, 1);
  {
    /* Lock size so a WM resize does not desync the blit. */
    XSizeHints hints;
    memset(&hints, 0, sizeof hints);
    hints.flags = PMinSize | PMaxSize;
    hints.min_width = hints.max_width = width;
    hints.min_height = hints.max_height = height;
    XSetWMNormalHints(g_dpy, g_win, &hints);
  }
  XSelectInput(g_dpy, g_win,
               ExposureMask | StructureNotifyMask | KeyPressMask |
                   ButtonPressMask | ButtonReleaseMask | Button1MotionMask);
  XMapWindow(g_dpy, g_win);
  g_gc = DefaultGC(g_dpy, screen);

  g_w = width;
  g_h = height;
  g_img_data = (char *)malloc(need);
  if (!g_img_data) {
    sz_embedder_shutdown();
    return 0;
  }

  g_img = XCreateImage(g_dpy, vis, (unsigned)depth, ZPixmap, 0, g_img_data,
                       (unsigned)width, (unsigned)height, 32, width * 4);
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

int sz_embedder_present(const char *title, int point_w, int point_h,
                        int pixel_w, int pixel_h, const uint8_t *rgba,
                        size_t nbytes) {
  size_t need;
  int width = pixel_w;
  int height = pixel_h;
  Visual *vis;
  uint32_t *dst;
  size_t i;
  size_t n;
  unsigned long rm;
  unsigned long gm;
  unsigned long bm;

  (void)point_w;
  (void)point_h;

  if (g_user_quit)
    return 0;
  if (!rgba)
    return 0;
  if (!frame_bytes(width, height, &need))
    return 0;
  if (nbytes < need)
    return 0;
  /* Do not probe DISPLAY after the connection is open. */
  if (!(g_dpy || ensure_window(title, width, height)))
    return 0;

  vis = DefaultVisual(g_dpy, DefaultScreen(g_dpy));
  dst = (uint32_t *)g_img_data;
  n = (size_t)width * (size_t)height;
  rm = vis->red_mask;
  gm = vis->green_mask;
  bm = vis->blue_mask;
  /* Fast path: 0xFF0000 / 0x00FF00 / 0x0000FF (typical TrueColor). */
  if (rm == 0xFF0000UL && gm == 0xFF00UL && bm == 0xFFUL) {
    for (i = 0; i < n; i++) {
      const uint8_t *p = rgba + i * 4;
      dst[i] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    }
  } else {
    for (i = 0; i < n; i++) {
      const uint8_t *p = rgba + i * 4;
      dst[i] = place_chan(p[0], rm) | place_chan(p[1], gm) | place_chan(p[2], bm);
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
    if (ev.type == ButtonPress && ev.xbutton.button == 1)
      enqueue_pointer(SZ_POINTER_DOWN, (float)ev.xbutton.x, (float)ev.xbutton.y);
    if (ev.type == MotionNotify && (ev.xmotion.state & Button1Mask))
      enqueue_pointer(SZ_POINTER_MOVE, (float)ev.xmotion.x, (float)ev.xmotion.y);
    if (ev.type == ButtonRelease && ev.xbutton.button == 1)
      enqueue_pointer(SZ_POINTER_UP, (float)ev.xbutton.x, (float)ev.xbutton.y);
    /* Wheel: 4 = up, 5 = down. Positive dy = content up (matches SZ_INPUT_SCROLL). */
    if (ev.type == ButtonPress && ev.xbutton.button == 4)
      enqueue_scroll((float)ev.xbutton.x, (float)ev.xbutton.y, 40.f);
    if (ev.type == ButtonPress && ev.xbutton.button == 5)
      enqueue_scroll((float)ev.xbutton.x, (float)ev.xbutton.y, -40.f);
    if (ev.type == KeyPress) {
      KeySym ks;
      char buf[8];
      int nkey;
      ks = XLookupKeysym(&ev.xkey, 0);
      if (ks == XK_q || ks == XK_Q || ks == XK_Escape) {
        g_user_quit = 1;
        sz_embedder_shutdown();
        return 1;
      }
      if (ks == XK_BackSpace) {
        enqueue_text_edit("");
        continue;
      }
      nkey = XLookupString(&ev.xkey, buf, (int)sizeof(buf) - 1, &ks, NULL);
      if (nkey == 1 && buf[0] >= 32 && buf[0] < 127) {
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
