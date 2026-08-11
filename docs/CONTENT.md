# Putting content on the device

All of Stack Wallet's content - loyalty cards, flight tickets, the DuitNow
QR, e-books, the to-do list, and Wi-Fi credentials - lives on the microSD
card, not in the firmware. Edit the card's contents, no reflash needed.

## Layout

Copy [`sdcard-template/`](../sdcard-template) onto the card as your
starting point:

```
/ (card root)
├── manifest.json      <- lists what's in cards/, tickets/, books/, and qr/
├── todo.txt            <- the to-do list
├── wifi.json            <- copy from wifi.json.example and fill in (see OTA.md)
├── cards/*.bmp          <- loyalty card barcodes/QRs, 480x800 1-bit BMP
├── tickets/*.bmp        <- flight ticket barcodes/QRs, 480x800 1-bit BMP
├── qr/duitnow.bmp        <- static "receive funds" QR, 480x800 1-bit BMP
└── books/*.txt           <- plain-text e-books, paginated on-device
```

Images **must** be 480x800 (portrait), 1-bit (monochrome) BMP - that's what
the panel's `GUI_ReadBmp()` loader expects in the firmware's default
orientation (see `DISPLAY_ROTATE` in `firmware/StackWallet/config.h`). Use
`tools/make_bmp.py` (below) rather than exporting BMPs by hand; it produces
exactly the right format and dimensions.

## manifest.json

```json
{
  "cards": [
    {"name": "Starbucks", "image": "cards/starbucks.bmp"}
  ],
  "tickets": [
    {"name": "MH370 KUL-LHR", "image": "tickets/mh370.bmp"}
  ],
  "books": [
    {"name": "Atomic Habits", "file": "books/atomic_habits.txt"}
  ],
  "duitnow_qr": "qr/duitnow.bmp",
  "duitnow_label": "Ken"
}
```

Paths are relative to the card root. Regenerate this file automatically
with `tools/build_manifest.py` instead of hand-editing it, once file names
are sorted out:

```sh
python tools/build_manifest.py /path/to/your/sdcard-folder
```

Item names come from file names (`starbucks_rewards.bmp` ->
"Starbucks Rewards") - rename files for nicer on-screen labels, or
hand-edit `manifest.json` afterward. `duitnow_label` (optional) is the
account name shown under the DuitNow QR on the receive screen.

## Generating card / ticket / QR images

`tools/make_bmp.py` turns a barcode value, QR payload, or existing image
into a properly-formatted 480x800 1-bit BMP:

```sh
pip install -r tools/requirements.txt

# A loyalty card barcode
python tools/make_bmp.py barcode 012345678905 sdcard-template/cards/starbucks.bmp \
  --type ean13 --show-text --label "Starbucks"

# A flight ticket - if you have a boarding-pass barcode value or a photo of
# the pass, either works
python tools/make_bmp.py barcode "M1DOE/JANE  EBOOKINGREF MH370 ..." \
  sdcard-template/tickets/mh370.bmp --type code128 --label "MH370 KUL-LHR"
python tools/make_bmp.py image boarding-pass-screenshot.png sdcard-template/tickets/mh370.bmp

# A static "receive funds" QR (e.g. a DuitNow payload string)
python tools/make_bmp.py qr "00020101021226..." sdcard-template/qr/duitnow.bmp \
  --label "Scan to pay"

# Convert a photo/screenshot of an existing QR (the receive screen draws a
# title and a scan hint over the --top/--bottom white bands, so reserve them;
# --crop trims the source image first if it has extra border/logo area)
python tools/make_bmp.py image KenQR.jpeg sdcard-template/qr/duitnow.bmp \
  --margin 40 --top 90 --bottom 85
```

`--type` accepts any [python-barcode](https://python-barcode.readthedocs.io/)
symbology (`code128`, `ean13`, `ean8`, `code39`, ...).

## E-books

The ESP32-S3 can't reasonably lay out real PDF pages on an e-ink panel, so
books go through a one-time conversion to plain text; the firmware
paginates that text itself at display time (word-wrapped to the panel's
character grid, one page per Up/Down press).

```sh
python tools/pdf_to_txt.py my-book.pdf sdcard-template/books/my-book.txt
```

Or just write/paste a `.txt` file directly into `books/`.

## To-do list

`todo.txt`, one item per line:

```
[ ] Buy milk
[x] Set up Stack Wallet
```

The device toggles `[ ]`/`[x]` with the select button and saves back to the
card immediately. In v1, adding/removing/reordering items means editing
this file on a computer; syncing the list from a phone over Bluetooth/Wi-Fi
is a planned follow-up (see the README roadmap).

## Wi-Fi credentials

Only needed for OTA checks (see [OTA.md](OTA.md)) - cards, tickets, the
to-do list, the QR code, and the e-book reader all work with no Wi-Fi at
all.

**On-device (no SD card needed):** on the device, go to **Settings > Wi-Fi
Setup**. It hosts a temporary open Wi-Fi hotspot named `StackWallet-Setup`;
connect to it from your phone or laptop, and a setup page should open
automatically (or browse to `http://192.168.4.1` if it doesn't). Enter your
network name and password there. Credentials are saved to the ESP32's
internal flash (NVS) - they survive OTA updates the same way SD-card
credentials would, they're never included in the compiled firmware, and no
SD card is involved at all. Press BOOT to cancel out of setup mode.

That AP is unauthenticated and the page posts over plain HTTP within it, so
only run setup somewhere you're comfortable with a brief open local
network - fine for "device in your hand, phone in the other," not for
leaving the portal open unattended.

**Or on the SD card, if you prefer:** copy `sdcard-template/wifi.json.example`
to `wifi.json` on the card root and fill in your network:

```json
{"ssid": "your-wifi-name", "password": "your-wifi-password"}
```

If both exist, the on-device NVS credentials (from Wi-Fi Setup, or migrated
from a previous SD-card `wifi.json`) take priority; the SD card is only
consulted when NVS has nothing saved yet.
