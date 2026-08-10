#pragma once

// Button-driven screen state machine: home menu -> loyalty cards, flight
// tickets, to-do list, DuitNow receive QR, e-book reader, settings.
// Draws with the vendored GUI_Paint API via display.h.
namespace UI {

// Call once from setup(), after Display::begin() and Storage::begin().
void begin();

// Call every loop() iteration. Cheap when nothing changed - only touches
// the panel when a button press changes what should be on screen.
void loop();

} // namespace UI
