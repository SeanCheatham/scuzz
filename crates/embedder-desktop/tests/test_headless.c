/* Headless checks for the X11 presenter. Runs without DISPLAY: window
 * creation must fail cleanly, and session state (the clipboard) must
 * survive those failures. Live input and blit paths need a real display. */
#include "scuzz_embedder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int ok, const char *name) {
  if (ok) {
    printf("ok: %s\n", name);
    return;
  }
  fprintf(stderr, "FAIL: %s\n", name);
  failures++;
}

int main(void) {
  static unsigned char px[16 * 16 * 4];
  const char *d = getenv("DISPLAY");
  char *got;
  char *again;

  if (d && *d) {
    fprintf(stderr, "test_headless: unset DISPLAY to run headless checks\n");
    return 2;
  }

  check(sz_embedder_available() == 0, "available: no DISPLAY");
  check(sz_embedder_alive() == 0, "alive: no DISPLAY");

  /* Session clipboard works before any window exists. */
  check(sz_embedder_clipboard_set("scuzz clip") == 1, "clipboard_set");
  got = sz_embedder_clipboard_get();
  check(got && strcmp(got, "scuzz clip") == 0, "clipboard_get roundtrip");
  again = sz_embedder_clipboard_get();
  check(again && again != got && strcmp(again, "scuzz clip") == 0,
        "clipboard_get returns a fresh copy");
  free(got);
  free(again);

  /* present() cannot open a display. The failure must not destroy the
   * clipboard: it is session state, not window state. */
  check(sz_embedder_present("t", 16, 16, 16, 16, px, sizeof px) == 0,
        "present without DISPLAY fails");
  got = sz_embedder_clipboard_get();
  check(got && strcmp(got, "scuzz clip") == 0,
        "clipboard survives failed present");
  free(got);

  /* X11 window geometry is CARD16 on the wire. Reject bigger frames up
   * front; truncating them would desync the blit. */
  check(sz_embedder_present("t", 70000, 16, 70000, 16, px, sizeof px) == 0,
        "present rejects width > 65535");
  check(sz_embedder_present("t", 16, 70000, 16, 70000, px, sizeof px) == 0,
        "present rejects height > 65535");
  got = sz_embedder_clipboard_get();
  check(got && strcmp(got, "scuzz clip") == 0,
        "clipboard survives rejected frames");
  free(got);

  /* Bad buffers must fail, not crash. */
  check(sz_embedder_present("t", 16, 16, 16, 16, NULL, sizeof px) == 0,
        "present rejects NULL frame");
  check(sz_embedder_present("t", 16, 16, 16, 16, px, 8) == 0,
        "present rejects short frame");

  /* Explicit shutdown ends the session and drops the clipboard. */
  sz_embedder_shutdown();
  got = sz_embedder_clipboard_get();
  check(got == NULL, "shutdown drops the clipboard");
  free(got);

  if (failures) {
    fprintf(stderr, "test_headless: %d failure(s)\n", failures);
    return 1;
  }
  printf("test_headless: all ok\n");
  return 0;
}
