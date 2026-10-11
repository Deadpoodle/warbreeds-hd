#pragma once
#include <string>

namespace diag {
void Init(const std::string& dir, void (*log)(const char*, ...));
void DumpWidgets();  // F10
void SaveFrame();    // F11
void SaveFrameAs(const char* fileName);  // current frame, in the game folder
void InstallDrawCallerLog(int (*widgetDepth)());  // [Debug] DrawCallers
void TraceWrites(uintptr_t addr);  // hardware write breakpoint on the calling thread
}  // namespace diag
