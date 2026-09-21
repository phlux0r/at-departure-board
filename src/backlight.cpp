#include "backlight.h"

#include <Arduino.h>

// The backlight GPIO differs per board, so it comes from a build flag
// (platformio.ini) like every display pin does. 32 is the classic-ESP32
// wiring in docs/hardware-notes.md.
#ifndef BACKLIGHT_PIN
#define BACKLIGHT_PIN 32
#endif

namespace {
constexpr int PIN = BACKLIGHT_PIN;
constexpr int CHANNEL = 0;
constexpr int FREQ_HZ = 5000;
constexpr int BITS = 8;
}  // namespace

void backlight_begin() {
  ledcSetup(CHANNEL, FREQ_HZ, BITS);
  ledcAttachPin(PIN, CHANNEL);
  backlight_set(255);
}

void backlight_set(uint8_t level) { ledcWrite(CHANNEL, level); }
