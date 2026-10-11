#pragma once
#include <string>

namespace scan {
void Open(const std::string& path);
void Tick(void* missionScene);  // call every frame; records when missionScene != nullptr
}  // namespace scan
