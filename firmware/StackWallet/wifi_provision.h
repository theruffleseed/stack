#pragma once
#include <Arduino.h>

// Wi-Fi credential storage and on-device setup portal. Credentials are
// stored in the ESP32's internal NVS flash (via Preferences), not on the
// SD card and not compiled into the firmware - so they survive OTA updates
// (which only rewrite the app partition), are never present in the public
// release binary, and don't require SD card access to configure. See
// docs/CONTENT.md for the tradeoffs of the setup portal's open AP.
namespace WifiProvision {

// Loads previously saved credentials. Returns false if none are stored.
bool load(String &ssid, String &password);

// Saves credentials to NVS for future boots.
void save(const String &ssid, const String &password);

// Blocking: hosts a temporary Wi-Fi AP + captive setup page so credentials
// can be entered from a phone or laptop. Draws its own e-ink status screens
// and can be cancelled with the BOOT/back button. Returns true if
// credentials were entered, saved, and connected to; false on timeout or
// cancellation (nothing is changed in that case).
bool runSetupPortal();

} // namespace WifiProvision
