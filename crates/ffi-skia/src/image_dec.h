#ifndef SCUZZ_IMAGE_DEC_H
#define SCUZZ_IMAGE_DEC_H
#include <stddef.h>
#include <stdint.h>

/* Return premultiplied RGBA pixels. The caller owns the buffer. */
uint8_t *sk_decode_rgba(const uint8_t *bytes, size_t len, int *width, int *height,
                       const char **error);
void sk_decode_rgba_free(uint8_t *pixels);
#endif
