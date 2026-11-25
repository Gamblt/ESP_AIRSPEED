#!/usr/bin/env bash
# flash.sh - writes the AIRSPEED firmware images from this folder to an ESP32 CYD.
#
# USAGE (run from anywhere):
#   ./flash.sh                      auto-detect the serial port
#   ./flash.sh /dev/cu.usbserial-0001   use a specific port
#   PORT=/dev/ttyUSB0 BAUD=115200 ./flash.sh
#   DRY_RUN=1 ./flash.sh            only print the esptool command
#
# FILES (placed here automatically by tools/copy_to_bin.py after `pio run`):
#   bootloader.bin  -> 0x1000     partitions.bin -> 0x8000
#   boot_app0.bin   -> 0xe000     firmware.bin   -> 0x10000
# These are the standard ESP32 offsets for the 4 MB "default" partition table
# used by this project. The NVS area (0x9000) with the saved ZERO calibration
# is NOT written, so flashing keeps the owner's calibration.
#
# REQUIREMENTS: esptool (`pip install esptool`) and a USB-serial driver for
# the board's CH340/CP210x chip. Close any serial monitor before flashing.
#
# WRITTEN FOR: macOS (bash/zsh). Linux works unchanged, apart from the
# port names (/dev/ttyUSB0 or /dev/ttyACM0) and needing permission to use
# the port: `sudo usermod -aG dialout $USER`, then log in again.
#
# ADAPTING TO WINDOWS: the script is bash, so use Git Bash/WSL, or run the
# same command in PowerShell with COM ports instead of /dev/... names:
#   esptool --chip esp32 --port COM10 --baud 230400 write-flash `
#     0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
# (older esptool versions are called `esptool.py`; the Windows port is shown
# in Device Manager -> Ports.) Only the port detection below is OS specific.
#
# BAUD: 230400 is used because 460800 gave corrupted transfers with this
# board's cable/adapter. Lower it (115200) if you still see errors.

set -euo pipefail
cd "$(dirname "$0")"

BAUD="${BAUD:-230400}"
PORT="${1:-${PORT:-}}"
FILES=(bootloader.bin partitions.bin boot_app0.bin firmware.bin)

for f in "${FILES[@]}"; do
    [[ -f "$f" ]] || { echo "Missing $f. Run 'pio run -e cyd' in the project root first." >&2; exit 1; }
done

# esptool v5 is `esptool`, older versions `esptool.py`; fall back to the module.
if command -v esptool >/dev/null 2>&1; then ESPTOOL=(esptool)
elif command -v esptool.py >/dev/null 2>&1; then ESPTOOL=(esptool.py)
elif python3 -m esptool version >/dev/null 2>&1; then ESPTOOL=(python3 -m esptool)
else echo "esptool not found. Install it: pip install esptool" >&2; exit 1; fi

# Port auto-detection (OS specific part: macOS names first, then Linux).
if [[ -z "$PORT" ]]; then
    shopt -s nullglob
    candidates=(/dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/ttyUSB* /dev/ttyACM*)
    shopt -u nullglob
    if [[ ${#candidates[@]} -eq 1 ]]; then
        PORT="${candidates[0]}"
    else
        echo "Found ${#candidates[@]} serial ports; pass the right one: ./flash.sh <port>" >&2
        printf '  %s\n' "${candidates[@]}" >&2
        exit 1
    fi
fi

CMD=("${ESPTOOL[@]}" --chip esp32 --port "$PORT" --baud "$BAUD" write-flash
     0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin)

echo "Flashing to $PORT at $BAUD baud:"
echo "  ${CMD[*]}"
[[ "${DRY_RUN:-0}" == 1 ]] && exit 0
"${CMD[@]}"
