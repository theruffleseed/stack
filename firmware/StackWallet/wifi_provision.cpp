#include "wifi_provision.h"
#include "config.h"
#include "display.h"
#include "fonts.h"

#include "GUI_Paint.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <vector>

namespace {

const char *kPrefsNamespace = "wifi";

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

const char *kSetupPageBody =
    "<!doctype html><html><head><meta name=viewport "
    "content='width=device-width,initial-scale=1'>"
    "<title>Stack Wallet Wi-Fi Setup</title></head>"
    "<body style='font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 16px'>"
    "<h2>Stack Wallet Wi-Fi Setup</h2>"
    "<form method=POST action=/save>"
    "<label>Network name (SSID)<br><input name=ssid required "
    "style='width:100%;padding:8px;margin:8px 0;box-sizing:border-box'></label><br>"
    "<label>Password<br><input name=password type=password "
    "style='width:100%;padding:8px;margin:8px 0;box-sizing:border-box'></label><br>"
    "<button type=submit style='padding:10px 20px'>Connect</button>"
    "</form></body></html>";

const char *kSavedPageBody =
    "<!doctype html><html><body style='font-family:sans-serif;text-align:center;padding-top:60px'>"
    "<h2>Saved</h2><p>Stack Wallet is reconnecting. You can close this page.</p></body></html>";

} // namespace

namespace WifiProvision {

bool load(String &ssid, String &password) {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, /*readOnly=*/true)) return false;
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("password", "");
    prefs.end();
    return ssid.length() > 0;
}

void save(const String &ssid, const String &password) {
    Preferences prefs;
    prefs.begin(kPrefsNamespace, /*readOnly=*/false);
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    prefs.end();
}

bool runSetupPortal() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SETUP_AP_SSID);
    IPAddress apIP = WiFi.softAPIP();

    DNSServer dns;
    dns.start(53, "*", apIP);

    WebServer server(80);

    bool submitted = false;
    String newSsid, newPassword;

    server.on("/save", HTTP_POST, [&]() {
        newSsid = server.arg("ssid");
        newPassword = server.arg("password");
        submitted = newSsid.length() > 0;
        server.send(200, "text/html", kSavedPageBody);
    });
    server.onNotFound([&]() { server.send(200, "text/html", kSetupPageBody); });
    server.begin();

    std::vector<String> instructions = {
        String("1. Connect phone/PC Wi-Fi to:"),
        String("   ") + WIFI_SETUP_AP_SSID,
        String("2. A setup page should open."),
        String("   If not, browse to:"),
        String("   http://") + apIP.toString(),
        String(""),
        String("BOOT to cancel"),
    };
    drawMessage("Wi-Fi Setup", instructions);

    unsigned long start = millis();
    while (!submitted && millis() - start < WIFI_SETUP_TIMEOUT_MS) {
        dns.processNextRequest();
        server.handleClient();
        if (digitalRead(BTN_BACK_PIN) == LOW) {
            delay(50); // debounce
            if (digitalRead(BTN_BACK_PIN) == LOW) break; // cancelled
        }
        delay(2);
    }

    dns.stop();
    server.stop();
    WiFi.softAPdisconnect(true);

    if (!submitted) {
        drawMessage("Wi-Fi Setup", {String("Cancelled or timed out."), String("No changes made.")});
        delay(1500);
        return false;
    }

    save(newSsid, newPassword);
    drawMessage("Wi-Fi Setup", {String("Saved. Reconnecting..."), newSsid});

    WiFi.mode(WIFI_STA);
    WiFi.begin(newSsid.c_str(), newPassword.c_str());
    unsigned long connStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - connStart < 15000) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        drawMessage("Wi-Fi Setup", {String("Connected!"), WiFi.localIP().toString()});
    } else {
        drawMessage("Wi-Fi Setup",
                     {String("Saved, but couldn't connect."), String("Check the password and retry.")});
    }
    delay(1500);
    return true;
}

} // namespace WifiProvision
