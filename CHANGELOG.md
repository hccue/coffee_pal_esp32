# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).


## [0.1.0] - 2026-11-01
### Added
- Animated mug UI for the Waveshare ESP32-S3-Touch-LCD-1.47.
- **+ Coffee** and **+ Froth** buttons to count drinks.
- **Reset** button with a 3-second **Sure?** confirmation.
- Drip tray bar with one notch per drink, an amber warning when almost full, and a pulsing red "EMPTY THE TRAY!" alert when full.
- **Emptied** button, shown in place of Reset when the tray is full.
- Counts saved across power cuts.
- Nap mode with a dimmed screen after 10 idle minutes, skipped while the tray is full.
- Tray rules and screen flip set in `src/config.h`.
- USB serial commands: `c`, `f`, `r`, `i` and `s`.
- Host-side test and screen preview in `test/preview.cpp`.
- On-device check script in `tools/device_check.py`.
- Font generator in `tools/make_font.py`, using Nunito (SIL Open Font License).

[Unreleased]: https://github.com/OWNER/coffee_pal_esp32/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/OWNER/coffee_pal_esp32/releases/tag/v0.1.0
