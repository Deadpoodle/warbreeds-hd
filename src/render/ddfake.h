#pragma once
#include <windows.h>

namespace render {
// Hook the exe's LoadLibraryA/GetProcAddress/FreeLibrary imports so that
// DDRAW.DLL resolves to our fake DirectDraw instead of the real one.
bool InstallFakeDirectDraw(HMODULE self, void (*log)(const char*, ...));
}
