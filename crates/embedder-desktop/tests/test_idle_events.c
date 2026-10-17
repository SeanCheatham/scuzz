/* An idle session reads X11 through poll_event. Send a close event after
 * one frame. Check that the session closes without another present call. */
#define _POSIX_C_SOURCE 199309L
#include "scuzz_embedder.h"
#include "x11_test.h"

#include <stdlib.h>

#define TITLE "scuzz-idle-event-test"
#define W 16
#define H 16

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

  send_close(inj, win);
  XSync(inj, False);
  for (i = 0; i < 1000 && sz_embedder_alive(); i++) {
    SzInputEvent dump;
    memset(&dump, 0, sizeof dump);
    (void)sz_embedder_poll_event(&dump);
    if (sz_embedder_alive())
      wait_for_events();
  }
  check(sz_embedder_alive() == 0, "poll receives close without present");
  XCloseDisplay(inj);

  sz_embedder_shutdown();
  if (failures) {
    fprintf(stderr, "test_idle_events: %d failure(s)\n", failures);
    return 1;
  }
  printf("test_idle_events: all ok\n");
  return 0;
}
