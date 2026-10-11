#pragma once
// Image files (PNG, JPEG, BMP, ...) through the Windows Imaging Component.
#include <stdint.h>

#include <string>
#include <vector>

namespace render {
// Loads `path` as top-down 32-bit BGRA pixels. False if missing or unreadable.
bool LoadImageBGRA(const std::string& path, int& w, int& h, std::vector<uint32_t>& pixels);
}  // namespace render
