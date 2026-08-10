#ifndef SCUZZ_PNG_ENC_H
#define SCUZZ_PNG_ENC_H

#include <stddef.h>
#include <stdint.h>

/* Encode RGBA8888 pixels to a PNG buffer. Caller frees with free(). */
uint8_t *sz_png_encode_rgba(const uint8_t *pixels, int width, int height,
                            int stride, size_t *out_len);

#endif
