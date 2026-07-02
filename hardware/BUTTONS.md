# Button GPIO Assignments (firmware v5+)

| Net    | GPIO  | Function                          |
|--------|-------|-----------------------------------|
| BTN_0  | GPIO8 | Alarm stop / divergence re-trigger |
| BTN_1  | GPIO12| Display mode cycle                |
| BTN_2  | GPIO13| Backlight profile cycle           |

Circuit (each button): 10k pull-up to +3V3, tact switch to GND (active low).

`~RTC_INT` (DS3231) is on **GPIO2**, not GPIO8.

When adding SW2/SW3 to the main board PCB, duplicate the SW1 (`XKB5858-W-E-75`) circuit and route nets `BTN_1` / `BTN_2` to ESP32-S3 IO12 and IO13.
