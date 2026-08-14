# Stack Wallet

A slim, Ridge-Wallet-style front panel built around a
[Waveshare ESP32-S3 ePaper 3.97"](https://www.waveshare.com/esp32-s3-epaper-3.97.htm)
(800x480 e-ink, 16MB flash, OPI PSRAM). It shows, at a glance and with
button navigation:

- **Loyalty card barcodes** - scan straight off the panel
- **Flight ticket barcodes/QRs**
- **A to-do list** - checkable on-device today; phone sync is planned (see
  Roadmap)
- **A static DuitNow QR** for receiving payments
- **A plain-text e-book/PDF reader**, paginated on-device
- **A live battery indicator** (AXP2101) - level bars plus a charging bolt,
  shown on the home screen and every sub-screen header

Content (cards, tickets, QR, books, to-do) lives on the microSD card and is
pushed there from a phone over the built-in Wi-Fi Sync portal, or by copying
files onto the card directly (see [docs/CONTENT.md](docs/CONTENT.md)).

Firmware is a standard Arduino IDE sketch. Pushing to GitHub is enough to
get new firmware onto the device over the air - no cable, no self-hosted
runner. See [docs/OTA.md](docs/OTA.md) for exactly how.

## Repository layout

```
firmware/StackWallet/   Arduino sketch (+ vendored Waveshare EPD driver)
tools/                    Python scripts: BMP/QR/barcode generation, PDF->text, manifest builder
sdcard-template/           Starting point for the microSD card's content
.github/workflows/         build.yml (compile check) + release.yml (OTA publish)
docs/                       HARDWARE.md, OTA.md, CONTENT.md
```

## Quickstart

### 1. Arduino IDE setup

1. **File > Preferences > Additional boards manager URLs**, add:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. **Tools > Board > Boards Manager**, install "esp32" (Espressif Systems).
3. Open `firmware/StackWallet/StackWallet.ino`.
4. **Tools**: Board = "ESP32S3 Dev Module", PSRAM = "OPI PSRAM", Flash Size
   = "16MB", **USB CDC On Boot = "Enabled"** (not the default - without
   this you'll flash fine but get no Serial output over USB, since this
   board uses the ESP32-S3's native USB peripheral rather than a separate
   UART bridge chip). Partition Scheme can be left on its default - the
   sketch ships its own `partitions.csv`, which Arduino always prefers over
   the Tools menu selection whenever one is present next to the `.ino` file
   (see [docs/OTA.md](docs/OTA.md) for why a custom table is needed at
   all).
5. **Sketch > Include Library > Manage Libraries**, install **ArduinoJson**
   (v7).
6. Plug the board in over USB, select its port, and click Upload once to
   get the first build onto the device.

Full pinout and rationale: [docs/HARDWARE.md](docs/HARDWARE.md).

### 2. Wi-Fi (only needed for OTA - everything else works offline)

No SD card or cable needed for this part: on the device, go to
**Settings > Wi-Fi Setup**, connect to the `StackWallet-Setup` hotspot it
starts from your phone, and enter your network details on the page that
opens. See [docs/CONTENT.md](docs/CONTENT.md#wi-fi-credentials) for what
that flow looks like and its tradeoffs, and for the alternative SD-card
`wifi.json` method if you'd rather set it that way.

### 3. Prepare the microSD card

Copy [`sdcard-template/`](sdcard-template) onto the card, then fill it in:

```sh
pip install -r tools/requirements.txt

# A loyalty card
python tools/make_bmp.py barcode 012345678905 sdcard-template/cards/starbucks.bmp \
  --type ean13 --show-text --label Starbucks

# Regenerate manifest.json from whatever's in cards/tickets/qr/books
python tools/build_manifest.py sdcard-template
```

Then copy the whole `sdcard-template/` folder onto the card. Full details,
including flight tickets, the DuitNow QR, and e-books:
[docs/CONTENT.md](docs/CONTENT.md).

### 4. Push firmware changes from GitHub

Once the first build is on the device and Wi-Fi is configured, further
firmware changes just need `git push` to `main`:

1. `.github/workflows/release.yml` compiles the sketch and publishes a
   GitHub Release with `firmware.bin` attached.
2. The device polls the Releases API (every 12h by default, or on demand
   from the Settings screen) and flashes itself when it finds a newer
   release.

Full explanation of the pull-based OTA design and its security tradeoffs:
[docs/OTA.md](docs/OTA.md).

## Controls

| Button | Action |
|--------|--------|
| Up / Down | Move selection, or flip pages/cards |
| Select | Open / confirm / toggle |
| Boot | Back / home |

## Roadmap

Deliberately out of scope for v1, called out here rather than half-built:

- Editing the to-do list from a phone over Bluetooth or Wi-Fi (v1 edits
  `todo.txt` on the card directly)
- On-device PDF rendering (v1 converts PDFs to plain text on a computer -
  see [docs/CONTENT.md](docs/CONTENT.md))
- Clock (PCF85063) readout - the I2C chip is on the board but unused so far
- Deep sleep / power tuning for longer battery life between button presses
- Firmware signature verification for OTA (currently TLS-only, see
  [docs/OTA.md](docs/OTA.md#security-tradeoffs-of-the-current-setup))

### Next phase (agreed 2026-08-14)

A richer phone-side upload experience for content, replacing the current
plain web page served by the Wi-Fi Sync portal:

- **Drag-drop / file-selection UI on the phone** for loyalty cards (.bmp),
  e-books (.txt), and to-do lists
- **todo.txt support in the portal** (its `/upload` handler currently
  accepts only `books` and `cards` directories - `sync_portal.cpp`)
- **Calendar** as a new content type: a new file format + manifest handling,
  a portal upload path, and an on-device month/day view
- Manifest/rescan handling so newly uploaded content appears without a
  manual rescan
