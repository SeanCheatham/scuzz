#include "sk_capi.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  SkSurface *cpu;
  SkSurface *gpu;
  SkCanvas *c;
  SkPaint *paint;
  const uint8_t *a;
  const uint8_t *b;
  size_t na = 0;
  size_t nb = 0;

  if (!sk_gpu_available()) {
    fputs("missing OpenGL — install mesa (libegl1-mesa-dev libgles2-mesa-dev "
          "libgl1-mesa-dri)\n",
          stderr);
    return 1;
  }
  cpu = sk_surface_make_raster_n32_premul(16, 8);
  gpu = sk_surface_make_gpu_n32_premul(16, 8);
  assert(cpu && gpu);
  paint = sk_paint_new();
  sk_paint_set_color(paint, sk_color_rgba(240, 10, 10, 255));
  c = sk_surface_get_canvas(cpu);
  sk_canvas_clear(c, sk_color_rgba(0, 0, 0, 255));
  sk_canvas_draw_rect(c, 0, 0, 8, 8, paint);
  c = sk_surface_get_canvas(gpu);
  sk_canvas_clear(c, sk_color_rgba(0, 0, 0, 255));
  sk_canvas_draw_rect(c, 0, 0, 8, 8, paint);
  a = sk_surface_peek_pixels(cpu, &na);
  b = sk_surface_peek_pixels(gpu, &nb);
  assert(a && b && na == nb);
  assert(memcmp(a, b, na) == 0);
  sk_paint_delete(paint);
  sk_surface_unref(cpu);
  sk_surface_unref(gpu);
  puts("ffi-skia gpu presenter ok");
  return 0;
}
