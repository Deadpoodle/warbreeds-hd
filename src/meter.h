#pragma once
// [Debug] GlobeMeter=1: on the 640x480 main menu, counts presented frames and
// how often the spinning globe's pixels change, and logs both every 2 s. Used
// to compare animation speed between presentation paths.
#include <stdint.h>

namespace meter {
void Enable(void (*log)(const char*, ...));
bool Enabled();
// One presented 8-bit frame.
void Frame(const uint8_t* bits, int w, int h, int pitch, bool bottomUp);
}  // namespace meter
