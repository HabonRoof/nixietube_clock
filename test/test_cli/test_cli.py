import sys
import time
import serial

def send_command(ser, cmd):
    print(f"Sending: {cmd}")
    ser.write(f"{cmd}\n".encode())
    time.sleep(0.1)
    # Read response (optional, just to clear buffer)
    while ser.in_waiting:
        print(f"Device: {ser.readline().decode().strip()}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python test_cli.py <port>")
        sys.exit(1)

    port = sys.argv[1]
    baud = 115200

    try:
        ser = serial.Serial(port, baud, timeout=1)
        print(f"Connected to {port} at {baud}")
        time.sleep(2) # Wait for connection to stabilize

        # Test 1: Set Nixie Number
        send_command(ser, "set_nixie --number 123456")
        input("Check device: Do you see 123456? Press Enter to continue...")

        # Test 2: Set Backlight Red
        send_command(ser, "set_backlight --rgb 255,0,0 --brightness 255")
        input("Check device: Is backlight RED? Press Enter to continue...")

        # Test 3: Set Backlight Blue
        send_command(ser, "set_backlight --rgb 0,0,255 --brightness 255")
        input("Check device: Is backlight BLUE? Press Enter to continue...")

        # Test 4: Set Backlight Dim Green
        send_command(ser, "set_backlight --rgb 0,255,0 --brightness 50")
        input("Check device: Is backlight DIM GREEN? Press Enter to continue...")

        # Test 5: Get UUID
        send_command(ser, "get_uuid")
        input("Check console: Did you see the UUID? Press Enter to continue...")

        # Test 6: Get HW Version
        send_command(ser, "get_hw_version")
        input("Check console: Did you see the HW Version? Press Enter to continue...")

        # Test 7: Get FW Version
        send_command(ser, "get_fw_version")
        input("Check console: Did you see the FW Version? Press Enter to continue...")

        print("Test Complete.")
        ser.close()

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()