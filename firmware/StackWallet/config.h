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

// ---------------------------------------------------------------------------
// Display orientation: portrait, chin (buttons/USB-C) at the bottom.
//
// DISPLAY_ROTATE must stay 90 (GUI_Paint.h's ROTATE_90): card/ticket/QR BMPs
// are generated at 480x800 (see tools/make_bmp.py), and GUI_ReadBmp() picks
// its own rotation from a BMP's dimensions - 480x800 auto-selects rotate-90.
// Setting DISPLAY_ROTATE to anything else would leave the menu/list screens
// in one rotation and loaded card/ticket images in another.
//
// If the chin lands on the wrong edge on your board, flip DISPLAY_MIRROR to
// 0x03 (GUI_Paint.h's MIRROR_ORIGIN, a 180-degree flip) instead - mirroring
// is applied after rotation and GUI_ReadBmp() never touches it, so it stays
// consistent across every screen including loaded BMPs.
// ---------------------------------------------------------------------------
#define DISPLAY_ROTATE 90   // GUI_Paint.h ROTATE_90
#define DISPLAY_MIRROR 0x00 // GUI_Paint.h MIRROR_NONE (0x03 = MIRROR_ORIGIN)

// ---------------------------------------------------------------------------
// Wi-Fi setup portal (Settings > Wi-Fi Setup): a temporary open AP + web
// page for entering credentials from a phone with no SD card or cable
// needed. See wifi_provision.h for where credentials end up stored.
// ---------------------------------------------------------------------------
#define WIFI_SETUP_AP_SSID "StackWallet-Setup"
#define WIFI_SETUP_TIMEOUT_MS (5UL * 60UL * 1000UL) // 5 minutes
