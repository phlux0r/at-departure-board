#include "settings_ui.h"

#include "config.h"
#include "model.h"  // MAX_WATCHES
#include "reorder_ui.h"
#include "theme.h"

namespace {

constexpr uint32_t IDLE_TIMEOUT_MS = 30000;
constexpr uint8_t BRIGHT_STEP = 26;  // ~10% a tap, so full range is ~9 taps

bool g_active = false;
uint32_t g_last_touch_ms = 0;

bool rect_contains(Rect r, int x, int y) {
  return r.x1 > r.x0 && x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1;
}

}  // namespace

void settings_ui_begin() { g_active = false; }

bool settings_ui_active() { return g_active; }

Rect settings_cog_rect() {
  // Left of the reorder toggle, in the same status-bar band and the same
  // size, so the two read as a pair.
  const Rect t = reorder_toggle_rect();
  return {t.x0 - 30, t.y0, t.x0 - 8, t.y1};
}

Rect settings_close_rect() { return {286, 2, 314, 26}; }
Rect settings_theme_rect() { return {8, 30, 312, 60}; }
Rect settings_bright_rect() { return {8, 64, 312, 94}; }
Rect settings_bright_minus_rect() { return {180, 64, 216, 94}; }
Rect settings_bright_plus_rect() { return {268, 64, 304, 94}; }

Rect settings_lane_rect(int index) {
  if (index < 0 || index >= MAX_WATCHES) return Rect{};
  const int y0 = 108 + index * 31;
  return {8, y0, 312, y0 + 28};
}

void settings_ui_touch(bool down_edge, int x, int y, int n_lanes, uint32_t now_ms) {
  if (!down_edge) return;

  if (!g_active) {
    if (rect_contains(settings_cog_rect(), x, y)) {
      g_active = true;
      g_last_touch_ms = now_ms;
      reorder_ui_close();  // the two pages are never up at once
    }
    return;
  }

  g_last_touch_ms = now_ms;

  if (rect_contains(settings_close_rect(), x, y) || rect_contains(settings_cog_rect(), x, y)) {
    g_active = false;
    return;
  }

  if (rect_contains(settings_theme_rect(), x, y)) {
    const uint8_t n = theme_count();
    if (n > 0) config_set_theme(static_cast<uint8_t>((config_theme() + 1) % n));
    return;
  }

  if (rect_contains(settings_bright_minus_rect(), x, y)) {
    const uint8_t b = config_brightness();
    config_set_brightness(b > BRIGHTNESS_MIN + BRIGHT_STEP ? static_cast<uint8_t>(b - BRIGHT_STEP)
                                                           : BRIGHTNESS_MIN);
    return;
  }
  if (rect_contains(settings_bright_plus_rect(), x, y)) {
    const uint8_t b = config_brightness();
    config_set_brightness(b > 255 - BRIGHT_STEP ? 255 : static_cast<uint8_t>(b + BRIGHT_STEP));
    return;
  }

  const int lanes = n_lanes < MAX_WATCHES ? n_lanes : MAX_WATCHES;
  for (int i = 0; i < lanes; i++) {
    if (!rect_contains(settings_lane_rect(i), x, y)) continue;
    // Refused when it would hide the last visible lane - config.cpp decides
    // that, and the row simply doesn't change.
    config_set_lane_visible(static_cast<uint8_t>(i), !config_lane_visible()[i]);
    return;
  }
}

void settings_ui_tick(uint32_t now_ms) {
  if (g_active && now_ms - g_last_touch_ms > IDLE_TIMEOUT_MS) g_active = false;
}
