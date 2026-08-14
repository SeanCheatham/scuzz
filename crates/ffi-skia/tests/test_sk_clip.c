/* Canvas clip against in-tree sk_sw. The pinned Skia archive does not
 * export save/clip/restore. */
#include "sk_capi.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
  SkSurface *surf = sk_surface_make_raster_n32_premul(32, 32);
  SkCanvas *canvas;
  SkPaint *paint;
  const uint8_t *px;
  size_t px_len = 0;

  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  paint = sk_paint_new();
  assert(canvas && paint);

  sk_canvas_clear(canvas, sk_color_rgba(20, 40, 80, 255));
  sk_canvas_save(canvas);
  sk_canvas_clip_rect(canvas, 8, 8, 12, 12);
  sk_paint_set_color(paint, sk_color_rgba(240, 240, 240, 255));
  sk_canvas_draw_rect(canvas, 0, 0, 32, 32, paint);
  sk_canvas_restore(canvas);
  px = sk_surface_peek_pixels(surf, &px_len);
  assert(px && px_len == 32 * 32 * 4);
  /* Inside clip: fill. Outside: clear color. */
  assert(px[(10 * 32 + 10) * 4] == 240);
  assert(px[(10 * 32 + 10) * 4 + 1] == 240);
  assert(px[(10 * 32 + 10) * 4 + 2] == 240);
  assert(px[0] == 20);
  assert(px[1] == 40);
  assert(px[2] == 80);
  assert(px[(20 * 32 + 20) * 4] == 20);

  sk_paint_delete(paint);
  sk_surface_unref(surf);
  puts("ffi-skia clip tests ok");
  return 0;
}
