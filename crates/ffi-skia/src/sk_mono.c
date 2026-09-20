/* Monospace measure/draw. Cell is the advance of "0" on the monospace face
 * so I and W share one column. View.text stays proportional. */
#include "sk_capi.h"
#include "sk_utf8.h"

#include <string.h>

float sk_font_mono_cell(float font_px) {
  static float cached_px = -1.f;
  static float cached_cell = 0.f;
  float px = font_px > 0.f ? font_px : 8.f;
  float cell;
  if (px == cached_px)
    return cached_cell;
  cell = sk_font_measure_string_mono("0", px);
  if (cell <= 0.f)
    cell = px;
  cached_px = px;
  cached_cell = cell;
  return cell;
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
    sk_canvas_draw_string_mono(canvas, tmp, cx, y, paint);
    cx += cell;
    p += clen;
  }
}
