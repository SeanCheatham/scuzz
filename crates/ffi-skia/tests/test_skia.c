#include "sk_capi.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cell_has_ink(const uint8_t *px, int w, int x0, int x1, int y0, int y1) {
  int x, y;
  for (y = y0; y < y1; y++) {
    for (x = x0; x < x1; x++) {
      const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
      if (p[0] > 40 || p[1] > 40 || p[2] > 40)
        return 1;
    }
  }
  return 0;
}

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

  float measured = sk_font_measure_string("Scuzz", 8.f);
  float cell;
  float ii;
  float ww;
  assert(measured > 0.f);
  assert(sk_font_measure_string("", 8.f) == 0.f);
  assert(sk_paint_get_text_size(paint) == 8.f);
  /* sk_sw is monospace (5 * 8); real Skia is proportional — both OK. */
  (void)measured;
  cell = sk_font_mono_cell(8.f);
  assert(cell > 0.f);
  ii = sk_font_measure_mono_string("ii", 8.f);
  ww = sk_font_measure_mono_string("WW", 8.f);
  assert(ii == ww);
  assert(ii == 2.f * sk_font_measure_mono_string("i", 8.f));
  assert(sk_font_measure_mono_string("", 8.f) == 0.f);
  sk_paint_set_color(paint, sk_color_rgba(240, 240, 240, 255));
  sk_canvas_draw_mono_string(canvas, "ii", 8, 28, paint);

  px = sk_surface_peek_pixels(surf, &px_len);
  assert(px && px_len == 64 * 32 * 4);
  /* Surface has ink somewhere after clear + rect + string. */
  {
    size_t i;
    int saw_light = 0;
    for (i = 0; i < px_len; i += 4) {
      if (px[i] > 200 && px[i + 1] > 200 && px[i + 2] > 200) {
        saw_light = 1;
        break;
      }
    }
    assert(saw_light);
  }

  /* Peek is row-major RGBA. Red is byte 0 with no channel remap. */
  {
    SkSurface *reds = sk_surface_make_raster_n32_premul(4, 4);
    SkCanvas *rc;
    const uint8_t *rp;
    size_t rn = 0;
    assert(reds);
    rc = sk_surface_get_canvas(reds);
    sk_paint_set_color(paint, sk_color_rgba(255, 0, 0, 255));
    sk_canvas_draw_rect(rc, 0, 0, 4, 4, paint);
    rp = sk_surface_peek_pixels(reds, &rn);
    assert(rp && rn >= 4);
    assert(rp[0] == 255 && rp[1] < 50 && rp[2] < 50);
    sk_surface_unref(reds);
  }

  /* UTF-8: one code point is one cell on sk_sw. Never two cells for "é". */
  {
    float e = sk_font_measure_string("e", 8.f);
    float acute = sk_font_measure_string("é", 8.f);
    float ee = sk_font_measure_string("ee", 8.f);
    assert(e > 0.f);
    assert(acute > 0.f);
    assert(acute != ee);
    if (sk_font_measure_string("i", 8.f) == sk_font_measure_string("W", 8.f))
      assert(acute == e);
  }

  /* draw_mono("éx"): x stays in cell 1. Cell 2 stays empty. */
  {
    SkSurface *mono = sk_surface_make_raster_n32_premul(48, 16);
    SkCanvas *mc;
    const uint8_t *mp;
    size_t mn = 0;
    int cw;
    assert(mono);
    mc = sk_surface_get_canvas(mono);
    sk_canvas_clear(mc, sk_color_rgba(0, 0, 0, 255));
    sk_paint_set_color(paint, sk_color_rgba(240, 240, 240, 255));
    sk_paint_set_text_size(paint, 8.f);
    sk_canvas_draw_mono_string(mc, "éx", 0, 8, paint);
    mp = sk_surface_peek_pixels(mono, &mn);
    assert(mp && mn == 48 * 16 * 4);
    cw = (int)(sk_font_mono_cell(8.f) + 0.5f);
    if (cw < 1)
      cw = 1;
    assert(cell_has_ink(mp, 48, 0, cw, 0, 16));
    assert(cell_has_ink(mp, 48, cw, cw * 2, 0, 16));
    assert(!cell_has_ink(mp, 48, cw * 2, cw * 3, 0, 16));
    sk_surface_unref(mono);
  }

  /* save / clip / restore on the archive this test linked. */
  {
    SkSurface *clip = sk_surface_make_raster_n32_premul(32, 32);
    SkCanvas *cc;
    const uint8_t *cp;
    size_t cn = 0;
    assert(clip);
    cc = sk_surface_get_canvas(clip);
    sk_canvas_clear(cc, sk_color_rgba(20, 40, 80, 255));
    sk_canvas_save(cc);
    sk_canvas_clip_rect(cc, 8, 8, 12, 12);
    sk_paint_set_color(paint, sk_color_rgba(240, 240, 240, 255));
    sk_canvas_draw_rect(cc, 0, 0, 32, 32, paint);
    sk_canvas_restore(cc);
    cp = sk_surface_peek_pixels(clip, &cn);
    assert(cp && cn == 32 * 32 * 4);
    assert(cp[(10 * 32 + 10) * 4] == 240);
    assert(cp[(10 * 32 + 10) * 4 + 1] == 240);
    assert(cp[(10 * 32 + 10) * 4 + 2] == 240);
    assert(cp[0] == 20);
    assert(cp[1] == 40);
    assert(cp[2] == 80);
    assert(cp[(20 * 32 + 20) * 4] == 20);
    sk_surface_unref(clip);
  }

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
