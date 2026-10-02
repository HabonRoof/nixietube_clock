# Agent notes

## Toolchain

Build with PlatformIO platform `espressif32` 6.12.0. That platform installs `framework-espidf` 3.50500.0, which is ESP-IDF 5.5.0 (`~/.platformio/packages/framework-espidf/version.txt`).

`platformio.ini` pins `platform = espressif32@6.12.0` for `esp32_s3_nixie` (default) and `esp32_s3_devkitc_1`.

PlatformIO reads `sdkconfig.esp32_s3_nixie` and `sdkconfig.esp32_s3_devkitc_1`. `dependencies.lock` records IDF 5.5.0. Keep those files on this stack.

## Cursor cloud environment

This repo does not commit `.cursor/environment.json`. The cloud environment is the dashboard-managed personal environment. Its `install` command should be:

```bash
bash scripts/install-espressif32-6.12.sh
```

The script is idempotent. It installs PlatformIO Core into `~/.platformio/penv` when that venv is missing, links `pio` onto `/usr/local/bin`, removes an installed `espressif32` 7.x platform and ESP-IDF 6.1 packages (`framework-espidf` 4.60100.0, xtensa/riscv toolchains `15.2.0+20251204`, `tool-esptoolpy` 2.41100.260830) when they are present, then installs `platformio/espressif32@6.12.0` plus `framework-espidf` 3.50500.0 and the IDF 5.5 toolchains (`toolchain-xtensa-esp-elf` 14.2.0+20241119, `toolchain-riscv32-esp` 14.2.0+20241119). It exits nonzero unless `version.txt` is `5.5.0`. A platform-only `pio pkg install -p` does not download the optional framework package, so the script installs that package itself.

Until that script is on the default branch, a dashboard `install` that cannot see this file can run the same steps inline. After this branch is merged, point `install` at the script.

## Verify

```bash
bash scripts/install-espressif32-6.12.sh
pio run -e esp32_s3_nixie
pio run -e esp32_s3_devkitc_1
```
