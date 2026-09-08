#include "scuzz_ui.h"

static const SzTheme k_default_theme = {
    .background = 0xFFF3EFE3u,
    .surface = 0xFFFFFCF4u,
    .foreground = 0xFF24231Fu,
    .primary = 0xFFE8EF48u,
    .on_primary = 0xFF24231Fu,
    .border = 0xFF24231Fu,
    .muted = 0xFF656157u,
    .accent = 0xFF923D24u,
    .selection = 0xFFE8EF48u,
    .pad = 12.f,
    .gap = 8.f,
    .control_h = 40.f,
    .font_px = 14.f,
};

const SzTheme *sz_theme_default(void) { return &k_default_theme; }

int64_t sz_theme_accent(void) { return (int64_t)(uint32_t)k_default_theme.accent; }
int64_t sz_theme_primary(void) { return (int64_t)(uint32_t)k_default_theme.primary; }
int64_t sz_theme_muted(void) { return (int64_t)(uint32_t)k_default_theme.muted; }
int64_t sz_theme_foreground(void) {
  return (int64_t)(uint32_t)k_default_theme.foreground;
}

int64_t sz_color_rgb(int64_t r, int64_t g, int64_t b) {
  uint32_t rr = (uint32_t)(r & 255);
  uint32_t gg = (uint32_t)(g & 255);
  uint32_t bb = (uint32_t)(b & 255);
  return (int64_t)(0xFF000000u | (rr << 16) | (gg << 8) | bb);
}

int64_t sz_color_rgba(int64_t r, int64_t g, int64_t b, int64_t a) {
  uint32_t rr = (uint32_t)(r & 255);
  uint32_t gg = (uint32_t)(g & 255);
  uint32_t bb = (uint32_t)(b & 255);
  uint32_t aa = (uint32_t)(a & 255);
  return (int64_t)((aa << 24) | (rr << 16) | (gg << 8) | bb);
}
