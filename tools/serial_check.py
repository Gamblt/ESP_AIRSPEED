"""Read a bounded startup log without sending commands or changing calibration."""
import sys
import time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM10"
with serial.Serial(port, 115200, timeout=0.5) as device:
    device.dtr = False
    device.rts = False
    end = time.monotonic() + 8
    while time.monotonic() < end:
        line = device.readline()
        if line:
            print(line.decode("utf-8", errors="replace").rstrip(), flush=True)
