/* Thin Skia-shaped C ABI. Default is the pinned CPU prebuilt
 * (third_party/skia/PIN). SCUZZ_SKIA=sk_sw uses in-tree software.
 * Callers depend only on this header.
 */
#ifndef SK_CAPI_H
#define SK_CAPI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SkSurface SkSurface;
typedef struct SkCanvas SkCanvas;
typedef struct SkPaint SkPaint;

typedef struct SkColor {
  uint8_t r, g, b, a;
} SkColor;

SkColor sk_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
SkColor sk_color_argb(uint32_t argb);

/* Offscreen RGBA8888 surface (CPU). */
SkSurface *sk_surface_make_raster_n32_premul(int width, int height);
/* Offscreen GPU presenter. Paint stays CPU; peek uploads and reads back.
 * Returns NULL when OpenGL is missing. */
SkSurface *sk_surface_make_gpu_n32_premul(int width, int height);
int sk_gpu_available(void);
void sk_surface_unref(SkSurface *surface);
SkCanvas *sk_surface_get_canvas(SkSurface *surface);
int sk_surface_width(const SkSurface *surface);
int sk_surface_height(const SkSurface *surface);
/* Row-major RGBA bytes; valid until surface is unref'd or redrawn. */
const uint8_t *sk_surface_peek_pixels(const SkSurface *surface, size_t *out_size);

void sk_canvas_clear(SkCanvas *canvas, SkColor color);
void sk_canvas_draw_rect(SkCanvas *canvas, float x, float y, float w, float h,
                         const SkPaint *paint);
/* Baseline-left text. Uses paint text size (default 8). */
void sk_canvas_draw_string(SkCanvas *canvas, const char *text, float x, float y,
                           const SkPaint *paint);
void sk_canvas_save(SkCanvas *canvas);
void sk_canvas_restore(SkCanvas *canvas);
/* Intersect the current clip with this rect. */
void sk_canvas_clip_rect(SkCanvas *canvas, float x, float y, float w, float h);

SkPaint *sk_paint_new(void);
void sk_paint_delete(SkPaint *paint);
void sk_paint_set_color(SkPaint *paint, SkColor color);
void sk_paint_set_stroke(SkPaint *paint, int stroke /* bool */);
void sk_paint_set_stroke_width(SkPaint *paint, float width);
void sk_paint_set_text_size(SkPaint *paint, float size);
float sk_paint_get_text_size(const SkPaint *paint);

/* Advance width of UTF-8 text at font_px. sk_sw is monospace. One advance
 * per code point. advance = round(font_px), min 1. Draw and measure use the
 * same advance. */
float sk_font_measure_string(const char *text, float font_px);
/* Monospace cell = max(measure("M"), measure("W")). Measure/draw use that
 * grid so editor caret columns match every presenter. View.text stays proportional. */
float sk_font_mono_cell(float font_px);
float sk_font_measure_mono_string(const char *text, float font_px);
void sk_canvas_draw_mono_string(SkCanvas *canvas, const char *text, float x,
                                float y, const SkPaint *paint);

/* Encode surface pixels as PNG into freshly allocated buffer (caller frees). */
int sk_encode_png(const SkSurface *surface, uint8_t **out_bytes, size_t *out_len);
int sk_encode_png_to_file(const SkSurface *surface, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* SK_CAPI_H */
