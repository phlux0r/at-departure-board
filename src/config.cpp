#include "config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "backlight.h"
#include "theme.h"
#include "watch_config.h"

static_assert(N_WATCHES <= MAX_WATCHES,
              "watch_config.h declares more watches than a Config can hold");

namespace {

constexpr char NVS_NS[] = "board";
constexpr char NVS_KEY[] = "cfg";
constexpr char NVS_ORDER_KEY[] = "order";  // separate key: never round-tripped
                                           // through cfg_parse/cfg_serialize
constexpr char NVS_BRIGHT_KEY[] = "bright";

Config g_cfg;
WatchConfig g_pub[MAX_WATCHES];
uint8_t g_n_pub = 0;

// g_pub[i] came from g_cfg.watches[g_pub_src[i]]. cfg_publish() compacts the
// enabled watches and doesn't report where each came from, so this mirrors its
// loop - the two have to agree, or a lane toggle writes the wrong watch's bit.
uint8_t g_pub_src[MAX_WATCHES] = {0, 1, 2, 3};

// The one value written at runtime. A uint8_t store is atomic on this target,
// which is the whole reason the theme may change without a reboot.
volatile uint8_t g_theme = 0;

// Display order, not config: see config.h. Touched only by reorder_ui.cpp,
// at human tap speed, so a plain array (no atomics) is fine - nothing else
// ever writes it, and ui.cpp only ever reads a fully-written result because
// config_set_lane_order() finishes the copy before returning.
uint8_t g_order[MAX_WATCHES] = {0, 1, 2, 3};

// Same reasoning as g_order: written only by the touch UI, at tap speed, and
// read by the renderer. A hidden lane is still fetched - see config.h.
bool g_visible[MAX_WATCHES] = {true, true, true, true};

// Not in the Config schema, so no version bump and nothing for the web page
// to round-trip. It can move there if brightness ever wants to be settable
// from the browser too.
volatile uint8_t g_brightness = 255;  // uint8_t store is atomic here, as theme

Preferences g_prefs;

// A Config document is ~4.6 KB at its cap - far too much for any task stack
// here (the loop task gets 8 KB), so serialisation uses these instead. One per
// task that serialises, because the two run concurrently on different cores:
// the touch UI writes theme and lane visibility from the loop task, and the
// setup page writes from the portal task on core 0 (portal.cpp).
char g_json_ui[CFG_JSON_CAP];      // loop task, and config_begin() before tasks exist
char g_json_portal[CFG_JSON_CAP];  // portal task

// Writes the document as an NVS *blob*. nvs_set_str caps a value at 4000
// bytes and a full four-group config is ~3.7 KB, which is too close to live
// with - see config_begin() for the migration off the old string form.
void store_json(const char* json) {
  if (!g_prefs.begin(NVS_NS, false)) return;
  g_prefs.putBytes(NVS_KEY, json, strlen(json));
  g_prefs.end();
}

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
  c.theme = 0;

  // One group, holding what watch_config.h declares. More groups are made on
  // the setup page; there is no point compiling in a second arrangement of
  // stops nobody has chosen yet. Its name is what the panel captions itself
  // with, which is why LOCATION seeds it.
  CfgGroup& g = c.groups[0];
  strncpy(g.name, LOCATION, sizeof g.name - 1);
  g.n_watches = 0;
  for (int i = 0; i < N_WATCHES && i < MAX_WATCHES; i++) {
    CfgWatch& d = g.watches[g.n_watches];
    strncpy(d.label, WATCHES[i].label, sizeof d.label - 1);
    strncpy(d.stop_code, WATCHES[i].stop_code, sizeof d.stop_code - 1);
    strncpy(d.route_short_name, WATCHES[i].route_short_name, sizeof d.route_short_name - 1);
    strncpy(d.toward_stop_code, WATCHES[i].toward_stop_code, sizeof d.toward_stop_code - 1);
    d.enabled = true;
    g.n_watches++;
  }
  c.n_groups = 1;
  c.active_group = 0;
  g_cfg = c;

  // Route the compiled defaults through the same validator the JSON path
  // uses. This cannot catch a field already truncated by strncpy above (that
  // information is gone by now), but it does catch an empty stop_code, zero
  // enabled watches, and a missing group name - the achievable part of
  // reject-don't-truncate for a path that itself only truncates.
  char* json = g_json_ui;
  Config scratch{};
  if (cfg_serialize(g_cfg, json, CFG_JSON_CAP) == 0) {
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
  char* json = g_json_ui;  // runs before any task exists; see the buffers above
  json[0] = '\0';

  // The document is stored as a blob, not a string: nvs_set_str caps a value
  // at 4000 bytes, and four full groups serialise to ~3.7 KB. Boards written
  // by an older build still have it under the same key as a *string*, so fall
  // back to reading one - see the migration below.
  bool from_string_form = false;
  if (g_prefs.begin(NVS_NS, true)) {  // read-only
    const size_t len = g_prefs.getBytesLength(NVS_KEY);
    if (len > 0 && len < CFG_JSON_CAP) {  // NOT sizeof json - that is a pointer
      g_prefs.getBytes(NVS_KEY, json, len);
      json[len] = '\0';
    } else {
      g_prefs.getString(NVS_KEY, json, CFG_JSON_CAP);
      from_string_form = json[0] != '\0';
    }
    g_prefs.end();
  }

  bool loaded = false;
  {
    const CfgError e = cfg_parse(json, &g_cfg, theme_count());
    if (e == CfgError::Ok) {
      loaded = true;
    } else if (json[0] != '\0') {
      Serial.printf("config: stored config rejected (%s), using defaults\n",
                    cfg_error_text(e));
    }
  }
  if (!loaded) seed_from_compiled_defaults();

  // Rewrite anything that came from the old string form - whether it was a v1
  // document lifted into a group, or a v2 one an older build had stored as a
  // string - so the next boot takes the blob path and the 4000-byte ceiling
  // stops applying.
  if (loaded && from_string_form) {
    if (cfg_serialize(g_cfg, json, CFG_JSON_CAP) != 0 && g_prefs.begin(NVS_NS, false)) {
      g_prefs.remove(NVS_KEY);  // drop the string-typed value before writing a blob
      g_prefs.end();
      store_json(json);
      Serial.println("config: migrated to the grouped schema");
    }
  }

  g_theme = g_cfg.theme;
  g_n_pub = cfg_publish(g_cfg, g_pub);
  {  // mirror cfg_publish's compaction to remember where each published watch came from
    uint8_t n = 0;
    if (g_cfg.active_group < g_cfg.n_groups) {
      const CfgGroup& g = g_cfg.groups[g_cfg.active_group];
      for (uint8_t i = 0; i < g.n_watches && n < MAX_WATCHES; i++) {
        if (g.watches[i].enabled) g_pub_src[n++] = i;
      }
    }
  }
  for (uint8_t i = 0; i < MAX_WATCHES; i++) g_visible[i] = true;  // everything published is shown
  Serial.printf("config: %s, %u watches, theme %u, group %u of %u (%s)\n",
                config_location(), static_cast<unsigned>(g_n_pub),
                static_cast<unsigned>(g_theme), static_cast<unsigned>(g_cfg.active_group + 1),
                static_cast<unsigned>(g_cfg.n_groups), loaded ? "nvs" : "compiled defaults");

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

  if (g_prefs.begin(NVS_NS, true)) {  // read-only
    g_brightness = g_prefs.getUChar(NVS_BRIGHT_KEY, 255);
    g_prefs.end();
  }
  if (g_brightness < BRIGHTNESS_MIN) g_brightness = BRIGHTNESS_MIN;  // never stored dark
  backlight_set(g_brightness);
}

const WatchConfig* config_watches() { return g_pub; }
uint8_t config_n_watches() { return g_n_pub; }
const char* config_location() {
  // The active group's name is the panel's label; there is no separate
  // location any more (config.h).
  return g_cfg.active_group < g_cfg.n_groups ? g_cfg.groups[g_cfg.active_group].name : "";
}
uint8_t config_theme() { return g_theme; }

uint8_t config_n_groups() { return g_cfg.n_groups; }
uint8_t config_active_group() { return g_cfg.active_group; }

const char* config_group_name(uint8_t index) {
  return index < g_cfg.n_groups ? g_cfg.groups[index].name : "";
}

bool config_set_active_group(uint8_t index) {
  if (index >= g_cfg.n_groups || index == g_cfg.active_group) return false;

  // Serialised from a copy, like config_set_lane_visible(): g_cfg is what the
  // portal reads on core 0 and what g_pub's pointers alias into, so it must
  // not move under either of them. The reboot is what makes the change real.
  Config snapshot = g_cfg;
  snapshot.theme = g_theme;
  snapshot.active_group = index;
  if (cfg_serialize(snapshot, g_json_ui, CFG_JSON_CAP) == 0) return false;
  store_json(g_json_ui);
  return true;
}

const uint8_t* config_lane_order() { return g_order; }

uint8_t config_brightness() { return g_brightness; }

void config_set_brightness(uint8_t level) {
  if (level < BRIGHTNESS_MIN) level = BRIGHTNESS_MIN;
  g_brightness = level;  // live, atomic
  backlight_set(level);
  if (g_prefs.begin(NVS_NS, false)) {
    g_prefs.putUChar(NVS_BRIGHT_KEY, level);
    g_prefs.end();
  }
}

const bool* config_lane_visible() { return g_visible; }

bool config_set_lane_visible(uint8_t index, bool visible) {
  if (index >= g_n_pub) return false;
  if (!visible) {
    uint8_t shown = 0;
    for (uint8_t i = 0; i < g_n_pub; i++) {
      if (g_visible[i]) shown++;
    }
    if (shown <= 1 && g_visible[index]) return false;  // never hide the last lane
  }
  g_visible[index] = visible;  // live: the renderer picks this up next frame

  // Persist as the watch's `enabled` bit, so the NEXT boot stops fetching it
  // too. Serialised from a copy: g_cfg is what config_to_json() hands the
  // portal on core 0, and g_pub is what the fetch task reads - neither is
  // touched here, which is what makes this safe to do while both are running.
  Config snapshot = g_cfg;
  snapshot.theme = g_theme;
  if (snapshot.active_group < snapshot.n_groups) {
    CfgGroup& g = snapshot.groups[snapshot.active_group];
    if (g_pub_src[index] < g.n_watches) g.watches[g_pub_src[index]].enabled = visible;
  }
  if (cfg_serialize(snapshot, g_json_ui, CFG_JSON_CAP) == 0) return true;  // live change stands
  store_json(g_json_ui);
  return true;
}

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
  if (cfg_serialize(g_cfg, g_json_ui, CFG_JSON_CAP) == 0) return;
  store_json(g_json_ui);
}

CfgError config_save_json(const char* json) {
  Config scratch{};
  const CfgError e = cfg_parse(json, &scratch, theme_count());
  if (e != CfgError::Ok) return e;

  // Re-serialise rather than storing the caller's bytes: this normalises the
  // document and guarantees what lands in NVS is something cfg_parse accepts.
  if (cfg_serialize(scratch, g_json_portal, CFG_JSON_CAP) == 0) return CfgError::FieldTooLong;
  store_json(g_json_portal);
  return CfgError::Ok;  // caller reboots; g_cfg deliberately untouched
}

size_t config_to_json(char* out, size_t cap) {
  Config snapshot = g_cfg;  // plain data, safe to copy
  snapshot.theme = g_theme;  // the live value, which may be ahead of g_cfg
  return cfg_serialize(snapshot, out, cap);
}
