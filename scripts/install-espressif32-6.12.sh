#!/usr/bin/env bash
# Install the Nixie clock toolchain: PlatformIO espressif32 6.12.0 / ESP-IDF 5.5.0.
# Safe to re-run. Removes espressif32 7.x and framework-espidf 6.1 when present.
set -euo pipefail

PIO_VENV="${HOME}/.platformio/penv"
PIO_PACKAGES="${HOME}/.platformio/packages"
PIO_PLATFORMS="${HOME}/.platformio/platforms"

if [[ ! -x "${PIO_VENV}/bin/pio" ]]; then
  mkdir -p "${HOME}/.platformio"
  python3 -m venv "${PIO_VENV}"
  "${PIO_VENV}/bin/pip" install --upgrade pip
  "${PIO_VENV}/bin/pip" install platformio
fi

link_cli() {
  local dest="/usr/local/bin"
  [[ -d "${dest}" ]] || return 0
  if [[ -w "${dest}" ]]; then
    ln -sfn "${PIO_VENV}/bin/pio" "${dest}/pio"
    ln -sfn "${PIO_VENV}/bin/platformio" "${dest}/platformio"
  elif command -v sudo >/dev/null 2>&1; then
    sudo ln -sfn "${PIO_VENV}/bin/pio" "${dest}/pio"
    sudo ln -sfn "${PIO_VENV}/bin/platformio" "${dest}/platformio"
  fi
}
link_cli

export PATH="${PIO_VENV}/bin:${PATH}"
hash -r || true

pkg_list() {
  pio pkg list -g
}

remove_espressif32_7() {
  local versions ver
  versions="$(pkg_list | sed -n 's/.*espressif32 @ \(7[^ ]*\).*/\1/p' || true)"
  [[ -n "${versions}" ]] || return 0
  while IFS= read -r ver; do
    [[ -z "${ver}" ]] && continue
    echo "Removing platformio/espressif32@${ver}"
    pio pkg uninstall -g --no-save -p "platformio/espressif32@${ver}"
  done <<< "${versions}"
}

remove_if_listed() {
  local needle="$1"
  local spec="$2"
  if pkg_list | grep -F -- "${needle}" >/dev/null; then
    echo "Removing ${spec}"
    pio pkg uninstall -g --no-save -t "${spec}"
  fi
}

remove_espressif32_7
remove_if_listed "framework-espidf @ 4.60100.0" "platformio/framework-espidf@4.60100.0"
remove_if_listed "toolchain-xtensa-esp-elf @ 15.2.0+20251204" "platformio/toolchain-xtensa-esp-elf@15.2.0+20251204"
remove_if_listed "toolchain-riscv32-esp @ 15.2.0+20251204" "platformio/toolchain-riscv32-esp@15.2.0+20251204"
remove_if_listed "tool-esptoolpy @ 2.41100.260830" "platformio/tool-esptoolpy@2.41100.260830"

if [[ -f "${PIO_PACKAGES}/framework-espidf/version.txt" ]]; then
  fw_now="$(tr -d '[:space:]' < "${PIO_PACKAGES}/framework-espidf/version.txt")"
  if [[ "${fw_now}" == 6.1* ]]; then
    echo "Removing leftover framework-espidf ${fw_now}"
    pio pkg uninstall -g --no-save -t "platformio/framework-espidf" || rm -rf "${PIO_PACKAGES}/framework-espidf"
  fi
fi

echo "Installing platformio/espressif32@6.12.0"
pio pkg install -g -p "platformio/espressif32@6.12.0"

# `pio pkg install -p` does not pull optional framework/toolchain packages.
# ESP-IDF builds need these; versions match platform-espressif32 6.12.0.
echo "Installing framework-espidf 3.50500.0 (ESP-IDF 5.5.0) and IDF 5.5 toolchains"
pio pkg install -g -t "platformio/framework-espidf@3.50500.0"
pio pkg install -g -t "platformio/toolchain-xtensa-esp-elf@14.2.0+20241119"
pio pkg install -g -t "platformio/toolchain-riscv32-esp@14.2.0+20241119"
pio pkg install -g -t "platformio/tool-cmake@~3.30.0"
pio pkg install -g -t "platformio/tool-ninja@^1.7.0"
pio pkg install -g -t "platformio/toolchain-esp32ulp@~1.23800.0"
pio pkg install -g -t "platformio/tool-esp-rom-elfs@0.0.1+20241011"
pio pkg install -g -t "espressif/tool-xtensa-esp-elf-gdb@~12.1.0"
pio pkg install -g -t "espressif/tool-riscv32-esp-elf-gdb@~12.1.0"

platform_json=""
for candidate in \
  "${PIO_PLATFORMS}/espressif32/platform.json" \
  "${PIO_PLATFORMS}/espressif32@6.12.0/platform.json"
do
  if [[ -f "${candidate}" ]]; then
    platform_json="${candidate}"
  fi
done

if [[ -z "${platform_json}" ]]; then
  echo "espressif32 platform.json not found after install" >&2
  exit 1
fi

platform_version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "${platform_json}")"
fw_pkg="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "${PIO_PACKAGES}/framework-espidf/.piopm")"
cmake_version="$(python3 - "${PIO_PACKAGES}/framework-espidf/tools/cmake/version.cmake" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
parts = dict(re.findall(r"set\(IDF_VERSION_(MAJOR|MINOR|PATCH) (\d+)\)", text))
print(f"{parts['MAJOR']}.{parts['MINOR']}.{parts['PATCH']}")
PY
)"
# PlatformIO writes version.txt on the first build when the package omits it.
# Create it here so the install matches a local framework that already has 5.5.0.
version_file="${PIO_PACKAGES}/framework-espidf/version.txt"
if [[ ! -f "${version_file}" ]]; then
  printf '%s\n' "${cmake_version}" > "${version_file}"
fi
fw_version="$(tr -d '[:space:]' < "${version_file}")"

echo "espressif32 platform: ${platform_version}"
echo "framework-espidf package: ${fw_pkg}"
echo "ESP-IDF version.txt: ${fw_version}"

if [[ "${platform_version}" != "6.12.0" ]]; then
  echo "Expected espressif32 6.12.0, found ${platform_version}" >&2
  exit 1
fi
if [[ "${fw_version}" != "5.5.0" || "${cmake_version}" != "5.5.0" ]]; then
  echo "Expected ESP-IDF 5.5.0, found version.txt=${fw_version} cmake=${cmake_version}" >&2
  exit 1
fi
if [[ "${fw_pkg}" != 3.50500.* ]]; then
  echo "Expected framework-espidf 3.50500.x, found ${fw_pkg}" >&2
  exit 1
fi

if compgen -G "${PIO_PLATFORMS}/espressif32@7*" > /dev/null; then
  echo "espressif32 7.x platform directory still present" >&2
  exit 1
fi

if pkg_list | grep -E 'espressif32 @ 7|framework-espidf @ 4\.60100|15\.2\.0\+20251204|tool-esptoolpy @ 2\.41100'; then
  echo "ESP-IDF 6.1 / espressif32 7.x packages are still installed" >&2
  exit 1
fi

echo "Toolchain pin OK: espressif32 6.12.0 / ESP-IDF 5.5.0"
pkg_list
