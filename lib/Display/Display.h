// OLED status screen for both nodes, set by `display` / `display_v2` in the [loracamera]
// section of platformio.ini. With the display off these calls compile to nothing.
#pragma once

#include <Arduino.h>

#ifndef LORACAMERA_DISPLAY
#define LORACAMERA_DISPLAY 0
#endif
static_assert(LORACAMERA_DISPLAY == 0 || LORACAMERA_DISPLAY == 1, "display must be 0 or 1");

#if LORACAMERA_DISPLAY
// Reset and start the OLED. Call with Vext on (vextOn()).
void displayBegin();

// Clear the screen and show up to four lines of text (nullptr = empty line).
void displayShow(const char *l1, const char *l2 = nullptr, const char *l3 = nullptr,
                 const char *l4 = nullptr);
#else
inline void displayBegin() {}
inline void displayShow(const char *, const char * = nullptr, const char * = nullptr,
                        const char * = nullptr) {}
#endif
