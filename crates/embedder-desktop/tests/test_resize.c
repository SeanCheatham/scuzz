/* A resize event reaches the session as SZ_INPUT_RESIZE. The next frame
 * uses the new size and the same window. */
#define _POSIX_C_SOURCE 199309L

#include "scuzz_embedder.h"
#include "x11_test.h"

#include <X11/Xatom.h>

#include <stdlib.h>

#define TITLE "scuzz-resize-test"
#define W 32
#define H 32
#define W2 96
#define H2 64
/* A window manager can select another size. Reserve a larger frame. */
#define MAXW 1024
#define MAXH 1024

/* A window manager can change a resize request. Without one, the server
 * uses the requested size. */
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
  for (i = 0; i < 1000 && !win; i++) {
    win = find_named(inj, DefaultRootWindow(inj), TITLE);
    if (!win)
      wait_for_events();
  }
  check(win != 0, "find window");
  if (!win) {
    XCloseDisplay(inj);
    sz_embedder_shutdown();
    return 1;
  }

  /* Resize the window through a separate connection. */
  wm = have_wm(inj);
  XResizeWindow(inj, win, W2, H2);
  XSync(inj, False);
  for (i = 0; i < 1000 && !got_resize; i++) {
    SzInputEvent ev;
    memset(&ev, 0, sizeof ev);
    while (sz_embedder_poll_event(&ev)) {
      if (ev.kind == SZ_INPUT_RESIZE && ev.width > 0 && ev.height > 0 &&
          (ev.width != W || ev.height != H)) {
        got_resize = 1;
        got_w = ev.width;
        got_h = ev.height;
      }
    }
    if (!got_resize)
      wait_for_events();
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

  /* Present the new frame in the same window. */
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
