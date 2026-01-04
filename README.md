# Nixie Clock Firmware

## Overview
- ESP-IDF firmware for a 6-tube nixie clock with WS2812 backlight (4 LEDs per tube).
- Structure favors small modules: tubes/backlight state, LED strip driver, effect engine, and a slim `app_main`.
- Effects run in a single LED service task; backlight parameters can be updated dynamically (e.g., UI, network).

## Nixie Tube Model
- `NixieTube` (`lib/include/nixie_tube.h`, `src/nixie_tube.cpp`):
  - Holds `DigitState` (numeral, nixie brightness, `BackLightState` with HSV + brightness).
  - Owns an attached `LedGroup` (4 LEDs for one tube).
  - Thread-safe via an internal mutex around state changes and LED updates.
  - Methods: `set_state`, `update_back_light`, `turn_off_back_light`, `attach_led_group`.
- `NixieTubeArray` (`lib/include/nixie_array.h`, `src/nixie_array.cpp`):
  - Owns the 6 tubes; attaches LED groups to the shared strip; seeds backlight defaults; exposes tube pointers for effects.

## LED Effects and Service
- Strip/LED abstractions:
  - `ILedStrip` interface, `Ws2812Strip` implementation (`lib/include/ws2812.h`, `src/ws2812.cpp`).
  - `Led` wraps a strip pixel; `LedGroup` holds per-tube LEDs.
- Effect engine (`lib/include/led_effect.h`, `src/led_effect.cpp`):
  - Base `LedEffect`: stores targets (`std::vector<NixieTube*>`), base `BackLightState`, enabled flag, and a mutex.
  - `BreathEffect`: parameters `speed_hz`, `max_brightness`; modulates brightness sinusoidally.
  - `RainbowEffect`: parameter `degrees_per_second`; sweeps hue while preserving brightness.
  - `LedService`: single FreeRTOS task ticking effects; categories (`Backlight`, reserved `Beats`) ensure one active effect per lane. Mutex guards configuration; task calls `tick(dt_ms)` then `strip.show()` when updates occurred.
- Setup (`lib/include/led_setup.h`, `src/led_setup.cpp`):
  - Builds tube array, attaches LED groups, seeds default backlight, wires `LedService` with breathing backlight and rainbow ready but disabled.
  - Returns `LedControlHandles {service, backlight, rainbow}` for higher-level control.

## Adding a New LED Effect (step by step)
1) **Define the effect class**  
   - Derive from `LedEffect` in `led_effect.h/led_effect.cpp`.  
   - Implement `run(uint32_t dt_ms)`; use `base_backlight()`, adjust its fields, and call `apply_backlight_to_targets()`.  
   - Add setters for parameters you need (e.g., speed, color); guard shared state with the provided mutex (`mutex_handle()`).

2) **Instantiate and configure**  
   - In `initialize_led_module` (or a new setup file), create a `static` instance of your effect.  
   - Call `set_targets(nixie_tube_array().tube_pointers())`, `set_base_backlight(...)`, parameter setters, and `set_enabled(true/false)` depending on default state.

3) **Register with the service**  
   - Decide the lane: use `LedEffectCategory::kBacklight` for the main backlight, or `kBeats` if you later use the reserved lane.  
   - `led_service.set_effect(category, &your_effect);`  
   - Enable/disable the lane with `enable_category(category, true/false)`; keep only one effect active per lane to avoid fighting writes.

4) **Expose control hooks**  
   - Surface handles (similar to `LedControlHandles`) so UI/network code can call setters and `set_targets` (single tube vs all tubes).

5) **Test quickly**  
   - Build (`pio run` or `idf.py build`) and flash. Use logs to confirm the service loop is running and the effect toggles as expected.

### Notes
- Mutex usage: `LedEffect`/`LedService` guard shared config with FreeRTOS mutexes; `NixieTube` guards its state similarly. Avoid long work inside the critical sections.
- Categories: they’re optional but help ensure only one effect drives a lane (e.g., backlight) at a time; you can swap the effect pointer to change modes.
