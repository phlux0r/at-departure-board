#pragma once
#include <stdint.h>

class TFT_eSPI;

// Touch bring-up: one-time calibration, persisted to NVS, plus raw reads.
// A no-op on a board with no touch controller wired - the classic ESP32's
// panel has the touch footprint unpopulated (docs/SETUP.md) - because
// TOUCH_CS is then left undefined and these become stubs (touch.cpp).
//
// No touch UI reads these yet; this is deliberately just wiring plus a
// serial printout, so the hardware can be trusted before anything is built
// on top of it.

// Loads a stored calibration into tft, or - if NVS holds none yet - runs
// TFT_eSPI's interactive touch-the-corners routine on tft and stores the
// result. Blocks while calibrating.
void touch_begin(TFT_eSPI& tft);

// True if the panel is currently pressed, with x/y filled in in screen
// coordinates (post-calibration, post-rotation - the same space draw() uses).
bool touch_read(TFT_eSPI& tft, uint16_t* x, uint16_t* y);
