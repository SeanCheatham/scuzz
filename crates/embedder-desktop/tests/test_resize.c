/* A WM resize must reach the session as SZ_INPUT_RESIZE, and the next
 * present must blit at the new size without recreating the window.
 * This test resizes the window from a second connection (the WM role),
 * polls for the event, then presents at the new size. */
#define _POSIX_C_SOURCE 199309L

#include "scuzz_embedder.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TITLE "scuzz-resize-test"
#define W 32
#define H 32
#define W2 96
#define H2 64
/* A WM may pick its own size. The frame buffer covers any plausible one. */
#define MAXW 1024
#define MAXH 1024

static int failures;

static void check(int ok, const char *name) {
  if (ok) {
    printf("ok: %s\n", name);
    return;
  }
  fprintf(stderr, "FAIL: %s\n", name);
  failures++;
}

static Window find_named(Display *dpy, Window w, const char *want) {
  Window root = 0;
  Window parent = 0;
  Window *kids = NULL;
  unsigned n = 0;
  unsigned i;
  char *name = NULL;
  Window hit = 0;

  if (XFetchName(dpy, w, &name)) {
    if (name && strcmp(name, want) == 0) {
      XFree(name);
      return w;
    }
    if (name)
      XFree(name);
  }
  if (!XQueryTree(dpy, w, &root, &parent, &kids, &n) || !kids)
    return 0;
  for (i = 0; i < n && !hit; i++)
    hit = find_named(dpy, kids[i], want);
  XFree(kids);
  return hit;
}

/* A WM may redirect or rewrite a client resize request. Without a WM the
 * server resizes exactly. */
static int have_wm(Display *dpy) {
  Atom atom;
  Atom type = None;
  int fmt = 0;
  unsigned long nitems = 0;
  unsigned long after = 0;
  unsigned char *data = NULL;
  int ok;
  atom = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", True);
  if (atom == None)
    return 0;
  ok = XGetWindowProperty(dpy, DefaultRootWindow(dpy), atom, 0, 1, False,
                          XA_WINDOW, &type, &fmt, &nitems, &after,
                          &data) == Success &&
       type == XA_WINDOW && nitems > 0;
  if (data)
    XFree(data);
  return ok;
}

int main(void) {
  static unsigned char px[MAXW * MAXH * 4];
  Display *inj;
  Window win = 0;
  int i;
  int got_resize = 0;
  int got_w = 0;
  int got_h = 0;
  int wm = 0;
  const char *d = getenv("DISPLAY");

  if (!d || !d[0]) {
    printf("test_resize: skip (no DISPLAY)\n");
    return 0;
  }

  memset(px, 0x40, sizeof px);
  if (!sz_embedder_present(TITLE, W, H, W, H, px, (size_t)W * H * 4)) {
    fprintf(stderr, "FAIL: present\n");
    return 1;
  }
  check(sz_embedder_alive() != 0, "alive after present");

  inj = XOpenDisplay(NULL);
  if (!inj) {
    fprintf(stderr, "FAIL: second Display\n");
    sz_embedder_shutdown();
    return 1;
  }
  for (i = 0; i < 1000 && !win; i++)
    win = find_named(inj, DefaultRootWindow(inj), TITLE);
  check(win != 0, "find window");
  if (!win) {
    XCloseDisplay(inj);
    sz_embedder_shutdown();
    return 1;
  }

  /* The WM role: resize the window. */
  wm = have_wm(inj);
  XResizeWindow(inj, win, W2, H2);
  XSync(inj, False);
  for (i = 0; i < 1000 && !got_resize; i++) {
    SzInputEvent ev;
    struct timespec ts;
    memset(&ev, 0, sizeof ev);
    while (sz_embedder_poll_event(&ev)) {
      if (ev.kind == SZ_INPUT_RESIZE && ev.width > 0 && ev.height > 0 &&
          (ev.width != W || ev.height != H)) {
        got_resize = 1;
        got_w = ev.width;
        got_h = ev.height;
      }
    }
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000L; /* 1 ms */
    nanosleep(&ts, NULL);
  }
  check(got_resize, "poll receives resize without present");
  if (!got_resize) {
    XCloseDisplay(inj);
    sz_embedder_shutdown();
    return 1;
  }
  if (!wm)
    check(got_w == W2 && got_h == H2, "resize matches the request (no WM)");
  XCloseDisplay(inj);
  if (got_w > MAXW || got_h > MAXH) {
    printf("test_resize: skip present (WM size %dx%d over test cap)\n", got_w,
           got_h);
    sz_embedder_shutdown();
    return failures ? 1 : 0;
  }

  /* The session side: paint lands at the new size on the same window. */
  check(sz_embedder_present(TITLE, got_w, got_h, got_w, got_h, px,
                            (size_t)got_w * got_h * 4) == 1,
        "present at new size");
  check(sz_embedder_alive() != 0, "alive after resized present");

  sz_embedder_shutdown();
  if (failures) {
    fprintf(stderr, "test_resize: %d failure(s)\n", failures);
    return 1;
  }
  printf("test_resize: all ok\n");
  return 0;
}
