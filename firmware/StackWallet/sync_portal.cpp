#include "sync_portal.h"
#include "config.h"
#include "display.h"
#include "storage.h"
#include "sync_page.h"

#include "GUI_Paint.h"
#include "fonts.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <vector>

namespace {

// One upload at a time: the phone streams a file as a sequence of small POST
// bodies (seq 0..n), each appended to the file on the card. State is kept in
// this single struct; WebServer serves one request at a time.
struct {
    FILE *file = nullptr;
    size_t bytes = 0;
    size_t total = 0;
} upload;

void drawMessage(const char *title, const std::vector<String> &lines) {
    Display::beginFrame();
    Paint_DrawRectangle(0, 0, Display::width() - 1, 40, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(10, 8, title, &Font20, WHITE, BLACK);

    int y = 55;
    int lineH = Font16.Height + 10;
    for (const auto &line : lines) {
        Paint_DrawString_EN(10, y, line.c_str(), &Font16, BLACK, WHITE);
        y += lineH;
    }

    Display::endFrame(true);
}

// Filenames arrive from the phone; keep only a safe character set so a
// crafted name can't escape the target directory.
String sanitizeName(const String &raw) {
    String s;
    for (unsigned i = 0; i < raw.length() && s.length() < 40; i++) {
        char c = raw[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.') {
            s += c;
        } else {
            s += '_';
        }
    }
    return s;
}

} // namespace

namespace SyncPortal {

bool runSyncPortal() {
    if (!Storage::isMounted()) {
        drawMessage("Wi-Fi Sync",
                    {String("No SD card detected."),
                     String("Insert the microSD card and retry.")});
        return false;
    }

    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SYNC_AP_SSID);
    IPAddress apIP = WiFi.softAPIP();

    DNSServer dns;
    dns.start(53, "*", apIP);

    WebServer server(80);

    // Every path that is not /upload serves the sync page, which also makes
    // the phone's captive-portal probe (hotspot-detect.html, generate_204,
    // ...) land on the page.
    server.on("/", HTTP_GET,
              [&]() { server.send_P(200, "text/html", kSyncPageHtml); });
    server.onNotFound(
        [&]() { server.send_P(200, "text/html", kSyncPageHtml); });
    server.begin();

    server.on("/upload", HTTP_POST, [&]() {
        const String dir = server.arg("dir");
        String name = sanitizeName(server.arg("name"));
        const long total = server.arg("total").toInt();
        const int seq = server.arg("seq").toInt();
        const bool isFinal = server.arg("final") == "1";

        if (dir != "books" && dir != "cards") {
            server.send(400, "text/plain", "bad dir");
            return;
        }
        if (name.length() == 0) {
            server.send(400, "text/plain", "bad name");
            return;
        }
        const char *ext = (dir == "books") ? ".txt" : ".bmp";
        if (!name.endsWith(ext)) name += ext;

        const String &body = server.arg("plain");

        if (seq == 0 || upload.file == nullptr) {
            if (upload.file) fclose(upload.file);
            upload.bytes = 0;
            upload.total = (total > 0) ? (size_t)total : 0;
            String path = String(SD_MOUNT_POINT) + "/" + dir + "/" + name;
            upload.file = fopen(path.c_str(), "wb");
            if (upload.file) {
                Serial.printf("Sync: receiving %s (%u bytes)\n", path.c_str(),
                              (unsigned)upload.total);
            }
        }
        if (!upload.file) {
            server.send(500, "text/plain", "open failed");
            return;
        }

        size_t wrote = fwrite(body.c_str(), 1, body.length(), upload.file);
        upload.bytes += wrote;

        if (isFinal) {
            fclose(upload.file);
            upload.file = nullptr;
            if (upload.total > 0 && upload.bytes != upload.total) {
                Serial.printf("Sync: size mismatch - got %u, expected %u\n",
                              (unsigned)upload.bytes, (unsigned)upload.total);
            }
            String rel = String(dir) + "/" + name;
            bool listed = Storage::addManifestItem(dir.c_str(), name, rel);
            Serial.printf("Sync: done %s (manifest %s)\n", rel.c_str(),
                          listed ? "updated" : "NOT updated");
            server.send(200, "text/plain", listed ? "OK" : "OK (manifest not updated)");
        } else {
            server.send(200, "text/plain", "OK");
        }
    });

    std::vector<String> instructions = {
        String("1. Connect phone/PC Wi-Fi to:"),
        String("   ") + WIFI_SYNC_AP_SSID,
        String("2. Open the page it suggests,"),
        String("   or browse to: http://") + apIP.toString(),
        String(""),
        String("Upload .txt books and"),
        String("membership QR cards."),
        String("BOOT to cancel"),
    };
    drawMessage("Wi-Fi Sync", instructions);

    Serial.printf("Sync portal on http://%s (SSID %s)\n", apIP.toString().c_str(),
                  WIFI_SYNC_AP_SSID);

    unsigned long start = millis();
    while (millis() - start < WIFI_SYNC_TIMEOUT_MS) {
        dns.processNextRequest();
        server.handleClient();
        if (digitalRead(BTN_BACK_PIN) == LOW) {
            delay(50); // debounce
            if (digitalRead(BTN_BACK_PIN) == LOW) break; // cancelled
        }
        delay(2);
    }

    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);

    if (upload.file) {
        fclose(upload.file);
        upload.file = nullptr;
    }

    drawMessage("Wi-Fi Sync",
                {String("Sync session ended."),
                 String("New content is ready on the wallet.")});
    return true;
}

} // namespace SyncPortal
