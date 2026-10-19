#pragma once
#include <windows.h>

namespace camera {
struct Settings {
    bool enabled = true;  // smooth arrow-key scrolling
    bool wasd = false;    // also scroll with W/A/S/D
    double speed = 1.0;   // screen widths per second
};
void Init(const Settings& s, void (*log)(const char*, ...));
// Engine fix: a frame as large as the map no longer halves the mission view (see camera.cpp).
bool InstallViewFix();
// A resize keeps the view's centre (a mission or a load starts centred at HD sizes).
bool InstallKeepCentre();
void Configure(const Settings& s);  // change settings live
bool OwnsKey(WPARAM vk);                 // swallow this key's WM_KEYDOWN/UP in missions
void Tick(void* sgame, HWND gameWindow);  // every frame tick; sgame = null outside missions
// Debug autopilot: report a key (e.g. VK_CONTROL) as held to the game's
// GetKeyboardState, so posted key messages can carry modifiers.
void ForceHeld(int vk, bool held);
}  // namespace camera
