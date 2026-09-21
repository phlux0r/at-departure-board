#pragma once
#include <stdint.h>

// The backlight is driven through LEDC rather than tied to 3V3, which is what
// lets the board dim instead of only switching off (spec section 8). The pin
// is the BACKLIGHT_PIN build flag - GPIO32 on the classic ESP32 wiring,
// GPIO7 on the S3 SuperMini.
void backlight_begin();
void backlight_set(uint8_t level);  // 0 = off, 255 = full
