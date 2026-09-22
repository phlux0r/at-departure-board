#include "config_schema.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

namespace {

// false when src will not fit dst (cap includes the NUL).
bool copy_field(char* dst, size_t cap, const char* src) {
  if (src == nullptr) src = "";
  if (strlen(src) >= cap) return false;
  strcpy(dst, src);
  return true;
}

}  // namespace

const char* cfg_error_text(CfgError e) {
  switch (e) {
    case CfgError::Ok: return "ok";
    case CfgError::BadJson: return "could not parse the config as JSON";
    case CfgError::BadVersion: return "config version is not supported";
    case CfgError::TooManyWatches: return "too many watches (maximum four)";
    case CfgError::NoWatches: return "at least one enabled watch is required";
    case CfgError::MissingStopCode: return "every watch needs a stop code";
    case CfgError::FieldTooLong: return "a watch field is too long";
    case CfgError::TooManyGroups: return "too many groups (maximum four)";
    case CfgError::NoGroups: return "at least one group is required";
    case CfgError::GroupNameTooLong: return "the group name is too long";
  }
  return "unknown error";
}

namespace {

// Reads one group's "watches" array into g. Shared by both schema versions:
// v1's top-level watches array is exactly a v2 group's, minus the name.
CfgError parse_watches(JsonArrayConst ws, CfgGroup* g) {
  if (ws.size() > MAX_WATCHES) return CfgError::TooManyWatches;

  uint8_t n_enabled = 0;
  for (JsonObjectConst w : ws) {
    CfgWatch& d = g->watches[g->n_watches];
    if (!copy_field(d.label, sizeof d.label, w["label"] | "") ||
        !copy_field(d.stop_code, sizeof d.stop_code, w["stop_code"] | "") ||
        !copy_field(d.route_short_name, sizeof d.route_short_name,
                    w["route_short_name"] | "") ||
        !copy_field(d.toward_stop_code, sizeof d.toward_stop_code,
                    w["toward_stop_code"] | "")) {
      return CfgError::FieldTooLong;
    }
    if (d.stop_code[0] == '\0') return CfgError::MissingStopCode;
    d.enabled = w["enabled"] | true;
    if (d.enabled) n_enabled++;
    g->n_watches++;
  }

  // Every group, not just the active one: a group you can switch to and get a
  // blank board is not worth being able to switch to.
  if (n_enabled == 0) return CfgError::NoWatches;
  return CfgError::Ok;
}

}  // namespace

CfgError cfg_parse(const char* json, Config* out, uint8_t theme_max) {
  if (json == nullptr || json[0] == '\0') return CfgError::BadJson;

  JsonDocument doc;
  if (deserializeJson(doc, json)) return CfgError::BadJson;
  if (doc["v"].isNull()) return CfgError::BadVersion;
  const uint8_t version = doc["v"].as<uint8_t>();
  if (version != CFG_SCHEMA_VERSION && version != CFG_SCHEMA_VERSION_V1) {
    return CfgError::BadVersion;
  }

  Config c{};
  const uint8_t t = doc["theme"] | 0;
  c.theme = (theme_max == 0 || t < theme_max) ? t : static_cast<uint8_t>(theme_max - 1);

  if (version == CFG_SCHEMA_VERSION_V1) {
    // A v1 document is one group's worth of watches with the group left
    // implicit. Lift it into the first group so an existing board keeps its
    // stops across the update; the name is what the web page will show for it.
    const CfgError e = parse_watches(doc["watches"].as<JsonArrayConst>(), &c.groups[0]);
    if (e != CfgError::Ok) return e;
    // v1's board-wide "location" becomes the group's name, which is what the
    // panel now labels itself with - so the caption survives the migration.
    const char* was = doc["location"] | "";
    if (!copy_field(c.groups[0].name, sizeof c.groups[0].name, was[0] ? was : "Main")) {
      return CfgError::GroupNameTooLong;
    }
    c.n_groups = 1;
    c.active_group = 0;
    *out = c;
    return CfgError::Ok;
  }

  JsonArrayConst gs = doc["groups"].as<JsonArrayConst>();
  if (gs.size() > MAX_GROUPS) return CfgError::TooManyGroups;
  if (gs.size() == 0) return CfgError::NoGroups;

  for (JsonObjectConst g : gs) {
    CfgGroup& d = c.groups[c.n_groups];
    if (!copy_field(d.name, sizeof d.name, g["name"] | "")) return CfgError::GroupNameTooLong;
    // Repaired, not rejected. This same function parses what NVS holds at
    // boot, so refusing here would drop every group over a cosmetic blank -
    // the same reasoning that clamps the theme and active_group below. The
    // setup page still insists on a name, where saying so is useful.
    if (d.name[0] == '\0') snprintf(d.name, sizeof d.name, "Group %u", c.n_groups + 1);
    const CfgError e = parse_watches(g["watches"].as<JsonArrayConst>(), &d);
    if (e != CfgError::Ok) return e;
    c.n_groups++;
  }

  // Clamped rather than rejected, for the same reason the theme is: a group
  // can be deleted from under a stored index, and that must not brick the
  // config.
  const uint8_t a = doc["active_group"] | 0;
  c.active_group = a < c.n_groups ? a : 0;

  *out = c;
  return CfgError::Ok;
}

size_t cfg_serialize(const Config& cfg, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';

  JsonDocument doc;
  doc["v"] = CFG_SCHEMA_VERSION;  // always the current version; v1 is read-only
  doc["theme"] = cfg.theme;
  doc["active_group"] = cfg.active_group;
  JsonArray gs = doc["groups"].to<JsonArray>();
  for (uint8_t gi = 0; gi < cfg.n_groups; gi++) {
    JsonObject g = gs.add<JsonObject>();
    g["name"] = cfg.groups[gi].name;
    JsonArray ws = g["watches"].to<JsonArray>();
    for (uint8_t i = 0; i < cfg.groups[gi].n_watches; i++) {
      const CfgWatch& src = cfg.groups[gi].watches[i];
      JsonObject w = ws.add<JsonObject>();
      w["label"] = src.label;
      w["stop_code"] = src.stop_code;
      w["route_short_name"] = src.route_short_name;
      w["toward_stop_code"] = src.toward_stop_code;
      w["enabled"] = src.enabled;
    }
  }

  // measureJson excludes the NUL, serializeJson needs room for it.
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

uint8_t cfg_publish(const Config& cfg, WatchConfig out[MAX_WATCHES]) {
  if (cfg.n_groups == 0 || cfg.active_group >= cfg.n_groups) return 0;
  const CfgGroup& g = cfg.groups[cfg.active_group];

  uint8_t n = 0;
  for (uint8_t i = 0; i < g.n_watches && n < MAX_WATCHES; i++) {
    if (!g.watches[i].enabled) continue;
    out[n].label = g.watches[i].label;
    out[n].stop_code = g.watches[i].stop_code;
    out[n].route_short_name = g.watches[i].route_short_name;
    out[n].toward_stop_code = g.watches[i].toward_stop_code;
    n++;
  }
  return n;
}
