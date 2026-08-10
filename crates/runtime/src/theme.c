#include "scalui_ui.h"

static const SuTheme k_default_theme = {
    .background = 0xFFF5F5F5u,
    .surface = 0xFFFFFFFFu,
    .foreground = 0xFF1A1A1Au,
    .primary = 0xFF142850u,
    .on_primary = 0xFFF0F0F0u,
    .border = 0xFFB0B0B0u,
    .muted = 0xFF6A6A6Au,
    .accent = 0xFF142850u,   /* matches primary — no golden drift */
    .disabled = 0xFF6A6A6Au, /* matches muted */
    .pad = 12.f,
    .gap = 8.f,
    .control_h = 32.f,
    .font_px = 8.f,
    .radius = 0.f, /* 0 preserves Phase 2–5 goldens; gallery may override */
};

const SuTheme *su_theme_default(void) { return &k_default_theme; }

int64_t su_theme_accent(void) { return (int64_t)(uint32_t)k_default_theme.accent; }
int64_t su_theme_primary(void) { return (int64_t)(uint32_t)k_default_theme.primary; }
int64_t su_theme_muted(void) { return (int64_t)(uint32_t)k_default_theme.muted; }
int64_t su_theme_foreground(void) {
  return (int64_t)(uint32_t)k_default_theme.foreground;
}

int64_t su_color_rgb(int64_t r, int64_t g, int64_t b) {
  uint32_t rr = (uint32_t)(r & 255);
  uint32_t gg = (uint32_t)(g & 255);
  uint32_t bb = (uint32_t)(b & 255);
  return (int64_t)(0xFF000000u | (rr << 16) | (gg << 8) | bb);
}
