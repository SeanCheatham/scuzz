#include "scalui_ui.h"

static const SuTheme k_default_theme = {
    .background = 0xFFF5F5F5u,
    .surface = 0xFFFFFFFFu,
    .foreground = 0xFF1A1A1Au,
    .primary = 0xFF142850u,
    .on_primary = 0xFFF0F0F0u,
    .border = 0xFFB0B0B0u,
    .muted = 0xFF6A6A6Au,
    .pad = 12.f,
    .gap = 8.f,
    .control_h = 32.f,
    .font_px = 8.f,
};

const SuTheme *su_theme_default(void) { return &k_default_theme; }
