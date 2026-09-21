#include "touch.h"

#ifdef TOUCH_CS

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

namespace {
// Separate namespace from config.cpp's "board"/"cfg": this is device-local
// calibration, never round-tripped through the web setup page's JSON.
constexpr char NVS_NS[] = "touch";
constexpr char NVS_KEY[] = "cal";
}  // namespace

void touch_begin(TFT_eSPI& tft) {
  uint16_t cal[5];
  Preferences prefs;
  bool loaded = false;
  if (prefs.begin(NVS_NS, true)) {  // read-only
    if (prefs.getBytesLength(NVS_KEY) == sizeof cal &&
        prefs.getBytes(NVS_KEY, cal, sizeof cal) == sizeof cal) {
      loaded = true;
    }
    prefs.end();
  }

  if (loaded) {
    tft.setTouch(cal);
    Serial.println("touch: calibration loaded from nvs");
    return;
  }

  Serial.println("touch: no stored calibration - touch each corner as prompted");
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(2);
  tft.setCursor(20, 0);
  tft.calibrateTouch(cal, TFT_MAGENTA, TFT_BLACK, 15);

  if (prefs.begin(NVS_NS, false)) {  // read-write
    prefs.putBytes(NVS_KEY, cal, sizeof cal);
    prefs.end();
  }
  Serial.println("touch: calibration saved to nvs");
}

bool touch_read(TFT_eSPI& tft, uint16_t* x, uint16_t* y) { return tft.getTouch(x, y); }

#else  // !TOUCH_CS - no touch controller on this board

void touch_begin(TFT_eSPI&) {}
bool touch_read(TFT_eSPI&, uint16_t*, uint16_t*) { return false; }

#endif

namespace {
// Both amounts are a guess to be tuned against how this panel actually
// bounces, not a measured constant.
constexpr uint8_t CONFIRM_READS = 2;    // consecutive down reads to accept
constexpr uint32_t COOLDOWN_MS = 300;   // then ignore further edges a while

uint8_t g_consecutive_down = 0;
uint32_t g_last_edge_ms = 0;
bool g_confirmed_down = false;  // true from a confirmed press until a read
                                // comes back up - latches out any further
                                // flicker within the same physical tap
}  // namespace

bool touch_debounce(bool down, uint16_t x, uint16_t y, uint32_t now_ms, uint16_t* out_x,
                     uint16_t* out_y) {
  if (!down) {
    g_consecutive_down = 0;
    g_confirmed_down = false;
    return false;
  }
  if (g_confirmed_down) return false;
  if (++g_consecutive_down < CONFIRM_READS) return false;
  if (now_ms - g_last_edge_ms < COOLDOWN_MS) return false;

  g_confirmed_down = true;
  g_last_edge_ms = now_ms;
  *out_x = x;
  *out_y = y;
  return true;
}
