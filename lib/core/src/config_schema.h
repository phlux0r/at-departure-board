#pragma once
#include <stddef.h>
#include <stdint.h>

#include "live.h"   // WatchConfig
#include "model.h"  // MAX_WATCHES

// Pure config logic: no Arduino headers, no NVS, no heap. Everything here is
// exercised by test/test_config_schema under `pio test -e native`.

constexpr int CFG_LOCATION_CAP = 24;  // matches Board::location
constexpr int CFG_FIELD_CAP = 32;
constexpr int CFG_GROUP_NAME_CAP = 24;
constexpr int MAX_GROUPS = 4;

// Four groups of four watches with every field at its cap serialise to about
// 3.7 KB. Deliberately well clear of that - and note the stored document is an
// NVS *blob*, not a string, because nvs_set_str caps a value at 4000 bytes and
// a worst-case config would sit uncomfortably close to it (config.cpp).
constexpr int CFG_JSON_CAP = 4608;

// v1 was a flat list of watches with no groups. cfg_parse still accepts it and
// lifts it into a single group, because a firmware update must not throw away
// the stops someone already configured.
constexpr uint8_t CFG_SCHEMA_VERSION = 2;
constexpr uint8_t CFG_SCHEMA_VERSION_V1 = 1;

struct CfgWatch {
  char label[CFG_FIELD_CAP];
  char stop_code[CFG_FIELD_CAP];
  char route_short_name[CFG_FIELD_CAP];  // "" means any route (spec 3)
  char toward_stop_code[CFG_FIELD_CAP];  // "" means no direction filter
  bool enabled;
};

// A named set of watches. Exactly one group is active at a time - the board
// only ever fetches and draws that one - so the rest are saved arrangements
// to switch between, not extra lanes.
struct CfgGroup {
  char name[CFG_GROUP_NAME_CAP];
  uint8_t n_watches;  // every watch in the group, enabled or not
  CfgWatch watches[MAX_WATCHES];
};

// Plain data - no pointers into itself, so this is safe to copy.
struct Config {
  char location[CFG_LOCATION_CAP];
  uint8_t theme;
  uint8_t n_groups;
  uint8_t active_group;  // always < n_groups; clamped on parse, never rejected
  CfgGroup groups[MAX_GROUPS];
};

enum class CfgError : uint8_t {
  Ok,
  BadJson,          // would not deserialise
  BadVersion,       // "v" missing, or a version this build does not know
  TooManyWatches,   // a group with more than MAX_WATCHES
  NoWatches,        // a group with zero watches, or zero enabled ones
  MissingStopCode,  // a watch with an empty stop_code
  FieldTooLong,     // a string that will not fit its buffer
  LocationTooLong,
  TooManyGroups,    // more than MAX_GROUPS
  NoGroups,         // zero groups
  GroupNameTooLong,
};

const char* cfg_error_text(CfgError e);

// Fills *out only on CfgError::Ok; *out is untouched otherwise, so a caller
// can keep its previous config on a failed save.
// theme is clamped to [0, theme_max), never rejected - a theme can disappear
// when the generated table changes and that must not brick the config.
CfgError cfg_parse(const char* json, Config* out, uint8_t theme_max);

// Serialises cfg as the document cfg_parse accepts. Returns the length
// written, or 0 if cap was too small (out is then left empty).
size_t cfg_serialize(const Config& cfg, char* out, size_t cap);

// Fills out[] with the ACTIVE group's enabled watches, compacted and in
// order, and returns how many. Every other group is ignored: switching which
// one is active is what changes the board, and that needs the fetch task
// restarted, so it happens through a save and a reboot (config.h).
//
// The WatchConfig pointers point INTO cfg, so cfg must outlive out[] and
// out[] must be refilled whenever cfg changes.
uint8_t cfg_publish(const Config& cfg, WatchConfig out[MAX_WATCHES]);
