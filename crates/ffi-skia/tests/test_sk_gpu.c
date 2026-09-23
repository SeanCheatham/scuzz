#include "sk_capi.h"

#include <stdio.h>
#include <string.h>

extern unsigned sk_sw_cpu_paint_ops(void);

static int max_delta(const uint8_t *a, const uint8_t *b, size_t n) {
  size_t i;
  int m = 0;
  for (i = 0; i < n; i++) {
    int d = (int)a[i] - (int)b[i];
    if (d < 0)
      d = -d;
    if (d > m)
      m = d;
  }
  return m;
}

static void paint_scene(SkSurface *s) {
  SkCanvas *c = sk_surface_get_canvas(s);
  SkPaint *p = sk_paint_new();
  sk_canvas_clear(c, sk_color_rgba(0, 0, 0, 255));
  sk_paint_set_color(p, sk_color_rgba(240, 10, 10, 255));
  sk_canvas_draw_rect(c, 0, 0, 16, 16, p);
  sk_canvas_save(c);
  sk_canvas_clip_rect(c, 4, 4, 8, 8);
  sk_paint_set_color(p, sk_color_rgba(10, 10, 240, 255));
  sk_canvas_draw_rect(c, 0, 0, 16, 16, p);
  sk_canvas_restore(c);
  sk_paint_set_color(p, sk_color_rgba(240, 240, 240, 255));
  sk_paint_set_text_size(p, 14.f);
  sk_canvas_draw_string(c, "A", 18, 20, p);
  sk_paint_set_color(p, sk_color_rgba(200, 100, 50, 128));
  sk_canvas_draw_rect(c, 0, 18, 8, 6, p);
  sk_paint_delete(p);
}

static int near(const uint8_t *px, int i, int r, int g, int b, int a) {
  int dr = (int)px[i] - r;
  int dg = (int)px[i + 1] - g;
  int db = (int)px[i + 2] - b;
  int da = (int)px[i + 3] - a;
  if (dr < 0)
    dr = -dr;
  if (dg < 0)
    dg = -dg;
  if (db < 0)
    db = -db;
  if (da < 0)
    da = -da;
  return dr <= 2 && dg <= 2 && db <= 2 && da <= 2;
}

int main(void) {
  SkSurface *cpu;
  SkSurface *gpu;
  SkSurface *gpu2;
  const uint8_t *a;
  const uint8_t *b;
  const uint8_t *c;
  size_t na = 0;
  size_t nb = 0;
  size_t nc = 0;
  unsigned ops;
  int delta;
  const int w = 40;
  const int h = 32;

  if (!sk_gpu_available()) {
    fputs("missing OpenGL — install mesa (libegl1-mesa-dev libgles2-mesa-dev "
          "libgl1-mesa-dri)\n",
          stderr);
    return 1;
  }
  cpu = sk_surface_make_raster_n32_premul(w, h);
  gpu = sk_surface_make_gpu_n32_premul(w, h);
  gpu2 = sk_surface_make_gpu_n32_premul(w, h);
  if (!cpu || !gpu || !gpu2) {
    fputs("gpu surface missing\n", stderr);
    return 1;
  }
  ops = sk_sw_cpu_paint_ops();
  paint_scene(gpu);
  paint_scene(gpu2);
  if (sk_sw_cpu_paint_ops() != ops) {
    fputs("gpu raster used a CPU paint pass\n", stderr);
    return 1;
  }
  paint_scene(cpu);
  if (sk_sw_cpu_paint_ops() == ops) {
    fputs("cpu paint counter did not move\n", stderr);
    return 1;
  }
  a = sk_surface_peek_pixels(cpu, &na);
  b = sk_surface_peek_pixels(gpu, &nb);
  c = sk_surface_peek_pixels(gpu2, &nc);
  if (!a || !b || !c || na != nb || nb != nc || na != (size_t)w * (size_t)h * 4u) {
    fputs("gpu readback missing\n", stderr);
    return 1;
  }
  delta = max_delta(a, b, na);
  if (delta > 2) {
    fprintf(stderr, "gpu raster delta %d exceeds 2\n", delta);
    return 1;
  }
  if (max_delta(b, c, nb) != 0) {
    fputs("gpu raster is not stable across repeats\n", stderr);
    return 1;
  }
  if (!near(b, (1 * w + 1) * 4, 240, 10, 10, 255) ||
      !near(b, (6 * w + 6) * 4, 10, 10, 240, 255) ||
      !near(b, (10 * w + 1) * 4, 240, 10, 10, 255)) {
    fputs("gpu raster pixels do not match the scene\n", stderr);
    return 1;
  }
  sk_surface_unref(cpu);
  sk_surface_unref(gpu);
  sk_surface_unref(gpu2);
  puts("ffi-skia gpu raster ok");
  return 0;
}
