// Stack Wallet - e-ink front panel firmware for the Waveshare ESP32-S3
// ePaper 3.97" board (800x480). See ../../README.md for the project
// overview and ../../docs/ for hardware, OTA, and content details.
#include "config.h"
#include "display.h"
#include "ota.h"
#include "storage.h"
#include "ui.h"
#include "version.h"
#include "wifi_provision.h"

#include <WiFi.h>

namespace {

void connectWiFi(unsigned long timeoutMs) {
    String ssid, password;
    if (!WifiProvision::load(ssid, password)) {
        // No saved credentials in NVS yet. Fall back to /sdcard/wifi.json
        // if present, and migrate it into NVS so future boots don't need
        // the SD card for this at all.
        if (Storage::loadWifiCredentials(ssid, password)) {
            WifiProvision::save(ssid, password);
        } else {
            Serial.println("No saved Wi-Fi credentials (Settings > Wi-Fi Setup, or "
                            "/sdcard/wifi.json) - staying offline (cards/tickets/todo/reader "
                            "still work; only OTA needs Wi-Fi)");
            return;
        }
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Wi-Fi connected, IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("Wi-Fi not connected - continuing offline (cards/tickets/todo/reader still work)");
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("Stack Wallet %s booting...\n", STACK_WALLET_VERSION);

    Display::begin();

    // A full-quality refresh once at boot avoids carrying over ghosting
    // from whatever was last on the panel (e.g. the factory demo image),
    // and exercises the panel's full-refresh waveform independently of
    // the fast/partial path the UI uses afterward.
    Serial.println("Full-panel clear (screen should flash white)...");
    Display::fullClear();
    Serial.println("Full-panel clear done");

    if (!Storage::begin()) {
        Serial.println("WARNING: microSD card not found or failed to mount");
    }

    connectWiFi(8000);
    OTA::begin();

    UI::begin();
}

void loop() {
    UI::loop();
    OTA::loop();
}
