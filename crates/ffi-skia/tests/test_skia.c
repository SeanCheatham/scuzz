#include "sk_capi.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  SkSurface *surf = sk_surface_make_raster_n32_premul(64, 32);
  SkCanvas *canvas;
  SkPaint *paint;
  uint8_t *png = NULL;
  size_t png_len = 0;
  const uint8_t *px;
  size_t px_len = 0;

  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  paint = sk_paint_new();
  assert(paint);

  sk_canvas_clear(canvas, sk_color_rgba(20, 40, 80, 255));
  sk_paint_set_color(paint, sk_color_rgba(240, 240, 240, 255));
  sk_canvas_draw_rect(canvas, 8, 8, 48, 16, paint);
  sk_paint_set_color(paint, sk_color_rgba(20, 40, 80, 255));
  sk_paint_set_text_size(paint, 8.f);
  sk_canvas_draw_string(canvas, "Scuzz Lang", 14, 20, paint);

  assert(sk_font_measure_string("Scuzz", 8.f) == 40.f);
  assert(sk_font_measure_string("", 8.f) == 0.f);
  assert(sk_paint_get_text_size(paint) == 8.f);

  px = sk_surface_peek_pixels(surf, &px_len);
  assert(px && px_len == 64 * 32 * 4);
  /* light bar pixel outside the glyph ink */
  assert(px[(10 * 64 + 10) * 4 + 0] > 200);
  /* background outside the bar stays dark */
  assert(px[(0 * 64 + 0) * 4 + 0] < 40);

  assert(sk_encode_png(surf, &png, &png_len));
  assert(png_len > 8);
  assert(png[0] == 137 && png[1] == 80 && png[2] == 78 && png[3] == 71);

  free(png);
  sk_paint_delete(paint);
  (void)canvas;
  sk_surface_unref(surf);
  puts("ffi-skia tests ok");
  return 0;
}
