#pragma once
#include <windows.h>
#include <stdint.h>

namespace portprobe {
void Install(void (*log)(const char*, ...), const uint8_t* (*frameBits)(), HDC (*frameDC)());
void OnWidgetDraw(void* widget, bool firstThisFrame);
void OnWidgetDrawDone();
void OnPresent();
}  // namespace portprobe
