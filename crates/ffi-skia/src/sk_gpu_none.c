/* CPU-only stand-in when SCUZZ_SKIA is not gpu. */
#include "sk_gpu.h"

int sk_gpu_available(void) { return 0; }

SkGpuTarget *sk_gpu_target_new(int width, int height) {
  (void)width;
  (void)height;
  return NULL;
}

void sk_gpu_target_free(SkGpuTarget *target) { (void)target; }

void sk_gpu_clear(SkGpuTarget *target, int x0, int y0, int x1, int y1,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  (void)target;
  (void)x0;
  (void)y0;
  (void)x1;
  (void)y1;
  (void)r;
  (void)g;
  (void)b;
  (void)a;
}

void sk_gpu_fill_rect(SkGpuTarget *target, int x0, int y0, int x1, int y1,
                      int cx0, int cy0, int cx1, int cy1, uint8_t r, uint8_t g,
                      uint8_t b, uint8_t a) {
  (void)target;
  (void)x0;
  (void)y0;
  (void)x1;
  (void)y1;
  (void)cx0;
  (void)cy0;
  (void)cx1;
  (void)cy1;
  (void)r;
  (void)g;
  (void)b;
  (void)a;
}

const uint8_t *sk_gpu_read(SkGpuTarget *target, size_t *out_size) {
  (void)target;
  if (out_size)
    *out_size = 0;
  return NULL;
}

void *sk_gpu_image_new(const uint8_t *pixels, int width, int height) {
  (void)pixels; (void)width; (void)height;
  return NULL;
}
void sk_gpu_image_free(void *image) { (void)image; }
void sk_gpu_draw_image(SkGpuTarget *target, const void *image, double x, double y,
                       double w, double h, float opacity,
                       int cx0, int cy0, int cx1, int cy1) {
  (void)target; (void)image; (void)x; (void)y; (void)w; (void)h;
  (void)opacity; (void)cx0; (void)cy0; (void)cx1; (void)cy1;
}

void sk_gpu_draw_target(SkGpuTarget *target, const SkGpuTarget *source, float opacity,
                        int cx0, int cy0, int cx1, int cy1) {
  (void)target; (void)source; (void)opacity;
  (void)cx0; (void)cy0; (void)cx1; (void)cy1;
}
