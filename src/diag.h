#pragma once
#include <string>

namespace diag {
void Init(const std::string& dir, void (*log)(const char*, ...));
void DumpWidgets();  // F10
void SaveFrame();    // F11
void SaveFrameAs(const char* fileName);  // current frame, in the game folder
void InstallDrawCallerLog(int (*widgetDepth)());  // [Debug] DrawCallers
void TraceWrites(uintptr_t addr);  // hardware write breakpoint on the calling thread
// Unhandled exceptions: logged (code, module+offset, stack) and WarBreedsHD-crash.dmp.
// Installed at start-up and again when multiplayer starts (DirectPlay may set its own).
void InstallCrashLog();
}  // namespace diag
