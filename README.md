# Nixie Clock Firmware

ESP-IDF firmware for a 6-tube Nixie clock powered by an ESP32-S3. The clock drives multiplexed IN-18 (or IN-4) tubes with RGB backlighting, audio playback, battery management, and a built-in web configuration UI.

## Features

- **6-tube display** — Multiplexed control via PCA9685 PWM drivers and a 74HC238 anode mux.
- **RGB backlight** — WS2812 addressable LEDs (4 per tube) with static, breath, rainbow, and off effects.
- **Audio** — DFPlayer Mini integration for sound effects and announcements.
- **RTC** — DS3231 for precise timekeeping with periodic resync to ESP32 system time.
- **Web UI** — Wi-Fi access point and HTTP server for settings and time configuration from a phone or laptop.
- **Serial CLI** — Interactive console for development, diagnostics, and power-rail control.
- **Power management** — BQ25601 battery charger, BQ27441 fuel gauge, and switched HV / DFPlayer rails.
- **Persistent settings** — Clock preferences stored in NVS and restored on boot.
- **Modular architecture** — FreeRTOS daemons coordinated by a central system controller.

## Hardware Configuration

Pin assignments are defined in `src/system_controller.cpp` and `lib/drivers/power_switch/gpio_power_switch.h`.

| Peripheral | Pin Name | GPIO | Description |
| :--- | :--- | :--- | :--- |
| **I2C** | SDA | GPIO 6 | I2C data (DS3231, PCA9685, BQ25601, BQ27441) |
| | SCL | GPIO 5 | I2C clock (400 kHz) |
| **UART** | TX | GPIO 18 | Audio TX → DFPlayer RX |
| | RX | GPIO 17 | Audio RX ← DFPlayer TX |
| **GPIO** | RTC_INT | GPIO 2 | DS3231 interrupt (active low) |
| | BTN_0 | GPIO 8 | Alarm stop / divergence re-trigger |
| | BTN_1 | GPIO 12 | Display mode cycle |
| | BTN_2 | GPIO 13 | Backlight profile cycle |
| | PCA_OE | GPIO 4 | PCA9685 output enable (active low) |
| | ANODE_A0–A2 | GPIO 9–11 | 74HC238 anode mux address lines |
| | HV_PWR | GPIO 15 | HV power rail enable (via `GpioPowerSwitch`) |
| | DF_PWR | GPIO 16 | DFPlayer power rail enable (via `GpioPowerSwitch`) |
| **RMT** | LED_DATA | GPIO 7 | WS2812 LED data line |

KiCad schematics and PCB layouts for the main board, display boards (IN-18 / IN-4), and HV power supply live under `hardware/`.

## Architecture

The firmware runs on FreeRTOS and uses a daemon-based design. `SystemState` holds shared, thread-safe snapshots of settings, battery, and time status.

```mermaid
flowchart TB
    subgraph Services
        Web[WebServer]
        CLI[CliDaemon]
    end

    SC[SystemController]
    DD[DisplayDaemon]
    AD[AudioDaemon]
    CC[ChargerController]
    PC[PowerController]

    Web --> SC
    CLI --> SC
    CLI --> CC
    CLI --> PC
    CLI --> AD

    SC --> DD
    SC --> AD
    SC --> SystemState[(SystemState / NVS)]

    DD --> Nixie[NixieDriver / PCA9685]
    DD --> LED[LedDriver / WS2812]
    AD --> Audio[AudioDriver / DFPlayer]
    AD --> PC
    SC --> RTC[DS3231]
    SC --> GG[GasgaugeService / BQ27441]
    CC --> Charger[BQ25601]
    PC --> Rails[HV & DFPlayer rails]
```

### System Controller (`src/system_controller.cpp`)

Central coordinator that:

- Initializes I2C, UART, RMT, and GPIO peripherals.
- Owns the DS3231 RTC and maintains ESP32 system time (UTC).
- Periodically resyncs from the RTC and publishes time status.
- Polls the BQ27441 fuel gauge and updates battery status.
- Dispatches 1 Hz time updates to `DisplayDaemon`.
- Accepts thread-safe settings changes from the web UI and CLI.

### Display Daemon (`src/daemons/display_daemon.cpp`)

Manages all visual output:

- Drives Nixie tubes via `NixieDriver` (dedicated high-priority multiplexing task).
- Drives LED backlights via `LedDriver`.
- Runs the LED effects engine (static, breath, rainbow, off).
- Updates hardware at 50 Hz.

### Audio Daemon (`src/daemons/audio_daemon.cpp`)

Handles asynchronous audio commands (play, stop, volume) and communicates with the DFPlayer Mini. Uses `PowerController` to switch the DFPlayer power rail.

### Web Server (`src/web_server.cpp`, `src/web_page.cpp`)

Starts a Wi-Fi access point and embedded HTTP server:

| Setting | Value |
| :--- | :--- |
| SSID | `NixieClock` |
| Password | `nixie2026` |
| AP IP | `192.168.8.8` |

Connect to the AP and open `http://192.168.8.8/` in a browser.

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/` | GET | Settings web UI |
| `/api/settings` | GET | Current clock settings (JSON) |
| `/api/settings` | POST | Apply settings and optional time (JSON) |
| `/api/time` | GET | Local time, RTC status, temperature (JSON) |

Configurable settings include timezone offset, alarm, backlight color/brightness/effect, and volume.

### CLI Daemon (`src/daemons/cli_daemon.cpp`)

Interactive serial console for development, diagnostics, and factory setup. Connect at **115200 baud** (8N1, no flow control) via USB or UART. The prompt is `nixie_clock> `. Type `help` for a summary of all commands.

```bash
pio run -t monitor
```

#### Display and backlight

| Command | Description |
| :--- | :--- |
| `set_nixie --number <n>` | Display a 6-digit number on the tubes |
| `set_backlight --rgb R,G,B --brightness N` | Set backlight color and brightness (0–255) |

#### Device info

| Command | Description |
| :--- | :--- |
| `get_uuid` | Wi-Fi MAC-based device ID |
| `get_hw_version` | ESP32-S3 chip and board revision |
| `get_fw_version` | Git commit hash and ESP-IDF version |
| `reboot` | Restart the device |

#### RTC and alarm (`rtctool`)

The DS3231 RTC stores **UTC** internally; CLI time and alarm values are **local wall clock** and are converted using the configured timezone offset. Set operations are asynchronous — run `rtctool read` afterward to confirm.

| Subcommand | Description |
| :--- | :--- |
| `rtctool read` | Read local time, UTC epoch, timezone, calibration/OSF status, temperature, and alarm settings |
| `rtctool set_tz <offset_hours>` | Set timezone offset (-12..+14) |
| `rtctool set_time <YYYY-MM-DD HH:MM:SS>` | Calibrate RTC and system clock (marks RTC as calibrated) |
| `rtctool set_alarm <HH:MM:SS> [--enable\|--disable] [--track <n>]` | Set daily alarm time; enables alarm by default |

Example workflow:

```text
nixie_clock> rtctool set_tz 8
nixie_clock> rtctool set_time 2026-07-06 16:30:00
nixie_clock> rtctool set_alarm 07:30:00 --enable --track 1
nixie_clock> rtctool read
```

To disable the alarm without changing its time:

```text
nixie_clock> rtctool set_alarm 07:30:00 --disable
```

#### Power and charging

| Command | Description |
| :--- | :--- |
| `get_bq25601_status` | Read BQ25601 charger status registers |
| `enable_charging` / `disable_charging` | Control battery charging |
| `enable_hv` / `disable_hv` | Control HV power rail |
| `enable_df_power` / `disable_df_power` | Control DFPlayer power rail |

#### DFPlayer audio (`dftool`)

Commands talk to the `AudioDaemon`, which powers the DFPlayer rail automatically when needed. Place MP3 files in an `mp3` folder on the SD card root and name them `0001.mp3`, `0002.mp3`, etc. (up to 9999). Track numbers match the numeric prefix in each filename.

| Subcommand | Description |
| :--- | :--- |
| `dftool list` | List tracks in the SD card `mp3/` folder |
| `dftool status` | Show current track, playback state, and track count |
| `dftool play <track> [--loop]` | Play a track; add `--loop` for repeat |
| `dftool toggle <track>` | Play/pause toggle (same behavior as the web UI) |
| `dftool pause` | Pause playback |
| `dftool resume` | Resume playback |
| `dftool stop` | Stop playback |
| `dftool next` | Play next track |
| `dftool prev` | Play previous track |
| `dftool volume <0-30>` | Set volume (DFPlayer range) |
| `dftool vol_up` / `dftool vol_down` | Step volume up or down |

Example workflow:

```text
nixie_clock> dftool list
nixie_clock> dftool play 1
nixie_clock> dftool status
nixie_clock> dftool pause
nixie_clock> dftool resume
nixie_clock> dftool volume 20
nixie_clock> dftool stop
```

#### Fuel gauge (`ggtool`)

| Subcommand | Description |
| :--- | :--- |
| `ggtool status` | Probe BQ27441 device info and gauging readiness |
| `ggtool read` | Read live SOC, SOH, voltage, and current |
| `ggtool cache` | Read last cached battery values from `SystemState` |
| `ggtool peek <reg_hex> [len]` | Dump raw register bytes |
| `ggtool block [class] [index]` | Dump a 32-byte state block |
| `ggtool config [mAh]` | Configure design capacity |

See `test/test_cli/README.md` for a manual test plan and automated test script.

### Drivers (`lib/drivers/`, `src/*_driver.cpp`)

| Driver | Role |
| :--- | :--- |
| `NixieDriver` | 4× PCA9685 chips driving 6 tubes with multiplexing |
| `LedDriver` | RMT-based WS2812 control |
| `AudioDriver` | High-level DFPlayer Mini interface |
| `Ds3231` | RTC read/write and temperature |
| `Bq25601` | Battery charger IC |
| `Bq27441` | Fuel gauge IC |
| `GpioPowerSwitch` | HV and DFPlayer power-rail switching |

## Building and Flashing

This project uses [PlatformIO](https://platformio.org/) with the ESP-IDF framework.

### Prerequisites

- [PlatformIO](https://platformio.org/install) (CLI or VS Code extension)

### Environments

| Environment | Board | Use case |
| :--- | :--- | :--- |
| `esp32_s3_devkitc_1` (default) | ESP32-S3-DevKitC-1 | General development |
| `eps32_s3_nixie` | Custom `esp32-s3-nixie` | Production Nixie clock board |

### Build

```bash
pio run
```

Build for the Nixie board:

```bash
pio run -e eps32_s3_nixie
```

### Flash

Connect the ESP32-S3 via USB, then:

```bash
pio run -t upload
```

### Monitor

View serial logs (115200 baud):

```bash
pio run -t monitor
```

The build embeds the current git commit hash via `generate_git_version.py`.

## Directory Structure

```
├── src/                  # Application source
│   ├── daemons/          # Display, Audio, and CLI tasks
│   ├── main.cpp          # Entry point and startup sequence
│   ├── system_controller.cpp
│   ├── system_state.cpp  # Shared state and NVS persistence
│   ├── web_server.cpp    # Wi-Fi AP and HTTP API
│   ├── web_page.cpp      # Embedded settings UI
│   ├── power_controller.cpp
│   ├── charger_controller.cpp
│   └── gasgauge_service.cpp
├── lib/
│   ├── drivers/          # Low-level device drivers
│   └── include/          # Driver interfaces
├── hardware/             # KiCad schematics and PCB layouts
├── boards/               # Custom PlatformIO board definitions
├── test/                 # CLI and component tests
└── doc/                  # Datasheets, waveforms, architecture diagrams
```

## Development Guide

### Adding a New LED Effect

1. Add a method such as `run_my_effect(uint32_t dt_ms)` in `DisplayDaemon`.
   Use `apply_backlight_to_all(state)` or manipulate `led_driver_` directly.
2. Add an entry to the `LedEffectType` enum in `display_daemon.h`.
3. Update `DisplayDaemon::process_message` to handle the new effect type.
4. Update `DisplayDaemon::update_effects` to call the new method.
5. Expose the effect in the web UI (`web_page.cpp`) and settings schema (`system_state.h`).

### Applying Settings from a New Interface

Use `SystemController::request_settings_update()` so the system controller task remains the sole owner of RTC and settings state. Read current values from `SystemState` and persist with `SystemState::save_settings()`.
