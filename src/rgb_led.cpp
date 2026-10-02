#include "rgb_led.h"

#include <Arduino.h>

// Driven with the Arduino-ESP32 core's built-in neopixelWrite() (RMT under
// the hood) rather than pulling in a NeoPixel library for a single "turn it
// off" write. That's the 2.0.x name; core 3.x renamed it rgbLedWrite() -
// this project pins 2.0.17 (platformio.ini), so neopixelWrite() is correct
// here. Revisit if the platform version ever moves.
#ifdef RGB_LED_PIN
void rgb_led_off() { neopixelWrite(RGB_LED_PIN, 0, 0, 0); }
#else
void rgb_led_off() {}
#endif
