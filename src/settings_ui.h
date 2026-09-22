#pragma once
#include <stdint.h>

#include "layout.h"  // Rect

// The settings page: a cog in the status bar opens a full-screen page with
// the theme, the backlight brightness, and an on/off row per watch.
//
// Same split as reorder_ui.h: this owns the state and the geometry, ui.cpp
// does the drawing, and the rects below are both the hit zones and the draw
// rects so the two can't drift apart. Nothing here touches config_watches()
// or the fetch task's data either - theme and brightness apply live, and a
// lane toggle is a display-level hide that only reaches the fetcher on the
// next boot (config.h).

void settings_ui_begin();

bool settings_ui_active();

// Call once per frame with the current touch sample; down_edge must be a
// debounced, confirmed press (touch.h), not a raw read.
//
// n_lanes is config_n_watches() - every *published* watch, including ones
// currently hidden, because a hidden lane still needs a row to be switched
// back on with.
void settings_ui_touch(bool down_edge, int x, int y, int n_lanes, uint32_t now_ms);

// Auto-closes after a while with no taps, so the page can't be left open
// over the board. Longer than reorder mode's: this one gets read, not just
// tapped through.
void settings_ui_tick(uint32_t now_ms);

Rect settings_cog_rect();  // in the status bar, left of the reorder toggle

// All of these are only meaningful while settings_ui_active().
Rect settings_close_rect();
Rect settings_theme_rect();
Rect settings_bright_rect();  // the row; the two buttons below sit inside it
Rect settings_bright_minus_rect();
Rect settings_bright_plus_rect();
Rect settings_lane_rect(int index);  // zeroed past what fits on the page
