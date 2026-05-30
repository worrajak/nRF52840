#!/usr/bin/env python3
"""Serial monitor for nRF52840 LoRa device output"""

import serial
import sys
import time

def main():
    port = "COM12"
    baudrate = 115200
    
    try:
        print(f"Opening {port} at {baudrate} baud...")
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"Connected! Press Ctrl+C to exit\n")
        
        time.sleep(1)  # Wait for device to be ready
        
        while True:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').rstrip()
                if line:
                    print(f"[{time.strftime('%H:%M:%S')}] {line}")
            time.sleep(0.01)
            
    except KeyboardInterrupt:
        print("\n\nSerial monitor stopped by user")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        try:
            ser.close()
            print("Serial port closed")
        except:
            pass

if __name__ == "__main__":
    main()
