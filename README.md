# Nixie Clock Firmware

## Overview
This is the ESP-IDF firmware for a 6-tube Nixie clock powered by an ESP32-S3. It features:
- **6-Tube Display**: Multiplexed control using PCA9685 PWM drivers.
- **RGB Backlight**: WS2812 addressable LEDs (4 per tube) for visual effects.
- **Audio**: DFPlayer Mini integration for sound effects and announcements.
- **RTC**: DS3231 for precise timekeeping.
- **Modular Architecture**: Separate daemons for display and audio, coordinated by a system controller.

## Hardware Configuration

The firmware is configured for the following pinout (defined in `src/system_controller.cpp`):

| Peripheral | Pin Name | GPIO | Description |
| :--- | :--- | :--- | :--- |
| **I2C** | SDA | GPIO 6 | I2C Data (DS3231, PCA9685) |
| | SCL | GPIO 5 | I2C Clock |
| **UART** | TX | GPIO 18 | Audio TX (to DFPlayer RX) |
| | RX | GPIO 17 | Audio RX (from DFPlayer TX) |
| **GPIO** | RTC_INT | GPIO 8 | DS3231 Interrupt (Active Low) |
| | PCA_OE | GPIO 4 | PCA9685 Output Enable (Active Low) |
| **RMT** | LED_DATA | GPIO 7 | WS2812 LED Data Line |

## Architecture

The system is built on FreeRTOS and follows a daemon-based architecture:

### 1. System Controller (`src/system_controller.cpp`)
- **Role**: Central coordinator.
- **Responsibilities**:
  - Initializes all hardware peripherals (I2C, UART, RMT, GPIO).
  - Manages the DS3231 RTC.
  - Periodically (1Hz) reads time and sends updates to the `DisplayDaemon`.
  - Handles system-wide events (e.g., button presses).

### 2. Display Daemon (`src/daemons/display_daemon.cpp`)
- **Role**: Manages all visual output.
- **Responsibilities**:
  - Controls Nixie tubes via `NixieDriver`.
  - Controls LED backlights via `LedDriver`.
  - Runs the LED effects engine (Breath, Rainbow).
  - Updates hardware at 50Hz.

### 3. Audio Daemon (`src/daemons/audio_daemon.cpp`)
- **Role**: Manages audio playback.
- **Responsibilities**:
  - Asynchronously handles audio commands (Play, Stop, Volume).
  - Communicates with the DFPlayer Mini via `AudioDriver`.

### 4. Drivers (`lib/drivers/`, `src/*_driver.cpp`)
- **NixieDriver**: Manages 4x PCA9685 chips to drive 6 tubes. Handles multiplexing in a dedicated high-priority task.
- **LedDriver**: Wraps the RMT peripheral to drive WS2812 LEDs.
- **AudioDriver**: Provides a high-level interface for the DFPlayer Mini.
- **Ds3231**: Low-level driver for the RTC.

## Building and Flashing

This project uses **PlatformIO**.

### Prerequisites
- Visual Studio Code with PlatformIO extension installed.

### Build
```bash
pio run
```

### Flash
Connect your ESP32-S3 board via USB and run:
```bash
pio run -t upload
```

### Monitor
To view serial logs:
```bash
pio run -t monitor
```

## Directory Structure

- `src/`: Application source code.
  - `daemons/`: High-level tasks (Display, Audio).
  - `main.cpp`: Entry point, system startup.
  - `system_controller.cpp`: Hardware init and coordination.
- `lib/`: Reusable hardware drivers.
  - `drivers/`: Low-level device drivers (DS3231, PCA9685, WS2812).
  - `include/`: Abstract interfaces for drivers.
- `include/`: Project-wide headers.

## Development Guide

### Adding a New LED Effect
1. **Implement Logic**: Add a new method `run_my_effect(uint32_t dt_ms)` in `DisplayDaemon`.
   - Use `apply_backlight_to_all(state)` or manipulate `led_driver_` directly.
2. **Register Effect**:
   - Add a new entry to the `LedEffectType` enum in `DisplayDaemon.h`.
   - Update `DisplayDaemon::process_message` to handle the new effect type.
   - Update `DisplayDaemon::update_effects` to call your new method.
