#ifndef SK_GPU_H
#define SK_GPU_H

#include <stddef.h>
#include <stdint.h>

typedef struct SkGpuTarget SkGpuTarget;

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
                      int cx0, int cy0, int cx1, int cy1);
const uint8_t *sk_gpu_read(SkGpuTarget *target, size_t *out_size);

#endif
