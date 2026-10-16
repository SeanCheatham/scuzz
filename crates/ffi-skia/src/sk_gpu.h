#ifndef SK_GPU_H
#define SK_GPU_H

#include <stddef.h>
#include <stdint.h>

typedef struct SkGpuTarget SkGpuTarget;

void *sk_gpu_image_new(const uint8_t *pixels, int width, int height);
void sk_gpu_image_free(void *image);
void sk_gpu_draw_image(SkGpuTarget *target, const void *image, float x, float y,
                       float w, float h, float opacity,
                       int cx0, int cy0, int cx1, int cy1);

void sk_gpu_draw_target(SkGpuTarget *target, const SkGpuTarget *source, float opacity,
                        int cx0, int cy0, int cx1, int cy1);

int sk_gpu_available(void);
SkGpuTarget *sk_gpu_target_new(int width, int height);
void sk_gpu_target_free(SkGpuTarget *target);
void sk_gpu_clear(SkGpuTarget *target, int x0, int y0, int x1, int y1,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void sk_gpu_fill_rect(SkGpuTarget *target, int x0, int y0, int x1, int y1,
                      int cx0, int cy0, int cx1, int cy1, uint8_t r, uint8_t g,
                      uint8_t b, uint8_t a);
void sk_gpu_draw_text(SkGpuTarget *target, const char *text, float x, float y,
                      float size, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                      int cx0, int cy0, int cx1, int cy1, int style);
const uint8_t *sk_gpu_read(SkGpuTarget *target, size_t *out_size);

#endif
