/* Idle pumps must still read X11. A static frame never calls present
 * again; poll_event is the only drain. This test sends WM_DELETE after
 * one present. Clicks use the same drain. */
#include "scuzz_embedder.h"

#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITLE "scuzz-idle-event-test"
#define W 16
#define H 16

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

static void send_close(Display *dpy, Window win) {
  XEvent ev;
  Atom proto;
  Atom del;
  memset(&ev, 0, sizeof ev);
  proto = XInternAtom(dpy, "WM_PROTOCOLS", False);
  del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  ev.xclient.type = ClientMessage;
  ev.xclient.display = dpy;
  ev.xclient.window = win;
  ev.xclient.message_type = proto;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = (long)del;
  ev.xclient.data.l[1] = CurrentTime;
  XSendEvent(dpy, win, False, NoEventMask, &ev);
}

int main(void) {
  static unsigned char px[W * H * 4];
  Display *inj;
  Window win = 0;
  int i;
  const char *d = getenv("DISPLAY");

  if (!d || !d[0]) {
    printf("test_idle_events: skip (no DISPLAY)\n");
    return 0;
  }

  memset(px, 0x80, sizeof px);
  if (!sz_embedder_present(TITLE, W, H, W, H, px, sizeof px)) {
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

  send_close(inj, win);
  XSync(inj, False);
  for (i = 0; i < 1000 && sz_embedder_alive(); i++) {
    SzInputEvent dump;
    memset(&dump, 0, sizeof dump);
    (void)sz_embedder_poll_event(&dump);
  }
  check(sz_embedder_alive() == 0, "poll receives close without present");
  XCloseDisplay(inj);

  if (failures) {
    fprintf(stderr, "test_idle_events: %d failure(s)\n", failures);
    return 1;
  }
  printf("test_idle_events: all ok\n");
  return 0;
}
