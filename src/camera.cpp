// Smooth keyboard camera scrolling. The game moves the camera in large jumps
// on arrow keys; we take those keys over in missions and glide the camera at a
// constant speed instead, like mouse edge-scrolling.
#include "camera.h"

#include <windows.h>
#include <math.h>

#include <initializer_list>

#include "game.h"

namespace camera {
namespace {

Settings g_s;
void (*g_log)(const char*, ...);
double g_fracX, g_fracY;  // sub-pixel carry
LARGE_INTEGER g_last, g_freq;
uint32_t g_ticks;
DWORD g_tickWindow;

typedef void(__thiscall* Clamp_t)(void* sgame);

BYTE g_forced[256];  // debug: keys reported as held
bool Down(int vk) { return g_forced[vk & 0xff] || (GetAsyncKeyState(vk) & 0x8000) != 0; }

decltype(&GetKeyboardState) o_GetKeyboardState;

BOOL WINAPI h_GetKeyboardState(PBYTE keys) {
    if (!o_GetKeyboardState(keys)) return FALSE;
    for (int vk = 0; vk < 256; ++vk)
        if (g_forced[vk]) keys[vk] |= 0x80;
    if (g_s.enabled) {
        for (int vk : {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN}) keys[vk] = 0;
        if (g_s.wasd)
            for (int vk : {'W', 'A', 'S', 'D'}) keys[vk] = 0;
    }
    return TRUE;
}

}  // namespace

void Init(const Settings& s, void (*log)(const char*, ...)) {
    g_s = s;
    g_log = log;
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_last);
    {  // always hooked: it only acts when smooth keys are enabled (Configure can turn them on)
        DWORD old;
        auto slot = (decltype(&GetKeyboardState)*)0x595a28;  // exe IAT
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        o_GetKeyboardState = *slot;
        *slot = &h_GetKeyboardState;
        VirtualProtect(slot, 4, old, &old);
    }
}

void Configure(const Settings& s) { g_s = s; }

void ForceHeld(int vk, bool held) {
    if (vk > 0 && vk < 256) g_forced[vk] = held;
}

bool OwnsKey(WPARAM vk) {
    if (!g_s.enabled) return false;
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN) return true;
    return g_s.wasd && (vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D');
}

void Tick(void* sgame, HWND gameWindow) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double dt = (double)(now.QuadPart - g_last.QuadPart) / g_freq.QuadPart;
    g_last = now;
    if (dt > 0.1) dt = 0.1;  // after a stall, don't leap across the map

    // Log the tick rate once, for tuning.
    if (++g_ticks == 1) g_tickWindow = GetTickCount();
    if (GetTickCount() - g_tickWindow >= 5000 && g_ticks > 1) {
        static bool logged;
        if (!logged) {
            logged = true;
            g_log("camera: frame tick runs at %.1f Hz", g_ticks * 1000.0 / (GetTickCount() - g_tickWindow));
        }
    }

    // Keys held by the debug autopilot count even though its window is hidden.
    bool forced = g_forced[VK_LEFT] || g_forced[VK_RIGHT] || g_forced[VK_UP] || g_forced[VK_DOWN];
    if (!g_s.enabled || !sgame || (GetForegroundWindow() != gameWindow && !forced)) {
        g_fracX = g_fracY = 0;
        return;
    }
    int dx = 0, dy = 0;
    if (Down(VK_LEFT) || (g_s.wasd && Down('A'))) dx--;
    if (Down(VK_RIGHT) || (g_s.wasd && Down('D'))) dx++;
    if (Down(VK_UP) || (g_s.wasd && Down('W'))) dy--;
    if (Down(VK_DOWN) || (g_s.wasd && Down('S'))) dy++;
    if (!dx && !dy) {
        g_fracX = g_fracY = 0;
        return;
    }
    // Speed is in screen-widths per second so it feels the same at any zoom.
    char* g = (char*)sgame;
    short viewW = *(short*)(g + game::SGameViewW);
    double len = (dx && dy) ? 0.70710678118654752 : 1.0;
    double step = g_s.speed * viewW * dt * len;
    g_fracX += dx * step;
    g_fracY += dy * step;
    int mx = (int)g_fracX, my = (int)g_fracY;
    g_fracX -= mx;
    g_fracY -= my;
    if (!mx && !my) return;
    *(short*)(g + game::SGameCameraX) += (short)mx;
    *(short*)(g + game::SGameCameraY) += (short)my;
    ((Clamp_t)game::SGameClampCamera)(g);
}

}  // namespace camera
