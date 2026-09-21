#include "config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "theme.h"
#include "watch_config.h"

static_assert(N_WATCHES <= MAX_WATCHES,
              "watch_config.h declares more watches than a Config can hold");

namespace {

constexpr char NVS_NS[] = "board";
constexpr char NVS_KEY[] = "cfg";
constexpr char NVS_ORDER_KEY[] = "order";  // separate key: never round-tripped
                                           // through cfg_parse/cfg_serialize

Config g_cfg;
WatchConfig g_pub[MAX_WATCHES];
uint8_t g_n_pub = 0;

// The one value written at runtime. A uint8_t store is atomic on this target,
// which is the whole reason the theme may change without a reboot.
volatile uint8_t g_theme = 0;

// Display order, not config: see config.h. Touched only by reorder_ui.cpp,
// at human tap speed, so a plain array (no atomics) is fine - nothing else
// ever writes it, and ui.cpp only ever reads a fully-written result because
// config_set_lane_order() finishes the copy before returning.
uint8_t g_order[MAX_WATCHES] = {0, 1, 2, 3};

Preferences g_prefs;

bool is_valid_permutation(const uint8_t* order, uint8_t n) {
  bool seen[MAX_WATCHES] = {};
  for (uint8_t i = 0; i < n; i++) {
    if (order[i] >= n || seen[order[i]]) return false;
    seen[order[i]] = true;
  }
  return true;
}

void seed_from_compiled_defaults() {
  Config c{};
  strncpy(c.location, LOCATION, sizeof c.location - 1);
  c.theme = 0;
  c.n_watches = 0;
  for (int i = 0; i < N_WATCHES && i < MAX_WATCHES; i++) {
    CfgWatch& d = c.watches[c.n_watches];
    strncpy(d.label, WATCHES[i].label, sizeof d.label - 1);
    strncpy(d.stop_code, WATCHES[i].stop_code, sizeof d.stop_code - 1);
    strncpy(d.route_short_name, WATCHES[i].route_short_name, sizeof d.route_short_name - 1);
    strncpy(d.toward_stop_code, WATCHES[i].toward_stop_code, sizeof d.toward_stop_code - 1);
    d.enabled = true;
    c.n_watches++;
  }
  g_cfg = c;

  // Route the compiled defaults through the same validator the JSON path
  // uses. This cannot catch a field already truncated by strncpy above (that
  // information is gone by now), but it does catch an empty stop_code, zero
  // enabled watches, and a too-long location - the achievable part of
  // reject-don't-truncate for a path that itself only truncates.
  char json[CFG_JSON_CAP];
  Config scratch{};
  if (cfg_serialize(g_cfg, json, sizeof json) == 0) {
    Serial.println(
        "config: watch_config.h defaults failed to serialise - "
        "watch_config.h is likely misconfigured");
  } else {
    const CfgError e = cfg_parse(json, &scratch, theme_count());
    if (e != CfgError::Ok) {
      Serial.printf(
          "config: watch_config.h defaults fail validation (%s) - "
          "watch_config.h is at fault\n",
          cfg_error_text(e));
    }
  }
}

}  // namespace

void config_begin() {
  char json[CFG_JSON_CAP];
  json[0] = '\0';

  bool loaded = false;
  if (g_prefs.begin(NVS_NS, true)) {  // read-only
    g_prefs.getString(NVS_KEY, json, sizeof json);
    g_prefs.end();
    const CfgError e = cfg_parse(json, &g_cfg, theme_count());
    if (e == CfgError::Ok) {
      loaded = true;
    } else if (json[0] != '\0') {
      Serial.printf("config: stored config rejected (%s), using defaults\n",
                    cfg_error_text(e));
    }
  }
  if (!loaded) seed_from_compiled_defaults();

  g_theme = g_cfg.theme;
  g_n_pub = cfg_publish(g_cfg, g_pub);
  Serial.printf("config: %s, %u watches, theme %u (%s)\n", g_cfg.location,
                static_cast<unsigned>(g_n_pub), static_cast<unsigned>(g_theme),
                loaded ? "nvs" : "compiled defaults");

  // Order is loaded separately from - and after - the watches themselves,
  // since it's only meaningful once g_n_pub is known: a stored order for a
  // different watch count (e.g. after editing stops on the setup page) is
  // stale and falls back to identity rather than silently misapplying.
  bool order_loaded = false;
  if (g_prefs.begin(NVS_NS, true)) {  // read-only
    uint8_t stored[MAX_WATCHES];
    if (g_prefs.getBytesLength(NVS_ORDER_KEY) == sizeof stored &&
        g_prefs.getBytes(NVS_ORDER_KEY, stored, sizeof stored) == sizeof stored &&
        is_valid_permutation(stored, g_n_pub)) {
      memcpy(g_order, stored, sizeof g_order);
      order_loaded = true;
    }
    g_prefs.end();
  }
  if (!order_loaded) {
    for (uint8_t i = 0; i < MAX_WATCHES; i++) g_order[i] = i;
  }
}

const WatchConfig* config_watches() { return g_pub; }
uint8_t config_n_watches() { return g_n_pub; }
const char* config_location() { return g_cfg.location; }
uint8_t config_theme() { return g_theme; }

const uint8_t* config_lane_order() { return g_order; }

void config_set_lane_order(const uint8_t order[MAX_WATCHES]) {
  if (!is_valid_permutation(order, g_n_pub)) return;
  memcpy(g_order, order, sizeof g_order);  // live
  if (g_prefs.begin(NVS_NS, false)) {       // persisted
    g_prefs.putBytes(NVS_ORDER_KEY, g_order, sizeof g_order);
    g_prefs.end();
  }
}

void config_set_theme(uint8_t t) {
  if (t >= theme_count()) return;
  g_theme = t;   // live, atomic
  g_cfg.theme = t;
  char json[CFG_JSON_CAP];
  if (cfg_serialize(g_cfg, json, sizeof json) == 0) return;
  if (g_prefs.begin(NVS_NS, false)) {
    g_prefs.putString(NVS_KEY, json);
    g_prefs.end();
  }
}

CfgError config_save_json(const char* json) {
  Config scratch{};
  const CfgError e = cfg_parse(json, &scratch, theme_count());
  if (e != CfgError::Ok) return e;

  // Re-serialise rather than storing the caller's bytes: this normalises the
  // document and guarantees what lands in NVS is something cfg_parse accepts.
  char clean[CFG_JSON_CAP];
  if (cfg_serialize(scratch, clean, sizeof clean) == 0) return CfgError::FieldTooLong;

  if (!g_prefs.begin(NVS_NS, false)) return CfgError::BadJson;
  g_prefs.putString(NVS_KEY, clean);
  g_prefs.end();
  return CfgError::Ok;  // caller reboots; g_cfg deliberately untouched
}

size_t config_to_json(char* out, size_t cap) {
  Config snapshot = g_cfg;  // plain data, safe to copy
  snapshot.theme = g_theme;  // the live value, which may be ahead of g_cfg
  return cfg_serialize(snapshot, out, cap);
}
