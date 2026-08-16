#ifndef SK_GPU_H
#define SK_GPU_H

#include <stddef.h>
#include <stdint.h>

int sk_gpu_available(void);
int sk_gpu_roundtrip(const uint8_t *src, uint8_t *dst, int w, int h);

#endif
