/* Canvas clip against the archive `make lib` actually built. */
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
  /* Inside clip: fill. Outside: clear color. Peek is RGBA. */
  assert(px[(10 * 32 + 10) * 4] == 240);
  assert(px[(10 * 32 + 10) * 4 + 1] == 240);
  assert(px[(10 * 32 + 10) * 4 + 2] == 240);
  assert(px[0] == 20);
  assert(px[1] == 40);
  assert(px[2] == 80);
  assert(px[(20 * 32 + 20) * 4] == 20);

  /* Deep save/clip nesting. view.c nests one save per view level. A deep
   * tree must not die or corrupt the clip. */
  {
    int i;
    for (i = 0; i < 64; i++) {
      sk_canvas_save(canvas);
      sk_canvas_clip_rect(canvas, (float)i / 4.f, 0, 32, 32);
    }
    sk_paint_set_color(paint, sk_color_rgba(255, 255, 255, 255));
    sk_canvas_draw_rect(canvas, 0, 0, 32, 32, paint);
    for (i = 0; i < 64; i++)
      sk_canvas_restore(canvas);
    px = sk_surface_peek_pixels(surf, &px_len);
    assert(px && px_len == 32 * 32 * 4);
    /* Clip narrowed to x >= 15. Left stays background. Right is white. */
    assert(px[0] == 20 && px[1] == 40 && px[2] == 80);
    assert(px[(20 * 32 + 20) * 4] == 255);
    /* Restore must return the full-surface clip. */
    sk_paint_set_color(paint, sk_color_rgba(20, 40, 80, 255));
    sk_canvas_draw_rect(canvas, 0, 0, 32, 32, paint);
    px = sk_surface_peek_pixels(surf, &px_len);
    assert(px && px[0] == 20 && px[(20 * 32 + 20) * 4] == 20);
  }

  /* Clear replaces. It does not blend. Clear to transparent erases. */
  {
    sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
    px = sk_surface_peek_pixels(surf, &px_len);
    assert(px && px_len == 32 * 32 * 4);
    assert(px[0] == 0 && px[1] == 0 && px[2] == 0 && px[3] == 0);
  }

  sk_paint_delete(paint);
  sk_surface_unref(surf);
  puts("ffi-skia clip tests ok");
  return 0;
}
