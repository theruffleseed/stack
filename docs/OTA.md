# OTA: pushing firmware from GitHub to the device

Stack Wallet uses a **pull-based OTA** model. There's no self-hosted runner
and no direct connection from GitHub to your device - instead, the device
periodically asks GitHub "is there anything newer than me?" and flashes
itself if so.

## The loop

```
you: git push (firmware/** changes) to main
        |
        v
.github/workflows/release.yml runs on GitHub's servers:
  - compiles firmware/StackWallet with arduino-cli
  - stamps version.h with STACK_WALLET_VERSION = "v0.1.<run number>"
  - publishes a GitHub Release tagged v0.1.<run number>,
    with firmware.bin attached
        |
        v
device, every OTA_CHECK_INTERVAL_MS (default 12h) or when you open
Settings and press the select button:
  - GET https://api.github.com/repos/<owner>/<repo>/releases/latest
  - compares tag_name to its own compiled-in STACK_WALLET_VERSION
  - if different: downloads the firmware.bin asset via HTTPUpdate,
    flashes the inactive OTA app slot, reboots into it
```

Because the version string is compiled into the binary itself (see
`firmware/StackWallet/version.h`, overwritten by the release workflow
before compiling), there's no separate "last applied version" bookkeeping
on the device - the running firmware always knows its own version, and a
freshly-flashed build naturally stops re-flashing itself once it matches
the latest release tag.

## Why Wi-Fi credentials aren't compiled in

Early on it's tempting to `#define WIFI_SSID "..."` in a gitignored
`secrets.h`. Two things rule that out for this project:

1. **`firmware.bin` is a public GitHub Release asset.** Anyone who can see
   the release can download it and pull the Wi-Fi password back out with a
   hex editor.
2. **OTA only rewrites the ESP32's internal app partition.** If Wi-Fi
   credentials were compiled in, every OTA build would need your real
   credentials baked in by CI to keep working - and the *first* OTA update
   built without them would leave the device unable to reach Wi-Fi at all,
   permanently cutting off future OTA checks too.

Instead, Wi-Fi credentials live either in the ESP32's internal NVS flash
(entered via **Settings > Wi-Fi Setup** on the device - see
[CONTENT.md](CONTENT.md#wi-fi-credentials)) or in `/sdcard/wifi.json`. OTA
only rewrites the app partition - it never touches NVS or the SD card -
so either way credentials survive every update, and neither ends up in a
compiled binary or in git.

## Partition table

The board's stock firmware uses a single non-OTA "factory" app partition.
Stack Wallet ships its own `firmware/StackWallet/partitions.csv` (dual
`ota_0`/`ota_1` app slots, the same layout as Arduino-ESP32's own
`default_16MB.csv`) so there's somewhere for `HTTPUpdate` to write the new
image. The Arduino-ESP32 build system always copies a `partitions.csv` that
sits next to the `.ino` file into the build output before falling back to
whatever the Tools > Partition Scheme menu would otherwise select
(confirmed against `platform.txt`'s `recipe.hooks.prebuild` rules) - so this
file takes effect automatically, whatever that menu is set to. CI passes no
`PartitionScheme` option for the same reason.

## Security tradeoffs of the current setup

- `ota.cpp` uses `WiFiClientSecure::setInsecure()` for both the GitHub API
  call and the firmware download - TLS is used for encryption but the
  server certificate isn't verified against a pinned CA. This is standard
  practice in a lot of ESP32 OTA tutorials and is a reasonable choice for a
  personal gadget pulling its own repo's public releases, but it does mean
  a network-position attacker could in principle serve a different binary.
  To harden this, embed GitHub's and objects.githubusercontent.com's root
  CA certs and pass them to `WiFiClientSecure::setCACert()` instead of
  calling `setInsecure()`.
- There's no signature check on `firmware.bin` beyond TLS - anyone who can
  push to `main` (or who compromises the repo) can ship code that runs on
  the wallet. For a personal project this is an accepted tradeoff; a
  stricter setup would sign releases and verify the signature on-device
  before calling `Update.end()`.
- If the repo is public, release tags and binary contents (but not,
  per above, Wi-Fi credentials) are visible to anyone.

## Manual checks and tuning

- **Settings screen > select button**: triggers `OTA::checkAndApplyNow()`
  immediately, regardless of the auto-check interval.
- `config.h`: `OTA_CHECK_INTERVAL_MS` (default 12h) and
  `ENABLE_OTA_AUTOCHECK` (set to `0` to disable automatic polling entirely
  and rely on manual checks - useful if you want to conserve battery).
- `workflow_dispatch` is enabled on `release.yml`, so you can also trigger
  a release manually from the Actions tab without pushing a commit.
