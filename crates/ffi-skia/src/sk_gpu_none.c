/* CPU-only stand-in when SCUZZ_SKIA is not gpu. */
#include "sk_gpu.h"

int sk_gpu_available(void) { return 0; }

int sk_gpu_roundtrip(const uint8_t *src, uint8_t *dst, int w, int h) {
  (void)src;
  (void)dst;
  (void)w;
  (void)h;
  return 0;
}
