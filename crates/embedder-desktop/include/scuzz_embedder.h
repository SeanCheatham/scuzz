#ifndef SCUZZ_EMBEDDER_H
#define SCUZZ_EMBEDDER_H

#include "scuzz_ui.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Desktop presenter (Linux X11 / Darwin Cocoa).
 * Weak stubs live in runtime when unlinked. */

/* Nonzero if a display can be opened (DISPLAY+X11, or Cocoa GUI session). */
int sz_embedder_available(void);

/* Backing scale for the primary display (1.0 if unknown / unavailable).
 * Retina macOS typically returns 2.0. */
double sz_embedder_display_scale(void);

/* Nonzero while the desktop window session must keep pumping.
 * Becomes 0 after the user closes the window.
 * Weak stub returns 0 (one-shot demos). */
int sz_embedder_alive(void);

/* Present an RGBA8888 frame in a window. Blocks briefly to process events.
 * point_w/point_h are the window size in points; pixel_w/pixel_h are the
 * rgba dimensions (point * backing scale). Returns 1 on success. */
int sz_embedder_present(const char *title, int point_w, int point_h,
                        int pixel_w, int pixel_h, const uint8_t *rgba,
                        size_t nbytes);

/* Destroy the window / display connection. */
void sz_embedder_shutdown(void);

/* Pop one queued OS event into out. Returns 1 if an event was written.
 * present() enqueues pointer / scroll / key events. pump drains through this. */
int sz_embedder_poll_event(SzInputEvent *out);

/* Session clipboard sync. `set` stores UTF-8 on the OS pasteboard when a
 * window exists. `get` returns malloc'd UTF-8 or NULL; caller frees with
 * free(). Weak stubs in the runtime return 0 / NULL. */
int sz_embedder_clipboard_set(const char *text);
char *sz_embedder_clipboard_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SCUZZ_EMBEDDER_H */
