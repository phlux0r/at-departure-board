#pragma once
#include <stdint.h>

#include "config_schema.h"

// The live configuration: NVS-backed, seeded from src/watch_config.h when NVS
// holds nothing valid. Spec section 6 - nothing but the theme (and, now, the
// lane display order) is written at runtime, because the fetch task reads
// the watches on core 0 with no lock.

void config_begin();  // call once in setup(), before fetcher_begin()

const WatchConfig* config_watches();
uint8_t config_n_watches();
const char* config_location();

uint8_t config_theme();
void config_set_theme(uint8_t t);  // applies live AND persists

// Which watch is drawn in each screen slot: order()[slot] is a watch index,
// 0 <= index < config_n_watches(). Only the first config_n_watches() entries
// are meaningful. This is display order only - it never reorders, reads, or
// otherwise touches config_watches() itself, so it's safe to change with the
// fetch task running. Written by the touch reorder UI (src/reorder_ui.cpp),
// never by the web setup page.
const uint8_t* config_lane_order();
void config_set_lane_order(const uint8_t order[MAX_WATCHES]);  // applies live AND persists

// Backlight level, 0-255. Applies live (backlight_set) and persists.
// Never goes below BRIGHTNESS_MIN: a board dimmed to nothing looks broken, and
// the only way back is the setting you can no longer read.
constexpr uint8_t BRIGHTNESS_MIN = 25;
uint8_t config_brightness();
void config_set_brightness(uint8_t level);

// Which published watches are currently drawn. visible()[i] refers to
// config_watches()[i]. Hiding is a DISPLAY-level thing so it can happen live:
// the fetch task keeps fetching a hidden watch (which is why re-showing one is
// instant), and only the next boot drops it, when the persisted `enabled` bit
// below feeds cfg_publish.
//
// Returns false, changing nothing, if this would hide the last visible lane -
// a board showing no lanes at all is never what the tap meant.
const bool* config_lane_visible();
bool config_set_lane_visible(uint8_t index, bool visible);

// Validates, then persists to NVS ONLY. The in-RAM config is deliberately not
// updated: the caller reboots so config_begin() picks the new values up at the
// one moment nothing else is reading them.
CfgError config_save_json(const char* json);

size_t config_to_json(char* out, size_t cap);
