#pragma once
#include <Arduino.h>

// Thin wrapper around the vendored Waveshare EPD_3in97 + GUI_Paint driver.
// Screens draw directly with the Paint_* API from GUI_Paint.h; this module
// only owns the frame buffer lifecycle and the panel refresh calls.
namespace Display {

// Initializes SPI/GPIO for the panel and allocates the 1bpp frame buffer.
// Must be called once from setup() before any Paint_* call.
void begin();

// Clears the panel to white with a full refresh. Slow (~1-2s); use at boot
// or when leaving a badly-ghosted state, not on every navigation step.
void fullClear();

// Selects the frame buffer for drawing and clears it to white in RAM only
// (does not touch the panel). Call before drawing a new screen.
void beginFrame();

// Pushes the frame buffer to the panel. `fast` uses the panel's quick LUT
// (a couple hundred ms, minor ghosting); pass false for a clean full-quality
// refresh (~1-2s) after several fast refreshes to clear ghosting.
void endFrame(bool fast = true);

// Partial refresh of a sub-rectangle of the current frame buffer content.
// Much faster than endFrame(); used for small updates like a todo checkbox.
void partialUpdate(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

// Puts the panel into deep sleep (near-zero current draw). The image
// persists on an e-paper panel while asleep. Call EPD_3IN97_Init() again
// (implicitly done by beginFrame() family) before drawing after waking.
void sleep();

uint16_t width();
uint16_t height();

} // namespace Display
