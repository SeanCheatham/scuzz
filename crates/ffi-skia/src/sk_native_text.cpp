#include "sk_native_text.h"
#include "sk_text_shape.h"
#include <hb-ot.h>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <mutex>

extern "C" {
extern const unsigned char scuzz_font_latin[], scuzz_font_devanagari[], scuzz_font_thai[], scuzz_font_sc[], scuzz_font_arabic[];
extern const unsigned int scuzz_font_latin_len, scuzz_font_devanagari_len, scuzz_font_thai_len, scuzz_font_sc_len, scuzz_font_arabic_len;
}

namespace {

hb_font_t *font_for(unsigned index, int style) {
  using Font = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;
  struct Entry {
    std::once_flag once;
    Font font{nullptr, hb_font_destroy};
  };
  static std::array<Entry, 20> entries;
  Entry &entry = entries[index * 4 + (style & 3)];
  std::call_once(entry.once, [&entry, index, style] {
    const unsigned char *data[] = {scuzz_font_latin, scuzz_font_devanagari, scuzz_font_thai, scuzz_font_sc, scuzz_font_arabic};
    const unsigned lengths[] = {scuzz_font_latin_len, scuzz_font_devanagari_len, scuzz_font_thai_len, scuzz_font_sc_len, scuzz_font_arabic_len};
    hb_blob_t *blob = hb_blob_create(reinterpret_cast<const char *>(data[index]), lengths[index], HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t *face = hb_face_create(blob, 0);
    hb_blob_destroy(blob);
    hb_font_t *font = hb_font_create(face);
    hb_face_destroy(face);
    hb_ot_font_set_funcs(font);
    hb_variation_t variations[] = {{HB_TAG('w','g','h','t'), 400.f}, {HB_TAG('w','d','t','h'), 100.f}};
    if (index) hb_font_set_variations(font, variations, 2);
    if (style & 1) hb_font_set_synthetic_bold(font, 1.f / 24.f, 1.f / 24.f, true);
    if (style & 2) hb_font_set_synthetic_slant(font, .2f);
    if (!hb_face_get_glyph_count(hb_font_get_face(font))) std::abort();
    hb_font_make_immutable(font);
    entry.font.reset(font);
  });
  return entry.font.get();
}

hb_font_t *shape_font(unsigned index) { return font_for(index, 0); }

float font_scale(hb_font_t *font, float size) {
  return (size > 0.f ? size : 8.f) / hb_face_get_upem(hb_font_get_face(font));
}

struct Point { double x, y; };
struct Edge { Point a, b; };
struct Outline {
  std::vector<Edge> edges;
  double x, y, scale;
  int x0, y0, x1, y1;

  Point point(float a, float b) const { return {x + a * scale, y - b * scale}; }

  void line(Point a, Point b) {
    if (a.y == b.y || std::max(a.y, b.y) <= y0 || std::min(a.y, b.y) >= y1) return;
    edges.push_back({a, b});
  }

  void curve(Point a, Point b, Point c, Point d, unsigned depth = 0) {
    double left = std::min({a.x, b.x, c.x, d.x}), right = std::max({a.x, b.x, c.x, d.x});
    double top = std::min({a.y, b.y, c.y, d.y}), bottom = std::max({a.y, b.y, c.y, d.y});
    if (bottom <= y0 || top >= y1) return;
    double error = std::max({std::abs(a.x - 2 * b.x + c.x), std::abs(a.y - 2 * b.y + c.y),
                             std::abs(b.x - 2 * c.x + d.x), std::abs(b.y - 2 * c.y + d.y)});
    if (right <= x0 || left >= x1 || error < .02 || depth == 32) {
      line(a, d);
      return;
    }
    auto mid = [](Point p, Point q) -> Point { return {(p.x + q.x) * .5, (p.y + q.y) * .5}; };
    Point ab = mid(a, b), bc = mid(b, c), cd = mid(c, d);
    Point abc = mid(ab, bc), bcd = mid(bc, cd), center = mid(abc, bcd);
    curve(a, ab, abc, center, depth + 1);
    curve(center, bcd, cd, d, depth + 1);
  }
};

void move_to(hb_draw_funcs_t *, void *, hb_draw_state_t *, float, float, void *) {}

void line_to(hb_draw_funcs_t *, void *data, hb_draw_state_t *state, float x, float y, void *) {
  auto &outline = *static_cast<Outline *>(data);
  outline.line(outline.point(state->current_x, state->current_y), outline.point(x, y));
}

void cubic_to(hb_draw_funcs_t *, void *data, hb_draw_state_t *state,
              float bx, float by, float cx, float cy, float dx, float dy, void *) {
  auto &outline = *static_cast<Outline *>(data);
  outline.curve(outline.point(state->current_x, state->current_y), outline.point(bx, by),
                outline.point(cx, cy), outline.point(dx, dy));
}

void close_path(hb_draw_funcs_t *, void *data, hb_draw_state_t *state, void *) {
  auto &outline = *static_cast<Outline *>(data);
  outline.line(outline.point(state->current_x, state->current_y), outline.point(state->path_start_x, state->path_start_y));
}

hb_draw_funcs_t *draw_functions() {
  using Functions = std::unique_ptr<hb_draw_funcs_t, decltype(&hb_draw_funcs_destroy)>;
  static const Functions functions = [] {
    Functions result(hb_draw_funcs_create(), hb_draw_funcs_destroy);
    hb_draw_funcs_set_move_to_func(result.get(), move_to, nullptr, nullptr);
    hb_draw_funcs_set_line_to_func(result.get(), line_to, nullptr, nullptr);
    hb_draw_funcs_set_cubic_to_func(result.get(), cubic_to, nullptr, nullptr);
    hb_draw_funcs_set_close_path_func(result.get(), close_path, nullptr, nullptr);
    hb_draw_funcs_make_immutable(result.get());
    return result;
  }();
  return functions.get();
}

void raster(const Outline &outline, SkTextCoverage emit, void *data) {
  if (outline.edges.empty()) return;
  double top = outline.y1, bottom = outline.y0, left = outline.x1, right = outline.x0;
  for (const auto &edge : outline.edges) {
    top = std::min({top, edge.a.y, edge.b.y}); bottom = std::max({bottom, edge.a.y, edge.b.y});
    left = std::min({left, edge.a.x, edge.b.x}); right = std::max({right, edge.a.x, edge.b.x});
  }
  int x0 = static_cast<int>(std::max<double>(outline.x0, std::min<double>(outline.x1, std::floor(left))));
  int x1 = static_cast<int>(std::max<double>(outline.x0, std::min<double>(outline.x1, std::ceil(right))));
  int y0 = static_cast<int>(std::max<double>(outline.y0, std::min<double>(outline.y1, std::floor(top))));
  int y1 = static_cast<int>(std::max<double>(outline.y0, std::min<double>(outline.y1, std::ceil(bottom))));
  if (x1 <= x0 || y1 <= y0) return;
  std::vector<double> coverage(x1 - x0);
  std::vector<uint8_t> pixels(x1 - x0);
  struct Crossing { double x; int winding; };
  std::vector<Crossing> crossings;
  crossings.reserve(outline.edges.size());
  for (int y = y0; y < y1; y++) {
    std::fill(coverage.begin(), coverage.end(), 0.);
    for (unsigned sample = 0; sample < 8; sample++) {
      double sy = y + (sample + .5) / 8.;
      crossings.clear();
      for (const auto &edge : outline.edges) {
        if (sy < std::min(edge.a.y, edge.b.y) || sy >= std::max(edge.a.y, edge.b.y)) continue;
        double x = edge.a.x + (sy - edge.a.y) * (edge.b.x - edge.a.x) / (edge.b.y - edge.a.y);
        crossings.push_back({x, edge.b.y > edge.a.y ? 1 : -1});
      }
      std::sort(crossings.begin(), crossings.end(), [](Crossing a, Crossing b) { return a.x < b.x; });
      int winding = 0;
      double start = 0.;
      for (const auto &crossing : crossings) {
        if (!winding) start = crossing.x;
        winding += crossing.winding;
        if (winding) continue;
        double a = std::max<double>(x0, start), b = std::min<double>(x1, crossing.x);
        if (!(b > a)) continue;
        int first = static_cast<int>(std::floor(a)), last = static_cast<int>(std::ceil(b));
        for (int x = first; x < last; x++) coverage[x - x0] += std::min(b, x + 1.) - std::max(a, static_cast<double>(x));
      }
    }
    for (size_t i = 0; i < pixels.size(); i++) pixels[i] = static_cast<uint8_t>(std::min(255., std::floor(coverage[i] * (255. / 8.) + .5)));
    emit(data, x0, y, x1 - x0, pixels.data());
  }
}

} // namespace

float sk_native_text_measure(SkTextRange range, float size) {
  return scuzz_text::shape_text(range, size, shape_font).width;
}

void sk_native_text_positions(SkTextRange range, float size, float *leading, float *trailing) {
  scuzz_text::text_positions(range, size, shape_font, leading, trailing);
}

void sk_native_text_breaks(const char *text, size_t bytes, uint8_t *breaks) {
  scuzz_text::cluster_breaks(text, bytes, breaks, shape_font);
}

void sk_native_text_metrics(float size, float *ascent, float *descent) {
  if (!std::isfinite(size)) {
    if (ascent) *ascent = 0.f;
    if (descent) *descent = 0.f;
    return;
  }
  hb_font_t *font = shape_font(0);
  hb_font_extents_t extents{};
  hb_font_get_h_extents(font, &extents);
  float scale = font_scale(font, size);
  if (ascent) *ascent = std::max(0.f, extents.ascender * scale);
  if (descent) *descent = std::max(0.f, -extents.descender * scale);
}

void sk_native_text_span_metrics(SkTextRange range, float size, int style, float *ascent, float *descent) {
  auto shaped = scuzz_text::shape_text(range, size, shape_font);
  float a = 0.f, d = 0.f;
  for (size_t i = 0; i < shaped.glyphs.size(); i++) {
    hb_font_t *font = font_for(shaped.fonts[i], style);
    hb_glyph_extents_t extents{};
    if (!hb_font_get_glyph_extents(font, shaped.glyphs[i], &extents)) continue;
    float scale = font_scale(font, size);
    a = std::max(a, extents.y_bearing * scale - shaped.positions[i].y);
    d = std::max(d, -(extents.y_bearing + extents.height) * scale + shaped.positions[i].y);
  }
  if (ascent) *ascent = a;
  if (descent) *descent = d;
}

void sk_native_text_draw(SkTextRange range, float size, int style, double x, double y, double scale,
                         int x0, int y0, int x1, int y1, SkTextCoverage emit, void *data) {
  if (!emit || x0 >= x1 || y0 >= y1 || !(scale > 0.) || !std::isfinite(scale) || !std::isfinite(x) || !std::isfinite(y)) return;
  auto shaped = scuzz_text::shape_text(range, size, shape_font);
  for (size_t i = 0; i < shaped.glyphs.size(); i++) {
    hb_font_t *font = font_for(shaped.fonts[i], style);
    Outline outline{{}, x + shaped.positions[i].x * scale, y + shaped.positions[i].y * scale,
                     font_scale(font, size) * scale, x0, y0, x1, y1};
    if (!std::isfinite(outline.x) || !std::isfinite(outline.y)) continue;
    hb_font_draw_glyph(font, shaped.glyphs[i], draw_functions(), &outline);
    raster(outline, emit, data);
  }
}
