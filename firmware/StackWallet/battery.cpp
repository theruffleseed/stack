// AXP2101 PMU readouts for the battery icon (docs/HARDWARE.md: SDA=41,
// SCL=42, address 0x34). Register map follows the datasheet plus
// Waveshare's own axpPower driver for this exact board. All registers are
// read raw on demand; the PMU keeps its own ADC and gauge running.
#include "battery.h"
#include "config.h"
#include <Wire.h>

namespace {

constexpr uint8_t kAddr = AXP2101_ADDR;
constexpr uint8_t kStatus1 = 0x00;   // bit3: battery connected
constexpr uint8_t kStatus2 = 0x01;   // bits7-5: 1=charging, 2=discharging, 0=standby(full)
constexpr uint8_t kAdcChannelCtrl = 0x30; // bit0: battery voltage ADC enable
constexpr uint8_t kBatDetCtrl = 0x68;     // bit0: battery detection enable
constexpr uint8_t kFuelGaugeCtrl = 0xA2;  // bit0: fuel gauge (coulomb counter) enable
constexpr uint8_t kFuelGaugeReset = 0x17; // bit2: fuel gauge reset (pulse to (re)load SOC)
constexpr uint8_t kBatPercent = 0xA4;     // gauge percentage, 0..100

bool readReg(uint8_t reg, uint8_t &val) {
    Wire.beginTransmission(kAddr);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom((uint8_t)kAddr, (uint8_t)1) != 1) return false;
    val = Wire.read();
    return true;
}

bool setRegBit(uint8_t reg, uint8_t bit, bool set) {
    uint8_t v;
    if (!readReg(reg, v)) return false;
    if (set) {
        v |= (uint8_t)(1 << bit);
    } else {
        v &= (uint8_t)~(1 << bit);
    }
    Wire.beginTransmission(kAddr);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

} // namespace

namespace Battery {

void begin() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    setRegBit(kAdcChannelCtrl, 0, true); // battery voltage ADC on
    setRegBit(kBatDetCtrl, 0, true);     // battery detection on
    // The gauge that drives the 0xA4 percentage is disabled by default on
    // the AXP2101 (0xA2 bit0 = 0). Previous firmware only worked because
    // the module's factory code had left it enabled in the PMU's registers;
    // after a gauge/power reset it reads 0. Own the init ourselves: enable
    // the gauge and pulse its reset so it (re)loads the SOC estimate.
    setRegBit(kFuelGaugeCtrl, 0, true);
    setRegBit(kFuelGaugeReset, 2, true);
    setRegBit(kFuelGaugeReset, 2, false);
}

bool present() {
    uint8_t s;
    return readReg(kStatus1, s) && (s & 0x08) != 0;
}

int percent() {
    uint8_t s, p;
    if (!present()) return -1;
    if (!readReg(kBatPercent, p) || p > 100) return -1;
    return p;
}

bool isCharging() {
    uint8_t s;
    if (!readReg(kStatus2, s)) return false;
    return ((s >> 5) & 0x07) == 0x01;
}

bool isFull() {
    uint8_t s;
    if (!present()) return false;
    if (!readReg(kStatus2, s)) return false;
    return ((s >> 5) & 0x07) == 0x00;
}

} // namespace Battery
