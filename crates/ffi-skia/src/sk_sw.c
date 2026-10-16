#include "sk_capi.h"
#include <limits.h>
#include <float.h>
#include "png_enc.h"
#include "sk_gpu.h"
#include "sk_utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <SheenBidi/SheenBidi.h>
#include "arabic_joining.h"
#include <math.h>
#endif

struct SkImage {
  int width, height;
  uint8_t *pixels;
  void *gpu_image;
};

struct SkPaint {
  SkColor color;
  float text_size;
};

typedef struct {
  int x0;
  int y0;
  int x1;
  int y1;
  uint8_t *pixels;
  SkGpuTarget *target;
  uint8_t alpha;
  int layer;
  float scale, tx, ty;
} SwSave;

struct SkCanvas {
  struct SkSurface *surface;
  int clip_x0;
  int clip_y0;
  int clip_x1;
  int clip_y1;
  float scale, tx, ty;
  SwSave *saves; /* heap; grows on demand */
  int save_n;
  int save_cap;
  /* save() calls that failed to allocate. restore() eats them so the pair
   * count stays balanced. The clip stays as it was. */
  int save_dropped;
};

struct SkSurface {
  int width;
  int height;
  int gpu;
  uint8_t *pixels;     /* RGBA8888 CPU raster. NULL on a GPU surface. */
  SkGpuTarget *target; /* GPU framebuffer when gpu != 0 */
  SkCanvas canvas;     /* borrowed through sk_surface_get_canvas */
};

static unsigned cpu_paint_ops;

unsigned sk_sw_cpu_paint_ops(void) { return cpu_paint_ops; }

SkSurface *sk_surface_make_raster_n32_premul(int width, int height) {
  SkSurface *s;
  if (width <= 0 || height <= 0)
    return NULL;
  s = (SkSurface *)calloc(1, sizeof(SkSurface));
  if (!s)
    return NULL;
  s->width = width;
  s->height = height;
  s->pixels = (uint8_t *)calloc((size_t)width * (size_t)height, 4);
  if (!s->pixels) {
    free(s);
    return NULL;
  }
  s->canvas.surface = s;
  s->canvas.scale = 1.f;
  s->canvas.clip_x0 = 0;
  s->canvas.clip_y0 = 0;
  s->canvas.clip_x1 = width;
  s->canvas.clip_y1 = height;
  return s;
}

SkSurface *sk_surface_make_gpu_n32_premul(int width, int height) {
  SkSurface *s;
  SkGpuTarget *target;
  if (width <= 0 || height <= 0)
    return NULL;
  if (!sk_gpu_available())
    return NULL;
  target = sk_gpu_target_new(width, height);
  if (!target)
    return NULL;
  s = (SkSurface *)calloc(1, sizeof(SkSurface));
  if (!s) {
    sk_gpu_target_free(target);
    return NULL;
  }
  s->width = width;
  s->height = height;
  s->gpu = 1;
  s->target = target;
  s->canvas.surface = s;
  s->canvas.scale = 1.f;
  s->canvas.clip_x0 = 0;
  s->canvas.clip_y0 = 0;
  s->canvas.clip_x1 = width;
  s->canvas.clip_y1 = height;
  return s;
}

void sk_surface_unref(SkSurface *surface) {
  if (!surface)
    return;
  while (surface->canvas.save_n > 0 || surface->canvas.save_dropped > 0)
    sk_canvas_restore(&surface->canvas);
  if (surface->gpu)
    sk_gpu_target_free(surface->target);
  free(surface->canvas.saves);
  free(surface->pixels);
  free(surface);
}

SkCanvas *sk_surface_get_canvas(SkSurface *surface) {
  return surface ? &surface->canvas : NULL;
}

int sk_surface_width(const SkSurface *surface) {
  return surface ? surface->width : 0;
}

int sk_surface_height(const SkSurface *surface) {
  return surface ? surface->height : 0;
}

const uint8_t *sk_surface_peek_pixels(const SkSurface *surface, size_t *out_size) {
  size_t n;
  if (!surface) {
    if (out_size)
      *out_size = 0;
    return NULL;
  }
  if (surface->gpu)
    return sk_gpu_read(surface->target, out_size);
  n = (size_t)surface->width * (size_t)surface->height * 4;
  if (out_size)
    *out_size = n;
  return surface->pixels;
}

static void put_pixel(SkSurface *s, int x, int y, SkColor color) {
  uint8_t *p;
  if (!s || x < 0 || y < 0 || x >= s->width || y >= s->height)
    return;
  if (x < s->canvas.clip_x0 || y < s->canvas.clip_y0 || x >= s->canvas.clip_x1 ||
      y >= s->canvas.clip_y1)
    return;
  p = s->pixels + ((size_t)y * (size_t)s->width + (size_t)x) * 4;
  if (color.a == 255) {
    p[0] = color.r;
    p[1] = color.g;
    p[2] = color.b;
    p[3] = 255;
  } else if (color.a == 0) {
    return;
  } else {
    uint8_t ia = (uint8_t)(255 - color.a);
    p[0] = (uint8_t)((color.r * color.a + p[0] * ia) / 255);
    p[1] = (uint8_t)((color.g * color.a + p[1] * ia) / 255);
    p[2] = (uint8_t)((color.b * color.a + p[2] * ia) / 255);
    /* SrcOver alpha: src_a + dst_a * (1 - src_a). Do not force opaque. A
     * translucent draw over a transparent clear keeps real alpha, like the
     * premul Skia backend. */
    p[3] = (uint8_t)(color.a + (p[3] * ia) / 255);
  }
}

/* Clear replaces pixels (Src mode), like SkCanvas::clear. It does not
 * blend. A clear to transparent erases the surface. Respects the clip.
 * The surface is premul, so a translucent clear stores r*a/255. */
void sk_canvas_clear(SkCanvas *canvas, SkColor color) {
  SkSurface *s;
  int x, y, x0, y0, x1, y1;
  uint8_t pr = color.r;
  uint8_t pg = color.g;
  uint8_t pb = color.b;
  if (!canvas || !canvas->surface)
    return;
  if (canvas->surface->gpu) {
    sk_gpu_clear(canvas->surface->target, canvas->clip_x0, canvas->clip_y0,
                 canvas->clip_x1, canvas->clip_y1, color.r, color.g, color.b,
                 color.a);
    return;
  }
  cpu_paint_ops++;
  if (color.a < 255) {
    pr = (uint8_t)((color.r * color.a) / 255);
    pg = (uint8_t)((color.g * color.a) / 255);
    pb = (uint8_t)((color.b * color.a) / 255);
  }
  s = canvas->surface;
  x0 = canvas->clip_x0 > 0 ? canvas->clip_x0 : 0;
  y0 = canvas->clip_y0 > 0 ? canvas->clip_y0 : 0;
  x1 = canvas->clip_x1 < s->width ? canvas->clip_x1 : s->width;
  y1 = canvas->clip_y1 < s->height ? canvas->clip_y1 : s->height;
  for (y = y0; y < y1; y++) {
    uint8_t *row = s->pixels + (size_t)y * (size_t)s->width * 4;
    for (x = x0; x < x1; x++) {
      uint8_t *p = row + (size_t)x * 4;
      p[0] = pr;
      p[1] = pg;
      p[2] = pb;
      p[3] = color.a;
    }
  }
}

/* Floor negative coordinates. Clamp before the integer conversion. */
static int sw_floor_int(double v) {
  if (!(v > (double)INT_MIN)) return INT_MIN;
  if (v >= (double)INT_MAX) return INT_MAX;
  int i = (int)v;
  return (double)i > v ? i - 1 : i;
}

static void sw_draw_rect(SkCanvas *canvas, double x, double y, double w, double h,
                         const SkPaint *paint) {
  int x0, y0, x1, y1, ix, iy;
  if (!canvas || !canvas->surface || !paint || !(w > 0) || !(h > 0) || !(canvas->scale > 0))
    return;
  x0 = sw_floor_int((double)x * canvas->scale + canvas->tx);
  y0 = sw_floor_int((double)y * canvas->scale + canvas->ty);
  x1 = sw_floor_int(((double)x + w) * canvas->scale + canvas->tx);
  y1 = sw_floor_int(((double)y + h) * canvas->scale + canvas->ty);
  if (x0 < canvas->clip_x0) x0 = canvas->clip_x0;
  if (y0 < canvas->clip_y0) y0 = canvas->clip_y0;
  if (x1 > canvas->clip_x1) x1 = canvas->clip_x1;
  if (y1 > canvas->clip_y1) y1 = canvas->clip_y1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > canvas->surface->width) x1 = canvas->surface->width;
  if (y1 > canvas->surface->height) y1 = canvas->surface->height;
  if (x0 >= x1 || y0 >= y1) return;
  if (canvas->surface->gpu) {
    sk_gpu_fill_rect(canvas->surface->target, x0, y0, x1, y1, canvas->clip_x0,
                     canvas->clip_y0, canvas->clip_x1, canvas->clip_y1,
                     paint->color.r, paint->color.g, paint->color.b,
                     paint->color.a);
    return;
  }
  cpu_paint_ops++;
  for (iy = y0; iy < y1; iy++)
    for (ix = x0; ix < x1; ix++)
      put_pixel(canvas->surface, ix, iy, paint->color);
}

void sk_canvas_draw_rect(SkCanvas *canvas, float x, float y, float w, float h,
                         const SkPaint *paint) {
  sw_draw_rect(canvas, x, y, w, h, paint);
}

int sk_canvas_scale(SkCanvas *canvas, float factor, float pivot_x, float pivot_y) {
  if (!canvas || canvas->save_dropped || !(factor >= 0.f) || !isfinite(factor) ||
      !isfinite(pivot_x) || !isfinite(pivot_y)) return 0;
  double local_x = (double)pivot_x * (1. - factor);
  double local_y = (double)pivot_y * (1. - factor);
  double scale = (double)canvas->scale * factor;
  double tx = canvas->tx + canvas->scale * local_x;
  double ty = canvas->ty + canvas->scale * local_y;
  if (fabs(local_x) > FLT_MAX || fabs(local_y) > FLT_MAX ||
      scale > FLT_MAX || fabs(tx) > FLT_MAX || fabs(ty) > FLT_MAX) return 0;
  canvas->scale = (float)scale;
  canvas->tx = (float)tx;
  canvas->ty = (float)ty;
  return 1;
}

static int sw_save(SkCanvas *canvas) {
  if (!canvas) return 0;
  if (canvas->save_n >= canvas->save_cap) {
    int cap = canvas->save_cap ? canvas->save_cap * 2 : 16;
    SwSave *s = realloc(canvas->saves, (size_t)cap * sizeof *s);
    if (!s) return 0;
    canvas->saves = s;
    canvas->save_cap = cap;
  }
  SwSave *top = &canvas->saves[canvas->save_n++];
  *top = (SwSave){.x0 = canvas->clip_x0, .y0 = canvas->clip_y0,
    .x1 = canvas->clip_x1, .y1 = canvas->clip_y1,
    .scale = canvas->scale, .tx = canvas->tx, .ty = canvas->ty};
  return 1;
}

void sk_canvas_save(SkCanvas *canvas) {
  if (canvas && !sw_save(canvas)) canvas->save_dropped++;
}

int sk_canvas_save_layer_alpha(SkCanvas *canvas, uint8_t alpha) {
  if (!canvas || !canvas->surface || canvas->save_dropped) return 0;
  SkSurface *s = canvas->surface;
  uint8_t *pixels = NULL;
  SkGpuTarget *target = NULL;
  if (s->gpu) target = sk_gpu_target_new(s->width, s->height);
  else pixels = calloc((size_t)s->width * (size_t)s->height, 4);
  if ((!pixels && !target) || !sw_save(canvas)) {
    free(pixels); sk_gpu_target_free(target); return 0;
  }
  SwSave *top = &canvas->saves[canvas->save_n - 1];
  top->layer = 1; top->alpha = alpha;
  top->pixels = s->pixels; top->target = s->target;
  s->pixels = pixels; s->target = target;
  return 1;
}

void sk_canvas_restore(SkCanvas *canvas) {
  if (!canvas) return;
  if (canvas->save_dropped > 0) { canvas->save_dropped--; return; }
  if (canvas->save_n <= 0) return;
  SwSave top = canvas->saves[--canvas->save_n];
  canvas->clip_x0 = top.x0; canvas->clip_y0 = top.y0;
  canvas->clip_x1 = top.x1; canvas->clip_y1 = top.y1;
  canvas->scale = top.scale; canvas->tx = top.tx; canvas->ty = top.ty;
  if (!top.layer) return;
  SkSurface *s = canvas->surface;
  uint8_t *pixels = s->pixels;
  SkGpuTarget *target = s->target;
  s->pixels = top.pixels; s->target = top.target;
  if (s->gpu) {
    sk_gpu_draw_target(s->target, target, top.alpha / 255.f,
      top.x0, top.y0, top.x1, top.y1);
    sk_gpu_target_free(target);
  } else {
    for (int y = top.y0; y < top.y1; y++) {
      for (int x = top.x0; x < top.x1; x++) {
        size_t offset = ((size_t)y * s->width + x) * 4;
        const uint8_t *src = pixels + offset;
        uint8_t *dst = s->pixels + offset;
        unsigned alpha = (src[3] * top.alpha + 127u) / 255u;
        for (int c = 0; c < 4; c++)
          dst[c] = (uint8_t)((src[c] * top.alpha + 127u) / 255u +
            (dst[c] * (255u - alpha) + 127u) / 255u);
      }
    }
    free(pixels);
  }
}

void sk_canvas_clip_rect(SkCanvas *canvas, float x, float y, float w, float h) {
  int x0, y0, x1, y1;
  if (!canvas)
    return;
  if (!(w > 0.f) || !(h > 0.f) || !(canvas->scale > 0.f)) {
    canvas->clip_x1 = canvas->clip_x0;
    canvas->clip_y1 = canvas->clip_y0;
    return;
  }
  x0 = sw_floor_int((double)x * canvas->scale + canvas->tx);
  y0 = sw_floor_int((double)y * canvas->scale + canvas->ty);
  x1 = sw_floor_int(((double)x + w) * canvas->scale + canvas->tx);
  y1 = sw_floor_int(((double)y + h) * canvas->scale + canvas->ty);
  if (x0 > canvas->clip_x0)
    canvas->clip_x0 = x0;
  if (y0 > canvas->clip_y0)
    canvas->clip_y0 = y0;
  if (x1 < canvas->clip_x1)
    canvas->clip_x1 = x1;
  if (y1 < canvas->clip_y1)
    canvas->clip_y1 = y1;
}

/* 8x8 ASCII font - printable 32..126; bits LSB = left. */
const uint8_t sk_font8[95][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},
    {0x7F, 0x06, 0x06, 0x3E, 0x06, 0x06, 0x7F, 0x00},
    {0x7F, 0x06, 0x06, 0x3E, 0x06, 0x06, 0x06, 0x00},
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
    {0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x7F, 0x00},
    {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00},
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},
    {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00},
    {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00},
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

#ifdef __EMSCRIPTEN__
/* Use the same browser font for text measurement and rasterization. */
EM_JS(int, web_text_direction, (const char *text, int bytes), {
  const source = UTF8ToString(Number(text), bytes);
  let ascii = true;
  for (let i = 0; i < source.length; i++) if (source.charCodeAt(i) > 127) { ascii = false; break; }
  if (ascii) return 0;
  const cache = Module.scuzzDirections || (Module.scuzzDirections = new Map());
  if (cache.has(source)) return cache.get(source);
  let probe = Module.scuzzDirectionProbe;
  if (!probe) {
    probe = Module.scuzzDirectionProbe = document.createElement('span');
    probe.dir = 'auto'; probe.setAttribute('aria-hidden', 'true');
    Object.assign(probe.style, {position: 'fixed', visibility: 'hidden', width: '0', height: '0', overflow: 'hidden'});
    document.body.append(probe);
  }
  probe.textContent = source;
  const rtl = Number(getComputedStyle(probe).direction === 'rtl');
  if (source.length <= 256) {
    if (cache.size >= 128) cache.delete(cache.keys().next().value);
    cache.set(source, rtl);
  }
  return rtl;
});

EM_JS(float, web_text_width, (const char *text, int bytes, float size, int style, int rtl, int force, int joins, int arabic), {
  const scale = Module.scuzzTextScale || 1;
  const canvas = Module.scuzzTextCanvas || (Module.scuzzTextCanvas = document.createElement('canvas'));
  const context = canvas.getContext('2d', {willReadFrequently: true});
  context.direction = rtl ? 'rtl' : 'ltr'; context.textAlign = 'left';
  context.font = ((style & 2) ? "italic " : "") + ((style & 1) ? "bold " : "") + size / scale + 'px ' + (arabic ? '"Arabic context"' : Module.scuzzFontStack);
  const source = UTF8ToString(Number(text), bytes);
  Module.loadTextFonts(source);
  Module.scuzzRunSource ||= source => Array.from(source).filter(point => {
    const cp = point.codePointAt(0);
    return !(cp >= 0x202a && cp <= 0x202e || cp >= 0x2066 && cp <= 0x2069);
  }).join("");
  /* CGJ keeps edge joiners in the browser shaping run under an override. */
  Module.scuzzShapingSource ||= (source, rtl, joins) => {
    const clean = Module.scuzzRunSource(source);
    const fence = joins || clean.includes("\u200d") ? "\u034f" : "";
    const wrap = text => "\u202e" + fence + text + fence + "\u202c";
    if (rtl) return wrap(((joins & 1) ? "\u200d" : "") + clean + ((joins & 2) ? "\u200d" : ""));
    const segmenter = Module.scuzzSegmenter || (Module.scuzzSegmenter = new Intl.Segmenter('und', {granularity: 'grapheme'}));
    const parts = [];
    for (const {segment} of segmenter.segment(clean)) {
      const arabic = /\\p{Script_Extensions=Arabic}/u.test(segment);
      if (parts.length && parts.at(-1).arabic === arabic) parts.at(-1).text.push(segment);
      else parts.push({arabic, text: [segment]});
    }
    /* Shape Arabic in visual order. Keep other scripts in the LTR run. */
    return "\u202d" + parts.map((part, i) => part.arabic ? wrap(
      ((i === parts.length - 1 && (joins & 2)) ? "\u200d" : "") + part.text.reverse().join("") +
      ((i === 0 && (joins & 1)) ? "\u200d" : "")) : part.text.join("")).join("") + "\u202c";
  };
  const shaped = force ? Module.scuzzShapingSource(source, rtl, joins) : source;
  return context.measureText(shaped).width * scale;
});

EM_JS(void, web_text_pixels, (const char *text, float size, int style, int width, int height, float x, float y, uint8_t *pixels, int rtl, int force, int joins, int arabic), {
  const scale = Module.scuzzTextScale || 1;
  const canvas = Module.scuzzTextCanvas;
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext('2d', {willReadFrequently: true});
  context.direction = rtl ? 'rtl' : 'ltr'; context.textAlign = 'left';
  context.font = ((style & 2) ? "italic " : "") + ((style & 1) ? "bold " : "") + size / scale + 'px ' + (arabic ? '"Arabic context"' : Module.scuzzFontStack);
  context.setTransform(scale, 0, 0, scale, 0, 0);
  context.fillStyle = '#ffffff';
  const source = UTF8ToString(Number(text));
  const shaped = force ? Module.scuzzShapingSource(source, rtl, joins) : source;
  context.fillText(shaped, x / scale, y / scale);
  HEAPU8.set(context.getImageData(0, 0, width, height).data, Number(pixels));
});
#endif

#ifdef __EMSCRIPTEN__
EM_JS(void, web_span_metrics, (const char *text, int bytes, float size, int style, int rtl, int force, int joins, int arabic, float *out), {
  const scale = Module.scuzzTextScale || 1;
  const canvas = Module.scuzzTextCanvas || (Module.scuzzTextCanvas = document.createElement('canvas'));
  const context = canvas.getContext('2d');
  context.direction = rtl ? 'rtl' : 'ltr'; context.textAlign = 'left';
  context.font = ((style & 2) ? "italic " : "") + ((style & 1) ? "bold " : "") + size / scale + 'px ' + (arabic ? '"Arabic context"' : Module.scuzzFontStack);
  const source = UTF8ToString(Number(text), bytes);
  const shaped = force ? Module.scuzzShapingSource(source, rtl, joins) : source;
  const metrics = context.measureText(shaped);
  HEAPF32[Number(out) / 4] = Math.max(0, metrics.actualBoundingBoxAscent) * scale;
  HEAPF32[Number(out) / 4 + 1] = Math.max(0, metrics.actualBoundingBoxDescent) * scale;
});
#endif

#ifdef __EMSCRIPTEN__
static unsigned web_joining_type(SBCodepoint cp) {
  unsigned type = sk_joining_type(cp);
  if (type != JOINING_TYPE_X) return type;
  SBGeneralCategory category = SBCodepointGetGeneralCategory(cp);
  return category == SBGeneralCategoryMN || category == SBGeneralCategoryME || category == SBGeneralCategoryCF ?
    JOINING_TYPE_T : JOINING_TYPE_U;
}

static int web_arabic_joiner(SBCodepoint cp) {
  return SBCodepointGetScript(cp) == SBScriptARAB || cp == 0x200d || cp == 0x640;
}

static int web_join_boundary(SkTextRange range, size_t boundary, int before, int rtl) {
  SBCodepointSequence sequence = {SBStringEncodingUTF8, (void *)range.source, range.bytes};
  SBCodepoint left = 0, right = 0;
  unsigned left_type = JOINING_TYPE_U, right_type = JOINING_TYPE_U;
  size_t left_at = range.line_start, right_at = range.line_end;
  for (SBUInteger at = range.line_start; at < range.line_end;) {
    size_t start = at;
    SBCodepoint cp = SBCodepointSequenceGetCodepointAt(&sequence, &at);
    unsigned type = web_joining_type(cp);
    if (type == JOINING_TYPE_T) continue;
    if (start < boundary) { left = cp; left_type = type; left_at = start; }
    else { right = cp; right_type = type; right_at = start; break; }
  }
  if (before ? right_at >= range.end : left_at < range.start) return 0;
  if (!rtl) { unsigned type = left_type; left_type = right_type; right_type = type; }
  return web_arabic_joiner(left) && web_arabic_joiner(right) &&
    (left_type == JOINING_TYPE_L || left_type == JOINING_TYPE_D) &&
    (right_type == JOINING_TYPE_R || right_type == JOINING_TYPE_D);
}

static int web_join_context(SkTextRange range, size_t start, size_t end, int rtl) {
  int before = start == range.start && start > range.line_start && web_join_boundary(range, start, 1, rtl);
  int after = end == range.end && end < range.line_end && web_join_boundary(range, end, 0, rtl);
  return before | (after << 1);
}

static int web_arabic_font(SBCodepoint cp) {
  SBGeneralCategory category = SBCodepointGetGeneralCategory(cp);
  return SBCodepointGetScript(cp) == SBScriptARAB || (cp >= 0x600 && cp <= 0x6ff) ||
    cp == 0x640 || cp == 0x200c || cp == 0x200d || category == SBGeneralCategoryMN ||
    category == SBGeneralCategoryME || category == SBGeneralCategoryCF;
}

typedef struct WebTextRun { size_t start, end; int rtl, joins, arabic; float x, width; } WebTextRun;
typedef struct WebTextLayout { WebTextRun *runs; size_t count; float width; } WebTextLayout;

static WebTextLayout web_text_layout(SkTextRange range, float size, int style) {
  WebTextLayout layout = {0};
  if (!range.source || range.start >= range.end || !sk_text_valid(range) || range.bytes > INT_MAX) return layout;
  SBCodepointSequence sequence = {SBStringEncodingUTF8, (void *)range.source, range.bytes};
  SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
  if (!algorithm) return layout;
  int failed = 0;
  for (SBUInteger offset = 0; offset < range.line_end;) {
    SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, offset, range.bytes - offset, SBLevelDefaultLTR);
    if (!paragraph) { failed = 1; break; }
    SBUInteger length = SBParagraphGetLength(paragraph);
    SBUInteger start = offset > range.line_start ? offset : range.line_start;
    SBUInteger end = offset + length < range.line_end ? offset + length : range.line_end;
    SBLineRef line = end > start ? SBParagraphCreateLine(paragraph, start, end - start) : NULL;
    if (line) {
      size_t count = SBLineGetRunCount(line);
      const SBRun *runs = SBLineGetRunsPtr(line);
      SBScriptLocatorRef locator = SBScriptLocatorCreate();
      if (!locator) failed = 1;
      for (size_t i = 0; i < count && !failed; i++) {
        size_t first = layout.count;
        SBScriptLocatorLoadCodepoints(locator, &sequence);
        while (SBScriptLocatorMoveNext(locator)) {
          const SBScriptAgent *script = SBScriptLocatorGetAgent(locator);
          size_t start = runs[i].offset > range.start ? runs[i].offset : range.start;
          size_t end = runs[i].offset + runs[i].length < range.end ? runs[i].offset + runs[i].length : range.end;
          if (start < script->offset) start = script->offset;
          if (end > script->offset + script->length) end = script->offset + script->length;
          if (start >= end) continue;
          for (SBUInteger part = start; part < end;) {
            SBUInteger at = part;
            SBCodepoint cp = SBCodepointSequenceGetCodepointAt(&sequence, &at);
            int arabic = script->script == SBScriptARAB && web_arabic_font(cp);
            /* Keep isolate controls as shaping boundaries. */
            int isolate = cp >= 0x2066 && cp <= 0x2069;
            while (at < end && !isolate) {
              SBUInteger next = at;
              SBCodepoint cp = SBCodepointSequenceGetCodepointAt(&sequence, &next);
              int font = script->script == SBScriptARAB && web_arabic_font(cp);
              if (font != arabic || (cp >= 0x2066 && cp <= 0x2069)) break;
              at = next;
            }
            WebTextRun *grown = layout.count < SIZE_MAX / sizeof(WebTextRun) ?
              realloc(layout.runs, (layout.count + 1) * sizeof(WebTextRun)) : NULL;
            if (!grown) { failed = 1; break; }
            layout.runs = grown;
            SkTextRange context = range;
            context.line_start = runs[i].offset;
            context.line_end = runs[i].offset + runs[i].length;
            layout.runs[layout.count++] = (WebTextRun){part, at,
              runs[i].level & 1, web_join_context(context, part, at, runs[i].level & 1), arabic, 0.f, 0.f};
            part = at;
          }
        }
        if (runs[i].level & 1) for (size_t j = 0; j < (layout.count - first) / 2; j++) {
          WebTextRun swap = layout.runs[first + j];
          layout.runs[first + j] = layout.runs[layout.count - 1 - j];
          layout.runs[layout.count - 1 - j] = swap;
        }
        for (size_t j = first; j < layout.count; j++) {
          WebTextRun *run = &layout.runs[j];
          run->x = layout.width;
          run->width = web_text_width(range.source + run->start, (int)(run->end - run->start), size, style, run->rtl, 1, run->joins, run->arabic);
          layout.width += run->width;
        }
      }
      if (locator) SBScriptLocatorRelease(locator);
      SBLineRelease(line);
    }
    SBParagraphRelease(paragraph);
    if (!length || failed) break;
    offset += length;
  }
  SBAlgorithmRelease(algorithm);
  if (failed) { free(layout.runs); return (WebTextLayout){0}; }
  return layout;
}
#endif

/* Cell advance. Draw and measure use the same advance so caret columns
 * match the pixels at any size. */
static int sw_advance(float font_px) {
  int a = (int)(font_px + 0.5f);
  return a < 1 ? 1 : a;
}

float sk_font_measure_string(const char *text, float font_px) {
#ifdef __EMSCRIPTEN__
  return text ? web_text_width(text, (int)strlen(text), font_px > 0.f ? font_px : 8.f, 0, web_text_direction(text, (int)strlen(text)), 0, 0, 0) : 0.f;
#else
  const char *p;
  int n = 0;
  float px = font_px > 0.f ? font_px : 8.f;
  if (!text)
    return 0.f;
  for (p = text; *p;) {
    int clen = sk_utf8_clen(p);
    if (clen < 1)
      clen = 1;
    p += clen;
    n++;
  }
  return (float)n * (float)sw_advance(px);
#endif
}

static void draw_styled_string(SkCanvas *canvas, const char *text, float x, float y,
                                const SkPaint *paint, int style, int direction, int joins, int arabic) {
#ifndef __EMSCRIPTEN__
  (void)direction; (void)joins; (void)arabic;
#endif
  float size, scale;
  int advance, baseline, row, col, sr, sc;
  double cx;
  const char *p;
  if (!canvas || !canvas->surface || !paint || !text || !(canvas->scale > 0.f) || !isfinite(x) || !isfinite(y))
    return;
  if (canvas->surface->gpu) {
    sk_gpu_draw_text(canvas->surface->target, text, x, y, paint->text_size,
                     paint->color.r, paint->color.g, paint->color.b,
                     paint->color.a, canvas->clip_x0, canvas->clip_y0,
                     canvas->clip_x1, canvas->clip_y1, style,
                     canvas->scale, canvas->tx, canvas->ty);
    return;
  }
  cpu_paint_ops++;
  size = paint->text_size > 0.f ? paint->text_size : 8.f;
#ifdef __EMSCRIPTEN__
  {
    int rtl = direction < 0 ? web_text_direction(text, (int)strlen(text)) : direction;
    int force = direction >= 0;
    float advance_width = web_text_width(text, (int)strlen(text), size, style, rtl, force, joins, arabic);
    float metrics[2];
    web_span_metrics(text, (int)strlen(text), size, style, rtl, force, joins, arabic, metrics);
    float ascent = metrics[0] * canvas->scale, descent = metrics[1] * canvas->scale;
    advance_width *= canvas->scale;
    x = (float)((double)x * canvas->scale + canvas->tx);
    y = (float)((double)y * canvas->scale + canvas->ty);
    size *= canvas->scale;
    if (!isfinite(size) || !isfinite(x) || !isfinite(y)) return;
    int left = (int)fmaxf(canvas->clip_x0, fminf(canvas->clip_x1, floorf(x - size)));
    int right = (int)fmaxf(canvas->clip_x0, fminf(canvas->clip_x1, ceilf(x + advance_width + size)));
    /* Keep antialias coverage outside the geometric glyph bounds. */
    int top = (int)fmaxf(canvas->clip_y0, fminf(canvas->clip_y1, floorf(y - ascent) - 1.f));
    int bottom = (int)fmaxf(canvas->clip_y0, fminf(canvas->clip_y1, ceilf(y + descent) + 1.f));
    int width = right - left, height = bottom - top;
    if (width < 1 || height < 1) return;
    uint8_t *pixels = malloc((size_t)width * height * 4);
    if (!pixels) return;
    web_text_pixels(text, size, style, width, height, x - left, y - top, pixels, rtl, force, joins, arabic);
    for (int row = 0; row < height; row++) {
      for (int col = 0; col < width; col++) {
        SkColor color = paint->color;
        color.a = (uint8_t)((unsigned)color.a * pixels[(row * width + col) * 4 + 3] / 255);
        put_pixel(canvas->surface, left + col, top + row, color);
      }
    }
    free(pixels);
    return;
  }
#endif
  scale = size / 8.f;
  advance = sw_advance(size);
  baseline = (int)(size - 1.f + 0.5f);
  if (baseline < 0)
    baseline = 0;
  cx = floor((double)x);
  for (p = text; *p;) {
    int clen = sk_utf8_clen(p);
    unsigned char ch = (unsigned char)*p;
    const uint8_t *glyph;
    double gy = floor((double)y) - baseline;
    if (clen < 1)
      clen = 1;
    if (ch < 32 || ch > 126)
      ch = '?';
    glyph = sk_font8[ch - 32];
    if (scale >= 0.999f && scale <= 1.001f) {
      for (row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (col = 0; col < 8; col++) {
          /* Font bytes are LSB-left in this table. */
          if (bits & (uint8_t)(1u << col)) {
            double gx = cx + col + ((style & 2) ? (7 - row) / 4 : 0);
            sw_draw_rect(canvas, gx, gy + row, 1, 1, paint);
            if (style & 1) sw_draw_rect(canvas, gx + 1, gy + row, 1, 1, paint);
          }
        }
      }
    } else {
      for (sr = 0; sr < advance; sr++) {
        row = (int)((float)sr / scale);
        if (row > 7)
          row = 7;
        for (sc = 0; sc < advance; sc++) {
          col = (int)((float)sc / scale);
          if (col > 7)
            col = 7;
          if (glyph[row] & (uint8_t)(1u << col)) {
            double gx = cx + sc + ((style & 2) ? (advance - sr) / 5 : 0);
            sw_draw_rect(canvas, gx, gy + sr, 1, 1, paint);
            if (style & 1) sw_draw_rect(canvas, gx + 1, gy + sr, 1, 1, paint);
          }
        }
      }
    }
    cx += advance;
    p += clen;
  }
}

void sk_canvas_draw_string(SkCanvas *canvas, const char *text, float x, float y,
                           const SkPaint *paint) {
  draw_styled_string(canvas, text, x, y, paint, 0, -1, 0, 0);
}

#ifdef __EMSCRIPTEN__
EM_JS(void, web_font_metrics, (float size, int style, float *out), {
  const scale = Module.scuzzTextScale || 1;
  const canvas = Module.scuzzTextCanvas || (Module.scuzzTextCanvas = document.createElement('canvas'));
  const context = canvas.getContext('2d', {willReadFrequently: true});
  context.font = ((style & 2) ? "italic " : "") + ((style & 1) ? "bold " : "") + size / scale + 'px ' + Module.scuzzFontStack;
  const metrics = context.measureText('Mg');
  HEAPF32[Number(out) / 4] = (metrics.fontBoundingBoxAscent ?? size / scale) * scale;
  HEAPF32[Number(out) / 4 + 1] = (metrics.fontBoundingBoxDescent ?? size / scale * 0.25) * scale;
});
#endif

#ifdef __EMSCRIPTEN__
EM_JS(void, web_cluster_breaks, (const char *text, int bytes, uint8_t *breaks), {
  const source = UTF8ToString(Number(text), bytes);
  Module.loadTextFonts(source);
  const segmenter = Module.scuzzSegmenter || (Module.scuzzSegmenter = new Intl.Segmenter('und', {granularity: 'grapheme'}));
  const encoder = new TextEncoder();
  let offset = 0;
  for (const {segment} of segmenter.segment(source)) {
    HEAPU8[Number(breaks) + offset] = 1;
    offset += encoder.encode(segment).length;
  }
  HEAPU8[Number(breaks) + bytes] = 1;
});
#endif

#ifdef __EMSCRIPTEN__
EM_JS(void, web_text_positions, (const char *text, int bytes, float size, int style, int rtl, int joins, int arabic, float advance, float *leading, float *trailing), {
  const scale = Module.scuzzTextScale || 1;
  const source = UTF8ToString(Number(text), bytes);
  let probe = Module.scuzzPositionProbe;
  if (!probe) {
    probe = Module.scuzzPositionProbe = document.createElement('span');
    probe.setAttribute('aria-hidden', 'true');
    Object.assign(probe.style, {position: 'fixed', left: '0', top: '0', visibility: 'hidden', whiteSpace: 'pre', pointerEvents: 'none', unicodeBidi: 'bidi-override'});
    document.body.append(probe);
  }
  probe.style.font = ((style & 2) ? "italic " : "") + ((style & 1) ? "bold " : "") + size / scale + 'px ' + (arabic ? '"Arabic context"' : Module.scuzzFontStack); probe.dir = rtl ? "rtl" : "ltr"; probe.textContent = ((joins & 1) ? "\u200d" : "") + Module.scuzzRunSource(source) + ((joins & 2) ? "\u200d" : "");
  const bounds = probe.getBoundingClientRect(), origin = bounds.left;
  const factor = bounds.width > 0 ? advance / bounds.width : 1;
  const range = document.createRange(), node = probe.firstChild;
  const caret = unit => {
    if (unit === ((joins & 1) ? 1 : 0)) return rtl ? advance : 0;
    if (unit === node.length - ((joins & 2) ? 1 : 0)) return rtl ? 0 : advance;
    range.setStart(node, unit); range.collapse(true);
    return (range.getBoundingClientRect().left - origin) * factor;
  };
  const segmenter = Module.scuzzSegmenter || (Module.scuzzSegmenter = new Intl.Segmenter('und', {granularity: 'grapheme'}));
  const encoder = new TextEncoder();
  let byte = 0, unit = (joins & 1) ? 1 : 0, last = rtl ? advance : 0;
  for (const {segment} of segmenter.segment(source)) {
    const display = Module.scuzzRunSource(segment);
    let a = last, b = last;
    if (node && display.length) {
      a = caret(unit);
      unit += display.length;
      b = caret(unit);
    }
    const length = encoder.encode(segment).length;
    for (let i = 0; i < length; i++) {
      HEAPF32[Number(leading) / 4 + byte + i] = a;
      HEAPF32[Number(trailing) / 4 + byte + i] = b;
    }
    byte += length; last = b;
  }
});
#endif

void sk_font_text_positions(SkTextRange range, float font_px, int style, float *leading, float *trailing) {
  if (!sk_text_valid(range)) return;
  const char *text = range.source ? range.source + range.start : NULL;
  size_t bytes = range.end - range.start;
  if (!leading || !trailing || bytes > INT32_MAX) return;
  memset(leading, 0, (bytes + 1) * sizeof(float));
  memset(trailing, 0, (bytes + 1) * sizeof(float));
  if (!text) return;
#ifdef __EMSCRIPTEN__
  WebTextLayout layout = web_text_layout(range, font_px, style);
  for (size_t i = 0; i < layout.count; i++) {
    const WebTextRun *run = &layout.runs[i];
    size_t start = run->start - range.start, end = run->end - range.start;
    web_text_positions(range.source + run->start, (int)(run->end - run->start), font_px, style, run->rtl, run->joins, run->arabic, run->width,
      leading + start, trailing + start);
    for (size_t byte = start; byte < end; byte++) { leading[byte] += run->x; trailing[byte] += run->x; }
  }
  if (bytes) leading[bytes] = trailing[bytes] = trailing[bytes - 1];
  free(layout.runs);
#else
  (void)style;
  float x = 0.f, advance = (float)sw_advance(font_px);
  for (size_t i = 0; i < bytes;) {
    unsigned char c = (unsigned char)text[i];
    size_t length = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
    if (length > bytes - i) length = 1;
    for (size_t j = 0; j < length; j++) { leading[i + j] = x; trailing[i + j] = x + advance; }
    x += advance; i += length;
  }
  leading[bytes] = trailing[bytes] = x;
#endif
}

void sk_text_cluster_breaks(const char *text, size_t bytes, uint8_t *breaks) {
  if (!breaks || bytes == SIZE_MAX) return;
  memset(breaks, 0, bytes + 1);
  breaks[0] = breaks[bytes] = 1;
  if (!text) return;
#ifdef __EMSCRIPTEN__
  if (bytes <= INT32_MAX) web_cluster_breaks(text, (int)bytes, breaks);
#else
  for (size_t i = 0; i < bytes;) {
    unsigned char c = (unsigned char)text[i];
    size_t length = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
    if (length > bytes - i) length = 1;
    breaks[i] = 1;
    i += length;
  }
#endif
}

void sk_font_metrics(float font_px, int style, float *ascent, float *descent) {
  if (font_px <= 0.f) font_px = 8.f;
#ifdef __EMSCRIPTEN__
  float metrics[2];
  web_font_metrics(font_px, style, metrics);
  if (ascent) *ascent = metrics[0];
  if (descent) *descent = metrics[1];
#else
  (void)style;
  if (ascent) *ascent = font_px;
  if (descent) *descent = 1.f;
#endif
}



void sk_font_span_metrics(SkTextRange range, float font_px, int style, float *ascent, float *descent) {
  if (!sk_text_valid(range)) return;
  const char *text = range.source ? range.source + range.start : NULL;
  size_t bytes = range.end - range.start;
  float metrics[2];
  sk_font_metrics(font_px, style, &metrics[0], &metrics[1]);
#ifdef __EMSCRIPTEN__
  (void)text; (void)bytes;
  WebTextLayout layout = web_text_layout(range, font_px, style);
  metrics[0] = metrics[1] = 0.f;
  for (size_t i = 0; i < layout.count; i++) {
    const WebTextRun *run = &layout.runs[i];
    float bounds[2];
    web_span_metrics(range.source + run->start, (int)(run->end - run->start), font_px > 0.f ? font_px : 8.f,
      style, run->rtl, 1, run->joins, run->arabic, bounds);
    metrics[0] = fmaxf(metrics[0], bounds[0]); metrics[1] = fmaxf(metrics[1], bounds[1]);
  }
  free(layout.runs);
#else
  (void)text; (void)bytes;
#endif
  if (ascent) *ascent = metrics[0];
  if (descent) *descent = metrics[1];
}

float sk_font_measure_span(SkTextRange range, float font_px, int style) {
  if (!sk_text_valid(range)) return 0.f;
  const char *text = range.source ? range.source + range.start : NULL;
  size_t bytes = range.end - range.start;
  if (!text) return 0.f;
  if (font_px <= 0.f) font_px = 8.f;
#ifdef __EMSCRIPTEN__
  (void)bytes;
  WebTextLayout layout = web_text_layout(range, font_px, style);
  float width = layout.width;
  free(layout.runs);
  return width;
#else
  (void)style;
  size_t i = 0, count = 0;
  while (i < bytes && text[i]) {
    int n = sk_utf8_clen(text + i);
    if (n < 1 || (size_t)n > bytes - i) n = 1;
    i += (size_t)n;
    count++;
  }
  return (float)count * (float)sw_advance(font_px);
#endif
}

void sk_canvas_draw_span(SkCanvas *canvas, SkTextRange range,
                         float x, float y, const SkPaint *paint, int style) {
  if (!sk_text_valid(range)) return;
  const char *text = range.source ? range.source + range.start : NULL;
  size_t bytes = range.end - range.start;
  if (!text || bytes == SIZE_MAX || !paint) return;
#ifdef __EMSCRIPTEN__
  WebTextLayout layout = web_text_layout(range, paint->text_size, style);
  for (size_t i = 0; i < layout.count; i++) {
    const WebTextRun *run = &layout.runs[i];
    size_t length = run->end - run->start;
    char *copy = malloc(length + 1);
    if (!copy) continue;
    memcpy(copy, range.source + run->start, length); copy[length] = 0;
    draw_styled_string(canvas, copy, x + run->x, y, paint, style, run->rtl, run->joins, run->arabic);
    free(copy);
  }
  free(layout.runs);
#else
  char *copy = malloc(bytes + 1);
  if (!copy) return;
  memcpy(copy, text, bytes); copy[bytes] = 0;
  draw_styled_string(canvas, copy, x, y, paint, style, -1, 0, 0);
  free(copy);
#endif
  if (style & SK_TEXT_UNDERLINE) {
    float width = sk_font_measure_span(range, paint->text_size, style);
    sk_canvas_draw_rect(canvas, x, y + paint->text_size * 0.1f, width,
                         fmaxf(1.f, paint->text_size / 16.f), paint);
  }
}

SkPaint *sk_paint_new(void) {
  SkPaint *p = (SkPaint *)calloc(1, sizeof(SkPaint));
  if (!p)
    return NULL;
  p->color = sk_color_rgba(0, 0, 0, 255);
  p->text_size = 8.f;
  return p;
}

void sk_paint_delete(SkPaint *paint) { free(paint); }

void sk_paint_set_color(SkPaint *paint, SkColor color) {
  if (paint)
    paint->color = color;
}

void sk_paint_set_text_size(SkPaint *paint, float size) {
  if (paint)
    paint->text_size = size > 0.f ? size : 8.f;
}

float sk_paint_get_text_size(const SkPaint *paint) {
  return paint ? paint->text_size : 8.f;
}

int sk_encode_png(const SkSurface *surface, uint8_t **out_bytes, size_t *out_len) {
  uint8_t *mem;
  uint8_t *straight = NULL;
  size_t len = 0;
  size_t px_len = 0;
  size_t i;
  const uint8_t *px;
  if (!surface || !out_bytes || !out_len)
    return 0;
  px = sk_surface_peek_pixels(surface, &px_len);
  if (!px)
    return 0;
  /* Pixels are premul. PNG stores straight alpha. Unpremultiply first. */
  for (i = 3; i < px_len; i += 4) {
    if (px[i] != 255)
      break;
  }
  if (i < px_len) {
    size_t j;
    straight = (uint8_t *)malloc(px_len);
    if (!straight)
      return 0;
    memcpy(straight, px, px_len);
    for (j = 0; j + 3 < px_len; j += 4) {
      unsigned a = straight[j + 3];
      if (a == 0) {
        straight[j] = 0;
        straight[j + 1] = 0;
        straight[j + 2] = 0;
      } else if (a < 255) {
        straight[j] = (uint8_t)((straight[j] * 255u + a / 2) / a);
        straight[j + 1] = (uint8_t)((straight[j + 1] * 255u + a / 2) / a);
        straight[j + 2] = (uint8_t)((straight[j + 2] * 255u + a / 2) / a);
      }
    }
    px = straight;
  }
  mem = sz_png_encode_rgba(px, surface->width, surface->height,
                           surface->width * 4, &len);
  free(straight);
  if (!mem)
    return 0;
  *out_bytes = mem;
  *out_len = len;
  return 1;
}

int sk_encode_png_to_file(const SkSurface *surface, const char *path) {
  uint8_t *bytes = NULL;
  size_t len = 0;
  FILE *f;
  size_t n;
  if (!path || !sk_encode_png(surface, &bytes, &len))
    return 0;
  f = fopen(path, "wb");
  if (!f) {
    free(bytes);
    return 0;
  }
  n = fwrite(bytes, 1, len, f);
  fclose(f);
  free(bytes);
  return n == len;
}

float sk_font_measure_string_mono(const char *text, float font_px) {
  return sk_font_measure_string(text, font_px);
}

void sk_canvas_draw_string_mono(SkCanvas *canvas, const char *text, float x,
                                float y, const SkPaint *paint) {
  sk_canvas_draw_string(canvas, text, x, y, paint);
}

SkImage *sk_image_make_rgba(const uint8_t *pixels, int width, int height) {
  if (!pixels || width < 1 || height < 1 || width > 4096 || height > 4096)
    return NULL;
  SkImage *image = (SkImage *)calloc(1, sizeof *image);
  if (!image)
    return NULL;
  image->pixels = (uint8_t *)malloc((size_t)width * height * 4);
  if (!image->pixels) {
    free(image);
    return NULL;
  }
  image->width = width;
  image->height = height;
  memcpy(image->pixels, pixels, (size_t)width * height * 4);
  return image;
}
void sk_image_unref(SkImage *image) {
  if (!image)
    return;
  if (image->gpu_image)
    sk_gpu_image_free(image->gpu_image);
  free(image->pixels);
  free(image);
}
int sk_image_width(const SkImage *image) { return image ? image->width : 0; }
int sk_image_height(const SkImage *image) { return image ? image->height : 0; }

void sk_canvas_draw_image(SkCanvas *canvas, const SkImage *image, float x,
                          float y, float w, float h, float opacity) {
  if (!canvas || !canvas->surface || !image || !(w > 0.f) || !(h > 0.f) ||
      !(opacity > 0.f) || !isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h))
    return;
  if (!(canvas->scale > 0.f)) return;
  double dx = (double)x * canvas->scale + canvas->tx;
  double dy = (double)y * canvas->scale + canvas->ty;
  double dw = (double)w * canvas->scale, dh = (double)h * canvas->scale;
  if (opacity > 1.f)
    opacity = 1.f;
  if (canvas->surface->gpu) {
    SkImage *cached = (SkImage *)image;
    if (!cached->gpu_image)
      cached->gpu_image = sk_gpu_image_new(image->pixels, image->width, image->height);
    sk_gpu_draw_image(canvas->surface->target, cached->gpu_image, dx, dy, dw, dh,
                       opacity, canvas->clip_x0, canvas->clip_y0,
                       canvas->clip_x1, canvas->clip_y1);
    return;
  }
  SkSurface *surface = canvas->surface;
  double left = fmax(0., fmax(canvas->clip_x0, dx));
  double top = fmax(0., fmax(canvas->clip_y0, dy));
  double right = fmin(surface->width, fmin(canvas->clip_x1, dx + dw));
  double bottom = fmin(surface->height, fmin(canvas->clip_y1, dy + dh));
  if (!(right > left) || !(bottom > top))
    return;
  int x0 = (int)ceil(left - 0.5), y0 = (int)ceil(top - 0.5);
  int x1 = (int)ceil(right - 0.5), y1 = (int)ceil(bottom - 0.5);
  cpu_paint_ops++;
  for (int py = y0; py < y1; py++) {
    float sy = fminf(image->height - 1.f, fmaxf(0.f, (py + 0.5 - dy) * image->height / dh - 0.5f));
    int iy = (int)sy, jy = iy + 1 < image->height ? iy + 1 : iy;
    float fy = sy - iy;
    for (int px = x0; px < x1; px++) {
      float sx = fminf(image->width - 1.f, fmaxf(0.f, (px + 0.5 - dx) * image->width / dw - 0.5f));
      int ix = (int)sx, jx = ix + 1 < image->width ? ix + 1 : ix;
      float fx = sx - ix;
      const uint8_t *a = image->pixels + ((size_t)iy * image->width + ix) * 4;
      const uint8_t *b = image->pixels + ((size_t)iy * image->width + jx) * 4;
      const uint8_t *c = image->pixels + ((size_t)jy * image->width + ix) * 4;
      const uint8_t *d = image->pixels + ((size_t)jy * image->width + jx) * 4;
      uint8_t rgba[4];
      for (int channel = 0; channel < 4; channel++) {
        float upper = a[channel] + (b[channel] - a[channel]) * fx;
        float lower = c[channel] + (d[channel] - c[channel]) * fx;
        rgba[channel] = (uint8_t)((upper + (lower - upper) * fy) * opacity + 0.5f);
      }
      uint8_t *pixel = surface->pixels + ((size_t)py * surface->width + px) * 4;
      unsigned inverse = 255u - rgba[3];
      for (int channel = 0; channel < 4; channel++)
        pixel[channel] = (uint8_t)(rgba[channel] + (pixel[channel] * inverse + 127u) / 255u);
    }
  }
}
