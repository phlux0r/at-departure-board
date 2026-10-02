#pragma once

// Some ESP32-S3 SuperMini units carry an onboard addressable RGB LED (a
// single WS2812-style pixel) that lights up on boot until firmware drives
// it. The pin comes from the RGB_LED_PIN build flag (platformio.ini); when
// that flag is undefined, rgb_led_off() is a no-op, so boards without the
// LED - and the classic ESP32 build - are unaffected.
void rgb_led_off();

