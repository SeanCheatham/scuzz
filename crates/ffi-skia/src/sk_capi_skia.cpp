/* Internal Skia-backed helpers for sk_capi (no sk_capi.h — avoids type collisions). */
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/encode/SkPngEncoder.h"
#include "include/ports/SkFontMgr_data.h"
#include "include/ports/SkFontMgr_empty.h"

#include <cstdlib>
#include <cstring>
#include <mutex>

struct CapPaint {
  SkPaint paint;
  float text_size = 8.f;
};

struct CapCanvas {
  sk_sp<SkSurface> surface;
  SkCanvas *raw = nullptr;
};

struct CapSurface {
  sk_sp<SkSurface> surface;
  CapCanvas canvas;
};

static sk_sp<SkTypeface> g_typeface;
static std::once_flag g_font_once;

static sk_sp<SkTypeface> default_typeface() {
  std::call_once(g_font_once, [] {
#if defined(SCUZZ_SKIA_EMBEDDED_FONT)
    extern const unsigned char scuzz_embedded_font[];
    extern const unsigned int scuzz_embedded_font_len;
    sk_sp<SkData> data =
        SkData::MakeWithoutCopy(scuzz_embedded_font, scuzz_embedded_font_len);
    if (data) {
      sk_sp<SkData> fonts[1] = {data};
      sk_sp<SkFontMgr> mgr =
          SkFontMgr_New_Custom_Data(SkSpan<sk_sp<SkData>>(fonts, 1));
      if (mgr)
        g_typeface = mgr->makeFromData(data);
    }
#endif
    if (!g_typeface) {
      sk_sp<SkFontMgr> mgr = SkFontMgr_New_Custom_Empty();
      if (mgr)
        g_typeface = mgr->legacyMakeTypeface(nullptr, SkFontStyle());
    }
  });
  return g_typeface;
}

static SkFont make_font(float size) {
  float px = size > 0.f ? size : 8.f;
  return SkFont(default_typeface(), px);
}

extern "C" {

void *scuzz_skia_surface_make(int width, int height) {
  if (width <= 0 || height <= 0)
    return nullptr;
  sk_sp<SkSurface> surf =
      SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
  if (!surf)
    return nullptr;
  auto *out = new CapSurface();
  out->surface = surf;
  out->canvas.surface = surf;
  out->canvas.raw = surf->getCanvas();
  return out;
}

void scuzz_skia_surface_unref(void *surface) {
  delete static_cast<CapSurface *>(surface);
}

void *scuzz_skia_surface_get_canvas(void *surface) {
  auto *s = static_cast<CapSurface *>(surface);
  return s ? &s->canvas : nullptr;
}

int scuzz_skia_surface_width(const void *surface) {
  auto *s = static_cast<const CapSurface *>(surface);
  return s && s->surface ? s->surface->width() : 0;
}

int scuzz_skia_surface_height(const void *surface) {
  auto *s = static_cast<const CapSurface *>(surface);
  return s && s->surface ? s->surface->height() : 0;
}

const uint8_t *scuzz_skia_surface_peek_pixels(const void *surface,
                                              size_t *out_size) {
  auto *s = static_cast<const CapSurface *>(surface);
  if (!s || !s->surface) {
    if (out_size)
      *out_size = 0;
    return nullptr;
  }
  SkPixmap pm;
  if (!s->surface->peekPixels(&pm)) {
    if (out_size)
      *out_size = 0;
    return nullptr;
  }
  if (out_size)
    *out_size = pm.computeByteSize();
  return static_cast<const uint8_t *>(pm.addr());
}

void scuzz_skia_canvas_clear(void *canvas, uint8_t r, uint8_t g, uint8_t b,
                             uint8_t a) {
  auto *c = static_cast<CapCanvas *>(canvas);
  if (!c || !c->raw)
    return;
  c->raw->clear(SkColor4f{r / 255.f, g / 255.f, b / 255.f, a / 255.f}.toSkColor());
}

void scuzz_skia_canvas_draw_rect(void *canvas, float x, float y, float w,
                                 float h, const void *paint) {
  auto *c = static_cast<CapCanvas *>(canvas);
  auto *p = static_cast<const CapPaint *>(paint);
  if (!c || !c->raw || !p || w <= 0 || h <= 0)
    return;
  c->raw->drawRect(SkRect::MakeXYWH(x, y, w, h), p->paint);
}

void scuzz_skia_canvas_draw_string(void *canvas, const char *text, float x,
                                   float y, const void *paint) {
  auto *c = static_cast<CapCanvas *>(canvas);
  auto *p = static_cast<const CapPaint *>(paint);
  if (!c || !c->raw || !p || !text)
    return;
  SkFont font = make_font(p->text_size);
  c->raw->drawSimpleText(text, std::strlen(text), SkTextEncoding::kUTF8, x, y,
                         font, p->paint);
}

void scuzz_skia_canvas_save(void *canvas) {
  auto *c = static_cast<CapCanvas *>(canvas);
  if (c && c->raw)
    c->raw->save();
}

void scuzz_skia_canvas_restore(void *canvas) {
  auto *c = static_cast<CapCanvas *>(canvas);
  if (c && c->raw)
    c->raw->restore();
}

void scuzz_skia_canvas_clip_rect(void *canvas, float x, float y, float w,
                                 float h) {
  auto *c = static_cast<CapCanvas *>(canvas);
  if (!c || !c->raw)
    return;
  if (w <= 0.f || h <= 0.f) {
    c->raw->clipRect(SkRect::MakeXYWH(0, 0, 0, 0), true);
    return;
  }
  c->raw->clipRect(SkRect::MakeXYWH(x, y, w, h), true);
}

void *scuzz_skia_paint_new(void) {
  auto *p = new CapPaint();
  p->paint.setAntiAlias(true);
  p->paint.setColor(SK_ColorBLACK);
  return p;
}

void scuzz_skia_paint_delete(void *paint) {
  delete static_cast<CapPaint *>(paint);
}

void scuzz_skia_paint_set_color(void *paint, uint8_t r, uint8_t g, uint8_t b,
                                uint8_t a) {
  auto *p = static_cast<CapPaint *>(paint);
  if (!p)
    return;
  p->paint.setColor4f(SkColor4f{r / 255.f, g / 255.f, b / 255.f, a / 255.f},
                      nullptr);
}

void scuzz_skia_paint_set_stroke(void *paint, int stroke) {
  auto *p = static_cast<CapPaint *>(paint);
  if (!p)
    return;
  p->paint.setStyle(stroke ? SkPaint::kStroke_Style : SkPaint::kFill_Style);
}

void scuzz_skia_paint_set_stroke_width(void *paint, float width) {
  auto *p = static_cast<CapPaint *>(paint);
  if (p)
    p->paint.setStrokeWidth(width);
}

void scuzz_skia_paint_set_text_size(void *paint, float size) {
  auto *p = static_cast<CapPaint *>(paint);
  if (p)
    p->text_size = size > 0.f ? size : 8.f;
}

float scuzz_skia_paint_get_text_size(const void *paint) {
  auto *p = static_cast<const CapPaint *>(paint);
  return p ? p->text_size : 8.f;
}

float scuzz_skia_font_measure_string(const char *text, float font_px) {
  if (!text)
    return 0.f;
  SkFont font = make_font(font_px);
  return font.measureText(text, std::strlen(text), SkTextEncoding::kUTF8);
}

int scuzz_skia_encode_png(const void *surface, uint8_t **out_bytes,
                          size_t *out_len) {
  auto *s = static_cast<const CapSurface *>(surface);
  if (!s || !s->surface || !out_bytes || !out_len)
    return 0;
  SkPixmap pm;
  if (!s->surface->peekPixels(&pm))
    return 0;
  SkDynamicMemoryWStream stream;
  if (!SkPngEncoder::Encode(&stream, pm, {}))
    return 0;
  sk_sp<SkData> data = stream.detachAsData();
  if (!data)
    return 0;
  auto *buf = static_cast<uint8_t *>(std::malloc(data->size()));
  if (!buf)
    return 0;
  std::memcpy(buf, data->data(), data->size());
  *out_bytes = buf;
  *out_len = data->size();
  return 1;
}

int scuzz_skia_encode_png_to_file(const void *surface, const char *path) {
  uint8_t *bytes = nullptr;
  size_t len = 0;
  if (!scuzz_skia_encode_png(surface, &bytes, &len))
    return 0;
  SkFILEWStream stream(path);
  bool ok = stream.write(bytes, len);
  std::free(bytes);
  return ok ? 1 : 0;
}

} /* extern "C" */
