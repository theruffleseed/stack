#include "storage.h"
#include "config.h"

#include <ArduinoJson.h>
#include <SD_MMC.h> // only for SD_MMC.begin()/setPins(); all file IO is POSIX

#include <dirent.h>
#include <sys/stat.h>

namespace {

bool mounted = false;
std::vector<ContentItem> cardsList;
std::vector<ContentItem> ticketsList;
std::vector<ContentItem> booksList;
String duitnowPath;
String duitnowLabelText;
String businessPath;

// Diagnostic: print every entry in the card root with its exact byte length
// and type. Windows can hide trailing spaces/unicode look-alikes in file
// names; this shows exactly what the device's VFS sees.
void dumpCardRoot();

String joinPath(const String &relative) {
    if (relative.length() == 0) return "";
    if (relative.startsWith("/")) return String(SD_MOUNT_POINT) + relative;
    return String(SD_MOUNT_POINT) + "/" + relative;
}

// All card files are accessed with raw POSIX calls (fopen/fgetc/fwrite):
// the Arduino SD_MMC wrapper in this core silently prepends its mountpoint
// to every path, so "/sdcard/manifest.json" becomes "/sdcard/sdcard/..." and
// always fails. GUI_ReadBmp() uses fopen() too - POSIX is the only path
// convention that works across this codebase.
String mountPath(const char *cardRootRelative) {
    return joinPath(cardRootRelative);
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

TodoItem parseTodoLine(String line) {
    line.trim();
    bool done = false;
    if (line.startsWith("[x]") || line.startsWith("[X]")) {
        done = true;
        line = line.substring(3);
    } else if (line.startsWith("[ ]")) {
        done = false;
        line = line.substring(3);
    }
    line.trim();
    if (line.length() == 0) line = "?";
    return {line, done};
}

// Reads a whole small text file (<= 16KB) into a String. ArduinoJson in
// this version can't deserialize from a FILE*, so small config files are
// read into RAM first.
String readAll(FILE *f) {
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 16384) return String();
    char *buf = (char *)malloc(size + 1);
    if (!buf) return String();
    size_t got = fread(buf, 1, size, f);
    buf[got] = '\0';
    String s = String(buf);
    free(buf);
    return s;
}

// Diagnostic: print every entry in the card root with its exact byte length
// and type. Windows can hide trailing spaces/unicode look-alikes in file
// names; this shows exactly what the device's VFS sees. Note the trailing
// slash: ESP-IDF's VFS refuses to open the mount point itself ("/sdcard"),
// but "/sdcard/" lists the directory.
void dumpCardRoot() {
    String rootPath = String(SD_MOUNT_POINT) + "/";
    DIR *d = opendir(rootPath.c_str());
    if (!d) {
        Serial.println("Storage: could not open card root directory");
        return;
    }
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) != nullptr) {
        char full[160];
        snprintf(full, sizeof(full), "%s/%s", SD_MOUNT_POINT, e->d_name);
        struct stat st;
        bool isDir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
        Serial.printf("Storage: root[%d] len=%u '%s' %s\n", n, (unsigned)strlen(e->d_name),
                      e->d_name, isDir ? "DIR" : "FILE");
        n++;
    }
    closedir(d);
    if (n == 0) Serial.println("Storage: card root is EMPTY");
}

} // namespace

namespace Storage {

bool begin() {
    // Match Waveshare's verified 05_SD_Test example for this exact board:
    // Arduino's SD_MMC wrapper over the IDF SDMMC driver, 1-bit mode. This
    // is the configuration proven to work on this hardware; the lower-level
    // esp_vfs_fat_sdmmc_mount() call used briefly during debugging mounted
    // the card but its VFS refused every file access, so we're back to the
    // reference path.
    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN);
    mounted = SD_MMC.begin(SD_MOUNT_POINT, /*mode1bit=*/true);
    if (!mounted) {
        Serial.println("Storage::begin: SD_MMC.begin() failed - card not detected or not FAT16/FAT32");
        Serial.println("Storage::begin: re-seat the card, or format it FAT32 on a PC, then retry");
        return false;
    }
    Serial.printf("Storage::begin: mounted at %s\n", SD_MOUNT_POINT);
    dumpCardRoot();
    return true;
}

bool isMounted() {
    return mounted;
}

bool loadManifest() {
    cardsList.clear();
    ticketsList.clear();
    booksList.clear();
    duitnowPath = "";
    duitnowLabelText = "";
    businessPath = "";

    if (!mounted) return false;

    FILE *f = fopen(mountPath(MANIFEST_PATH).c_str(), "r");
    if (!f) {
        Serial.printf("Storage::loadManifest: could not open %s (errno %d)\n",
                      mountPath(MANIFEST_PATH).c_str(), errno);
        return false;
    }
    String content = readAll(f);
    fclose(f);
    if (content.length() == 0) {
        Serial.println("Storage::loadManifest: manifest.json empty or too large");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, content.c_str());
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
    duitnowLabelText = doc["duitnow_label"].as<String>();
    if (doc["business_card"].is<const char *>()) {
        businessPath = joinPath(doc["business_card"].as<String>());
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

const String &duitnowLabel() {
    return duitnowLabelText;
}

const String &businessCardPath() {
    return businessPath;
}

std::vector<TodoItem> loadTodo() {
    std::vector<TodoItem> items;
    if (!mounted) return items;

    FILE *f = fopen(mountPath(TODO_PATH).c_str(), "r");
    if (!f) return items;

    String line;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') {
            if (line.length() > 0) {
                items.push_back(parseTodoLine(line));
                line = "";
            }
        } else if (c != '\r') {
            line += (char)c;
        }
    }
    if (line.length() > 0) items.push_back(parseTodoLine(line));
    fclose(f);
    return items;
}

bool loadWifiCredentials(String &ssid, String &password) {
    if (!mounted) return false;

    FILE *f = fopen(mountPath(WIFI_CONFIG_PATH).c_str(), "r");
    if (!f) {
        Serial.printf("Storage::loadWifiCredentials: could not open %s (copy "
                       "wifi.json.example from sdcard-template/ and fill it in)\n",
                       mountPath(WIFI_CONFIG_PATH).c_str());
        return false;
    }
    String content = readAll(f);
    fclose(f);
    if (content.length() == 0) {
        Serial.println("Storage::loadWifiCredentials: wifi.json empty or too large");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, content.c_str());
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

    FILE *f = fopen(mountPath(TODO_PATH).c_str(), "w");
    if (!f) return false;

    for (const auto &item : items) {
        fprintf(f, "%s%s\n", item.done ? "[x] " : "[ ] ", item.text.c_str());
    }
    fclose(f);
    return true;
}

} // namespace Storage
