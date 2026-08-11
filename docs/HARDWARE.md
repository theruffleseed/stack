# Hardware reference

Board: [Waveshare ESP32-S3 ePaper 3.97"](https://www.waveshare.com/esp32-s3-epaper-3.97.htm)
([wiki](https://docs.waveshare.com/ESP32-S3-ePaper-3.97),
[schematic](https://files.waveshare.com/wiki/ESP32-S3-ePaper-3.97/ESP32-S3_e-Paper-3.97-schematic.pdf)).

Every pin number and setting below was pulled directly from Waveshare's own
Arduino and ESP-IDF example code for this exact board
([waveshareteam/ESP32-S3-ePaper-3.97](https://github.com/waveshareteam/ESP32-S3-ePaper-3.97)),
not guessed - see the `Arduino/examples/02_E-Paper_Example` and `05_SD_Test`
sketches, and the `ESP-IDF/08_ESP32-S3_e-Paper-3.97/components/button_bsp`
and `i2c_bsp` components.

## Display

- Panel: 3.97", 800x480 native resolution, black/white (4-gray also
  supported by the vendored driver but not currently used by the firmware)
- Driven by the vendored `EPD_3in97` + `GUI_Paint` driver (bit-banged SPI,
  not the Arduino `SPI` class)
- Rendered in **portrait** (480x800, chin at the bottom) by default via
  `DISPLAY_ROTATE` in `firmware/StackWallet/config.h` - the panel's own
  scan direction is unchanged, GUI_Paint remaps drawing coordinates. If the
  chin lands on the wrong edge for your unit, flip `DISPLAY_MIRROR` in that
  same file rather than the rotation (see the comment there for why).

| Signal | GPIO |
|--------|------|
| SCK    | 11   |
| MOSI   | 12   |
| CS     | 10   |
| RST    | 46   |
| DC     | 9    |
| BUSY   | 3    |
| PWR    | not used on this board revision (9-pin panel cable) |

## microSD (SDMMC, used in 1-bit mode)

| Signal | GPIO |
|--------|------|
| CLK    | 16   |
| CMD    | 17   |
| D0     | 15   |
| D1     | 7    |
| D2     | 8    |
| D3     | 18   |

Mounted at `/sdcard`. Waveshare's own SD test example wires all four data
pins but calls `SD_MMC.begin("/sdcard", true)` (1-bit mode) - the firmware
matches that exact call for consistency with a verified-working config.

## Buttons (active-LOW, `INPUT_PULLUP`)

| Button   | GPIO | Firmware role         |
|----------|------|------------------------|
| Up       | 4    | move selection up / previous page |
| Function | 5    | select / toggle / confirm |
| Down     | 6    | move selection down / next page |
| Boot     | 0    | back / home (shared with the BOOT strapping pin) |

## Onboard peripherals not used by Stack Wallet (yet)

The board also carries an I2C bus (SDA=41, SCL=42) with an AXP2101 PMU/fuel
gauge (0x34), a PCF85063 RTC (0x51), an SHTC3 temp/humidity sensor (0x70),
and a QMI8658 IMU (0x6a/0x6b), plus I2S audio. None of these are wired up in
v1 - see the README roadmap for where a battery percentage readout (AXP2101)
or clock (PCF85063) would plug in.

## Flash / PSRAM

16MB flash, octal (OPI) PSRAM. In Arduino IDE: **Tools > PSRAM > OPI
PSRAM**, **Tools > Flash Size > 16MB**. Partition Scheme can be left on its
default - this sketch ships its own `partitions.csv` with dual OTA app
slots, which the Arduino-ESP32 build system always prefers over the Tools
menu selection (see [OTA.md](OTA.md) for why the board's stock
single-partition layout can't take OTA updates).

## USB

This board uses the ESP32-S3's native USB peripheral over the single
USB-C port - there's no separate CP210x/CH340 UART bridge chip, so no
vendor USB driver should be needed on any OS. In Arduino IDE, **Tools >
USB CDC On Boot > Enabled** is required to get Serial output over that
same port (it's not the toolchain default - confirmed against Waveshare's
own recommended Tools configuration for this board). USB Mode ("Hardware
CDC and JTAG") and Upload Mode ("UART0 / Hardware CDC") can stay on their
defaults, which already match Waveshare's recommendation.
