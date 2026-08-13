#include "display.h"
#include "config.h"
#include "EPD_3in97.h"
#include "GUI_Paint.h"

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
bool syncOldPending = false;
unsigned long refreshKickMs = 0;
// Full refreshes take ~3.5s; anything past 8s means BUSY is stuck (wiring,
// power, or a crashed controller) and the UI must recover rather than hang.
const unsigned long kRefreshTimeoutMs = 8000;
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
            syncOldPending = true; // service() re-syncs 0x26 once done
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

    // Full-frame partial refresh. The SSD1677 on this panel has reversed
    // gate lines, so the driver always pushes the whole frame with the RAM Y
    // window declared gate-reversed (see EPD_3IN97_Display_Partial); the
    // rectangle above is validated only to reject degenerate calls. The
    // differential waveform needs 0x26 synced once the refresh completes.
    if (EPD_3IN97_Display_Partial(frameBuffer, 0, 0, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT)) {
        refreshInFlight = true;
        refreshKickMs = millis();
        syncOldPending = true;
    }
    panelState = PanelState::Unknown; // partial path resets the controller
}

bool busy() {
    return refreshInFlight;
}

void service() {
    if (!refreshInFlight && !syncOldPending) return;

    if (refreshInFlight && EPD_3IN97_IsBusy()) {
        if (millis() - refreshKickMs < kRefreshTimeoutMs) return; // still refreshing
        // BUSY stuck high: abandon this refresh so the UI recovers. Skip the
        // baseline sync - what is on the panel is now unknown.
        Serial.println("Display::service: refresh timeout, BUSY stuck high?");
        syncOldPending = false;
        refreshInFlight = false;
        panelState = PanelState::Unknown; // force a full re-init next frame
        return;
    }

    // The refresh (if any) is done; bring the differential baseline (0x26)
    // up to date with what is actually on the panel now. One full-frame sync
    // covers both fast refreshes and full-frame partial refreshes.
    if (syncOldPending) {
        EPD_3IN97_SyncOldRam(frameBuffer);
        syncOldPending = false;
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
