#pragma once
#include <windows.h>

namespace gditrace {
void Install(void (*log)(const char*, ...), HWND (*window)());
}
