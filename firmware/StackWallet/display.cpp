#include "display.h"
#include "config.h"
#include "EPD_3in97.h"
#include "GUI_Paint.h"
#include <string.h>

namespace {
UBYTE *frameBuffer = nullptr;
UBYTE *prevFrame = nullptr;
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

    prevFrame = (UBYTE *)malloc(frameBufferSize);
    if (prevFrame == nullptr) {
        Serial.println("Display::begin: failed to allocate previous-frame buffer");
        while (true) {
            delay(1000);
        }
    }
    memset(prevFrame, 0xFF, frameBufferSize); // baseline starts as a white screen

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
    // Whatever was just pushed is now what the screen shows; keep it as the
    // baseline for the next differential partial refresh.
    memcpy(prevFrame, frameBuffer, frameBufferSize);
}

void partialUpdate(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // The caller passes logical (post-rotation/mirror) coordinates. Convert
    // them to panel-native (800x480) to match the raw framebuffer layout and
    // what EPD_3IN97_Display_Partial expects.
    uint16_t px0, py0, px1, py1;
    if (DISPLAY_ROTATE == 90 && DISPLAY_MIRROR == 0x03) {
        px0 = y0;
        py0 = EPD_3IN97_HEIGHT - x1 - 1;
        px1 = y1;
        py1 = EPD_3IN97_HEIGHT - x0 - 1;
    } else if (DISPLAY_ROTATE == 90) {
        px0 = EPD_3IN97_WIDTH - y1 - 1;
        py0 = x0;
        px1 = EPD_3IN97_WIDTH - y0 - 1;
        py1 = x1;
    } else {
        px0 = x0;
        py0 = y0;
        px1 = x1;
        py1 = y1;
    }

    // EPD_3IN97_Display_Partial sends a sequential block from the base of
    // the Image pointer. Offset into the framebuffer so the first byte
    // corresponds to the window's top-left pixel.
    uint16_t rowBytes = (EPD_3IN97_WIDTH + 7) / 8;
    const UBYTE *region = frameBuffer + (uint32_t)py0 * rowBytes + px0 / 8;
    EPD_3IN97_Display_Partial(region, px0, py0, px1, py1);
}

void partialFullFrame() {
    // Differential full-frame refresh: baseline (RAM 0x26) is written from
    // the last pushed frame, the new frame to 0x24, then the panel is driven
    // with GxEPD2's partial LUT - changed pixels only, no full-screen flash.
    EPD_3IN97_DisplayPartial_Diff(prevFrame, frameBuffer);
    memcpy(prevFrame, frameBuffer, frameBufferSize);
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
