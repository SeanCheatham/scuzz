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
    .line_gap = 6.f,
};

const SzTheme *sz_theme_default(void) { return &k_default_theme; }

int64_t sz_theme_accent(void) { return (int64_t)(uint32_t)k_default_theme.accent; }
int64_t sz_icon_book(void) { return (int64_t)'B'; }
int64_t sz_icon_code(void) { return (int64_t)'{'; }
int64_t sz_icon_link(void) { return (int64_t)'~'; }
int64_t sz_icon_web(void) { return (int64_t)'W'; }
int64_t sz_icon_gui(void) { return (int64_t)'['; }
int64_t sz_icon_install(void) { return (int64_t)'i'; }
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

SzTheme *sz_theme_colors(int64_t background, int64_t surface, int64_t foreground,
                        int64_t primary, int64_t on_primary, int64_t border,
                        int64_t muted, int64_t accent, int64_t selection) {
  SzTheme *theme = sz_rc_alloc(sizeof *theme, SZ_RC_RAW);
  *theme = *sz_theme_default();
  theme->background = (uint32_t)background;
  theme->surface = (uint32_t)surface;
  theme->foreground = (uint32_t)foreground;
  theme->primary = (uint32_t)primary;
  theme->on_primary = (uint32_t)on_primary;
  theme->border = (uint32_t)border;
  theme->muted = (uint32_t)muted;
  theme->accent = (uint32_t)accent;
  theme->selection = (uint32_t)selection;
  return theme;
}

SzTheme *sz_theme_light(void) {
  const SzTheme *t = sz_theme_default();
  return sz_theme_colors(t->background, t->surface, t->foreground, t->primary,
                         t->on_primary, t->border, t->muted, t->accent, t->selection);
}

SzTheme *sz_theme_dark(void) {
  return sz_theme_colors(0xFF24231Fu, 0xFF302E28u, 0xFFF3EFE3u, 0xFFE8EF48u,
                         0xFF24231Fu, 0xFFC6C0B1u, 0xFFBDB6A5u, 0xFFFFB49Bu,
                         0xFF5E641Cu);
}

SzTheme *sz_theme_metrics(const SzTheme *source, int64_t font_size, int64_t line_gap,
                          int64_t padding, int64_t gap, int64_t control_height) {
  if (font_size < 1 || line_gap < 0 || padding < 0 || gap < 0 ||
      control_height < font_size || line_gap > control_height - font_size)
    sz_panic("Theme metrics need positive text size and enough control height");
  SzTheme *theme = sz_rc_alloc(sizeof *theme, SZ_RC_RAW);
  *theme = source ? *source : *sz_theme_default();
  theme->font_px = (float)font_size;
  theme->line_gap = (float)line_gap;
  theme->pad = (float)padding;
  theme->gap = (float)gap;
  theme->control_h = (float)control_height;
  return theme;
}
