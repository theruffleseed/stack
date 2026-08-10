#include "display.h"
#include "EPD_3in97.h"
#include "GUI_Paint.h"

namespace {
UBYTE *frameBuffer = nullptr;
UDOUBLE frameBufferSize = 0;
} // namespace

namespace Display {

void begin() {
    DEV_Module_Init();
    EPD_3IN97_Init();

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

    Paint_NewImage(frameBuffer, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT, ROTATE_0, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(frameBuffer);
    Paint_Clear(WHITE);
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

void endFrame(bool fast) {
    if (fast) {
        EPD_3IN97_Init_Fast();
        EPD_3IN97_Display_Fast(frameBuffer);
    } else {
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
    return EPD_3IN97_WIDTH;
}

uint16_t height() {
    return EPD_3IN97_HEIGHT;
}

} // namespace Display
