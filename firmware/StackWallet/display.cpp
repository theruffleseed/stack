#include "display.h"
#include "config.h"
#include "EPD_3in97.h"
#include "GUI_Paint.h"
#include <string.h> // memcpy (partial window packing)

namespace {
UBYTE *frameBuffer = nullptr;
UDOUBLE frameBufferSize = 0;
int fastRefreshesSinceClean = 0;
// Most navigation now uses partial refreshes, which barely ghost; the
// periodic clean full refresh is only a backstop, not an every-5-presses
// interruption.
const int kFastRefreshesBeforeClean = 10;

// Panel mode tracking: the inits (full/fast) include a hardware reset plus
// several busy-waits (~150ms+), so they're only re-run when the mode
// actually changes or the panel has to be woken. The partial-update path
// resets the controller itself and leaves the mode Unknown.
enum class PanelState { Unknown, Full, Fast, Asleep };
PanelState panelState = PanelState::Unknown;

// Async refresh bookkeeping. refreshInFlight is set when a refresh is
// kicked and cleared by service() once BUSY drops. The sync requests defer
// the "old image" (0x26) RAM re-write until the refresh has completed -
// writing it earlier would corrupt the differential baseline mid-refresh.
bool refreshInFlight = false;
bool syncFullPending = false;
unsigned long refreshKickMs = 0;
// Full refreshes take ~3.5s; anything past 8s means BUSY is stuck (wiring,
// power, or a crashed controller) and the UI must recover rather than hang.
const unsigned long kRefreshTimeoutMs = 8000;
struct PartialSync {
    bool pending;
    UBYTE *buf;
    UWORD x, y, w, h; // panel-space, x 8-aligned, half-open
};
PartialSync partialSync = {false, nullptr, 0, 0, 0, 0};
} // namespace

namespace Display {

void begin() {
    Serial.println("Display::begin: DEV_Module_Init...");
    DEV_Module_Init();
    Serial.println("Display::begin: EPD_3IN97_Init...");
    EPD_3IN97_Init();
    panelState = PanelState::Full;
    Serial.println("Display::begin: EPD_3IN97_Init done");

    frameBufferSize = ((EPD_3IN97_WIDTH % 8 == 0) ? (EPD_3IN97_WIDTH / 8)
                                                   : (EPD_3IN97_WIDTH / 8 + 1)) *
                       EPD_3IN97_HEIGHT;
    frameBuffer = (UBYTE *)malloc(frameBufferSize);
    if (frameBuffer == nullptr) {
        Serial.println("Display::begin: failed to allocate frame buffer");
        while (true) {
            delay(1000);
        }
    }

    Paint_NewImage(frameBuffer, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT, DISPLAY_ROTATE, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(frameBuffer);
    Paint_SetMirroring(DISPLAY_MIRROR);
    Paint_Clear(WHITE);
    Serial.println("Display::begin: done");
}

void fullClear() {
    waitIdle();
    EPD_3IN97_Init();
    EPD_3IN97_Clear(); // blocking; boot path only
    panelState = PanelState::Full;
}

void beginFrame() {
    // Pure frame-buffer housekeeping - no panel I/O. The panel is only ever
    // touched by endFrame()/partialUpdate(), which run the init their mode
    // needs. (The stock code re-ran the full panel init on every frame:
    // ~150-400ms of resets and busy-waits wasted per redraw.)
    Paint_SelectImage(frameBuffer);
    Paint_Clear(WHITE);
}

void beginPartialDraw() {
    Paint_SelectImage(frameBuffer);
}

void endFrame(bool fast) {
    if (busy()) {
        // The UI pipeline normally guarantees no kick while busy; blocking
        // here beats silently dropping the frame and leaving the screen out
        // of sync with the UI state.
        waitIdle();
    }
    if (fast && ++fastRefreshesSinceClean >= kFastRefreshesBeforeClean) {
        fast = false; // enough quick updates; clear the ghosting with a clean refresh
    }
    if (fast) {
        if (panelState != PanelState::Fast) {
            EPD_3IN97_Init_Fast(); // includes reset; also recovers from Asleep
            panelState = PanelState::Fast;
        }
        if (EPD_3IN97_Display_Fast(frameBuffer)) {
            refreshInFlight = true;
            refreshKickMs = millis();
            syncFullPending = true; // service() re-syncs 0x26 once done
        }
    } else {
        fastRefreshesSinceClean = 0;
        EPD_3IN97_Init();
        panelState = PanelState::Full;
        if (EPD_3IN97_Display_Base(frameBuffer)) { // writes 0x24 AND 0x26
            refreshInFlight = true;
            refreshKickMs = millis();
        }
    }
}

void partialUpdate(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    if (busy()) waitIdle(); // see endFrame()

    const int W = width();
    const int H = height();
    if (x0 >= W || y0 >= H) return;
    if (x1 >= W) x1 = W - 1;
    if (y1 >= H) y1 = H - 1;
    if (x1 <= x0 || y1 <= y0) return;

    // The caller passes logical (post-rotation/mirror) coordinates. Convert
    // them to panel-native (800x480) to match the raw framebuffer layout and
    // what EPD_3IN97_Display_Partial expects.
    UWORD px0i, py0i, px1i, py1i; // inclusive
    if (DISPLAY_ROTATE == 90 && DISPLAY_MIRROR == 0x03) {
        px0i = y0;
        py0i = EPD_3IN97_HEIGHT - x1 - 1;
        px1i = y1;
        py1i = EPD_3IN97_HEIGHT - x0 - 1;
    } else if (DISPLAY_ROTATE == 90) {
        px0i = EPD_3IN97_WIDTH - y1 - 1;
        py0i = x0;
        px1i = EPD_3IN97_WIDTH - y0 - 1;
        py1i = x1;
    } else {
        px0i = x0;
        py0i = y0;
        px1i = x1;
        py1i = y1;
    }

    // The controller's RAM window is byte-granular in X: align outward to
    // 8-pixel boundaries (half-open end) so window and data agree exactly.
    UWORD px0 = px0i & ~7;
    UWORD px1 = (UWORD)((px1i + 8) & ~7); // inclusive -> half-open, aligned up
    if (px1 > EPD_3IN97_WIDTH) px1 = EPD_3IN97_WIDTH;
    UWORD py0 = py0i;
    UWORD py1 = py1i + 1; // half-open
    if (px1 <= px0 || py1 <= py0) return;

    const UWORD rowBytes = (EPD_3IN97_WIDTH + 7) / 8;
    const UWORD wBytes = (px1 - px0) / 8;
    const UDOUBLE h = py1 - py0;

    // Pack the window into a contiguous buffer: the driver sends the window
    // as one sequential block, so its row stride must equal the window
    // width, not the full-frame stride.
    UBYTE *buf = (UBYTE *)malloc((UDOUBLE)wBytes * h);
    if (buf == nullptr) {
        Serial.println("Display::partialUpdate: window buffer alloc failed, skipping");
        return; // next full redraw recovers
    }
    for (UDOUBLE r = 0; r < h; r++) {
        memcpy(buf + r * wBytes, frameBuffer + (py0 + r) * rowBytes + px0 / 8, wBytes);
    }

    UBYTE kicked = EPD_3IN97_Display_Partial(buf, px0, py0, px1, py1);
    panelState = PanelState::Unknown; // partial path resets the controller
    if (kicked) {
        refreshInFlight = true;
        refreshKickMs = millis();
        // Keep the buffer alive until service() has re-synced 0x26 with it.
        partialSync.pending = true;
        partialSync.buf = buf;
        partialSync.x = px0;
        partialSync.y = py0;
        partialSync.w = px1 - px0;
        partialSync.h = (UWORD)h;
    } else {
        free(buf);
    }
}

bool busy() {
    return refreshInFlight;
}

void service() {
    if (!refreshInFlight && !syncFullPending && !partialSync.pending) return;

    if (refreshInFlight && EPD_3IN97_IsBusy()) {
        if (millis() - refreshKickMs < kRefreshTimeoutMs) return; // still refreshing
        // BUSY stuck high: abandon this refresh so the UI recovers. Skip the
        // baseline sync - what is on the panel is now unknown.
        Serial.println("Display::service: refresh timeout, BUSY stuck high?");
        if (partialSync.pending) {
            free(partialSync.buf);
            partialSync.buf = nullptr;
            partialSync.pending = false;
        }
        syncFullPending = false;
        refreshInFlight = false;
        panelState = PanelState::Unknown; // force a full re-init next frame
        return;
    }

    // The refresh (if any) is done; bring the differential baseline (0x26)
    // up to date with what is actually on the panel now.
    if (partialSync.pending) {
        EPD_3IN97_SyncOldRamWindow(partialSync.buf, partialSync.x, partialSync.y,
                                   partialSync.x + partialSync.w,
                                   partialSync.y + partialSync.h);
        free(partialSync.buf);
        partialSync.buf = nullptr;
        partialSync.pending = false;
    }
    if (syncFullPending) {
        EPD_3IN97_SyncOldRam(frameBuffer);
        syncFullPending = false;
    }
    refreshInFlight = false;
}

void waitIdle() {
    while (refreshInFlight) {
        service();
        delay(1);
    }
    service(); // drain any pending sync bookkeeping
}

void sleep() {
    waitIdle();
    EPD_3IN97_Sleep();
    panelState = PanelState::Asleep;
}

uint16_t width() {
    return (DISPLAY_ROTATE == 90 || DISPLAY_ROTATE == 270) ? EPD_3IN97_HEIGHT : EPD_3IN97_WIDTH;
}

uint16_t height() {
    return (DISPLAY_ROTATE == 90 || DISPLAY_ROTATE == 270) ? EPD_3IN97_WIDTH : EPD_3IN97_HEIGHT;
}

} // namespace Display
