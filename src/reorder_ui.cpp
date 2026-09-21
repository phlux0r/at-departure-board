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
  // Centred in the gap between the location text and the live/stale label,
  // well clear of the left and right edge dead zones either way. See
  // reorder_ui.h for why this is below y=18, not inside the status bar.
  return {236, 12, 268, 34};
}

Rect reorder_lane_chevron(int slot, int n, bool up) {
  Rect empty{};
  if (up && slot == 0) return empty;
  if (!up && slot == n - 1) return empty;

  Rect r{};
  if (!lane_rect(slot, n, &r)) return empty;

  // The bottom lane needs its own extra margin - checked by slot, not by
  // comparing r.y1 to H, since integer-division rounding can leave the last
  // lane's rect a couple of rows short of the true screen edge (see
  // test_lane_rects_split_the_space_below_the_status_bar). Every other
  // lane's card bottom is already clear of the dead zone.
  const int bottom = (slot == n - 1) ? H - 22 : r.y1;
  const int x0 = W - MARKER_INSET + 20;
  const int y0 = up ? bottom - 30 : bottom - 14;
  return {x0, y0, x0 + 16, y0 + 12};
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
