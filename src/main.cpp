#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <math.h>

// CYD CN1, confirmed by the owner. Do not initialize the RGB LED on GPIO22.
constexpr int SDA_PIN = 27;
constexpr int SCL_PIN = 22;
constexpr uint8_t SENSOR_ADDRESS = 0x28;
constexpr float RHO = 1.225f; // Standard density: indicated airspeed, not true airspeed.
constexpr float SPEED_ZERO_BAND_PA = 3.0f; // Suppress near-zero noise only in speed output.
constexpr float PSI_PA = 6894.757f;
constexpr unsigned ZERO_SAMPLES = 150;
// CYD touch (XPT2046) sits on its own SPI pins, separate from the display (HSPI).
constexpr int TOUCH_CLK = 25, TOUCH_MISO = 39, TOUCH_MOSI = 32, TOUCH_CS = 33;
constexpr uint32_t TOUCH_HOLD_MS = 1000; // Hold the DIFF PRESS card to start ZERO.
// DIFF PRESS card (middle one): x=112..208, y=145..211, plus a small margin.
constexpr int CARD_X0 = 108, CARD_X1 = 212, CARD_Y0 = 141, CARD_Y1 = 215;
// The touch library uses the global SPI object (VSPI); the display uses HSPI.
XPT2046_Touchscreen touch(TOUCH_CS);
TFT_eSPI lcd;
TFT_eSprite screen(&lcd);
bool buffered = false;
Preferences prefs;
float zeroPa = 0, filteredPa = 0, temperatureC = 0;
float zeroSum = 0, zeroMin = 0, zeroMax = 0;
unsigned zeroCount = 0;
bool calibrated = false, zeroing = false, haveFilter = false;
uint32_t lastValid = 0, lastRead = 0, lastDisplay = 0, zeroStarted = 0;
uint32_t lastButtonChange = 0;
bool buttonWasDown = false;
const char *sensorState = "WAITING";

void logLcdRegister(uint8_t command) {
    SPIClass &spi = lcd.getSPIinstance();
    spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(15, LOW);
    digitalWrite(2, LOW);
    spi.transfer(command);
    digitalWrite(2, HIGH);
    Serial.printf("LCD raw %02X:", command);
    for (unsigned i = 0; i < 6; ++i) Serial.printf(" %02X", spi.transfer(0));
    digitalWrite(15, HIGH);
    spi.endTransaction();
    Serial.println();
}

void startZero() {
    zeroing = true;
    zeroCount = 0;
    zeroSum = 0;
    zeroStarted = millis();
    Serial.println("ZERO: stop airflow; collecting 150 fresh samples");
}

// Holding the DIFF PRESS card for TOUCH_HOLD_MS starts the manual ZERO,
// same as BOOT / serial 'z'. A single hold fires once until the finger is lifted.
void handleTouch(uint32_t now) {
    static uint32_t lastPoll = 0, pressStart = 0;
    static bool fired = false, wasTouched = false;
    if (now - lastPoll < 20) return;
    lastPoll = now;
    bool inside = false;
    const bool touched = touch.touched();
    if (touched) {
        const TS_Point p = touch.getPoint();
        const int x = map(p.x, 200, 3700, 0, 320);
        const int y = map(p.y, 240, 3800, 0, 240);
        if (!wasTouched) Serial.printf("TOUCH raw=%d,%d -> x=%d y=%d\n", p.x, p.y, x, y);
        inside = x >= CARD_X0 && x <= CARD_X1 && y >= CARD_Y0 && y <= CARD_Y1;
    }
    wasTouched = touched;
    if (!touched) {
        // Re-arm only after the finger is really lifted.
        pressStart = 0;
        fired = false;
    } else if (!inside) {
        pressStart = 0; // Sliding off the card restarts the hold timer.
    } else if (!fired) {
        if (!pressStart) pressStart = now;
        else if (now - pressStart >= TOUCH_HOLD_MS) {
            fired = true; // Latch even if ZERO is already running: one hold = one trigger.
            if (!zeroing) {
                Serial.println("TOUCH: DIFF PRESS held -> ZERO");
                startZero();
            }
        }
    }
}

bool readSensor(float &pa, float &tc) {
    if (Wire.requestFrom(SENSOR_ADDRESS, uint8_t(4)) != 4) {
        while (Wire.available()) Wire.read();
        sensorState = "I2C: NO SENSOR";
        return false;
    }
    uint8_t b[4];
    for (auto &v : b) v = Wire.read();
    const unsigned status = b[0] >> 6;
    if (status != 0) {
        sensorState = status == 2 ? "STALE DATA" : "SENSOR FAULT";
        return false;
    }
    const unsigned raw = ((b[0] & 0x3f) << 8) | b[1];
    const unsigned rawT = ((unsigned(b[2]) << 8) | b[3]) >> 5;
    if (raw <= 1638 || raw >= 14745) {
        sensorState = "PRESSURE RANGE";
        return false;
    }
    // MS4525DO type A, differential +/-1 psi, 10..90% transfer function.
    // Positive with the dynamic tube on the top port (ArduPilot convention).
    pa = -(((float(raw) - 0.1f * 16383.0f) / (0.8f * 16383.0f)) * 2.0f - 1.0f) * PSI_PA;
    tc = float(rawT) * 200.0f / 2047.0f - 50.0f;
    sensorState = "OK";
    return true;
}

template <typename Canvas>
void paintScreen(Canvas &canvas, bool fresh, bool ready, float dp, float speed) {
    const uint16_t bg = lcd.color565(15, 27, 41);
    const uint16_t card = lcd.color565(31, 49, 67);
    const uint16_t muted = lcd.color565(146, 170, 190);
    const uint16_t white = lcd.color565(244, 247, 252);
    const uint16_t green = lcd.color565(151, 231, 178);
    const uint16_t amber = lcd.color565(255, 201, 106);
    canvas.fillScreen(bg);
    canvas.setTextDatum(TL_DATUM);
    canvas.setTextColor(muted, bg);
    canvas.drawString("AIR SPEED METER", 10, 10, 1);
    canvas.fillRect(240, 4, 80, 20, bg);
    canvas.setTextColor(fresh ? green : TFT_RED, bg);
    canvas.drawString(fresh ? "SENSOR OK" : "NO DATA", 258, 10, 1);
    canvas.fillCircle(249, 13, 2, fresh ? green : TFT_RED);

    canvas.setTextDatum(MC_DATUM);
    canvas.setTextColor(muted, bg);
    canvas.drawString("AIRSPEED", 160, 43, 2);
    canvas.setTextColor(white, bg);
    canvas.setFreeFont(&FreeSansBold24pt7b);
    canvas.drawString(ready ? String(speed * 3.6f, 1) : String("--.-"), 160, 80);
    canvas.setTextFont(1);
    canvas.setTextColor(muted, bg);
    canvas.drawString("km/h", 160, 114, 2);

    const char *hint = "INDICATED AIRSPEED";
    if (zeroing) hint = "ZEROING: KEEP AIR STILL";
    else if (!fresh) hint = sensorState;
    else if (!calibrated) hint = "STOP FLOW + HOLD DIFF PRESS";
    else if (dp < -10) hint = "REVERSE: CHECK PITOT TUBES";
    const bool reverse = !zeroing && fresh && calibrated && dp < -10;
    canvas.setTextColor(reverse ? TFT_RED : (ready && dp >= -10 ? muted : amber), bg);
    canvas.drawString(hint, 160, 131, 1);

    const char *labels[] = {"SPEED", "DIFF PRESS", "SENSOR TEMP"};
    const char *units[] = {"m/s", "Pa", "C"};
    String values[] = {ready ? String(speed, 1) : String("--.-"),
                       fresh ? String(dp, 0) : String("---"),
                       fresh ? String(temperatureC, 1) : String("--.-")};
    for (int i = 0; i < 3; ++i) {
        const int x = 10 + i * 102;
        const int center = x + 48;
        canvas.fillRoundRect(x, 145, 96, 66, 7, card);
        canvas.setTextColor(muted, card);
        canvas.drawString(labels[i], center, 156, 1);
        canvas.setTextColor(white, card);
        canvas.drawString(values[i], center, 179, 4);
        canvas.setTextColor(muted, card);
        canvas.drawString(units[i], center, 201, 1);
    }
    // Clear the whole footer, including the previous status text's footprint.
    canvas.fillRect(0, 216, 320, 24, bg);
    canvas.setTextDatum(TL_DATUM);
    canvas.setTextColor(muted, bg);
    canvas.drawString("rho 1.225 kg/m3", 10, 225, 1);
    canvas.setTextColor(calibrated && !zeroing ? green : amber, bg);
    // Keep ZERO at a fixed left edge; only the separate detail field changes.
    canvas.drawString("ZERO", 232, 225, 1);
    const String zeroDetail = zeroing ? String(zeroCount) + "/150" :
                              calibrated ? "CAL OK" : "REQUIRED";
    canvas.drawString(zeroDetail, 268, 225, 1);
}

void drawScreen() {
    const bool fresh = lastValid && millis() - lastValid < 500;
    const bool ready = fresh && calibrated && !zeroing;
    const float dp = filteredPa - zeroPa;
    const float speed = fabsf(dp) <= SPEED_ZERO_BAND_PA ? 0.0f : sqrtf(2.0f * fabsf(dp) / RHO);
    if (buffered) {
        paintScreen(screen, fresh, ready, dp, speed);
        static int previousZeroState = -1;
        const int zeroState = zeroing ? 2 : calibrated ? 1 : 0;
        if (zeroState != previousZeroState) {
            // Also invalidate the physical footer when switching calibration states.
            lcd.fillRect(0, 216, 320, 24, lcd.color565(15, 27, 41));
            previousZeroState = zeroState;
        }
        static int previousSensorState = -1;
        const int sensorStatus = fresh ? 1 : 0;
        if (sensorStatus != previousSensorState) {
            lcd.fillRect(240, 4, 80, 20, lcd.color565(15, 27, 41));
            previousSensorState = sensorStatus;
        }
        screen.pushSprite(0, 0);
    } else {
        paintScreen(lcd, fresh, ready, dp, speed);
    }
    Serial.printf("state=%s calibrated=%d zeroing=%d dp_Pa=%.2f IAS_mps=%.2f temp_C=%.2f\n",
                  fresh ? sensorState : "NO_FRESH_DATA", calibrated, zeroing,
                  fresh ? dp : NAN, ready ? speed : NAN, fresh ? temperatureC : NAN);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("AIRSPEED v6 dashboard | fixed header/footer | CYD ST7789 | speed zero band +/-3Pa");
    pinMode(0, INPUT_PULLUP);
    // Keep the unused SD controller deselected; touch CS is driven by the touch library.
    pinMode(5, OUTPUT); digitalWrite(5, HIGH);
    SPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin();
    touch.setRotation(1);
    lcd.init();
    lcd.setRotation(1);
    logLcdRegister(0x04);
    logLcdRegister(0xD3);
    logLcdRegister(0x09);
    logLcdRegister(0xDA);
    logLcdRegister(0xDB);
    logLcdRegister(0xDC);
    Serial.printf("LCD driver=ST7789 rotation=%d size=%dx%d\n", lcd.getRotation(), lcd.width(), lcd.height());
    lcd.fillScreen(TFT_BLACK);
    pinMode(21, OUTPUT); digitalWrite(21, HIGH);
    screen.setColorDepth(16);
    buffered = screen.createSprite(320, 240) != nullptr;
    if (!buffered) {
        screen.setColorDepth(8);
        buffered = screen.createSprite(320, 240) != nullptr;
    }
    Serial.printf("Display buffer=%d free_heap=%u\n", buffered, ESP.getFreeHeap());
    prefs.begin("airspeed", false);
    calibrated = prefs.getBool("cal", false);
    zeroPa = prefs.isKey("zero") ? prefs.getFloat("zero", 0) : 0;
    if (!isfinite(zeroPa) || fabsf(zeroPa) > 1000) calibrated = false;
    Wire.begin(SDA_PIN, SCL_PIN, 100000);
    Wire.setTimeOut(30);
    Serial.println("I2C scan:");
    for (uint8_t address = 1; address < 127; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) Serial.printf("  found 0x%02X\n", address);
    }
    Serial.println("Press BOOT after startup or send z to zero in still air.");
}

void loop() {
    const uint32_t now = millis();
    while (Serial.available()) {
        const char c = Serial.read();
        if (c == 'z' || c == 'Z') startZero();
    }
    const bool down = digitalRead(0) == LOW;
    if (down != buttonWasDown && now - lastButtonChange > 50) {
        buttonWasDown = down;
        lastButtonChange = now;
        if (down) startZero();
    }
    handleTouch(now);
    if (now - lastRead >= 20) {
        lastRead = now;
        float pa, tc;
        if (readSensor(pa, tc)) {
            lastValid = now;
            temperatureC = tc;
            filteredPa = haveFilter ? filteredPa + 0.12f * (pa - filteredPa) : pa;
            haveFilter = true;
            if (zeroing) {
                if (!zeroCount) zeroMin = zeroMax = pa;
                zeroMin = fminf(zeroMin, pa); zeroMax = fmaxf(zeroMax, pa);
                zeroSum += pa;
                if (++zeroCount >= ZERO_SAMPLES) {
                    zeroing = false;
                    const float candidate = zeroSum / zeroCount;
                    if (zeroMax - zeroMin > 40 || fabsf(candidate) > 500) {
                        Serial.println("ZERO REJECTED: pressure unstable or too large; stop airflow and retry.");
                    } else {
                        zeroPa = candidate;
                        filteredPa = zeroPa;
                        calibrated = true;
                        prefs.putFloat("zero", zeroPa);
                        prefs.putBool("cal", true);
                        Serial.printf("ZERO SAVED: %.3f Pa\n", zeroPa);
                    }
                }
            }
        }
    }
    if (zeroing && now - zeroStarted > 10000) {
        zeroing = false;
        Serial.println("ZERO TIMEOUT: insufficient fresh samples; previous zero retained.");
    }
    if (now - lastDisplay >= 250) { lastDisplay = now; drawScreen(); }
    delay(1);
}
