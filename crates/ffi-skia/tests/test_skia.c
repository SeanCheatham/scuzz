#include "sk_capi.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cell_has_ink(const uint8_t *px, int w, int x0, int x1, int y0, int y1) {
  int x, y;
  for (y = y0; y < y1; y++) {
    for (x = x0; x < x1; x++) {
      const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
      if (p[0] > 40 || p[1] > 40 || p[2] > 40)
        return 1;
    }
  }
  return 0;
}

static const uint8_t image_png[] = {
    137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
    0, 0, 0, 2, 0, 0, 0, 2, 8, 6, 0, 0, 0, 114, 182, 13,
    36, 0, 0, 0, 20, 73, 68, 65, 84, 120, 156, 99, 248, 207, 192, 240,
    31, 8, 27, 24, 32, 224, 255, 127, 0, 50, 228, 5, 124, 41, 109, 19,
    187, 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130,
};
static const uint8_t image_jpeg[] = {
    255, 216, 255, 224, 0, 16, 74, 70, 73, 70, 0, 1, 1, 0, 0, 1,
    0, 1, 0, 0, 255, 219, 0, 67, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 255, 219, 0, 67, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 255, 192,
    0, 17, 8, 0, 3, 0, 2, 3, 1, 17, 0, 2, 17, 1, 3, 17,
    1, 255, 196, 0, 31, 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10, 11, 255, 196, 0, 181, 16, 0, 2, 1, 3, 3, 2, 4, 3, 5,
    5, 4, 4, 0, 0, 1, 125, 1, 2, 3, 0, 4, 17, 5, 18, 33,
    49, 65, 6, 19, 81, 97, 7, 34, 113, 20, 50, 129, 145, 161, 8, 35,
    66, 177, 193, 21, 82, 209, 240, 36, 51, 98, 114, 130, 9, 10, 22, 23,
    24, 25, 26, 37, 38, 39, 40, 41, 42, 52, 53, 54, 55, 56, 57, 58,
    67, 68, 69, 70, 71, 72, 73, 74, 83, 84, 85, 86, 87, 88, 89, 90,
    99, 100, 101, 102, 103, 104, 105, 106, 115, 116, 117, 118, 119, 120, 121, 122,
    131, 132, 133, 134, 135, 136, 137, 138, 146, 147, 148, 149, 150, 151, 152, 153,
    154, 162, 163, 164, 165, 166, 167, 168, 169, 170, 178, 179, 180, 181, 182, 183,
    184, 185, 186, 194, 195, 196, 197, 198, 199, 200, 201, 202, 210, 211, 212, 213,
    214, 215, 216, 217, 218, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 241,
    242, 243, 244, 245, 246, 247, 248, 249, 250, 255, 196, 0, 31, 1, 0, 3,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1,
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 255, 196, 0, 181, 17, 0,
    2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119, 0,
    1, 2, 3, 17, 4, 5, 33, 49, 6, 18, 65, 81, 7, 97, 113, 19,
    34, 50, 129, 8, 20, 66, 145, 161, 177, 193, 9, 35, 51, 82, 240, 21,
    98, 114, 209, 10, 22, 36, 52, 225, 37, 241, 23, 24, 25, 26, 38, 39,
    40, 41, 42, 53, 54, 55, 56, 57, 58, 67, 68, 69, 70, 71, 72, 73,
    74, 83, 84, 85, 86, 87, 88, 89, 90, 99, 100, 101, 102, 103, 104, 105,
    106, 115, 116, 117, 118, 119, 120, 121, 122, 130, 131, 132, 133, 134, 135, 136,
    137, 138, 146, 147, 148, 149, 150, 151, 152, 153, 154, 162, 163, 164, 165, 166,
    167, 168, 169, 170, 178, 179, 180, 181, 182, 183, 184, 185, 186, 194, 195, 196,
    197, 198, 199, 200, 201, 202, 210, 211, 212, 213, 214, 215, 216, 217, 218, 226,
    227, 228, 229, 230, 231, 232, 233, 234, 242, 243, 244, 245, 246, 247, 248, 249,
    250, 255, 218, 0, 12, 3, 1, 0, 2, 17, 3, 17, 0, 63, 0, 252,
    71, 175, 224, 243, 253, 8, 63, 255, 217,
};
static const uint8_t image_large[] = {
    137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
    0, 0, 16, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0, 177, 227, 0,
    66, 0, 0, 0, 8, 73, 68, 65, 84, 120, 156, 3, 0, 0, 0, 0,
    1, 72, 6, 137, 210, 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96,
    130,
};

static void test_images(void) {
  const char *error = NULL;
  assert(!sk_image_decode(NULL, 0, &error) && error);
  assert(!sk_image_decode(image_png, 32u * 1024u * 1024u + 1, &error) && error);
  assert(!sk_image_decode(image_large, sizeof image_large, &error) && error);
  for (size_t len = 0; len < sizeof image_png; len++)
    assert(!sk_image_decode(image_png, len, &error) && error);
  uint8_t broken[sizeof image_png];
  for (size_t i = 8; i < sizeof image_png; i++) {
    memcpy(broken, image_png, sizeof broken);
    broken[i] ^= 1;
    assert(!sk_image_decode(broken, sizeof broken, &error) && error);
  }
  SkImage *png = sk_image_decode(image_png, sizeof image_png, &error);
  assert(png && !error && sk_image_width(png) == 2 && sk_image_height(png) == 2);
  SkImage *jpeg = sk_image_decode(image_jpeg, sizeof image_jpeg, &error);
  assert(jpeg && !error && sk_image_width(jpeg) == 2 && sk_image_height(jpeg) == 3);
  assert(!sk_image_decode(image_jpeg, sizeof image_jpeg - 2, &error) && error);
  const uint8_t expected[16] = {255, 0, 0, 255, 0, 128, 0, 128,
                               0, 0, 0, 0, 0, 0, 255, 255};
  for (int gpu = 0; gpu <= sk_gpu_available(); gpu++) {
    SkSurface *surface = gpu ? sk_surface_make_gpu_n32_premul(5, 5)
                             : sk_surface_make_raster_n32_premul(5, 5);
    assert(surface);
    SkCanvas *canvas = sk_surface_get_canvas(surface);
    sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
    sk_canvas_draw_image(canvas, png, 0, 0, 2, 2, 1.f);
    size_t len = 0;
    const uint8_t *pixels = sk_surface_peek_pixels(surface, &len);
    assert(pixels && len == 100);
    for (int row = 0; row < 2; row++)
      for (int i = 0; i < 8; i++)
        assert(abs(pixels[row * 20 + i] - expected[row * 8 + i]) <= 1);
    sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
    sk_canvas_save(canvas);
    sk_canvas_clip_rect(canvas, 2, 2, 1, 1);
    sk_canvas_draw_image(canvas, png, 1, 1, 3, 3, 1.f);
    sk_canvas_restore(canvas);
    pixels = sk_surface_peek_pixels(surface, &len);
    const uint8_t middle[4] = {64, 32, 64, 160};
    for (int i = 0; i < 100; i++) {
      int want = i >= 48 && i < 52 ? middle[i - 48] : 0;
      assert(abs(pixels[i] - want) <= 1);
    }
    sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
    sk_canvas_draw_image(canvas, png, -1, -1, 2, 2, 0.5f);
    pixels = sk_surface_peek_pixels(surface, &len);
    assert(pixels[0] == 0 && pixels[1] == 0 && abs(pixels[2] - 128) <= 1 && abs(pixels[3] - 128) <= 1);
    sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
    sk_canvas_draw_image(canvas, jpeg, 0, 0, 2, 3, 1.f);
    pixels = sk_surface_peek_pixels(surface, &len);
    assert(abs(pixels[0] - 120) <= 2 && abs(pixels[1] - 60) <= 2 && abs(pixels[2] - 20) <= 2 && pixels[3] == 255);
    sk_surface_unref(surface);
  }
  sk_image_unref(png);
  sk_image_unref(jpeg);
}

static void assert_text_equivalent(const char *source, const char *const *pieces,
                                   int count, int style) {
  SkSurface *surface = sk_surface_make_raster_n32_premul(640, 100);
  assert(surface);
  SkCanvas *canvas = sk_surface_get_canvas(surface);
  SkPaint *paint = sk_paint_new();
  sk_paint_set_color(paint, sk_color_rgba(20, 30, 40, 255));
  sk_paint_set_text_size(paint, 28);
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  sk_canvas_draw_span(canvas, sk_text_range(source, strlen(source)), 8, 50, paint, style);
  size_t bytes = 0;
  const uint8_t *actual = sk_surface_peek_pixels(surface, &bytes);
  uint8_t *expected = malloc(bytes);
  assert(expected && actual);
  memcpy(expected, actual, bytes);
  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  float x = 8;
  for (int i = 0; i < count; i++) {
    sk_canvas_draw_span(canvas, sk_text_range(pieces[i], strlen(pieces[i])), x, 50, paint, style);
    x += sk_font_measure_span(sk_text_range(pieces[i], strlen(pieces[i])), 28, style);
  }
  assert(fabsf(sk_font_measure_span(sk_text_range(source, strlen(source)), 28, style) - (x - 8)) < .001f);
  actual = sk_surface_peek_pixels(surface, NULL);
  assert(memcmp(expected, actual, bytes) == 0);
  free(expected);
  sk_paint_delete(paint);
  sk_surface_unref(surface);
}

static void test_paragraph_ranges(void) {
  if (sk_font_measure_string("i", 24) == sk_font_measure_string("W", 24)) return;
  const char *prefix[] = {"אבגדהוז ", "אבגדהוז\n", ("\xe2\x80\xab" "abcdefg "),
    "abcdefg ", "abcdefg \xe2\x81\xa7", "אבגדהוז ", "abcdefg "};
  const char *line[] = {"AB אב", "AB אב", "AB אב", "אב AB", "AB אב", "12: AB אב!", "12 אב AB!"};
  const char *suffix[] = {"", "", "\xe2\x80\xac", "", "\xe2\x81\xa9", "", ""};
  const int rtl[] = {1, 0, 1, 0, 1, 1, 0};
  for (int i = 0; i < 7; i++) {
    char source[128], reference[128];
    snprintf(source, sizeof source, "%s%s%s", prefix[i], line[i], suffix[i]);
    snprintf(reference, sizeof reference, "%s%s", rtl[i] ? "\xe2\x80\x8f" : "\xe2\x80\x8e", line[i]);
    SkTextRange range = {source, strlen(source), strlen(prefix[i]), strlen(prefix[i]) + strlen(line[i])};
    float leading[128], trailing[128], ref_leading[128], ref_trailing[128];
    sk_font_text_positions(range, 24, 0, leading, trailing);
    sk_font_text_positions(sk_text_range(reference, strlen(reference)), 24, 0, ref_leading, ref_trailing);
    for (size_t byte = 0; byte <= strlen(line[i]); byte++) {
      assert(fabsf(leading[byte] - ref_leading[byte + 3]) < .001f);
      assert(fabsf(trailing[byte] - ref_trailing[byte + 3]) < .001f);
    }
    float width = sk_font_measure_span(range, 24, 0);
    assert(fabsf(width - sk_font_measure_span(sk_text_range(reference, strlen(reference)), 24, 0)) < .001f);
    SkSurface *actual = sk_surface_make_raster_n32_premul(256, 64);
    SkSurface *expected = sk_surface_make_raster_n32_premul(256, 64);
    assert(actual && expected);
    SkCanvas *a = sk_surface_get_canvas(actual), *b = sk_surface_get_canvas(expected);
    sk_canvas_clear(a, sk_color_argb(0)); sk_canvas_clear(b, sk_color_argb(0));
    SkPaint *paint = sk_paint_new();
    sk_paint_set_text_size(paint, 24); sk_paint_set_color(paint, sk_color_argb(0xff165ca0));
    sk_canvas_draw_span(a, range, 4, 40, paint, 0);
    sk_canvas_draw_span(b, sk_text_range(reference, strlen(reference)), 4, 40, paint, 0);
    assert(!memcmp(sk_surface_peek_pixels(actual, NULL), sk_surface_peek_pixels(expected, NULL), 256 * 64 * 4));
    sk_paint_delete(paint); sk_surface_unref(actual); sk_surface_unref(expected);
  }
}

static void test_paragraph_brackets(void) {
  if (sk_font_measure_string("i", 24) == sk_font_measure_string("W", 24)) return;
  const char *source = "WWWWWW אב[ אבגדה ] AB", *prefix = "WWWWWW אב[ ";
  const char *first = "אבגדה ]", *last = " AB";
  SkTextRange range = {source, strlen(source), strlen(prefix), strlen(source)};
  float leading[64], trailing[64];
  sk_font_text_positions(range, 24, 0, leading, trailing);
  size_t bracket = strlen("אבגדה ");
  assert(leading[bracket] > trailing[bracket]);
  float first_width = sk_font_measure_span(sk_text_range(first, strlen(first)), 24, 0);
  float width = first_width + sk_font_measure_span(sk_text_range(last, strlen(last)), 24, 0);
  assert(fabsf(width - sk_font_measure_span(range, 24, 0)) < .001f);
  SkSurface *actual = sk_surface_make_raster_n32_premul(256, 64);
  SkSurface *expected = sk_surface_make_raster_n32_premul(256, 64);
  assert(actual && expected);
  SkCanvas *a = sk_surface_get_canvas(actual), *b = sk_surface_get_canvas(expected);
  sk_canvas_clear(a, sk_color_argb(0)); sk_canvas_clear(b, sk_color_argb(0));
  SkPaint *paint = sk_paint_new();
  sk_paint_set_text_size(paint, 24); sk_paint_set_color(paint, sk_color_argb(0xff165ca0));
  sk_canvas_draw_span(a, range, 4, 40, paint, 0);
  sk_canvas_draw_span(b, sk_text_range(first, strlen(first)), 4, 40, paint, 0);
  sk_canvas_draw_span(b, sk_text_range(last, strlen(last)), 4 + first_width, 40, paint, 0);
  assert(!memcmp(sk_surface_peek_pixels(actual, NULL), sk_surface_peek_pixels(expected, NULL), 256 * 64 * 4));
  sk_paint_delete(paint); sk_surface_unref(actual); sk_surface_unref(expected);
}

static void test_shaped_text(void) {
  if (sk_font_measure_string("i", 28) == sk_font_measure_string("W", 28)) return;
  uint8_t breaks[16];
  sk_text_cluster_breaks("é", 3, breaks);
  assert(breaks[0] && !breaks[1] && !breaks[2] && breaks[3]);
  sk_text_cluster_breaks("fi", 2, breaks);
  assert(breaks[0] && !breaks[1] && breaks[2]);
  sk_text_cluster_breaks("لا", 4, breaks);
  assert(breaks[0] && !breaks[1] && !breaks[2] && !breaks[3] && breaks[4]);
  float leading[32], trailing[32];
  sk_font_text_positions(sk_text_range("אב", 4), 28, 0, leading, trailing);
  assert(leading[0] > leading[2] && leading[2] > leading[4]);
  assert(fabsf(leading[0] - sk_font_measure_string("אב", 28)) < .001f);
  assert(leading[4] == 0.f && trailing[3] == 0.f);
  sk_font_text_positions(sk_text_range("AV", 2), 28, 0, leading, trailing);
  assert(leading[1] < sk_font_measure_string("A", 28));
  sk_font_text_positions(sk_text_range("fi", 2), 28, 0, leading, trailing);
  assert(leading[0] < leading[1] && leading[1] < leading[2]);
  sk_font_text_positions(sk_text_range("fi", 2), 28, SK_TEXT_MONO, leading, trailing);
  assert(fabsf(leading[2] - sk_font_measure_span(sk_text_range("fi", 2), 28, SK_TEXT_MONO)) < .001f);
  assert(fabsf(leading[1] - sk_font_measure_span(sk_text_range("f", 1), 28, SK_TEXT_MONO)) < .001f);
  sk_font_text_positions(sk_text_range("é", 3), 28, 0, leading, trailing);
  assert(leading[1] == trailing[0] && leading[3] == trailing[0]);
  float separate = sk_font_measure_string("A", 28) + sk_font_measure_string("V", 28);
  assert(sk_font_measure_string("AV", 28) < separate - 1.f);
  const char *accent[] = {"é"};
  const char *ligature[] = {"ﬁ"};
  const char *joined[] = {"ﻻ"};
  const char *latin[] = {"ab ", "12", " ", "אב"};
  const char *arabic[] = {"AB 12", " ", "اب"};
  const char *isolate[] = {"A ", "אב", " B"};
  const char *brackets[] = {"X (", "אב", ")"};
  for (int style = 0; style <= 3; style++) {
    assert_text_equivalent("é", accent, 1, style);
    assert_text_equivalent("fi", ligature, 1, style);
    assert_text_equivalent("لا", joined, 1, style);
    assert_text_equivalent("ab אב 12", latin, 4, style);
    assert_text_equivalent("اب AB 12", arabic, 3, style);
    assert_text_equivalent("A ⁧אב⁩ B", isolate, 3, style);
    assert_text_equivalent("X (אב)", brackets, 3, style);
  }
  assert(sk_font_measure_span(sk_text_range("ab ignored", 2), 28, 0) == sk_font_measure_string("ab", 28));
}

static void test_font_fallback(void) {
  if (sk_font_measure_string("i", 32) == sk_font_measure_string("W", 32)) return;
  /* The pinned font files give these advances at weight 400 and size 32. */
  const char *source[] = {"नमस्ते", "कि", "क्षि", "สวัสดี", "ปู่", "中文漢字", "日本語"};
  const float widths[] = {67.584f, 32.864f, 31.232f, 72.064f, 19.36f, 128.f, 96.f};
  for (size_t i = 0; i < sizeof widths / sizeof widths[0]; i++) {
    size_t bytes = strlen(source[i]);
    for (int style = 0; style < 4; style++)
      assert(fabsf(sk_font_measure_span(sk_text_range(source[i], bytes), 32, style) - widths[i]) < .002f);
    float leading[64], trailing[64];
    sk_font_text_positions(sk_text_range(source[i], bytes), 32, 0, leading, trailing);
    assert(fabsf(leading[bytes] - widths[i]) < .002f);
    for (size_t j = 0; j <= bytes; j++) {
      assert(isfinite(leading[j]) && isfinite(trailing[j]));
      assert(leading[j] >= -.01f && trailing[j] <= widths[i] + .01f);
    }
  }
  uint8_t breaks[32];
  for (int i = 0; i < 2; i++) {
    const char *cluster = i ? "ปู่" : "क्षि";
    size_t bytes = strlen(cluster);
    sk_text_cluster_breaks(cluster, bytes, breaks);
    assert(breaks[0] && breaks[bytes]);
    for (size_t j = 1; j < bytes; j++) assert(!breaks[j]);
  }
  const char *mixed[] = {"A ", "中文", " ", "क्षि", " ", "ปู่"};
  assert_text_equivalent("A 中文 क्षि ปู่", mixed, 6, 0);
  /* An absent symbol does not remove a neighboring supported glyph. */
  const char *partial[] = {"中", "🫨", "文"};
  assert_text_equivalent("中🫨文", partial, 3, 0);
  SkSurface *surface = sk_surface_make_raster_n32_premul(320, 100);
  SkCanvas *canvas = sk_surface_get_canvas(surface);
  SkPaint *paint = sk_paint_new();
  sk_paint_set_text_size(paint, 32);
  sk_paint_set_color(paint, sk_color_rgba(0, 0, 0, 255));
  for (int style = 0; style < 4; style++) for (int i = 0; i < 7; i++) {
    sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
    float a, d;
    sk_font_span_metrics(sk_text_range(source[i], strlen(source[i])), 32, style, &a, &d);
    sk_canvas_draw_span(canvas, sk_text_range(source[i], strlen(source[i])), 40, 50, paint, style);
    const uint8_t *pixels = sk_surface_peek_pixels(surface, NULL);
    int ink = 0;
    for (int y = 0; y < 100; y++) for (int x = 0; x < 320; x++) {
      if (pixels[(y * 320 + x) * 4 + 3]) {
        ink++;
        assert(y >= (int)floorf(50 - a) && y < (int)ceilf(50 + d));
      }
    }
    assert(ink > 20);
  }
  sk_paint_delete(paint);
  sk_surface_unref(surface);
}

static void test_text_spans(void) {
  for (int gpu = 0; gpu <= sk_gpu_available(); gpu++) {
    SkSurface *surface = gpu ? sk_surface_make_gpu_n32_premul(240, 80) : sk_surface_make_raster_n32_premul(240, 80);
    assert(surface);
    SkCanvas *canvas = sk_surface_get_canvas(surface);
    SkPaint *paint = sk_paint_new();
    sk_paint_set_color(paint, sk_color_rgba(255, 0, 0, 255));
    sk_paint_set_text_size(paint, 24);
    const char *source = "Word ignored";
    for (int style = 0; style < 16; style++) {
      sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
      float width = sk_font_measure_span(sk_text_range(source, 4), 24, style);
      assert(width > 0 && width < 240);
      sk_canvas_draw_span(canvas, sk_text_range(source, 4), 4, 40, paint, style);
      const uint8_t *pixels = sk_surface_peek_pixels(surface, NULL);
      int ink = 0;
      for (int y = 0; y < 80; y++) for (int x = 0; x < 240; x++) {
        const uint8_t *p = pixels + (y * 240 + x) * 4;
        if (p[3]) {
          assert(x < 4 + width + 24);
          ink++;
        }
      }
      assert(ink > 0);
    }
    assert(sk_font_measure_span(sk_text_range("", 0), 24, 15) == 0);
    sk_paint_delete(paint);
    sk_surface_unref(surface);
  }
}

static void layer_pixel(const uint8_t *pixels, int x, int y, int r, int g, int b, int a) {
  const uint8_t *p = pixels + ((size_t)y * 20 + x) * 4;
  assert(abs((int)p[0] - r) <= 1 && abs((int)p[1] - g) <= 1 &&
    abs((int)p[2] - b) <= 1 && abs((int)p[3] - a) <= 1);
}

static void test_opacity_layers(int gpu) {
  SkSurface *surface = gpu ? sk_surface_make_gpu_n32_premul(20, 12) : sk_surface_make_raster_n32_premul(20, 12);
  assert(surface);
  SkCanvas *canvas = sk_surface_get_canvas(surface);
  SkPaint *paint = sk_paint_new();
  sk_canvas_clear(canvas, sk_color_rgba(255, 255, 255, 255));
  assert(sk_canvas_save_layer_alpha(canvas, 128));
  sk_paint_set_color(paint, sk_color_rgba(255, 0, 0, 255));
  sk_canvas_draw_rect(canvas, 0, 0, 12, 8, paint);
  sk_paint_set_color(paint, sk_color_rgba(0, 0, 255, 255));
  sk_canvas_draw_rect(canvas, 4, 0, 12, 8, paint);
  sk_canvas_restore(canvas);
  const uint8_t *pixels = sk_surface_peek_pixels(surface, NULL);
  layer_pixel(pixels, 1, 2, 255, 127, 127, 255);
  layer_pixel(pixels, 6, 2, 127, 127, 255, 255);
  layer_pixel(pixels, 18, 2, 255, 255, 255, 255);
  layer_pixel(pixels, 6, 10, 255, 255, 255, 255);

  sk_canvas_clear(canvas, sk_color_rgba(255, 255, 255, 255));
  sk_canvas_save(canvas);
  sk_canvas_clip_rect(canvas, 2, 1, 8, 8);
  assert(sk_canvas_save_layer_alpha(canvas, 128));
  assert(sk_canvas_save_layer_alpha(canvas, 128));
  sk_paint_set_color(paint, sk_color_rgba(255, 0, 0, 255));
  sk_canvas_draw_rect(canvas, 0, 0, 20, 12, paint);
  sk_canvas_save(canvas);
  sk_canvas_clip_rect(canvas, 5, 3, 2, 2);
  sk_canvas_restore(canvas);
  sk_canvas_restore(canvas);
  sk_canvas_restore(canvas);
  pixels = sk_surface_peek_pixels(surface, NULL);
  layer_pixel(pixels, 3, 2, 255, 191, 191, 255);
  layer_pixel(pixels, 1, 2, 255, 255, 255, 255);
  layer_pixel(pixels, 3, 10, 255, 255, 255, 255);
  sk_paint_set_color(paint, sk_color_rgba(0, 0, 255, 255));
  sk_canvas_draw_rect(canvas, 0, 0, 2, 2, paint);
  pixels = sk_surface_peek_pixels(surface, NULL);
  layer_pixel(pixels, 1, 1, 255, 255, 255, 255);
  sk_canvas_restore(canvas);
  sk_canvas_draw_rect(canvas, 0, 0, 2, 2, paint);
  pixels = sk_surface_peek_pixels(surface, NULL);
  layer_pixel(pixels, 1, 1, 0, 0, 255, 255);

  sk_canvas_clear(canvas, sk_color_rgba(0, 0, 0, 0));
  assert(sk_canvas_save_layer_alpha(canvas, 128));
  sk_paint_set_color(paint, sk_color_rgba(255, 0, 0, 128));
  sk_canvas_draw_rect(canvas, 0, 0, 20, 12, paint);
  sk_canvas_restore(canvas);
  pixels = sk_surface_peek_pixels(surface, NULL);
  layer_pixel(pixels, 1, 1, 64, 0, 0, 64);
  assert(sk_canvas_save_layer_alpha(canvas, 0));
  sk_paint_set_color(paint, sk_color_rgba(0, 255, 0, 255));
  sk_canvas_draw_rect(canvas, 0, 0, 20, 12, paint);
  sk_canvas_restore(canvas);
  pixels = sk_surface_peek_pixels(surface, NULL);
  layer_pixel(pixels, 1, 1, 64, 0, 0, 64);
  assert(sk_canvas_save_layer_alpha(canvas, 255));
  sk_canvas_save(canvas);
  sk_paint_delete(paint);
  sk_surface_unref(surface);
}

int main(void) {
  test_opacity_layers(0);
  if (sk_gpu_available()) test_opacity_layers(1);
  test_shaped_text();
  test_paragraph_ranges();
  test_paragraph_brackets();
  test_font_fallback();
  test_text_spans();
  test_images();
  SkSurface *surf = sk_surface_make_raster_n32_premul(64, 32);
  SkCanvas *canvas;
  SkPaint *paint;
  uint8_t *png = NULL;
  size_t png_len = 0;
  const uint8_t *px;
  size_t px_len = 0;

  assert(surf);
  canvas = sk_surface_get_canvas(surf);
  assert(canvas);
  paint = sk_paint_new();
  assert(paint);

  sk_canvas_clear(canvas, sk_color_rgba(20, 40, 80, 255));
  sk_paint_set_color(paint, sk_color_rgba(240, 240, 240, 255));
  sk_canvas_draw_rect(canvas, 8, 8, 48, 16, paint);
  sk_paint_set_color(paint, sk_color_rgba(20, 40, 80, 255));
  sk_paint_set_text_size(paint, 8.f);
  sk_canvas_draw_string(canvas, "Scuzz Lang", 14, 20, paint);

  float measured = sk_font_measure_string("Scuzz", 8.f);
  float cell;
  float ii;
  float ww;
  assert(measured > 0.f);
  assert(sk_font_measure_string("", 8.f) == 0.f);
  assert(sk_paint_get_text_size(paint) == 8.f);
  /* sk_sw is monospace (5 * 8); real Skia is proportional - both OK. */
  (void)measured;
  cell = sk_font_mono_cell(8.f);
  assert(cell > 0.f);
  assert(sk_font_measure_string_mono("I", 8.f) == sk_font_measure_string_mono("W", 8.f));
  ii = sk_font_measure_string_mono("ii", 8.f);
  ww = sk_font_measure_string_mono("WW", 8.f);
  assert(ii == ww);
  assert(ii == 2.f * cell);
  sk_paint_set_color(paint, sk_color_rgba(240, 240, 240, 255));
  sk_canvas_draw_mono_string(canvas, "ii", 8, 28, paint);

  px = sk_surface_peek_pixels(surf, &px_len);
  assert(px && px_len == 64 * 32 * 4);
  /* Surface has ink somewhere after clear + rect + string. */
  {
    size_t i;
    int saw_light = 0;
    for (i = 0; i < px_len; i += 4) {
      if (px[i] > 200 && px[i + 1] > 200 && px[i + 2] > 200) {
        saw_light = 1;
        break;
      }
    }
    assert(saw_light);
  }

  /* Peek is row-major RGBA. Red is byte 0 with no channel remap. */
  {
    SkSurface *reds = sk_surface_make_raster_n32_premul(4, 4);
    SkCanvas *rc;
    const uint8_t *rp;
    size_t rn = 0;
    assert(reds);
    rc = sk_surface_get_canvas(reds);
    sk_paint_set_color(paint, sk_color_rgba(255, 0, 0, 255));
    sk_canvas_draw_rect(rc, 0, 0, 4, 4, paint);
    rp = sk_surface_peek_pixels(reds, &rn);
    assert(rp && rn >= 4);
    assert(rp[0] == 255 && rp[1] < 50 && rp[2] < 50);
    sk_surface_unref(reds);
  }

  /* UTF-8: one code point is one cell on sk_sw. Never two cells for "é". */
  {
    float e = sk_font_measure_string("e", 8.f);
    float acute = sk_font_measure_string("é", 8.f);
    float ee = sk_font_measure_string("ee", 8.f);
    assert(e > 0.f);
    assert(acute > 0.f);
    assert(acute != ee);
    if (sk_font_measure_string("i", 8.f) == sk_font_measure_string("W", 8.f))
      assert(acute == e);
  }

  /* draw_mono("éx"): x stays in cell 1. Cell 2 stays empty. */
  {
    SkSurface *mono = sk_surface_make_raster_n32_premul(48, 16);
    SkCanvas *mc;
    const uint8_t *mp;
    size_t mn = 0;
    int cw;
    assert(mono);
    mc = sk_surface_get_canvas(mono);
    sk_canvas_clear(mc, sk_color_rgba(0, 0, 0, 255));
    sk_paint_set_color(paint, sk_color_rgba(240, 240, 240, 255));
    sk_paint_set_text_size(paint, 8.f);
    sk_canvas_draw_mono_string(mc, "éx", 0, 8, paint);
    mp = sk_surface_peek_pixels(mono, &mn);
    assert(mp && mn == 48 * 16 * 4);
    cw = (int)(sk_font_mono_cell(8.f) + 0.5f);
    if (cw < 1)
      cw = 1;
    assert(cell_has_ink(mp, 48, 0, cw, 0, 16));
    assert(cell_has_ink(mp, 48, cw, cw * 2, 0, 16));
    assert(!cell_has_ink(mp, 48, cw * 2, cw * 3, 0, 16));
    sk_surface_unref(mono);
  }

  /* save / clip / restore on the archive this test linked. */
  {
    SkSurface *clip = sk_surface_make_raster_n32_premul(32, 32);
    SkCanvas *cc;
    const uint8_t *cp;
    size_t cn = 0;
    assert(clip);
    cc = sk_surface_get_canvas(clip);
    sk_canvas_clear(cc, sk_color_rgba(20, 40, 80, 255));
    sk_canvas_save(cc);
    sk_canvas_clip_rect(cc, 8, 8, 12, 12);
    sk_paint_set_color(paint, sk_color_rgba(240, 240, 240, 255));
    sk_canvas_draw_rect(cc, 0, 0, 32, 32, paint);
    sk_canvas_restore(cc);
    cp = sk_surface_peek_pixels(clip, &cn);
    assert(cp && cn == 32 * 32 * 4);
    assert(cp[(10 * 32 + 10) * 4] == 240);
    assert(cp[(10 * 32 + 10) * 4 + 1] == 240);
    assert(cp[(10 * 32 + 10) * 4 + 2] == 240);
    assert(cp[0] == 20);
    assert(cp[1] == 40);
    assert(cp[2] == 80);
    assert(cp[(20 * 32 + 20) * 4] == 20);
    sk_surface_unref(clip);
  }

  assert(sk_encode_png(surf, &png, &png_len));
  assert(png_len > 8);
  assert(png[0] == 137 && png[1] == 80 && png[2] == 78 && png[3] == 71);

  free(png);
  sk_paint_delete(paint);
  (void)canvas;
  sk_surface_unref(surf);
  puts("ffi-skia tests ok");
  return 0;
}
