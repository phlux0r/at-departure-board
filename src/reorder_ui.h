#pragma once
#include <stdint.h>

#include "layout.h"  // Rect

// Touch-driven lane reordering: a small status-bar toggle reveals up/down
// chevrons on each lane, tapping one swaps that lane with its neighbour.
//
// This never reorders, reads, or otherwise touches config_watches() or the
// fetch task's data - it only permutes config_lane_order() (config.h), which
// is safe to change live for exactly that reason. It draws nothing itself;
// ui.cpp asks it what to draw each frame via reorder_ui_active() and the
// chevron-rect functions below, which double as both hit zones and draw
// rects so the two can never drift apart.
//
// Geometry note: an early, inaccurate touch calibration made this panel
// measurably fail to register near every screen edge; recalibrating fixed
// it (docs/hardware-notes.md), so the rects below sit close to where they
// visually belong (toggle in the status bar, chevrons in each lane's actual
// bottom-right corner) rather than inset away from the edges.

void reorder_ui_begin();  // loads config_lane_order() as the working copy

// Call once per frame with the current touch sample. down_edge must be a
// debounced, confirmed press (touch.h) - a held finger must not repeat a
// swap every frame.
//
// slot_to_order maps each on-screen lane slot to its index in order(), and
// n_slots is how many lanes are actually drawn. The two differ whenever a
// lane is hidden (config.h): order() stays a permutation of every published
// watch, hidden ones included, so a swap has to move entries at those
// indices rather than at the screen slots - otherwise hiding a lane would
// silently reshuffle the ones still on screen.
void reorder_ui_touch(bool down_edge, int x, int y, const uint8_t* slot_to_order, int n_slots,
                      uint32_t now_ms);

// Auto-exits reorder mode after a period with no taps, so it can't get
// stuck open if the toggle is missed on the way out.
void reorder_ui_tick(uint32_t now_ms);

bool reorder_ui_active();
void reorder_ui_close();  // settings_ui.cpp: the two pages are never up at once

// order()[slot] is the watch index ui.cpp should draw in that screen slot.
// Identical to config_lane_order() except immediately after a swap, before
// config_set_lane_order()'s NVS write has to complete.
const uint8_t* reorder_ui_order();

Rect reorder_toggle_rect();
Rect reorder_lane_chevron(int slot, int n, bool up);  // zeroed Rect if slot
                                                       // has no such chevron
                                                       // (top has no up, etc)
