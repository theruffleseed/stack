#include "ui.h"
#include "config.h"
#include "display.h"
#include "ota.h"
#include "storage.h"
#include "version.h"

#include "GUI_BMPfile.h"
#include "GUI_Paint.h"
#include "fonts.h"

#include <FS.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <vector>

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
    SCREEN_BOOKS,
    SCREEN_BOOK_READ,
    SCREEN_SETTINGS,
};

struct MenuEntry {
    const char *label;
    Screen target;
};

const MenuEntry kHomeMenu[] = {
    {"Loyalty Cards", SCREEN_CARDS},
    {"Flight Tickets", SCREEN_TICKETS},
    {"To-Do List", SCREEN_TODO},
    {"Receive (DuitNow QR)", SCREEN_QR},
    {"E-Book Reader", SCREEN_BOOKS},
    {"Settings", SCREEN_SETTINGS},
};
const int kHomeMenuCount = sizeof(kHomeMenu) / sizeof(kHomeMenu[0]);

Screen currentScreen = SCREEN_HOME;
bool dirty = true;

struct ListState {
    int selected = 0;
    int scrollTop = 0;
};

ListState homeState, cardsState, ticketsState, booksState, todoState;
std::vector<TodoItem> todoItems;

// Book reader state
File readerFile;
std::vector<uint32_t> readerPageOffsets;
int readerPageIndex = 0;
String readerTitle;

int visibleRows() {
    int rowH = Font16.Height + 14;
    return max(1, (Display::height() - 50) / rowH);
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
// Generic list rendering, used for the home menu, cards, tickets, books.
// ---------------------------------------------------------------------------

void drawListScreen(const char *title, const std::vector<String> &items, const ListState &st,
                     const char *emptyMessage, const char *footer) {
    Display::beginFrame();

    Paint_DrawRectangle(0, 0, Display::width() - 1, 40, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(10, 8, title, &Font20, WHITE, BLACK);

    if (items.empty()) {
        Paint_DrawString_EN(10, 60, emptyMessage, &Font16, BLACK, WHITE);
    } else {
        int rowH = Font16.Height + 14;
        int rows = visibleRows();
        for (int i = 0; i < rows; i++) {
            int idx = st.scrollTop + i;
            if (idx >= (int)items.size()) break;
            int y = 50 + i * rowH;
            bool isSel = (idx == st.selected);
            if (isSel) {
                Paint_DrawRectangle(5, y, Display::width() - 5, y + rowH - 4, BLACK, DOT_PIXEL_1X1,
                                     DRAW_FILL_FULL);
            }
            Paint_DrawString_EN(15, y + 6, items[idx].c_str(), &Font16, isSel ? WHITE : BLACK,
                                 isSel ? BLACK : WHITE);
        }
    }

    Paint_DrawString_EN(10, Display::height() - 20, footer, &Font12, BLACK, WHITE);
    Display::endFrame(true);
}

std::vector<String> namesOf(const std::vector<ContentItem> &items) {
    std::vector<String> out;
    out.reserve(items.size());
    for (const auto &item : items) out.push_back(item.name);
    return out;
}

void showImage(const char *title, const String &path) {
    Display::beginFrame();
    if (path.length() == 0) {
        Paint_DrawString_EN(10, 10, title, &Font20, BLACK, WHITE);
        Paint_DrawString_EN(10, 60, "No image configured.", &Font16, BLACK, WHITE);
    } else if (!SD_MMC.exists(path)) {
        Paint_DrawString_EN(10, 10, title, &Font20, BLACK, WHITE);
        Paint_DrawString_EN(10, 60, "File missing on SD card:", &Font16, BLACK, WHITE);
        Paint_DrawString_EN(10, 80, path.c_str(), &Font12, BLACK, WHITE);
    } else {
        GUI_ReadBmp(path.c_str(), 0, 0);
    }
    Display::endFrame(/*fast=*/false); // images benefit from a clean, ghost-free refresh
}

// ---------------------------------------------------------------------------
// To-do list
// ---------------------------------------------------------------------------

void drawTodoScreen() {
    Display::beginFrame();
    Paint_DrawRectangle(0, 0, Display::width() - 1, 40, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(10, 8, "To-Do List", &Font20, WHITE, BLACK);

    if (todoItems.empty()) {
        Paint_DrawString_EN(10, 60, "todo.txt is empty or missing.", &Font16, BLACK, WHITE);
    } else {
        int rowH = Font16.Height + 14;
        int rows = visibleRows();
        for (int i = 0; i < rows; i++) {
            int idx = todoState.scrollTop + i;
            if (idx >= (int)todoItems.size()) break;
            int y = 50 + i * rowH;
            bool isSel = (idx == todoState.selected);
            if (isSel) {
                Paint_DrawRectangle(5, y, Display::width() - 5, y + rowH - 4, BLACK, DOT_PIXEL_1X1,
                                     DRAW_FILL_FULL);
            }
            String line = String(todoItems[idx].done ? "[x] " : "[ ] ") + todoItems[idx].text;
            Paint_DrawString_EN(15, y + 6, line.c_str(), &Font16, isSel ? WHITE : BLACK,
                                 isSel ? BLACK : WHITE);
        }
    }

    Paint_DrawString_EN(10, Display::height() - 20, "UP/DOWN move  SELECT check  BOOT back", &Font12,
                         BLACK, WHITE);
    Display::endFrame(true);
}

// ---------------------------------------------------------------------------
// E-book reader (plain text, paginated with lazily-discovered page offsets)
// ---------------------------------------------------------------------------

uint32_t renderBookPage(File &f, uint32_t startOffset) {
    f.seek(startOffset);
    Display::beginFrame();
    Paint_DrawString_EN(10, 8, readerTitle.c_str(), &Font16, BLACK, WHITE);

    const int marginX = 10;
    const int top = 34;
    const int lineH = Font16.Height + 4;
    const int maxLines = max(1, (Display::height() - top - 24) / lineH);
    const int maxCharsPerLine = max(1, (Display::width() - 2 * marginX) / Font16.Width);

    int line = 0;
    String cur, word;
    uint32_t pos = startOffset;
    uint32_t wordStartPos = startOffset;
    bool stoppedForOverflow = false;
    bool eof = false;

    while (true) {
        if (!f.available()) {
            eof = true;
            break;
        }
        int c = f.read();
        pos++;
        if (c == '\r') continue;

        if (c == ' ' || c == '\n') {
            bool fits = (cur.length() == 0) ||
                        (cur.length() + 1 + word.length() <= (unsigned)maxCharsPerLine);
            if (!fits) {
                Paint_DrawString_EN(marginX, top + line * lineH, cur.c_str(), &Font16, BLACK, WHITE);
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
                Paint_DrawString_EN(marginX, top + line * lineH, cur.c_str(), &Font16, BLACK, WHITE);
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

    Paint_DrawString_EN(10, Display::height() - 20,
                         eof ? "End of book   BOOT: back" : "UP/DOWN page   BOOT: back", &Font12,
                         BLACK, WHITE);
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
    if (readerFile) readerFile.close();
    readerFile = SD_MMC.open(book.path, FILE_READ);
    readerTitle = book.name;
    readerPageOffsets.clear();
    readerPageOffsets.push_back(0);
    readerPageIndex = 0;
    if (!readerFile) {
        Display::beginFrame();
        Paint_DrawString_EN(10, 10, "E-Book Reader", &Font20, BLACK, WHITE);
        Paint_DrawString_EN(10, 60, "Could not open file on SD card.", &Font16, BLACK, WHITE);
        Paint_DrawString_EN(10, 60 + Font16.Height + 6, book.path.c_str(), &Font12, BLACK, WHITE);
        Display::endFrame(false);
        return;
    }
    showReaderPage(readerPageIndex);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void drawSettingsScreen() {
    Display::beginFrame();
    Paint_DrawRectangle(0, 0, Display::width() - 1, 40, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(10, 8, "Settings", &Font20, WHITE, BLACK);

    int y = 55;
    int lineH = Font16.Height + 10;
    String line;

    line = String("Firmware: ") + STACK_WALLET_VERSION;
    Paint_DrawString_EN(10, y, line.c_str(), &Font16, BLACK, WHITE);
    y += lineH;

    line = String("Wi-Fi: ") +
           (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("not connected"));
    Paint_DrawString_EN(10, y, line.c_str(), &Font16, BLACK, WHITE);
    y += lineH;

    line = String("SD card: ") + (Storage::isMounted() ? "mounted" : "not found");
    Paint_DrawString_EN(10, y, line.c_str(), &Font16, BLACK, WHITE);
    y += lineH;

    line = String("OTA: ") + OTA::lastCheckStatus();
    Paint_DrawString_EN(10, y, line.c_str(), &Font16, BLACK, WHITE);
    y += lineH;

    Paint_DrawString_EN(10, Display::height() - 20, "SELECT check for updates now  BOOT back",
                         &Font12, BLACK, WHITE);
    Display::endFrame(true);
}

// ---------------------------------------------------------------------------
// Rendering dispatch
// ---------------------------------------------------------------------------

void render() {
    switch (currentScreen) {
        case SCREEN_HOME: {
            std::vector<String> labels;
            for (int i = 0; i < kHomeMenuCount; i++) labels.push_back(kHomeMenu[i].label);
            drawListScreen("Stack Wallet", labels, homeState, "", "UP/DOWN move  SELECT open");
            break;
        }
        case SCREEN_CARDS:
            drawListScreen("Loyalty Cards", namesOf(Storage::cards()), cardsState,
                            "No cards on SD card. See docs/CONTENT.md.",
                            "UP/DOWN move  SELECT open  BOOT back");
            break;
        case SCREEN_CARD_VIEW:
            showImage("Loyalty Card", Storage::cards()[cardsState.selected].path);
            break;
        case SCREEN_TICKETS:
            drawListScreen("Flight Tickets", namesOf(Storage::tickets()), ticketsState,
                            "No tickets on SD card. See docs/CONTENT.md.",
                            "UP/DOWN move  SELECT open  BOOT back");
            break;
        case SCREEN_TICKET_VIEW:
            showImage("Flight Ticket", Storage::tickets()[ticketsState.selected].path);
            break;
        case SCREEN_TODO:
            drawTodoScreen();
            break;
        case SCREEN_QR:
            showImage("Receive - DuitNow", Storage::duitnowQrPath());
            break;
        case SCREEN_BOOKS:
            drawListScreen("E-Book Reader", namesOf(Storage::books()), booksState,
                            "No books on SD card. See docs/CONTENT.md.",
                            "UP/DOWN move  SELECT open  BOOT back");
            break;
        case SCREEN_BOOK_READ:
            // Page already rendered by showReaderPage() when this screen was entered
            // or navigated; nothing to redraw here on its own.
            break;
        case SCREEN_SETTINGS:
            drawSettingsScreen();
            break;
    }
}

void goHome() {
    currentScreen = SCREEN_HOME;
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
        default:
            break;
    }
}

void handleDown() {
    switch (currentScreen) {
        case SCREEN_HOME:
            moveSelection(homeState, 1, kHomeMenuCount);
            dirty = true;
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
            drawSettingsScreen(); // show "checking..." implicitly via serial; screen updates after
            OTA::checkAndApplyNow();
            dirty = true; // reflects updated lastCheckStatus() (no-op if OTA rebooted already)
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
            if (readerFile) readerFile.close();
            currentScreen = SCREEN_BOOKS;
            dirty = true;
            break;
        default:
            goHome();
            break;
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
}

void loop() {
    if (btnUp.pressed()) handleUp();
    if (btnDown.pressed()) handleDown();
    if (btnSelect.pressed()) handleSelect();
    if (btnBack.pressed()) handleBack();

    if (dirty) {
        render();
        dirty = false;
    }
}

} // namespace UI
