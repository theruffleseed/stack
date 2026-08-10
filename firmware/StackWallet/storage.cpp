#include "storage.h"
#include "config.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <SD_MMC.h>

namespace {

bool mounted = false;
std::vector<ContentItem> cardsList;
std::vector<ContentItem> ticketsList;
std::vector<ContentItem> booksList;
String duitnowPath;

String joinPath(const String &relative) {
    if (relative.length() == 0) return "";
    if (relative.startsWith("/")) return String(SD_MOUNT_POINT) + relative;
    return String(SD_MOUNT_POINT) + "/" + relative;
}

void readItemArray(JsonArrayConst arr, const char *fileKey, std::vector<ContentItem> &out) {
    out.clear();
    for (JsonObjectConst obj : arr) {
        ContentItem item;
        item.name = obj["name"].as<String>();
        item.path = joinPath(obj[fileKey].as<String>());
        if (item.name.length() && item.path.length()) {
            out.push_back(item);
        }
    }
}

} // namespace

namespace Storage {

bool begin() {
    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN);
    // mode1bit=true matches Waveshare's own verified 05_SD_Test example for
    // this board, even though all 4 data pins are wired.
    mounted = SD_MMC.begin(SD_MOUNT_POINT, /*mode1bit=*/true);
    if (!mounted) {
        Serial.println("Storage::begin: SD_MMC.begin() failed");
    }
    return mounted;
}

bool isMounted() {
    return mounted;
}

bool loadManifest() {
    cardsList.clear();
    ticketsList.clear();
    booksList.clear();
    duitnowPath = "";

    if (!mounted) return false;

    File f = SD_MMC.open(MANIFEST_PATH, FILE_READ);
    if (!f) {
        Serial.printf("Storage::loadManifest: could not open %s\n", MANIFEST_PATH);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("Storage::loadManifest: JSON parse error: %s\n", err.c_str());
        return false;
    }

    if (doc["cards"].is<JsonArrayConst>()) {
        readItemArray(doc["cards"].as<JsonArrayConst>(), "image", cardsList);
    }
    if (doc["tickets"].is<JsonArrayConst>()) {
        readItemArray(doc["tickets"].as<JsonArrayConst>(), "image", ticketsList);
    }
    if (doc["books"].is<JsonArrayConst>()) {
        readItemArray(doc["books"].as<JsonArrayConst>(), "file", booksList);
    }
    if (doc["duitnow_qr"].is<const char *>()) {
        duitnowPath = joinPath(doc["duitnow_qr"].as<String>());
    }

    return true;
}

const std::vector<ContentItem> &cards() {
    return cardsList;
}

const std::vector<ContentItem> &tickets() {
    return ticketsList;
}

const std::vector<ContentItem> &books() {
    return booksList;
}

const String &duitnowQrPath() {
    return duitnowPath;
}

std::vector<TodoItem> loadTodo() {
    std::vector<TodoItem> items;
    if (!mounted) return items;

    File f = SD_MMC.open(TODO_PATH, FILE_READ);
    if (!f) return items;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        bool done = false;
        if (line.startsWith("[x]") || line.startsWith("[X]")) {
            done = true;
            line = line.substring(3);
        } else if (line.startsWith("[ ]")) {
            done = false;
            line = line.substring(3);
        }
        line.trim();
        if (line.length() == 0) continue;

        items.push_back({line, done});
    }
    f.close();
    return items;
}

bool loadWifiCredentials(String &ssid, String &password) {
    if (!mounted) return false;

    File f = SD_MMC.open(WIFI_CONFIG_PATH, FILE_READ);
    if (!f) {
        Serial.printf("Storage::loadWifiCredentials: could not open %s (copy "
                       "wifi.json.example from sdcard-template/ and fill it in)\n",
                       WIFI_CONFIG_PATH);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("Storage::loadWifiCredentials: JSON parse error: %s\n", err.c_str());
        return false;
    }

    String s = doc["ssid"].as<String>();
    if (s.length() == 0) {
        Serial.println("Storage::loadWifiCredentials: wifi.json has no ssid");
        return false;
    }

    ssid = s;
    password = doc["password"].as<String>();
    return true;
}

bool saveTodo(const std::vector<TodoItem> &items) {
    if (!mounted) return false;

    File f = SD_MMC.open(TODO_PATH, FILE_WRITE);
    if (!f) return false;

    for (const auto &item : items) {
        f.print(item.done ? "[x] " : "[ ] ");
        f.println(item.text);
    }
    f.close();
    return true;
}

} // namespace Storage
