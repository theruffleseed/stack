#include "timesvc.h"
#include "config.h"

#include <Preferences.h>
#include <WiFi.h>

namespace {

Preferences prefs;
time_t cachedEpoch = 0;
uint32_t cachedAtMs = 0;
bool sntpStarted = false;
bool savedThisBoot = false;
unsigned long lastSaveMs = 0;

// time(nullptr) before the first SNTP answer is the 1970 epoch; anything
// past 2025-01-01 means SNTP has actually set the clock.
constexpr time_t kSaneEpoch = 1735689600;

constexpr unsigned long kSaveIntervalMs = 60UL * 60UL * 1000UL; // 1 hour

time_t estimateEpoch() {
    if (time(nullptr) > kSaneEpoch) return time(nullptr);
    if (cachedEpoch == 0) return 0;
    return cachedEpoch + (millis() - cachedAtMs) / 1000;
}

} // namespace

namespace TimeSvc {

void begin() {
    prefs.begin("pdtime", false);
    cachedEpoch = (time_t)prefs.getULong64("epoch", 0);
    cachedAtMs = millis(); // the save moment is treated as "now" for this boot

    if (WiFi.status() == WL_CONNECTED) {
        configTime(NTP_TZ_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
        sntpStarted = true;
    }
}

void keepAlive() {
    if (!sntpStarted && WiFi.status() == WL_CONNECTED) {
        configTime(NTP_TZ_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
        sntpStarted = true;
    }
    if (!synced()) return;
    // Save the first sync promptly, then re-save at most hourly (NVS writes
    // wear the flash, so they stay rare).
    if (savedThisBoot && millis() - lastSaveMs < kSaveIntervalMs) return;

    prefs.putULong64("epoch", (uint64_t)time(nullptr));
    savedThisBoot = true;
    lastSaveMs = millis();
}

bool synced() {
    return time(nullptr) > kSaneEpoch;
}

bool now(struct tm &out, bool &fromCache) {
    fromCache = !synced();
    time_t t = estimateEpoch();
    if (t == 0) return false;
    localtime_r(&t, &out);
    return true;
}

} // namespace TimeSvc
