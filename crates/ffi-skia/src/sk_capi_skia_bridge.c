/* C wrappers for sk_capi_skia.cpp. Opaque Sk* stays out of the C++ TU. */
#include "sk_capi.h"

void *scuzz_skia_surface_make(int width, int height);
void scuzz_skia_surface_unref(void *surface);
void *scuzz_skia_surface_get_canvas(void *surface);
int scuzz_skia_surface_width(const void *surface);
int scuzz_skia_surface_height(const void *surface);
const uint8_t *scuzz_skia_surface_peek_pixels(const void *surface,
                                              size_t *out_size);
void scuzz_skia_canvas_clear(void *canvas, uint8_t r, uint8_t g, uint8_t b,
                             uint8_t a);
void scuzz_skia_canvas_draw_rect(void *canvas, float x, float y, float w,
                                 float h, const void *paint);
void scuzz_skia_canvas_draw_string(void *canvas, const char *text, float x,
                                   float y, const void *paint);
void scuzz_skia_canvas_save(void *canvas);
void scuzz_skia_canvas_restore(void *canvas);
void scuzz_skia_canvas_clip_rect(void *canvas, float x, float y, float w,
                                 float h);
void *scuzz_skia_paint_new(void);
void scuzz_skia_paint_delete(void *paint);
void scuzz_skia_paint_set_color(void *paint, uint8_t r, uint8_t g, uint8_t b,
                                uint8_t a);
void scuzz_skia_paint_set_stroke(void *paint, int stroke);
void scuzz_skia_paint_set_stroke_width(void *paint, float width);
void scuzz_skia_paint_set_text_size(void *paint, float size);
float scuzz_skia_paint_get_text_size(const void *paint);
float scuzz_skia_font_measure_string(const char *text, float font_px);
int scuzz_skia_encode_png(const void *surface, uint8_t **out_bytes,
                          size_t *out_len);
int scuzz_skia_encode_png_to_file(const void *surface, const char *path);

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
