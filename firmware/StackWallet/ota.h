#pragma once
#include <Arduino.h>

// Pull-based OTA: the device polls the GitHub Releases API for this repo
// and flashes the "firmware.bin" asset of the latest release when its tag
// differs from the version this build was compiled with. See docs/OTA.md.
namespace OTA {

// Call once after Wi-Fi has been (attempted to be) connected.
void begin();

// Call from loop(); auto-checks every OTA_CHECK_INTERVAL_MS when
// ENABLE_OTA_AUTOCHECK is set. Non-blocking except during an actual update.
void loop();

// Synchronously checks GitHub now and flashes+reboots on a newer release.
// Returns false (having updated lastCheckStatus()) if nothing was applied;
// on success the device reboots and this call never returns.
bool checkAndApplyNow();

String currentVersion();
String lastCheckStatus();

} // namespace OTA
