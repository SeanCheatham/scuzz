/* SkColor constructors. Shared by every backend so the packing is defined
 * in one place. */
#include "sk_capi.h"

SkColor sk_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  SkColor c = {r, g, b, a};
  return c;
}

SkColor sk_color_argb(uint32_t argb) {
  SkColor c;
  c.a = (uint8_t)((argb >> 24) & 0xff);
  c.r = (uint8_t)((argb >> 16) & 0xff);
  c.g = (uint8_t)((argb >> 8) & 0xff);
  c.b = (uint8_t)(argb & 0xff);
  return c;
}
