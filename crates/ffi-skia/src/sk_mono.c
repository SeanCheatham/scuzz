/* Monospace measure/draw. Cell is max(measure("M"), measure("W")) so
 * editor caret columns match Skia and sk_sw. View.text stays proportional. */
#include "sk_capi.h"
#include "sk_utf8.h"

#include <string.h>

float sk_font_mono_cell(float font_px) {
  float m;
  float w;
  float px = font_px > 0.f ? font_px : 8.f;
  m = sk_font_measure_string("M", px);
  w = sk_font_measure_string("W", px);
  if (w > m)
    m = w;
  if (m <= 0.f)
    m = px;
  return m;
}

float sk_font_measure_mono_string(const char *text, float font_px) {
  float cell = sk_font_mono_cell(font_px);
  const char *p;
  int n = 0;
  if (!text)
    return 0.f;
  for (p = text; *p;) {
    int clen = sk_utf8_clen(p);
    if (clen < 1)
      clen = 1;
    p += clen;
    n++;
  }
  return (float)n * cell;
}

void sk_canvas_draw_mono_string(SkCanvas *canvas, const char *text, float x,
                                float y, const SkPaint *paint) {
  float cell;
  const char *p;
  float cx;
  if (!canvas || !paint || !text)
    return;
  cell = sk_font_mono_cell(sk_paint_get_text_size(paint));
  cx = x;
  for (p = text; *p;) {
    int clen = sk_utf8_clen(p);
    char tmp[8];
    if (clen < 1)
      clen = 1;
    if (clen > 4)
      clen = 4;
    memcpy(tmp, p, (size_t)clen);
    tmp[clen] = '\0';
    sk_canvas_draw_string(canvas, tmp, cx, y, paint);
    cx += cell;
    p += clen;
  }
}
