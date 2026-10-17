#include "image_dec.h"

#include <stdint.h>
#include <string.h>

/* Source: https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h */
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBIDEF static __attribute__((unused))
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 4096
#include "stb_image.h"

#define IMAGE_INPUT_LIMIT (32u * 1024u * 1024u)

static uint32_t read_be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static uint32_t png_crc(const uint8_t *p, size_t n) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < n; i++) {
    crc ^= p[i];
    for (int bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

static int valid_png(const uint8_t *bytes, size_t len) {
  size_t at = 8;
  int first = 1, data = 0;
  while (at < len) {
    if (len - at < 12)
      return 0;
    uint32_t size = read_be32(bytes + at);
    if ((size_t)size > len - at - 12)
      return 0;
    const uint8_t *type = bytes + at + 4;
    if (first && (memcmp(type, "IHDR", 4) || size != 13))
      return 0;
    first = 0;
    if (png_crc(type, (size_t)size + 4) != read_be32(type + 4 + size))
      return 0;
    if (!memcmp(type, "IDAT", 4))
      data = 1;
    at += (size_t)size + 12;
    if (!memcmp(type, "IEND", 4))
      return size == 0 && data && at == len;
  }
  return 0;
}

uint8_t *sk_decode_rgba(const uint8_t *bytes, size_t len, int *out_width, int *out_height, const char **error) {
  static const uint8_t png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  int width = 0, height = 0, channels = 0;
  if (error)
    *error = "Invalid PNG or JPEG data";
  if (!bytes || len == 0)
    return NULL;
  if (len > IMAGE_INPUT_LIMIT) {
    if (error)
      *error = "Image data exceeds 32 MiB";
    return NULL;
  }
  if (len >= 8 && !memcmp(bytes, png_signature, 8)) {
    if (!valid_png(bytes, len))
      return NULL;
  } else if (len < 4 || bytes[0] != 255 || bytes[1] != 216 ||
             bytes[len - 2] != 255 || bytes[len - 1] != 217) {
    return NULL;
  }
  if (!stbi_info_from_memory(bytes, (int)len, &width, &height, &channels))
    return NULL;
  if (width < 1 || height < 1 || width > 4096 || height > 4096) {
    if (error)
      *error = "Image dimensions exceed 4096 pixels";
    return NULL;
  }
  uint8_t *pixels = stbi_load_from_memory(bytes, (int)len, &width, &height,
                                         &channels, 4);
  if (!pixels)
    return NULL;
  size_t count = (size_t)width * (size_t)height;
  for (size_t i = 0; i < count; i++) {
    uint8_t *p = pixels + i * 4;
    p[0] = (uint8_t)((p[0] * p[3]) / 255);
    p[1] = (uint8_t)((p[1] * p[3]) / 255);
    p[2] = (uint8_t)((p[2] * p[3]) / 255);
  }
  *out_width = width;
  *out_height = height;
  if (error)
    *error = NULL;
  return pixels;
}

void sk_decode_rgba_free(uint8_t *pixels) { stbi_image_free(pixels); }
