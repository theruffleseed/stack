#include "display.h"
#include "config.h"
#include "EPD_3in97.h"
#include "GUI_Paint.h"

namespace {
UBYTE *frameBuffer = nullptr;
UDOUBLE frameBufferSize = 0;
int fastRefreshesSinceClean = 0;
const int kFastRefreshesBeforeClean = 5;
} // namespace

namespace Display {

void begin() {
    Serial.println("Display::begin: DEV_Module_Init...");
    DEV_Module_Init();
    Serial.println("Display::begin: EPD_3IN97_Init...");
    EPD_3IN97_Init();
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
    EPD_3IN97_Init();
    EPD_3IN97_Clear();
}

void beginFrame() {
    EPD_3IN97_Init();
    Paint_SelectImage(frameBuffer);
    Paint_Clear(WHITE);
}

void beginPartialDraw() {
    Paint_SelectImage(frameBuffer);
}

void endFrame(bool fast) {
    if (fast && ++fastRefreshesSinceClean >= kFastRefreshesBeforeClean) {
        fast = false; // enough quick updates; clear the ghosting with a clean refresh
    }
    if (fast) {
        EPD_3IN97_Init_Fast();
        EPD_3IN97_Display_Fast(frameBuffer);
    } else {
        fastRefreshesSinceClean = 0;
        EPD_3IN97_Init();
        EPD_3IN97_Display_Base(frameBuffer);
    }
}

void partialUpdate(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    EPD_3IN97_Display_Partial(frameBuffer, x0, y0, x1, y1);
}

void sleep() {
    EPD_3IN97_Sleep();
}

uint16_t width() {
    return (DISPLAY_ROTATE == 90 || DISPLAY_ROTATE == 270) ? EPD_3IN97_HEIGHT : EPD_3IN97_WIDTH;
}

uint16_t height() {
    return (DISPLAY_ROTATE == 90 || DISPLAY_ROTATE == 270) ? EPD_3IN97_WIDTH : EPD_3IN97_HEIGHT;
}

} // namespace Display
