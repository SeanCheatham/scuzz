/* Thin Skia-shaped C ABI. Default is the pinned CPU prebuilt
 * (third_party/skia/PIN). SCUZZ_SKIA=sk_sw uses in-tree software.
 * Callers depend only on this header.
 */
#ifndef SK_CAPI_H
#define SK_CAPI_H

#include <stddef.h>
#include "sk_text.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SkSurface SkSurface;
typedef struct SkCanvas SkCanvas;
typedef struct SkPaint SkPaint;
typedef struct SkImage SkImage;

typedef struct SkColor {
  uint8_t r, g, b, a;
} SkColor;

SkColor sk_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
SkColor sk_color_argb(uint32_t argb);

/* Offscreen RGBA8888 surface (CPU). */
SkSurface *sk_surface_make_raster_n32_premul(int width, int height);
/* Offscreen GPU raster surface. Draw commands run on the GPU.
 * Peek reads the framebuffer. There is no CPU paint buffer.
 * Returns NULL when OpenGL is missing. */
SkSurface *sk_surface_make_gpu_n32_premul(int width, int height);
int sk_gpu_available(void);
void sk_surface_unref(SkSurface *surface);
SkCanvas *sk_surface_get_canvas(SkSurface *surface);
int sk_surface_width(const SkSurface *surface);
int sk_surface_height(const SkSurface *surface);
/* Row-major RGBA bytes; valid until surface is unref'd or redrawn. */
const uint8_t *sk_surface_peek_pixels(const SkSurface *surface, size_t *out_size);

/* Decode PNG or JPEG bytes. The caller owns the returned image.
 * The error points to static text. Decode limits apply before allocation. */
SkImage *sk_image_decode(const uint8_t *bytes, size_t len, const char **error);
/* Copy premultiplied RGBA bytes. */
SkImage *sk_image_make_rgba(const uint8_t *pixels, int width, int height);
void sk_image_unref(SkImage *image);
int sk_image_width(const SkImage *image);
int sk_image_height(const SkImage *image);
/* Use linear sampling and source-over alpha. Respect the canvas clip. */
void sk_canvas_draw_image(SkCanvas *canvas, const SkImage *image, float x,
                          float y, float w, float h, float opacity);

void sk_canvas_clear(SkCanvas *canvas, SkColor color);
void sk_canvas_draw_rect(SkCanvas *canvas, float x, float y, float w, float h,
                         const SkPaint *paint);
/* Baseline-left text. Uses paint text size (default 8). */
void sk_canvas_draw_string(SkCanvas *canvas, const char *text, float x, float y,
                           const SkPaint *paint);
void sk_canvas_save(SkCanvas *canvas);
/* Paint into a transparent layer. Restore composites the layer once.
 * Return 0 on allocation failure. A failed call leaves the stack unchanged. */
int sk_canvas_save_layer_alpha(SkCanvas *canvas, uint8_t alpha);
void sk_canvas_restore(SkCanvas *canvas);
/* Compose a nonnegative uniform scale about this point. Save and restore
 * include the scale. Return 0 for invalid or nonfinite matrix values. */
int sk_canvas_scale(SkCanvas *canvas, float factor, float pivot_x, float pivot_y);
/* Intersect the current clip with this rect. */
void sk_canvas_clip_rect(SkCanvas *canvas, float x, float y, float w, float h);

SkPaint *sk_paint_new(void);
void sk_paint_delete(SkPaint *paint);
void sk_paint_set_color(SkPaint *paint, SkColor color);
void sk_paint_set_text_size(SkPaint *paint, float size);
float sk_paint_get_text_size(const SkPaint *paint);

enum { SK_TEXT_BOLD = 1, SK_TEXT_ITALIC = 2, SK_TEXT_MONO = 4, SK_TEXT_UNDERLINE = 8 };
/* Store positions for the selected bytes. Allocate end - start + 1 entries. */
void sk_font_text_positions(SkTextRange text, float font_px, int style, float *leading, float *trailing);
/* Mark source byte offsets at shaped cluster boundaries. Allocate bytes + 1. */
void sk_text_cluster_breaks(const char *text, size_t bytes, uint8_t *breaks);
void sk_font_metrics(float font_px, int style, float *ascent, float *descent);
/* Return source text bounds above and below the baseline. */
void sk_font_span_metrics(SkTextRange text, float font_px, int style, float *ascent, float *descent);
float sk_font_measure_span(SkTextRange text, float font_px, int style);
void sk_canvas_draw_span(SkCanvas *canvas, SkTextRange text,
                         float x, float y, const SkPaint *paint, int style);

/* Advance width of UTF-8 text at font_px. sk_sw is monospace. One advance
 * per code point. advance = round(font_px), min 1. Draw and measure use the
 * same advance. */
float sk_font_measure_string(const char *text, float font_px);
/* Advance of UTF-8 text on the monospace face. Editor cells use this so I
 * and W share one column without padding I to a proportional @ width. */
float sk_font_measure_string_mono(const char *text, float font_px);
void sk_canvas_draw_string_mono(SkCanvas *canvas, const char *text, float x,
                                float y, const SkPaint *paint);
/* Monospace cell = measure_string_mono("0"). Measure/draw use that grid so
 * editor caret columns match every presenter. View.text stays proportional. */
float sk_font_mono_cell(float font_px);
void sk_canvas_draw_mono_string(SkCanvas *canvas, const char *text, float x,
                                float y, const SkPaint *paint);

/* Encode surface pixels as PNG into freshly allocated buffer (caller frees). */
int sk_encode_png(const SkSurface *surface, uint8_t **out_bytes, size_t *out_len);
int sk_encode_png_to_file(const SkSurface *surface, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* SK_CAPI_H */
