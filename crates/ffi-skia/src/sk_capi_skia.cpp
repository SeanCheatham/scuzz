/* Internal Skia-backed helpers for sk_capi (no sk_capi.h - avoids type
 * collisions). Declarations come from the shared header so the C bridge and
 * these definitions are checked against each other. */
#include "sk_capi_skia.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontArguments.h"
#include <array>
#include <algorithm>
#include "include/encode/SkPngEncoder.h"
#include "include/ports/SkFontMgr_data.h"
#include "include/ports/SkFontMgr_empty.h"

#include <hb.h>
#include <hb-ot.h>
#include <SheenBidi/SheenBidi.h>

#include <climits>
#include <memory>
#include <map>
#include <vector>
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
static sk_sp<SkTypeface> g_mono_typeface;
static std::once_flag g_font_once;
static std::once_flag g_mono_once;

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

static sk_sp<SkTypeface> mono_typeface() {
  std::call_once(g_mono_once, [] {
#if defined(SCUZZ_SKIA_EMBEDDED_MONO_FONT)
    extern const unsigned char scuzz_embedded_mono_font[];
    extern const unsigned int scuzz_embedded_mono_font_len;
    sk_sp<SkData> data = SkData::MakeWithoutCopy(scuzz_embedded_mono_font,
                                                scuzz_embedded_mono_font_len);
    if (data) {
      sk_sp<SkData> fonts[1] = {data};
      sk_sp<SkFontMgr> mgr =
          SkFontMgr_New_Custom_Data(SkSpan<sk_sp<SkData>>(fonts, 1));
      if (mgr)
        g_mono_typeface = mgr->makeFromData(data);
    }
#endif
    if (!g_mono_typeface)
      g_mono_typeface = default_typeface();
  });
  return g_mono_typeface;
}

static SkFont make_font(float size) {
  float px = size > 0.f ? size : 8.f;
  return SkFont(default_typeface(), px);
}

static SkFont make_mono_font(float size) {
  float px = size > 0.f ? size : 8.f;
  return SkFont(mono_typeface(), px);
}

static hb_blob_t *font_table(hb_face_t *, hb_tag_t tag, void *data) {
  auto *typeface = static_cast<SkTypeface *>(data);
  sk_sp<SkData> table = typeface->copyTableData(tag);
  if (!table || table->size() > UINT_MAX) return hb_blob_get_empty();
  auto *bytes = static_cast<const char *>(table->data());
  unsigned length = static_cast<unsigned>(table->size());
  return hb_blob_create(bytes, length, HB_MEMORY_MODE_READONLY, table.release(),
    [](void *ptr) { static_cast<SkData *>(ptr)->unref(); });
}

extern "C" {
extern const unsigned char scuzz_font_devanagari[], scuzz_font_thai[], scuzz_font_sc[];
extern const unsigned int scuzz_font_devanagari_len, scuzz_font_thai_len, scuzz_font_sc_len;
}

static sk_sp<SkTypeface> text_typeface(unsigned index) {
  if (!index) return default_typeface();
  static std::array<sk_sp<SkTypeface>, 3> faces;
  static std::array<std::once_flag, 3> once;
  std::call_once(once[index - 1], [index] {
    const unsigned char *data[] = {scuzz_font_devanagari, scuzz_font_thai, scuzz_font_sc};
    const unsigned lengths[] = {scuzz_font_devanagari_len, scuzz_font_thai_len, scuzz_font_sc_len};
    auto bytes = SkData::MakeWithoutCopy(data[index - 1], lengths[index - 1]);
    auto manager = SkFontMgr_New_Custom_Empty();
    auto face = manager->makeFromData(bytes);
    if (!face) std::abort();
    SkFontArguments::VariationPosition::Coordinate coordinates[] = {
      {SkSetFourByteTag('w','g','h','t'), 400.f}, {SkSetFourByteTag('w','d','t','h'), 100.f}};
    SkFontArguments arguments;
    arguments.setVariationDesignPosition({coordinates, 2});
    auto normal = face->makeClone(arguments);
    if (!normal) std::abort();
    faces[index - 1] = normal;
  });
  return faces[index - 1];
}

static hb_font_t *shape_font(unsigned index = 0) {
  using Font = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;
  static std::array<Font, 4> fonts = {Font(nullptr, hb_font_destroy), Font(nullptr, hb_font_destroy),
    Font(nullptr, hb_font_destroy), Font(nullptr, hb_font_destroy)};
  static std::array<std::once_flag, 4> once;
  std::call_once(once[index], [index] {
    sk_sp<SkTypeface> typeface = text_typeface(index);
    if (!typeface) std::abort();
    typeface->ref();
    hb_face_t *face = hb_face_create_for_tables(font_table, typeface.get(),
      [](void *ptr) { static_cast<SkTypeface *>(ptr)->unref(); });
    hb_font_t *font = hb_font_create(face);
    hb_face_destroy(face);
    hb_ot_font_set_funcs(font);
    if (index) {
      hb_variation_t variations[] = {{HB_TAG('w','g','h','t'), 400.f}, {HB_TAG('w','d','t','h'), 100.f}};
      hb_font_set_variations(font, variations, 2);
    }
    hb_font_make_immutable(font);
    fonts[index].reset(font);
  });
  return fonts[index].get();
}

static bool font_covers(unsigned index, const char *text, size_t bytes) {
  SBCodepointSequence sequence = {SBStringEncodingUTF8, const_cast<char *>(text), bytes};
  hb_font_t *font = shape_font(index);
  for (SBUInteger offset = 0; offset < bytes;) {
    SBCodepoint cp = SBCodepointSequenceGetCodepointAt(&sequence, &offset);
    auto category = hb_unicode_general_category(hb_unicode_funcs_get_default(), cp);
    if (category == HB_UNICODE_GENERAL_CATEGORY_FORMAT || cp == '\n' || cp == '\r' ||
        (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF)) continue;
    hb_codepoint_t glyph;
    if (!hb_font_get_nominal_glyph(font, cp, &glyph)) return false;
  }
  return true;
}

static unsigned select_font(const char *text, size_t bytes) {
  for (unsigned index = 0; index < 4; index++)
    if (font_covers(index, text, bytes)) return index;
  return 0;
}

struct ShapedText {
  std::vector<SkGlyphID> glyphs;
  std::vector<unsigned> clusters, fonts;
  std::vector<float> starts, ends;
  std::vector<bool> rtl;
  std::vector<SkPoint> positions;
  float width = 0.f;
};

static ShapedText shape_text(SkTextRange range, float size) {
  ShapedText result;
  if (!range.source || range.start >= range.end || range.end > range.bytes || range.bytes > INT_MAX) return result;
  const char *text = range.source;
  size_t bytes = range.bytes;
  SBCodepointSequence sequence = {SBStringEncodingUTF8, const_cast<char *>(text), bytes};
  SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
  SBScriptLocatorRef locator = SBScriptLocatorCreate();
  if (!algorithm || !locator) {
    if (locator) SBScriptLocatorRelease(locator);
    if (algorithm) SBAlgorithmRelease(algorithm);
    return result;
  }
  std::vector<SBScriptAgent> scripts;
  SBScriptLocatorLoadCodepoints(locator, &sequence);
  while (SBScriptLocatorMoveNext(locator)) scripts.push_back(*SBScriptLocatorGetAgent(locator));
  SBScriptLocatorRelease(locator);
  hb_buffer_t *buffer = hb_buffer_create();
  auto shape = [&](SBUInteger start, SBUInteger length, SBScript script, bool rtl, unsigned index) {
    hb_font_t *font = shape_font(index);
    unsigned units = hb_face_get_upem(hb_font_get_face(font));
    float scale = (size > 0.f ? size : 8.f) / static_cast<float>(units ? units : 1);
    hb_buffer_clear_contents(buffer);
    hb_buffer_add_utf8(buffer, text + range.start, static_cast<int>(range.end - range.start),
      static_cast<unsigned>(start - range.start), static_cast<int>(length));
    /* Use one language on each host. The process locale does not select glyphs. */
    hb_buffer_set_language(buffer, hb_language_from_string("und", -1));
    hb_buffer_set_direction(buffer, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, hb_script_from_iso15924_tag(SBScriptGetUnicodeTag(script)));
    hb_shape(font, buffer, nullptr, 0);
    unsigned count = 0;
    const hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buffer, &count);
    const hb_glyph_position_t *position = hb_buffer_get_glyph_positions(buffer, nullptr);
    float x = result.width, y = 0.f;
    for (unsigned i = 0; i < count; i++) {
      result.glyphs.push_back(static_cast<SkGlyphID>(info[i].codepoint));
      result.clusters.push_back(info[i].cluster);
      result.fonts.push_back(index);
      result.starts.push_back(x);
      result.ends.push_back(x + position[i].x_advance * scale);
      result.rtl.push_back(rtl);
      result.positions.push_back({x + position[i].x_offset * scale, y - position[i].y_offset * scale});
      x += position[i].x_advance * scale;
      y -= position[i].y_advance * scale;
    }
    result.width = x;
  };
  auto append = [&](SBUInteger start, SBUInteger length, SBScript script, bool rtl) {
    if (font_covers(0, text + start, length)) {
      shape(start, length, script, rtl, 0);
      return;
    }
    /* Keep the shaper's source clusters together when coverage is partial. */
    hb_buffer_clear_contents(buffer);
    hb_buffer_add_utf8(buffer, text + range.start, static_cast<int>(range.end - range.start),
      static_cast<unsigned>(start - range.start), static_cast<int>(length));
    hb_buffer_set_direction(buffer, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, hb_script_from_iso15924_tag(SBScriptGetUnicodeTag(script)));
    hb_buffer_set_language(buffer, hb_language_from_string("und", -1));
    hb_shape(shape_font(), buffer, nullptr, 0);
    unsigned count;
    const auto *info = hb_buffer_get_glyph_infos(buffer, &count);
    std::vector<unsigned> boundaries = {static_cast<unsigned>(start), static_cast<unsigned>(start + length)};
    for (unsigned i = 0; i < count; i++) boundaries.push_back(static_cast<unsigned>(range.start) + info[i].cluster);
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    struct Part { unsigned start, end, font; };
    std::vector<Part> parts;
    for (size_t i = 1; i < boundaries.size(); i++) {
      unsigned a = boundaries[i - 1], b = boundaries[i];
      unsigned font = select_font(text + a, b - a);
      if (!parts.empty() && parts.back().font == font) parts.back().end = b;
      else parts.push_back({a, b, font});
    }
    if (rtl) std::reverse(parts.begin(), parts.end());
    for (const auto &part : parts) shape(part.start, part.end - part.start, script, rtl, part.font);
  };
  for (SBUInteger offset = 0; offset < bytes;) {
    SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, offset, bytes - offset, SBLevelDefaultLTR);
    if (!paragraph) break;
    SBUInteger length = SBParagraphGetLength(paragraph);
    SBUInteger start = std::max(offset, static_cast<SBUInteger>(range.start));
    SBUInteger end = std::min(offset + length, static_cast<SBUInteger>(range.end));
    SBLineRef line = end > start ? SBParagraphCreateLine(paragraph, start, end - start) : nullptr;
    if (line) {
      const SBRun *runs = SBLineGetRunsPtr(line);
      SBUInteger count = SBLineGetRunCount(line);
      for (SBUInteger i = 0; i < count; i++) {
        const SBRun &run = runs[i];
        std::vector<SBScriptAgent> parts;
        for (const auto &script : scripts) {
          SBUInteger start = std::max(run.offset, script.offset);
          SBUInteger end = std::min(run.offset + run.length, script.offset + script.length);
          if (end > start) parts.push_back({start, end - start, script.script});
        }
        if (run.level & 1) {
          for (auto p = parts.rbegin(); p != parts.rend(); ++p) append(p->offset, p->length, p->script, true);
        } else {
          for (const auto &p : parts) append(p.offset, p.length, p.script, false);
        }
      }
      SBLineRelease(line);
    }
    SBParagraphRelease(paragraph);
    if (!length) break;
    offset += length;
  }
  hb_buffer_destroy(buffer);
  SBAlgorithmRelease(algorithm);
  return result;
}

static bool advances_caret(SBCodepoint codepoint) {
  hb_unicode_general_category_t category = hb_unicode_general_category(hb_unicode_funcs_get_default(), codepoint);
  return category != HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK &&
    category != HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK &&
    category != HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK &&
    category != HB_UNICODE_GENERAL_CATEGORY_FORMAT;
}

void scuzz_skia_font_text_positions(SkTextRange range, float font_px, int style,
                                    float *leading, float *trailing) {
  if (!leading || !trailing || range.start > range.end || range.end > range.bytes || range.bytes > INT_MAX) return;
  size_t bytes = range.end - range.start;
  const char *text = range.source ? range.source + range.start : nullptr;
  std::fill(leading, leading + bytes + 1, 0.f);
  std::fill(trailing, trailing + bytes + 1, 0.f);
  if (!text) return;
  if (style & 4) {
    SkFont font = make_mono_font(font_px);
    SBCodepointSequence sequence = {SBStringEncodingUTF8, const_cast<char *>(text), bytes};
    float x = 0.f;
    for (SBUInteger p = 0; p < bytes;) {
      SBUInteger start = p;
      SBCodepointSequenceGetCodepointAt(&sequence, &p);
      float next = x + font.measureText(text + start, p - start, SkTextEncoding::kUTF8);
      for (SBUInteger i = start; i < p; i++) { leading[i] = x; trailing[i] = next; }
      x = next;
    }
    leading[bytes] = trailing[bytes] = x;
    return;
  }
  ShapedText shaped = shape_text(range, font_px);
  struct Cluster { float left, right; bool rtl; };
  std::map<unsigned, Cluster> clusters;
  for (size_t i = 0; i < shaped.clusters.size(); i++) {
    unsigned byte = shaped.clusters[i];
    auto inserted = clusters.emplace(byte, Cluster{shaped.starts[i], shaped.ends[i], shaped.rtl[i]});
    Cluster &cluster = inserted.first->second;
    cluster.left = std::min(cluster.left, shaped.starts[i]);
    cluster.right = std::max(cluster.right, shaped.ends[i]);
  }
  SBCodepointSequence sequence = {SBStringEncodingUTF8, const_cast<char *>(text), bytes};
  for (auto i = clusters.begin(); i != clusters.end(); ++i) {
    auto next = std::next(i);
    SBUInteger start = i->first, end = next == clusters.end() ? bytes : next->first;
    if (start >= end || end > bytes) continue;
    unsigned bases = 0, step = 0;
    for (SBUInteger p = start; p < end;) bases += advances_caret(SBCodepointSequenceGetCodepointAt(&sequence, &p));
    bool mark_only = bases == 0;
    if (mark_only) bases = 1;
    float a = i->second.rtl ? i->second.right : i->second.left;
    float b = i->second.rtl ? i->second.left : i->second.right;
    for (SBUInteger p = start; p < end;) {
      SBUInteger byte = p;
      SBCodepoint codepoint = SBCodepointSequenceGetCodepointAt(&sequence, &p);
      float before = a + (b - a) * static_cast<float>(step) / bases;
      if (advances_caret(codepoint) || (mark_only && byte == start)) step++;
      float after = a + (b - a) * static_cast<float>(step) / bases;
      while (byte < p) { leading[byte] = before; trailing[byte] = after; byte++; }
    }
  }
  if (bytes) leading[bytes] = trailing[bytes] = trailing[bytes - 1];
}

void scuzz_skia_text_cluster_breaks(const char *text, size_t bytes, uint8_t *breaks) {
  if (!breaks || bytes == SIZE_MAX) return;
  std::memset(breaks, 0, bytes + 1);
  breaks[0] = breaks[bytes] = 1;
  ShapedText shaped = shape_text(sk_text_range(text, bytes), 14.f);
  for (unsigned cluster : shaped.clusters) if (cluster <= bytes) breaks[cluster] = 1;
}

static float shaped_width(const char *text, size_t bytes, float size) {
  return shape_text(sk_text_range(text, bytes), size).width;
}

static void draw_shaped(CapCanvas *canvas, SkTextRange range,
                        float x, float y, const SkFont &font, const SkPaint &paint) {
  ShapedText shaped = shape_text(range, font.getSize());
  for (size_t start = 0; start < shaped.glyphs.size();) {
    size_t end = start + 1;
    while (end < shaped.glyphs.size() && shaped.fonts[end] == shaped.fonts[start]) end++;
    SkFont selected = font;
    selected.setTypeface(text_typeface(shaped.fonts[start]));
    canvas->raw->drawGlyphs(static_cast<int>(end - start), shaped.glyphs.data() + start,
      shaped.positions.data() + start, {x, y}, selected, paint);
    start = end;
  }
}

void *scuzz_skia_surface_make(int width, int height) {
  if (width <= 0 || height <= 0)
    return nullptr;
  sk_sp<SkSurface> surf = SkSurfaces::Raster(SkImageInfo::Make(
      width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType));
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
  draw_shaped(c, sk_text_range(text, std::strlen(text)), x, y, font, p->paint);
}

void scuzz_skia_canvas_draw_string_mono(void *canvas, const char *text, float x,
                                        float y, const void *paint) {
  auto *c = static_cast<CapCanvas *>(canvas);
  auto *p = static_cast<const CapPaint *>(paint);
  if (!c || !c->raw || !p || !text)
    return;
  SkFont font = make_mono_font(p->text_size);
  c->raw->drawSimpleText(text, std::strlen(text), SkTextEncoding::kUTF8, x, y,
                         font, p->paint);
}

void scuzz_skia_canvas_save(void *canvas) {
  auto *c = static_cast<CapCanvas *>(canvas);
  if (c && c->raw)
    c->raw->save();
}

int scuzz_skia_canvas_save_layer_alpha(void *canvas, uint8_t alpha) {
  auto *c = static_cast<CapCanvas *>(canvas);
  if (!c || !c->raw) return 0;
  c->raw->saveLayerAlpha(nullptr, alpha);
  return 1;
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

void scuzz_skia_paint_set_text_size(void *paint, float size) {
  auto *p = static_cast<CapPaint *>(paint);
  if (p)
    p->text_size = size > 0.f ? size : 8.f;
}

float scuzz_skia_paint_get_text_size(const void *paint) {
  auto *p = static_cast<const CapPaint *>(paint);
  return p ? p->text_size : 8.f;
}

static SkFont styled_font(float size, int style) {
  SkFont font = (style & 4) ? make_mono_font(size) : make_font(size);
  font.setEmbolden((style & 1) != 0);
  if (style & 2) font.setSkewX(-0.2f);
  return font;
}

void scuzz_skia_font_metrics(float font_px, int style, float *ascent, float *descent) {
  SkFontMetrics metrics;
  styled_font(font_px, style).getMetrics(&metrics);
  if (ascent) *ascent = std::max(0.f, -metrics.fAscent);
  if (descent) *descent = std::max(0.f, metrics.fDescent);
}

void scuzz_skia_font_span_metrics(SkTextRange range, float font_px, int style,
                                 float *ascent, float *descent) {
  float a = 0.f, d = 0.f;
  if (style & 4) scuzz_skia_font_metrics(font_px, style, &a, &d);
  else {
    ShapedText shaped = shape_text(range, font_px);
    SkFont font = styled_font(font_px, style);
    for (size_t i = 0; i < shaped.glyphs.size(); i++) {
      font.setTypeface(text_typeface(shaped.fonts[i]));
      SkRect bounds;
      font.getBounds(&shaped.glyphs[i], 1, &bounds, nullptr);
      a = std::max(a, -bounds.top() - shaped.positions[i].y());
      d = std::max(d, bounds.bottom() + shaped.positions[i].y());
    }
  }
  if (ascent) *ascent = a;
  if (descent) *descent = d;
}

float scuzz_skia_font_measure_span(SkTextRange range, float font_px, int style) {
  if (!range.source || range.start > range.end || range.end > range.bytes || range.bytes > INT_MAX) return 0.f;
  return style & 4 ? styled_font(font_px, style).measureText(range.source + range.start,
    range.end - range.start, SkTextEncoding::kUTF8) : shape_text(range, font_px).width;
}

void scuzz_skia_canvas_draw_span(void *canvas, SkTextRange range,
                                float x, float y, const void *paint, int style) {
  auto *c = static_cast<CapCanvas *>(canvas);
  auto *p = static_cast<const CapPaint *>(paint);
  if (!c || !c->raw || !p || !range.source || range.start > range.end || range.end > range.bytes || range.bytes > INT_MAX) return;
  const char *text = range.source + range.start;
  size_t bytes = range.end - range.start;
  SkFont font = styled_font(p->text_size, style);
  if (style & 4) c->raw->drawSimpleText(text, bytes, SkTextEncoding::kUTF8, x, y, font, p->paint);
  else draw_shaped(c, range, x, y, font, p->paint);
  if (style & 8) {
    float width = scuzz_skia_font_measure_span(range, p->text_size, style);
    c->raw->drawRect(SkRect::MakeXYWH(x, y + p->text_size * 0.1f, width,
                                    std::max(1.f, p->text_size / 16.f)), p->paint);
  }
}

float scuzz_skia_font_measure_string(const char *text, float font_px) {
  if (!text)
    return 0.f;
  return shaped_width(text, std::strlen(text), font_px);
}

float scuzz_skia_font_measure_string_mono(const char *text, float font_px) {
  if (!text)
    return 0.f;
  SkFont font = make_mono_font(font_px);
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
  if (!path)
    return 0;
  if (!scuzz_skia_encode_png(surface, &bytes, &len))
    return 0;
  SkFILEWStream stream(path);
  bool ok = stream.write(bytes, len);
  std::free(bytes);
  return ok ? 1 : 0;
}

void *scuzz_skia_image_make(const uint8_t *pixels, int width, int height) {
  if (!pixels || width < 1 || height < 1 || width > 4096 || height > 4096)
    return nullptr;
  auto data = SkData::MakeWithCopy(pixels, (size_t)width * height * 4);
  auto image = SkImages::RasterFromData(
      SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
      data, (size_t)width * 4);
  return image ? new sk_sp<SkImage>(image) : nullptr;
}
void scuzz_skia_image_unref(void *image) {
  delete static_cast<sk_sp<SkImage> *>(image);
}
int scuzz_skia_image_width(const void *image) {
  auto value = static_cast<const sk_sp<SkImage> *>(image);
  return value && *value ? (*value)->width() : 0;
}
int scuzz_skia_image_height(const void *image) {
  auto value = static_cast<const sk_sp<SkImage> *>(image);
  return value && *value ? (*value)->height() : 0;
}
void scuzz_skia_canvas_draw_image(void *canvas, const void *image, float x,
                                  float y, float w, float h, float opacity) {
  auto target = static_cast<CapCanvas *>(canvas);
  auto value = static_cast<const sk_sp<SkImage> *>(image);
  if (!target || !target->raw || !value || !*value || !(w > 0.f) ||
      !(h > 0.f) || !(opacity > 0.f))
    return;
  SkPaint paint;
  paint.setAlphaf(opacity > 1.f ? 1.f : opacity);
  target->raw->drawImageRect(*value, SkRect::MakeXYWH(x, y, w, h),
                            SkSamplingOptions(SkFilterMode::kLinear), &paint);
}
