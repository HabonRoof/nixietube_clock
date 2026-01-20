# Nixie Clock Firmware

## Overview
- ESP-IDF firmware for a 6-tube nixie clock with WS2812 backlight (4 LEDs per tube).
- Structure favors small modules: tubes/backlight state, LED strip driver, effect engine, and a slim `app_main`.
- Effects run in a single LED service task; backlight parameters can be updated dynamically (e.g., UI, network).

## Nixie Tube Model
- `NixieTube` (`lib/include/nixie_tube.h`, `src/nixie_tube.cpp`):
  - Holds `DigitState` (numeral, nixie brightness).
  - Thread-safe via an internal mutex around state changes.
  - Methods: `set_state`, `set_numeral`.
- `NixieDriver` (`lib/include/nixie_driver.h`, `src/nixie_driver.cpp`):
  - Owns the 6 tubes directly.
  - Provides high-level methods like `display_time`.

## LED Control
- `ILedDriver` (`lib/include/led_driver.h`, `src/led_driver.cpp`):
  - Abstract interface for controlling LEDs.
  - `LedDriver` implementation wraps `Ws2812Strip`.
- `DisplayDaemon` (`src/daemons/display_daemon.cpp`):
  - Manages LED effects (Breath, Rainbow) internally.
  - Maps tubes to LEDs (e.g., Tube 0 -> LEDs 0-3) and updates `LedDriver` directly.

## Audio (DFPlayer Mini)
- UART-backed DFPlayer control (`lib/include/dfplayer_mini.h`, `src/dfplayer_mini.cpp`):
  - Commands for volume (set/up/down), track playback, next/previous, loop toggling, pause/resume, EQ presets, low-power sleep/wake, and reset.
  - Holds `AudioPlaybackState` (volume, track, loop, low power, paused) behind a FreeRTOS mutex.
- Audio Driver (`lib/include/audio_driver.h`, `src/audio_driver.cpp`):
  - `IAudioDriver` interface defines standard audio operations (play, stop, pause, volume control).
  - `AudioDriver` implements the interface using `DfPlayerMini`.
  - Initialized in `app_main` with UART configuration.
- Audio Daemon (`src/daemons/audio_daemon.cpp`, `src/daemons/audio_daemon.h`):
  - Runs in its own task to handle audio operations asynchronously.
  - Receives commands via a queue (`AudioMessage`).
  - Processes commands like `PLAY_TRACK`, `STOP`, `PAUSE`, `SET_VOLUME`, etc.

## Adding a New LED Effect
1) **Implement effect logic in `DisplayDaemon`**
   - Add a new method `run_my_effect(uint32_t dt_ms)` in `DisplayDaemon`.
   - Use `apply_backlight_to_all(state)` or manipulate `led_driver_` directly.

2) **Register with DisplayDaemon**
   - Add a new `LedEffectType` enum.
   - Update `DisplayDaemon::process_message` to handle switching to the new effect.
   - Update `DisplayDaemon::update_effects` to call your new method.
