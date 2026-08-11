/* sk_capi implementation backed by real Skia (CPU).
 *
 * Built out-of-tree by scripts/build_skia_prebuilt.sh against a pinned Skia
 * checkout. Callers still include only sk_capi.h.
 */
#include "sk_capi.h"

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

#if defined(SK_FONTMGR_FONTCONFIG_AVAILABLE)
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#elif defined(SK_FONTMGR_CORETEXT_AVAILABLE)
#include "include/ports/SkFontMgr_mac_ct.h"
#endif

#include <cstdlib>
#include <cstring>
#include <mutex>

struct SkPaint {
  ::SkPaint paint;
  float text_size;
  SkPaint() : text_size(8.f) {}
};

struct SkCanvas {
  sk_sp<::SkSurface> surface;
  ::SkCanvas *raw = nullptr;
};

struct SkSurface {
  sk_sp<::SkSurface> surface;
  SkCanvas canvas;
};

static sk_sp<::SkTypeface> g_typeface;
static std::once_flag g_font_once;

#ifndef SCUZZ_SKIA_FONT_PATH
#define SCUZZ_SKIA_FONT_PATH ""
#endif

static sk_sp<::SkFontMgr> make_font_mgr() {
#if defined(SK_FONTMGR_FONTCONFIG_AVAILABLE)
  return SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
#elif defined(SK_FONTMGR_CORETEXT_AVAILABLE)
  return SkFontMgr_New_CoreText(nullptr);
#else
  return nullptr;
#endif
}

static sk_sp<::SkTypeface> default_typeface() {
  std::call_once(g_font_once, [] {
    sk_sp<::SkFontMgr> mgr = make_font_mgr();
    if (mgr) {
#if defined(SCUZZ_SKIA_EMBEDDED_FONT)
      /* bytes + len provided by build_skia_prebuilt.sh generated header */
      extern const unsigned char scuzz_embedded_font[];
      extern const unsigned int scuzz_embedded_font_len;
      sk_sp<SkData> data =
          SkData::MakeWithoutCopy(scuzz_embedded_font, scuzz_embedded_font_len);
      if (data)
        g_typeface = mgr->makeFromData(data);
#endif
      if (!g_typeface) {
        const char *path = SCUZZ_SKIA_FONT_PATH;
        if (path && path[0]) {
          sk_sp<SkData> data = SkData::MakeFromFileName(path);
          if (data)
            g_typeface = mgr->makeFromData(data);
        }
      }
      if (!g_typeface)
        g_typeface = mgr->matchFamilyStyle("DejaVu Sans", SkFontStyle());
      if (!g_typeface)
        g_typeface = mgr->matchFamilyStyle("sans-serif", SkFontStyle());
      if (!g_typeface)
        g_typeface = mgr->legacyMakeTypeface(nullptr, SkFontStyle());
    }
  });
  return g_typeface;
}

static ::SkFont make_font(float size) {
  float px = size > 0.f ? size : 8.f;
  return ::SkFont(default_typeface(), px);
}

extern "C" {

SkColor sk_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  SkColor c = {r, g, b, a};
  return c;
}

SkColor sk_color_argb(uint32_t argb) {
  SkColor c;
  c.a = (uint8_t)((argb >> 24) & 0xff);
  c.r = (uint8_t)((argb >> 16) & 0xff);
  c.g = (uint8_t)((argb >> 8) & 0xff);
  c.b = (uint8_t)(argb & 0xff);
  return c;
}

static SkColor4f to_color4f(SkColor c) {
  return SkColor4f{c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
}

SkSurface *sk_surface_make_raster_n32_premul(int width, int height) {
  if (width <= 0 || height <= 0)
    return nullptr;
  sk_sp<::SkSurface> surf =
      SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
  if (!surf)
    return nullptr;
  auto *out = new SkSurface();
  out->surface = surf;
  out->canvas.surface = surf;
  out->canvas.raw = surf->getCanvas();
  return out;
}

void sk_surface_unref(SkSurface *surface) { delete surface; }

SkCanvas *sk_surface_get_canvas(SkSurface *surface) {
  return surface ? &surface->canvas : nullptr;
}

int sk_surface_width(const SkSurface *surface) {
  return surface && surface->surface ? surface->surface->width() : 0;
}

int sk_surface_height(const SkSurface *surface) {
  return surface && surface->surface ? surface->surface->height() : 0;
}

const uint8_t *sk_surface_peek_pixels(const SkSurface *surface,
                                      size_t *out_size) {
  if (!surface || !surface->surface) {
    if (out_size)
      *out_size = 0;
    return nullptr;
  }
  ::SkPixmap pm;
  if (!surface->surface->peekPixels(&pm)) {
    if (out_size)
      *out_size = 0;
    return nullptr;
  }
  if (out_size)
    *out_size = pm.computeByteSize();
  return static_cast<const uint8_t *>(pm.addr());
}

void sk_canvas_clear(SkCanvas *canvas, SkColor color) {
  if (!canvas || !canvas->raw)
    return;
  canvas->raw->clear(to_color4f(color).toSkColor());
}

void sk_canvas_draw_rect(SkCanvas *canvas, float x, float y, float w, float h,
                         const SkPaint *paint) {
  if (!canvas || !canvas->raw || !paint || w <= 0 || h <= 0)
    return;
  canvas->raw->drawRect(SkRect::MakeXYWH(x, y, w, h), paint->paint);
}

void sk_canvas_draw_string(SkCanvas *canvas, const char *text, float x, float y,
                           const SkPaint *paint) {
  if (!canvas || !canvas->raw || !paint || !text)
    return;
  ::SkFont font = make_font(paint->text_size);
  canvas->raw->drawSimpleText(text, std::strlen(text), SkTextEncoding::kUTF8, x,
                              y, font, paint->paint);
}

SkPaint *sk_paint_new(void) {
  auto *p = new SkPaint();
  p->paint.setAntiAlias(true);
  p->paint.setColor(SK_ColorBLACK);
  return p;
}

void sk_paint_delete(SkPaint *paint) { delete paint; }

void sk_paint_set_color(SkPaint *paint, SkColor color) {
  if (!paint)
    return;
  paint->paint.setColor4f(to_color4f(color), nullptr);
}

void sk_paint_set_stroke(SkPaint *paint, int stroke) {
  if (!paint)
    return;
  paint->paint.setStyle(stroke ? ::SkPaint::kStroke_Style
                               : ::SkPaint::kFill_Style);
}

void sk_paint_set_stroke_width(SkPaint *paint, float width) {
  if (paint)
    paint->paint.setStrokeWidth(width);
}

void sk_paint_set_text_size(SkPaint *paint, float size) {
  if (paint)
    paint->text_size = size > 0.f ? size : 8.f;
}

float sk_paint_get_text_size(const SkPaint *paint) {
  return paint ? paint->text_size : 8.f;
}

float sk_font_measure_string(const char *text, float font_px) {
  if (!text)
    return 0.f;
  ::SkFont font = make_font(font_px);
  return font.measureText(text, std::strlen(text), SkTextEncoding::kUTF8);
}

int sk_encode_png(const SkSurface *surface, uint8_t **out_bytes,
                  size_t *out_len) {
  if (!surface || !surface->surface || !out_bytes || !out_len)
    return 0;
  ::SkPixmap pm;
  if (!surface->surface->peekPixels(&pm))
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

int sk_encode_png_to_file(const SkSurface *surface, const char *path) {
  uint8_t *bytes = nullptr;
  size_t len = 0;
  if (!sk_encode_png(surface, &bytes, &len))
    return 0;
  SkFILEWStream stream(path);
  bool ok = stream.write(bytes, len);
  std::free(bytes);
  return ok ? 1 : 0;
}

} /* extern "C" */
