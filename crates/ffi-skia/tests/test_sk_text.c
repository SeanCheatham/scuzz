#include "sk_capi.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  assert(argc == 2);
  const char *sources[] = {"AV fi é — Proportional text", "السلام عليكم", "אב AB 12",
                          "नमस्ते क्षि", "สวัสดี ปู่", "中文漢字", "é Ω Ж λ ≤ →"};
  const char *backend = getenv("SCUZZ_SKIA");
  int gpu = backend && !strcmp(backend, "gpu");
  SkSurface *surface = gpu ? sk_surface_make_gpu_n32_premul(600, 168 * 48)
                           : sk_surface_make_raster_n32_premul(600, 168 * 48);
  assert(surface);
  SkCanvas *canvas = sk_surface_get_canvas(surface);
  SkPaint *paint = sk_paint_new();
  assert(canvas && paint);
  sk_canvas_clear(canvas, sk_color_rgba(250, 250, 250, 255));
  sk_paint_set_color(paint, sk_color_rgba(25, 35, 45, 255));
  puts("[");
  int row = 0;
  const float sizes[] = {16.f, 16.25f, 32.f};
  for (size_t size_index = 0; size_index < sizeof sizes / sizeof sizes[0]; size_index++) for (int style = 0; style < 8; style++) {
    float size = sizes[size_index];
    sk_paint_set_text_size(paint, size);
    for (size_t i = 0; i < sizeof sources / sizeof sources[0]; i++, row++) {
      size_t bytes = strlen(sources[i]);
      SkTextRange range = sk_text_range(sources[i], bytes);
      float *positions = calloc(2 * (bytes + 1), sizeof(float));
      uint8_t *breaks = calloc(bytes + 1, 1);
      assert(positions && breaks);
      float ascent, descent;
      sk_font_text_positions(range, (float)size, style, positions, positions + bytes + 1);
      sk_text_cluster_breaks(sources[i], bytes, breaks);
      sk_font_span_metrics(range, (float)size, style, &ascent, &descent);
      printf("%s{\"width\":%.9g,\"ascent\":%.9g,\"descent\":%.9g,\"positions\":[",
             row ? "," : "", sk_font_measure_span(range, (float)size, style), ascent, descent);
      for (size_t n = 0; n < 2 * (bytes + 1); n++) printf("%s%.9g", n ? "," : "", positions[n]);
      printf("],\"breaks\":[");
      for (size_t n = 0; n <= bytes; n++) printf("%s%u", n ? "," : "", breaks[n]);
      puts("]}");
      sk_canvas_draw_span(canvas, range, 20, (float)(row * 48 + 36), paint, style);
      free(positions);
      free(breaks);
    }
  }
  puts("]");
  assert(row == 168 && sk_encode_png_to_file(surface, argv[1]));
  sk_paint_delete(paint);
  sk_surface_unref(surface);
  return 0;
}
