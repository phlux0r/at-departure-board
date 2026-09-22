#pragma once
#include <stdint.h>

#include "layout.h"  // Rect

// The settings page: a cog in the status bar opens a full-screen page with
// the theme, the backlight brightness, the group picker, and an on/off row
// per watch in the active group.
//
// Same split as reorder_ui.h: this owns the state and the geometry, ui.cpp
// does the drawing, and the rects below are both the hit zones and the draw
// rects so the two can't drift apart.
//
// Theme, brightness and the lane toggles all apply live. Switching group is
// the one thing here that cannot: a different group means different stops,
// and the fetch task is reading the current ones with no lock, so it
// persists and reboots (config.h).

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
Rect settings_top_rect();  // theme and brightness share a row, for the space
Rect settings_theme_rect();
Rect settings_bright_minus_rect();
Rect settings_bright_plus_rect();

// Up to MAX_GROUPS chips side by side; zeroed past what n_groups fills. Side
// by side rather than stacked because the lane rows below need the height.
Rect settings_group_rect(int index, int n_groups);

Rect settings_lane_rect(int index);  // zeroed past what fits on the page
