/* Canvas clip against the archive `make lib` actually built. */
#include "sk_capi.h"

#include <assert.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const uint8_t *pixels(SkSurface *surface) {
  size_t len = 0;
  const uint8_t *data = sk_surface_peek_pixels(surface, &len);
  assert(data && len == 64 * 64 * 4);
  return data;
}

static void check_box(SkSurface *surface, int x0, int y0, int x1, int y1, int alpha) {
  const uint8_t *data = pixels(surface);
  for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
    const uint8_t *p = data + (y * 64 + x) * 4;
    int a = x >= x0 && x < x1 && y >= y0 && y < y1 ? alpha : 0;
    assert(p[0] == a && p[1] == 0 && p[2] == 0 && p[3] == a);
  }
}

static void check_scale(int gpu) {
  SkSurface *surface = gpu ? sk_surface_make_gpu_n32_premul(64, 64)
                           : sk_surface_make_raster_n32_premul(64, 64);
  assert(surface);
  SkCanvas *canvas = sk_surface_get_canvas(surface);
  SkPaint *paint = sk_paint_new();
  assert(canvas && paint);
  sk_paint_set_color(paint, sk_color_rgba(255, 0, 0, 255));

  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_save(canvas);
  assert(sk_canvas_scale(canvas, 2, 8, 8));
  sk_canvas_draw_rect(canvas, 6, 6, 4, 4, paint);
  check_box(surface, 4, 4, 12, 12, 255);
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_save(canvas);
  assert(sk_canvas_scale(canvas, .5f, 8, 8));
  sk_canvas_draw_rect(canvas, 6, 6, 4, 4, paint);
  check_box(surface, 6, 6, 10, 10, 255);
  sk_canvas_restore(canvas);
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_draw_rect(canvas, 6, 6, 4, 4, paint);
  check_box(surface, 4, 4, 12, 12, 255);
  sk_canvas_restore(canvas);

  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_save(canvas);
  assert(sk_canvas_scale(canvas, .5f, 8, 8));
  sk_canvas_draw_rect(canvas, 6, 6, 4, 4, paint);
  check_box(surface, 7, 7, 9, 9, 255);
  sk_canvas_restore(canvas);

  /* The ancestor clip stays in device coordinates. The child clip scales. */
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_save(canvas);
  sk_canvas_clip_rect(canvas, 5, 3, 6, 7);
  assert(sk_canvas_scale(canvas, 2, 8, 8));
  sk_canvas_clip_rect(canvas, 6, 6, 4, 4);
  sk_canvas_draw_rect(canvas, 0, 0, 64, 64, paint);
  check_box(surface, 5, 4, 11, 10, 255);
  sk_canvas_restore(canvas);

  /* Composite each layer once in device coordinates. */
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_save(canvas);
  assert(sk_canvas_scale(canvas, 2, 8, 8));
  assert(sk_canvas_save_layer_alpha(canvas, 128));
  sk_canvas_draw_rect(canvas, 6, 6, 4, 4, paint);
  sk_canvas_restore(canvas);
  check_box(surface, 4, 4, 12, 12, 128);
  assert(sk_canvas_save_layer_alpha(canvas, 255));
  assert(sk_canvas_scale(canvas, .5f, 8, 8));
  sk_canvas_restore(canvas);
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_draw_rect(canvas, 6, 6, 4, 4, paint);
  check_box(surface, 4, 4, 12, 12, 255);
  sk_canvas_restore(canvas);

  /* Invalid transforms leave the matrix unchanged. */
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_save(canvas);
  assert(sk_canvas_scale(canvas, 2, 8, 8));
  assert(!sk_canvas_scale(canvas, -1, 0, 0));
  assert(!sk_canvas_scale(canvas, NAN, 0, 0));
  assert(!sk_canvas_scale(canvas, INFINITY, 0, 0));
  assert(!sk_canvas_scale(canvas, 1, NAN, 0));
  assert(!sk_canvas_scale(canvas, 1, 0, INFINITY));
  assert(!sk_canvas_scale(canvas, FLT_MAX, FLT_MAX, 0));
  assert(!sk_canvas_scale(canvas, FLT_MAX, 0, 0));
  sk_canvas_draw_rect(canvas, 6, 6, 4, 4, paint);
  check_box(surface, 4, 4, 12, 12, 255);
  sk_canvas_restore(canvas);
  assert(!sk_canvas_scale(NULL, 1, 0, 0));

  uint8_t image_pixels[8 * 4];
  for (int i = 0; i < 8; i++) {
    image_pixels[i * 4] = i < 4 ? 255 : 0;
    image_pixels[i * 4 + 1] = 0;
    image_pixels[i * 4 + 2] = i < 4 ? 0 : 255;
    image_pixels[i * 4 + 3] = 255;
  }
  SkImage *image = sk_image_make_rgba(image_pixels, 8, 1);
  assert(image);
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_save(canvas);
  sk_canvas_clip_rect(canvas, 16, 16, 32, 32);
  assert(sk_canvas_scale(canvas, 2, 0, 0));
  sk_canvas_draw_image(canvas, image, 0, 0, 32, 32, .5f);
  sk_canvas_restore(canvas);
  const uint8_t *data = pixels(surface);
  for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
    const uint8_t *p = data + (y * 64 + x) * 4;
    int inside = x >= 16 && x < 48 && y >= 16 && y < 48;
    assert(abs(p[3] - (inside ? 128 : 0)) <= 1);
    if (inside && x <= 25) assert(p[0] >= 127 && p[2] == 0);
    if (inside && x >= 38) assert(p[0] == 0 && p[2] >= 127);
    if (inside && x == 30) { assert(abs(p[0] - 88) <= 2); assert(abs(p[2] - 40) <= 2); }
    if (inside && x == 33) { assert(abs(p[0] - 40) <= 2); assert(abs(p[2] - 88) <= 2); }
  }

  /* Compare a text transform with the same glyphs at twice the size. */
  for (int mode = 0; mode < 3; mode++) {
    uint8_t original[64 * 64 * 4];
    sk_paint_set_text_size(paint, 14);
    int style = mode ? SK_TEXT_MONO : 0;
    const char *text = mode ? "éЖ" : "AB";
    SkTextRange range = sk_text_range(text, strlen(text));
    float advance = sk_font_measure_span(range, 14, style);
    sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
    if (mode == 2) sk_canvas_draw_mono_string(canvas, text, 2, 24, paint);
    else sk_canvas_draw_span(canvas, range, 2, 24, paint, style);
    memcpy(original, pixels(surface), sizeof original);
    if (mode == 2) {
      sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
      sk_canvas_draw_span(canvas, range, 2, 24, paint, SK_TEXT_MONO);
      data = pixels(surface);
      for (size_t i = 0; i < sizeof original; i++) assert(abs(data[i] - original[i]) <= 1);
    }
    sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
    sk_canvas_save(canvas);
    assert(sk_canvas_scale(canvas, 2, 0, 0));
    assert(sk_font_measure_span(range, 14, style) == advance);
    if (mode) assert(fabsf(sk_font_measure_span(range, 28, style) - 2 * advance) < .001f);
    if (mode == 2) sk_canvas_draw_mono_string(canvas, text, 2, 24, paint);
    else sk_canvas_draw_span(canvas, range, 2, 24, paint, style);
    sk_canvas_restore(canvas);
    data = pixels(surface);
    SkSurface *reference = sk_surface_make_raster_n32_premul(64, 64);
    assert(reference);
    SkCanvas *reference_canvas = sk_surface_get_canvas(reference);
    sk_canvas_clear(reference_canvas, sk_color_rgba(0, 0, 0, 0));
    sk_paint_set_text_size(paint, 28);
    if (mode == 2) sk_canvas_draw_mono_string(reference_canvas, text, 4, 48, paint);
    else sk_canvas_draw_span(reference_canvas, range, 4, 48, paint, style);
    const uint8_t *expected = pixels(reference);
    int before = 0, after = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
      const uint8_t *p = data + (y * 64 + x) * 4;
      const uint8_t *q = expected + (y * 64 + x) * 4;
      for (int channel = 0; channel < 4; channel++) assert(abs(p[channel] - q[channel]) <= 1);
      if (original[(y * 64 + x) * 4 + 3]) before++;
      if (p[3]) { after++; assert(y >= 20 && y < 52 && x < 64); }
    }
    assert(before > 20 && after > before * 2);
    sk_surface_unref(reference);
    sk_paint_set_text_size(paint, 14);
  }

  /* Zero scale excludes paint. Clear still uses the device clip. */
  sk_canvas_save(canvas);
  assert(sk_canvas_scale(canvas, 0, 8, 8));
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_draw_rect(canvas, 0, 0, 64, 64, paint);
  sk_canvas_draw_string(canvas, "AB", 2, 24, paint);
  sk_canvas_draw_image(canvas, image, 0, 0, 32, 32, 1);
  check_box(surface, 0, 0, 0, 0, 0);
  sk_canvas_restore(canvas);

  /* Large scales keep painting bounded by the visible clip. */
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_save(canvas);
  sk_canvas_clip_rect(canvas, 8, 8, 12, 12);
  assert(sk_canvas_scale(canvas, (float)INT_MAX, 0, 0));
  sk_canvas_draw_rect(canvas, -1, -1, 2, 2, paint);
  check_box(surface, 8, 8, 20, 20, 255);
  sk_canvas_draw_string(canvas, "AB", (float)INT_MAX, (float)INT_MAX, paint);
  sk_canvas_draw_image(canvas, image, -1, -1, 2, 2, 1);
  data = pixels(surface);
  for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++)
    assert(data[(y * 64 + x) * 4 + 3] == (x >= 8 && x < 20 && y >= 8 && y < 20 ? 255 : 0));
  sk_canvas_restore(canvas);
  /* The visible stem of a large glyph fills only the clip. */
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_save(canvas);
  sk_canvas_clip_rect(canvas, 8, 8, 12, 12);
  assert(sk_canvas_scale(canvas, (float)INT_MAX, 0, 0));
  sk_canvas_draw_string(canvas, "H", -2, 5, paint);
  check_box(surface, 8, 8, 20, 20, 255);
  sk_canvas_restore(canvas);
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_draw_rect(canvas, 6, 6, 4, 4, paint);
  check_box(surface, 6, 6, 10, 10, 255);

  sk_image_unref(image);
  sk_paint_delete(paint);
  sk_surface_unref(surface);
}

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

  /* Translucent SrcOver keeps premultiplied alpha: a 50% fill over a
   * transparent clear stays at alpha 128. It must not force opaque. A
   * second fill grows the alpha. It must not stay at 255. */
  {
    unsigned a1;
    sk_paint_set_color(paint, sk_color_rgba(200, 100, 50, 128));
    sk_canvas_draw_rect(canvas, 0, 0, 4, 4, paint);
    px = sk_surface_peek_pixels(surf, &px_len);
    assert(px && px_len == 32 * 32 * 4);
    a1 = px[(1 * 32 + 1) * 4 + 3];
    assert(a1 == 128);
    assert(px[(1 * 32 + 1) * 4] >= 99 && px[(1 * 32 + 1) * 4] <= 102);
    sk_canvas_draw_rect(canvas, 0, 0, 4, 4, paint);
    px = sk_surface_peek_pixels(surf, &px_len);
    assert(px[(1 * 32 + 1) * 4 + 3] > a1);
    assert(px[(1 * 32 + 1) * 4 + 3] < 255);
  }

  /* Large coordinates fill only the current clip. */
  {
    const float sizes[] = {(float)INT_MAX, (float)INT_MAX * 2.f};
    for (int i = 0; i < 2; i++) {
      float origin = i ? -sizes[i] * .5f : 0.f;
      sk_canvas_clear(canvas, sk_color_rgba(20, 40, 80, 255));
      sk_canvas_save(canvas);
      sk_canvas_clip_rect(canvas, origin, origin, sizes[i], sizes[i]);
      sk_canvas_clip_rect(canvas, 8, 8, 12, 12);
      sk_paint_set_color(paint, sk_color_rgba(240, 240, 240, 255));
      sk_canvas_draw_rect(canvas, origin, origin, sizes[i], sizes[i], paint);
      sk_canvas_restore(canvas);
      px = sk_surface_peek_pixels(surf, &px_len);
      assert(px && px_len == 32 * 32 * 4);
      for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        const uint8_t *pixel = px + (y * 32 + x) * 4;
        int inside = x >= 8 && x < 20 && y >= 8 && y < 20;
        assert(pixel[0] == (inside ? 240 : 20));
        assert(pixel[1] == (inside ? 240 : 40));
        assert(pixel[2] == (inside ? 240 : 80));
        assert(pixel[3] == 255);
      }
    }
  }

  sk_paint_delete(paint);
  sk_surface_unref(surf);
  check_scale(0);
  const char *backend = getenv("SCUZZ_SKIA");
  if (backend && !strcmp(backend, "gpu")) check_scale(1);
  puts("ffi-skia clip and scale tests ok");
  return 0;
}
