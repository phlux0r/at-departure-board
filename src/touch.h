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

// Feed this the same touch_read() sample already taken this frame (down, x,
// y) and this frame's timestamp. Returns true only on a debounced, confirmed
// press, with the position latched at the moment it fires.
//
// A raw touch_read() edge is not good enough for anything that acts on a
// tap: this panel's resistive touch is noisy enough right at the moment of
// contact that a single physical tap can read down/up/down across a few
// frames rather than staying cleanly down (docs/hardware-notes.md, touch
// bring-up), which would otherwise fire a UI action twice, or twice and
// cancel back out. touch.cpp's own bring-up serial log deliberately prints
// every raw edge instead of this, since seeing that flicker is the point.
bool touch_debounce(bool down, uint16_t x, uint16_t y, uint32_t now_ms, uint16_t* out_x,
                     uint16_t* out_y);
