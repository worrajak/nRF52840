#!/usr/bin/env python3
import serial
import time
import sys

port = "COM11"
baud = 115200

try:
    ser = serial.Serial(port, baud, timeout=2)
    print(f"Opened {port}")
    time.sleep(2)
    
    start = time.time()
    timeout = 20  # 20 second timeout
    
    while time.time() - start < timeout:
        if ser.in_waiting:
            try:
                line = ser.readline().decode('utf-8', errors='replace').rstrip('\r\n')
                if line:
                    print(line)
            except:
                pass
        time.sleep(0.01)
    
    print("\n[Timeout - no more output]")
    ser.close()
    
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)
