#include "reorder_ui.h"

#include <string.h>

#include "config.h"
#include "model.h"  // MAX_WATCHES

namespace {

constexpr uint32_t IDLE_TIMEOUT_MS = 15000;

bool g_active = false;
uint32_t g_last_touch_ms = 0;
uint8_t g_order[MAX_WATCHES];

// A zeroed Rect (width 0) never contains a real touch, so this also does
// the "does this chevron exist" check for the caller.
bool rect_contains(Rect r, int x, int y) {
  return r.x1 > r.x0 && x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1;
}

}  // namespace

void reorder_ui_begin() { memcpy(g_order, config_lane_order(), sizeof g_order); }

Rect reorder_toggle_rect() {
  // Inside the status bar, in the gap between the location text and the
  // live/stale label - shifted left of that label's own space (which runs
  // to about x=266, status_bar() in ui.cpp) so it doesn't sit under the
  // "live"/"stale Nm" text. An earlier calibration was inaccurate enough
  // that touch measurably missed the ~20px nearest every edge, which
  // pushed this below the (18px-tall) status bar entirely - recalibrating
  // fixed that (docs/hardware-notes.md), so it moved back in.
  return {194, 3, 216, 15};
}

Rect reorder_lane_chevron(int slot, int n, bool up) {
  Rect empty{};
  if (up && slot == 0) return empty;
  if (!up && slot == n - 1) return empty;

  Rect r{};
  if (!lane_rect(slot, n, &r)) return empty;

  // The card's own bottom-right corner (MARGIN clears its rounded edge -
  // see draw_lane) - no longer inset further for the touch panel's edge
  // accuracy, which recalibrating fixed (docs/hardware-notes.md).
  const int x1 = r.x1 - MARGIN - 2;
  const int down_y1 = r.y1 - 3;
  const int down_y0 = down_y1 - 12;
  const int up_y1 = down_y0 - 2;
  const int up_y0 = up_y1 - 12;
  return up ? Rect{x1 - 14, up_y0, x1, up_y1} : Rect{x1 - 14, down_y0, x1, down_y1};
}

void reorder_ui_touch(bool down_edge, int x, int y, int n, uint32_t now_ms) {
  if (!down_edge) return;  // act on the press edge only
  g_last_touch_ms = now_ms;

  if (rect_contains(reorder_toggle_rect(), x, y)) {
    g_active = !g_active;
    return;
  }
  if (!g_active) return;

  for (int slot = 0; slot < n; slot++) {
    int other;
    if (rect_contains(reorder_lane_chevron(slot, n, true), x, y)) {
      other = slot - 1;
    } else if (rect_contains(reorder_lane_chevron(slot, n, false), x, y)) {
      other = slot + 1;
    } else {
      continue;
    }
    const uint8_t tmp = g_order[slot];
    g_order[slot] = g_order[other];
    g_order[other] = tmp;
    config_set_lane_order(g_order);  // live + persisted, same pattern as the theme
    break;
  }
}

void reorder_ui_tick(uint32_t now_ms) {
  if (g_active && now_ms - g_last_touch_ms > IDLE_TIMEOUT_MS) g_active = false;
}

bool reorder_ui_active() { return g_active; }
const uint8_t* reorder_ui_order() { return g_order; }
