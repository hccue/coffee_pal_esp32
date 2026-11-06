#pragma once
#include <stdint.h>
#include "config.h"

// Counts since the tray was last emptied, and what they mean for the tray.
struct Tray {
  uint16_t coffees = 0;
  uint16_t froths = 0;

  int units() const { return coffees * COFFEE_UNITS + froths * FROTH_UNITS; }
  int remaining() const {
    int left = TRAY_CAPACITY_UNITS - units();
    return left > 0 ? left : 0;
  }
  bool full() const { return remaining() == 0; }
  bool warn() const { return !full() && remaining() <= TRAY_WARN_REMAINING_UNITS; }
  float level() const {
    float f = (float)units() / TRAY_CAPACITY_UNITS;
    return f > 1.f ? 1.f : f;
  }
  bool empty() const { return coffees == 0 && froths == 0; }
};
