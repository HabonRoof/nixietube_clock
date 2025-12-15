WS2812 support

- `Kconfig.projbuild` provides `CONFIG_WS2812_LED_COUNT` (default 2).
- WS2812 API and control object:
  - `ws2812_t *ws2812_create(gpio_num_t pin, size_t led_count)` — pass 0 to use `CONFIG_WS2812_LED_COUNT` or the default 24 LEDs.
  - `ws2812_set_group(strip, group_index, r,g,b)` — sets color for a group (6 groups of 4 LEDs each).
  - `ws2812_set_pixel_ws`, `ws2812_show_ws`, `ws2812_set_all_ws`, `ws2812_destroy` are also available.
  - Legacy wrappers (`ws2812_init`, `ws2812_set_pixel`, `ws2812_show`) remain for compatibility.
- Demo: define `WS2812_DEMO` to enable the group demo in `src/main.c`. By default it is enabled.

Testing

1. Build and flash for `esp32_s3_devkitc_1`:

```bash
platformio run -e esp32_s3_devkitc_1 -t upload
platformio device monitor -e esp32_s3_devkitc_1 -b 115200
```

2. Expect to see the WS2812 demo patterns on the chained LEDs.
