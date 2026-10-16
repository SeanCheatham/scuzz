#ifndef SCUZZ_TEXT_SHAPE_H
#define SCUZZ_TEXT_SHAPE_H

#include "../include/sk_text.h"
#include <hb.h>
#include <SheenBidi/SheenBidi.h>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace scuzz_text {

/* Font providers return immutable fonts with design-unit metrics. */
using FontProvider = hb_font_t *(*)(unsigned);
struct Point { float x, y; };

inline bool font_covers(FontProvider shape_font, unsigned index, const char *text, size_t bytes) {
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

inline unsigned select_font(FontProvider shape_font, const char *text, size_t bytes) {
  for (unsigned index = 0; index < 5; index++)
    if (font_covers(shape_font, index, text, bytes)) return index;
  return 0;
}

struct ShapedText {
  std::vector<uint16_t> glyphs;
  std::vector<unsigned> clusters, fonts;
  std::vector<float> starts, ends;
  std::vector<bool> rtl;
  std::vector<Point> positions;
  float width = 0.f;
};

inline ShapedText shape_text(SkTextRange range, float size, FontProvider shape_font) {
  ShapedText result;
  if (!range.source || range.start >= range.end || !sk_text_valid(range) || range.bytes > INT_MAX) return result;
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
  SBUInteger context_start = 0, context_end = 0;
  int64_t cluster_base = 0;
  auto fill_buffer = [&](SBUInteger start, SBUInteger length, SBScript script, bool rtl) {
    hb_buffer_clear_contents(buffer);
    if (script == SBScriptARAB && !rtl) {
      /* HarfBuzz reverses Arabic input for LTR runs. Reverse its context too. */
      auto reverse = [&](SBUInteger a, SBUInteger b) {
        std::string value;
        for (SBUInteger end = b; end > a;) {
          SBUInteger begin = end - 1;
          while (begin > a && (static_cast<unsigned char>(text[begin]) & 0xc0) == 0x80) begin--;
          value.append(text + begin, end - begin);
          end = begin;
        }
        return value;
      };
      std::string value = reverse(start + length, context_end);
      size_t item = value.size();
      value.append(text + start, length);
      value += reverse(context_start, start);
      hb_buffer_add_utf8(buffer, value.data(), static_cast<int>(value.size()), static_cast<unsigned>(item), static_cast<int>(length));
      cluster_base = static_cast<int64_t>(start) - static_cast<int64_t>(item);
    } else {
      hb_buffer_add_utf8(buffer, text + context_start, static_cast<int>(context_end - context_start),
        static_cast<unsigned>(start - context_start), static_cast<int>(length));
      cluster_base = context_start;
    }
  };
  auto shape = [&](SBUInteger start, SBUInteger length, SBScript script, bool rtl, unsigned index) {
    hb_font_t *font = shape_font(index);
    unsigned units = hb_face_get_upem(hb_font_get_face(font));
    float scale = (size > 0.f ? size : 8.f) / static_cast<float>(units ? units : 1);
    fill_buffer(start, length, script, rtl);
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
      result.glyphs.push_back(static_cast<uint16_t>(info[i].codepoint));
      result.clusters.push_back(static_cast<unsigned>(cluster_base + info[i].cluster - range.start));
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
    if (font_covers(shape_font, 0, text + start, length)) {
      shape(start, length, script, rtl, 0);
      return;
    }
    /* Keep the shaper's source clusters together when coverage is partial. */
    fill_buffer(start, length, script, rtl);
    hb_buffer_set_direction(buffer, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, hb_script_from_iso15924_tag(SBScriptGetUnicodeTag(script)));
    hb_buffer_set_language(buffer, hb_language_from_string("und", -1));
    hb_shape(shape_font(0), buffer, nullptr, 0);
    unsigned count;
    const auto *info = hb_buffer_get_glyph_infos(buffer, &count);
    std::vector<unsigned> boundaries = {static_cast<unsigned>(start), static_cast<unsigned>(start + length)};
    for (unsigned i = 0; i < count; i++) boundaries.push_back(static_cast<unsigned>(cluster_base + info[i].cluster));
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    struct Part { unsigned start, end, font; };
    std::vector<Part> parts;
    for (size_t i = 1; i < boundaries.size(); i++) {
      unsigned a = boundaries[i - 1], b = boundaries[i];
      unsigned font = select_font(shape_font, text + a, b - a);
      if (!parts.empty() && parts.back().font == font) parts.back().end = b;
      else parts.push_back({a, b, font});
    }
    if (rtl) std::reverse(parts.begin(), parts.end());
    for (const auto &part : parts) shape(part.start, part.end - part.start, script, rtl, part.font);
  };
  for (SBUInteger offset = 0; offset < range.line_end;) {
    SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, offset, bytes - offset, SBLevelDefaultLTR);
    if (!paragraph) break;
    SBUInteger length = SBParagraphGetLength(paragraph);
    SBUInteger start = std::max(offset, static_cast<SBUInteger>(range.line_start));
    SBUInteger end = std::min(offset + length, static_cast<SBUInteger>(range.line_end));
    SBLineRef line = end > start ? SBParagraphCreateLine(paragraph, start, end - start) : nullptr;
    if (line) {
      const SBRun *runs = SBLineGetRunsPtr(line);
      SBUInteger count = SBLineGetRunCount(line);
      for (SBUInteger i = 0; i < count; i++) {
        const SBRun &run = runs[i];
        context_start = run.offset;
        context_end = run.offset + run.length;
        std::vector<SBScriptAgent> parts;
        for (const auto &script : scripts) {
          SBUInteger start = std::max({run.offset, script.offset, static_cast<SBUInteger>(range.start)});
          SBUInteger end = std::min({run.offset + run.length, script.offset + script.length, static_cast<SBUInteger>(range.end)});
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

inline bool advances_caret(SBCodepoint codepoint) {
  hb_unicode_general_category_t category = hb_unicode_general_category(hb_unicode_funcs_get_default(), codepoint);
  return category != HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK &&
    category != HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK &&
    category != HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK &&
    category != HB_UNICODE_GENERAL_CATEGORY_FORMAT;
}

inline void text_positions(SkTextRange range, float font_px, FontProvider shape_font,
                                    float *leading, float *trailing) {
  if (!leading || !trailing || !sk_text_valid(range) || range.bytes > INT_MAX) return;
  size_t bytes = range.end - range.start;
  const char *text = range.source ? range.source + range.start : nullptr;
  std::fill(leading, leading + bytes + 1, 0.f);
  std::fill(trailing, trailing + bytes + 1, 0.f);
  if (!text) return;
  ShapedText shaped = shape_text(range, font_px, shape_font);
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

inline void cluster_breaks(const char *text, size_t bytes, uint8_t *breaks, FontProvider shape_font) {
  if (!breaks || bytes == SIZE_MAX) return;
  std::memset(breaks, 0, bytes + 1);
  breaks[0] = breaks[bytes] = 1;
  ShapedText shaped = shape_text(sk_text_range(text, bytes), 14.f, shape_font);
  for (unsigned cluster : shaped.clusters) if (cluster <= bytes) breaks[cluster] = 1;
}


} // namespace scuzz_text

#endif
