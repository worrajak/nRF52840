#!/usr/bin/env python3
"""
rssi_logger.py — ดึง RSSI log จาก nRF52840 ผ่าน Serial
ใช้: python3 rssi_logger.py /dev/tty.usbmodemXXXX [--output data.csv]
"""
import serial
import sys
import time
import argparse

def main():
    parser = argparse.ArgumentParser(description="ดึง RSSI log จาก nRF52840")
    parser.add_argument("port", help="Serial port (e.g. /dev/tty.usbmodem1234)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--output", "-o", default="rssi_log.csv", help="Output CSV file")
    args = parser.parse_args()

    print(f"Connecting to {args.port} @ {args.baud}...")
    ser = serial.Serial(args.port, args.baud, timeout=5)
    time.sleep(2)  # wait for board reset

    # Flush any boot messages
    ser.reset_input_buffer()

    print("Sending DATADUMP command...")
    ser.write(b"DATADUMP\n")
    time.sleep(1)

    lines = []
    capturing = False
    start_time = time.time()

    while time.time() - start_time < 10:  # 10s timeout
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if line == "=== RSSI LOG DUMP ===":
            capturing = True
            print("  Capturing...")
            continue
        if line == "=== END ===":
            capturing = False
            print("  Done!")
            break
        if capturing:
            lines.append(line)
            sys.stdout.write(f"\r  {len(lines)} samples")
            sys.stdout.flush()

    ser.close()

    if not lines:
        print("No data received. Check port and baud rate.")
        return

    # Write CSV
    with open(args.output, "w") as f:
        # Header line first
        if lines and lines[0].startswith("ts"):
            f.write(lines[0] + "\n")
            lines = lines[1:]
        else:
            f.write("ts_ms,src,hop,rssi,snr,flags\n")
        for line in lines:
            f.write(line + "\n")

    print(f"\nSaved {len(lines)} samples to {args.output}")
    print(f"Run: python3 train_model.py {args.output}")

if __name__ == "__main__":
    main()