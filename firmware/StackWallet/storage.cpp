#include "storage.h"
#include "config.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <SD_MMC.h>

#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace {

bool mounted = false;
std::vector<ContentItem> cardsList;
std::vector<ContentItem> ticketsList;
std::vector<ContentItem> booksList;
String duitnowPath;
String duitnowLabelText;

// Diagnostic: print every entry in the card root with its exact byte length
// and type. Windows can hide trailing spaces/unicode look-alikes in file
// names; this shows exactly what the device's VFS sees.
void dumpCardRoot();
void dumpMbr(sdmmc_card_t *card);

String joinPath(const String &relative) {
    if (relative.length() == 0) return "";
    if (relative.startsWith("/")) return String(SD_MOUNT_POINT) + relative;
    return String(SD_MOUNT_POINT) + "/" + relative;
}

// The config.h *_PATH constants are relative to the card root (e.g.
// "/manifest.json"), but the card's VFS is registered under
// SD_MOUNT_POINT ("/sdcard") - POSIX lookups of a bare "/manifest.json"
// hit the root VFS and fail. All card access must go through the mount
// point, exactly like joinPath() does for manifest content entries.
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

// Diagnostic: print every entry in the card root with its exact byte length
// and type. Windows can hide trailing spaces/unicode look-alikes in file
// names; this shows exactly what the device's VFS sees. Note the trailing
// slash: ESP-IDF's VFS refuses to open the mount point itself ("/sdcard"),
// but "/sdcard/" lists the directory.
void dumpCardRoot() {
    File root = SD_MMC.open(String(SD_MOUNT_POINT) + "/");
    if (!root || !root.isDirectory()) {
        Serial.println("Storage: could not open card root directory");
        return;
    }
    File entry = root.openNextFile();
    int n = 0;
    while (entry) {
        const char *name = entry.name();
        const char *slash = strrchr(name, '/');
        const char *base = slash ? slash + 1 : name;
        Serial.printf("Storage: root[%d] len=%u '%s' %s %u bytes\n", n, (unsigned)strlen(base),
                      base, entry.isDirectory() ? "DIR" : "FILE", (unsigned)entry.size());
        entry = root.openNextFile();
        n++;
    }
    if (n == 0) Serial.println("Storage: card root is EMPTY");
}

// Prints the card's MBR partition table (if any). A card with leftover
// partition tables from a camera/phone can expose a different FAT volume to
// the ESP32 than the one Windows shows - this makes that visible.
void dumpMbr(sdmmc_card_t *card) {
    uint8_t mbr[512];
    if (sdmmc_read_sectors(card, mbr, 0, 1) != ESP_OK) {
        Serial.println("Storage: could not read MBR sector");
        return;
    }
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        Serial.println("Storage: no MBR signature (superfloppy layout)");
        return;
    }
    for (int i = 0; i < 4; i++) {
        const uint8_t *pe = mbr + 446 + i * 16;
        if (pe[4] == 0x00 && pe[8] == 0 && pe[9] == 0 && pe[10] == 0 && pe[11] == 0) continue;
        uint32_t lba = pe[8] | (pe[9] << 8) | (pe[10] << 16) | ((uint32_t)pe[11] << 24);
        uint32_t n = pe[12] | (pe[13] << 8) | (pe[14] << 16) | ((uint32_t)pe[15] << 24);
        Serial.printf("Storage: MBR[%d] type=0x%02X lba=%u sectors=%u\n", i, pe[4], lba, n);
    }
}

} // namespace

namespace Storage {

bool begin() {
    // Mount via the ESP-IDF SDMMC layer directly (same driver Arduino's
    // SD_MMC wrapper uses) so we can report *why* a card isn't mounting
    // instead of a bare "failed" - the reason in Serial tells us whether
    // it's a missing card, a bad contact, or an unsupported filesystem.
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1; // 1-bit mode: matches Waveshare's verified 05_SD_Test config
    slot.clk = (gpio_num_t)SD_CLK_PIN;
    slot.cmd = (gpio_num_t)SD_CMD_PIN;
    slot.d0 = (gpio_num_t)SD_D0_PIN;
    slot.d1 = (gpio_num_t)SD_D1_PIN;
    slot.d2 = (gpio_num_t)SD_D2_PIN;
    slot.d3 = (gpio_num_t)SD_D3_PIN;

    esp_vfs_fat_sdmmc_mount_config_t mountConfig = {};
    mountConfig.format_if_mount_failed = false;
    mountConfig.max_files = 8;
    mountConfig.allocation_unit_size = 16 * 1024;

    sdmmc_card_t *card = nullptr;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mountConfig, &card);
    if (ret != ESP_OK) {
        mounted = false;
        Serial.printf("Storage::begin: SD mount failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    mounted = true;
    sdmmc_card_print_info(stdout, card);
    Serial.printf("Storage::begin: mounted at %s\n", SD_MOUNT_POINT);
    dumpMbr(card);
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

    if (!mounted) return false;

    errno = 0;
    int fd = ::open(mountPath(MANIFEST_PATH).c_str(), O_RDONLY);
    if (fd < 0) {
        Serial.printf("Storage::loadManifest: open %s errno=%d (%s)\n",
                      mountPath(MANIFEST_PATH).c_str(), errno, strerror(errno));
        return false;
    }
    ::close(fd);

    File f = SD_MMC.open(mountPath(MANIFEST_PATH), FILE_READ);
    if (!f) {
        Serial.printf("Storage::loadManifest: SD_MMC open failed for %s\n",
                      mountPath(MANIFEST_PATH).c_str());
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
    duitnowLabelText = doc["duitnow_label"].as<String>();

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

std::vector<TodoItem> loadTodo() {
    std::vector<TodoItem> items;
    if (!mounted) return items;

    File f = SD_MMC.open(mountPath(TODO_PATH), FILE_READ);
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

    File f = SD_MMC.open(mountPath(WIFI_CONFIG_PATH), FILE_READ);
    if (!f) {
        Serial.printf("Storage::loadWifiCredentials: could not open %s (copy "
                       "wifi.json.example from sdcard-template/ and fill it in)\n",
                       mountPath(WIFI_CONFIG_PATH).c_str());
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

    File f = SD_MMC.open(mountPath(TODO_PATH), FILE_WRITE);
    if (!f) return false;

    for (const auto &item : items) {
        f.print(item.done ? "[x] " : "[ ] ");
        f.println(item.text);
    }
    f.close();
    return true;
}

} // namespace Storage
