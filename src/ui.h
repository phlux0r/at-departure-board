#pragma once
#include <TFT_eSPI.h>

#include "model.h"

// Draws a Board. A 320x240 16-bit frame needs 153,600 contiguous bytes and the
// largest free block on this chip is ~114 KB (docs/hardware-notes.md), so the
// frame is built one band at a time in a single reused sprite and pushed band
// by band. Everything is redrawn every frame; the scenery RNG makes that safe.
constexpr int BAND_H = 48;  // 320 x 48 x 2 = 30,720 bytes; 5 bands = 240 rows

class Ui {
 public:
  explicit Ui(TFT_eSPI& tft);
  bool begin();  // false if the band sprite could not be allocated

  // touch_down_edge must be true only on the frame a press begins (main.cpp
  // tracks this) - it's forwarded to the reorder UI (reorder_ui.h), which
  // debounces its own taps from that edge rather than a held finger.
  void draw(const Board& board, uint32_t ms, bool touch_down_edge, int touch_x, int touch_y);

 private:
  TFT_eSprite band_;
};
