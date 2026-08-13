#pragma once
#include <Arduino.h>

// Thin wrapper around the vendored Waveshare EPD_3in97 + GUI_Paint driver.
// Screens draw directly with the Paint_* API from GUI_Paint.h; this module
// owns the frame buffer lifecycle, the panel refresh calls, and the
// non-blocking refresh pipeline.
//
// ASYNC CONTRACT: endFrame()/partialUpdate() only KICK a refresh - they
// return once the panel asserts BUSY (a few ms), and the physical refresh
// (0.6s partial .. ~3s full) completes in the background. Callers must:
//   1. call Display::service() every loop iteration (finishes bookkeeping
//      when a refresh completes),
//   2. never call endFrame()/partialUpdate() while Display::busy() is true
//      (both degrade to a blocking waitIdle() as a safety net),
//   3. call Display::waitIdle() before anything that must see a settled
//      panel (sleep, flash ops).
// Drawing into the frame buffer with Paint_* while a refresh runs is SAFE:
// the pixel data is already in the panel's own RAM before BUSY asserts.
namespace Display {

// Initializes SPI/GPIO for the panel and allocates the 1bpp frame buffer.
// Must be called once from setup() before any Paint_* call.
void begin();

// Clears the panel to white with a full refresh (blocking). Slow; use at
// boot or when leaving a badly-ghosted state, not on navigation steps.
void fullClear();

// Selects the frame buffer for drawing and clears it to white in RAM only
// (does not touch the panel). Call before drawing a new screen.
void beginFrame();

// Selects the frame buffer for drawing WITHOUT clearing it, so a small
// region can be redrawn in RAM and pushed with partialUpdate(). Use only
// after beginFrame() + endFrame() have left the buffer holding the current
// screen; draw only inside the region you will update.
void beginPartialDraw();

// Pushes the frame buffer to the panel (async). `fast` uses the panel's
// quick LUT (~1.5s, minor ghosting); pass false for a clean full-quality
// refresh (~3s) after several fast refreshes to clear ghosting. Every
// FAST_REFRESHES_BEFORE_CLEAN consecutive fast refreshes, a clean refresh
// is forced automatically. Redundant mode re-inits are skipped: repeat fast
// refreshes cost no extra reset cycles.
void endFrame(bool fast = true);

// Partial (differential-waveform) refresh of a sub-rectangle of the current
// frame buffer content, in logical (post-rotation/mirror) coordinates,
// inclusive. ~0.6s and flash-free; used for selection moves, checkbox
// toggles, etc. Implemented as a full-frame differential refresh sharing the
// RAM convention of the 0x26 baseline sync (see EPD_3IN97_Display_Partial);
// the rectangle is only validated, the whole frame is pushed.
void partialUpdate(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

// True while a kicked refresh is still physically running on the panel.
bool busy();

// Drives the async pipeline: when a refresh completes, re-syncs the panel's
// "old image" RAM so the next differential partial update has the right
// baseline. Must be called every loop iteration.
void service();

// Blocks until any in-flight refresh completes (including the sync writes).
void waitIdle();

// Puts the panel into deep sleep (near-zero current draw). The image
// persists on an e-paper panel while asleep. Waits for any in-flight
// refresh first; the next endFrame() re-initializes and wakes the panel.
void sleep();

// Logical drawable width/height, i.e. after DISPLAY_ROTATE (config.h) is
// applied - 480x800 in the default portrait orientation, not the panel's
// raw 800x480 memory layout. All screen layout code should use these
// rather than the EPD_3IN97_WIDTH/HEIGHT constants directly.
uint16_t width();
uint16_t height();

} // namespace Display
