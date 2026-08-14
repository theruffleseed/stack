#pragma once

namespace Battery {

// Must be called once at boot before any other function here.
void begin();

// True when a lithium cell is detected on the AXP2101.
bool present();

// Battery percentage 0..100 from the AXP2101's built-in gauge,
// or -1 when unavailable (no battery / I2C failure).
int percent();

bool isCharging();
bool isFull();

} // namespace Battery
