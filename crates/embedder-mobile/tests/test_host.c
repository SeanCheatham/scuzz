#define _POSIX_C_SOURCE 200809L
#include "scuzz_mobile.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  uint8_t pixels[3 * 3 * 4] = {0};
  SzInputEvent event = {0};
  char log[4096] = {0};
  FILE *capture = tmpfile();
  int saved_stderr = dup(STDERR_FILENO);
  char *first;
  char *second;
  assert(capture && saved_stderr >= 0);
  assert(dup2(fileno(capture), STDERR_FILENO) >= 0);

  /* A disabled shell cannot present a frame. */
  assert(unsetenv("SCUZZ_MOBILE_SHELL") == 0);
  assert(!sz_mobile_available());
  assert(!sz_mobile_present("frame", 1, 1, 1, 1, pixels, sizeof pixels));
  assert(setenv("SCUZZ_MOBILE_SHELL", "0", 1) == 0);
  assert(!sz_mobile_available());
  assert(setenv("SCUZZ_MOBILE_SHELL", "", 1) == 0);
  assert(!sz_mobile_available());
  assert(setenv("SCUZZ_MOBILE_SHELL", "1", 1) == 0);
  assert(sz_mobile_available());
  assert(!sz_mobile_alive());
  assert(!sz_mobile_poll_event(&event));

  /* Each positive pixel size needs four bytes per pixel. */
  for (int width = -1; width <= 3; width++) {
    for (int height = -1; height <= 3; height++) {
      for (size_t bytes = 0; bytes <= sizeof pixels; bytes++) {
        int expected = width > 0 && height > 0 &&
                       bytes >= (size_t)width * (size_t)height * 4;
        assert(sz_mobile_present("frame", 1, 1, width, height, pixels,
                                 bytes) == expected);
      }
    }
  }
  assert(!sz_mobile_present("frame", 0, 1, 1, 1, pixels, sizeof pixels));
  assert(!sz_mobile_present("frame", 1, -1, 1, 1, pixels, sizeof pixels));
  assert(!sz_mobile_present("frame", 1, 1, 1, 1, NULL, sizeof pixels));
  assert(!sz_mobile_present("frame", 1, 1, INT_MAX, INT_MAX, pixels,
                            sizeof pixels));

  /* Clipboard reads own separate copies. Shutdown clears session state. */
  assert(!sz_mobile_clipboard_get());
  assert(sz_mobile_clipboard_set("text"));
  first = sz_mobile_clipboard_get();
  second = sz_mobile_clipboard_get();
  assert(first && second && first != second);
  assert(!strcmp(first, "text") && !strcmp(second, "text"));
  first[0] = 'X';
  assert(!strcmp(second, "text"));
  assert(sz_mobile_clipboard_set(NULL));
  assert(!strcmp(second, "text"));
  free(first);
  free(second);
  first = sz_mobile_clipboard_get();
  assert(first && !*first);
  free(first);
  sz_mobile_set_keyboard(1);
  sz_mobile_shutdown();
  assert(!sz_mobile_clipboard_get());
  sz_mobile_shutdown();

  /* A new session starts at frame one with the keyboard hidden. */
  assert(fflush(stderr) == 0);
  assert(ftruncate(fileno(capture), 0) == 0);
  rewind(capture);
  assert(sz_mobile_present(NULL, 1, 1, 1, 1, pixels, sizeof pixels));
  sz_mobile_set_keyboard(1);
  assert(sz_mobile_present("title", 1, 1, 1, 1, pixels, sizeof pixels));
  sz_mobile_shutdown();
  assert(fflush(stderr) == 0);
  assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
  close(saved_stderr);
  rewind(capture);
  assert(fread(log, 1, sizeof log - 1, capture) > 0);
  assert(!strcmp(log,
                 "scuzz mobile: present 1x1 px=1x1 frame=1 keyboard=0 title=Scuzz Lang\n"
                 "scuzz mobile: soft keyboard show\n"
                 "scuzz mobile: present 1x1 px=1x1 frame=2 keyboard=1 title=title\n"
                 "scuzz mobile: shutdown\n"));
  fclose(capture);
  puts("mobile host tests ok");
  return 0;
}
