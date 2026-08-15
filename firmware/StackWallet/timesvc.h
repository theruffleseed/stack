// Wall-clock time for the calendar screen. There is no battery-backed RTC
// in use yet (the PCF85063 on the board is a follow-up), so:
//   - online:  SNTP via configTime() keeps time(nullptr) live;
//   - offline: an epoch cached in NVS on the last online run, advanced by
//     millis() since boot, gives an estimate that only drifts with the
//     crystal (seconds/day) but resets whenever the battery dies.
#pragma once

#include <time.h>

namespace TimeSvc {

// Load the NVS cache and start SNTP if Wi-Fi is connected. Call once after
// the boot-time Wi-Fi connect attempt.
void begin();

// Cheap periodic call (e.g. from loop()): while NTP-synced, re-save the
// epoch to NVS at most once an hour so offline boots start from a recent
// time. Also starts SNTP late if Wi-Fi connected after begin().
void keepAlive();

// SNTP has answered at least once (this boot): time(nullptr) is real.
bool synced();

// Best-effort local time. Returns false if the clock was never set by any
// means. `fromCache` is true when the value is the offline estimate rather
// than a live SNTP reading.
bool now(struct tm &out, bool &fromCache);

} // namespace TimeSvc
