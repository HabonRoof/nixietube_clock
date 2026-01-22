# CLI Test Plan

This document outlines the test plan for the Nixie Clock CLI.

## Prerequisites

1.  **Hardware**: Nixie Clock device connected via USB/UART.
2.  **Software**: Python 3 installed on the host machine.
3.  **Dependencies**: `pyserial` library (`pip install pyserial`).

## Manual Testing

1.  **Connect**: Open a serial terminal (e.g., PuTTY, screen, or VSCode Serial Monitor) to the device's COM port at 115200 baud.
2.  **Help**: Type `help` and press Enter. Verify that `nixie_clock` is listed.
3.  **Set Nixie Number**:
    *   Command: `set_nixie --number 123456`
    *   Expected Result: The Nixie tubes should display `123456`.
4.  **Set Backlight Color (Red)**:
    *   Command: `set_backlight --rgb 255,0,0 --brightness 255`
    *   Expected Result: The backlight LEDs should turn bright red.
5.  **Set Backlight Color (Green, Dim)**:
    *   Command: `set_backlight --rgb 0,255,0 --brightness 50`
    *   Expected Result: The backlight LEDs should turn dim green.
6.  **Get UUID**:
    *   Command: `get_uuid`
    *   Expected Result: Output similar to `UUID: AABBCCDDEEFF`.
7.  **Get HW Version**:
    *   Command: `get_hw_version`
    *   Expected Result: Output similar to `HW Version: ESP32-S3 (Rev 1), Board: v1.0`.
8.  **Get FW Version**:
    *   Command: `get_fw_version`
    *   Expected Result: Output similar to:
        ```
        App Version: a1b2c3d...
        IDF Version: v5.x.x...
        ```
9.  **Invalid Command**:
    *   Command: `set_nixie --number` (missing value)
    *   Expected Result: Error message indicating missing argument.

## Automated Testing

A Python script `test_cli.py` is provided to automate sending commands.

### Usage

```bash
python test_cli.py <port>
```

Example:
```bash
python test_cli.py /dev/ttyUSB0
```

The script will send a sequence of commands and prompt you to verify the visual output on the device.