#define _POSIX_C_SOURCE 200809L

#include "scuzz_embedder.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define EVENT_CAP 64
#define TEXT_RING 64
#define TEXT_LEN 128
#define KEY_NAME_LEN 32

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
static char g_key_bufs[TEXT_RING][KEY_NAME_LEN];
static char g_text_bufs[TEXT_RING][TEXT_LEN];
static int g_text_i;
static int g_key_repeat_pending;
static unsigned int g_held_keycode;
static int g_key_held;
static char *g_clip;
static int g_clip_own;
static Atom g_atom_clipboard;
static Atom g_atom_utf8;
static Atom g_atom_targets;
static Atom g_atom_prop;

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

static void x11_clip_atoms(void) {
  if (!g_dpy || g_atom_clipboard)
    return;
  g_atom_clipboard = XInternAtom(g_dpy, "CLIPBOARD", False);
  g_atom_utf8 = XInternAtom(g_dpy, "UTF8_STRING", False);
  g_atom_targets = XInternAtom(g_dpy, "TARGETS", False);
  g_atom_prop = XInternAtom(g_dpy, "SCUZZ_CLIPBOARD", False);
}

static void x11_serve_selection(XSelectionRequestEvent *req) {
  XEvent notify;
  int ok = 0;
  if (!req || !g_dpy)
    return;
  x11_clip_atoms();
  if (req->target == g_atom_targets) {
    Atom targets[3];
    targets[0] = g_atom_targets;
    targets[1] = g_atom_utf8;
    targets[2] = XA_STRING;
    XChangeProperty(g_dpy, req->requestor, req->property, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)targets, 3);
    ok = 1;
  } else if ((req->target == g_atom_utf8 || req->target == XA_STRING) &&
             g_clip) {
    XChangeProperty(g_dpy, req->requestor, req->property, req->target, 8,
                    PropModeReplace, (unsigned char *)g_clip,
                    (int)strlen(g_clip));
    ok = 1;
  }
  memset(&notify, 0, sizeof notify);
  notify.xselection.type = SelectionNotify;
  notify.xselection.display = req->display;
  notify.xselection.requestor = req->requestor;
  notify.xselection.selection = req->selection;
  notify.xselection.target = req->target;
  notify.xselection.property = ok ? req->property : None;
  notify.xselection.time = req->time;
  XSendEvent(g_dpy, req->requestor, True, NoEventMask, &notify);
}

int sz_embedder_clipboard_set(const char *text) {
  free(g_clip);
  g_clip = clip_dup(text ? text : "");
  if (!g_clip)
    return 0;
  if (g_dpy && g_win) {
    x11_clip_atoms();
    XStoreBytes(g_dpy, g_clip, (int)strlen(g_clip));
    XSetSelectionOwner(g_dpy, g_atom_clipboard, g_win, CurrentTime);
    g_clip_own = XGetSelectionOwner(g_dpy, g_atom_clipboard) == g_win;
    XFlush(g_dpy);
  }
  return 1;
}

static char *x11_read_prop(Atom prop) {
  Atom type = None;
  int fmt = 0;
  unsigned long nitems = 0;
  unsigned long after = 0;
  unsigned char *data = NULL;
  char *out;
  if (!g_dpy || !g_win || prop == None)
    return NULL;
  if (XGetWindowProperty(g_dpy, g_win, prop, 0, 65536, True, AnyPropertyType,
                         &type, &fmt, &nitems, &after, &data) != Success ||
      !data)
    return NULL;
  out = (char *)malloc(nitems + 1);
  if (out) {
    memcpy(out, data, nitems);
    out[nitems] = '\0';
  }
  XFree(data);
  return out;
}

char *sz_embedder_clipboard_get(void) {
  int i;
  if (g_clip_own && g_clip)
    return clip_dup(g_clip);
  if (!g_dpy || !g_win)
    return g_clip ? clip_dup(g_clip) : NULL;
  x11_clip_atoms();
  XConvertSelection(g_dpy, g_atom_clipboard, g_atom_utf8, g_atom_prop, g_win,
                    CurrentTime);
  XFlush(g_dpy);
  for (i = 0; i < 50; i++) {
    while (XPending(g_dpy)) {
      XEvent ev;
      XNextEvent(g_dpy, &ev);
      if (ev.type == SelectionRequest)
        x11_serve_selection(&ev.xselectionrequest);
      else if (ev.type == SelectionClear)
        g_clip_own = 0;
      else if (ev.type == SelectionNotify) {
        char *got = NULL;
        if (ev.xselection.property != None)
          got = x11_read_prop(ev.xselection.property);
        if (got)
          return got;
        return g_clip ? clip_dup(g_clip) : NULL;
      }
    }
    {
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 2000000L; /* 2 ms */
      nanosleep(&ts, NULL);
    }
  }
  return g_clip ? clip_dup(g_clip) : NULL;
}

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
    if (g_queue[i].kind == SZ_INPUT_KEY &&
        (g_queue[i].key == slot || g_queue[i].text == slot))
      return 1;
  }
  return 0;
}

static void stash_key_text(const char *name, const char *text, const char **out_key,
                           const char **out_text) {
  size_t nk;
  size_t nt;
  char *kdst;
  char *tdst;
  if (!name)
    name = "";
  if (!text)
    text = "";
  nk = strlen(name);
  nt = strlen(text);
  if (nk >= KEY_NAME_LEN)
    nk = KEY_NAME_LEN - 1;
  if (nt >= TEXT_LEN)
    nt = TEXT_LEN - 1;
  kdst = g_key_bufs[g_text_i];
  tdst = g_text_bufs[g_text_i];
  g_text_i = (g_text_i + 1) % TEXT_RING;
  memcpy(kdst, name, nk);
  kdst[nk] = '\0';
  memcpy(tdst, text, nt);
  tdst[nt] = '\0';
  *out_key = kdst;
  *out_text = tdst;
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

static void enqueue_pointer(SzPointerPhase phase, float x, float y, int button) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = phase;
  ev.pointer_button = button;
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

static void enqueue_key(const char *name, const char *text, int mods, int repeat) {
  SzInputEvent ev;
  const char *k;
  const char *t;
  if (q_full() || text_slot_queued(g_key_bufs[g_text_i]) ||
      text_slot_queued(g_text_bufs[g_text_i]))
    return;
  stash_key_text(name, text, &k, &t);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = k;
  ev.text = t;
  ev.key_mods = mods;
  ev.key_repeat = repeat ? 1 : 0;
  q_push(&ev);
}

static int x11_is_modifier(KeySym ks) {
  return ks == XK_Shift_L || ks == XK_Shift_R || ks == XK_Control_L ||
         ks == XK_Control_R || ks == XK_Alt_L || ks == XK_Alt_R ||
         ks == XK_Meta_L || ks == XK_Meta_R || ks == XK_Super_L ||
         ks == XK_Super_R || ks == XK_Hyper_L || ks == XK_Hyper_R ||
         ks == XK_Caps_Lock || ks == XK_Num_Lock || ks == XK_Scroll_Lock ||
         ks == XK_Mode_switch || ks == XK_ISO_Level3_Shift;
}

static int x11_key_no_insert(const char *name) {
  return strcmp(name, "Backspace") == 0 || strcmp(name, "Enter") == 0 ||
         strcmp(name, "Tab") == 0 || strcmp(name, "Escape") == 0 ||
         strcmp(name, "Delete") == 0 || strncmp(name, "Arrow", 5) == 0 ||
         strcmp(name, "Home") == 0 || strcmp(name, "End") == 0 ||
         strcmp(name, "PageUp") == 0 || strcmp(name, "PageDown") == 0;
}

static void x11_key_name(KeySym ks, char *out, size_t cap) {
  const char *s = NULL;
  const char *xs;
  if (!out || cap == 0)
    return;
  out[0] = '\0';
  switch (ks) {
  case XK_Return:
  case XK_KP_Enter:
    s = "Enter";
    break;
  case XK_Tab:
  case XK_ISO_Left_Tab:
    s = "Tab";
    break;
  case XK_BackSpace:
    s = "Backspace";
    break;
  case XK_Delete:
  case XK_KP_Delete:
    s = "Delete";
    break;
  case XK_Escape:
    s = "Escape";
    break;
  case XK_Left:
  case XK_KP_Left:
    s = "ArrowLeft";
    break;
  case XK_Right:
  case XK_KP_Right:
    s = "ArrowRight";
    break;
  case XK_Up:
  case XK_KP_Up:
    s = "ArrowUp";
    break;
  case XK_Down:
  case XK_KP_Down:
    s = "ArrowDown";
    break;
  case XK_Home:
  case XK_KP_Home:
    s = "Home";
    break;
  case XK_End:
  case XK_KP_End:
    s = "End";
    break;
  case XK_Page_Up:
  case XK_KP_Page_Up:
    s = "PageUp";
    break;
  case XK_Page_Down:
  case XK_KP_Page_Down:
    s = "PageDown";
    break;
  case XK_space:
  case XK_KP_Space:
    s = "Space";
    break;
  default:
    break;
  }
  if (s) {
    snprintf(out, cap, "%s", s);
    return;
  }
  if (ks >= XK_A && ks <= XK_Z) {
    out[0] = (char)('a' + (int)(ks - XK_A));
    out[1] = '\0';
    return;
  }
  if (ks >= XK_a && ks <= XK_z) {
    out[0] = (char)ks;
    out[1] = '\0';
    return;
  }
  if (ks >= XK_0 && ks <= XK_9) {
    out[0] = (char)ks;
    out[1] = '\0';
    return;
  }
  if (ks >= 0x20 && ks <= 0x7e) {
    out[0] = (char)ks;
    out[1] = '\0';
    return;
  }
  xs = XKeysymToString(ks);
  if (xs && xs[0]) {
    snprintf(out, cap, "%s", xs);
    return;
  }
  snprintf(out, cap, "Unidentified");
}

static void latin1_to_utf8(const char *in, int n, char *out, size_t cap) {
  size_t o = 0;
  int i;
  if (!out || cap == 0)
    return;
  out[0] = '\0';
  if (!in || n <= 0)
    return;
  for (i = 0; i < n && o + 3 < cap; i++) {
    unsigned char c = (unsigned char)in[i];
    if (c < 0x80)
      out[o++] = (char)c;
    else {
      out[o++] = (char)(0xc0 | (c >> 6));
      out[o++] = (char)(0x80 | (c & 0x3f));
    }
  }
  out[o] = '\0';
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
                   ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                   Button1MotionMask);
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
    if (ev.type == SelectionRequest)
      x11_serve_selection(&ev.xselectionrequest);
    if (ev.type == SelectionClear)
      g_clip_own = 0;
    if (ev.type == ButtonPress && ev.xbutton.button == 1)
      enqueue_pointer(SZ_POINTER_DOWN, (float)ev.xbutton.x, (float)ev.xbutton.y,
                      1);
    if (ev.type == ButtonPress && ev.xbutton.button == 3)
      enqueue_pointer(SZ_POINTER_DOWN, (float)ev.xbutton.x, (float)ev.xbutton.y,
                      3);
    if (ev.type == MotionNotify) {
      int button = 0;
      if (ev.xmotion.state & Button1Mask)
        button = 1;
      else if (ev.xmotion.state & Button3Mask)
        button = 3;
      enqueue_pointer(SZ_POINTER_MOVE, (float)ev.xmotion.x, (float)ev.xmotion.y,
                      button);
    }
    if (ev.type == ButtonRelease && ev.xbutton.button == 1)
      enqueue_pointer(SZ_POINTER_UP, (float)ev.xbutton.x, (float)ev.xbutton.y,
                      1);
    if (ev.type == ButtonRelease && ev.xbutton.button == 3)
      enqueue_pointer(SZ_POINTER_UP, (float)ev.xbutton.x, (float)ev.xbutton.y,
                      3);
    /* Wheel: 4 = up, 5 = down. Positive dy = content up (matches SZ_INPUT_SCROLL). */
    if (ev.type == ButtonPress && ev.xbutton.button == 4)
      enqueue_scroll((float)ev.xbutton.x, (float)ev.xbutton.y, 40.f);
    if (ev.type == ButtonPress && ev.xbutton.button == 5)
      enqueue_scroll((float)ev.xbutton.x, (float)ev.xbutton.y, -40.f);
    if (ev.type == KeyRelease) {
      if (XEventsQueued(g_dpy, QueuedAfterReading) > 0) {
        XEvent nev;
        XPeekEvent(g_dpy, &nev);
        if (nev.type == KeyPress && nev.xkey.keycode == ev.xkey.keycode &&
            nev.xkey.time == ev.xkey.time) {
          g_key_repeat_pending = 1;
          continue;
        }
      }
      if (g_key_held && ev.xkey.keycode == g_held_keycode)
        g_key_held = 0;
      continue;
    }
    if (ev.type == KeyPress) {
      KeySym unshifted;
      char buf[64];
      char name[KEY_NAME_LEN];
      char utf8[TEXT_LEN];
      int nkey;
      int mods = 0;
      int repeat = g_key_repeat_pending;
      g_key_repeat_pending = 0;
      if (g_key_held && ev.xkey.keycode == g_held_keycode)
        repeat = 1;
      g_held_keycode = ev.xkey.keycode;
      g_key_held = 1;
      unshifted = XLookupKeysym(&ev.xkey, 0);
      if (x11_is_modifier(unshifted))
        continue;
      nkey = XLookupString(&ev.xkey, buf, (int)sizeof(buf) - 1, NULL, NULL);
      if (nkey < 0)
        nkey = 0;
      buf[nkey] = '\0';
      x11_key_name(unshifted, name, sizeof name);
      utf8[0] = '\0';
      if (!x11_key_no_insert(name)) {
        latin1_to_utf8(buf, nkey, utf8, sizeof utf8);
        if (utf8[0] && (unsigned char)utf8[0] < 32)
          utf8[0] = '\0';
      }
      if (ev.xkey.state & ShiftMask)
        mods |= SZ_KEY_SHIFT;
      if (ev.xkey.state & ControlMask)
        mods |= SZ_KEY_CTRL;
      if (ev.xkey.state & Mod1Mask)
        mods |= SZ_KEY_ALT;
      if (ev.xkey.state & Mod4Mask)
        mods |= SZ_KEY_CMD;
      enqueue_key(name, utf8, mods, repeat);
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
  g_key_repeat_pending = 0;
  g_key_held = 0;
  g_clip_own = 0;
  g_atom_clipboard = g_atom_utf8 = g_atom_targets = g_atom_prop = 0;
  free(g_clip);
  g_clip = NULL;
}
