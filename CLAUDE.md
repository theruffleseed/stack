# Stack Wallet — project memory

E-ink "Stack Wallet" (Waveshare ESP32-S3 ePaper 3.97", 480x800 portrait,
SSD1677). Arduino sketch at `firmware/StackWallet`. See README.md for the
user-facing story, docs/HARDWARE.md + docs/OTA.md for hardware/OTA details.

## Build & flash

- arduino-cli compile:
  `arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc" --libraries ~/Arduino/libraries firmware/StackWallet`
  (cores live in `~/.arduino15`, core esp32 3.3.11; ArduinoJson + GxEPD2 in
  `~/Arduino/libraries`). Sketch uses ~94% of flash — keep an eye on size.
- Flash over USB (device is `/dev/ttyACM0`, ESP32-S3 native USB, no UART
  bridge):
  `esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 StackWallet.ino.bootloader.bin 0x8000 StackWallet.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 StackWallet.ino.bin`
  (offsets from build `flash_args`). App-only updates: `0x10000` only.
- Device console is on the same `/dev/ttyACM0` (CDCOnBoot=cdc).

## Version stamping (CRITICAL)

- `version.h` is the *local-build* value. GitHub `release.yml` overwrites it
  with `v0.2.<run-number>` when publishing; it then tags that commit. Local
  builds of a stale `version.h` flash an OLD version.
- OTA compares by **equality** (ota.cpp: `tag == STACK_WALLET_VERSION`):
  ANY mismatch triggers an auto-update (12h poll, `ENABLE_OTA_AUTOCHECK`).
  So after a release, bump `version.h` to the newest tag (check
  `git ls-remote --tags origin`) or the device will revert the local flash
  to the release build — silently undoing local changes.
- Version was bumped 0.2.18 -> 0.2.21 (2026-08-14) for exactly this reason.

## Display refresh architecture (display.cpp + ui.cpp)

- Full push: `endFrame(true)` = Init_Fast + full-panel fast refresh;
  every 5th fast refresh is forced to a clean full refresh (ghosting guard).
- Differential partial: `beginPartialFrame()` (RAM-only clear, no panel
  reset) then `partialFullFrame()` = `EPD_3IN97_DisplayPartial_Diff(prevFrame,
  frameBuffer)` — pushes only changed pixels, no flash, no Init (~400ms saved
  per screen change). Home screen moves use this.
- 2026-08-14 speed fix: cards/tickets/books/todo/settings now render with
  the differential partial on entry, on every move, on todo check toggles,
  and on back-navigation (`listPartialPending` / `settingsPartialPending` /
  `homePartialPending` flags in ui.cpp). Images (QR, card/ticket BMPs) keep
  the clean refresh — photos ghost badly on partials. The diff writer
  re-asserts its own register state, so it works after both fast and clean
  inits.

## Battery (battery.cpp, AXP2101 @ 0x34, SDA=41 SCL=42)

- `Battery::begin()` MUST enable the fuel gauge (0xA2 bit0) + pulse its
  reset (0x17 bit2), otherwise the percent register 0xA4 reads 0 — it only
  "worked" before because the factory firmware had left the gauge on in the
  PMU's registers. Never rely on PMU register persistence.
- Percent register: 0xA4 (0..100); battery-present bit: 0x00 bit3;
  charging: 0x01 bits7-5 == 1.
- Battery icon (ui.cpp `drawBatteryIcon`): outline + 4 segments in
  foreground color; charging bolt drawn in the SAME color (drawn in the
  background color it was invisible on the white home masthead). Header
  band (ui.cpp `drawHeader`) is filled BLACK (white title) — the battery
  icon there is drawn in WHITE. Sync portal message screens use the same
  black band.

## Wi-Fi Sync portal (sync_portal.cpp)

- AP mode (SSID from config.h), captive portal, streams uploads as
  seq POSTs to `/upload`. Currently restricts `dir` to `books` (.txt) and
  `cards` (.bmp) — todo.txt upload is NOT supported yet (next-phase item).

## Next phase (agreed 2026-08-14)

Better upload UI for content: loyalty cards, e-books, to-do lists, and a
**calendar** (new content type). Currently upload = Wi-Fi Sync portal with a
plain web page; the roadmap target is a richer flow (drag-drop/selection UI
on the phone side, todo.txt + calendar support in the portal's `dir`
handling, possibly rescan/manifest handling for new types).

## Misc

- SD card is NOT exposed over USB (no MSC) — copy files to the microSD
  directly or via the Wi-Fi Sync portal. todo.txt format: `[ ]`/`[x]` +
  text per line (`parseTodoLine` in storage.cpp).
- Idle sleep: panel sleeps after `IDLE_SLEEP_MS`; first button press wakes
  and re-renders (full push, not partial).
- Build flashes at 94% of 1.3MB app partition — watch size when adding the
  calendar/upload features.
