#include "image.h"
#include "../../ffi-skia/src/image_dec.h"
#include <string.h>

static void image_drop(void *ptr) {
  SzImage *image = (SzImage *)ptr;
  void *bitmap = atomic_load(&image->bitmap);
  void (*drop)(void *) = atomic_load(&image->drop_bitmap);
  if (bitmap && drop)
    drop(bitmap);
}

SzImage *sz_image_decode(const uint8_t *bytes, size_t len, const char **error) {
  int width = 0, height = 0;
  uint8_t *pixels = sk_decode_rgba(bytes, len, &width, &height, error);
  if (!pixels)
    return NULL;
  sz_rc_set_image_drop(image_drop);
  size_t size = (size_t)width * height * 4;
  SzImage *image = (SzImage *)sz_rc_alloc(sizeof *image + size, SZ_RC_IMAGE);
  image->width = width;
  image->height = height;
  atomic_init(&image->bitmap, NULL);
  atomic_init(&image->drop_bitmap, NULL);
  memcpy(image->pixels, pixels, size);
  sk_decode_rgba_free(pixels);
  return image;
}

void sz_image_free(SzImage *image) { sz_release(image); }
int64_t sz_image_width(const SzImage *image) {
  return image ? image->width : 0;
}
int64_t sz_image_height(const SzImage *image) {
  return image ? image->height : 0;
}

SzAdt *sz_lang_image_decode(const SzString *bytes) {
  const char *error = NULL;
  SzImage *image = sz_image_decode(bytes ? (const uint8_t *)bytes->data : NULL,
                                  bytes ? (size_t)bytes->len : 0, &error);
  if (image) {
    SzAdt *result = sz_adt_new(1, image);
    sz_release(image);
    return result;
  }
  SzString *message = sz_string_from_cstr(error ? error : "Image decode fails");
  SzAdt *result = sz_adt_new(0, message);
  sz_release(message);
  return result;
}
