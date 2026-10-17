#ifndef SCUZZ_X11_TEST_H
#define SCUZZ_X11_TEST_H

#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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

/* Give the X server time to deliver events between polls. */
static void wait_for_events(void) {
  const struct timespec delay = {0, 1000000L};
  nanosleep(&delay, NULL);
}

#endif
