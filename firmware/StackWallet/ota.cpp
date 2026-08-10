#include "ota.h"
#include "config.h"
#include "version.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {

unsigned long lastCheckMs = 0;
String lastStatus = "not checked yet";

// GitHub's API and the objects.githubusercontent.com CDN that release
// assets redirect to use certificates this sketch does not pin, so TLS
// verification is disabled here. That means OTA traffic is encrypted but
// not authenticated against a known CA - acceptable for a personal gadget
// pulling its own repo's public releases, but see docs/OTA.md if you want
// to harden this (pin GitHub's root CA instead).
WiFiClientSecure makeInsecureClient() {
    WiFiClientSecure client;
    client.setInsecure();
    return client;
}

bool fetchLatestRelease(String &tag, String &assetUrl) {
    WiFiClientSecure client = makeInsecureClient();
    HTTPClient https;

    String url = String("https://api.github.com/repos/") + GITHUB_OWNER + "/" + GITHUB_REPO +
                 "/releases/latest";
    if (!https.begin(client, url)) {
        lastStatus = "failed to start HTTPS request";
        return false;
    }
    https.addHeader("User-Agent", "StackWallet-ESP32");
    https.addHeader("Accept", "application/vnd.github+json");

    int code = https.GET();
    if (code != HTTP_CODE_OK) {
        lastStatus = "GitHub API returned HTTP " + String(code);
        https.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, https.getStream());
    https.end();
    if (err) {
        lastStatus = String("release JSON parse error: ") + err.c_str();
        return false;
    }

    tag = doc["tag_name"].as<String>();
    assetUrl = "";
    for (JsonObjectConst asset : doc["assets"].as<JsonArrayConst>()) {
        if (asset["name"].as<String>() == "firmware.bin") {
            assetUrl = asset["browser_download_url"].as<String>();
            break;
        }
    }

    if (tag.length() == 0 || assetUrl.length() == 0) {
        lastStatus = "latest release has no firmware.bin asset";
        return false;
    }
    return true;
}

} // namespace

namespace OTA {

void begin() {
    // Trigger the first check promptly from loop() rather than at boot,
    // so it doesn't compete with initial screen rendering.
    lastCheckMs = millis() - OTA_CHECK_INTERVAL_MS + 15000;
}

void loop() {
#if ENABLE_OTA_AUTOCHECK
    if (millis() - lastCheckMs >= OTA_CHECK_INTERVAL_MS) {
        checkAndApplyNow();
    }
#endif
}

bool checkAndApplyNow() {
    lastCheckMs = millis();

    if (WiFi.status() != WL_CONNECTED) {
        lastStatus = "Wi-Fi not connected";
        return false;
    }

    String tag, assetUrl;
    if (!fetchLatestRelease(tag, assetUrl)) {
        return false;
    }

    if (tag == STACK_WALLET_VERSION) {
        lastStatus = "up to date (" + tag + ")";
        return false;
    }

    lastStatus = "downloading " + tag + "...";
    Serial.printf("OTA: updating %s -> %s\n", STACK_WALLET_VERSION, tag.c_str());

    WiFiClientSecure client = makeInsecureClient();
    t_httpUpdate_return ret = httpUpdate.update(client, assetUrl);

    switch (ret) {
        case HTTP_UPDATE_OK:
            lastStatus = "updated to " + tag + ", rebooting";
            Serial.println("OTA: update OK, rebooting");
            delay(500);
            ESP.restart();
            return true; // unreachable, ESP.restart() does not return

        case HTTP_UPDATE_FAILED:
            lastStatus = "update failed: " + httpUpdate.getLastErrorString();
            Serial.printf("OTA: update failed: %s\n", httpUpdate.getLastErrorString().c_str());
            return false;

        default: // HTTP_UPDATE_NO_UPDATES
            lastStatus = "no update applied";
            return false;
    }
}

String currentVersion() {
    return STACK_WALLET_VERSION;
}

String lastCheckStatus() {
    return lastStatus;
}

} // namespace OTA
