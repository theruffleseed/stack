#include "weather.h"
#include "config.h"

#include <Arduino.h>
#include <Wire.h>

namespace {

float lastTemp = 0.0f;
float lastHum = 0.0f;
bool available = false;
unsigned long lastReadMs = 0;

// 0x3517 = wake command, 0x7CA2 = measure T+RH (normal mode, clock stretching).
// The sensor stretches I2C until measurement completes (~12ms), so
// Wire.endTransmission() blocks briefly.
bool shct3Command(uint16_t cmd) {
    Wire.beginTransmission(SHTC3_I2C_ADDR);
    Wire.write(cmd >> 8);
    Wire.write(cmd & 0xFF);
    return Wire.endTransmission() == 0;
}

bool shct3Read(float &t, float &h) {
    Wire.requestFrom((int)SHTC3_I2C_ADDR, 6);
    if (Wire.available() != 6) return false;

    uint16_t rawT = ((uint16_t)Wire.read() << 8) | Wire.read();
    Wire.read(); // CRC (skipped)
    uint16_t rawH = ((uint16_t)Wire.read() << 8) | Wire.read();
    // Wire.read(); // CRC (skipped)

    t = -45.0f + 175.0f * (rawT / 65535.0f);
    h = 100.0f * (rawH / 65535.0f);
    if (h > 100.0f) h = 100.0f;
    if (h < 0.0f) h = 0.0f;
    return true;
}

} // namespace

namespace Weather {

bool begin() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(100000);

    // Wake the sensor once to check it responds
    if (!shct3Command(0x3517)) {
        Serial.println("Weather: SHTC3 not responding (wake failed)");
        return false;
    }
    delay(1);
    if (!shct3Command(0x7CA2)) {
        Serial.println("Weather: SHTC3 not responding (measure failed)");
        return false;
    }
    float t, h;
    if (!shct3Read(t, h)) {
        Serial.println("Weather: SHTC3 read failed");
        return false;
    }
    lastTemp = t;
    lastHum = h;
    lastReadMs = millis();
    available = true;
    Serial.printf("Weather: SHTC3 online  %.1f C  %.1f %%\n", t, h);
    return true;
}

bool read(float &t, float &h) {
    if (!available) return false;

    // Throttle: don't hammer the sensor faster than the configured interval
    unsigned long now = millis();
    if (now - lastReadMs < WEATHER_READ_INTERVAL_MS) {
        t = lastTemp;
        h = lastHum;
        return true;
    }

    if (!shct3Command(0x3517)) return false; // wake
    delay(1);
    if (!shct3Command(0x7CA2)) return false; // measure
    if (!shct3Read(t, h)) return false;

    lastTemp = t;
    lastHum = h;
    lastReadMs = now;
    return true;
}

float temperature() { return lastTemp; }
float humidity() { return lastHum; }
bool isAvailable() { return available; }

} // namespace Weather
