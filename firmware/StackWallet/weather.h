#pragma once

// Reads the onboard SHTC3 temperature + humidity sensor over I2C.
// Returns cached values; call read() periodically from the main loop.
namespace Weather {

// Initialises the I2C bus (SDA=41, SCL=42) and probes the SHTC3 at 0x70.
// Returns false if the chip does not respond.
bool begin();

// Polls the sensor for a new sample. Call every few seconds.
bool read(float &temperature, float &humidity);

// Last cached readings (0.0 if never read or sensor failed).
float temperature();
float humidity();

// True if the SHTC3 responded during begin().
bool isAvailable();

} // namespace Weather
