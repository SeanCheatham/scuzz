#include "sk_capi.h"
#include "image_dec.h"

SkImage *sk_image_decode(const uint8_t *bytes, size_t len, const char **error) {
  int width = 0, height = 0;
  uint8_t *pixels = sk_decode_rgba(bytes, len, &width, &height, error);
  if (!pixels)
    return NULL;
  SkImage *image = sk_image_make_rgba(pixels, width, height);
  sk_decode_rgba_free(pixels);
  if (error)
    *error = image ? NULL : "Image allocation fails";
  return image;
}
