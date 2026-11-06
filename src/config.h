#pragma once

// ---- Tray rules: change these if your machine's tray behaves differently ----
// Every drink adds "units" of water. The tray is full at TRAY_CAPACITY_UNITS.
// 1 / 1 / 12 gives: 12 coffees alone, or 6 coffees + 6 froths.
#define COFFEE_UNITS 1
#define FROTH_UNITS 1
#define TRAY_CAPACITY_UNITS 12
// The assistant starts looking worried when this many units remain.
#define TRAY_WARN_REMAINING_UNITS 2

// ---- Device behaviour ----
// Set to 1 to turn the picture upside down (USB port on the other side).
#define FLIP_SCREEN 0
// Backlight percentages.
#define BRIGHTNESS_NORMAL 80
#define BRIGHTNESS_SLEEP 12
// Idle time before the assistant naps and the screen dims. Never naps on a full tray.
#define SLEEP_AFTER_MS (10UL * 60UL * 1000UL)
// How long the reset button waits for its confirming second tap.
#define RESET_CONFIRM_MS 3000UL
