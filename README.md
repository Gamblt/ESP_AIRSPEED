# AIRSPEED — ESP32 CYD + Matek ASPD-4525

PlatformIO environment: `cyd`, ESP32 Arduino 2.0.17, Espressif32 6.9.0.
TFT_eSPI v2.5.33 is included in `lib/` (upstream license retained).
Source archive: https://github.com/Bodmer/TFT_eSPI/releases/tag/v2.5.33

## Wiring

Confirmed CN1 connection: GPIO27 → SDA, GPIO22 → SCL, GND → GND.
Matek board power: 5 V (manufacturer specifies 4–5.5 V).
I2C signal levels must be 3.3 V. Do not connect the sensor power to CN1 3.3 V.
GPIO22 is shared with the CYD red LED; firmware does not drive that LED.

Display uses the ST7789 variant (v3 fix after distorted ILI9341 output), with
BGR color order, inversion off, landscape rotation 1 (320x240), SPI 10 MHz.
The actual panel returned raw RDID 04 bytes `08 42 A9`, D3 all zero;
this is the ST7789 ID stream with the dummy-bit shift, not an ILI9341 ID.
The wiring remains:
SCLK14, MOSI13, MISO12, CS15, DC2, backlight21.
On startup the firmware logs raw ID registers and logical display dimensions.
Touch (XPT2046, own SPI pins CLK25/MOSI32/MISO39/CS33) is used only to start
zeroing: hold the middle DIFF PRESS card for about 1 second.

The v2 dashboard follows the supplied design: dark background, large km/h
readout, three cards for m/s, differential pressure and sensor temperature,
sensor status and saved-zero status. A frame buffer reduces redraw flicker.
Until zero is calibrated, speed is hidden and the screen explicitly says
`STOP FLOW + PRESS BOOT`; pressure and temperature remain visible.

## Operation

After boot, stop all airflow and press BOOT briefly (do not hold it during reset),
or send `z` over serial at 115200 baud. Zeroing collects 150 fresh samples,
rejects unstable/large offsets and saves the accepted zero in ESP32 NVS.
No zero is inferred automatically at boot. A stable flow cannot reliably be
distinguished from sensor offset: ensure still air yourself before zeroing.

The screen displays indicated airspeed (IAS) using fixed density 1.225 kg/m³,
not density-corrected true airspeed. Sensor temperature is displayed for
diagnostics; it is not ambient-temperature compensation. Negative pressure is
flagged as REVERSE. The pressure magnitude is used for the speed display.
Missing/stale/faulty readings hide the speed after 500 ms.

v4: the speed output (display and serial) is clamped to zero when the absolute
filtered, zero-corrected pressure is at most 3 Pa. The pressure display remains
unchanged. At density 1.225 this hides speeds up to about 2.21 m/s; above this
threshold the original pressure-to-speed formula is used without subtracting
the dead band. This stabilizes the resting display and does not improve
measurement accuracy or replace still-air zero calibration.

This is an initial measurement firmware, not a calibrated reference instrument.
For reference use, validate against a calibrated instrument and account for
air density, probe placement/swirl, pressure tubing, and compressibility.

## Build and upload (PowerShell)

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor
```

Close the serial monitor before uploading. Port is configured as COM10.
On a new computer install PlatformIO Core first (or the PlatformIO extension
for VS Code), then run `pio run -e cyd` from this directory. PlatformIO downloads
the pinned platform, Arduino framework and compiler on the first build; the
included TFT_eSPI library does not need a separate library-registry download.
Adjust upload/monitor ports for the connected board if COM10 is different.
`tools/serial_check.py` also requires Python with `pyserial`; the existing
PlatformIO Python environment already provides it on the original computer.

The original local workspace has a complete 4 MB flash backup at
`backups/original-esp32-4MB.bin`. It is deliberately excluded from source ZIPs:
full backups can contain private settings or credentials.

## Working with agents and sharing

Read `AGENTS.md` first for hardware constraints, build steps and UI regression
rules. See `docs/PROJECT_STATE.md` for implemented features, limitations and
the manual verification checklist. Current source prints v6 at startup;
later footer-coordinate adjustments did not change that version string.

Create and verify a portable source archive from PowerShell:

```powershell
powershell -NoProfile -File tools/package.ps1
```

The ZIP in `dist/` includes application source, PlatformIO configuration,
agent documentation, tools and the complete TFT_eSPI distribution with its
license. It contains `SHA256SUMS.txt`, and a separate `.zip.sha256` file is
written alongside it. It excludes `.pio/`, private flash backups, previous
archives, the redundant `vendor/` download ZIP and installed toolchains.
Extract the archive, open its `AIRSPEED/` folder and build with PlatformIO.

Sensor specifications: https://www.mateksys.com/?portfolio=aspd-4525
Pressure decoding reference: MS4525 type-A differential ±1 PSI transfer
function; https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_Airspeed/AP_Airspeed_MS4525.cpp
