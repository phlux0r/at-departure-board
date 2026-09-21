// Firmware entry point. DEMO_MODE plays the canonical scenes from
// tools/board/scenes.py, counting down in real time; otherwise this is the
// live data path (spec section 9).
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#include <time.h>

#include "backlight.h"
#include "reorder_ui.h"
#include "touch.h"
#include "ui.h"

#ifdef DEMO_MODE
#include "demo.h"
#else
#include <WiFi.h>

#include "config.h"
#include "fetcher.h"
#include "live.h"
#include "portal.h"
#include "secrets.h"
#endif

namespace {

TFT_eSPI tft;
Ui ui(tft);

constexpr uint32_t FRAME_MS = 1000 / 15;  // 15 fps, spec section 5
constexpr uint32_t REPORT_MS = 5000;

#ifdef DEMO_MODE
void report(uint32_t now, uint32_t frames, uint32_t draw_ms_total, uint32_t since) {
  Serial.printf("fps %.1f  draw %lums  heap %u  largest %u  min-ever %u\n",
                frames * 1000.0f / (now - since),
                static_cast<unsigned long>(draw_ms_total / frames), ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), ESP.getMinFreeHeap());
}

Board board_now(uint32_t ms, int64_t) { return demo_board(ms); }
#else
Snapshot snap;  // static storage: a Snapshot is far too big for a task stack

// Reuses the snapshot board_now() already copied this frame - never a second
// fetcher_snapshot() call, which would be one more memcpy under the mutex.
void report(uint32_t now, uint32_t frames, uint32_t draw_ms_total, uint32_t since) {
  char rows[48] = "";
  size_t off = 0;
  for (uint8_t i = 0; i < snap.n_watches && off + 4 < sizeof rows; i++) {
    off += snprintf(rows + off, sizeof(rows) - off, "%s%u", i ? "/" : "",
                    static_cast<unsigned>(snap.watches[i].n_rows));
  }
  Serial.printf("fps %.1f  draw %lums  heap %u  largest %u  min-ever %u  rows %s\n",
                frames * 1000.0f / (now - since),
                static_cast<unsigned long>(draw_ms_total / frames), ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), ESP.getMinFreeHeap(), rows);
}

Board board_now(uint32_t, int64_t now) {
  fetcher_snapshot(&snap);  // a memcpy under the mutex, never a network wait
  return build_board(snap, config_watches(), config_location(), now, config_theme());
}
#endif

}  // namespace

void setup() {
  Serial.begin(115200);
  backlight_begin();
  tft.init();
  tft.setRotation(1);
  if (!ui.begin()) {
    Serial.println("FATAL: band sprite allocation failed");
    tft.fillScreen(TFT_RED);
    for (;;) delay(1000);
  }

  // Bring-up only: no touch UI reads this yet (spec section 8a). A no-op on
  // a board with no TOUCH_CS defined. Runs before BOOT-OK so a first-time
  // calibration's on-screen prompts aren't mistaken for a hung boot.
  touch_begin(tft);

#ifdef DEMO_MODE
  Serial.println("BOOT-OK demo");
#else
  WiFi.mode(WIFI_STA);

  // Before fetcher_begin(): that reads config_n_watches().
  config_begin();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  bool wifi_ok = false;
  for (int i = 0; i < 20; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      wifi_ok = true;
      break;
    }
    Serial.printf("wifi: status %d\n", WiFi.status());
    delay(1000);
  }
  if (wifi_ok) {
    Serial.printf("wifi: connected, ip %s rssi %d\n", WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
  } else {
    Serial.printf("wifi: FAILED status %d\n", WiFi.status());
  }

  // NTP is started here and waited for only as a courtesy to the log: the
  // fetcher retries both WiFi and the clock on its own.
  configTime(0, 0, "pool.ntp.org");  // UTC - nztime does the local conversion
  bool time_ok = false;
  if (wifi_ok) {
    for (int i = 0; i < 15; i++) {
      if (time(nullptr) >= CLOCK_SET_AFTER) {
        time_ok = true;
        break;
      }
      delay(1000);
    }
  }
  if (time_ok) {
    Serial.printf("time: %lld\n", static_cast<int64_t>(time(nullptr)));
  } else {
    Serial.println("time: not synced yet");
  }

  // Every network call from here on happens on the fetch task: core 0, 16 KB
  // of stack. The loop task does nothing but draw (spec 8).
  fetcher_begin();
  portal_begin();

  Serial.println("BOOT-OK live");
#endif

  // Placed after config_begin() (live branch, above), so a saved order
  // loads. DEMO_MODE never calls config_begin() at all, so this just picks
  // up config.cpp's compiled-in identity default there instead - reorder
  // still works in the demo, it just never persists across boots.
  reorder_ui_begin();
}

void loop() {
  static uint32_t frames = 0, draw_ms_total = 0, last_report = 0;
  static uint32_t next_frame = 0;

  const uint32_t start = millis();
  if (next_frame == 0) next_frame = start;

#ifdef DEMO_MODE
  static int last_scene = -1;

  const int scene = static_cast<int>((start / DEMO_SCENE_MS) % demo_scene_count());
  if (scene != last_scene) {
    const Board b = demo_board(start);
    Serial.printf("scene %s  theme %u\n", demo_scene(scene).name, b.theme);
    last_scene = scene;
  }
#endif

  // time(nullptr) is whatever the RTC has. Before NTP lands it reads some
  // small value near the 1970 epoch, but every watch is still
  // WatchState::Starting until the fetcher's first successful resolve, so
  // build_board skips its rows regardless (live.cpp: `if (lw.state !=
  // WatchState::Ok) continue;`) - the lanes just say "starting" and no
  // countdown is drawn from the bad clock.
  const int64_t now = time(nullptr);

  // One touch read per frame. The raw down/up edges go straight to the
  // bring-up log below - every transition, flicker included, since seeing
  // that flicker is the point. The reorder UI acts on touch_debounce()'s
  // confirmed edge instead (touch.h), or a noisy contact could fire (and
  // silently cancel out) more than one swap per physical tap.
  static bool touch_was_down = false;
  uint16_t tx = 0, ty = 0;
  const bool touch_is_down = touch_read(tft, &tx, &ty);
  if (touch_is_down && !touch_was_down) {
    Serial.printf("touch: down at %u,%u\n", tx, ty);
  } else if (!touch_is_down && touch_was_down) {
    Serial.println("touch: up");
  }
  touch_was_down = touch_is_down;

  uint16_t confirmed_x = 0, confirmed_y = 0;
  const bool touch_confirmed =
      touch_debounce(touch_is_down, tx, ty, start, &confirmed_x, &confirmed_y);

  ui.draw(board_now(start, now), start, touch_confirmed, confirmed_x, confirmed_y);

  const uint32_t took = millis() - start;
  frames++;
  draw_ms_total += took;
  if (start - last_report >= REPORT_MS) {
    report(start, frames, draw_ms_total, last_report);
    frames = 0;
    draw_ms_total = 0;
    last_report = start;
  }

  // Pace against an absolute deadline rather than delaying by (FRAME_MS -
  // took): a relative delay drifts by the draw time every frame, and over a
  // long-running board that adds up. If we have fallen behind (a slow frame,
  // or a Serial.printf report), snap the deadline to now instead of trying to
  // catch up in one step.
  next_frame += FRAME_MS;
  const uint32_t now_ms = millis();
  if (static_cast<int32_t>(next_frame - now_ms) > 0) {
    delay(next_frame - now_ms);
  } else {
    next_frame = now_ms;  // we are behind; do not spiral
  }
}
