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

// The label drawn on the panel, which is the ACTIVE GROUP's name - groups
// replaced the board-wide location, since every group already needed one and
// two names for the same caption is one too many. Still called "location"
// because that is the Board field it fills.
const char* config_location();

uint8_t config_theme();
void config_set_theme(uint8_t t);  // applies live AND persists

// The groups, for the settings page's picker. Names are the panel captions;
// config_group_name() returns "" for an index that does not exist.
uint8_t config_n_groups();
uint8_t config_active_group();
const char* config_group_name(uint8_t index);

// Persists a new active group to NVS and NOTHING else - the in-RAM config is
// deliberately untouched, exactly as config_save_json() leaves it, because a
// different group means different stops and the fetch task is reading the
// current ones on core 0 with no lock. THE CALLER MUST REBOOT: the new group
// arrives via config_begin() on the way back up, at the one moment nothing
// else is reading it.
//
// False (and nothing written) if the index does not exist or is already
// active, so a stray tap cannot cost a reboot.
bool config_set_active_group(uint8_t index);

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
