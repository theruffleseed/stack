#pragma once

// Wi-Fi credentials live in /sdcard/wifi.json, not compiled into the
// firmware - see docs/CONTENT.md and Storage::loadWifiCredentials(). This
// keeps them out of the OTA release binary (which is a public GitHub
// Release asset) and means they survive OTA updates, which only rewrite
// the internal flash app partition and never touch the SD card.
#define WIFI_CONFIG_PATH "/wifi.json"

// ---------------------------------------------------------------------------
// microSD (SDMMC 4-bit bus). Verified against Waveshare's own 05_SD_Test
// Arduino example for this exact board.
// ---------------------------------------------------------------------------
#define SD_CLK_PIN 16
#define SD_CMD_PIN 17
#define SD_D0_PIN 15
#define SD_D1_PIN 7
#define SD_D2_PIN 8
#define SD_D3_PIN 18
#define SD_MOUNT_POINT "/sdcard"

// ---------------------------------------------------------------------------
// Front-panel buttons. Verified against Waveshare's ESP-IDF button_bsp
// component for this board. All are active-LOW with INPUT_PULLUP.
// ---------------------------------------------------------------------------
#define BTN_UP_PIN 4
#define BTN_SELECT_PIN 5
#define BTN_DOWN_PIN 6
#define BTN_BACK_PIN 0 // shared with the BOOT strapping button

// ---------------------------------------------------------------------------
// OTA: the device polls the GitHub Releases API for this repo and, if the
// latest release tag differs from the version this firmware was built with,
// downloads and flashes releases/latest/download's "firmware.bin" asset.
// See docs/OTA.md for how the release is produced and the security tradeoffs
// of the setInsecure() TLS mode used below.
// ---------------------------------------------------------------------------
#define GITHUB_OWNER "theruffleseed"
#define GITHUB_REPO "stack"

#ifndef ENABLE_OTA_AUTOCHECK
#define ENABLE_OTA_AUTOCHECK 1
#endif
// How often the device checks for a new release on its own. Manual checks
// (Settings screen) are always available regardless of this interval.
#define OTA_CHECK_INTERVAL_MS (12UL * 60UL * 60UL * 1000UL) // 12 hours

// ---------------------------------------------------------------------------
// Content layout on the microSD card - see docs/CONTENT.md.
// ---------------------------------------------------------------------------
#define MANIFEST_PATH "/manifest.json"
#define TODO_PATH "/todo.txt"
