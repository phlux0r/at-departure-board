#include "rgb_led.h"

#include <Arduino.h>

// Driven with the Arduino-ESP32 core's built-in rgbLedWrite() (RMT under the
// hood) rather than pulling in a NeoPixel library for a single "turn it
// off" write. Available since core 2.0.14; this project pins 2.0.17
// (platformio.ini).
#ifdef RGB_LED_PIN
void rgb_led_off() { rgbLedWrite(RGB_LED_PIN, 0, 0, 0); }
#else
void rgb_led_off() {}
#endif
