#pragma once
#include <Arduino.h>
#include <vector>

// A single named item that points at a file on the SD card (an image for
// cards/tickets, a text file for books). Paths are absolute, e.g.
// "/sdcard/cards/starbucks.bmp".
struct ContentItem {
    String name;
    String path;
};

struct TodoItem {
    String text;
    bool done;
};

// Reads /sdcard/manifest.json and /sdcard/todo.txt - see docs/CONTENT.md for
// the file formats. All of this is read-only content except the todo list,
// which the device can check off and persist back to the card.
namespace Storage {

// Mounts the microSD card over SDMMC (4-bit bus). Returns false if no card
// is present or it fails to mount; content screens then show "no SD card"
// instead of failing the whole boot.
bool begin();

// Parses manifest.json into the in-memory lists below. Safe to call again
// later (e.g. from the Settings screen) to pick up SD card edits.
bool loadManifest();

const std::vector<ContentItem> &cards();
const std::vector<ContentItem> &tickets();
const std::vector<ContentItem> &books();
// Empty string if no DuitNow QR is configured in the manifest.
const String &duitnowQrPath();
// Optional display name of the receive account ("duitnow_label" in the
// manifest). Empty string if not configured.
const String &duitnowLabel();
// Empty string if no business card image is configured in the manifest.
const String &businessCardPath();

std::vector<TodoItem> loadTodo();
bool saveTodo(const std::vector<TodoItem> &items);

// Reads /sdcard/wifi.json ({"ssid": "...", "password": "..."}). Returns
// false (leaving ssid/password untouched) if the card isn't mounted, the
// file is missing, or it has no non-empty "ssid" field.
bool loadWifiCredentials(String &ssid, String &password);

bool isMounted();

// Appends a new entry to the "cards" or "books" section of manifest.json
// (replacing an existing entry with the same name). `filename` is a path
// relative to the SD card root, e.g. "cards/gold.bmp". Used after a Wi-Fi
// Sync upload so the lists pick the new content up on the next rescan.
// Returns false if the card is not mounted or the manifest can't be edited.
bool addManifestItem(const char *section, const String &name, const String &filename);

} // namespace Storage
