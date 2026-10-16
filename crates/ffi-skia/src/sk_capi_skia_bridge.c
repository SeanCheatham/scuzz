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

int sk_canvas_save_layer_alpha(SkCanvas *canvas, uint8_t alpha) {
  return scuzz_skia_canvas_save_layer_alpha(canvas, alpha);
}

void sk_canvas_restore(SkCanvas *canvas) { scuzz_skia_canvas_restore(canvas); }

int sk_canvas_scale(SkCanvas *canvas, float factor, float pivot_x, float pivot_y) {
  return scuzz_skia_canvas_scale(canvas, factor, pivot_x, pivot_y);
}

void sk_canvas_clip_rect(SkCanvas *canvas, float x, float y, float w, float h) {
  scuzz_skia_canvas_clip_rect(canvas, x, y, w, h);
}

SkPaint *sk_paint_new(void) { return (SkPaint *)scuzz_skia_paint_new(); }

void sk_paint_delete(SkPaint *paint) { scuzz_skia_paint_delete(paint); }

void sk_paint_set_color(SkPaint *paint, SkColor color) {
  scuzz_skia_paint_set_color(paint, color.r, color.g, color.b, color.a);
}

void sk_paint_set_text_size(SkPaint *paint, float size) {
  scuzz_skia_paint_set_text_size(paint, size);
}

float sk_paint_get_text_size(const SkPaint *paint) {
  return scuzz_skia_paint_get_text_size(paint);
}

void sk_font_text_positions(SkTextRange text, float font_px, int style, float *leading, float *trailing) {
  scuzz_skia_font_text_positions(text, font_px, style, leading, trailing);
}

void sk_text_cluster_breaks(const char *text, size_t bytes, uint8_t *breaks) {
  scuzz_skia_text_cluster_breaks(text, bytes, breaks);
}

void sk_font_metrics(float font_px, int style, float *ascent, float *descent) {
  scuzz_skia_font_metrics(font_px, style, ascent, descent);
}

void sk_font_span_metrics(SkTextRange text, float font_px, int style, float *ascent, float *descent) {
  scuzz_skia_font_span_metrics(text, font_px, style, ascent, descent);
}

float sk_font_measure_span(SkTextRange text, float font_px, int style) {
  return scuzz_skia_font_measure_span(text, font_px, style);
}

void sk_canvas_draw_span(SkCanvas *canvas, SkTextRange text,
                         float x, float y, const SkPaint *paint, int style) {
  scuzz_skia_canvas_draw_span(canvas, text, x, y, paint, style);
}

float sk_font_measure_string(const char *text, float font_px) {
  return scuzz_skia_font_measure_string(text, font_px);
}

float sk_font_measure_string_mono(const char *text, float font_px) {
  return scuzz_skia_font_measure_string_mono(text, font_px);
}

void sk_canvas_draw_string_mono(SkCanvas *canvas, const char *text, float x,
                                float y, const SkPaint *paint) {
  scuzz_skia_canvas_draw_string_mono(canvas, text, x, y, paint);
}

int sk_encode_png(const SkSurface *surface, uint8_t **out_bytes,
                  size_t *out_len) {
  return scuzz_skia_encode_png(surface, out_bytes, out_len);
}

int sk_encode_png_to_file(const SkSurface *surface, const char *path) {
  return scuzz_skia_encode_png_to_file(surface, path);
}

SkImage *sk_image_make_rgba(const uint8_t *pixels, int width, int height) {
  return (SkImage *)scuzz_skia_image_make(pixels, width, height);
}
void sk_image_unref(SkImage *image) { scuzz_skia_image_unref(image); }
int sk_image_width(const SkImage *image) { return scuzz_skia_image_width(image); }
int sk_image_height(const SkImage *image) { return scuzz_skia_image_height(image); }
void sk_canvas_draw_image(SkCanvas *canvas, const SkImage *image, float x,
                          float y, float w, float h, float opacity) {
  scuzz_skia_canvas_draw_image(canvas, image, x, y, w, h, opacity);
}
