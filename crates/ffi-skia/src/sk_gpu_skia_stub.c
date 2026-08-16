/* Symbols the pinned Skia CPU archive does not export. */
#include "sk_capi.h"

#include <stddef.h>

int sk_gpu_available(void) { return 0; }

SkSurface *sk_surface_make_gpu_n32_premul(int width, int height) {
  (void)width;
  (void)height;
  return NULL;
}
