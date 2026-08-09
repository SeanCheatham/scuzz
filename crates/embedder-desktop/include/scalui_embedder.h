#ifndef SCALUI_EMBEDDER_H
#define SCALUI_EMBEDDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Linux X11 embedder (Phase 3). Weak stubs live in runtime when unlinked. */

/* Nonzero if a display can be opened (DISPLAY set + X11). */
int su_embedder_available(void);

/* Present an RGBA8888 frame in a window. Blocks briefly to process X events.
 * Returns 1 on success, 0 if embedder unavailable / failed. */
int su_embedder_present(const char *title, int width, int height,
                        const uint8_t *rgba, size_t nbytes);

/* Destroy the window / display connection. */
void su_embedder_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* SCALUI_EMBEDDER_H */
