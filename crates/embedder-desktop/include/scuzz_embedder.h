#ifndef SCUZZ_EMBEDDER_H
#define SCUZZ_EMBEDDER_H

#include "scuzz_ui.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Desktop Window presenter (Linux X11 / Darwin Cocoa).
 * Weak stubs live in runtime when unlinked. */

/* Nonzero if a display can be opened (DISPLAY+X11, or Cocoa GUI session). */
int sz_embedder_available(void);

/* Nonzero while the desktop window session should keep pumping.
 * Becomes 0 after the user quits (q / Escape / window close).
 * Weak stub returns 0 (one-shot demos). */
int sz_embedder_alive(void);

/* Present an RGBA8888 frame in a window. Blocks briefly to process events.
 * Returns 1 on success, 0 if embedder unavailable / failed. */
int sz_embedder_present(const char *title, int width, int height,
                        const uint8_t *rgba, size_t nbytes);

/* Destroy the window / display connection. */
void sz_embedder_shutdown(void);

/* Pop one queued OS event into out. Returns 1 if an event was written.
 * present() enqueues taps / text edits; pump drains via this. */
int sz_embedder_poll_event(SzInputEvent *out);

#ifdef __cplusplus
}
#endif

#endif /* SCUZZ_EMBEDDER_H */
