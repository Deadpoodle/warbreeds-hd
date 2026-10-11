#include "meter.h"

#include <windows.h>

namespace meter {
namespace {

void (*g_log)(const char*, ...);
// The globe on SHeadquarters (640x480).
constexpr int kX0 = 460, kY0 = 245, kX1 = 590, kY1 = 375;
uint32_t g_lastHash;
int g_frames, g_changes;
DWORD g_start;

}  // namespace

void Enable(void (*log)(const char*, ...)) { g_log = log; }
bool Enabled() { return g_log != nullptr; }

void Frame(const uint8_t* bits, int w, int h, int pitch, bool bottomUp) {
    if (!g_log || !bits || w != 640 || h != 480) return;
    uint32_t hash = 2166136261u;
    for (int y = kY0; y < kY1; ++y) {
        const uint8_t* row = bits + (size_t)(bottomUp ? h - 1 - y : y) * pitch;
        for (int x = kX0; x < kX1; ++x) hash = (hash ^ row[x]) * 16777619u;
    }
    DWORD now = GetTickCount();
    if (!g_start) g_start = now;
    ++g_frames;
    if (hash != g_lastHash) ++g_changes;
    g_lastHash = hash;
    if (now - g_start >= 2000) {
        double s = (now - g_start) / 1000.0;
        g_log("globe: %.1f frames/s, %.1f globe changes/s", g_frames / s, g_changes / s);
        g_frames = g_changes = 0;
        g_start = now;
    }
}

}  // namespace meter
