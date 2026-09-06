/* Internal ABI between sk_capi_skia.cpp (Skia backend) and
 * sk_capi_skia_bridge.c (sk_capi.h wrappers). Opaque void * keeps Sk* types
 * out of the C TU. Both TUs include this header so the compiler checks that
 * definitions match declarations. */
#ifndef SK_CAPI_SKIA_H
#define SK_CAPI_SKIA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* SK_CAPI_SKIA_H */
