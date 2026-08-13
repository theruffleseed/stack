#pragma once

// Wi-Fi Sync: a temporary open access point that serves a small web page
// (sync_page.h) for pushing content to the SD card from a phone - e-books
// (.txt) and membership QR cards (generated as 1-bit BMPs in the browser).
// The device receives chunked uploads over HTTP and writes them straight to
// the card, then registers them in manifest.json so the lists pick them up
// on the next rescan. See docs/CONTENT.md for the on-card layout.
namespace SyncPortal {

// Blocking, like WifiProvision::runSetupPortal(). Requires the SD card to
// be mounted; draws progress screens on the panel and returns when the
// session times out or BOOT is pressed. Call Storage::loadManifest() after
// it returns.
bool runSyncPortal();

} // namespace SyncPortal
