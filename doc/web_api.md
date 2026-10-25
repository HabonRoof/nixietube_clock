# Web API

HTTP JSON API served when the Nixie clock config Wi-Fi AP is active (`NixieClock-XXXX` / `192.168.8.8`). Enter config mode with a **3 s long-press on button 1 + button 2 together**.

All JSON endpoints use `Content-Type: application/json`. Successful mutations return `{"ok": true}`. Errors return HTTP 4xx/5xx with:

```json
{
  "ok": false,
  "error": {
    "code": "invalid_type",
    "field": "alarm.time",
    "message": "must be a string"
  }
}
```

`field` is omitted when the error is not tied to a specific key.

Maximum POST body size: **4096 bytes**.

## Endpoints

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/` | GET | Embedded settings web UI (HTML) |
| `/api/settings` | GET | Current persisted settings |
| `/api/settings` | POST | Apply a partial settings update |
| `/api/time` | GET | Live clock readout and RTC health |
| `/api/audio/tracks` | GET | MP3 track list from SD card |
| `/api/audio/status` | GET | Current playback state |
| `/api/audio/play` | POST | Toggle play/pause for a track |
| `/api/ap/status` | GET | Config AP active flag, SSID, password, session code, remaining seconds |
| `/api/ap/stop` | POST | Deauth connected clients and stop config AP |
| `/api/wifi` | GET | Home WiFi enable flag and SSID (password never returned) |
| `/api/wifi` | POST | Enable or disable home WiFi, and optionally save credentials |
| `/api/wifi` | DELETE | Clear home WiFi credentials |
| `/api/ntp/status` | GET | Last NTP sync timestamp and success flag |

## `GET /api/ap/status`

```json
{
  "active": true,
  "session_code": 3847,
  "ssid": "NixieClock-3847",
  "password": "nixie2026",
  "remaining_sec": 842,
  "client_count": 1
}
```

## `POST /api/ap/stop`

Stops the config AP after deauthenticating connected stations. Response: `{"ok": true}`.

## `GET /api/wifi`

```json
{
  "enabled": false,
  "configured": true,
  "ssid": "MyHomeWiFi"
}
```

`enabled` defaults to false. Scheduled NTP runs only when `enabled` is true and an SSID is saved. While disabled, the clock keeps offline time sync from the settings page.

## `POST /api/wifi`

Enable or disable without changing the saved network:

```json
{ "enabled": true }
```

Save credentials. `password` may be empty. Include `enabled` when the switch is on:

```json
{
  "enabled": true,
  "ssid": "MyHomeWiFi",
  "password": "secret"
}
```

## `DELETE /api/wifi`

Clears stored home WiFi credentials and turns `enabled` off. NTP scheduled sync is skipped until home WiFi is enabled again and an SSID is saved.

## `GET /api/ntp/status`

```json
{
  "configured": true,
  "last_success": true,
  "last_ntp_unix": 1736123456
}
```

`configured` is true only when home WiFi is enabled and an SSID is saved.

## `GET /api/settings`

```json
{
  "schema_version": 1,
  "clock": {
    "timezone_offset_hours": 8,
    "rtc_calibrated": true
  },
  "alarm": {
    "enabled": true,
    "time": "07:30:00",
    "track": 1
  },
  "display": {
    "backlight": {
      "color": { "r": 255, "g": 128, "b": 0 },
      "brightness": 128,
      "effect": "static"
    },
    "nixie": {
      "brightness": 255,
      "transition": "fade"
    }
  },
  "audio": { "volume": 15 },
  "profiles": {
    "active_index": 0,
    "items": [
      { "index": 0, "display": { "...": "..." } }
    ]
  },
  "hibernation": {
    "enabled": false,
    "start": "00:00",
    "end": "07:00"
  }
}
```

### Field reference

| Path | Type | Range / values |
| :--- | :--- | :--- |
| `schema_version` | integer | API schema version (currently `1`) |
| `clock.timezone_offset_hours` | integer | `-12` … `14` |
| `clock.rtc_calibrated` | boolean | `true` after explicit time sync |
| `alarm.enabled` | boolean | |
| `alarm.time` | string | `HH:MM:SS` (local wall clock) |
| `alarm.track` | integer | DFPlayer track number (`1` … `9999`) |
| `display.backlight.color.{r,g,b}` | integer | `0` … `255` |
| `display.backlight.brightness` | integer | `0` … `255` |
| `display.backlight.effect` | string | `static`, `breath`, `rainbow`, `off` |
| `display.nixie.brightness` | integer | `0` … `255` |
| `display.nixie.transition` | string | `instant`, `fade` |
| `audio.volume` | integer | `0` … `30` (DFPlayer hardware range) |
| `profiles.active_index` | integer | `0` … `3` |
| `profiles.items[].index` | integer | Profile slot `0` … `3` |
| `hibernation.enabled` | boolean | |
| `hibernation.start` / `end` | string | `HH:MM` (local time) |

`display` reflects the **active** profile's nixie settings and the top-level backlight fields.

## `POST /api/settings`

Send only the groups you want to change. Unknown keys are rejected.

`persist` defaults to `true`. When `persist` is `false`, only a `display` preview may be sent. The preview is applied to the tubes immediately, except during hibernation or a hibernate peek, and it is **not** written to NVS. It stays until the page sends `preview.cancel`, a later request is saved with `persist: true`, or button 2 changes the profile. `SystemController` creates a preview timer whose callback would restore the saved profile, but the firmware never starts that timer.

### Examples

Live display preview (RAM only, not saved):

```json
{
  "display": { "...": "..." },
  "persist": false
}
```

Discard an active web preview immediately and restore the committed active profile:

```json
{
  "preview": { "cancel": true }
}
```

Set volume only:

```json
{ "audio": { "volume": 20 } }
```

Sync RTC from the browser's local clock:

```json
{
  "clock": {
    "timezone_offset_hours": 8,
    "local_time": "2026-07-13 16:30:00"
  }
}
```

Save current display to profile slot 2 (also activates slot 2):

```json
{
  "display": { "...": "..." },
  "profiles": { "save_index": 2 },
  "persist": true
}
```

Update hibernation schedule:

```json
{
  "hibernation": {
    "enabled": true,
    "start": "23:00",
    "end": "07:00"
  }
}
```

### `profiles` semantics

| Field | Behavior |
| :--- | :--- |
| `save_index` | Copies the current display state (after any `display` fields in the same request) into the given slot and makes it active. Requires `persist: true` (default). |

The web UI switches profiles by tapping a color swatch (`profiles.active_index`). Button 2 cycles the same four slots on the device.

### Web UI behavior

- The **Light** tab sends `persist: false` display previews while sliders move (throttled live updates via pointer events; flush on pointer up).
- **Save 1–4** commits the preview to a profile slot with `persist: true` and clears the preview flag.
- Leaving the Light tab or closing the browser cancels the preview via `preview.cancel`, restores the form from the last committed display, and restores that profile on the tubes unless the clock is hibernating.
- Button 2 cancels an active web preview, then advances to the next profile.

## `GET /api/time`

```json
{
  "local_time": "2026-07-13 16:30:00",
  "unix_utc": 1783938600,
  "time_valid": true,
  "osf": false,
  "temperature": 25.50,
  "clock": {
    "timezone_offset_hours": 8,
    "rtc_calibrated": true
  }
}
```

| Field | Description |
| :--- | :--- |
| `local_time` | Current local wall-clock string |
| `unix_utc` | UTC epoch seconds |
| `time_valid` | Whether the RTC/system time is considered valid |
| `osf` | DS3231 oscillator-stop flag (backup power lost) |
| `temperature` | DS3231 temperature (°C) |
| `clock.timezone_offset_hours` | Configured timezone offset |
| `clock.rtc_calibrated` | Same meaning as in `/api/settings` |

## Audio endpoints

### `GET /api/audio/tracks`

```json
{
  "folder": "mp3",
  "count": 3,
  "tracks": [
    { "id": 1, "name": "mp3/0001.mp3" }
  ]
}
```

On SD read failure, `count` is `0`, `tracks` is `[]`, and `error` is `"query_failed"`.

### `GET /api/audio/status`

```json
{ "track": 1, "state": "playing", "count": 3 }
```

`state` is `playing`, `paused`, or `stopped`.

### `POST /api/audio/play`

```json
{ "track": 1 }
```

Response:

```json
{ "track": 1, "state": "playing" }
```

## Migration from the flat JSON API

The previous API used flat keys and numeric enums. Key changes:

| Old (GET/POST) | New |
| :--- | :--- |
| `tz_offset` | `clock.timezone_offset_hours` |
| `time` (POST) | `clock.local_time` |
| `alarm_enabled` | `alarm.enabled` |
| `alarm_time` | `alarm.time` |
| `alarm_track` | `alarm.track` |
| `backlight_rgb` (`"r,g,b"`) | `display.backlight.color` object |
| `backlight_brightness` | `display.backlight.brightness` |
| `backlight_effect` (`0`–`3`) | `display.backlight.effect` (string) |
| `nixie_brightness` | `display.nixie.brightness` |
| `nixie_transition` (`0`/`1`) | `display.nixie.transition` (`instant`/`fade`) |
| `volume` | `audio.volume` |
| `save_profile_index` | `profiles.save_index` |
| `profiles` (array) | `profiles.items` (array of `{index, display}`) |
| `active_profile_index` (GET) | `profiles.active_index` |
| POST success body `"OK"` | `{"ok": true}` |
| `/api/time` `tz_offset` | `clock.timezone_offset_hours` |
| `/api/time` `rtc_calibrated` (top-level) | `clock.rtc_calibrated` |

Clients should check `schema_version` on `GET /api/settings` before parsing.
