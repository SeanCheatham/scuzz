/* C wrappers for sk_capi_skia.cpp. Opaque Sk* stays out of the C++ TU.
 * Declarations come from the shared header so drift fails at compile time. */
#include "sk_capi.h"
#include "sk_capi_skia.h"

SkSurface *sk_surface_make_raster_n32_premul(int width, int height) {
  return (SkSurface *)scuzz_skia_surface_make(width, height);
}

void sk_surface_unref(SkSurface *surface) {
  scuzz_skia_surface_unref(surface);
}

SkCanvas *sk_surface_get_canvas(SkSurface *surface) {
  return (SkCanvas *)scuzz_skia_surface_get_canvas(surface);
}

int sk_surface_width(const SkSurface *surface) {
  return scuzz_skia_surface_width(surface);
}

int sk_surface_height(const SkSurface *surface) {
  return scuzz_skia_surface_height(surface);
}

const uint8_t *sk_surface_peek_pixels(const SkSurface *surface,
                                      size_t *out_size) {
  return scuzz_skia_surface_peek_pixels(surface, out_size);
}

void sk_canvas_clear(SkCanvas *canvas, SkColor color) {
  scuzz_skia_canvas_clear(canvas, color.r, color.g, color.b, color.a);
}

void sk_canvas_draw_rect(SkCanvas *canvas, float x, float y, float w, float h,
                         const SkPaint *paint) {
  scuzz_skia_canvas_draw_rect(canvas, x, y, w, h, paint);
}

void sk_canvas_draw_string(SkCanvas *canvas, const char *text, float x, float y,
                           const SkPaint *paint) {
  scuzz_skia_canvas_draw_string(canvas, text, x, y, paint);
}

void sk_canvas_save(SkCanvas *canvas) { scuzz_skia_canvas_save(canvas); }

void sk_canvas_restore(SkCanvas *canvas) { scuzz_skia_canvas_restore(canvas); }

void sk_canvas_clip_rect(SkCanvas *canvas, float x, float y, float w, float h) {
  scuzz_skia_canvas_clip_rect(canvas, x, y, w, h);
}

SkPaint *sk_paint_new(void) { return (SkPaint *)scuzz_skia_paint_new(); }

void sk_paint_delete(SkPaint *paint) { scuzz_skia_paint_delete(paint); }

void sk_paint_set_color(SkPaint *paint, SkColor color) {
  scuzz_skia_paint_set_color(paint, color.r, color.g, color.b, color.a);
}

void sk_paint_set_stroke(SkPaint *paint, int stroke) {
  scuzz_skia_paint_set_stroke(paint, stroke);
}

void sk_paint_set_stroke_width(SkPaint *paint, float width) {
  scuzz_skia_paint_set_stroke_width(paint, width);
}

void sk_paint_set_text_size(SkPaint *paint, float size) {
  scuzz_skia_paint_set_text_size(paint, size);
}

float sk_paint_get_text_size(const SkPaint *paint) {
  return scuzz_skia_paint_get_text_size(paint);
}

float sk_font_measure_string(const char *text, float font_px) {
  return scuzz_skia_font_measure_string(text, font_px);
}

int sk_encode_png(const SkSurface *surface, uint8_t **out_bytes,
                  size_t *out_len) {
  return scuzz_skia_encode_png(surface, out_bytes, out_len);
}

int sk_encode_png_to_file(const SkSurface *surface, const char *path) {
  return scuzz_skia_encode_png_to_file(surface, path);
}
