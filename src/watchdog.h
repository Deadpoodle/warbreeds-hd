#pragma once
#include <windows.h>

namespace watchdog {
void Start(void (*log)(const char*, ...), HWND (*window)());  // from DllMain (main thread)
void Beat();                                // call regularly from the main thread
}  // namespace watchdog
