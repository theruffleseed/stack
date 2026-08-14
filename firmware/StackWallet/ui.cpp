#include "ui.h"
#include "battery.h"
#include "config.h"
#include "display.h"
#include "ota.h"
#include "storage.h"
#include "sync_portal.h"
#include "version.h"
#include "wifi_provision.h"

#include "GUI_BMPfile.h"
#include "GUI_Paint.h"
#include "fonts.h"

#include <WiFi.h>
#include <vector>
#include <cstdio>

namespace {

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

struct Button {
    uint8_t pin;
    bool stableLow = false;
    bool lastReading = false;
    unsigned long lastChangeMs = 0;

    // Explicit constructor rather than relying on aggregate init: some
    // ESP32 Arduino core versions still compile with -std=gnu++11, where a
    // struct with default member initializers is not an aggregate and
    // Button btnUp{PIN} would fail to find a matching constructor.
    explicit Button(uint8_t p) : pin(p) {}

    void begin() {
        pinMode(pin, INPUT_PULLUP);
    }

    // Returns true once per press (falling edge, debounced).
    bool pressed() {
        bool reading = (digitalRead(pin) == LOW);
        if (reading != lastReading) {
            lastReading = reading;
            lastChangeMs = millis();
        }
        if (millis() - lastChangeMs > 30 && stableLow != reading) {
            stableLow = reading;
            return stableLow; // fires on the LOW (pressed) transition only
        }
        return false;
    }
};

Button btnUp{BTN_UP_PIN};
Button btnDown{BTN_DOWN_PIN};
Button btnSelect{BTN_SELECT_PIN};
Button btnBack{BTN_BACK_PIN};

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

enum Screen {
    SCREEN_HOME,
    SCREEN_CARDS,
    SCREEN_CARD_VIEW,
    SCREEN_TICKETS,
    SCREEN_TICKET_VIEW,
    SCREEN_TODO,
    SCREEN_QR,
    SCREEN_BUSINESS,
    SCREEN_BOOKS,
    SCREEN_BOOK_READ,
    SCREEN_SETTINGS,
};

Screen currentScreen = SCREEN_HOME;
bool dirty = true;

struct ListState {
    int selected = 0;
    int scrollTop = 0;
};

ListState homeState, cardsState, ticketsState, booksState, todoState, settingsState;
std::vector<TodoItem> todoItems;

const char *kSettingsActions[] = {"Rescan SD Card", "Check for Updates", "Wi-Fi Setup",
                                  "Wi-Fi Sync"};
const int kSettingsActionCount = sizeof(kSettingsActions) / sizeof(kSettingsActions[0]);

// Book reader state
FILE *readerFile = nullptr;
std::vector<uint32_t> readerPageOffsets;
int readerPageIndex = 0;
String readerTitle;

// ---------------------------------------------------------------------------
// Layout metrics (480x800 portrait, 1-bit). All screens share these so the
// whole UI reads as one system.
// ---------------------------------------------------------------------------

const int kHeaderH = 44;   // black title band at the top of list screens
const int kFooterH = 26;   // hint line + rule at the bottom
const int kRowH = 34;      // standard list row (Font16 + padding)
const int kListTop = kHeaderH + 12;
const int kEdge = 14;      // horizontal margin used by headers/footers/rows
const int kAccentW = 8;    // width of the left accent bar on the selected row

int textWidth(const char *s, const sFONT &font) {
    return strlen(s) * font.Width;
}

int textWidth(const String &s, const sFONT &font) {
    return s.length() * font.Width;
}

int centerX(const char *s, const sFONT &font) {
    return max(0, (Display::width() - textWidth(s, font)) / 2);
}

int visibleRows() {
    return max(1, (Display::height() - kListTop - kFooterH) / kRowH);
}

void clampSelection(ListState &st, int count) {
    if (count == 0) {
        st.selected = 0;
        st.scrollTop = 0;
        return;
    }
    st.selected = constrain(st.selected, 0, count - 1);
    int rows = visibleRows();
    if (st.selected < st.scrollTop) st.scrollTop = st.selected;
    if (st.selected >= st.scrollTop + rows) st.scrollTop = st.selected - rows + 1;
    if (st.scrollTop < 0) st.scrollTop = 0;
}

void moveSelection(ListState &st, int delta, int count) {
    if (count == 0) return;
    st.selected = constrain(st.selected + delta, 0, count - 1);
    clampSelection(st, count);
}

// ---------------------------------------------------------------------------
// Shared chrome: header band, footer hint line
// ---------------------------------------------------------------------------

// Battery icon, right-aligned in the top-right corner. `c` is the foreground
// color so it works on white (home) and black (header band) backgrounds.
// Classic 4-bar fill for crisp 1-bit e-ink rendering; a lightning bolt is
// cut out while charging. Hidden when no cell is detected.
void drawBatteryIcon(UWORD c) {
    const int W = Display::width();
    const int h = 20;
    const int y = 12;
    const int w = 30;
    const int x = W - w - 12;
    const UWORD bg = (c == WHITE) ? BLACK : WHITE; // bolt color = background

    const bool present = Battery::present();
    const int pct = Battery::percent();
    const bool charging = Battery::isCharging();
    if (!present && !charging) return;

    // Terminal nub
    Paint_DrawRectangle(x + w, y + 6, x + w + 2, y + 13, c, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    // Outline
    Paint_DrawRectangle(x, y, x + w - 1, y + h - 1, c, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    // Segmented fill: 1..4 bars, 1px gaps
    if (pct >= 0 && !charging) {
        const int segs = max(1, (pct + 24) / 25);
        const int segW = 5;
        const int gap = 1;
        for (int i = 0; i < segs; i++) {
            Paint_DrawRectangle(x + 2 + i * (segW + gap), y + 2,
                                x + 2 + i * (segW + gap) + segW - 1, y + h - 3, c,
                                DOT_PIXEL_1X1, DRAW_FILL_FULL);
        }
    }

    // Lightning bolt while charging
    if (charging) {
        const int bx = x + w / 2 - 4;
        Paint_DrawLine(bx + 7, y + 1, bx + 1, y + 8, bg, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(bx + 1, y + 8, bx + 6, y + 8, bg, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(bx + 6, y + 8, bx + 2, y + h - 2, bg, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }
}

void drawHeader(const char *title) {
    Paint_DrawString_EN(kEdge, (kHeaderH - Font20.Height) / 2, title, &Font20, BLACK, WHITE);
    Paint_DrawLine(0, kHeaderH - 1, Display::width(), kHeaderH - 1, BLACK, DOT_PIXEL_1X1,
                   LINE_STYLE_SOLID);
    drawBatteryIcon(WHITE);
}

void drawFooter(const char *hint) {
    const int fy = Display::height() - kFooterH;
    Paint_DrawLine(0, fy, Display::width(), fy, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawString_EN(kEdge, fy + 5, hint, &Font12, BLACK, WHITE);
}

// ---------------------------------------------------------------------------
// Home menu: wordmark, live status line, icon menu rows
// ---------------------------------------------------------------------------

enum HomeIcon {
    ICON_CARD,
    ICON_TICKET,
    ICON_TODO,
    ICON_QR,
    ICON_BOOK,
    ICON_SETTINGS,
};

struct HomeEntry {
    const char *label;
    const char *caption;
    HomeIcon icon;
    Screen target;
};

const HomeEntry kHomeMenu[] = {
    {"DuitNow QR", "Receive money", ICON_QR, SCREEN_QR},
    {"Business Card", "Scan to save contact", ICON_CARD, SCREEN_BUSINESS},
    {"Loyalty Cards", "Rewards & barcodes", ICON_CARD, SCREEN_CARDS},
    {"To-Do List", "Check things off", ICON_TODO, SCREEN_TODO},
    {"E-Book Reader", "Read from the card", ICON_BOOK, SCREEN_BOOKS},
    {"Settings", "Wi-Fi, updates, info", ICON_SETTINGS, SCREEN_SETTINGS},
};
const int kHomeMenuCount = sizeof(kHomeMenu) / sizeof(kHomeMenu[0]);

// Geometric glyphs drawn in a 32x32 box. `c` is the foreground color, so the
// same drawing code works on white (normal) and black (selected) rows.
void drawIcon(HomeIcon icon, int x, int y, UWORD c) {
    const UWORD bg = (c == WHITE) ? BLACK : WHITE; // to punch holes in fills
    switch (icon) {
        case ICON_CARD:
            Paint_DrawRectangle(x + 2, y + 5, x + 29, y + 26, c, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            Paint_DrawRectangle(x + 6, y + 8, x + 13, y + 13, c, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            Paint_DrawLine(x + 5, y + 18, x + 26, y + 18, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawLine(x + 8, y + 21, x + 14, y + 21, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            break;
        case ICON_TICKET:
            Paint_DrawRectangle(x + 2, y + 4, x + 29, y + 27, c, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            Paint_DrawLine(x + 19, y + 4, x + 19, y + 27, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawRectangle(x + 17, y + 2, x + 21, y + 5, c, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            Paint_DrawRectangle(x + 17, y + 26, x + 21, y + 29, c, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            break;
        case ICON_TODO:
            Paint_DrawRectangle(x + 4, y + 4, x + 27, y + 27, c, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            Paint_DrawLine(x + 7, y + 16, x + 13, y + 22, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawLine(x + 13, y + 22, x + 24, y + 9, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            break;
        case ICON_QR: {
            // Three finder patterns (ring + solid center) + module dots.
            auto finder = [&](int fx, int fy) {
                Paint_DrawRectangle(fx, fy, fx + 7, fy + 7, c, DOT_PIXEL_1X1, DRAW_FILL_FULL);
                Paint_DrawRectangle(fx + 1, fy + 1, fx + 6, fy + 6, bg, DOT_PIXEL_1X1,
                                    DRAW_FILL_FULL);
                Paint_DrawRectangle(fx + 2, fy + 2, fx + 5, fy + 5, c, DOT_PIXEL_1X1,
                                    DRAW_FILL_FULL);
            };
            finder(x + 2, y + 2);
            finder(x + 22, y + 2);
            finder(x + 2, y + 22);
            Paint_DrawRectangle(x + 14, y + 14, x + 15, y + 15, c, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            Paint_DrawRectangle(x + 19, y + 11, x + 20, y + 12, c, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            Paint_DrawRectangle(x + 24, y + 24, x + 25, y + 25, c, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            break;
        }
        case ICON_BOOK:
            Paint_DrawRectangle(x + 3, y + 5, x + 28, y + 26, c, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            Paint_DrawLine(x + 8, y + 5, x + 8, y + 26, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawLine(x + 12, y + 10, x + 25, y + 10, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawLine(x + 12, y + 14, x + 25, y + 14, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawLine(x + 12, y + 18, x + 25, y + 18, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            break;
        case ICON_SETTINGS:
            Paint_DrawLine(x + 3, y + 8, x + 28, y + 8, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawRectangle(x + 11, y + 5, x + 16, y + 11, c, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            Paint_DrawLine(x + 3, y + 16, x + 28, y + 16, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawRectangle(x + 20, y + 13, x + 25, y + 19, c, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            Paint_DrawLine(x + 3, y + 24, x + 28, y + 24, c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawRectangle(x + 7, y + 21, x + 12, y + 27, c, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            break;
    }
}

String homeStatusLine() {
    String s;
    if (WiFi.status() == WL_CONNECTED) {
        s = "Wi-Fi " + WiFi.localIP().toString();
    } else {
        s = "Wi-Fi off";
    }
    s += "   ";
    s += Storage::isMounted() ? "SD on" : "SD missing";
    s += "   FW ";
    s += STACK_WALLET_VERSION;
    return s;
}

// When true, the next home render is pushed with the differential partial
// refresh so the selection accent bar moves without a full-panel flash.
bool homePartialPending = false;

void drawHomeContent(bool initPanel) {
    const int W = Display::width();
    const int H = Display::height();
    if (initPanel) {
        Display::beginFrame();
    } else {
        // Consecutive menu moves: the panel was already initialized by the
        // previous refresh, so skip the per-move reset for speed.
        Display::beginPartialFrame();
    }

    // Wordmark
    Paint_DrawString_EN(centerX("STACK WALLET", Font24), 20, "STACK WALLET", &Font24, BLACK,
                        WHITE);

    // Live status line (Wi-Fi / SD / firmware)
    String status = homeStatusLine();
    Paint_DrawString_EN(centerX(status.c_str(), Font12), 20 + Font24.Height + 8, status.c_str(),
                        &Font12, BLACK, WHITE);

    // Battery status, top-right of the masthead row
    drawBatteryIcon(BLACK);

    // Divider between the masthead and the menu
    Paint_DrawLine(24, 76, W - 24, 76, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    // Menu rows: icon + label + caption + chevron; the selected row gets a
    // left accent bar (text stays black on white for crisp e-ink rendering).
    const int rowH = 74;
    const int y0 = 86;
    for (int i = 0; i < kHomeMenuCount; i++) {
        const int y = y0 + i * rowH;
        const bool sel = (i == homeState.selected);

        if (sel) {
            Paint_DrawRectangle(8, y + 8, 8 + kAccentW + 4, y + rowH - 8, BLACK, DOT_PIXEL_1X1,
                                DRAW_FILL_FULL);
        }

        drawIcon(kHomeMenu[i].icon, 18, y + (rowH - 8 - 32) / 2, BLACK);
        Paint_DrawString_EN(64, y + 12, kHomeMenu[i].label, &Font20, BLACK, WHITE);
        Paint_DrawString_EN(64, y + 12 + Font20.Height + 5, kHomeMenu[i].caption, &Font12, BLACK,
                            WHITE);

        // Chevron
        const int cy = y + (rowH - 8) / 2;
        Paint_DrawLine(W - 34, cy - 6, W - 26, cy, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(W - 34, cy + 6, W - 26, cy, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

        // Row separator
        if (i < kHomeMenuCount - 1) {
            Paint_DrawLine(8, y + rowH - 4, W - 8, y + rowH - 4, BLACK, DOT_PIXEL_1X1,
                           LINE_STYLE_SOLID);
        }
    }

    // Owner banner at the foot of the screen: thin border box, contact and
    // medical (+/cross) icon so a lost unit can be returned to its owner.
    const int boxH = 76;
    const int by = H - kFooterH - boxH - 14;
    Paint_DrawRectangle(10, by, W - 10, by + boxH, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    // Plus/cross glyph, vertically centered next to the contact block.
    const int cy = by + boxH / 2;
    Paint_DrawRectangle(28, cy - 16, 36, cy + 16, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(20, cy - 6, 44, cy + 6, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    Paint_DrawString_EN(56, by + 10, "Property of Ken - if found, kindly", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(56, by + 32, "contact 017 8088 700 - Bloodtype: B", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(56, by + 54, "Emergency Contact: Sai 016 518 5081", &Font16, BLACK, WHITE);

    drawFooter("UP/DOWN move   SELECT open");
}

void drawHomeScreen() {
    drawHomeContent(true);
    Display::endFrame(true);
}

// ---------------------------------------------------------------------------
// Generic list rendering, used for the cards, tickets, books menus.
// ---------------------------------------------------------------------------

void drawListRow(bool sel, const String &text, int y) {
    const int W = Display::width();
    // Selection is a left accent bar, not an inverted fill: text stays black
    // on white so it renders crisply on e-ink fast refreshes.
    if (sel) {
        Paint_DrawRectangle(8, y + 5, 8 + kAccentW, y + kRowH - 9, BLACK, DOT_PIXEL_1X1,
                            DRAW_FILL_FULL);
    }
    Paint_DrawString_EN(sel ? kEdge + kAccentW + 6 : kEdge, y + 8, text.c_str(), &Font16, BLACK,
                        WHITE);
    Paint_DrawLine(kEdge, y + kRowH - 2, W - kEdge, y + kRowH - 2, BLACK, DOT_PIXEL_1X1,
                   LINE_STYLE_SOLID);
}

void drawListScreen(const char *title, const std::vector<String> &items, const ListState &st,
                    const char *emptyMessage, const char *footer) {
    Display::beginFrame();
    drawHeader(title);

    if (items.empty()) {
        Paint_DrawString_EN(kEdge, kListTop, emptyMessage, &Font16, BLACK, WHITE);
    } else {
        int rows = visibleRows();
        for (int i = 0; i < rows; i++) {
            int idx = st.scrollTop + i;
            if (idx >= (int)items.size()) break;
            drawListRow(idx == st.selected, items[idx], kListTop + i * kRowH);
        }
    }

    drawFooter(footer);
    Display::endFrame(true);
}

std::vector<String> namesOf(const std::vector<ContentItem> &items) {
    std::vector<String> out;
    out.reserve(items.size());
    for (const auto &item : items) out.push_back(item.name);
    return out;
}

// ---------------------------------------------------------------------------
// Image viewer (cards, tickets). The BMP is full-panel, so this is just the
// missing-file/empty diagnostics around GUI_ReadBmp(). A clean refresh keeps
// the photo-like content ghost-free.
// ---------------------------------------------------------------------------

// POSIX existence probe: the Arduino SD_MMC.exists() wrapper double-prefixes
// the mount point in this core and would report every file as missing.
bool fileExists(const String &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

void showImage(const char *title, const String &path) {
    Display::beginFrame();
    if (path.length() == 0) {
        Paint_DrawString_EN(14, 60, title, &Font20, BLACK, WHITE);
        Paint_DrawString_EN(14, 96, "No image configured.", &Font16, BLACK, WHITE);
        drawFooter("BOOT back");
    } else if (!fileExists(path)) {
        Paint_DrawString_EN(14, 60, title, &Font20, BLACK, WHITE);
        Paint_DrawString_EN(14, 96, "File missing on SD card:", &Font16, BLACK, WHITE);
        Paint_DrawString_EN(14, 120, path.c_str(), &Font12, BLACK, WHITE);
        drawFooter("BOOT back");
    } else {
        GUI_ReadBmp(path.c_str(), 0, 0);
    }
    Display::endFrame(/*fast=*/false); // images benefit from a clean, ghost-free refresh
}

// ---------------------------------------------------------------------------
// DuitNow receive screen. The QR BMP reserves white space at the top and
// bottom (see tools/make_bmp.py --top/--bottom); the firmware composites
// the title and the scan hint on top of that white space, so the receive
// screen reads as a designed screen rather than a raw image dump.
// ---------------------------------------------------------------------------

void drawDuitNowScreen() {
    const int W = Display::width();
    const int H = Display::height();
    const String &path = Storage::duitnowQrPath();
    Display::beginFrame();

    if (path.length() == 0) {
        Paint_DrawString_EN(kEdge, 60, "No DuitNow QR configured.", &Font20, BLACK, WHITE);
        Paint_DrawString_EN(kEdge, 96, "Put qr/duitnow.bmp on the SD card", &Font16, BLACK,
                            WHITE);
        Paint_DrawString_EN(kEdge, 120, "and set duitnow_qr in manifest.json.", &Font16, BLACK,
                            WHITE);
        drawFooter("BOOT back");
    } else if (!fileExists(path)) {
        Paint_DrawString_EN(kEdge, 60, "QR file missing on SD card:", &Font20, BLACK, WHITE);
        Paint_DrawString_EN(kEdge, 100, path.c_str(), &Font12, BLACK, WHITE);
        drawFooter("BOOT back");
    } else {
        GUI_ReadBmp(path.c_str(), 0, 0);

        // Top band (reserved white in the BMP): title + underline + tagline
        Paint_DrawString_EN(26, 22, "DuitNow", &Font24, BLACK, WHITE);
        Paint_DrawRectangle(28, 22 + Font24.Height + 5, 28 + textWidth("DuitNow", Font24),
                            22 + Font24.Height + 8, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_EN(28, 22 + Font24.Height + 15, "Scan to receive money", &Font16, BLACK,
                            WHITE);

        // Bottom band (reserved white in the BMP): hint + account label
        Paint_DrawString_EN(centerX("Scan with your banking app", Font16), H - 74,
                            "Scan with your banking app", &Font16, BLACK, WHITE);
        if (Storage::duitnowLabel().length() > 0) {
            Paint_DrawString_EN(centerX(Storage::duitnowLabel().c_str(), Font20), H - 46,
                                Storage::duitnowLabel().c_str(), &Font20, BLACK, WHITE);
        }
    }

    Display::endFrame(/*fast=*/false); // clean refresh: QR must be crisp
}

// ---------------------------------------------------------------------------
// To-do list
// ---------------------------------------------------------------------------

void drawTodoScreen() {
    Display::beginFrame();
    drawHeader("To-Do List");

    if (todoItems.empty()) {
        Paint_DrawString_EN(kEdge, kListTop, "todo.txt is empty or missing.", &Font16, BLACK,
                            WHITE);
    } else {
        int rows = visibleRows();
        for (int i = 0; i < rows; i++) {
            int idx = todoState.scrollTop + i;
            if (idx >= (int)todoItems.size()) break;
            const int y = kListTop + i * kRowH;
            const bool sel = (idx == todoState.selected);
            const int cx = sel ? kEdge + kAccentW + 6 : kEdge;

            if (sel) {
                Paint_DrawRectangle(8, y + 5, 8 + kAccentW, y + kRowH - 9, BLACK, DOT_PIXEL_1X1,
                                    DRAW_FILL_FULL);
            }

            // Checkbox glyph: outlined box, ticked when done
            Paint_DrawRectangle(cx, y + 10, cx + 11, y + 21, BLACK, DOT_PIXEL_1X1,
                                DRAW_FILL_EMPTY);
            if (todoItems[idx].done) {
                Paint_DrawLine(cx + 2, y + 15, cx + 5, y + 18, BLACK, DOT_PIXEL_1X1,
                               LINE_STYLE_SOLID);
                Paint_DrawLine(cx + 5, y + 18, cx + 9, y + 12, BLACK, DOT_PIXEL_1X1,
                               LINE_STYLE_SOLID);
            }

            Paint_DrawString_EN(cx + 18, y + 8, todoItems[idx].text.c_str(), &Font16, BLACK,
                                WHITE);
            Paint_DrawLine(kEdge, y + kRowH - 2, Display::width() - kEdge, y + kRowH - 2, BLACK,
                           DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        }
    }

    drawFooter("UP/DOWN move   SELECT check   BOOT back");
    Display::endFrame(true);
}

// ---------------------------------------------------------------------------
// E-book reader (plain text, paginated with lazily-discovered page offsets)
// ---------------------------------------------------------------------------

uint32_t renderBookPage(FILE *f, uint32_t startOffset) {
    fseek(f, startOffset, SEEK_SET);
    Display::beginFrame();

    Paint_DrawString_EN(kEdge, 10, readerTitle.c_str(), &Font16, BLACK, WHITE);
    Paint_DrawLine(kEdge, 36, Display::width() - kEdge, 36, BLACK, DOT_PIXEL_1X1,
                   LINE_STYLE_SOLID);

    const int marginX = kEdge;
    const int top = 44;
    const int lineH = Font16.Height + 4;
    const int maxLines = max(1, (Display::height() - top - kFooterH) / lineH);
    const int maxCharsPerLine = max(1, (Display::width() - 2 * marginX) / Font16.Width);

    int line = 0;
    String cur, word;
    uint32_t pos = startOffset;
    uint32_t wordStartPos = startOffset;
    bool stoppedForOverflow = false;
    bool eof = false;

    while (true) {
        int c = fgetc(f);
        if (c == EOF) {
            eof = true;
            break;
        }
        pos++;
        if (c == '\r') continue;

        if (c == ' ' || c == '\n') {
            bool fits = (cur.length() == 0) ||
                        (cur.length() + 1 + word.length() <= (unsigned)maxCharsPerLine);
            if (!fits) {
                Paint_DrawString_EN(marginX, top + line * lineH, cur.c_str(), &Font16, BLACK,
                                    WHITE);
                line++;
                if (line >= maxLines) {
                    stoppedForOverflow = true;
                    pos = wordStartPos;
                    break;
                }
                cur = word;
            } else {
                if (cur.length()) cur += ' ';
                cur += word;
            }
            word = "";
            wordStartPos = pos;

            if (c == '\n') {
                Paint_DrawString_EN(marginX, top + line * lineH, cur.c_str(), &Font16, BLACK,
                                    WHITE);
                line++;
                cur = "";
                if (line >= maxLines) break;
            }
        } else {
            if (word.length() == 0) wordStartPos = pos - 1;
            if (word.length() < (unsigned)maxCharsPerLine) word += (char)c;
        }
    }

    if (!stoppedForOverflow) {
        if (word.length()) {
            if (cur.length()) cur += ' ';
            cur += word;
        }
        if (cur.length() && line < maxLines) {
            Paint_DrawString_EN(marginX, top + line * lineH, cur.c_str(), &Font16, BLACK, WHITE);
            line++;
        }
    }

    drawFooter(eof ? "End of book   BOOT: back" : "UP/DOWN page   BOOT: back");
    Display::endFrame(true);
    return pos;
}

void showReaderPage(int idx) {
    if (idx >= (int)readerPageOffsets.size()) idx = readerPageOffsets.size() - 1;
    if (idx < 0) idx = 0;
    readerPageIndex = idx;
    uint32_t next = renderBookPage(readerFile, readerPageOffsets[idx]);
    if (idx == (int)readerPageOffsets.size() - 1 && next != readerPageOffsets[idx]) {
        readerPageOffsets.push_back(next);
    }
}

void openBook(const ContentItem &book) {
    if (readerFile) fclose(readerFile);
    readerFile = fopen(book.path.c_str(), "r");
    readerTitle = book.name;
    readerPageOffsets.clear();
    readerPageOffsets.push_back(0);
    readerPageIndex = 0;
    if (!readerFile) {
        Display::beginFrame();
        Paint_DrawString_EN(kEdge, 60, "Could not open file on SD card.", &Font20, BLACK, WHITE);
        Paint_DrawString_EN(kEdge, 96, book.path.c_str(), &Font12, BLACK, WHITE);
        drawFooter("BOOT back");
        Display::endFrame(false);
        return;
    }
    showReaderPage(readerPageIndex);
}

// ---------------------------------------------------------------------------
// Settings: label/value info block + action rows
// ---------------------------------------------------------------------------

struct SettingsInfoRow {
    const char *label;
    String value;
};

void settingsInfoRows(SettingsInfoRow *rows, int &count) {
    rows[0] = {"FIRMWARE", STACK_WALLET_VERSION};
    rows[1] = {"WI-FI", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString()
                                                      : String("not connected")};
    rows[2] = {"SD CARD", Storage::isMounted() ? String("mounted") : String("not found")};
    rows[3] = {"OTA", OTA::lastCheckStatus()};
    count = 4;
}

// Draws the settings info rows starting at y, optionally reusing an existing
// OTA value (for a partial update after a manual check).
void drawSettingsInfo(int y, const char *otaStatusOverride = nullptr) {
    SettingsInfoRow rows[4];
    int count = 0;
    settingsInfoRows(rows, count);
    if (otaStatusOverride) rows[3].value = otaStatusOverride;

    for (int i = 0; i < count; i++) {
        Paint_DrawString_EN(kEdge, y + i * 30, rows[i].label, &Font12, BLACK, WHITE);
        Paint_DrawString_EN(kEdge + 88, y + i * 30 - 2, rows[i].value.c_str(), &Font16, BLACK,
                            WHITE);
    }
}

// Like the home screen, consecutive settings moves push only the changed
// pixels with a differential partial refresh instead of re-sending the whole
// panel every press. Full refresh only on first entry.
bool settingsPartialPending = false;

void drawSettingsScreen(bool partial) {
    if (partial) {
        Display::beginPartialFrame();
    } else {
        Display::beginFrame();
    }
    drawHeader("Settings");
    drawSettingsInfo(56);
    Paint_DrawLine(kEdge, 56 + 4 * 30 + 4, Display::width() - kEdge, 56 + 4 * 30 + 4, BLACK,
                   DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    const int y0 = 56 + 4 * 30 + 14;
    for (int i = 0; i < kSettingsActionCount; i++) {
        drawListRow(i == settingsState.selected, kSettingsActions[i], y0 + i * kRowH);
    }

    drawFooter("UP/DOWN move   SELECT choose   BOOT back");
    if (partial) {
        Display::partialFullFrame();
    } else {
        Display::endFrame(true);
    }
}

// ---------------------------------------------------------------------------
// Rendering dispatch
// ---------------------------------------------------------------------------

void render() {
    switch (currentScreen) {
        case SCREEN_HOME:
            if (homePartialPending) {
                homePartialPending = false;
                drawHomeContent(false);
                Display::partialFullFrame();
            } else {
                drawHomeScreen();
            }
            break;
        case SCREEN_CARDS:
            drawListScreen("Loyalty Cards", namesOf(Storage::cards()), cardsState,
                           "No cards on SD card. See docs/CONTENT.md.",
                           "UP/DOWN move   SELECT open   BOOT back");
            break;
        case SCREEN_CARD_VIEW:
            showImage("Loyalty Card", Storage::cards()[cardsState.selected].path);
            break;
        case SCREEN_TICKETS:
            drawListScreen("Flight Tickets", namesOf(Storage::tickets()), ticketsState,
                           "No tickets on SD card. See docs/CONTENT.md.",
                           "UP/DOWN move   SELECT open   BOOT back");
            break;
        case SCREEN_TICKET_VIEW:
            showImage("Flight Ticket", Storage::tickets()[ticketsState.selected].path);
            break;
        case SCREEN_TODO:
            drawTodoScreen();
            break;
        case SCREEN_QR:
            drawDuitNowScreen();
            break;
        case SCREEN_BUSINESS:
            showImage("Business Card", Storage::businessCardPath());
            break;
        case SCREEN_BOOKS:
            drawListScreen("E-Book Reader", namesOf(Storage::books()), booksState,
                           "No books on SD card. See docs/CONTENT.md.",
                           "UP/DOWN move   SELECT open   BOOT back");
            break;
        case SCREEN_BOOK_READ:
            // Page already rendered by showReaderPage() when this screen was entered
            // or navigated; nothing to redraw here on its own.
            break;
        case SCREEN_SETTINGS:
            if (settingsPartialPending) {
                settingsPartialPending = false;
                drawSettingsScreen(true);
            } else {
                drawSettingsScreen(false);
            }
            break;
    }
}

void goHome() {
    currentScreen = SCREEN_HOME;
    homePartialPending = false;
    dirty = true;
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void handleUp() {
    switch (currentScreen) {
        case SCREEN_HOME:
            moveSelection(homeState, -1, kHomeMenuCount);
            dirty = true;
            homePartialPending = true;
            break;
        case SCREEN_CARDS:
            moveSelection(cardsState, -1, Storage::cards().size());
            dirty = true;
            break;
        case SCREEN_TICKETS:
            moveSelection(ticketsState, -1, Storage::tickets().size());
            dirty = true;
            break;
        case SCREEN_BOOKS:
            moveSelection(booksState, -1, Storage::books().size());
            dirty = true;
            break;
        case SCREEN_TODO:
            moveSelection(todoState, -1, todoItems.size());
            dirty = true;
            break;
        case SCREEN_CARD_VIEW:
            if (!Storage::cards().empty()) {
                moveSelection(cardsState, -1, Storage::cards().size());
                dirty = true;
            }
            break;
        case SCREEN_TICKET_VIEW:
            if (!Storage::tickets().empty()) {
                moveSelection(ticketsState, -1, Storage::tickets().size());
                dirty = true;
            }
            break;
        case SCREEN_BOOK_READ:
            if (readerFile) showReaderPage(readerPageIndex - 1);
            break;
        case SCREEN_SETTINGS:
            moveSelection(settingsState, -1, kSettingsActionCount);
            dirty = true;
            settingsPartialPending = true;
            break;
        default:
            break;
    }
}

void handleDown() {
    switch (currentScreen) {
        case SCREEN_HOME:
            moveSelection(homeState, 1, kHomeMenuCount);
            dirty = true;
            homePartialPending = true;
            break;
        case SCREEN_CARDS:
            moveSelection(cardsState, 1, Storage::cards().size());
            dirty = true;
            break;
        case SCREEN_TICKETS:
            moveSelection(ticketsState, 1, Storage::tickets().size());
            dirty = true;
            break;
        case SCREEN_BOOKS:
            moveSelection(booksState, 1, Storage::books().size());
            dirty = true;
            break;
        case SCREEN_TODO:
            moveSelection(todoState, 1, todoItems.size());
            dirty = true;
            break;
        case SCREEN_CARD_VIEW:
            if (!Storage::cards().empty()) {
                moveSelection(cardsState, 1, Storage::cards().size());
                dirty = true;
            }
            break;
        case SCREEN_TICKET_VIEW:
            if (!Storage::tickets().empty()) {
                moveSelection(ticketsState, 1, Storage::tickets().size());
                dirty = true;
            }
            break;
        case SCREEN_BOOK_READ:
            if (readerFile) showReaderPage(readerPageIndex + 1);
            break;
        case SCREEN_SETTINGS:
            moveSelection(settingsState, 1, kSettingsActionCount);
            dirty = true;
            settingsPartialPending = true;
            break;
        default:
            break;
    }
}

void handleSelect() {
    switch (currentScreen) {
        case SCREEN_HOME:
            currentScreen = kHomeMenu[homeState.selected].target;
            dirty = true;
            break;
        case SCREEN_CARDS:
            if (!Storage::cards().empty()) {
                currentScreen = SCREEN_CARD_VIEW;
                dirty = true;
            }
            break;
        case SCREEN_TICKETS:
            if (!Storage::tickets().empty()) {
                currentScreen = SCREEN_TICKET_VIEW;
                dirty = true;
            }
            break;
        case SCREEN_BOOKS:
            if (!Storage::books().empty()) {
                openBook(Storage::books()[booksState.selected]);
                currentScreen = SCREEN_BOOK_READ;
                dirty = false; // openBook() already drew the first page
            }
            break;
        case SCREEN_TODO:
            if (!todoItems.empty()) {
                todoItems[todoState.selected].done = !todoItems[todoState.selected].done;
                Storage::saveTodo(todoItems);
                dirty = true;
            }
            break;
        case SCREEN_SETTINGS:
            if (settingsState.selected == 0) {
                // Rescan SD card: retry the mount (hot-plug fix) and reload content
                bool wasMounted = Storage::isMounted();
                if (!Storage::isMounted()) {
                    Storage::begin();
                }
                if (Storage::isMounted()) {
                    Storage::loadManifest();
                    todoItems = Storage::loadTodo();
                }
                if (!wasMounted) dirty = true; // settings info shows the new SD state
            } else if (settingsState.selected == 1) {
                OTA::checkAndApplyNow(); // reboots on success; falls through to redraw on failure
                // Partial update just the OTA row instead of a full screen flash
                const int rowY = 56 + 3 * 30;
                Display::beginPartialDraw();
                drawSettingsInfo(56, OTA::lastCheckStatus().c_str());
                Display::partialUpdate(kEdge, rowY - 4, Display::width() - kEdge, rowY + 28);
            } else if (settingsState.selected == 2) {
                WifiProvision::runSetupPortal();
                dirty = true;
            } else {
                // Wi-Fi Sync: phone connects to the wallet's AP and pushes
                // books / membership QR cards to the SD card.
                SyncPortal::runSyncPortal();
                Storage::loadManifest();
                dirty = true;
            }
            break;
        default:
            break;
    }
}

void handleBack() {
    switch (currentScreen) {
        case SCREEN_HOME:
            break; // already at the top
        case SCREEN_CARD_VIEW:
            currentScreen = SCREEN_CARDS;
            dirty = true;
            break;
        case SCREEN_TICKET_VIEW:
            currentScreen = SCREEN_TICKETS;
            dirty = true;
            break;
        case SCREEN_BOOK_READ:
            if (readerFile) fclose(readerFile);
            readerFile = nullptr;
            currentScreen = SCREEN_BOOKS;
            dirty = true;
            break;
        default:
            goHome();
            break;
    }
}

// ---------------------------------------------------------------------------
// Idle deep sleep: the e-ink image persists while the panel sleeps, so the
// device just goes quiet; the first button press wakes it.
// ---------------------------------------------------------------------------

bool asleep = false;
unsigned long lastActivityMs = 0;
unsigned long lastMountAttemptMs = 0;

void markActivity() {
    lastActivityMs = millis();
    if (asleep) asleep = false;
}

// The SD slot has no hot-plug detection: if the card isn't there (or isn't
// fully seated) when the firmware boots, it stays unmounted. Keep retrying
// quietly in the background so a card can be inserted/re-seated later and
// picked up without a reboot.
void retryMount() {
    if (Storage::isMounted()) return;
    if (millis() - lastMountAttemptMs < 3000) return;
    lastMountAttemptMs = millis();
    if (Storage::begin()) {
        Storage::loadManifest();
        todoItems = Storage::loadTodo();
        dirty = true;
        Serial.println("UI: SD card mounted (hot-plug)");
    }
}

void checkIdleSleep() {
    if (!asleep && IDLE_SLEEP_MS > 0 && millis() - lastActivityMs >= IDLE_SLEEP_MS) {
        asleep = true;
        Serial.println("UI: idle timeout - panel sleeping (press any button to wake)");
        Display::sleep();
    }
}

} // namespace

namespace UI {

void begin() {
    btnUp.begin();
    btnDown.begin();
    btnSelect.begin();
    btnBack.begin();

    Storage::loadManifest();
    todoItems = Storage::loadTodo();

    currentScreen = SCREEN_HOME;
    dirty = true;
    lastActivityMs = millis();
}

void loop() {
    if (asleep) {
        // Wake on any button; the press that woke the device is consumed so
        // it doesn't also navigate somewhere.
        if (btnUp.pressed() || btnDown.pressed() || btnSelect.pressed() || btnBack.pressed()) {
            markActivity();
            dirty = true; // re-init + redraw the current screen
            Serial.println("UI: woke from panel sleep");
        }
        return;
    }

    if (btnUp.pressed()) {
        markActivity();
        handleUp();
    }
    if (btnDown.pressed()) {
        markActivity();
        handleDown();
    }
    if (btnSelect.pressed()) {
        markActivity();
        handleSelect();
    }
    if (btnBack.pressed()) {
        markActivity();
        handleBack();
    }

    if (dirty) {
        render();
        dirty = false;
    }

    retryMount();
    checkIdleSleep();
}

} // namespace UI
